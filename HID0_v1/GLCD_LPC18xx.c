/*-----------------------------------------------------------------------------
 * Name:    GLCD_LPC18xx.c
 * Purpose: LPC18xx low level Graphic LCD (240x320 pixels) using
 *          SPI and LCD controller in RGB mode
 * Note(s):
 *-----------------------------------------------------------------------------
 * This file is part of the uVision/ARM development tools.
 * This software may only be used under the terms of a valid, current,
 * end user licence from KEIL for a compatible version of KEIL software
 * development tools. Nothing else gives you the right to use this software.
 *
 * This software is supplied "AS IS" without warranties of any kind.
 *
 * Copyright (c) 2005-2013 KEIL - An ARM Company. All rights reserved.
 *----------------------------------------------------------------------------*/

#include <LPC18xx.h>                    /* LPC18xx Definitions                */
#include "GLCD.h"
#include "Font_6x8_h.h"
#include "Font_16x24_h.h"

#define SDRAM_BASE_ADDR 0x28000000

/************************** Orientation  configuration ************************/

#define LANDSCAPE   1                   /* 1 for landscape, 0 for portrait    */
#define ROTATE180   0                   /* 1 to rotate the screen for 180 deg */

/*********************** Hardware specific configuration **********************/

/* SPI Interface: SSP0
   
   PINS:
   - CS     = PF_1 (GPIO7[16] pin)
   - RS     = GND
   - WR/SCK = PF_0 (SCK0)
   - RD     = GND
   - SDO    = PF_2 (MISO0)
   - SDI    = PF_3 (MOSI0)                                                    */

#define PIN_CS      (1 << 16)

/* SPI_SR - bit definitions                                                   */
#define TFE         0x01
#define RNE         0x04
#define BSY         0x10

/*------------------------- Speed dependant settings -------------------------*/

/* If processor works on high frequency delay has to be increased, it can be 
   increased by factor 2^N by this constant                                   */
#define DELAY_2N    18

/*---------------------- Graphic LCD size definitions ------------------------*/

#if (LANDSCAPE == 1)
#define WIDTH       320                 /* Screen Width  (in pixels)          */
#define HEIGHT      240                 /* Screen Height (in pixels)          */
#else
#define WIDTH       240                 /* Screen Width  (in pixels)          */
#define HEIGHT      320                 /* Screen Height (in pixels)          */
#endif
#define BPP         16                  /* Bits per pixel                     */
#define BYPP        ((BPP+7)/8)         /* Bytes per pixel                    */

#define PHYS_XSZ    240                 /* Physical screen width              */
#define PHYS_YSZ    320                 /* Physical screen height             */
/*--------------- Graphic LCD interface hardware definitions -----------------*/

/* Pin CS setting to 0 or 1                                                   */
#define LCD_CS(x)   ((x) ? (LPC_GPIO_PORT->SET[7] = PIN_CS) : (LPC_GPIO_PORT->CLR[7] = PIN_CS))

#define SPI_START   (0x70)              /* Start byte for SPI transfer        */
#define SPI_RD      (0x01)              /* WR bit 1 within start              */
#define SPI_WR      (0x00)              /* WR bit 0 within start              */
#define SPI_DATA    (0x02)              /* RS bit 1 within start byte         */
#define SPI_INDEX   (0x00)              /* RS bit 0 within start byte         */

#define BG_COLOR  0                     /* Background color                   */
#define TXT_COLOR 1                     /* Text color                         */

/* Clock Control Unit register bits */
#define CCU_CLK_CFG_RUN   (1 << 0)
#define CCU_CLK_CFG_AUTO  (1 << 1)
#define CCU_CLK_STAT_RUN  (1 << 0)

/* SCU pin configuration definitions */
#define SPI_PIN_SET ((1 << 7) | (1 << 6) | (1 << 5))
#define LCD_PIN_SET ((1 << 7) | (1 << 6) | (1 << 5))
#define LCD_NPR_SET ((1 << 5) | (1 << 4))

/*---------------------------- Global variables ------------------------------*/

/******************************************************************************/
static uint16_t *fb  = (uint16_t *)SDRAM_BASE_ADDR;
static uint16_t Color[2] = {White, Black};


