/* linux/include/asm-arm/arch-s3c2410/system.h
 *
 * Copyright (c) 2003 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - System function defines and includes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/arch/map.h>
#include <asm/arch/idle.h>
#include <asm/arch/reset.h>

#include <asm/plat-s3c/regs-watchdog.h>
#include <asm/arch/regs-clock.h>

#include <linux/clk.h>
#include <linux/err.h>

void (*s3c24xx_idle)(void);
void (*s3c24xx_reset_hook)(void);

void s3c24xx_default_idle(void)
{
	unsigned long tmp;
	int i;

	/* idle the system by using the idle mode which will wait for an
	 * interrupt to happen before restarting the system.
	 */

	/* Warning: going into idle state upsets jtag scanning */

	__raw_writel(__raw_readl(S3C2410_CLKCON) | S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);

	/* the samsung port seems to do a loop and then unset idle.. */
	for (i = 0; i < 50; i++) {
		tmp += __raw_readl(S3C2410_CLKCON); /* ensure loop not optimised out */
	}

	/* this bit is not cleared on re-start... */

	__raw_writel(__raw_readl(S3C2410_CLKCON) & ~S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);
}

static void arch_idle(void)
{
	if (s3c24xx_idle != NULL)
		(s3c24xx_idle)();
	else
		s3c24xx_default_idle();
}

static void
arch_reset(char mode)
{
	struct clk *wdtclk;

	if (mode == 's') {
		cpu_reset(0);
	}

	if (s3c24xx_reset_hook)
		s3c24xx_reset_hook();

	printk("arch_reset: attempting watchdog reset\n");

	__raw_writel(0, S3C2410_WTCON);	  /* disable watchdog, to be safe  */

	wdtclk = clk_get(NULL, "watchdog");
	if (!IS_ERR(wdtclk)) {
		clk_enable(wdtclk);
	} else
		printk(KERN_WARNING "%s: warning: cannot get watchdog clock\n", __func__);

	/* put initial values into count and data */
	__raw_writel(0x80, S3C2410_WTCNT);
	__raw_writel(0x80, S3C2410_WTDAT);

	/* set the watchdog to go and reset... */
	__raw_writel(S3C2410_WTCON_ENABLE|S3C2410_WTCON_DIV16|S3C2410_WTCON_RSTEN |
		     S3C2410_WTCON_PRESCALE(0x20), S3C2410_WTCON);

	/* wait for reset to assert... */
	mdelay(500);

	printk(KERN_ERR "Watchdog reset failed to assert reset\n");

	/* delay to allow the serial port to show the message */
	mdelay(50);

	/* we'll take a jump through zero as a poor second */
	cpu_reset(0);
}
