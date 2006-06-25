#ifndef __ASM_SH64_IRQ_H
#define __ASM_SH64_IRQ_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/irq.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */


/*
 * Encoded IRQs are not considered worth to be supported.
 * Main reason is that there's no per-encoded-interrupt
 * enable/disable mechanism (as there was in SH3/4).
 * An all enabled/all disabled is worth only if there's
 * a cascaded IC to disable/enable/ack on. Until such
 * IC is available there's no such support.
 *
 * Presumably Encoded IRQs may use extra IRQs beyond 64,
 * below. Some logic must be added to cope with IRQ_IRL?
 * in an exclusive way.
 *
 * Priorities are set at Platform level, when IRQ_IRL0-3
 * are set to 0 Encoding is allowed. Otherwise it's not
 * allowed.
 */

/* Independent IRQs */
#define IRQ_IRL0	0
#define IRQ_IRL1	1
#define IRQ_IRL2	2
#define IRQ_IRL3	3

#define IRQ_INTA	4
#define IRQ_INTB	5
#define IRQ_INTC	6
#define IRQ_INTD	7

#define IRQ_SERR	12
#define IRQ_ERR		13
#define IRQ_PWR3	14
#define IRQ_PWR2	15
#define IRQ_PWR1	16
#define IRQ_PWR0	17

#define IRQ_DMTE0	18
#define IRQ_DMTE1	19
#define IRQ_DMTE2	20
#define IRQ_DMTE3	21
#define IRQ_DAERR	22

#define IRQ_TUNI0	32
#define IRQ_TUNI1	33
#define IRQ_TUNI2	34
#define IRQ_TICPI2	35

#define IRQ_ATI		36
#define IRQ_PRI		37
#define IRQ_CUI		38

#define IRQ_ERI		39
#define IRQ_RXI		40
#define IRQ_BRI		41
#define IRQ_TXI		42

#define IRQ_ITI		63

#define NR_INTC_IRQS	64

#ifdef CONFIG_SH_CAYMAN
#define NR_EXT_IRQS     32
#define START_EXT_IRQS  64

/* PCI bus 2 uses encoded external interrupts on the Cayman board */
#define IRQ_P2INTA      (START_EXT_IRQS + (3*8) + 0)
#define IRQ_P2INTB      (START_EXT_IRQS + (3*8) + 1)
#define IRQ_P2INTC      (START_EXT_IRQS + (3*8) + 2)
#define IRQ_P2INTD      (START_EXT_IRQS + (3*8) + 3)

#define I8042_KBD_IRQ	(START_EXT_IRQS + 2)
#define I8042_AUX_IRQ	(START_EXT_IRQS + 6)

#define IRQ_CFCARD	(START_EXT_IRQS + 7)
#define IRQ_PCMCIA	(0)

#else
#define NR_EXT_IRQS	0
#endif

#define NR_IRQS		(NR_INTC_IRQS+NR_EXT_IRQS)


/* Default IRQs, fixed */
#define TIMER_IRQ	IRQ_TUNI0
#define RTC_IRQ		IRQ_CUI

/* Default Priorities, Platform may choose differently */
#define	NO_PRIORITY	0	/* Disabled */
#define TIMER_PRIORITY	2
#define RTC_PRIORITY	TIMER_PRIORITY
#define SCIF_PRIORITY	3
#define INTD_PRIORITY	3
#define	IRL3_PRIORITY	4
#define INTC_PRIORITY	6
#define	IRL2_PRIORITY	7
#define INTB_PRIORITY	9
#define	IRL1_PRIORITY	10
#define INTA_PRIORITY	12
#define	IRL0_PRIORITY	13
#define TOP_PRIORITY	15

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

extern int intc_evt_to_irq[(0xE20/0x20)+1];
int intc_irq_describe(char* p, int irq);

#define irq_canonicalize(irq)	(irq)

#ifdef CONFIG_SH_CAYMAN
int cayman_irq_demux(int evt);
int cayman_irq_describe(char* p, int irq);
#define irq_demux(x) cayman_irq_demux(x)
#define irq_describe(p, x) cayman_irq_describe(p, x)
#else
#define irq_demux(x) (intc_evt_to_irq[x])
#define irq_describe(p, x) intc_irq_describe(p, x)
#endif

/*
 * Function for "on chip support modules".
 */

/*
 * SH-5 supports Priority based interrupts only.
 * Interrupt priorities are defined at platform level.
 */
#define set_ipr_data(a, b, c, d)
#define make_ipr_irq(a)
#define make_imask_irq(a)

#endif /* __ASM_SH64_IRQ_H */