/************************ Local auxiliary functions ***************************/

/*******************************************************************************
* Delay in while loop cycles                                                   *
*   Parameter:    cnt:    number of while cycles to delay                      *
*   Return:                                                                    *
*******************************************************************************/

static void delay (int cnt) {
  cnt <<= DELAY_2N;
  while (cnt--);
}


/*******************************************************************************
* Transfer 1 byte over the serial communication                                *
*   Parameter:    byte:   byte to be sent                                      *
*   Return:               byte read while sending                              *
*******************************************************************************/

static __inline unsigned char spi_tran (unsigned char byte) {

  LPC_SSP0->DR = byte;
  while (!(LPC_SSP0->SR & RNE));        /* Wait for send to finish            */
  return (LPC_SSP0->DR);
}


/*******************************************************************************
* Write a command the LCD controller                                           *
*   Parameter:    cmd:    command to be written                                *
*   Return:                                                                    *
*******************************************************************************/

static __inline void wr_cmd (unsigned char cmd) {
  LCD_CS(0);
  spi_tran(SPI_START | SPI_WR | SPI_INDEX);   /* Write : RS = 0, RW = 0       */
  spi_tran(0);
  spi_tran(cmd);
  LCD_CS(1);
}


/*******************************************************************************
* Write data to the LCD controller                                             *
*   Parameter:    dat:    data to be written                                   *
*   Return:                                                                    *
*******************************************************************************/

static __inline void wr_dat (unsigned short dat) {
  LCD_CS(0);
  spi_tran(SPI_START | SPI_WR | SPI_DATA);    /* Write : RS = 1, RW = 0       */
  spi_tran((dat >>   8));                     /* Write D8..D15                */
  spi_tran((dat & 0xFF));                     /* Write D0..D7                 */
  LCD_CS(1);
}


/*******************************************************************************
* Write a value to the to LCD register                                         *
*   Parameter:    reg:    register to be written                               *
*                 val:    value to write to the register                       *
*******************************************************************************/

static __inline void wr_reg (unsigned char reg, unsigned short val) {
  wr_cmd(reg);
  wr_dat(val);
}


/************************ Exported functions **********************************/

