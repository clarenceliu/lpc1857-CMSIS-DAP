/*----------------------------------------------------------------------------
 *      U S B  -  K e r n e l
 *----------------------------------------------------------------------------
 * Name:    usbhw.c
 * Purpose: USB Hardware Layer Module for Philips LPC17xx
 * Version: V1.20
 *----------------------------------------------------------------------------
 *      This software is supplied "AS IS" without any warranties, express,
 *      implied or statutory, including but not limited to the implied
 *      warranties of fitness for purpose, satisfactory quality and
 *      noninfringement. Keil extends you a royalty-free right to reproduce
 *      and distribute executable files created using this software for use
 *      on NXP Semiconductors LPC microcontroller devices only. Nothing else
 *      gives you the right to use this software.
 *
 * Copyright (c) 2009 Keil - An ARM Company. All rights reserved.
 *----------------------------------------------------------------------------
 * History:
 *          V1.20 Added USB_ClearEPBuf
 *          V1.00 Initial Version
 *----------------------------------------------------------------------------*/
#include "LPC18xx.h"                        /* LPC13xx definitions */
#include "usbreg.h"
#include "usbhw.h"
#include "cox_config.h"
#include <stdio.h>

//#include "usb_config_USB0.c"


/* Endpoint queue head                                                        */
typedef struct __EPQH {
  uint32_t   cap;
  uint32_t   curr_dTD;
  uint32_t   next_dTD;
  uint32_t   dTD_token;
  uint32_t   buf[5];
  uint32_t   reserved;
  uint32_t   setup[2];
  uint32_t   reserved1[4];
} EPQH;

/* Endpoint transfer descriptor                                               */
typedef struct __dTD {
  uint32_t   next_dTD;
  uint32_t   dTD_token;
  uint32_t   buf[5];
  uint32_t   reserved;
} dTD;

/* Endpoint                                                                   */
typedef struct __EP{
  uint8_t    *buf;
  uint32_t   maxPacket;
} EP;
#define USBD_EP_NUM  3
#define USBD_MAX_PACKET0            64

EPQH __attribute((aligned (2048))) EPQHx[(USBD_EP_NUM + 1) * 2];
dTD  __attribute((aligned (32))) dTDx[ (USBD_EP_NUM + 1) * 2];

EP        Ep[(USBD_EP_NUM + 1) * 2];
uint32_t  BufUsed;

uint32_t  IsoEp;

uint32_t  cmpl_pnd;

#define LPC_USBx            LPC_USB0

#define ENDPTCTRL(EPNum)  *(volatile uint32_t *)((uint32_t)(&LPC_USBx->ENDPTCTRL0) + 4 * EPNum)
#define EP_OUT_IDX(EPNum)  (EPNum * 2    )
#define EP_IN_IDX(EPNum)   (EPNum * 2 + 1)

#define HS(en)             (USBD_HS_ENABLE * en)

/* reserve RAM for endpoint buffers                                           */
#if USBD_VENDOR_ENABLE
/* custom class: user defined buffer size                                     */
#define EP_BUF_POOL_SIZE  0x1000
uint8_t __align(4096) EPBufPool[EP_BUF_POOL_SIZE]
#else
///* supported classes are used                                                 */
//uint8_t __attribute((aligned (4096))) EPBufPool[
//                                USBD_MAX_PACKET0                                                                                                     * 2 +
//                                USBD_HID_ENABLE     *  (HS(USBD_HID_HS_ENABLE)     ? USBD_HID_HS_WMAXPACKETSIZE      : USBD_HID_WMAXPACKETSIZE)      * 2 +
//                                USBD_MSC_ENABLE     *  (HS(USBD_MSC_HS_ENABLE)     ? USBD_MSC_HS_WMAXPACKETSIZE      : USBD_MSC_WMAXPACKETSIZE)      * 2 +
//                                USBD_ADC_ENABLE     *  (HS(USBD_ADC_HS_ENABLE)     ? USBD_ADC_HS_WMAXPACKETSIZE      : USBD_ADC_WMAXPACKETSIZE)          +
//                                USBD_CDC_ACM_ENABLE * ((HS(USBD_CDC_ACM_HS_ENABLE) ? USBD_CDC_ACM_HS_WMAXPACKETSIZE  : USBD_CDC_ACM_WMAXPACKETSIZE)      +
//                                                       (HS(USBD_CDC_ACM_HS_ENABLE) ? USBD_CDC_ACM_HS_WMAXPACKETSIZE1 : USBD_CDC_ACM_WMAXPACKETSIZE1) * 2 )];

