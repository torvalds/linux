/* linux/include/asm/hardware/s3c2410/regs-gpio.h
 *
 * Copyright (c) 2003,2004 Simtec Electronics <linux@simtec.co.uk>
 *		           http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 GPIO register definitions
 *
 *  Changelog:
 *    19-06-2003     BJD     Created file
 *    23-06-2003     BJD     Updated GSTATUS registers
 *    12-03-2004     BJD     Updated include protection
 *    20-07-2004     BJD     Added GPIO pin numbers, added Port A definitions
 *    04-10-2004     BJD     Fixed number of bugs, added EXT IRQ filter defs
 *    17-10-2004     BJD     Added GSTATUS1 register definitions
 *    18-11-2004     BJD     Fixed definitions of GPE3, GPE4, GPE5 and GPE6
 *    18-11-2004     BJD     Added S3C2440 AC97 controls
 *    10-Mar-2005    LCVR    Changed S3C2410_VA to S3C24XX_VA
 *    28-Mar-2005    LCVR    Fixed definition of GPB10
*/


#ifndef __ASM_ARCH_REGS_GPIO_H
#define __ASM_ARCH_REGS_GPIO_H "$Id: gpio.h,v 1.5 2003/05/19 12:51:08 ben Exp $"

#define S3C2410_GPIONO(bank,offset) ((bank) + (offset))

#define S3C2410_GPIO_BANKA   (32*0)
#define S3C2410_GPIO_BANKB   (32*1)
#define S3C2410_GPIO_BANKC   (32*2)
#define S3C2410_GPIO_BANKD   (32*3)
#define S3C2410_GPIO_BANKE   (32*4)
#define S3C2410_GPIO_BANKF   (32*5)
#define S3C2410_GPIO_BANKG   (32*6)
#define S3C2410_GPIO_BANKH   (32*7)

#define S3C2410_GPIO_BASE(pin)   ((((pin) & ~31) >> 1) + S3C24XX_VA_GPIO)
#define S3C2410_GPIO_OFFSET(pin) ((pin) & 31)

/* general configuration options */

#define S3C2410_GPIO_LEAVE   (0xFFFFFFFF)

/* configure GPIO ports A..G */

#define S3C2410_GPIOREG(x) ((x) + S3C24XX_VA_GPIO)

/* port A - 22bits, zero in bit X makes pin X output
 * 1 makes port special function, this is default
*/
#define S3C2410_GPACON	   S3C2410_GPIOREG(0x00)
#define S3C2410_GPADAT	   S3C2410_GPIOREG(0x04)

#define S3C2410_GPA0         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 0)
#define S3C2410_GPA0_OUT     (0<<0)
#define S3C2410_GPA0_ADDR0   (1<<0)

#define S3C2410_GPA1         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 1)
#define S3C2410_GPA1_OUT     (0<<1)
#define S3C2410_GPA1_ADDR16  (1<<1)

#define S3C2410_GPA2         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 2)
#define S3C2410_GPA2_OUT     (0<<2)
#define S3C2410_GPA2_ADDR17  (1<<2)

#define S3C2410_GPA3         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 3)
#define S3C2410_GPA3_OUT     (0<<3)
#define S3C2410_GPA3_ADDR18  (1<<3)

#define S3C2410_GPA4         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 4)
#define S3C2410_GPA4_OUT     (0<<4)
#define S3C2410_GPA4_ADDR19  (1<<4)

#define S3C2410_GPA5         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 5)
#define S3C2410_GPA5_OUT     (0<<5)
#define S3C2410_GPA5_ADDR20  (1<<5)

#define S3C2410_GPA6         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 6)
#define S3C2410_GPA6_OUT     (0<<6)
#define S3C2410_GPA6_ADDR21  (1<<6)

#define S3C2410_GPA7         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 7)
#define S3C2410_GPA7_OUT     (0<<7)
#define S3C2410_GPA7_ADDR22  (1<<7)

#define S3C2410_GPA8         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 8)
#define S3C2410_GPA8_OUT     (0<<8)
#define S3C2410_GPA8_ADDR23  (1<<8)

#define S3C2410_GPA9         S3C2410_GPIONO(S3C2410_GPIO_BANKA, 9)
#define S3C2410_GPA9_OUT     (0<<9)
#define S3C2410_GPA9_ADDR24  (1<<9)

#define S3C2410_GPA10        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 10)
#define S3C2410_GPA10_OUT    (0<<10)
#define S3C2410_GPA10_ADDR25 (1<<10)

#define S3C2410_GPA11        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 11)
#define S3C2410_GPA11_OUT    (0<<11)
#define S3C2410_GPA11_ADDR26 (1<<11)

#define S3C2410_GPA12        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 12)
#define S3C2410_GPA12_OUT    (0<<12)
#define S3C2410_GPA12_nGCS1  (1<<12)

#define S3C2410_GPA13        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 13)
#define S3C2410_GPA13_OUT    (0<<13)
#define S3C2410_GPA13_nGCS2  (1<<13)

#define S3C2410_GPA14        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 14)
#define S3C2410_GPA14_OUT    (0<<14)
#define S3C2410_GPA14_nGCS3  (1<<14)

#define S3C2410_GPA15        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 15)
#define S3C2410_GPA15_OUT    (0<<15)
#define S3C2410_GPA15_nGCS4  (1<<15)

