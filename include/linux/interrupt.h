/* SPDX-License-Identifier: GPL-2.0 */
/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <linux/irqnr.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/hrtimer.h>
#include <linux/kref.h>
#include <linux/workqueue.h>
#include <linux/jump_label.h>

#include <linux/atomic.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/sections.h>

/*
 * These correspond to the IORESOURCE_IRQ_* defines in
 * linux/ioport.h to select the interrupt line behaviour.  When
 * requesting an interrupt without specifying a IRQF_TRIGGER, the
 * setting should be assumed to be "as already configured", which
 * may be as per machine or firmware initialisation.
 */
#define IRQF_TRIGGER_NONE	0x00000000
#define IRQF_TRIGGER_RISING	0x00000001
#define IRQF_TRIGGER_FALLING	0x00000002
#define IRQF_TRIGGER_HIGH	0x00000004
#define IRQF_TRIGGER_LOW	0x00000008
#define IRQF_TRIGGER_MASK	(IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW | \
				 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)
#define IRQF_TRIGGER_PROBE	0x00000010

/*
 * These flags used only by the kernel as part of the
 * irq handling routines.
 *
 * IRQF_SHARED - allow sharing the irq among several devices
 * IRQF_PROBE_SHARED - set by callers when they expect sharing mismatches to occur
 * IRQF_TIMER - Flag to mark this interrupt as timer interrupt
 * IRQF_PERCPU - Interrupt is per cpu
 * IRQF_NOBALANCING - Flag to exclude this interrupt from irq balancing
 * IRQF_IRQPOLL - Interrupt is used for polling (only the interrupt that is
 *                registered first in a shared interrupt is considered for
 *                performance reasons)
 * IRQF_ONESHOT - Interrupt is not reenabled after the hardirq handler finished.
 *                Used by threaded interrupts which need to keep the
 *                irq line disabled until the threaded handler has been run.
 * IRQF_NO_SUSPEND - Do not disable this IRQ during suspend.  Does not guarantee
 *                   that this interrupt will wake the system from a suspended
 *                   state.  See Documentation/power/suspend-and-interrupts.rst
 * IRQF_FORCE_RESUME - Force enable it on resume even if IRQF_NO_SUSPEND is set
 * IRQF_NO_THREAD - Interrupt cannot be threaded
 * IRQF_EARLY_RESUME - Resume IRQ early during syscore instead of at device
 *                resume time.
 * IRQF_COND_SUSPEND - If the IRQ is shared with a NO_SUSPEND user, execute this
 *                interrupt handler after suspending interrupts. For system
 *                wakeup devices users need to implement wakeup detection in
 *                their interrupt handlers.
 * IRQF_NO_AUTOEN - Don't enable IRQ or NMI automatically when users request it.
 *                Users will enable it explicitly by enable_irq() or enable_nmi()
 *                later.
 * IRQF_NO_DEBUG - Exclude from runnaway detection for IPI and similar handlers,
 *		   depends on IRQF_PERCPU.
 */
#define IRQF_SHARED		0x00000080
#define IRQF_PROBE_SHARED	0x00000100
#define __IRQF_TIMER		0x00000200
#define IRQF_PERCPU		0x00000400
#define IRQF_NOBALANCING	0x00000800
#define IRQF_IRQPOLL		0x00001000
#define IRQF_ONESHOT		0x00002000
#define IRQF_NO_SUSPEND		0x00004000
#define IRQF_FORCE_RESUME	0x00008000
#define IRQF_NO_THREAD		0x00010000
#define IRQF_EARLY_RESUME	0x00020000
#define IRQF_COND_SUSPEND	0x00040000
#define IRQF_NO_AUTOEN		0x00080000
#define IRQF_NO_DEBUG		0x00100000

#define IRQF_TIMER		(__IRQF_TIMER | IRQF_NO_SUSPEND | IRQF_NO_THREAD)

/*
 * These values can be returned by request_any_context_irq() and
 * describe the context the interrupt will be run in.
 *
 * IRQC_IS_HARDIRQ - interrupt runs in hardirq context
 * IRQC_IS_NESTED - interrupt runs in a nested threaded context
 */