uint8_t __attribute((aligned (4096))) EPBufPool[0x1000];
#endif

void USBD_PrimeEp     (uint32_t EPNum, uint32_t cnt);

void LPC_USBReset (void);
void LPC_USBSetAddress (uint32_t adr);

USB_Event_Handler USB_P_EP_EVENT[USB_LOGIC_EP_NUM]={NULL};
USB_Event_Handler USB_WakeUp_Event = NULL;
USB_Event_Handler USB_Reset_Event = NULL;
USB_Event_Handler USB_Suspend_Event = NULL;
USB_Event_Handler USB_Resume_Event = NULL;
USB_Event_Handler USB_Error_Event = NULL;
USB_Event_Handler USB_SOF_Event = NULL;

void *USB_P_EP_EVENT_DATA[USB_LOGIC_EP_NUM]={NULL};

void USBD_Intr (int ena) {
  if (ena) {
    NVIC_EnableIRQ  (USB0_IRQn);        /* Enable USB interrupt               */
  } else {
    NVIC_DisableIRQ (USB0_IRQn);        /* Disable USB interrupt              */
  }
}

//#pragma diag_suppress 1441
/*
 *    USB and IO Clock configuration only.
 *    The same as call PeriClkIOInit(IOCON_USB);
 *    The purpose is to reduce the code space for
 *    overall USB project and reserve code space for
 *    USB debugging.
 *    Parameters:      None
 *    Return Value:    None
 */
void LPC_USBIOClkConfig( void )
{
    /* Enable AHB clock to the GPIO domain. */
//    LPC_SYSCON->SYSAHBCLKCTRL |= (1<<6);
//
//    LPC_IOCON->PIO0_1 &= ~0x07;
//    LPC_IOCON->PIO0_1 |= 0x01;		/* CLK OUT */
//
//    /* Enable AHB clock to the USB block. */
//    LPC_SYSCON->SYSAHBCLKCTRL |= (1<<14);
//    LPC_IOCON->PIO0_3   &= ~0x1F;
//    LPC_IOCON->PIO0_3   |= 0x01;		/* Secondary function VBUS */
//    LPC_IOCON->PIO0_6   &= ~0x07;
//    LPC_IOCON->PIO0_6   |= 0x01;		/* Secondary function SoftConn */
    return;
}


/*
 *  USB Initialize Function
 *   Called by the User to initialize USB
 *    Return Value:    None
 */

void LPC_USBInit (void)
{
	USBD_Intr(0);

	  /* BASE_USB0_CLK                                                            */
	  LPC_CGU->BASE_USB0_CLK   = (0x01 << 11) |       /* Autoblock En             */
	                             (0x07 << 24) ;       /* Clock source: PLL0       */
	  LPC_CCU1->CLK_M3_USB0_CFG |= 1;
	  while (!(LPC_CCU1->CLK_M3_USB0_STAT & 1));

	  LPC_SCU->SFSP6_3 = 1;                 /* pwr en                             */
	  LPC_SCU->SFSP6_6 = 3;                 /* pwr fault                          */
	  LPC_SCU->SFSP8_1 = 1;                 /* port indicator LED control out 1   */
	  LPC_SCU->SFSP8_2 = 1;                 /* port indicator LED control out 0   */

	  LPC_USBx->USBCMD_D |= (1UL << 1);     /* usb reset                          */
	  while (LPC_USBx->USBCMD_D & (1UL << 1));

	  LPC_CREG->CREG0 &= ~(1 << 5);

	  LPC_USBx->USBMODE_D  = 2 | (1UL << 3);/* device mode                        */

	#if USBD_HS_ENABLE
	  LPC_USBx->PORTSC1_D &= ~(1UL << 24);
	#else
	  LPC_USBx->PORTSC1_D |=  (1UL << 24);
	#endif


	  LPC_USBx->OTGSC = 1 | (1UL << 3);

	  Ep[EP_OUT_IDX(0)].maxPacket = USBD_MAX_PACKET0;

	  LPC_USBx->USBINTR_D  = (1UL << 0 ) |  /* usb int enable                     */
	                         (1UL << 2 ) |  /* port change detect int enable      */
	                         (1UL << 8 ) |  /* suspend int enable                 */
	                         (1UL << 16) |  /* nak int enable                     */
	                         (1UL << 6 ) |  /* reset int enable                   */
	#ifdef __RTX
	              ((USBD_RTX_DevTask   != 0) ? (1UL << 7) : 0) |   /* SOF         */
	              ((USBD_RTX_DevTask   != 0) ? (1UL << 1) : 0) ;   /* Error       */
	#else
	              ((USB_SOF_Event   != 0) ? (1UL << 7) : 0) |   /* SOF         */
	              ((USB_Error_Event != 0) ? (1UL << 1) : 0) ;   /* Error       */
	#endif

	  LPC_USBReset();
	  USBD_Intr(1);
    return;
}


