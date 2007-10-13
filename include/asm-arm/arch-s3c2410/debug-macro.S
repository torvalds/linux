/* linux/include/asm-arm/arch-s3c2410/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Copyright (C) 2005 Simtec Electronics
 *
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <asm/arch/map.h>
#include <asm/arch/regs-gpio.h>
#include <asm/plat-s3c/regs-serial.h>

#define S3C2410_UART1_OFF (0x4000)
#define SHIFT_2440TXF (14-9)

	.macro addruart, rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1
		ldreq	\rx, = S3C24XX_PA_UART
		ldrne	\rx, = S3C24XX_VA_UART
#if CONFIG_DEBUG_S3C_UART != 0
		add	\rx, \rx, #(S3C2410_UART1_OFF * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

	.macro fifo_full_s3c24xx rd, rx
		@ check for arm920 vs arm926. currently assume all arm926
		@ devices have an 64 byte FIFO identical to the s3c2440
		mrc	p15, 0, \rd, c0, c0
		and	\rd, \rd, #0xff0
		teq	\rd, #0x260
		beq	1004f
		mrc	p15, 0, \rd, c1, c0
		tst	\rd, #1
		addeq	\rd, \rx, #(S3C24XX_PA_GPIO - S3C24XX_PA_UART)
		addne	\rd, \rx, #(S3C24XX_VA_GPIO - S3C24XX_VA_UART)
		bic	\rd, \rd, #0xff000
		ldr	\rd, [ \rd, # S3C2410_GSTATUS1 - S3C2410_GPIOREG(0) ]
		and	\rd, \rd, #0x00ff0000
		teq	\rd, #0x00440000		@ is it 2440?
1004:
		ldr	\rd, [ \rx, # S3C2410_UFSTAT ]
		moveq	\rd, \rd, lsr #SHIFT_2440TXF
		tst	\rd, #S3C2410_UFSTAT_TXFULL
	.endm

	.macro  fifo_full_s3c2410 rd, rx
		ldr	\rd, [ \rx, # S3C2410_UFSTAT ]
		tst	\rd, #S3C2410_UFSTAT_TXFULL
	.endm

/* fifo level reading */

	.macro fifo_level_s3c24xx rd, rx
		@ check for arm920 vs arm926. currently assume all arm926
		@ devices have an 64 byte FIFO identical to the s3c2440
		mrc	p15, 0, \rd, c0, c0
		and	\rd, \rd, #0xff0
		teq	\rd, #0x260
		beq	10000f
		mrc	p15, 0, \rd, c1, c0
		tst	\rd, #1
		addeq	\rd, \rx, #(S3C24XX_PA_GPIO - S3C24XX_PA_UART)
		addne	\rd, \rx, #(S3C24XX_VA_GPIO - S3C24XX_VA_UART)
		bic	\rd, \rd, #0xff000
		ldr	\rd, [ \rd, # S3C2410_GSTATUS1 - S3C2410_GPIOREG(0) ]
		and	\rd, \rd, #0x00ff0000
		teq	\rd, #0x00440000		@ is it 2440?

10000:
		ldr	\rd, [ \rx, # S3C2410_UFSTAT ]
		andne	\rd, \rd, #S3C2410_UFSTAT_TXMASK
		andeq	\rd, \rd, #S3C2440_UFSTAT_TXMASK
	.endm

	.macro fifo_level_s3c2410 rd, rx
		ldr	\rd, [ \rx, # S3C2410_UFSTAT ]
		and	\rd, \rd, #S3C2410_UFSTAT_TXMASK
	.endm

/* Select the correct implementation depending on the configuration. The
 * S3C2440 will get selected by default, as these are the most widely
 * used variants of these
*/

#if defined(CONFIG_CPU_LLSERIAL_S3C2410_ONLY)
#define fifo_full  fifo_full_s3c2410
#define fifo_level fifo_level_s3c2410
#warning 2410only
#elif !defined(CONFIG_CPU_LLSERIAL_S3C2440_ONLY)
#define fifo_full  fifo_full_s3c24xx
#define fifo_level fifo_level_s3c24xx
#warning generic
#endif

/* include the reset of the code which will do the work */

#include <asm/plat-s3c/debug-macro.S>