enum {
	IRQC_IS_HARDIRQ	= 0,
	IRQC_IS_NESTED,
};

typedef irqreturn_t (*irq_handler_t)(int, void *);

/**
 * struct irqaction - per interrupt action descriptor
 * @handler:	interrupt handler function
 * @name:	name of the device
 * @dev_id:	cookie to identify the device
 * @percpu_dev_id:	cookie to identify the device
 * @next:	pointer to the next irqaction for shared interrupts
 * @irq:	interrupt number
 * @flags:	flags (see IRQF_* above)
 * @thread_fn:	interrupt handler function for threaded interrupts
 * @thread:	thread pointer for threaded interrupts
 * @secondary:	pointer to secondary irqaction (force threading)
 * @thread_flags:	flags related to @thread
 * @thread_mask:	bitmask for keeping track of @thread activity
 * @dir:	pointer to the proc/irq/NN/name entry
 */
struct irqaction {
	irq_handler_t		handler;
	void			*dev_id;
	void __percpu		*percpu_dev_id;
	struct irqaction	*next;
	irq_handler_t		thread_fn;
	struct task_struct	*thread;
	struct irqaction	*secondary;
	unsigned int		irq;
	unsigned int		flags;
	unsigned long		thread_flags;
	unsigned long		thread_mask;
	const char		*name;
	struct proc_dir_entry	*dir;
} ____cacheline_internodealigned_in_smp;

extern irqreturn_t no_action(int cpl, void *dev_id);

/*
 * If a (PCI) device interrupt is not connected we set dev->irq to
 * IRQ_NOTCONNECTED. This causes request_irq() to fail with -ENOTCONN, so we
 * can distingiush that case from other error returns.
 *
 * 0x80000000 is guaranteed to be outside the available range of interrupts
 * and easy to distinguish from other possible incorrect values.
 */
#define IRQ_NOTCONNECTED	(1U << 31)

extern int __must_check
request_threaded_irq(unsigned int irq, irq_handler_t handler,
		     irq_handler_t thread_fn,
		     unsigned long flags, const char *name, void *dev);

/**
 * request_irq - Add a handler for an interrupt line
 * @irq:	The interrupt line to allocate
 * @handler:	Function to be called when the IRQ occurs.
 *		Primary handler for threaded interrupts
 *		If NULL, the default primary handler is installed
 * @flags:	Handling flags
 * @name:	Name of the device generating this interrupt
 * @dev:	A cookie passed to the handler function
 *
 * This call allocates an interrupt and establishes a handler; see
 * the documentation for request_threaded_irq() for details.
 */
static inline int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}

extern int __must_check
request_any_context_irq(unsigned int irq, irq_handler_t handler,
			unsigned long flags, const char *name, void *dev_id);

extern int __must_check
__request_percpu_irq(unsigned int irq, irq_handler_t handler,
		     unsigned long flags, const char *devname,
		     void __percpu *percpu_dev_id);

extern int __must_check
request_nmi(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev);

static inline int __must_check
request_percpu_irq(unsigned int irq, irq_handler_t handler,
		   const char *devname, void __percpu *percpu_dev_id)
{
	return __request_percpu_irq(irq, handler, 0,
				    devname, percpu_dev_id);
}

extern int __must_check
request_percpu_nmi(unsigned int irq, irq_handler_t handler,
		   const char *devname, void __percpu *dev);

extern const void *free_irq(unsigned int, void *);
extern void free_percpu_irq(unsigned int, void __percpu *);

extern const void *free_nmi(unsigned int irq, void *dev_id);
extern void free_percpu_nmi(unsigned int irq, void __percpu *percpu_dev_id);

struct device;

extern int __must_check
devm_request_threaded_irq(struct device *dev, unsigned int irq,
			  irq_handler_t handler, irq_handler_t thread_fn,
			  unsigned long irqflags, const char *devname,
			  void *dev_id);

static inline int __must_check
devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler,
		 unsigned long irqflags, const char *devname, void *dev_id)
{
	return devm_request_threaded_irq(dev, irq, handler, NULL, irqflags,
					 devname, dev_id);
}

