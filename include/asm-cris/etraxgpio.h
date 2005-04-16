/* $Id: etraxgpio.h,v 1.8 2002/06/17 15:53:07 johana Exp $ */
/*
 * The following devices are accessable using this driver using
 * GPIO_MAJOR (120) and a couple of minor numbers:
 * For ETRAX 100LX (ARCH_V10):
 * /dev/gpioa  minor 0, 8 bit GPIO, each bit can change direction
 * /dev/gpiob  minor 1, 8 bit GPIO, each bit can change direction
 * /dev/leds   minor 2, Access to leds depending on kernelconfig
 * /dev/gpiog  minor 3
         g0dir, g8_15dir, g16_23dir, g24 dir configurable in R_GEN_CONFIG
         g1-g7 and g25-g31 is both input and outputs but on different pins
         Also note that some bits change pins depending on what interfaces 
         are enabled.
 *
 *
 * For ETRAX 200 (ARCH_V32):
 * /dev/gpioa  minor 0,  8 bit GPIO, each bit can change direction
 * /dev/gpiob  minor 1, 18 bit GPIO, each bit can change direction
 * /dev/gpioc  minor 2, 18 bit GPIO, each bit can change direction
 * /dev/gpiod  minor 3, 18 bit GPIO, each bit can change direction
 * /dev/gpioe  minor 4, 18 bit GPIO, each bit can change direction
 * /dev/leds   minor 5, Access to leds depending on kernelconfig
 *
 */
#ifndef _ASM_ETRAXGPIO_H
#define _ASM_ETRAXGPIO_H

#include <linux/config.h>
/* etraxgpio _IOC_TYPE, bits 8 to 15 in ioctl cmd */
#ifdef CONFIG_ETRAX_ARCH_V10
#define ETRAXGPIO_IOCTYPE 43
#define GPIO_MINOR_A 0
#define GPIO_MINOR_B 1
#define GPIO_MINOR_LEDS 2
#define GPIO_MINOR_G 3
#define GPIO_MINOR_LAST 3
#endif
#ifdef CONFIG_ETRAX_ARCH_V32
#define ETRAXGPIO_IOCTYPE 43
#define GPIO_MINOR_A 0
#define GPIO_MINOR_B 1
#define GPIO_MINOR_C 2
#define GPIO_MINOR_D 3
#define GPIO_MINOR_E 4
#define GPIO_MINOR_LEDS 5
#define GPIO_MINOR_LAST 5
#endif

/* supported ioctl _IOC_NR's */

#define IO_READBITS  0x1  /* read and return current port bits (obsolete) */
#define IO_SETBITS   0x2  /* set the bits marked by 1 in the argument */
#define IO_CLRBITS   0x3  /* clear the bits marked by 1 in the argument */

/* the alarm is waited for by select() */

#define IO_HIGHALARM 0x4  /* set alarm on high for bits marked by 1 */
#define IO_LOWALARM  0x5  /* set alarm on low for bits marked by 1 */
#define IO_CLRALARM  0x6  /* clear alarm for bits marked by 1 */

/* LED ioctl */
#define IO_LEDACTIVE_SET 0x7 /* set active led
                              * 0=off, 1=green, 2=red, 3=yellow */

/* GPIO direction ioctl's */
#define IO_READDIR    0x8  /* Read direction 0=input 1=output  (obsolete) */
#define IO_SETINPUT   0x9  /* Set direction for bits set, 0=unchanged 1=input, 
                              returns mask with current inputs (obsolete) */
#define IO_SETOUTPUT  0xA  /* Set direction for bits set, 0=unchanged 1=output,
                              returns mask with current outputs (obsolete)*/

/* LED ioctl extended */
#define IO_LED_SETBIT 0xB
#define IO_LED_CLRBIT 0xC

/* SHUTDOWN ioctl */
#define IO_SHUTDOWN   0xD
#define IO_GET_PWR_BT 0xE

/* Bit toggling in driver settings */
/* bit set in low byte0 is CLK mask (0x00FF), 
   bit set in byte1 is DATA mask    (0xFF00) 
   msb, data_mask[7:0] , clk_mask[7:0]
 */
#define IO_CFG_WRITE_MODE 0xF 
#define IO_CFG_WRITE_MODE_VALUE(msb, data_mask, clk_mask) \
  ( (((msb)&1) << 16) | (((data_mask) &0xFF) << 8) | ((clk_mask) & 0xFF) )

/* The following 4 ioctl's take a pointer as argument and handles
 * 32 bit ports (port G) properly.
 * These replaces IO_READBITS,IO_SETINPUT AND IO_SETOUTPUT
 */
#define IO_READ_INBITS   0x10 /* *arg is result of reading the input pins */
#define IO_READ_OUTBITS  0x11 /* *arg is result of reading the output shadow */
#define IO_SETGET_INPUT  0x12 /* bits set in *arg is set to input,
                               * *arg updated with current input pins.
                               */
#define IO_SETGET_OUTPUT 0x13 /* bits set in *arg is set to output,
                               * *arg updated with current output pins.
                               */



#endif
