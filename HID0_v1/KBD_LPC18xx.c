/*-----------------------------------------------------------------------------
 * Name:    KBD.c
 * Purpose: Low level keyboard functions
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
#include "KBD.h"

#define NUM_KEYS  3                     /* Number of available keys           */

/* Keys for MCB1800/MCB4300                                                   */
#define P4_0       0x01
#define P4_3       0x02
#define P4_4       0x04
#define WAKEUP0    0x08


/* Clock Control Unit register bits */
#define CCU_CLK_CFG_RUN   (1 << 0)
#define CCU_CLK_CFG_AUTO  (1 << 1)
#define CCU_CLK_STAT_RUN  (1 << 0)
/*-----------------------------------------------------------------------------
 *       KBD_Init:  Initialize keyboard/buttons
 *
 * Parameters: (none)
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void KBD_Init (void) {

  /* Enable clock and init GPIO inputs */
  LPC_CCU1->CLK_M3_GPIO_CFG  = CCU_CLK_CFG_AUTO | CCU_CLK_CFG_RUN;
  while (!(LPC_CCU1->CLK_M3_GPIO_STAT & CCU_CLK_STAT_RUN)); 

  LPC_SCU->SFSP4_0  = (1 << 6) | 0;
  LPC_SCU->SFSP4_3  = (1 << 6) | 0;
  LPC_SCU->SFSP4_4  = (1 << 6) | 0;

  LPC_GPIO_PORT->DIR[2]  &= ~(1 << 0);
  LPC_GPIO_PORT->DIR[2]  &= ~(1 << 3);
  LPC_GPIO_PORT->DIR[2]  &= ~(1 << 4);

  /* Configure WAKEUP0 input */
  LPC_CREG->CREG0 &= ~(3 << 14);
  LPC_EVENTROUTER->HILO    &= ~1;       /* Detect LOW level or falling edge   */
  LPC_EVENTROUTER->EDGE    &= ~1;       /* Detect LOW level                   */
  LPC_EVENTROUTER->CLR_STAT =  1;       /* Clear STATUS event bit 0           */
}


/*-----------------------------------------------------------------------------
 *       KBD_UnInit:  Uninitialize keyboard/buttons
 *
 * Parameters: (none)
 * Return:     (none)
 *----------------------------------------------------------------------------*/
void KBD_UnInit (void) {
  LPC_SCU->SFSP4_0 = 0;
  LPC_SCU->SFSP4_3 = 0;
  LPC_SCU->SFSP4_4 = 0;
}


/*-----------------------------------------------------------------------------
 *       KBD_GetKeys:  Get keyboard state
 *
 * Parameters: (none)
 * Return:      Keys bitmask
 *----------------------------------------------------------------------------*/
uint32_t KBD_GetKeys (void) {
  /* Read board keyboard inputs */
  uint32_t val = 0;

  if ((LPC_GPIO_PORT->PIN[2] & (1 << 0)) == 0) {
    /* P4_0 button */
    val |= P4_0;
  }
  if ((LPC_GPIO_PORT->PIN[2] & (1 << 3)) == 0) {
    /* P4_3 button */
    val |= P4_3;
  }
  if ((LPC_GPIO_PORT->PIN[2] & (1 << 4)) == 0) {
    /* P4_4 button */
    val |= P4_4;  
  }

  LPC_EVENTROUTER->CLR_STAT =  1;     /* Clear STATUS event bit 0           */
  if (LPC_EVENTROUTER->STATUS & 1) {    
    /* WAKEUP0 button */
    val |= WAKEUP0;    
  }
  return (val);
}


/*-----------------------------------------------------------------------------
 *       KBD_Num:  Get number of available keys
 *
 * Parameters: (none)
 * Return:      number of keys
 *----------------------------------------------------------------------------*/
uint32_t KBD_Num (void) {
  return (NUM_KEYS);
}

/*-----------------------------------------------------------------------------
 * End of file
 *----------------------------------------------------------------------------*/