extern int __must_check
devm_request_any_context_irq(struct device *dev, unsigned int irq,
		 irq_handler_t handler, unsigned long irqflags,
		 const char *devname, void *dev_id);

extern void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id);

bool irq_has_action(unsigned int irq);
extern void disable_irq_nosync(unsigned int irq);
extern bool disable_hardirq(unsigned int irq);
extern void disable_irq(unsigned int irq);
extern void disable_percpu_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);
extern void enable_percpu_irq(unsigned int irq, unsigned int type);
extern bool irq_percpu_is_enabled(unsigned int irq);
extern void irq_wake_thread(unsigned int irq, void *dev_id);

extern void disable_nmi_nosync(unsigned int irq);
extern void disable_percpu_nmi(unsigned int irq);
extern void enable_nmi(unsigned int irq);
extern void enable_percpu_nmi(unsigned int irq, unsigned int type);
extern int prepare_percpu_nmi(unsigned int irq);
extern void teardown_percpu_nmi(unsigned int irq);

extern int irq_inject_interrupt(unsigned int irq);

/* The following three functions are for the core kernel use only. */
extern void suspend_device_irqs(void);
extern void resume_device_irqs(void);
extern void rearm_wake_irq(unsigned int irq);

/**
 * struct irq_affinity_notify - context for notification of IRQ affinity changes
 * @irq:		Interrupt to which notification applies
 * @kref:		Reference count, for internal use
 * @work:		Work item, for internal use
 * @notify:		Function to be called on change.  This will be
 *			called in process context.
 * @release:		Function to be called on release.  This will be
 *			called in process context.  Once registered, the
 *			structure must only be freed when this function is
 *			called or later.
 */
struct irq_affinity_notify {
	unsigned int irq;
	struct kref kref;
	struct work_struct work;
	void (*notify)(struct irq_affinity_notify *, const cpumask_t *mask);
	void (*release)(struct kref *ref);
};

#define	IRQ_AFFINITY_MAX_SETS  4

/**
 * struct irq_affinity - Description for automatic irq affinity assignements
 * @pre_vectors:	Don't apply affinity to @pre_vectors at beginning of
 *			the MSI(-X) vector space
 * @post_vectors:	Don't apply affinity to @post_vectors at end of
 *			the MSI(-X) vector space
 * @nr_sets:		The number of interrupt sets for which affinity
 *			spreading is required
 * @set_size:		Array holding the size of each interrupt set
 * @calc_sets:		Callback for calculating the number and size
 *			of interrupt sets
 * @priv:		Private data for usage by @calc_sets, usually a
 *			pointer to driver/device specific data.
 */
struct irq_affinity {
	unsigned int	pre_vectors;
	unsigned int	post_vectors;
	unsigned int	nr_sets;
	unsigned int	set_size[IRQ_AFFINITY_MAX_SETS];
	void		(*calc_sets)(struct irq_affinity *, unsigned int nvecs);
	void		*priv;
};

/**
 * struct irq_affinity_desc - Interrupt affinity descriptor
 * @mask:	cpumask to hold the affinity assignment
 * @is_managed: 1 if the interrupt is managed internally
 */
struct irq_affinity_desc {
	struct cpumask	mask;
	unsigned int	is_managed : 1;
};

#if defined(CONFIG_SMP)

extern cpumask_var_t irq_default_affinity;

extern int irq_set_affinity(unsigned int irq, const struct cpumask *cpumask);
extern int irq_force_affinity(unsigned int irq, const struct cpumask *cpumask);

extern int irq_can_set_affinity(unsigned int irq);
extern int irq_select_affinity(unsigned int irq);

extern int __irq_apply_affinity_hint(unsigned int irq, const struct cpumask *m,
				     bool setaffinity);

/**
 * irq_update_affinity_hint - Update the affinity hint
 * @irq:	Interrupt to update
 * @m:		cpumask pointer (NULL to clear the hint)
 *
 * Updates the affinity hint, but does not change the affinity of the interrupt.
 */
static inline int
irq_update_affinity_hint(unsigned int irq, const struct cpumask *m)
{
	return __irq_apply_affinity_hint(irq, m, false);
}