/*******************************************************************************
* Initialize the Graphic LCD controller                                        *
*   Parameter:                                                                 *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_Init (void) {
  uint32_t i;

  /* Connect base clock */
  LPC_CGU->BASE_SSP0_CLK = (1    << 11) |
                           (0x09 << 24) ; /* PLL1 is SSP0 clock source        */

  LPC_CGU->BASE_LCD_CLK  = (1    << 11) |
                           (0x10 << 24) ; /* IDIVE is clock source            */

  /* Enable GPIO register interface clock */
  LPC_CCU1->CLK_M3_GPIO_CFG  = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(LPC_CCU1->CLK_M3_GPIO_STAT & CCU_CLK_STAT_RUN));

  /* Enable SSP0 register interface clock */
  LPC_CCU1->CLK_M3_SSP0_CFG  = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(LPC_CCU1->CLK_M3_SSP0_STAT & CCU_CLK_STAT_RUN));

  /* Enable LCD clock */
  LPC_CCU1->CLK_M3_LCD_CFG   = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(LPC_CCU1->CLK_M3_LCD_STAT & CCU_CLK_STAT_RUN));


  /* Configure SSP0 peripheral -----------------------------------------------*/
  LPC_SSP0->CR0  = (1<<7)|(1<<6)|0x7;   /* 8-bit transfer CPHA = 1, CPOL = 1  */
  LPC_SSP0->CPSR = 0x08;                /* SPI Clock = PCLK / (8 + 1)         */
  LPC_SSP0->CR1  = (0<<2)|(1<<1);       /* Enable SSP controller, master mode */

  /* Configure SPI peripheral pins -------------------------------------------*/
  LPC_SCU->SFSPF_0  = SPI_PIN_SET | 0;  /* PF_0: SCL (SSP0 pin)               */
  LPC_SCU->SFSPF_1  = SPI_PIN_SET | 4;  /* PF_1: CS  (GPIO7[16] pin)          */
  LPC_SCU->SFSPF_2  = SPI_PIN_SET | 2;  /* PF_2: SDO (SSP0 pin)               */
  LPC_SCU->SFSPF_3  = SPI_PIN_SET | 2;  /* PF_3: SDI (SSP0 pin)               */

  LPC_GPIO_PORT->DIR[7] |= PIN_CS;      /* Chip select pin is GPIO output     */

  /* Configure LCD controller pins -------------------------------------------*/
  LPC_SCU->SFSP4_1  = LCD_PIN_SET | 5;  /* P4_1:  LCD_VD19                    */
  LPC_SCU->SFSP4_2  = LCD_PIN_SET | 2;  /* P4_2:  LCD_VD3                     */
  LPC_SCU->SFSP4_5  = LCD_PIN_SET | 2;  /* P4_5:  LCD_VSYNC                   */
  LPC_SCU->SFSP4_6  = LCD_PIN_SET | 2;  /* P4_6:  LCD_EN                      */
  LPC_SCU->SFSP4_7  = LCD_PIN_SET | 0;  /* P4_7:  LCD_DOTCLK                  */
  LPC_SCU->SFSP4_9  = LCD_PIN_SET | 2;  /* P4_9:  LCD_VD11                    */
  LPC_SCU->SFSP4_10 = LCD_PIN_SET | 2;  /* P4_10: LCD_VD10                    */

  LPC_SCU->SFSP7_0  = LCD_NPR_SET | 0;  /* P7_0:  LCD_BL_EN                   */
  LPC_SCU->SFSP7_6  = LCD_PIN_SET | 3;  /* P7_6:  LCD_HSYNC                   */

  LPC_SCU->SFSP8_3  = LCD_PIN_SET | 3;  /* P8_3:  LCD_VD12                    */
  LPC_SCU->SFSP8_4  = LCD_PIN_SET | 3;  /* P8_4:  LCD_VD7                     */
  LPC_SCU->SFSP8_5  = LCD_PIN_SET | 3;  /* P8_5:  LCD_VD6                     */
  LPC_SCU->SFSP8_6  = LCD_PIN_SET | 3;  /* P8_6:  LCD_VD5                     */
  LPC_SCU->SFSP8_7  = LCD_PIN_SET | 3;  /* P8_7:  LCD_VD4                     */

  LPC_SCU->SFSPB_0  = LCD_PIN_SET | 2;  /* PB_0:  LCD_VD23                    */
  LPC_SCU->SFSPB_1  = LCD_PIN_SET | 2;  /* PB_1:  LCD_VD22                    */
  LPC_SCU->SFSPB_2  = LCD_PIN_SET | 2;  /* PB_2:  LCD_VD21                    */
  LPC_SCU->SFSPB_3  = LCD_PIN_SET | 2;  /* PB_3:  LCD_VD20                    */
  LPC_SCU->SFSPB_4  = LCD_PIN_SET | 2;  /* PB_4:  LCD_VD15                    */
  LPC_SCU->SFSPB_5  = LCD_PIN_SET | 2;  /* PB_5:  LCD_VD14                    */
  LPC_SCU->SFSPB_6  = LCD_PIN_SET | 2;  /* PB_6:  LCD_VD13                    */


  /* LCD with HX8347-D LCD Controller                                         */
  /* Driving ability settings ------------------------------------------------*/
  wr_reg(0xEA, 0x00);                   /* Power control internal used (1)    */
  wr_reg(0xEB, 0x20);                   /* Power control internal used (2)    */
  wr_reg(0xEC, 0x0C);                   /* Source control internal used (1)   */
  wr_reg(0xED, 0xC7);                   /* Source control internal used (2)   */
  wr_reg(0xE8, 0x38);                   /* Source output period Normal mode   */
  wr_reg(0xE9, 0x10);                   /* Source output period Idle mode     */
  wr_reg(0xF1, 0x01);                   /* RGB 18-bit interface ;0x0110       */
  wr_reg(0xF2, 0x10);

  /* Adjust the Gamma Curve --------------------------------------------------*/
  wr_reg(0x40, 0x01);
  wr_reg(0x41, 0x00);
  wr_reg(0x42, 0x00);
  wr_reg(0x43, 0x10);
  wr_reg(0x44, 0x0E);
  wr_reg(0x45, 0x24);
  wr_reg(0x46, 0x04);
  wr_reg(0x47, 0x50);
  wr_reg(0x48, 0x02);
  wr_reg(0x49, 0x13);
  wr_reg(0x4A, 0x19);
  wr_reg(0x4B, 0x19);
  wr_reg(0x4C, 0x16);

  wr_reg(0x50, 0x1B);
  wr_reg(0x51, 0x31);
  wr_reg(0x52, 0x2F);
  wr_reg(0x53, 0x3F);
  wr_reg(0x54, 0x3F);
  wr_reg(0x55, 0x3E);
  wr_reg(0x56, 0x2F);
  wr_reg(0x57, 0x7B);
  wr_reg(0x58, 0x09);
  wr_reg(0x59, 0x06);
  wr_reg(0x5A, 0x06);
  wr_reg(0x5B, 0x0C);
  wr_reg(0x5C, 0x1D);
  wr_reg(0x5D, 0xCC);

  /* Power voltage setting ---------------------------------------------------*/
  wr_reg(0x1B, 0x1B);
  wr_reg(0x1A, 0x01);
  wr_reg(0x24, 0x2F);
  wr_reg(0x25, 0x57);
  wr_reg(0x23, 0x88);

  /* Power on setting --------------------------------------------------------*/
  wr_reg(0x18, 0x36);                   /* Internal oscillator frequency adj  */
  wr_reg(0x19, 0x01);                   /* Enable internal oscillator         */
  wr_reg(0x01, 0x00);                   /* Normal mode, no scrool             */
  wr_reg(0x1F, 0x88);                   /* Power control 6 - DDVDH Off        */
  delay(20);
  wr_reg(0x1F, 0x82);                   /* Power control 6 - Step-up: 3 x VCI */
  delay(5);
  wr_reg(0x1F, 0x92);                   /* Power control 6 - Step-up: On      */
  delay(5);
  wr_reg(0x1F, 0xD2);                   /* Power control 6 - VCOML active     */
  delay(5);

  /* Color selection ---------------------------------------------------------*/
  wr_reg(0x17, 0x55);                   /* RGB, System interface: 16 Bit/Pixel*/
  wr_reg(0x00, 0x00);                   /* Scrolling off, no standby          */

  /* Interface config --------------------------------------------------------*/
  wr_reg(0x2F, 0x11);                   /* LCD Drive: 1-line inversion        */
  wr_reg(0x31, 0x02);                   /* Value for SPI: 0x02, RBG: 0x02     */
  wr_reg(0x32, 0x00);                   /* DPL=0, HSPL=0, VSPL=0, EPL=0       */

  /* Display on setting ------------------------------------------------------*/
  wr_reg(0x28, 0x38);                   /* PT(0,0) active, VGL/VGL            */
  delay(20);
  wr_reg(0x28, 0x3C);                   /* Display active, VGL/VGL            */