/*
 *  USB Connect Function
 *   Called by the User to Connect/Disconnect USB
 *    Parameters:      con:   Connect/Disconnect
 *    Return Value:    None
 */

void LPC_USBConnect (uint32_t con) {
  if (con) {
    LPC_USBx->USBCMD_D |= 1;            /* run                                */
  }
  else {
    LPC_USBx->USBCMD_D &= ~1;           /* stop                               */
  }
}


/*
 *  USB Reset Function
 *   Called automatically on USB Reset
 *    Return Value:    None
 */

void LPC_USBReset (void) {
  uint32_t i;
  uint8_t * ptr;

  cmpl_pnd = 0;

  for (i = 1; i < USBD_EP_NUM + 1; i++) {
    ENDPTCTRL(i) &= ~((1UL << 7) | (1UL << 23));
  }

  /* clear interrupts                                                         */
  LPC_USBx->ENDPTNAK       = 0xFFFFFFFF;
  LPC_USBx->ENDPTNAKEN     = 0;
  LPC_USBx->USBSTS_D       = 0xFFFFFFFF;
  LPC_USBx->ENDPTSETUPSTAT = LPC_USBx->ENDPTSETUPSTAT;
  LPC_USBx->ENDPTCOMPLETE  = LPC_USBx->ENDPTCOMPLETE;
  while (LPC_USBx->ENDPTPRIME);

  LPC_USBx->ENDPTFLUSH = 0xFFFFFFFF;
  while (LPC_USBx->ENDPTFLUSH);

  LPC_USBx->USBCMD_D &= ~0x00FF0000;    /* immediate intrrupt treshold        */

  /* clear ednpoint queue heads                                               */
  ptr = (uint8_t *)EPQHx;
  for (i = 0; i < (sizeof(EPQHx)*((USBD_EP_NUM + 1) * 2)); i++) {
    ptr[i] = 0;
  }

  /* clear endpoint transfer descriptors                                      */
  ptr = (uint8_t *)dTDx;
  for (i = 0; i < (sizeof(dTDx)*((USBD_EP_NUM + 1) * 2)); i++) {
    ptr[i] = 0;
  }

  Ep[EP_OUT_IDX(0)].maxPacket  = USBD_MAX_PACKET0;
  Ep[EP_OUT_IDX(0)].buf        = EPBufPool;
  BufUsed                      = USBD_MAX_PACKET0;
  Ep[EP_IN_IDX(0)].maxPacket   = USBD_MAX_PACKET0;
  Ep[EP_IN_IDX(0)].buf         = &(EPBufPool[BufUsed]);
  BufUsed                     += USBD_MAX_PACKET0;

  dTDx[EP_OUT_IDX(0)].next_dTD = 1;
  dTDx[EP_IN_IDX( 0)].next_dTD = 1;

  dTDx[EP_OUT_IDX(0)].dTD_token = (USBD_MAX_PACKET0 << 16) | /* total bytes   */
                                  (1UL << 15);               /* int on compl  */
  dTDx[EP_IN_IDX( 0)].dTD_token = (USBD_MAX_PACKET0 << 16) | /* total bytes   */
                                  (1UL << 15);               /* int on compl  */

  EPQHx[EP_OUT_IDX(0)].next_dTD = (uint32_t) &dTDx[EP_OUT_IDX(0)];
  EPQHx[EP_IN_IDX( 0)].next_dTD = (uint32_t) &dTDx[EP_IN_IDX( 0)];

  EPQHx[EP_OUT_IDX(0)].cap = ((USBD_MAX_PACKET0 & 0x0EFF) << 16) |
                             (1UL << 29) |
                             (1UL << 15);                    /* int on setup  */

  EPQHx[EP_IN_IDX( 0)].cap = (USBD_MAX_PACKET0 << 16) |
                             (1UL << 29) |
                             (1UL << 15);                    /* int on setup  */

  LPC_USBx->ENDPOINTLISTADDR = (uint32_t)EPQHx;

  LPC_USBx->USBMODE_D |= (1UL << 3 );   /* Setup lockouts off                 */
  LPC_USBx->ENDPTCTRL0 = 0x00C000C0;

  USBD_PrimeEp(0, Ep[EP_OUT_IDX(0)].maxPacket);
}