#define S3C2410_GPA16        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 16)
#define S3C2410_GPA16_OUT    (0<<16)
#define S3C2410_GPA16_nGCS5  (1<<16)

#define S3C2410_GPA17        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 17)
#define S3C2410_GPA17_OUT    (0<<17)
#define S3C2410_GPA17_CLE    (1<<17)

#define S3C2410_GPA18        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 18)
#define S3C2410_GPA18_OUT    (0<<18)
#define S3C2410_GPA18_ALE    (1<<18)

#define S3C2410_GPA19        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 19)
#define S3C2410_GPA19_OUT    (0<<19)
#define S3C2410_GPA19_nFWE   (1<<19)

#define S3C2410_GPA20        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 20)
#define S3C2410_GPA20_OUT    (0<<20)
#define S3C2410_GPA20_nFRE   (1<<20)

#define S3C2410_GPA21        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 21)
#define S3C2410_GPA21_OUT    (0<<21)
#define S3C2410_GPA21_nRSTOUT (1<<21)

#define S3C2410_GPA22        S3C2410_GPIONO(S3C2410_GPIO_BANKA, 22)
#define S3C2410_GPA22_OUT    (0<<22)
#define S3C2410_GPA22_nFCE   (1<<22)

/* 0x08 and 0x0c are reserved */

/* GPB is 10 IO pins, each configured by 2 bits each in GPBCON.
 *   00 = input, 01 = output, 10=special function, 11=reserved
 * bit 0,1 = pin 0, 2,3= pin 1...
 *
 * CPBUP = pull up resistor control, 1=disabled, 0=enabled
*/

#define S3C2410_GPBCON	   S3C2410_GPIOREG(0x10)
#define S3C2410_GPBDAT	   S3C2410_GPIOREG(0x14)
#define S3C2410_GPBUP	   S3C2410_GPIOREG(0x18)

/* no i/o pin in port b can have value 3! */

#define S3C2410_GPB0         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 0)
#define S3C2410_GPB0_INP     (0x00 << 0)
#define S3C2410_GPB0_OUTP    (0x01 << 0)
#define S3C2410_GPB0_TOUT0   (0x02 << 0)

#define S3C2410_GPB1         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 1)
#define S3C2410_GPB1_INP     (0x00 << 2)
#define S3C2410_GPB1_OUTP    (0x01 << 2)
#define S3C2410_GPB1_TOUT1   (0x02 << 2)

#define S3C2410_GPB2         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 2)
#define S3C2410_GPB2_INP     (0x00 << 4)
#define S3C2410_GPB2_OUTP    (0x01 << 4)
#define S3C2410_GPB2_TOUT2   (0x02 << 4)

#define S3C2410_GPB3         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 3)
#define S3C2410_GPB3_INP     (0x00 << 6)
#define S3C2410_GPB3_OUTP    (0x01 << 6)
#define S3C2410_GPB3_TOUT3   (0x02 << 6)

#define S3C2410_GPB4         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 4)
#define S3C2410_GPB4_INP     (0x00 << 8)
#define S3C2410_GPB4_OUTP    (0x01 << 8)
#define S3C2410_GPB4_TCLK0   (0x02 << 8)
#define S3C2410_GPB4_MASK    (0x03 << 8)

#define S3C2410_GPB5         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 5)
#define S3C2410_GPB5_INP     (0x00 << 10)
#define S3C2410_GPB5_OUTP    (0x01 << 10)
#define S3C2410_GPB5_nXBACK  (0x02 << 10)

#define S3C2410_GPB6         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 6)
#define S3C2410_GPB6_INP     (0x00 << 12)
#define S3C2410_GPB6_OUTP    (0x01 << 12)
#define S3C2410_GPB6_nXBREQ  (0x02 << 12)

#define S3C2410_GPB7         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 7)
#define S3C2410_GPB7_INP     (0x00 << 14)
#define S3C2410_GPB7_OUTP    (0x01 << 14)
#define S3C2410_GPB7_nXDACK1 (0x02 << 14)

#define S3C2410_GPB8         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 8)
#define S3C2410_GPB8_INP     (0x00 << 16)
#define S3C2410_GPB8_OUTP    (0x01 << 16)
#define S3C2410_GPB8_nXDREQ1 (0x02 << 16)

#define S3C2410_GPB9         S3C2410_GPIONO(S3C2410_GPIO_BANKB, 9)
#define S3C2410_GPB9_INP     (0x00 << 18)
#define S3C2410_GPB9_OUTP    (0x01 << 18)
#define S3C2410_GPB9_nXDACK0 (0x02 << 18)

#define S3C2410_GPB10        S3C2410_GPIONO(S3C2410_GPIO_BANKB, 10)
#define S3C2410_GPB10_INP    (0x00 << 20)
#define S3C2410_GPB10_OUTP   (0x01 << 20)
#define S3C2410_GPB10_nXDRE0 (0x02 << 20)

/* Port C consits of 16 GPIO/Special function
 *
 * almost identical setup to port b, but the special functions are mostly
 * to do with the video system's sync/etc.
*/

#define S3C2410_GPCCON	   S3C2410_GPIOREG(0x20)
#define S3C2410_GPCDAT	   S3C2410_GPIOREG(0x24)
#define S3C2410_GPCUP	   S3C2410_GPIOREG(0x28)

#define S3C2410_GPC0            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 0)
#define S3C2410_GPC0_INP	(0x00 << 0)
#define S3C2410_GPC0_OUTP	(0x01 << 0)
#define S3C2410_GPC0_LEND	(0x02 << 0)