/*                  MY         MX         MV         ML         BGR           */
#if (LANDSCAPE == 1)
 #if (ROTATE180 == 0)
  wr_reg (0x16, ((1 << 7) | (0 << 6) | (0 << 5) | (0 << 4) | (0 << 3)));
 #else
  wr_reg (0x16, ((0 << 7) | (1 << 6) | (0 << 5) | (1 << 4) | (0 << 3)));
 #endif
#else /* Portrait */
 #if (ROTATE180 == 0)
  wr_reg (0x16, ((0 << 7) | (0 << 6) | (0 << 5) | (0 << 4) | (0 << 3)));
 #else
  wr_reg (0x16, ((1 << 7) | (1 << 6) | (0 << 5) | (0 << 4) | (0 << 3)));
 #endif
#endif

  /* Display scrolling settings ----------------------------------------------*/
  wr_reg(0x0E, 0x00);                   /* TFA MSB                            */
  wr_reg(0x0F, 0x00);                   /* TFA LSB                            */
  wr_reg(0x10, 320 >> 8);               /* VSA MSB                            */
  wr_reg(0x11, 320 &  0xFF);            /* VSA LSB                            */
  wr_reg(0x12, 0x00);                   /* BFA MSB                            */
  wr_reg(0x13, 0x00);                   /* BFA LSB                            */


  /* Configure LCD controller ------------------------------------------------*/
  LPC_RGU->RESET_CTRL0 = (1U << 16);
  while ((LPC_RGU->RESET_ACTIVE_STATUS0 & (1U << 16)) == 0);

  LPC_LCD->CTRL &= ~(1 << 0);           /* Disable LCD                        */
  LPC_LCD->INTMSK = 0;                  /* Disable all interrupts             */

  LPC_LCD->UPBASE = SDRAM_BASE_ADDR;

  LPC_LCD->TIMH  = (7    << 24) |       /* Horizontal back porch              */
                   (3    << 16) |       /* Horizontal front porch             */
                   (3    <<  8) |       /* Horizontal sync pulse width        */
                   (14   <<  2) ;       /* Pixels-per-line                    */
  LPC_LCD->TIMV  = (3    << 24) |       /* Vertical back porch                */
                   (2    << 16) |       /* Vertical front porch               */
                   (3    << 10) |       /* Vertical sync pulse width          */
                   (319  <<  0) ;       /* Lines per panel                    */
  LPC_LCD->POL   = (1    << 26) |       /* Bypass pixel clock divider         */
                   (239  << 16) |       /* Clocks per line: num of LCDCLKs    */
                   (1    << 13) |       /* Invert panel clock                 */
                   (1    << 12) |       /* Invert HSYNC                       */
                   (1    << 11) ;       /* Invert VSYNC                       */
  LPC_LCD->LE    = (1    << 16) |       /* LCDLE Enabled: 1, Disabled: 0      */
                   (9    <<  0) ;       /* Line-end delay: LCDCLK clocks - 1  */
  LPC_LCD->CTRL  = (1    << 11) |       /* LCD Power Enable                   */
                   (1    <<  5) |       /* 0 = STN display, 1: TFT display    */
                   (6    <<  1) ;       /* Bits per pixel: 16bpp (5:6:5)      */

  for (i = 0; i < 256; i++) {
    LPC_LCD->PAL[i] = 0;                /* Clear palette                      */
  }
  LPC_LCD->CTRL |= (1 <<  0);           /* LCD enable                         */

  /* Turn on backlight -------------------------------------------------------*/
  LPC_GPIO_PORT->DIR[3] |= (1 << 8);
  LPC_GPIO_PORT->SET[3] =  (1 << 8);
}


