/* $Id: irq.h,v 1.21 2002/01/23 11:27:36 davem Exp $
 * irq.h: IRQ registers on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
 */

#ifndef _SPARC64_IRQ_H
#define _SPARC64_IRQ_H

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <asm/pil.h>
#include <asm/ptrace.h>

struct ino_bucket;

#define MAX_IRQ_DESC_ACTION	4

struct irq_desc {
	void			(*pre_handler)(struct ino_bucket *, void *, void *);
	void			*pre_handler_arg1;
	void			*pre_handler_arg2;
	u32			action_active_mask;
	struct irqaction	action[MAX_IRQ_DESC_ACTION];
};

/* You should not mess with this directly. That's the job of irq.c.
 *
 * If you make changes here, please update hand coded assembler of
 * the vectored interrupt trap handler in entry.S -DaveM
 *
 * This is currently one DCACHE line, two buckets per L2 cache
 * line.  Keep this in mind please.
 */
struct ino_bucket {
	/* Next handler in per-CPU PIL worklist.  We know that
	 * bucket pointers have the high 32-bits clear, so to
	 * save space we only store the bits we need.
	 */
/*0x00*/unsigned int irq_chain;

	/* PIL to schedule this IVEC at. */
/*0x04*/unsigned char pil;

	/* If an IVEC arrives while irq_info is NULL, we
	 * set this to notify request_irq() about the event.
	 */
/*0x05*/unsigned char pending;

	/* Miscellaneous flags. */
/*0x06*/unsigned char flags;

	/* Currently unused.  */
/*0x07*/unsigned char __pad;

	/* Reference to IRQ descriptor for this bucket. */
/*0x08*/struct irq_desc *irq_info;

	/* Sun5 Interrupt Clear Register. */
/*0x10*/unsigned long iclr;

	/* Sun5 Interrupt Mapping Register. */
/*0x18*/unsigned long imap;

};

/* IMAP/ICLR register defines */
#define IMAP_VALID		0x80000000	/* IRQ Enabled		*/
#define IMAP_TID_UPA		0x7c000000	/* UPA TargetID		*/
#define IMAP_TID_JBUS		0x7c000000	/* JBUS TargetID	*/
#define IMAP_TID_SHIFT		26
#define IMAP_AID_SAFARI		0x7c000000	/* Safari AgentID	*/
#define IMAP_AID_SHIFT		26
#define IMAP_NID_SAFARI		0x03e00000	/* Safari NodeID	*/
#define IMAP_NID_SHIFT		21
#define IMAP_IGN		0x000007c0	/* IRQ Group Number	*/
#define IMAP_INO		0x0000003f	/* IRQ Number		*/
#define IMAP_INR		0x000007ff	/* Full interrupt number*/

#define ICLR_IDLE		0x00000000	/* Idle state		*/
#define ICLR_TRANSMIT		0x00000001	/* Transmit state	*/
#define ICLR_PENDING		0x00000003	/* Pending state	*/

/* Only 8-bits are available, be careful.  -DaveM */
#define IBF_PCI		0x02	/* PSYCHO/SABRE/SCHIZO PCI interrupt.	 */
#define IBF_ACTIVE	0x04	/* Interrupt is active and has a handler.*/
#define IBF_INPROGRESS	0x10	/* IRQ is being serviced.		 */

#define NUM_IVECS	(IMAP_INR + 1)
extern struct ino_bucket ivector_table[NUM_IVECS];

#define __irq_ino(irq) \
        (((struct ino_bucket *)(unsigned long)(irq)) - &ivector_table[0])
#define __irq_pil(irq) ((struct ino_bucket *)(unsigned long)(irq))->pil
#define __bucket(irq) ((struct ino_bucket *)(unsigned long)(irq))
#define __irq(bucket) ((unsigned int)(unsigned long)(bucket))

#define NR_IRQS    16

#define irq_canonicalize(irq)	(irq)
extern void disable_irq(unsigned int);
#define disable_irq_nosync disable_irq
extern void enable_irq(unsigned int);
extern unsigned int build_irq(int pil, int inofixup, unsigned long iclr, unsigned long imap);
extern unsigned int sun4v_build_irq(u32 devhandle, unsigned int devino, int pil, unsigned char flags);
extern unsigned int sbus_build_irq(void *sbus, unsigned int ino);

static __inline__ void set_softint(unsigned long bits)
{
	__asm__ __volatile__("wr	%0, 0x0, %%set_softint"
			     : /* No outputs */
			     : "r" (bits));
}

static __inline__ void clear_softint(unsigned long bits)
{
	__asm__ __volatile__("wr	%0, 0x0, %%clear_softint"
			     : /* No outputs */
			     : "r" (bits));
}

static __inline__ unsigned long get_softint(void)
{
	unsigned long retval;

	__asm__ __volatile__("rd	%%softint, %0"
			     : "=r" (retval));
	return retval;
}

struct irqaction;
struct pt_regs;
int handle_IRQ_event(unsigned int, struct pt_regs *, struct irqaction *);

#endif
