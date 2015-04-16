/*-----------------------------------------------------------------------------
 * Name:    LED.c
 * Purpose: Low level LED functions
 * Note(s):
 *-----------------------------------------------------------------------------
 * This file is part of the uVision/ARM development tools.
 * This software may only be used under the terms of a valid, current,
 * end user licence from KEIL for a compatible version of KEIL software
 * development tools. Nothing else gives you the right to use this software.
 *
 * This software is supplied "AS IS" without warranties of any kind.
 *
 * Copyright (c) 2004-2013 KEIL - An ARM Company. All rights reserved.
 *----------------------------------------------------------------------------*/

#include <LPC18xx.h>                    /* LPC18xx Definitions                */
#include "LED.h"

#define NUM_LEDS  8                     /* Number of LEDs                     */

const uint32_t led_mask[] = { 1UL << 14,
                              1UL << 13,
                              1UL << 12,
                              1UL << 28,
                              1UL << 27,
                              1UL << 26,
                              1UL << 25,
                              1UL << 24};

/* Clock Control Unit register bits */
#define CCU_CLK_CFG_RUN   (1 << 0)
#define CCU_CLK_CFG_AUTO  (1 << 1)
#define CCU_CLK_STAT_RUN  (1 << 0)

/*-----------------------------------------------------------------------------
 *      LED_Init:  Initialize LEDs
 *
 * Parameters: (none)
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void LED_Init (void) {

  /* Enable clock and init GPIO outputs */
  LPC_CCU1->CLK_M3_GPIO_CFG  = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(LPC_CCU1->CLK_M3_GPIO_STAT & CCU_CLK_STAT_RUN));

  LPC_SCU->SFSPD_10  =  4;              /* GPIO6[24]                          */
  LPC_SCU->SFSPD_11  =  4;              /* GPIO6[25]                          */
  LPC_SCU->SFSPD_12  =  4;              /* GPIO6[26]                          */
  LPC_SCU->SFSPD_13  =  4;              /* GPIO6[27]                          */
  LPC_SCU->SFSPD_14  =  4;              /* GPIO6[28]                          */
  LPC_SCU->SFSP9_0   =  0;              /* GPIO4[12]                          */
  LPC_SCU->SFSP9_1   =  0;              /* GPIO4[13]                          */
  LPC_SCU->SFSP9_2   =  0;              /* GPIO4[14]                          */

  LPC_GPIO_PORT->DIR[6] |= (led_mask[3] | led_mask[4] | led_mask[5] |
                            led_mask[6] | led_mask[7]);

  LPC_GPIO_PORT->DIR[4] |= (led_mask[0] | led_mask[1] | led_mask[2]);
}


/*-----------------------------------------------------------------------------
 *      LED_UnInit:  Uninitialize LEDs; pins are set to reset state
 *
 * Parameters: (none)
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void LED_UnInit(void) {
  LPC_GPIO_PORT->DIR[6] &= ~(led_mask[3] | led_mask[4] | led_mask[5] |
                             led_mask[6] | led_mask[7]);

  LPC_GPIO_PORT->DIR[4] &= ~(led_mask[0] | led_mask[1] | led_mask[2]);
}

/*-----------------------------------------------------------------------------
 *      LED_On: Turns on requested LED
 *
 * Parameters:  num - LED number
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void LED_On (uint32_t num) {

  if (num < 3) {
    LPC_GPIO_PORT->SET[4] = led_mask[num];
  }
  else {
    LPC_GPIO_PORT->SET[6] = led_mask[num];
  }
}

/*-----------------------------------------------------------------------------
 *       LED_Off: Turns off requested LED
 *
 * Parameters:  num - LED number
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void LED_Off (uint32_t num) {

  if (num < 3) {
    LPC_GPIO_PORT->CLR[4] = led_mask[num];
  }
  else {
    LPC_GPIO_PORT->CLR[6] = led_mask[num];
  }
}

/*-----------------------------------------------------------------------------
 *       LED_Val: Write value to LEDs
 *
 * Parameters:  val - value to be displayed on LEDs
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void LED_Val (uint32_t val) {
  int i;

  for (i = 0; i < NUM_LEDS; i++) {
    if (val & (1<<i)) {
      LED_On (i);
    } else {
      LED_Off(i);
    }
  }
}

/*-----------------------------------------------------------------------------
 *       LED_Num: Get number of available LEDs
 *
 * Parameters: (none)
 * Return:      number of available LEDs
 *----------------------------------------------------------------------------*/
uint32_t LED_Num (void) {
  return (NUM_LEDS);
}

/*-----------------------------------------------------------------------------
 * End of file
 *----------------------------------------------------------------------------*/
