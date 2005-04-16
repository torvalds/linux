/*
 * include/asm-v850/gbus_int.h -- Midas labs GBUS interrupt support
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_GBUS_INT_H__
#define __V850_GBUS_INT_H__


/* The GBUS interrupt interface has 32 interrupts shared among 4
   processor interrupts.  The 32 GBUS interrupts are divided into two
   sets of 16 each, for allocating among control registers, etc (there
   are two of each control register, with bits 0-15 controlling an
   interrupt each).  */

/* The GBUS interrupts themselves.  */
#define IRQ_GBUS_INT(n)		(GBUS_INT_BASE_IRQ + (n))
#define IRQ_GBUS_INT_NUM	32

/* Control registers.  */
#define GBUS_INT_STATUS_ADDR(w)	(GBUS_INT_BASE_ADDR + (w)*0x40)
#define GBUS_INT_STATUS(w)	(*(volatile u16 *)GBUS_INT_STATUS_ADDR(w))
#define GBUS_INT_CLEAR_ADDR(w)	(GBUS_INT_BASE_ADDR + 0x10 + (w)*0x40)
#define GBUS_INT_CLEAR(w)	(*(volatile u16 *)GBUS_INT_CLEAR_ADDR(w))
#define GBUS_INT_EDGE_ADDR(w)	(GBUS_INT_BASE_ADDR + 0x20 + (w)*0x40)
#define GBUS_INT_EDGE(w)	(*(volatile u16 *)GBUS_INT_EDGE_ADDR(w))
#define GBUS_INT_POLARITY_ADDR(w)	(GBUS_INT_BASE_ADDR + 0x30 + (w)*0x40)
#define GBUS_INT_POLARITY(w)	(*(volatile u16 *)GBUS_INT_POLARITY_ADDR(w))
/* This allows enabling interrupt bits in word W for interrupt GINTn.  */
#define GBUS_INT_ENABLE_ADDR(w, n) \
   (GBUS_INT_BASE_ADDR + 0x100 + (w)*0x10 + (n)*0x20)
#define GBUS_INT_ENABLE(w, n)	(*(volatile u16 *)GBUS_INT_ENABLE_ADDR(w, n))

/* Mapping between kernel interrupt numbers and hardware control regs/bits.  */
#define GBUS_INT_BITS_PER_WORD	16
#define GBUS_INT_NUM_WORDS	(IRQ_GBUS_INT_NUM / GBUS_INT_BITS_PER_WORD)
#define GBUS_INT_IRQ_WORD(irq)	(((irq) - GBUS_INT_BASE_IRQ) >> 4)
#define GBUS_INT_IRQ_BIT(irq)	(((irq) - GBUS_INT_BASE_IRQ) & 0xF)
#define GBUS_INT_IRQ_MASK(irq)	(1 << GBUS_INT_IRQ_BIT(irq))


/* Possible priorities for GBUS interrupts.  */
#define GBUS_INT_PRIORITY_HIGH		2
#define GBUS_INT_PRIORITY_MEDIUM	4
#define GBUS_INT_PRIORITY_LOW		6


#ifndef __ASSEMBLY__

/* Enable interrupt handling for interrupt IRQ.  */
extern void gbus_int_enable_irq (unsigned irq);
/* Disable interrupt handling for interrupt IRQ.  Note that any
   interrupts received while disabled will be delivered once the
   interrupt is enabled again, unless they are explicitly cleared using
   `gbus_int_clear_pending_irq'.  */
extern void gbus_int_disable_irq (unsigned irq);
/* Return true if interrupt handling for interrupt IRQ is enabled.  */
extern int gbus_int_irq_enabled (unsigned irq);
/* Disable all GBUS irqs.  */
extern void gbus_int_disable_irqs (void);
/* Clear any pending interrupts for IRQ.  */
extern void gbus_int_clear_pending_irq (unsigned irq);
/* Return true if interrupt IRQ is pending (but disabled).  */
extern int gbus_int_irq_pending (unsigned irq);


struct gbus_int_irq_init {
	const char *name;	/* name of interrupt type */

	/* Range of kernel irq numbers for this type:
	   BASE, BASE+INTERVAL, ..., BASE+INTERVAL*NUM  */
	unsigned base, num, interval;

	unsigned priority;	/* interrupt priority to assign */
};
struct hw_interrupt_type;	/* fwd decl */

/* Initialize HW_IRQ_TYPES for GBUS irqs described in array
   INITS (which is terminated by an entry with the name field == 0).  */
extern void gbus_int_init_irq_types (struct gbus_int_irq_init *inits,
				     struct hw_interrupt_type *hw_irq_types);

/* Initialize GBUS interrupts.  */
extern void gbus_int_init_irqs (void);

#endif /* !__ASSEMBLY__ */


#endif /* __V850_GBUS_INT_H__ */