/*******************************************************************************
* Draw a pixel in foreground color                                             *
*   Parameter:      x:        horizontal position                              *
*                   y:        vertical position                                *
*   Return:                                                                    *
*******************************************************************************/
void GLCD_PutPixel (unsigned int x, unsigned int y) {
 #if (LANDSCAPE == 0)
  fb[y*PHYS_XSZ + x] = Color[TXT_COLOR];
 #else
  fb[x*PHYS_XSZ + y] = Color[TXT_COLOR];
 #endif
}


/*******************************************************************************
* Set foreground color                                                         *
*   Parameter:      color:    foreground color                                 *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_SetTextColor (unsigned short color) {

  Color[TXT_COLOR] = color;
}


/*******************************************************************************
* Set background color                                                         *
*   Parameter:      color:    background color                                 *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_SetBackColor (unsigned short color) {

  Color[BG_COLOR] = color;
}


/*******************************************************************************
* Clear display                                                                *
*   Parameter:      color:    display clearing color                           *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_Clear (unsigned short color) {
  unsigned int i;

  for (i = 0; i < (WIDTH*HEIGHT); i++) {
    fb[i] = color;
  }
}


/*******************************************************************************
* Draw a line at specified coordinates (x,y) with specified length (len) in    *
* given direction (dir).                                                       *
*   Parameter:      x:        x position                                       *
*                   y:        y position                                       *
*                   len:      length                                           *
*                   dir:      direction                                        *
*   Return:                                                                    *
*******************************************************************************/
void GLCD_DrawLine (unsigned int x, unsigned int y, unsigned int len, unsigned int dir) {
  uint32_t i;

  if (dir == 0) {                       /* Vertical line                      */
    for (i = 0; i < len; i++) { GLCD_PutPixel (x, y + i); }
  }
  else {                                /* Horizontal line                    */
    for (i = 0; i < len; i++) { GLCD_PutPixel (x + i, y); }
  }
}


