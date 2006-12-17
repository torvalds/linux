/* linux/include/asm-arm/arch-s3c2410/h1940-latch.h
 *
 * Copyright (c) 2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *  iPAQ H1940 series - latch definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_H1940_LATCH_H
#define __ASM_ARCH_H1940_LATCH_H


#ifndef __ASSEMBLY__
#define H1940_LATCH		((void __iomem *)0xF8000000)
#else
#define H1940_LATCH		0xF8000000
#endif

#define H1940_PA_LATCH		(S3C2410_CS2)

/* SD layer latch */

#define H1940_LATCH_SDQ1		(1<<16)
#define H1940_LATCH_LCD_P1		(1<<17)
#define H1940_LATCH_LCD_P2		(1<<18)
#define H1940_LATCH_LCD_P3		(1<<19)
#define H1940_LATCH_MAX1698_nSHUTDOWN	(1<<20)		/* LCD backlight */
#define H1940_LATCH_LED_RED		(1<<21)
#define H1940_LATCH_SDQ7		(1<<22)
#define H1940_LATCH_USB_DP		(1<<23)

/* CPU layer latch */

#define H1940_LATCH_UDA_POWER		(1<<24)
#define H1940_LATCH_AUDIO_POWER		(1<<25)
#define H1940_LATCH_SM803_ENABLE	(1<<26)
#define H1940_LATCH_LCD_P4		(1<<27)
#define H1940_LATCH_CPUQ5		(1<<28)		/* untraced */
#define H1940_LATCH_BLUETOOTH_POWER	(1<<29)		/* active high */
#define H1940_LATCH_LED_GREEN		(1<<30)
#define H1940_LATCH_LED_FLASH		(1<<31)

/* default settings */

#define H1940_LATCH_DEFAULT		\
	H1940_LATCH_LCD_P4		| \
	H1940_LATCH_SM803_ENABLE	| \
	H1940_LATCH_SDQ1		| \
	H1940_LATCH_LCD_P1		| \
	H1940_LATCH_LCD_P2		| \
	H1940_LATCH_LCD_P3		| \
	H1940_LATCH_MAX1698_nSHUTDOWN   | \
	H1940_LATCH_CPUQ5

/* control functions */

extern void h1940_latch_control(unsigned int clear, unsigned int set);

#endif /* __ASM_ARCH_H1940_LATCH_H */