/*
 *  USB Suspend Function
 *   Called automatically on USB Suspend
 *    Return Value:    None
 */

void LPC_USBSuspend (void)
{
    /* Performed by Hardware */
}


/*
 *  USB Resume Function
 *   Called automatically on USB Resume
 *    Return Value:    None
 */

void LPC_USBResume (void)
{
    /* Performed by Hardware */
}


/*
 *  USB Remote Wakeup Function
 *   Called automatically on USB Remote Wakeup
 *    Return Value:    None
 */

void LPC_USBWakeUp (void)
{

	LPC_USBx->PORTSC1_D |= (1UL << 6);
}


/*
 *  USB Remote Wakeup Configuration Function
 *    Parameters:      cfg:   Enable/Disable
 *    Return Value:    None
 */

void LPC_USBWakeUpCfg (uint32_t cfg)
{
    cfg = cfg;  /* Not needed */
}


/*
 *  USB Set Address Function
 *    Parameters:      adr:   USB Address
 *    Return Value:    None
 */

void LPC_USBSetAddress (uint32_t adr)
{
    LPC_USBx->DEVICEADDR  = (adr << 25);
    LPC_USBx->DEVICEADDR |= (1UL << 24);
}


/*
 *  USB Configure Function
 *    Parameters:      cfg:   Configure/Deconfigure
 *    Return Value:    None
 */

void LPC_USBConfigure (uint32_t cfg)
{

	  uint32_t i;

	  if (!cfg) {
	    for (i = 2; i < (2 * (USBD_EP_NUM + 1)); i++) {
	      Ep[i].buf       = 0;
	      Ep[i].maxPacket = 0;
	    }
	    BufUsed           = 2 * USBD_MAX_PACKET0;
	  }
    return;
}


/*
 *  Configure USB Endpoint according to Descriptor
 *    Parameters:      pEPD:  Pointer to Endpoint Descriptor
 *    Return Value:    None
 */

void LPC_USBConfigEP (USB_ENDPOINT_DESCRIPTOR *pEPD)
{
	 uint32_t num, val, type, idx;

	  if ((pEPD->bEndpointAddress & 0x80)) {
	    val = 16;
	    num = pEPD->bEndpointAddress & ~0x80;
	    idx = EP_IN_IDX(num);
	  }
	  else {
	    val = 0;
	    num = pEPD->bEndpointAddress;
	    idx = EP_OUT_IDX(num);
	  }

	  type = pEPD->bmAttributes & 0x80;

	  if (!(Ep[idx].buf)) {
	    Ep[idx].buf          =  &(EPBufPool[BufUsed]);
	    Ep[idx].maxPacket    =  pEPD->wMaxPacketSize;
	    BufUsed             +=  pEPD->wMaxPacketSize;

	    /* Isochronous endpoint                                                   */
	    if (type == 1) {
	      IsoEp |= (1UL << (num + val));
	    }
	  }

	  dTDx[idx].buf[0]    = (uint32_t)(Ep[idx].buf);
	  dTDx[idx].next_dTD  =  1;
	  EPQHx[idx].cap      = (Ep[idx].maxPacket << 16) |
	                        (1UL               << 29);

	  ENDPTCTRL(num)  &= ~(0xFFFF << val);
	  ENDPTCTRL(num)  |=  ((type << 2) << val) |
	                      ((1UL  << 6) << val);   /* Data toogle reset            */
    return;
}


/*
 *  Set Direction for USB Control Endpoint
 *    Parameters:      dir:   Out (dir == 0), In (dir <> 0)
 *    Return Value:    None
 */

void LPC_USBDirCtrlEP (uint32_t dir)
{
    dir = dir;  /* Not needed */
}


/*
 *  Enable USB Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void LPC_USBEnableEP (uint32_t EPNum)
{
	  if (EPNum & 0x80) {
	    EPNum &= 0x7F;
	    ENDPTCTRL(EPNum) |= (1UL << 23);    /* EP enabled                         */
	  }
	  else {
	    ENDPTCTRL(EPNum) |= (1UL << 7 );    /* EP enabled                         */
	  }
}


