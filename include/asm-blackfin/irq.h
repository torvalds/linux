/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed by HuTao Apr18, 2003
 *
 * Copyright was missing when I got the code so took from MIPS arch ...MaTed---
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 2001 by Ralf Baechle
 *
 * Adapted for BlackFin (ADI) by Ted Ma <mated@sympatico.ca>
 * Copyright (c) 2002 Arcturus Networks Inc. (www.arcturusnetworks.com)
 * Copyright (c) 2002 Lineo, Inc. <mattw@lineo.com>
 */

#ifndef _BFIN_IRQ_H_
#define _BFIN_IRQ_H_

#include <asm/mach/irq.h>
#include <asm/ptrace.h>

/*******************************************************************************
 *****   INTRODUCTION ***********
 *   On the Blackfin, the interrupt structure allows remmapping of the hardware
 *   levels.
 * - I'm going to assume that the H/W level is going to stay at the default
 *   settings. If someone wants to go through and abstart this out, feel free
 *   to mod the interrupt numbering scheme.
 * - I'm abstracting the interrupts so that uClinux does not know anything
 *   about the H/W levels. If you want to change the H/W AND keep the abstracted
 *   levels that uClinux sees, you should be able to do most of it here.
 * - I've left the "abstract" numbering sparce in case someone wants to pull the
 *   interrupts apart (just the TX/RX for the various devices)
 *******************************************************************************/

/* SYS_IRQS and NR_IRQS are defined in <asm/mach-bf5xx/irq.h>*/

/*
 * Machine specific interrupt sources.
 *
 * Adding an interrupt service routine for a source with this bit
 * set indicates a special machine specific interrupt source.
 * The machine specific files define these sources.
 *
 * The IRQ_MACHSPEC bit is now gone - the only thing it did was to
 * introduce unnecessary overhead.
 *
 * All interrupt handling is actually machine specific so it is better
 * to use function pointers, as used by the Sparc port, and select the
 * interrupt handling functions when initializing the kernel. This way
 * we save some unnecessary overhead at run-time.
 *                                                      01/11/97 - Jes
 */

extern void ack_bad_irq(unsigned int irq);

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

/* count of spurious interrupts */
/* extern volatile unsigned int num_spurious; */

#ifndef NO_IRQ
#define NO_IRQ ((unsigned int)(-1))
#endif

#endif				/* _BFIN_IRQ_H_ */