/**
 * irq_set_affinity_and_hint - Update the affinity hint and apply the provided
 *			     cpumask to the interrupt
 * @irq:	Interrupt to update
 * @m:		cpumask pointer (NULL to clear the hint)
 *
 * Updates the affinity hint and if @m is not NULL it applies it as the
 * affinity of that interrupt.
 */
static inline int
irq_set_affinity_and_hint(unsigned int irq, const struct cpumask *m)
{
	return __irq_apply_affinity_hint(irq, m, true);
}

/*
 * Deprecated. Use irq_update_affinity_hint() or irq_set_affinity_and_hint()
 * instead.
 */
static inline int irq_set_affinity_hint(unsigned int irq, const struct cpumask *m)
{
	return irq_set_affinity_and_hint(irq, m);
}

extern int irq_update_affinity_desc(unsigned int irq,
				    struct irq_affinity_desc *affinity);

extern int
irq_set_affinity_notifier(unsigned int irq, struct irq_affinity_notify *notify);

struct irq_affinity_desc *
irq_create_affinity_masks(unsigned int nvec, struct irq_affinity *affd);

unsigned int irq_calc_affinity_vectors(unsigned int minvec, unsigned int maxvec,
				       const struct irq_affinity *affd);

#else /* CONFIG_SMP */

static inline int irq_set_affinity(unsigned int irq, const struct cpumask *m)
{
	return -EINVAL;
}

static inline int irq_force_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	return 0;
}

static inline int irq_can_set_affinity(unsigned int irq)
{
	return 0;
}

static inline int irq_select_affinity(unsigned int irq)  { return 0; }

static inline int irq_update_affinity_hint(unsigned int irq,
					   const struct cpumask *m)
{
	return -EINVAL;
}

static inline int irq_set_affinity_and_hint(unsigned int irq,
					    const struct cpumask *m)
{
	return -EINVAL;
}

static inline int irq_set_affinity_hint(unsigned int irq,
					const struct cpumask *m)
{
	return -EINVAL;
}

static inline int irq_update_affinity_desc(unsigned int irq,
					   struct irq_affinity_desc *affinity)
{
	return -EINVAL;
}

static inline int
irq_set_affinity_notifier(unsigned int irq, struct irq_affinity_notify *notify)
{
	return 0;
}

static inline struct irq_affinity_desc *
irq_create_affinity_masks(unsigned int nvec, struct irq_affinity *affd)
{
	return NULL;
}

static inline unsigned int
irq_calc_affinity_vectors(unsigned int minvec, unsigned int maxvec,
			  const struct irq_affinity *affd)
{
	return maxvec;
}

#endif /* CONFIG_SMP */

/*
 * Special lockdep variants of irq disabling/enabling.
 * These should be used for locking constructs that
 * know that a particular irq context which is disabled,
 * and which is the only irq-context user of a lock,
 * that it's safe to take the lock in the irq-disabled
 * section without disabling hardirqs.
 *
 * On !CONFIG_LOCKDEP they are equivalent to the normal
 * irq disable/enable methods.
 */