/*
 *  Disable USB Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void LPC_USBDisableEP (uint32_t EPNum)
{
	  if (EPNum & 0x80) {
	    EPNum &= 0x7F;
	    ENDPTCTRL(EPNum) &= ~(1UL << 23);   /* EP disabled                        */
	  }
	  else {
	    ENDPTCTRL(EPNum)     &= ~(1UL << 7 );/* EP disabled                       */
	  }
}


/*
 *  Reset USB Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void LPC_USBResetEP (uint32_t EPNum)
{
	if (EPNum & 0x80) {
	    EPNum &= 0x7F;
	    EPQHx[EP_IN_IDX(EPNum)].dTD_token &= 0xC0;
	    LPC_USBx->ENDPTFLUSH = (1UL << (EPNum + 16));  /* flush endpoint          */
	    while (LPC_USBx->ENDPTFLUSH & (1UL << (EPNum + 16)));
	    ENDPTCTRL(EPNum) |= (1UL << 22);    /* data toggle reset                  */
	  }
	  else {
	    EPQHx[EP_OUT_IDX(EPNum)].dTD_token &= 0xC0;
	    LPC_USBx->ENDPTFLUSH = (1UL << EPNum);         /* flush endpoint          */
	    while (LPC_USBx->ENDPTFLUSH & (1UL << EPNum));
	    ENDPTCTRL(EPNum) |= (1UL << 6 );    /* data toggle reset                  */
	    USBD_PrimeEp(EPNum, Ep[EP_OUT_IDX(EPNum)].maxPacket);
	  }
}


/*
 *  Set Stall for USB Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void LPC_USBSetStallEP (uint32_t EPNum)
{
	  if (EPNum & 0x80) {
	    EPNum &= 0x7F;
	    ENDPTCTRL(EPNum) |= (1UL << 16);    /* IN endpoint stall                  */
	  }
	  else {
	    ENDPTCTRL(EPNum) |= (1UL << 0 );    /* OUT endpoint stall                 */
	  }
}


/*
 *  Clear Stall for USB Endpoint
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void LPC_USBClrStallEP (uint32_t EPNum)
{
	if (EPNum & 0x80) {
	    EPNum &= 0x7F;
	    ENDPTCTRL(EPNum) &= ~(1UL << 16);   /* clear stall                        */
	    ENDPTCTRL(EPNum) |=  (1UL << 22);   /* data toggle reset                  */
	    while (ENDPTCTRL(EPNum) & (1UL << 16));
	    LPC_USBResetEP(EPNum | 0x80);
	  }
	  else {
	    ENDPTCTRL(EPNum) &= ~(1UL << 0 );   /* clear stall                        */
	    ENDPTCTRL(EPNum) |=  (1UL << 6 );   /* data toggle reset                  */
	  }
}


/*
 *  Clear USB Endpoint Buffer
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */

void LPC_USBClearEPBuf (uint32_t EPNum)
{
//    WrCmdEP(EPNum, CMD_CLR_BUF);
}


/*
 *  USB Device Prime endpoint function
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     cnt:   Bytes to transfer/receive
 *    Return Value:    None
 */

void USBD_PrimeEp (uint32_t EPNum, uint32_t cnt) {
  uint32_t idx, val;

  /* IN endpoint                                                              */
  if (EPNum & 0x80) {
    EPNum &= 0x7F;
    idx    = EP_IN_IDX(EPNum);
    val    = (1UL << (EPNum + 16));
  }
  /* OUT endpoint                                                             */
  else {
    val    = (1UL << EPNum);
    idx    = EP_OUT_IDX(EPNum);
  }

  dTDx[idx].buf[0]    = (uint32_t)(Ep[idx].buf);
  dTDx[idx].next_dTD  = 1;

  if (IsoEp & val) {
    if (Ep[idx].maxPacket <= cnt) {
      dTDx[idx].dTD_token = (1 << 10);             /* MultO = 1               */
    }
    else if ((Ep[idx].maxPacket * 2) <= cnt) {
      dTDx[idx].dTD_token = (2 << 10);             /* MultO = 2               */
    }
    else {
      dTDx[idx].dTD_token = (3 << 10);             /* MultO = 3               */
    }
  }
  else {
    dTDx[idx].dTD_token = 0;
  }

  dTDx[idx].dTD_token  |= (cnt   << 16) |          /* bytes to transfer       */
                          (1UL   << 15) |          /* int on complete         */
                           0x80;                   /* status - active         */

  EPQHx[idx].next_dTD   = (uint32_t)(&dTDx[idx]);
  EPQHx[idx].dTD_token &= ~0xC0;

  LPC_USBx->ENDPTPRIME = (val);
  while ((LPC_USBx->ENDPTPRIME & val));
}


