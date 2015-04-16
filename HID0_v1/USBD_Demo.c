/*----------------------------------------------------------------------------
 *      RL-ARM - USB
 *----------------------------------------------------------------------------
 *      Name:    USBD_Demo.c
 *      Purpose: USB Device Demonstration
 *      Rev.:    V4.70
 *----------------------------------------------------------------------------
 *      This code is part of the RealView Run-Time Library.
 *      Copyright (c) 2004-2013 KEIL - An ARM Company. All rights reserved.
 *---------------------------------------------------------------------------*/

#include <RTL.h>
#include <rl_usb.h>
#include "GLCD.h"
#include <LPC18xx.H>
#include "KBD.h"
#include "DAP_config.h"
#include "DAP.h"
#include "stdio.h"

extern void usbd_hid_process ();

void MY_printf_num(int num)
{
 unsigned char NumStringBuf[12];
 sprintf (NumStringBuf, "%d", (uint32_t)num);
 GLCD_DisplayString (3, 0, 1, NumStringBuf);
}
int main (void) {
  static U8 but_ex;
         U8 but;
         U8 buf[512];
				 int i;
	//char NumStringBuf[12];
  //sprintf (NumStringBuf, "%d", (uint32_t)987456123);
    
				 for(i=0;i<512;i++){
				 buf[i]=i;
				 }
				 DAP_Setup();
  GLCD_Init          ();
  GLCD_Clear         (Blue);
  GLCD_SetBackColor  (Blue);
  GLCD_SetTextColor  (White);
  GLCD_DisplayString (4, 0, 1, "  USB Device Demo   ");
  GLCD_DisplayString (5, 0, 1, "   www.keil.com     ");
	MY_printf_num(PIN_SWCLK_TCK_IN());
	//MY_printf_num(PIN_SWDIO_TMS_IN());

  usbd_init();                          /* USB Device Initialization          */
  usbd_connect(__TRUE);                 /* USB Device Connect                 */

  while (1) {                           /* Loop forever                       */
   //  but = (U8)(KBD_GetKeys ());
   // if (but ^ but_ex) {
   // buf[0] = but;
    usbd_hid_process();
//		LPC_GPIO_PORT->CLR[5] = (1<<3);
//		LPC_GPIO_PORT->SET[5] = (1<<3);
//	  LPC_GPIO_PORT->CLR[5] = (1<<4);
//		LPC_GPIO_PORT->SET[5] = (1<<4);
    
		//SW_WRITE_BIT(0);
		//SW_WRITE_BIT(1);
   // but_ex = but;
   //}   	
  }
}