#define S3C2410_GPC1            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 1)
#define S3C2410_GPC1_INP	(0x00 << 2)
#define S3C2410_GPC1_OUTP	(0x01 << 2)
#define S3C2410_GPC1_VCLK	(0x02 << 2)

#define S3C2410_GPC2            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 2)
#define S3C2410_GPC2_INP	(0x00 << 4)
#define S3C2410_GPC2_OUTP	(0x01 << 4)
#define S3C2410_GPC2_VLINE	(0x02 << 4)

#define S3C2410_GPC3            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 3)
#define S3C2410_GPC3_INP	(0x00 << 6)
#define S3C2410_GPC3_OUTP	(0x01 << 6)
#define S3C2410_GPC3_VFRAME	(0x02 << 6)

#define S3C2410_GPC4            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 4)
#define S3C2410_GPC4_INP	(0x00 << 8)
#define S3C2410_GPC4_OUTP	(0x01 << 8)
#define S3C2410_GPC4_VM		(0x02 << 8)

#define S3C2410_GPC5            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 5)
#define S3C2410_GPC5_INP	(0x00 << 10)
#define S3C2410_GPC5_OUTP	(0x01 << 10)
#define S3C2410_GPC5_LCDVF0	(0x02 << 10)

#define S3C2410_GPC6            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 6)
#define S3C2410_GPC6_INP	(0x00 << 12)
#define S3C2410_GPC6_OUTP	(0x01 << 12)
#define S3C2410_GPC6_LCDVF1	(0x02 << 12)

#define S3C2410_GPC7            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 7)
#define S3C2410_GPC7_INP	(0x00 << 14)
#define S3C2410_GPC7_OUTP	(0x01 << 14)
#define S3C2410_GPC7_LCDVF2	(0x02 << 14)

#define S3C2410_GPC8            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 8)
#define S3C2410_GPC8_INP	(0x00 << 16)
#define S3C2410_GPC8_OUTP	(0x01 << 16)
#define S3C2410_GPC8_VD0	(0x02 << 16)

#define S3C2410_GPC9            S3C2410_GPIONO(S3C2410_GPIO_BANKC, 9)
#define S3C2410_GPC9_INP	(0x00 << 18)
#define S3C2410_GPC9_OUTP	(0x01 << 18)
#define S3C2410_GPC9_VD1	(0x02 << 18)

#define S3C2410_GPC10           S3C2410_GPIONO(S3C2410_GPIO_BANKC, 10)
#define S3C2410_GPC10_INP	(0x00 << 20)
#define S3C2410_GPC10_OUTP	(0x01 << 20)
#define S3C2410_GPC10_VD2	(0x02 << 20)

#define S3C2410_GPC11           S3C2410_GPIONO(S3C2410_GPIO_BANKC, 11)
#define S3C2410_GPC11_INP	(0x00 << 22)
#define S3C2410_GPC11_OUTP	(0x01 << 22)
#define S3C2410_GPC11_VD3	(0x02 << 22)

#define S3C2410_GPC12           S3C2410_GPIONO(S3C2410_GPIO_BANKC, 12)
#define S3C2410_GPC12_INP	(0x00 << 24)
#define S3C2410_GPC12_OUTP	(0x01 << 24)
#define S3C2410_GPC12_VD4	(0x02 << 24)

#define S3C2410_GPC13           S3C2410_GPIONO(S3C2410_GPIO_BANKC, 13)
#define S3C2410_GPC13_INP	(0x00 << 26)
#define S3C2410_GPC13_OUTP	(0x01 << 26)
#define S3C2410_GPC13_VD5	(0x02 << 26)

#define S3C2410_GPC14           S3C2410_GPIONO(S3C2410_GPIO_BANKC, 14)
#define S3C2410_GPC14_INP	(0x00 << 28)
#define S3C2410_GPC14_OUTP	(0x01 << 28)
#define S3C2410_GPC14_VD6	(0x02 << 28)

#define S3C2410_GPC15           S3C2410_GPIONO(S3C2410_GPIO_BANKC, 15)
#define S3C2410_GPC15_INP	(0x00 << 30)
#define S3C2410_GPC15_OUTP	(0x01 << 30)
#define S3C2410_GPC15_VD7	(0x02 << 30)

/* Port D consists of 16 GPIO/Special function
 *
 * almost identical setup to port b, but the special functions are mostly
 * to do with the video system's data.
*/

#define S3C2410_GPDCON	   S3C2410_GPIOREG(0x30)
#define S3C2410_GPDDAT	   S3C2410_GPIOREG(0x34)
#define S3C2410_GPDUP	   S3C2410_GPIOREG(0x38)

#define S3C2410_GPD0            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 0)
#define S3C2410_GPD0_INP	(0x00 << 0)
#define S3C2410_GPD0_OUTP	(0x01 << 0)
#define S3C2410_GPD0_VD8	(0x02 << 0)

#define S3C2410_GPD1            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 1)
#define S3C2410_GPD1_INP	(0x00 << 2)
#define S3C2410_GPD1_OUTP	(0x01 << 2)
#define S3C2410_GPD1_VD9	(0x02 << 2)

#define S3C2410_GPD2            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 2)
#define S3C2410_GPD2_INP	(0x00 << 4)
#define S3C2410_GPD2_OUTP	(0x01 << 4)
#define S3C2410_GPD2_VD10	(0x02 << 4)