/*
 *  Read USB Endpoint Data
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     pData: Pointer to Data Buffer
 *    Return Value:    Number of bytes read
 */

uint32_t LPC_USBReadEP (uint32_t EPNum, uint8_t *pData)
{
	  uint32_t cnt  = 0;
	  uint32_t i;

	  /* Setup packet                                                             */
	  if ((LPC_USBx->ENDPTSETUPSTAT & 1) && (!EPNum)) {
	    LPC_USBx->ENDPTSETUPSTAT = 1;
	    while (LPC_USBx->ENDPTSETUPSTAT & 1);
	    do {
	      *((__attribute__((packed)) uint32_t*) pData)      = EPQHx[EP_OUT_IDX(0)].setup[0];
	      *((__attribute__((packed)) uint32_t*)(pData + 4)) = EPQHx[EP_OUT_IDX(0)].setup[1];
	      cnt = 8;
	      LPC_USBx->USBCMD_D |= (1UL << 13);
	    } while (!(LPC_USBx->USBCMD_D & (1UL << 13)));
	    LPC_USBx->USBCMD_D &= (~(1UL << 13));
	    LPC_USBx->ENDPTFLUSH = (1UL << EPNum) | (1UL << (EPNum + 16));
	    while (LPC_USBx->ENDPTFLUSH & ((1UL << (EPNum + 16)) | (1UL << EPNum)));
	    while (LPC_USBx->ENDPTSETUPSTAT & 1);
	    USBD_PrimeEp(EPNum, Ep[EP_OUT_IDX(EPNum)].maxPacket);
	  }

	  /* OUT Packet                                                               */
	  else {
	    if (Ep[EP_OUT_IDX(EPNum)].buf) {
	      cnt = Ep[EP_OUT_IDX(EPNum)].maxPacket -
	           ((dTDx[EP_OUT_IDX(EPNum)].dTD_token >> 16) & 0x7FFF);
	      for (i = 0; i < cnt; i++) {
	        pData[i] =  Ep[EP_OUT_IDX(EPNum)].buf[i];
	      }
	    }
	    LPC_USBx->ENDPTCOMPLETE = (1UL << EPNum);
	    cmpl_pnd &= ~(1UL << EPNum);
	    USBD_PrimeEp(EPNum, Ep[EP_OUT_IDX(EPNum)].maxPacket);
	  }

	  return (cnt);
}


/*
 *  Write USB Endpoint Data
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     pData: Pointer to Data Buffer
 *                     cnt:   Number of bytes to write
 *    Return Value:    Number of bytes written
 */

uint32_t LPC_USBWriteEP (uint32_t EPNum, uint8_t *pData, uint32_t cnt)
{
	  uint32_t i;

	  EPNum &= 0x7f;

	  for (i = 0; i < cnt; i++) {
	    Ep[EP_IN_IDX(EPNum)].buf[i] = pData[i];
	  }

	  USBD_PrimeEp(EPNum | 0x80, cnt);

	  return (cnt);
}

/*
 *  Get USB Last Frame Number
 *    Parameters:      None
 *    Return Value:    Frame Number
 */

uint32_t LPC_USBGetFrame (void)
{
	return ((LPC_USBx->FRINDEX_D >> 3) & 0x0FFF);
}


/*
 *  USB Interrupt Service Routine
 */

