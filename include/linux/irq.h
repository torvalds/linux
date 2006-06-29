#ifndef _LINUX_IRQ_H
#define _LINUX_IRQ_H

/*
 * Please do not include this file in generic code.  There is currently
 * no requirement for any architecture to implement anything held
 * within this file.
 *
 * Thanks. --rmk
 */

#include <linux/smp.h>

#ifndef CONFIG_S390

#include <linux/linkage.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <linux/irqreturn.h>

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
#ifdef CONFIG_IRQ_PER_CPU
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
	const char	*typename;
	unsigned int	(*startup)(unsigned int irq);
	void		(*shutdown)(unsigned int irq);
	void		(*enable)(unsigned int irq);
	void		(*disable)(unsigned int irq);
	void		(*ack)(unsigned int irq);
	void		(*end)(unsigned int irq);
	void		(*set_affinity)(unsigned int irq, cpumask_t dest);
	int		(*retrigger)(unsigned int irq);

	/* Currently used only by UML, might disappear one day.*/
#ifdef CONFIG_IRQ_RELEASE_METHOD
	void		(*release)(unsigned int irq, void *dev_id);
#endif
};

typedef struct hw_interrupt_type  hw_irq_controller;

struct proc_dir_entry;

/*
 * This is the "IRQ descriptor", which contains various information
 * about the irq, including what kind of hardware handling it has,
 * whether it is disabled etc etc.
 *
 * Pad this out to 32 bytes for cache and indexing reasons.
 */
struct irq_desc {
	hw_irq_controller	*chip;
	void			*chip_data;
	struct irqaction	*action;	/* IRQ action list */
	unsigned int		status;		/* IRQ status */
	unsigned int		depth;		/* nested irq disables */
	unsigned int		irq_count;	/* For detecting broken IRQs */
	unsigned int		irqs_unhandled;
	spinlock_t		lock;
#ifdef CONFIG_SMP
	cpumask_t		affinity;
#endif
#if defined(CONFIG_GENERIC_PENDING_IRQ) || defined(CONFIG_IRQBALANCE)
	cpumask_t		pending_mask;
	unsigned int		move_irq;	/* need to re-target IRQ dest */
#endif
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *dir;
#endif
} ____cacheline_aligned;

extern struct irq_desc irq_desc[NR_IRQS];

/*
 * Migration helpers for obsolete names, they will go away:
 */
typedef struct irq_desc		irq_desc_t;

/*
 * Pick up the arch-dependent methods:
 */
#include <asm/hw_irq.h>

extern int setup_irq(unsigned int irq, struct irqaction *new);

#ifdef CONFIG_GENERIC_HARDIRQS

#ifdef CONFIG_SMP
static inline void set_native_irq_info(int irq, cpumask_t mask)
{
	irq_desc[irq].affinity = mask;
}
#else
static inline void set_native_irq_info(int irq, cpumask_t mask)
{
}
#endif

#ifdef CONFIG_SMP

#if defined(CONFIG_GENERIC_PENDING_IRQ) || defined(CONFIG_IRQBALANCE)

void set_pending_irq(unsigned int irq, cpumask_t mask);
void move_native_irq(int irq);

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

#else /* CONFIG_PCI_MSI */

static inline void move_irq(int irq)
{
	move_native_irq(irq);
}

static inline void set_irq_info(int irq, cpumask_t mask)
{
	set_native_irq_info(irq, mask);
}

#endif /* CONFIG_PCI_MSI */

#else /* CONFIG_GENERIC_PENDING_IRQ || CONFIG_IRQBALANCE */

static inline void move_irq(int irq)
{
}

static inline void move_native_irq(int irq)
{
}

static inline void set_pending_irq(unsigned int irq, cpumask_t mask)
{
}

static inline void set_irq_info(int irq, cpumask_t mask)
{
	set_native_irq_info(irq, mask);
}

#endif /* CONFIG_GENERIC_PENDING_IRQ */

#else /* CONFIG_SMP */

#define move_irq(x)
#define move_native_irq(x)

#endif /* CONFIG_SMP */

#ifdef CONFIG_IRQBALANCE
extern void set_balance_irq_affinity(unsigned int irq, cpumask_t mask);
#else
static inline void set_balance_irq_affinity(unsigned int irq, cpumask_t mask)
{
}
#endif

#ifdef CONFIG_AUTO_IRQ_AFFINITY
extern int select_smp_affinity(unsigned int irq);
#else
static inline int select_smp_affinity(unsigned int irq)
{
	return 1;
}
#endif

extern int no_irq_affinity;
extern int noirqdebug_setup(char *str);

extern irqreturn_t handle_IRQ_event(unsigned int irq, struct pt_regs *regs,
				    struct irqaction *action);
/*
 * Explicit fastcall, because i386 4KSTACKS calls it from assembly:
 */
extern fastcall unsigned int __do_IRQ(unsigned int irq, struct pt_regs *regs);

extern void note_interrupt(unsigned int irq, struct irq_desc *desc,
			   int action_ret, struct pt_regs *regs);
extern int can_request_irq(unsigned int irq, unsigned long irqflags);

extern void init_irq_proc(void);

#endif /* CONFIG_GENERIC_HARDIRQS */

extern hw_irq_controller no_irq_type;  /* needed in every arch ? */

#endif /* !CONFIG_S390 */

#endif /* _LINUX_IRQ_H */
