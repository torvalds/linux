/*
 * DaVinci system defines
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/io.h>
#include <asm/hardware.h>

extern void davinci_watchdog_reset(void);

static void arch_idle(void)
{
	cpu_do_idle();
}

static void arch_reset(char mode)
{
	davinci_watchdog_reset();
}

#endif /* __ASM_ARCH_SYSTEM_H */
