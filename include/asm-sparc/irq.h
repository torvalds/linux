/* $Id: irq.h,v 1.32 2000/08/26 02:42:28 anton Exp $
 * irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

#include <linux/linkage.h>
#include <linux/threads.h>     /* For NR_CPUS */
#include <linux/interrupt.h>

#include <asm/system.h>     /* For SUN4M_NCPUS */
#include <asm/btfixup.h>

#define NR_IRQS    16

#define irq_canonicalize(irq)	(irq)

/* Dave Redman (djhr@tadpole.co.uk)
 * changed these to function pointers.. it saves cycles and will allow
 * the irq dependencies to be split into different files at a later date
 * sun4c_irq.c, sun4m_irq.c etc so we could reduce the kernel size.
 * Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Changed these to btfixup entities... It saves cycles :)
 */
BTFIXUPDEF_CALL(void, disable_irq, unsigned int)
BTFIXUPDEF_CALL(void, enable_irq, unsigned int)

static inline void disable_irq_nosync(unsigned int irq)
{
	BTFIXUP_CALL(disable_irq)(irq);
}

static inline void disable_irq(unsigned int irq)
{
	BTFIXUP_CALL(disable_irq)(irq);
}

static inline void enable_irq(unsigned int irq)
{
	BTFIXUP_CALL(enable_irq)(irq);
}

extern int request_fast_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, __const__ char *devname);

#endif
