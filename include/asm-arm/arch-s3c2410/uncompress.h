/* linux/include/asm-arm/arch-s3c2410/uncompress.h
 *
 * (c) 2003 Simtec Electronics
 *    Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - uncompress code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  22-May-2003 BJD  Created
 *  08-Sep-2003 BJD  Moved to linux v2.6
 *  12-Mar-2004 BJD  Updated header protection
 *  12-Oct-2004 BJD  Take account of debug uart configuration
 *  15-Nov-2004 BJD  Fixed uart configuration
 *  22-Feb-2005 BJD  Added watchdog to uncompress
 *  04-Apr-2005 LCVR Added support to S3C2400 (no cpuid at GSTATUS1)
*/

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <linux/config.h>

/* defines for UART registers */
#include "asm/arch/regs-serial.h"
#include "asm/arch/regs-gpio.h"
#include "asm/arch/regs-watchdog.h"

#include <asm/arch/map.h>

/* working in physical space... */
#undef S3C2410_GPIOREG
#undef S3C2410_WDOGREG

#define S3C2410_GPIOREG(x) ((S3C2410_PA_GPIO + (x)))
#define S3C2410_WDOGREG(x) ((S3C2410_PA_WATCHDOG + (x)))

/* how many bytes we allow into the FIFO at a time in FIFO mode */
#define FIFO_MAX	 (14)

#define uart_base S3C2410_PA_UART + (0x4000*CONFIG_S3C2410_LOWLEVEL_UART_PORT)

static __inline__ void
uart_wr(unsigned int reg, unsigned int val)
{
	volatile unsigned int *ptr;

	ptr = (volatile unsigned int *)(reg + uart_base);
	*ptr = val;
}

static __inline__ unsigned int
uart_rd(unsigned int reg)
{
	volatile unsigned int *ptr;

	ptr = (volatile unsigned int *)(reg + uart_base);
	return *ptr;
}


/* we can deal with the case the UARTs are being run
 * in FIFO mode, so that we don't hold up our execution
 * waiting for tx to happen...
*/

static void
putc(char ch)
{
	int cpuid = S3C2410_GSTATUS1_2410;

#ifndef CONFIG_CPU_S3C2400
	cpuid = *((volatile unsigned int *)S3C2410_GSTATUS1);
	cpuid &= S3C2410_GSTATUS1_IDMASK;
#endif

	if (ch == '\n')
		putc('\r');    /* expand newline to \r\n */

	if (uart_rd(S3C2410_UFCON) & S3C2410_UFCON_FIFOMODE) {
		int level;

		while (1) {
			level = uart_rd(S3C2410_UFSTAT);

			if (cpuid == S3C2410_GSTATUS1_2440) {
				level &= S3C2440_UFSTAT_TXMASK;
				level >>= S3C2440_UFSTAT_TXSHIFT;
			} else {
				level &= S3C2410_UFSTAT_TXMASK;
				level >>= S3C2410_UFSTAT_TXSHIFT;
			}

			if (level < FIFO_MAX)
				break;
		}

	} else {
		/* not using fifos */

		while ((uart_rd(S3C2410_UTRSTAT) & S3C2410_UTRSTAT_TXE) != S3C2410_UTRSTAT_TXE);
	}

	/* write byte to transmission register */
	uart_wr(S3C2410_UTXH, ch);
}

static void
putstr(const char *ptr)
{
	for (; *ptr != '\0'; ptr++) {
		putc(*ptr);
	}
}

/* CONFIG_S3C2410_BOOT_WATCHDOG
 *
 * Simple boot-time watchdog setup, to reboot the system if there is
 * any problem with the boot process
*/

#ifdef CONFIG_S3C2410_BOOT_WATCHDOG

#define WDOG_COUNT (0xff00)

#define __raw_writel(d,ad) do { *((volatile unsigned int *)(ad)) = (d); } while(0)

static inline void arch_decomp_wdog(void)
{
	__raw_writel(WDOG_COUNT, S3C2410_WTCNT);
}

static void arch_decomp_wdog_start(void)
{
	__raw_writel(WDOG_COUNT, S3C2410_WTDAT);
	__raw_writel(WDOG_COUNT, S3C2410_WTCNT);
	__raw_writel(S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV128 | S3C2410_WTCON_RSTEN | S3C2410_WTCON_PRESCALE(0x40), S3C2410_WTCON);
}

#else
#define arch_decomp_wdog_start()
#define arch_decomp_wdog()
#endif

static void error(char *err);

static void
arch_decomp_setup(void)
{
	/* we may need to setup the uart(s) here if we are not running
	 * on an BAST... the BAST will have left the uarts configured
	 * after calling linux.
	 */

	arch_decomp_wdog_start();
}


#endif /* __ASM_ARCH_UNCOMPRESS_H */