static inline void disable_irq_nosync_lockdep(unsigned int irq)
{
	disable_irq_nosync(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_disable();
#endif
}

static inline void disable_irq_nosync_lockdep_irqsave(unsigned int irq, unsigned long *flags)
{
	disable_irq_nosync(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_save(*flags);
#endif
}

static inline void disable_irq_lockdep(unsigned int irq)
{
	disable_irq(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_disable();
#endif
}

static inline void enable_irq_lockdep(unsigned int irq)
{
#ifdef CONFIG_LOCKDEP
	local_irq_enable();
#endif
	enable_irq(irq);
}

static inline void enable_irq_lockdep_irqrestore(unsigned int irq, unsigned long *flags)
{
#ifdef CONFIG_LOCKDEP
	local_irq_restore(*flags);
#endif
	enable_irq(irq);
}

/* IRQ wakeup (PM) control: */
extern int irq_set_irq_wake(unsigned int irq, unsigned int on);

static inline int enable_irq_wake(unsigned int irq)
{
	return irq_set_irq_wake(irq, 1);
}

static inline int disable_irq_wake(unsigned int irq)
{
	return irq_set_irq_wake(irq, 0);
}

/*
 * irq_get_irqchip_state/irq_set_irqchip_state specific flags
 */
enum irqchip_irq_state {
	IRQCHIP_STATE_PENDING,		/* Is interrupt pending? */
	IRQCHIP_STATE_ACTIVE,		/* Is interrupt in progress? */
	IRQCHIP_STATE_MASKED,		/* Is interrupt masked? */
	IRQCHIP_STATE_LINE_LEVEL,	/* Is IRQ line high? */
};

extern int irq_get_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
				 bool *state);
extern int irq_set_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
				 bool state);

#ifdef CONFIG_IRQ_FORCED_THREADING
# ifdef CONFIG_PREEMPT_RT
#  define force_irqthreads()	(true)
# else
DECLARE_STATIC_KEY_FALSE(force_irqthreads_key);
#  define force_irqthreads()	(static_branch_unlikely(&force_irqthreads_key))
# endif
#else
#define force_irqthreads()	(false)
#endif

#ifndef local_softirq_pending

#ifndef local_softirq_pending_ref
#define local_softirq_pending_ref irq_stat.__softirq_pending
#endif

#define local_softirq_pending()	(__this_cpu_read(local_softirq_pending_ref))
#define set_softirq_pending(x)	(__this_cpu_write(local_softirq_pending_ref, (x)))
#define or_softirq_pending(x)	(__this_cpu_or(local_softirq_pending_ref, (x)))

/**
 * __cpu_softirq_pending() - Checks to see if softirq is pending on a cpu
 *
 * This helper is inherently racy, as we're accessing per-cpu data w/o locks.
 * But peeking at the flag can still be useful when deciding where to place a
 * task.
 */
static inline u32 __cpu_softirq_pending(int cpu)
{
	return (u32)per_cpu(local_softirq_pending_ref, cpu);
}
#endif /* local_softirq_pending */

/* Some architectures might implement lazy enabling/disabling of
 * interrupts. In some cases, such as stop_machine, we might want
 * to ensure that after a local_irq_disable(), interrupts have
 * really been disabled in hardware. Such architectures need to
 * implement the following hook.
 */
#ifndef hard_irq_disable
#define hard_irq_disable()	do { } while(0)
#endif

/* PLEASE, avoid to allocate new softirqs, if you need not _really_ high
   frequency threaded job scheduling. For almost all the purposes
   tasklets are more than enough. F.e. all serial device BHs et
   al. should be converted to tasklets, not to softirqs.
 */

enum
{
	HI_SOFTIRQ=0,
	TIMER_SOFTIRQ,
	NET_TX_SOFTIRQ,
	NET_RX_SOFTIRQ,
	BLOCK_SOFTIRQ,
	IRQ_POLL_SOFTIRQ,
	TASKLET_SOFTIRQ,
	SCHED_SOFTIRQ,
	HRTIMER_SOFTIRQ,
	RCU_SOFTIRQ,    /* Preferable RCU should always be the last softirq */

	NR_SOFTIRQS
};

/*
 * The following vectors can be safely ignored after ksoftirqd is parked:
 *
 * _ RCU:
 * 	1) rcutree_migrate_callbacks() migrates the queue.
 * 	2) rcu_report_dead() reports the final quiescent states.
 *
 * _ IRQ_POLL: irq_poll_cpu_dead() migrates the queue
 *
 * _ (HR)TIMER_SOFTIRQ: (hr)timers_dead_cpu() migrates the queue
 */
#define SOFTIRQ_HOTPLUG_SAFE_MASK (BIT(TIMER_SOFTIRQ) | BIT(IRQ_POLL_SOFTIRQ) |\
				   BIT(HRTIMER_SOFTIRQ) | BIT(RCU_SOFTIRQ))

/* Softirq's where the handling might be long: */
#define LONG_SOFTIRQ_MASK (BIT(NET_TX_SOFTIRQ)    | \
			   BIT(NET_RX_SOFTIRQ)    | \
			   BIT(BLOCK_SOFTIRQ)     | \
			   BIT(IRQ_POLL_SOFTIRQ))

/* map softirq index to softirq name. update 'softirq_to_name' in
 * kernel/softirq.c when adding a new softirq.
 */
