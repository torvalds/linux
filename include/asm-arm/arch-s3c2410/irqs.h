/* linux/include/asm-arm/arch-s3c2410/irqs.h
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  12-May-2003 BJD  Created file
 *  08-Jan-2003 BJD  Linux 2.6.0 version, moved BAST bits out
 *  12-Mar-2004 BJD  Fixed bug in header protection
 *  10-Feb-2005 BJD  Added camera IRQ from guillaume.gourat@nexvision.tv
 *  28-Feb-2005 BJD  Updated s3c2440 IRQs
 */


#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H __FILE__


/* we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 */

#define S3C2410_CPUIRQ_OFFSET	 (16)

#define S3C2410_IRQ(x) ((x) + S3C2410_CPUIRQ_OFFSET)

/* main cpu interrupts */
#define IRQ_EINT0      S3C2410_IRQ(0)	    /* 16 */
#define IRQ_EINT1      S3C2410_IRQ(1)
#define IRQ_EINT2      S3C2410_IRQ(2)
#define IRQ_EINT3      S3C2410_IRQ(3)
#define IRQ_EINT4t7    S3C2410_IRQ(4)	    /* 20 */
#define IRQ_EINT8t23   S3C2410_IRQ(5)
#define IRQ_RESERVED6  S3C2410_IRQ(6)	    /* for s3c2410 */
#define IRQ_CAM        S3C2410_IRQ(6)	    /* for s3c2440 */
#define IRQ_BATT_FLT   S3C2410_IRQ(7)
#define IRQ_TICK       S3C2410_IRQ(8)	    /* 24 */
#define IRQ_WDT	       S3C2410_IRQ(9)
#define IRQ_TIMER0     S3C2410_IRQ(10)
#define IRQ_TIMER1     S3C2410_IRQ(11)
#define IRQ_TIMER2     S3C2410_IRQ(12)
#define IRQ_TIMER3     S3C2410_IRQ(13)
#define IRQ_TIMER4     S3C2410_IRQ(14)
#define IRQ_UART2      S3C2410_IRQ(15)
#define IRQ_LCD	       S3C2410_IRQ(16)	    /* 32 */
#define IRQ_DMA0       S3C2410_IRQ(17)
#define IRQ_DMA1       S3C2410_IRQ(18)
#define IRQ_DMA2       S3C2410_IRQ(19)
#define IRQ_DMA3       S3C2410_IRQ(20)
#define IRQ_SDI	       S3C2410_IRQ(21)
#define IRQ_SPI0       S3C2410_IRQ(22)
#define IRQ_UART1      S3C2410_IRQ(23)
#define IRQ_RESERVED24 S3C2410_IRQ(24)	    /* 40 */
#define IRQ_NFCON      S3C2410_IRQ(24)	    /* for s3c2440 */
#define IRQ_USBD       S3C2410_IRQ(25)
#define IRQ_USBH       S3C2410_IRQ(26)
#define IRQ_IIC	       S3C2410_IRQ(27)
#define IRQ_UART0      S3C2410_IRQ(28)	    /* 44 */
#define IRQ_SPI1       S3C2410_IRQ(29)
#define IRQ_RTC	       S3C2410_IRQ(30)
#define IRQ_ADCPARENT  S3C2410_IRQ(31)

/* interrupts generated from the external interrupts sources */
#define IRQ_EINT4      S3C2410_IRQ(32)	   /* 48 */
#define IRQ_EINT5      S3C2410_IRQ(33)
#define IRQ_EINT6      S3C2410_IRQ(34)
#define IRQ_EINT7      S3C2410_IRQ(35)
#define IRQ_EINT8      S3C2410_IRQ(36)
#define IRQ_EINT9      S3C2410_IRQ(37)
#define IRQ_EINT10     S3C2410_IRQ(38)
#define IRQ_EINT11     S3C2410_IRQ(39)
#define IRQ_EINT12     S3C2410_IRQ(40)
#define IRQ_EINT13     S3C2410_IRQ(41)
#define IRQ_EINT14     S3C2410_IRQ(42)
#define IRQ_EINT15     S3C2410_IRQ(43)
#define IRQ_EINT16     S3C2410_IRQ(44)
#define IRQ_EINT17     S3C2410_IRQ(45)
#define IRQ_EINT18     S3C2410_IRQ(46)
#define IRQ_EINT19     S3C2410_IRQ(47)
#define IRQ_EINT20     S3C2410_IRQ(48)	   /* 64 */
#define IRQ_EINT21     S3C2410_IRQ(49)
#define IRQ_EINT22     S3C2410_IRQ(50)
#define IRQ_EINT23     S3C2410_IRQ(51)


#define IRQ_EINT(x)    S3C2410_IRQ((x >= 4) ? (IRQ_EINT4 + (x) - 4) : (S3C2410_IRQ(0) + (x)))

#define IRQ_LCD_FIFO   S3C2410_IRQ(52)
#define IRQ_LCD_FRAME  S3C2410_IRQ(53)

/* IRQs for the interal UARTs, and ADC
 * these need to be ordered in number of appearance in the
 * SUBSRC mask register
*/
#define IRQ_S3CUART_RX0  S3C2410_IRQ(54)   /* 70 */
#define IRQ_S3CUART_TX0  S3C2410_IRQ(55)   /* 71 */
#define IRQ_S3CUART_ERR0 S3C2410_IRQ(56)

#define IRQ_S3CUART_RX1  S3C2410_IRQ(57)
#define IRQ_S3CUART_TX1  S3C2410_IRQ(58)
#define IRQ_S3CUART_ERR1 S3C2410_IRQ(59)

#define IRQ_S3CUART_RX2  S3C2410_IRQ(60)
#define IRQ_S3CUART_TX2  S3C2410_IRQ(61)
#define IRQ_S3CUART_ERR2 S3C2410_IRQ(62)

#define IRQ_TC		 S3C2410_IRQ(63)
#define IRQ_ADC		 S3C2410_IRQ(64)

/* extra irqs for s3c2440 */

#define IRQ_S3C2440_CAM_C	S3C2410_IRQ(65)
#define IRQ_S3C2440_CAM_P	S3C2410_IRQ(66)
#define IRQ_S3C2440_WDT		S3C2410_IRQ(67)
#define IRQ_S3C2440_AC97	S3C2410_IRQ(68)

#define NR_IRQS (IRQ_S3C2440_AC97+1)


#endif /* __ASM_ARCH_IRQ_H */