#define S3C2410_GPD3            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 3)
#define S3C2410_GPD3_INP	(0x00 << 6)
#define S3C2410_GPD3_OUTP	(0x01 << 6)
#define S3C2410_GPD3_VD11	(0x02 << 6)

#define S3C2410_GPD4            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 4)
#define S3C2410_GPD4_INP	(0x00 << 8)
#define S3C2410_GPD4_OUTP	(0x01 << 8)
#define S3C2410_GPD4_VD12	(0x02 << 8)

#define S3C2410_GPD5            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 5)
#define S3C2410_GPD5_INP	(0x00 << 10)
#define S3C2410_GPD5_OUTP	(0x01 << 10)
#define S3C2410_GPD5_VD13	(0x02 << 10)

#define S3C2410_GPD6            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 6)
#define S3C2410_GPD6_INP	(0x00 << 12)
#define S3C2410_GPD6_OUTP	(0x01 << 12)
#define S3C2410_GPD6_VD14	(0x02 << 12)

#define S3C2410_GPD7            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 7)
#define S3C2410_GPD7_INP	(0x00 << 14)
#define S3C2410_GPD7_OUTP	(0x01 << 14)
#define S3C2410_GPD7_VD15	(0x02 << 14)

#define S3C2410_GPD8            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 8)
#define S3C2410_GPD8_INP	(0x00 << 16)
#define S3C2410_GPD8_OUTP	(0x01 << 16)
#define S3C2410_GPD8_VD16	(0x02 << 16)

#define S3C2410_GPD9            S3C2410_GPIONO(S3C2410_GPIO_BANKD, 9)
#define S3C2410_GPD9_INP	(0x00 << 18)
#define S3C2410_GPD9_OUTP	(0x01 << 18)
#define S3C2410_GPD9_VD17	(0x02 << 18)

#define S3C2410_GPD10           S3C2410_GPIONO(S3C2410_GPIO_BANKD, 10)
#define S3C2410_GPD10_INP	(0x00 << 20)
#define S3C2410_GPD10_OUTP	(0x01 << 20)
#define S3C2410_GPD10_VD18	(0x02 << 20)

#define S3C2410_GPD11           S3C2410_GPIONO(S3C2410_GPIO_BANKD, 11)
#define S3C2410_GPD11_INP	(0x00 << 22)
#define S3C2410_GPD11_OUTP	(0x01 << 22)
#define S3C2410_GPD11_VD19	(0x02 << 22)

#define S3C2410_GPD12           S3C2410_GPIONO(S3C2410_GPIO_BANKD, 12)
#define S3C2410_GPD12_INP	(0x00 << 24)
#define S3C2410_GPD12_OUTP	(0x01 << 24)
#define S3C2410_GPD12_VD20	(0x02 << 24)

#define S3C2410_GPD13           S3C2410_GPIONO(S3C2410_GPIO_BANKD, 13)
#define S3C2410_GPD13_INP	(0x00 << 26)
#define S3C2410_GPD13_OUTP	(0x01 << 26)
#define S3C2410_GPD13_VD21	(0x02 << 26)

#define S3C2410_GPD14           S3C2410_GPIONO(S3C2410_GPIO_BANKD, 14)
#define S3C2410_GPD14_INP	(0x00 << 28)
#define S3C2410_GPD14_OUTP	(0x01 << 28)
#define S3C2410_GPD14_VD22	(0x02 << 28)

#define S3C2410_GPD15           S3C2410_GPIONO(S3C2410_GPIO_BANKD, 15)
#define S3C2410_GPD15_INP	(0x00 << 30)
#define S3C2410_GPD15_OUTP	(0x01 << 30)
#define S3C2410_GPD15_VD23	(0x02 << 30)

/* Port E consists of 16 GPIO/Special function
 *
 * again, the same as port B, but dealing with I2S, SDI, and
 * more miscellaneous functions
*/

#define S3C2410_GPECON	   S3C2410_GPIOREG(0x40)
#define S3C2410_GPEDAT	   S3C2410_GPIOREG(0x44)
#define S3C2410_GPEUP	   S3C2410_GPIOREG(0x48)

#define S3C2410_GPE0           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 0)
#define S3C2410_GPE0_INP       (0x00 << 0)
#define S3C2410_GPE0_OUTP      (0x01 << 0)
#define S3C2410_GPE0_I2SLRCK   (0x02 << 0)
#define S3C2410_GPE0_MASK      (0x03 << 0)

#define S3C2410_GPE1           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 1)
#define S3C2410_GPE1_INP       (0x00 << 2)
#define S3C2410_GPE1_OUTP      (0x01 << 2)
#define S3C2410_GPE1_I2SSCLK   (0x02 << 2)
#define S3C2410_GPE1_MASK      (0x03 << 2)

#define S3C2410_GPE2           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 2)
#define S3C2410_GPE2_INP       (0x00 << 4)
#define S3C2410_GPE2_OUTP      (0x01 << 4)
#define S3C2410_GPE2_CDCLK     (0x02 << 4)

#define S3C2410_GPE3           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 3)
#define S3C2410_GPE3_INP       (0x00 << 6)
#define S3C2410_GPE3_OUTP      (0x01 << 6)
#define S3C2410_GPE3_I2SSDI    (0x02 << 6)
#define S3C2410_GPE3_nSS0      (0x03 << 6)
#define S3C2410_GPE3_MASK      (0x03 << 6)