void USB0_IRQHandler (void)
{
	  uint32_t sts, cmpl, num;

	  sts  = LPC_USBx->USBSTS_D & LPC_USBx->USBINTR_D;
	  cmpl = LPC_USBx->ENDPTCOMPLETE;

	  LPC_USBx->USBSTS_D = sts;                     /* clear interupt flags       */

	  /* reset interrupt                                                          */
	  if (sts & (1UL << 6)) {
		  LPC_USBReset();
	    //usbd_reset_core();
	#ifdef __RTX
	    if (USBD_RTX_DevTask) {
	      isr_evt_set(USBD_EVT_RESET, USBD_RTX_DevTask);
	    }
	#else
	    if (0) {
//	      USBD_P_Reset_Event();
	    }
	#endif
	  }

	  /* suspend interrupt                                                        */
	  if (sts & (1UL << 8)) {
//	    USBD_Suspend();
	#ifdef __RTX
	    if (USBD_RTX_DevTask) {
	      isr_evt_set(USBD_EVT_SUSPEND, USBD_RTX_DevTask);
	    }
	#else
	    if (0) {
//	      USBD_P_Suspend_Event();
	    }
	#endif
	  }

	  /* SOF interrupt                                                            */
	  if (sts & (1UL << 7)) {
	    if (IsoEp) {
	      for (num = 0; num < USBD_EP_NUM + 1; num++) {
	        if (IsoEp & (1UL << num)) {
	          USBD_PrimeEp (num, Ep[EP_OUT_IDX(num)].maxPacket);
	        }
	      }
	    }
	    else {
	#ifdef __RTX
	      if (USBD_RTX_DevTask) {
	        isr_evt_set(USBD_EVT_SOF, USBD_RTX_DevTask);
	      }
	#else
	      if (0) {
//	        USBD_P_SOF_Event();
	      }
	#endif
	    }
	  }

	  /* port change detect interrupt                                             */
	  if (sts & (1UL << 2)) {
	    if (((LPC_USBx->PORTSC1_D >> 26) & 0x03) == 2) {
	      //USBD_HighSpeed = __TRUE; TODO:
	    }
//	    USBD_Resume();
	#ifdef __RTX
	    if (USBD_RTX_DevTask) {
	      isr_evt_set(USBD_EVT_RESUME,  USBD_RTX_DevTask);
	    }
	#else
	    if (0) {
//	      USBD_P_Resume_Event();
	    }
	#endif
	  }

	  /* USB interrupt - completed transfer                                       */
	  if (sts & 1) {
	    /* Setup Packet                                                           */
	    if (LPC_USBx->ENDPTSETUPSTAT) {
	#ifdef __RTX
	      if (USBD_RTX_EPTask[0]) {
	        isr_evt_set(USBD_EVT_SETUP, USBD_RTX_EPTask[0]);
	      }
	#else
	      if (USB_P_EP_EVENT[0]) {
	    	  USB_P_EP_EVENT[0](USB_P_EP_EVENT_DATA[0],USB_EVT_SETUP);
	      }
	#endif
	    }
	    /* IN Packet                                                              */
	    if (cmpl & (0x3F << 16)) {
	      for (num = 0; num < USBD_EP_NUM + 1; num++) {
	        if (((cmpl >> 16) & 0x3F) & (1UL << num)) {
	          LPC_USBx->ENDPTCOMPLETE = (1UL << (num + 16));    /* Clear completed*/
	#ifdef __RTX
	          if (USBD_RTX_EPTask[num]) {
	            isr_evt_set(USBD_EVT_IN,  USBD_RTX_EPTask[num]);
	          }
	#else
	          if (USB_P_EP_EVENT[num]) {
	        	  USB_P_EP_EVENT[num](USB_P_EP_EVENT_DATA[num],USB_EVT_IN);

	          }
	#endif
	        }
	        else if (0){

			}
	      }
	    }

	    /* OUT Packet                                                             */
	    if (cmpl & 0x3F) {
	      for (num = 0; num < USBD_EP_NUM + 1; num++) {
	        if ((cmpl ^ cmpl_pnd) & cmpl & (1UL << num)) {
	          cmpl_pnd |= 1UL << num;
	#ifdef __RTX
	          if (USBD_RTX_EPTask[num]) {
	            isr_evt_set(USBD_EVT_OUT, USBD_RTX_EPTask[num]);
	          }
	          else if (IsoEp & (1UL << num)) {
	            if (USBD_RTX_DevTask) {
	              isr_evt_set(USBD_EVT_SOF, USBD_RTX_DevTask);
	            }
	          }
	#else
	          if (USB_P_EP_EVENT[num]) {
	        	  USB_P_EP_EVENT[num](USB_P_EP_EVENT_DATA[num],USB_EVT_OUT);
	        	  LED_On(num);
	          }
	          else if (IsoEp & (1UL << num)) {
//	            if (USBD_P_SOF_Event) {
//	              USBD_P_SOF_Event();
//	            }
	          }
	#endif
	        }
	        else if (cmpl & (1UL << num)) {

			}
	      }
	    }
	  }

	  /* error interrupt                                                          */
	  if (sts & (1UL << 1)) {

	    for (num = 0; num < USBD_EP_NUM + 1; num++) {
	      if (cmpl & (1UL << num)) {
	    	  LED_On(num+4);
	#ifdef __RTX
	        if (USBD_RTX_DevTask) {
	          LastError = dTDx[EP_OUT_IDX(num)].dTD_token & 0xE8;
	          isr_evt_set(USBD_EVT_ERROR, USBD_RTX_DevTask);
	        }
	#else
//	        if (USBD_P_Error_Event) {
//	          USBD_P_Error_Event(dTDx[EP_OUT_IDX(num)].dTD_token & 0xE8);
//	        }
	#endif
	      }
	      if (cmpl & (1UL << (num + 16))) {
	#ifdef __RTX
	        if (USBD_RTX_DevTask) {
	          LastError = dTDx[EP_IN_IDX(num)].dTD_token & 0xE8;
	          isr_evt_set(USBD_EVT_ERROR, USBD_RTX_DevTask);
	        }
	#else
//	        if (USBD_P_Error_Event) {
//	          USBD_P_Error_Event(dTDx[EP_IN_IDX(num)].dTD_token & 0xE8);
//	        }
	#endif
	      }
	    }
	  }
    return;
}



