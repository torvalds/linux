#ifndef __irq_h
#define __irq_h

/*
 * Please do not include this file in generic code.  There is currently
 * no requirement for any architecture to implement anything held
 * within this file.
 *
 * Thanks. --rmk
 */

#include <linux/config.h>
#include <asm/smp.h>		/* cpu_online_map */

#if !defined(CONFIG_ARCH_S390)

#include <linux/linkage.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>

#include <asm/irq.h>
#include <asm/ptrace.h>

/*
 * IRQ line status.
 */
#define IRQ_INPROGRESS	1	/* IRQ handler active - do not enter! */
#define IRQ_DISABLED	2	/* IRQ disabled - do not enter! */
#define IRQ_PENDING	4	/* IRQ pending - replay on enable */
#define IRQ_REPLAY	8	/* IRQ has been replayed but not acked yet */
#define IRQ_AUTODETECT	16	/* IRQ is being autodetected */
#define IRQ_WAITING	32	/* IRQ not yet seen - for autodetection */
#define IRQ_LEVEL	64	/* IRQ level triggered */
#define IRQ_MASKED	128	/* IRQ masked - shouldn't be seen again */
#if defined(ARCH_HAS_IRQ_PER_CPU)
# define IRQ_PER_CPU	256	/* IRQ is per CPU */
# define CHECK_IRQ_PER_CPU(var) ((var) & IRQ_PER_CPU)
#else
# define CHECK_IRQ_PER_CPU(var) 0
#endif

/*
 * Interrupt controller descriptor. This is all we need
 * to describe about the low-level hardware. 
 */
struct hw_interrupt_type {
	const char * typename;
	unsigned int (*startup)(unsigned int irq);
	void (*shutdown)(unsigned int irq);
	void (*enable)(unsigned int irq);
	void (*disable)(unsigned int irq);
	void (*ack)(unsigned int irq);
	void (*end)(unsigned int irq);
	void (*set_affinity)(unsigned int irq, cpumask_t dest);
	/* Currently used only by UML, might disappear one day.*/
#ifdef CONFIG_IRQ_RELEASE_METHOD
	void (*release)(unsigned int irq, void *dev_id);
#endif
};

typedef struct hw_interrupt_type  hw_irq_controller;

/*
 * This is the "IRQ descriptor", which contains various information
 * about the irq, including what kind of hardware handling it has,
 * whether it is disabled etc etc.
 *
 * Pad this out to 32 bytes for cache and indexing reasons.
 */
typedef struct irq_desc {
	hw_irq_controller *handler;
	void *handler_data;
	struct irqaction *action;	/* IRQ action list */
	unsigned int status;		/* IRQ status */
	unsigned int depth;		/* nested irq disables */
	unsigned int irq_count;		/* For detecting broken interrupts */
	unsigned int irqs_unhandled;
	spinlock_t lock;
#if defined (CONFIG_GENERIC_PENDING_IRQ) || defined (CONFIG_IRQBALANCE)
	unsigned int move_irq;		/* Flag need to re-target intr dest*/
#endif
} ____cacheline_aligned irq_desc_t;

extern irq_desc_t irq_desc [NR_IRQS];

/* Return a pointer to the irq descriptor for IRQ.  */
static inline irq_desc_t *
irq_descp (int irq)
{
	return irq_desc + irq;
}

#include <asm/hw_irq.h> /* the arch dependent stuff */

extern int setup_irq(unsigned int irq, struct irqaction * new);

#ifdef CONFIG_GENERIC_HARDIRQS
extern cpumask_t irq_affinity[NR_IRQS];

#ifdef CONFIG_SMP
static inline void set_native_irq_info(int irq, cpumask_t mask)
{
	irq_affinity[irq] = mask;
}
#else
static inline void set_native_irq_info(int irq, cpumask_t mask)
{
}
#endif

#ifdef CONFIG_SMP

#if defined (CONFIG_GENERIC_PENDING_IRQ) || defined (CONFIG_IRQBALANCE)
extern cpumask_t pending_irq_cpumask[NR_IRQS];

static inline void set_pending_irq(unsigned int irq, cpumask_t mask)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	desc->move_irq = 1;
	pending_irq_cpumask[irq] = mask;
	spin_unlock_irqrestore(&desc->lock, flags);
}

static inline void
move_native_irq(int irq)
{
	cpumask_t tmp;
	irq_desc_t *desc = irq_descp(irq);

	if (likely (!desc->move_irq))
		return;

	desc->move_irq = 0;

	if (likely(cpus_empty(pending_irq_cpumask[irq])))
		return;

	if (!desc->handler->set_affinity)
		return;

	/* note - we hold the desc->lock */
	cpus_and(tmp, pending_irq_cpumask[irq], cpu_online_map);

	/*
	 * If there was a valid mask to work with, please
	 * do the disable, re-program, enable sequence.
	 * This is *not* particularly important for level triggered
	 * but in a edge trigger case, we might be setting rte
	 * when an active trigger is comming in. This could
	 * cause some ioapics to mal-function.
	 * Being paranoid i guess!
	 */
	if (unlikely(!cpus_empty(tmp))) {
		desc->handler->disable(irq);
		desc->handler->set_affinity(irq,tmp);
		desc->handler->enable(irq);
	}
	cpus_clear(pending_irq_cpumask[irq]);
}

#ifdef CONFIG_PCI_MSI
/*
 * Wonder why these are dummies?
 * For e.g the set_ioapic_affinity_vector() calls the set_ioapic_affinity_irq()
 * counter part after translating the vector to irq info. We need to perform
 * this operation on the real irq, when we dont use vector, i.e when
 * pci_use_vector() is false.
 */
static inline void move_irq(int irq)
{
}

static inline void set_irq_info(int irq, cpumask_t mask)
{
}

#else // CONFIG_PCI_MSI

static inline void move_irq(int irq)
{
	move_native_irq(irq);
}

static inline void set_irq_info(int irq, cpumask_t mask)
{
	set_native_irq_info(irq, mask);
}
#endif // CONFIG_PCI_MSI

#else	// CONFIG_GENERIC_PENDING_IRQ || CONFIG_IRQBALANCE

#define move_irq(x)
#define move_native_irq(x)
#define set_pending_irq(x,y)
static inline void set_irq_info(int irq, cpumask_t mask)
{
	set_native_irq_info(irq, mask);
}

#endif // CONFIG_GENERIC_PENDING_IRQ

#else // CONFIG_SMP

#define move_irq(x)
#define move_native_irq(x)

#endif // CONFIG_SMP

extern int no_irq_affinity;
extern int noirqdebug_setup(char *str);

extern fastcall int handle_IRQ_event(unsigned int irq, struct pt_regs *regs,
					struct irqaction *action);
extern fastcall unsigned int __do_IRQ(unsigned int irq, struct pt_regs *regs);
extern void note_interrupt(unsigned int irq, irq_desc_t *desc,
					int action_ret, struct pt_regs *regs);
extern int can_request_irq(unsigned int irq, unsigned long irqflags);

extern void init_irq_proc(void);
#endif

extern hw_irq_controller no_irq_type;  /* needed in every arch ? */

#endif

#endif /* __irq_h */