extern const char * const softirq_to_name[NR_SOFTIRQS];

/* softirq mask and active fields moved to irq_cpustat_t in
 * asm/hardirq.h to get better cache usage.  KAO
 */

struct softirq_action
{
	void	(*action)(struct softirq_action *);
};

asmlinkage void do_softirq(void);
asmlinkage void __do_softirq(void);

#ifdef CONFIG_PREEMPT_RT
extern void do_softirq_post_smp_call_flush(unsigned int was_pending);
#else
static inline void do_softirq_post_smp_call_flush(unsigned int unused)
{
	do_softirq();
}
#endif

extern void open_softirq(int nr, void (*action)(struct softirq_action *));
extern void softirq_init(void);
extern void __raise_softirq_irqoff(unsigned int nr);

extern void raise_softirq_irqoff(unsigned int nr);
extern void raise_softirq(unsigned int nr);

DECLARE_PER_CPU(struct task_struct *, ksoftirqd);

#ifdef CONFIG_RT_SOFTIRQ_AWARE_SCHED
DECLARE_PER_CPU(u32, active_softirqs);
#endif

static inline struct task_struct *this_cpu_ksoftirqd(void)
{
	return this_cpu_read(ksoftirqd);
}

/* Tasklets --- multithreaded analogue of BHs.

   This API is deprecated. Please consider using threaded IRQs instead:
   https://lore.kernel.org/lkml/20200716081538.2sivhkj4hcyrusem@linutronix.de

   Main feature differing them of generic softirqs: tasklet
   is running only on one CPU simultaneously.

   Main feature differing them of BHs: different tasklets
   may be run simultaneously on different CPUs.

   Properties:
   * If tasklet_schedule() is called, then tasklet is guaranteed
     to be executed on some cpu at least once after this.
   * If the tasklet is already scheduled, but its execution is still not
     started, it will be executed only once.
   * If this tasklet is already running on another CPU (or schedule is called
     from tasklet itself), it is rescheduled for later.
   * Tasklet is strictly serialized wrt itself, but not
     wrt another tasklets. If client needs some intertask synchronization,
     he makes it with spinlocks.
 */

struct tasklet_struct
{
	struct tasklet_struct *next;
	unsigned long state;
	atomic_t count;
	bool use_callback;
	union {
		void (*func)(unsigned long data);
		void (*callback)(struct tasklet_struct *t);
	};
	unsigned long data;
};

#define DECLARE_TASKLET(name, _callback)		\
struct tasklet_struct name = {				\
	.count = ATOMIC_INIT(0),			\
	.callback = _callback,				\
	.use_callback = true,				\
}

#define DECLARE_TASKLET_DISABLED(name, _callback)	\
struct tasklet_struct name = {				\
	.count = ATOMIC_INIT(1),			\
	.callback = _callback,				\
	.use_callback = true,				\
}

#define from_tasklet(var, callback_tasklet, tasklet_fieldname)	\
	container_of(callback_tasklet, typeof(*var), tasklet_fieldname)

#define DECLARE_TASKLET_OLD(name, _func)		\
struct tasklet_struct name = {				\
	.count = ATOMIC_INIT(0),			\
	.func = _func,					\
}

#define DECLARE_TASKLET_DISABLED_OLD(name, _func)	\
struct tasklet_struct name = {				\
	.count = ATOMIC_INIT(1),			\
	.func = _func,					\
}

enum
{
	TASKLET_STATE_SCHED,	/* Tasklet is scheduled for execution */
	TASKLET_STATE_RUN	/* Tasklet is running (SMP only) */
};

#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT)
static inline int tasklet_trylock(struct tasklet_struct *t)
{
	return !test_and_set_bit(TASKLET_STATE_RUN, &(t)->state);
}

void tasklet_unlock(struct tasklet_struct *t);
void tasklet_unlock_wait(struct tasklet_struct *t);
void tasklet_unlock_spin_wait(struct tasklet_struct *t);