/********************************************************************************************************//**
 * @brief     Get USB Last Frame Number
 * @param[in] event:   The logic number of USB endpoint
 *            handler: The callback function of USB endpoint
 *            data:    The extern type which is using here
 * @return    0=> COX_SUCCESS,1=>COX_ERROR
************************************************************************************************************/
void LPC_USBEvent (uint8_t event, USB_Event_Handler handler, void *data)
{
//        COX_Status err = COX_ERROR;
        if(event == 0)
        {
                USB_P_EP_EVENT[0] = handler;
                USB_P_EP_EVENT_DATA[0] = data;
//                err = COX_SUCCESS;
                return;
        }
        switch(event)
        {
                case USB_EVENT_EP1 :
                        USB_P_EP_EVENT[1] = handler;
                        USB_P_EP_EVENT_DATA[1] = data;
//                        err = COX_SUCCESS;
                        break;
                case USB_EVENT_EP2 :
                        USB_P_EP_EVENT[2] = handler;
                        USB_P_EP_EVENT_DATA[2] = data;
//                        err = COX_SUCCESS;
                        break;
                case USB_EVENT_EP3 :
                        USB_P_EP_EVENT[3] = handler;
                        USB_P_EP_EVENT_DATA[3] = data;
//                        err = COX_SUCCESS;
                        break;
                case USB_EVENT_EP4 :
                        USB_P_EP_EVENT[4] = handler;
                        USB_P_EP_EVENT_DATA[4] = data;
//                        err = COX_SUCCESS;
                        break;
                case USB_EVENT_EP5 :
                        USB_P_EP_EVENT[5] = handler;
                        USB_P_EP_EVENT_DATA[5] = data;
//                        err = COX_SUCCESS;
                        break;
                case USB_EVENT_EP6 :
                        USB_P_EP_EVENT[6] = handler;
                        USB_P_EP_EVENT_DATA[6] = data;
//                        err = COX_SUCCESS;
                        break;
                case USB_EVENT_EP7 :
                        USB_P_EP_EVENT[7] = handler;
                        USB_P_EP_EVENT_DATA[7] = data;
//                        err = COX_SUCCESS;
                        break;
        }
//        return err;

}

void LPC_USBControlOutEn()
{

}

COX_USB_PI pi_usb =
{
        LPC_USBIOClkConfig,
        LPC_USBInit,
        LPC_USBConnect,
        LPC_USBReset,
        LPC_USBSuspend,
        LPC_USBResume,
        LPC_USBWakeUp,
        LPC_USBWakeUpCfg,
        LPC_USBSetAddress,
        LPC_USBConfigure,
        LPC_USBConfigEP,
        LPC_USBDirCtrlEP,
        LPC_USBEnableEP,
        LPC_USBDisableEP,
        LPC_USBResetEP,
        LPC_USBSetStallEP,
        LPC_USBClrStallEP,
        LPC_USBControlOutEn,
        LPC_USBClearEPBuf,
        LPC_USBReadEP,
        LPC_USBWriteEP,
        LPC_USBGetFrame,
        LPC_USBEvent,
};