#define S3C2410_GPE4           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 4)
#define S3C2410_GPE4_INP       (0x00 << 8)
#define S3C2410_GPE4_OUTP      (0x01 << 8)
#define S3C2410_GPE4_I2SSDO    (0x02 << 8)
#define S3C2410_GPE4_I2SSDI    (0x03 << 8)
#define S3C2410_GPE4_MASK      (0x03 << 8)

#define S3C2410_GPE5           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 5)
#define S3C2410_GPE5_INP       (0x00 << 10)
#define S3C2410_GPE5_OUTP      (0x01 << 10)
#define S3C2410_GPE5_SDCLK     (0x02 << 10)

#define S3C2410_GPE6           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 6)
#define S3C2410_GPE6_INP       (0x00 << 12)
#define S3C2410_GPE6_OUTP      (0x01 << 12)
#define S3C2410_GPE6_SDCMD     (0x02 << 12)

#define S3C2410_GPE7           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 7)
#define S3C2410_GPE7_INP       (0x00 << 14)
#define S3C2410_GPE7_OUTP      (0x01 << 14)
#define S3C2410_GPE7_SDDAT0    (0x02 << 14)

#define S3C2410_GPE8           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 8)
#define S3C2410_GPE8_INP       (0x00 << 16)
#define S3C2410_GPE8_OUTP      (0x01 << 16)
#define S3C2410_GPE8_SDDAT1    (0x02 << 16)

#define S3C2410_GPE9           S3C2410_GPIONO(S3C2410_GPIO_BANKE, 9)
#define S3C2410_GPE9_INP       (0x00 << 18)
#define S3C2410_GPE9_OUTP      (0x01 << 18)
#define S3C2410_GPE9_SDDAT2    (0x02 << 18)

#define S3C2410_GPE10          S3C2410_GPIONO(S3C2410_GPIO_BANKE, 10)
#define S3C2410_GPE10_INP      (0x00 << 20)
#define S3C2410_GPE10_OUTP     (0x01 << 20)
#define S3C2410_GPE10_SDDAT3   (0x02 << 20)

#define S3C2410_GPE11          S3C2410_GPIONO(S3C2410_GPIO_BANKE, 11)
#define S3C2410_GPE11_INP      (0x00 << 22)
#define S3C2410_GPE11_OUTP     (0x01 << 22)
#define S3C2410_GPE11_SPIMISO0 (0x02 << 22)

#define S3C2410_GPE12          S3C2410_GPIONO(S3C2410_GPIO_BANKE, 12)
#define S3C2410_GPE12_INP      (0x00 << 24)
#define S3C2410_GPE12_OUTP     (0x01 << 24)
#define S3C2410_GPE12_SPIMOSI0 (0x02 << 24)

#define S3C2410_GPE13          S3C2410_GPIONO(S3C2410_GPIO_BANKE, 13)
#define S3C2410_GPE13_INP      (0x00 << 26)
#define S3C2410_GPE13_OUTP     (0x01 << 26)
#define S3C2410_GPE13_SPICLK0  (0x02 << 26)

#define S3C2410_GPE14          S3C2410_GPIONO(S3C2410_GPIO_BANKE, 14)
#define S3C2410_GPE14_INP      (0x00 << 28)
#define S3C2410_GPE14_OUTP     (0x01 << 28)
#define S3C2410_GPE14_IICSCL   (0x02 << 28)
#define S3C2410_GPE14_MASK     (0x03 << 28)

#define S3C2410_GPE15          S3C2410_GPIONO(S3C2410_GPIO_BANKE, 15)
#define S3C2410_GPE15_INP      (0x00 << 30)
#define S3C2410_GPE15_OUTP     (0x01 << 30)
#define S3C2410_GPE15_IICSDA   (0x02 << 30)
#define S3C2410_GPE15_MASK     (0x03 << 30)

#define S3C2440_GPE0_ACSYNC    (0x03 << 0)
#define S3C2440_GPE1_ACBITCLK  (0x03 << 2)
#define S3C2440_GPE2_ACRESET   (0x03 << 4)
#define S3C2440_GPE3_ACIN      (0x03 << 6)
#define S3C2440_GPE4_ACOUT     (0x03 << 8)

#define S3C2410_GPE_PUPDIS(x)  (1<<(x))

/* Port F consists of 8 GPIO/Special function
 *
 * GPIO / interrupt inputs
 *
 * GPFCON has 2 bits for each of the input pins on port F
 *   00 = 0 input, 1 output, 2 interrupt (EINT0..7), 3 undefined
 *
 * pull up works like all other ports.
*/

#define S3C2410_GPFCON	   S3C2410_GPIOREG(0x50)
#define S3C2410_GPFDAT	   S3C2410_GPIOREG(0x54)
#define S3C2410_GPFUP	   S3C2410_GPIOREG(0x58)

#define S3C2410_GPF0        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 0)
#define S3C2410_GPF0_INP    (0x00 << 0)
#define S3C2410_GPF0_OUTP   (0x01 << 0)
#define S3C2410_GPF0_EINT0  (0x02 << 0)

#define S3C2410_GPF1        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 1)
#define S3C2410_GPF1_INP    (0x00 << 2)
#define S3C2410_GPF1_OUTP   (0x01 << 2)
#define S3C2410_GPF1_EINT1  (0x02 << 2)