/*******************************************************************************
* Draw character on given position                                             *
*   Parameter:      x:        horizontal position                              *
*                   y:        vertical position                                *
*                   cw:       character width in pixel                         *
*                   ch:       character height in pixels                       *
*                   c:        pointer to character bitmap                      *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_DrawChar (unsigned int x, unsigned int y, unsigned int cw, unsigned int ch, unsigned char *c) {
  unsigned int i, j, k, pixs;

  k  = (cw + 7)/8;

  if (k == 1) {
    for (j = 0; j < ch; j++) {
      pixs = *(unsigned char *)c;
      c += 1;
    
      for (i = 0; i < cw; i++) {
       #if (LANDSCAPE == 0)
        fb[(y+j)*PHYS_XSZ + (x+i)] = Color[(pixs >> i) & 1];
       #else
        fb[(x+i)*PHYS_XSZ + (y+j)] = Color[(pixs >> i) & 1];
       #endif
      }
    }
  }
  else if (k == 2) {
    for (j = 0; j < ch; j++) {
      pixs = *(unsigned short *)c;
      c += 2;
      
      for (i = 0; i < cw; i++) {
       #if (LANDSCAPE == 0)
        fb[(y+j)*PHYS_XSZ + (x+i)] = Color[(pixs >> i) & 1];
       #else
        fb[(x+i)*PHYS_XSZ + (y+j)] = Color[(pixs >> i) & 1];
       #endif
      }
    }
  }
}


/*******************************************************************************
* Display character on given line                                              *
*   Parameter:      ln:       line number                                      *
*                   col:      column number                                    *
*                   fi:       font index (0 = 6x8, 1 = 16x24)                  *
*                   c:        ascii character                                  *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_DisplayChar (unsigned int ln, unsigned int col, unsigned char fi, unsigned char c) {

  c -= 32;
  switch (fi) {
    case 0:  /* Font 6 x 8 */
      GLCD_DrawChar(col *  6, ln *  8,  6,  8, (unsigned char *)&Font_6x8_h  [c * 8]);
      break;
    case 1:  /* Font 16 x 24 */
      GLCD_DrawChar(col * 16, ln * 24, 16, 24, (unsigned char *)&Font_16x24_h[c * 24]);
      break;
  }
}


/*******************************************************************************
* Display string on given line                                                 *
*   Parameter:      ln:       line number                                      *
*                   col:      column number                                    *
*                   fi:       font index (0 = 6x8, 1 = 16x24)                  *
*                   s:        pointer to string                                *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_DisplayString (unsigned int ln, unsigned int col, unsigned char fi, unsigned char *s) {

  while (*s) {
    GLCD_DisplayChar(ln, col++, fi, *s++);
  }
}

/*******************************************************************************
* Draw string at given coordinated                                             *
*   Parameter:      x:        x coordinate                                     *
*                   y:        y coordinate                                     *
*                   fi:       font index (0 = 6x8, 1 = 16x24)                  *
*                   s:        pointer to string                                *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_DrawString (unsigned int x, unsigned int y, unsigned char fi, unsigned char *s) {
  unsigned char c;
  unsigned int  cx = x;

  while (*s) {
    c  = *s++;
    c -= 32;
    switch (fi) {
      case 0:  /* Font 6 x 8 */
        GLCD_DrawChar (cx, y,  6,  8, (unsigned char *)&Font_6x8_h  [c * 8]);
        cx += 6;
        break;
      case 1:  /* Font 16 x 24 */
        GLCD_DrawChar (cx, y, 16, 24, (unsigned char *)&Font_16x24_h[c * 24]);
        cx += 16;
        break;
    }
  }
}


