/*
 * MTD primitives for XIP support
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 2, 2004
 * Copyright:	(C) 2004 MontaVista Software, Inc.
 *
 * This XIP support for MTD has been loosely inspired
 * by an earlier patch authored by David Woodhouse.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MTD_XIP_H__
#define __LINUX_MTD_XIP_H__


#ifdef CONFIG_MTD_XIP

/*
 * We really don't want gcc to guess anything.
 * We absolutely _need_ proper inlining.
 */
#include <linux/compiler.h>

/*
 * Function that are modifying the flash state away from array mode must
 * obviously not be running from flash.  The __xipram is therefore marking
 * those functions so they get relocated to ram.
 */
#define __xipram noinline __attribute__ ((__section__ (".data")))

/*
 * Each architecture has to provide the following macros.  They must access
 * the hardware directly and not rely on any other (XIP) functions since they
 * won't be available when used (flash not in array mode).
 *
 * xip_irqpending()
 *
 * 	return non zero when any hardware interrupt is pending.
 *
 * xip_currtime()
 *
 * 	return a platform specific time reference to be used with
 * 	xip_elapsed_since().
 *
 * xip_elapsed_since(x)
 *
 * 	return in usecs the elapsed timebetween now and the reference x as
 * 	returned by xip_currtime().
 *
 * 	note 1: convertion to usec can be approximated, as long as the
 * 		returned value is <= the real elapsed time.
 * 	note 2: this should be able to cope with a few seconds without
 * 		overflowing.
 *
 * xip_iprefetch()
 *
 *      Macro to fill instruction prefetch
 *	e.g. a series of nops:  asm volatile (".rep 8; nop; .endr");
 */

#include <asm/mtd-xip.h>

#ifndef xip_irqpending

#warning "missing IRQ and timer primitives for XIP MTD support"
#warning "some of the XIP MTD support code will be disabled"
#warning "your system will therefore be unresponsive when writing or erasing flash"

#define xip_irqpending()	(0)
#define xip_currtime()		(0)
#define xip_elapsed_since(x)	(0)

#endif

#ifndef xip_iprefetch
#define xip_iprefetch()		do { } while (0)
#endif

/*
 * xip_cpu_idle() is used when waiting for a delay equal or larger than
 * the system timer tick period.  This should put the CPU into idle mode
 * to save power and to be woken up only when some interrupts are pending.
 * This should not rely upon standard kernel code.
 */
#ifndef xip_cpu_idle
#define xip_cpu_idle()  do { } while (0)
#endif

#else

#define __xipram

#endif /* CONFIG_MTD_XIP */

#endif /* __LINUX_MTD_XIP_H__ */