#define S3C2410_GPF2        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 2)
#define S3C2410_GPF2_INP    (0x00 << 4)
#define S3C2410_GPF2_OUTP   (0x01 << 4)
#define S3C2410_GPF2_EINT2  (0x02 << 4)

#define S3C2410_GPF3        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 3)
#define S3C2410_GPF3_INP    (0x00 << 6)
#define S3C2410_GPF3_OUTP   (0x01 << 6)
#define S3C2410_GPF3_EINT3  (0x02 << 6)

#define S3C2410_GPF4        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 4)
#define S3C2410_GPF4_INP    (0x00 << 8)
#define S3C2410_GPF4_OUTP   (0x01 << 8)
#define S3C2410_GPF4_EINT4  (0x02 << 8)

#define S3C2410_GPF5        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 5)
#define S3C2410_GPF5_INP    (0x00 << 10)
#define S3C2410_GPF5_OUTP   (0x01 << 10)
#define S3C2410_GPF5_EINT5  (0x02 << 10)

#define S3C2410_GPF6        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 6)
#define S3C2410_GPF6_INP    (0x00 << 12)
#define S3C2410_GPF6_OUTP   (0x01 << 12)
#define S3C2410_GPF6_EINT6  (0x02 << 12)

#define S3C2410_GPF7        S3C2410_GPIONO(S3C2410_GPIO_BANKF, 7)
#define S3C2410_GPF7_INP    (0x00 << 14)
#define S3C2410_GPF7_OUTP   (0x01 << 14)
#define S3C2410_GPF7_EINT7  (0x02 << 14)

/* Port G consists of 8 GPIO/IRQ/Special function
 *
 * GPGCON has 2 bits for each of the input pins on port F
 *   00 = 0 input, 1 output, 2 interrupt (EINT0..7), 3 special func
 *
 * pull up works like all other ports.
*/

#define S3C2410_GPGCON	   S3C2410_GPIOREG(0x60)
#define S3C2410_GPGDAT	   S3C2410_GPIOREG(0x64)
#define S3C2410_GPGUP	   S3C2410_GPIOREG(0x68)

#define S3C2410_GPG0          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 0)
#define S3C2410_GPG0_INP      (0x00 << 0)
#define S3C2410_GPG0_OUTP     (0x01 << 0)
#define S3C2410_GPG0_EINT8    (0x02 << 0)

#define S3C2410_GPG1          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 1)
#define S3C2410_GPG1_INP      (0x00 << 2)
#define S3C2410_GPG1_OUTP     (0x01 << 2)
#define S3C2410_GPG1_EINT9    (0x02 << 2)

#define S3C2410_GPG2          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 2)
#define S3C2410_GPG2_INP      (0x00 << 4)
#define S3C2410_GPG2_OUTP     (0x01 << 4)
#define S3C2410_GPG2_EINT10   (0x02 << 4)

#define S3C2410_GPG3          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 3)
#define S3C2410_GPG3_INP      (0x00 << 6)
#define S3C2410_GPG3_OUTP     (0x01 << 6)
#define S3C2410_GPG3_EINT11   (0x02 << 6)

#define S3C2410_GPG4          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 4)
#define S3C2410_GPG4_INP      (0x00 << 8)
#define S3C2410_GPG4_OUTP     (0x01 << 8)
#define S3C2410_GPG4_EINT12   (0x02 << 8)
#define S3C2410_GPG4_LCDPWREN (0x03 << 8)

#define S3C2410_GPG5          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 5)
#define S3C2410_GPG5_INP      (0x00 << 10)
#define S3C2410_GPG5_OUTP     (0x01 << 10)
#define S3C2410_GPG5_EINT13   (0x02 << 10)
#define S3C2410_GPG5_SPIMISO1 (0x03 << 10)

#define S3C2410_GPG6          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 6)
#define S3C2410_GPG6_INP      (0x00 << 12)
#define S3C2410_GPG6_OUTP     (0x01 << 12)
#define S3C2410_GPG6_EINT14   (0x02 << 12)
#define S3C2410_GPG6_SPIMOSI1 (0x03 << 12)

#define S3C2410_GPG7          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 7)
#define S3C2410_GPG7_INP      (0x00 << 14)
#define S3C2410_GPG7_OUTP     (0x01 << 14)
#define S3C2410_GPG7_EINT15   (0x02 << 14)
#define S3C2410_GPG7_SPICLK1  (0x03 << 14)

#define S3C2410_GPG8          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 8)
#define S3C2410_GPG8_INP      (0x00 << 16)
#define S3C2410_GPG8_OUTP     (0x01 << 16)
#define S3C2410_GPG8_EINT16   (0x02 << 16)

#define S3C2410_GPG9          S3C2410_GPIONO(S3C2410_GPIO_BANKG, 9)
#define S3C2410_GPG9_INP      (0x00 << 18)
#define S3C2410_GPG9_OUTP     (0x01 << 18)
#define S3C2410_GPG9_EINT17   (0x02 << 18)

#define S3C2410_GPG10         S3C2410_GPIONO(S3C2410_GPIO_BANKG, 10)
#define S3C2410_GPG10_INP     (0x00 << 20)
#define S3C2410_GPG10_OUTP    (0x01 << 20)
#define S3C2410_GPG10_EINT18  (0x02 << 20)