#else
static inline int tasklet_trylock(struct tasklet_struct *t) { return 1; }
static inline void tasklet_unlock(struct tasklet_struct *t) { }
static inline void tasklet_unlock_wait(struct tasklet_struct *t) { }
static inline void tasklet_unlock_spin_wait(struct tasklet_struct *t) { }
#endif

extern void __tasklet_schedule(struct tasklet_struct *t);

static inline void tasklet_schedule(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_schedule(t);
}

extern void __tasklet_hi_schedule(struct tasklet_struct *t);

static inline void tasklet_hi_schedule(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_hi_schedule(t);
}

static inline void tasklet_disable_nosync(struct tasklet_struct *t)
{
	atomic_inc(&t->count);
	smp_mb__after_atomic();
}

/*
 * Do not use in new code. Disabling tasklets from atomic contexts is
 * error prone and should be avoided.
 */
static inline void tasklet_disable_in_atomic(struct tasklet_struct *t)
{
	tasklet_disable_nosync(t);
	tasklet_unlock_spin_wait(t);
	smp_mb();
}

static inline void tasklet_disable(struct tasklet_struct *t)
{
	tasklet_disable_nosync(t);
	tasklet_unlock_wait(t);
	smp_mb();
}

static inline void tasklet_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic();
	atomic_dec(&t->count);
}

extern void tasklet_kill(struct tasklet_struct *t);
extern void tasklet_init(struct tasklet_struct *t,
			 void (*func)(unsigned long), unsigned long data);
extern void tasklet_setup(struct tasklet_struct *t,
			  void (*callback)(struct tasklet_struct *));

/*
 * Autoprobing for irqs:
 *
 * probe_irq_on() and probe_irq_off() provide robust primitives
 * for accurate IRQ probing during kernel initialization.  They are
 * reasonably simple to use, are not "fooled" by spurious interrupts,
 * and, unlike other attempts at IRQ probing, they do not get hung on
 * stuck interrupts (such as unused PS2 mouse interfaces on ASUS boards).
 *
 * For reasonably foolproof probing, use them as follows:
 *
 * 1. clear and/or mask the device's internal interrupt.
 * 2. sti();
 * 3. irqs = probe_irq_on();      // "take over" all unassigned idle IRQs
 * 4. enable the device and cause it to trigger an interrupt.
 * 5. wait for the device to interrupt, using non-intrusive polling or a delay.
 * 6. irq = probe_irq_off(irqs);  // get IRQ number, 0=none, negative=multiple
 * 7. service the device to clear its pending interrupt.
 * 8. loop again if paranoia is required.
 *
 * probe_irq_on() returns a mask of allocated irq's.
 *
 * probe_irq_off() takes the mask as a parameter,
 * and returns the irq number which occurred,
 * or zero if none occurred, or a negative irq number
 * if more than one irq occurred.
 */

#if !defined(CONFIG_GENERIC_IRQ_PROBE) 
static inline unsigned long probe_irq_on(void)
{
	return 0;
}
static inline int probe_irq_off(unsigned long val)
{
	return 0;
}
static inline unsigned int probe_irq_mask(unsigned long val)
{
	return 0;
}
#else
extern unsigned long probe_irq_on(void);	/* returns 0 on failure */
extern int probe_irq_off(unsigned long);	/* returns 0 or negative on failure */
extern unsigned int probe_irq_mask(unsigned long);	/* returns mask of ISA interrupts */
#endif

#ifdef CONFIG_PROC_FS
/* Initialize /proc/irq/ */
extern void init_irq_proc(void);
#else
static inline void init_irq_proc(void)
{
}
#endif

#ifdef CONFIG_IRQ_TIMINGS
void irq_timings_enable(void);
void irq_timings_disable(void);
u64 irq_timings_next_event(u64 now);
#endif

struct seq_file;
int show_interrupts(struct seq_file *p, void *v);
int arch_show_interrupts(struct seq_file *p, int prec);

extern int early_irq_init(void);
extern int arch_probe_nr_irqs(void);
extern int arch_early_irq_init(void);

/*
 * We want to know which function is an entrypoint of a hardirq or a softirq.
 */
#ifndef __irq_entry
# define __irq_entry	 __section(".irqentry.text")
#endif

#define __softirq_entry  __section(".softirqentry.text")

#endif