/*******************************************************************************
* Clear given line                                                             *
*   Parameter:      ln:       line number                                      *
*                   fi:       font index (0 = 6x8, 1 = 16x24)                  *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_ClearLn (unsigned int ln, unsigned char fi) {
  unsigned char i;
  unsigned char buf[60];

  switch (fi) {
    case 0:  /* Font 6 x 8 */
      for (i = 0; i < (WIDTH+5)/6; i++)
        buf[i] = ' ';
      buf[i+1] = 0;
      break;
    case 1:  /* Font 16 x 24 */
      for (i = 0; i < (WIDTH+15)/16; i++)
        buf[i] = ' ';
      buf[i+1] = 0;
      break;
  }
  GLCD_DisplayString (ln, 0, fi, buf);
}

/*******************************************************************************
* Draw bargraph                                                                *
*   Parameter:      x:        horizontal position                              *
*                   y:        vertical position                                *
*                   w:        maximum width of bargraph (in pixels)            *
*                   h:        bargraph height                                  *
*                   val:      value of active bargraph (in 1/1024)             *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_Bargraph (unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned int val) {
  int i,j;

  val = (val * w) >> 10;                /* Scale value                        */
  for (i = 0; i < h; i++) {
    for (j = 0; j <= w-1; j++) {
     #if (LANDSCAPE == 0)
      if(j >= val) {
        fb[(y+i)*PHYS_XSZ + (x+j)] = Color[BG_COLOR];
      } else {
        fb[(y+i)*PHYS_XSZ + (x+j)] = Color[TXT_COLOR];
      }
     #else
      if(j >= val) {
        fb[(x+j)*PHYS_XSZ + (y+i)] = Color[BG_COLOR];
      } else {
        fb[(x+j)*PHYS_XSZ + (y+i)] = Color[TXT_COLOR];
      }
     #endif
    }
  }
}


/*******************************************************************************
* Display graphical bitmap image at position x horizontally and y vertically   *
* (This function is optimized for 16 bits per pixel format, it has to be       *
*  adapted for any other bits per pixel format)                                *
*   Parameter:      x:        horizontal position                              *
*                   y:        vertical position                                *
*                   w:        width of bitmap                                  *
*                   h:        height of bitmap                                 *
*                   bitmap:   address at which the bitmap data resides         *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_Bitmap (unsigned int x, unsigned int y, unsigned int w, unsigned int h, unsigned char *bitmap) {
  int i, j, ofs;
  unsigned short *bitmap_ptr = (unsigned short *)bitmap;

  for (i = (h-1)*w, ofs = 0; i >= 0; i -= w, ofs++) {    
    for (j = 0; j < w; j++) {
     #if (LANDSCAPE == 0)
      fb[(x+ofs)*PHYS_XSZ + (y+j)] = bitmap_ptr[i+j];
     #else
      fb[(x+j)*PHYS_XSZ + (y+ofs)] = bitmap_ptr[i+j];
     #endif
    }
  }
}



/*******************************************************************************
* Scroll content of the whole display for dy pixels vertically                 *
*   Parameter:      dy:       number of pixels for vertical scroll             *
*   Return:                                                                    *
*******************************************************************************/

void GLCD_ScrollVertical (unsigned int dy) {
#if (LANDSCAPE == 0)
  uint32_t x, y;
  for (y = 0; y < (PHYS_YSZ - dy); y++) {
    for (x = 0; x < PHYS_XSZ; x++) {
      fb[y*PHYS_XSZ + x] = fb[(y+dy)*PHYS_XSZ + x];
    }
  }
  for (; y < PHYS_YSZ; y++) {
    for (x = 0; x < PHYS_XSZ; x++) {
      fb[y*PHYS_XSZ + x] = Color[BG_COLOR];
    }
  }
#endif
}


/*******************************************************************************
* Write a command to the LCD controller                                        *
*   Parameter:      cmd:      command to write to the LCD                      *
*   Return:                                                                    *
*******************************************************************************/
void GLCD_WrCmd (unsigned char cmd) {
  wr_cmd (cmd);
}


/*******************************************************************************
* Write a value into LCD controller register                                   *
*   Parameter:      reg:      lcd register address                             *
*                   val:      value to write into reg                          *
*   Return:                                                                    *
*******************************************************************************/
void GLCD_WrReg (unsigned char reg, unsigned short val) {
  wr_reg (reg, val);
}
/******************************************************************************/