#define S3C2410_GPG11         S3C2410_GPIONO(S3C2410_GPIO_BANKG, 11)
#define S3C2410_GPG11_INP     (0x00 << 22)
#define S3C2410_GPG11_OUTP    (0x01 << 22)
#define S3C2410_GPG11_EINT19  (0x02 << 22)
#define S3C2410_GPG11_TCLK1   (0x03 << 22)

#define S3C2410_GPG12         S3C2410_GPIONO(S3C2410_GPIO_BANKG, 12)
#define S3C2410_GPG12_INP     (0x00 << 24)
#define S3C2410_GPG12_OUTP    (0x01 << 24)
#define S3C2410_GPG12_EINT20  (0x02 << 24)
#define S3C2410_GPG12_XMON    (0x03 << 24)

#define S3C2410_GPG13         S3C2410_GPIONO(S3C2410_GPIO_BANKG, 13)
#define S3C2410_GPG13_INP     (0x00 << 26)
#define S3C2410_GPG13_OUTP    (0x01 << 26)
#define S3C2410_GPG13_EINT21  (0x02 << 26)
#define S3C2410_GPG13_nXPON   (0x03 << 26)

#define S3C2410_GPG14         S3C2410_GPIONO(S3C2410_GPIO_BANKG, 14)
#define S3C2410_GPG14_INP     (0x00 << 28)
#define S3C2410_GPG14_OUTP    (0x01 << 28)
#define S3C2410_GPG14_EINT22  (0x02 << 28)
#define S3C2410_GPG14_YMON    (0x03 << 28)

#define S3C2410_GPG15         S3C2410_GPIONO(S3C2410_GPIO_BANKG, 15)
#define S3C2410_GPG15_INP     (0x00 << 30)
#define S3C2410_GPG15_OUTP    (0x01 << 30)
#define S3C2410_GPG15_EINT23  (0x02 << 30)
#define S3C2410_GPG15_nYPON   (0x03 << 30)


#define S3C2410_GPG_PUPDIS(x)  (1<<(x))

/* Port H consists of11 GPIO/serial/Misc pins
 *
 * GPGCON has 2 bits for each of the input pins on port F
 *   00 = 0 input, 1 output, 2 interrupt (EINT0..7), 3 special func
 *
 * pull up works like all other ports.
*/

#define S3C2410_GPHCON	   S3C2410_GPIOREG(0x70)
#define S3C2410_GPHDAT	   S3C2410_GPIOREG(0x74)
#define S3C2410_GPHUP	   S3C2410_GPIOREG(0x78)

#define S3C2410_GPH0        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 0)
#define S3C2410_GPH0_INP    (0x00 << 0)
#define S3C2410_GPH0_OUTP   (0x01 << 0)
#define S3C2410_GPH0_nCTS0  (0x02 << 0)

#define S3C2410_GPH1        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 1)
#define S3C2410_GPH1_INP    (0x00 << 2)
#define S3C2410_GPH1_OUTP   (0x01 << 2)
#define S3C2410_GPH1_nRTS0  (0x02 << 2)

#define S3C2410_GPH2        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 2)
#define S3C2410_GPH2_INP    (0x00 << 4)
#define S3C2410_GPH2_OUTP   (0x01 << 4)
#define S3C2410_GPH2_TXD0   (0x02 << 4)

#define S3C2410_GPH3        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 3)
#define S3C2410_GPH3_INP    (0x00 << 6)
#define S3C2410_GPH3_OUTP   (0x01 << 6)
#define S3C2410_GPH3_RXD0   (0x02 << 6)

#define S3C2410_GPH4        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 4)
#define S3C2410_GPH4_INP    (0x00 << 8)
#define S3C2410_GPH4_OUTP   (0x01 << 8)
#define S3C2410_GPH4_TXD1   (0x02 << 8)

#define S3C2410_GPH5        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 5)
#define S3C2410_GPH5_INP    (0x00 << 10)
#define S3C2410_GPH5_OUTP   (0x01 << 10)
#define S3C2410_GPH5_RXD1   (0x02 << 10)

#define S3C2410_GPH6        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 6)
#define S3C2410_GPH6_INP    (0x00 << 12)
#define S3C2410_GPH6_OUTP   (0x01 << 12)
#define S3C2410_GPH6_TXD2   (0x02 << 12)
#define S3C2410_GPH6_nRTS1  (0x03 << 12)

#define S3C2410_GPH7        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 7)
#define S3C2410_GPH7_INP    (0x00 << 14)
#define S3C2410_GPH7_OUTP   (0x01 << 14)
#define S3C2410_GPH7_RXD2   (0x02 << 14)
#define S3C2410_GPH7_nCTS1  (0x03 << 14)

#define S3C2410_GPH8        S3C2410_GPIONO(S3C2410_GPIO_BANKH, 8)
#define S3C2410_GPH8_INP    (0x00 << 16)
#define S3C2410_GPH8_OUTP   (0x01 << 16)
#define S3C2410_GPH8_UCLK   (0x02 << 16)

#define S3C2410_GPH9          S3C2410_GPIONO(S3C2410_GPIO_BANKH, 9)
#define S3C2410_GPH9_INP      (0x00 << 18)
#define S3C2410_GPH9_OUTP     (0x01 << 18)
#define S3C2410_GPH9_CLKOUT0  (0x02 << 18)

#define S3C2410_GPH10         S3C2410_GPIONO(S3C2410_GPIO_BANKH, 10)
#define S3C2410_GPH10_INP     (0x00 << 20)
#define S3C2410_GPH10_OUTP    (0x01 << 20)
#define S3C2410_GPH10_CLKOUT1 (0x02 << 20)

/* miscellaneous control */

#define S3C2410_MISCCR	   S3C2410_GPIOREG(0x80)
#define S3C2410_DCLKCON	   S3C2410_GPIOREG(0x84)

/* see clock.h for dclk definitions */

/* pullup control on databus */
#define S3C2410_MISCCR_SPUCR_HEN    (0)
#define S3C2410_MISCCR_SPUCR_HDIS   (1<<0)
#define S3C2410_MISCCR_SPUCR_LEN    (0)
#define S3C2410_MISCCR_SPUCR_LDIS   (1<<1)

#define S3C2410_MISCCR_USBDEV	    (0)
#define S3C2410_MISCCR_USBHOST	    (1<<3)

#define S3C2410_MISCCR_CLK0_MPLL    (0<<4)
#define S3C2410_MISCCR_CLK0_UPLL    (1<<4)
#define S3C2410_MISCCR_CLK0_FCLK    (2<<4)
#define S3C2410_MISCCR_CLK0_HCLK    (3<<4)
#define S3C2410_MISCCR_CLK0_PCLK    (4<<4)
#define S3C2410_MISCCR_CLK0_DCLK0   (5<<4)

#define S3C2410_MISCCR_CLK1_MPLL    (0<<8)
#define S3C2410_MISCCR_CLK1_UPLL    (1<<8)
#define S3C2410_MISCCR_CLK1_FCLK    (2<<8)
#define S3C2410_MISCCR_CLK1_HCLK    (3<<8)
#define S3C2410_MISCCR_CLK1_PCLK    (4<<8)
#define S3C2410_MISCCR_CLK1_DCLK1   (5<<8)

#define S3C2410_MISCCR_USBSUSPND0   (1<<12)
#define S3C2410_MISCCR_USBSUSPND1   (1<<13)

#define S3C2410_MISCCR_nRSTCON	    (1<<16)

#define S3C2410_MISCCR_nEN_SCLK0    (1<<17)
#define S3C2410_MISCCR_nEN_SCLK1    (1<<18)
#define S3C2410_MISCCR_nEN_SCLKE    (1<<19)
#define S3C2410_MISCCR_SDSLEEP	    (7<<17)

/* external interrupt control... */
/* S3C2410_EXTINT0 -> irq sense control for EINT0..EINT7
 * S3C2410_EXTINT1 -> irq sense control for EINT8..EINT15
 * S3C2410_EXTINT2 -> irq sense control for EINT16..EINT23
 *
 * note S3C2410_EXTINT2 has filtering options for EINT16..EINT23
 *
 * Samsung datasheet p9-25
*/

#define S3C2410_EXTINT0	   S3C2410_GPIOREG(0x88)
#define S3C2410_EXTINT1	   S3C2410_GPIOREG(0x8C)
#define S3C2410_EXTINT2	   S3C2410_GPIOREG(0x90)

/* values for S3C2410_EXTINT0/1/2 */
#define S3C2410_EXTINT_LOWLEV	 (0x00)
#define S3C2410_EXTINT_HILEV	 (0x01)
#define S3C2410_EXTINT_FALLEDGE	 (0x02)
#define S3C2410_EXTINT_RISEEDGE	 (0x04)
#define S3C2410_EXTINT_BOTHEDGE	 (0x06)

/* interrupt filtering conrrol for EINT16..EINT23 */
#define S3C2410_EINFLT0	   S3C2410_GPIOREG(0x94)
#define S3C2410_EINFLT1	   S3C2410_GPIOREG(0x98)
#define S3C2410_EINFLT2	   S3C2410_GPIOREG(0x9C)
#define S3C2410_EINFLT3	   S3C2410_GPIOREG(0xA0)

/* values for interrupt filtering */
#define S3C2410_EINTFLT_PCLK		(0x00)
#define S3C2410_EINTFLT_EXTCLK		(1<<7)
#define S3C2410_EINTFLT_WIDTHMSK(x)	((x) & 0x3f)

/* removed EINTxxxx defs from here, not meant for this */

/* GSTATUS have miscellaneous information in them
 *
 */

#define S3C2410_GSTATUS0   S3C2410_GPIOREG(0x0AC)
#define S3C2410_GSTATUS1   S3C2410_GPIOREG(0x0B0)
#define S3C2410_GSTATUS2   S3C2410_GPIOREG(0x0B4)
#define S3C2410_GSTATUS3   S3C2410_GPIOREG(0x0B8)
#define S3C2410_GSTATUS4   S3C2410_GPIOREG(0x0BC)

#define S3C2410_GSTATUS0_nWAIT	   (1<<3)
#define S3C2410_GSTATUS0_NCON	   (1<<2)
#define S3C2410_GSTATUS0_RnB	   (1<<1)
#define S3C2410_GSTATUS0_nBATTFLT  (1<<0)

#define S3C2410_GSTATUS1_IDMASK	   (0xffff0000)
#define S3C2410_GSTATUS1_2410	   (0x32410000)
#define S3C2410_GSTATUS1_2440	   (0x32440000)

#define S3C2410_GSTATUS2_WTRESET   (1<<2)
#define S3C2410_GSTATUS2_OFFRESET  (1<<1)
#define S3C2410_GSTATUS2_PONRESET  (1<<0)

#endif	/* __ASM_ARCH_REGS_GPIO_H */

