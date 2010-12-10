/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/preempt.h>
#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <linux/irqnr.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>

#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <trace/events/irq.h>

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
 * IRQF_DISABLED - keep irqs disabled when calling the action handler.
 *                 DEPRECATED. This flag is a NOOP and scheduled to be removed
 * IRQF_SAMPLE_RANDOM - irq is used to feed the random generator
 * IRQF_SHARED - allow sharing the irq among several devices
 * IRQF_PROBE_SHARED - set by callers when they expect sharing mismatches to occur
 * IRQF_TIMER - Flag to mark this interrupt as timer interrupt
 * IRQF_PERCPU - Interrupt is per cpu
 * IRQF_NOBALANCING - Flag to exclude this interrupt from irq balancing
 * IRQF_IRQPOLL - Interrupt is used for polling (only the interrupt that is
 *                registered first in an shared interrupt is considered for
 *                performance reasons)
 * IRQF_ONESHOT - Interrupt is not reenabled after the hardirq handler finished.
 *                Used by threaded interrupts which need to keep the
 *                irq line disabled until the threaded handler has been run.
 * IRQF_NO_SUSPEND - Do not disable this IRQ during suspend
 *
 */
#define IRQF_DISABLED		0x00000020
#define IRQF_SAMPLE_RANDOM	0x00000040
#define IRQF_SHARED		0x00000080
#define IRQF_PROBE_SHARED	0x00000100
#define __IRQF_TIMER		0x00000200
#define IRQF_PERCPU		0x00000400
#define IRQF_NOBALANCING	0x00000800
#define IRQF_IRQPOLL		0x00001000
#define IRQF_ONESHOT		0x00002000
#define IRQF_NO_SUSPEND		0x00004000

#define IRQF_TIMER		(__IRQF_TIMER | IRQF_NO_SUSPEND)

/*
 * Bits used by threaded handlers:
 * IRQTF_RUNTHREAD - signals that the interrupt handler thread should run
 * IRQTF_DIED      - handler thread died
 * IRQTF_WARNED    - warning "IRQ_WAKE_THREAD w/o thread_fn" has been printed
 * IRQTF_AFFINITY  - irq thread is requested to adjust affinity
 */
enum {
	IRQTF_RUNTHREAD,
	IRQTF_DIED,
	IRQTF_WARNED,
	IRQTF_AFFINITY,
};

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
 * @flags:	flags (see IRQF_* above)
 * @name:	name of the device
 * @dev_id:	cookie to identify the device
 * @next:	pointer to the next irqaction for shared interrupts
 * @irq:	interrupt number
 * @dir:	pointer to the proc/irq/NN/name entry
 * @thread_fn:	interupt handler function for threaded interrupts
 * @thread:	thread pointer for threaded interrupts
 * @thread_flags:	flags related to @thread
 */
struct irqaction {
	irq_handler_t handler;
	unsigned long flags;
	const char *name;
	void *dev_id;
	struct irqaction *next;
	int irq;
	struct proc_dir_entry *dir;
	irq_handler_t thread_fn;
	struct task_struct *thread;
	unsigned long thread_flags;
};

extern irqreturn_t no_action(int cpl, void *dev_id);

#ifdef CONFIG_GENERIC_HARDIRQS
extern int __must_check
request_threaded_irq(unsigned int irq, irq_handler_t handler,
		     irq_handler_t thread_fn,
		     unsigned long flags, const char *name, void *dev);

static inline int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}

extern int __must_check
request_any_context_irq(unsigned int irq, irq_handler_t handler,
			unsigned long flags, const char *name, void *dev_id);

extern void exit_irq_thread(void);
#else

extern int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev);

/*
 * Special function to avoid ifdeffery in kernel/irq/devres.c which
 * gets magically built by GENERIC_HARDIRQS=n architectures (sparc,
 * m68k). I really love these $@%#!* obvious Makefile references:
 * ../../../kernel/irq/devres.o
 */
static inline int __must_check
request_threaded_irq(unsigned int irq, irq_handler_t handler,
		     irq_handler_t thread_fn,
		     unsigned long flags, const char *name, void *dev)
{
	return request_irq(irq, handler, flags, name, dev);
}

static inline int __must_check
request_any_context_irq(unsigned int irq, irq_handler_t handler,
			unsigned long flags, const char *name, void *dev_id)
{
	return request_irq(irq, handler, flags, name, dev_id);
}

static inline void exit_irq_thread(void) { }
#endif

extern void free_irq(unsigned int, void *);

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

extern void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id);

/*
 * On lockdep we dont want to enable hardirqs in hardirq
 * context. Use local_irq_enable_in_hardirq() to annotate
 * kernel code that has to do this nevertheless (pretty much
 * the only valid case is for old/broken hardware that is
 * insanely slow).
 *
 * NOTE: in theory this might break fragile code that relies
 * on hardirq delivery - in practice we dont seem to have such
 * places left. So the only effect should be slightly increased
 * irqs-off latencies.
 */
#ifdef CONFIG_LOCKDEP
# define local_irq_enable_in_hardirq()	do { } while (0)
#else
# define local_irq_enable_in_hardirq()	local_irq_enable()
#endif

extern void disable_irq_nosync(unsigned int irq);
extern void disable_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);

/* The following three functions are for the core kernel use only. */
#ifdef CONFIG_GENERIC_HARDIRQS
extern void suspend_device_irqs(void);
extern void resume_device_irqs(void);
#ifdef CONFIG_PM_SLEEP
extern int check_wakeup_irqs(void);
#else
static inline int check_wakeup_irqs(void) { return 0; }
#endif
#else
static inline void suspend_device_irqs(void) { };
static inline void resume_device_irqs(void) { };
static inline int check_wakeup_irqs(void) { return 0; }
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_GENERIC_HARDIRQS)

extern cpumask_var_t irq_default_affinity;

extern int irq_set_affinity(unsigned int irq, const struct cpumask *cpumask);
extern int irq_can_set_affinity(unsigned int irq);
extern int irq_select_affinity(unsigned int irq);

extern int irq_set_affinity_hint(unsigned int irq, const struct cpumask *m);
#else /* CONFIG_SMP */

static inline int irq_set_affinity(unsigned int irq, const struct cpumask *m)
{
	return -EINVAL;
}

static inline int irq_can_set_affinity(unsigned int irq)
{
	return 0;
}

static inline int irq_select_affinity(unsigned int irq)  { return 0; }

static inline int irq_set_affinity_hint(unsigned int irq,
                                        const struct cpumask *m)
{
	return -EINVAL;
}
#endif /* CONFIG_SMP && CONFIG_GENERIC_HARDIRQS */

#ifdef CONFIG_GENERIC_HARDIRQS
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
extern int set_irq_wake(unsigned int irq, unsigned int on);

static inline int enable_irq_wake(unsigned int irq)
{
	return set_irq_wake(irq, 1);
}

static inline int disable_irq_wake(unsigned int irq)
{
	return set_irq_wake(irq, 0);
}

#else /* !CONFIG_GENERIC_HARDIRQS */
/*
 * NOTE: non-genirq architectures, if they want to support the lock
 * validator need to define the methods below in their asm/irq.h
 * files, under an #ifdef CONFIG_LOCKDEP section.
 */
#ifndef CONFIG_LOCKDEP
#  define disable_irq_nosync_lockdep(irq)	disable_irq_nosync(irq)
#  define disable_irq_nosync_lockdep_irqsave(irq, flags) \
						disable_irq_nosync(irq)
#  define disable_irq_lockdep(irq)		disable_irq(irq)
#  define enable_irq_lockdep(irq)		enable_irq(irq)
#  define enable_irq_lockdep_irqrestore(irq, flags) \
						enable_irq(irq)
# endif

static inline int enable_irq_wake(unsigned int irq)
{
	return 0;
}

static inline int disable_irq_wake(unsigned int irq)
{
	return 0;
}
#endif /* CONFIG_GENERIC_HARDIRQS */

#ifndef __ARCH_SET_SOFTIRQ_PENDING
#define set_softirq_pending(x) (local_softirq_pending() = (x))
#define or_softirq_pending(x)  (local_softirq_pending() |= (x))
#endif

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
	BLOCK_IOPOLL_SOFTIRQ,
	TASKLET_SOFTIRQ,
	SCHED_SOFTIRQ,
	HRTIMER_SOFTIRQ,
	RCU_SOFTIRQ,	/* Preferable RCU should always be the last softirq */

	NR_SOFTIRQS
};

/* map softirq index to softirq name. update 'softirq_to_name' in
 * kernel/softirq.c when adding a new softirq.
 */
extern char *softirq_to_name[NR_SOFTIRQS];

/* softirq mask and active fields moved to irq_cpustat_t in
 * asm/hardirq.h to get better cache usage.  KAO
 */

struct softirq_action
{
	void	(*action)(struct softirq_action *);
};

asmlinkage void do_softirq(void);
asmlinkage void __do_softirq(void);
extern void open_softirq(int nr, void (*action)(struct softirq_action *));
extern void softirq_init(void);
static inline void __raise_softirq_irqoff(unsigned int nr)
{
	trace_softirq_raise(nr);
	or_softirq_pending(1UL << nr);
}

extern void raise_softirq_irqoff(unsigned int nr);
extern void raise_softirq(unsigned int nr);

/* This is the worklist that queues up per-cpu softirq work.
 *
 * send_remote_sendirq() adds work to these lists, and
 * the softirq handler itself dequeues from them.  The queues
 * are protected by disabling local cpu interrupts and they must
 * only be accessed by the local cpu that they are for.
 */
DECLARE_PER_CPU(struct list_head [NR_SOFTIRQS], softirq_work_list);

/* Try to send a softirq to a remote cpu.  If this cannot be done, the
 * work will be queued to the local cpu.
 */
extern void send_remote_softirq(struct call_single_data *cp, int cpu, int softirq);

/* Like send_remote_softirq(), but the caller must disable local cpu interrupts
 * and compute the current cpu, passed in as 'this_cpu'.
 */
extern void __send_remote_softirq(struct call_single_data *cp, int cpu,
				  int this_cpu, int softirq);

/* Tasklets --- multithreaded analogue of BHs.

   Main feature differing them of generic softirqs: tasklet
   is running only on one CPU simultaneously.

   Main feature differing them of BHs: different tasklets
   may be run simultaneously on different CPUs.

   Properties:
   * If tasklet_schedule() is called, then tasklet is guaranteed
     to be executed on some cpu at least once after this.
   * If the tasklet is already scheduled, but its excecution is still not
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
	void (*func)(unsigned long);
	unsigned long data;
};

#define DECLARE_TASKLET(name, func, data) \
struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(0), func, data }

#define DECLARE_TASKLET_DISABLED(name, func, data) \
struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(1), func, data }


enum
{
	TASKLET_STATE_SCHED,	/* Tasklet is scheduled for execution */
	TASKLET_STATE_RUN	/* Tasklet is running (SMP only) */
};

#ifdef CONFIG_SMP
static inline int tasklet_trylock(struct tasklet_struct *t)
{
	return !test_and_set_bit(TASKLET_STATE_RUN, &(t)->state);
}

static inline void tasklet_unlock(struct tasklet_struct *t)
{
	smp_mb__before_clear_bit(); 
	clear_bit(TASKLET_STATE_RUN, &(t)->state);
}

static inline void tasklet_unlock_wait(struct tasklet_struct *t)
{
	while (test_bit(TASKLET_STATE_RUN, &(t)->state)) { barrier(); }
}
#else
#define tasklet_trylock(t) 1
#define tasklet_unlock_wait(t) do { } while (0)
#define tasklet_unlock(t) do { } while (0)
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

extern void __tasklet_hi_schedule_first(struct tasklet_struct *t);

/*
 * This version avoids touching any other tasklets. Needed for kmemcheck
 * in order not to take any page faults while enqueueing this tasklet;
 * consider VERY carefully whether you really need this or
 * tasklet_hi_schedule()...
 */
static inline void tasklet_hi_schedule_first(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_hi_schedule_first(t);
}


static inline void tasklet_disable_nosync(struct tasklet_struct *t)
{
	atomic_inc(&t->count);
	smp_mb__after_atomic_inc();
}

static inline void tasklet_disable(struct tasklet_struct *t)
{
	tasklet_disable_nosync(t);
	tasklet_unlock_wait(t);
	smp_mb();
}

static inline void tasklet_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic_dec();
	atomic_dec(&t->count);
}

static inline void tasklet_hi_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic_dec();
	atomic_dec(&t->count);
}

extern void tasklet_kill(struct tasklet_struct *t);
extern void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu);
extern void tasklet_init(struct tasklet_struct *t,
			 void (*func)(unsigned long), unsigned long data);

struct tasklet_hrtimer {
	struct hrtimer		timer;
	struct tasklet_struct	tasklet;
	enum hrtimer_restart	(*function)(struct hrtimer *);
};

extern void
tasklet_hrtimer_init(struct tasklet_hrtimer *ttimer,
		     enum hrtimer_restart (*function)(struct hrtimer *),
		     clockid_t which_clock, enum hrtimer_mode mode);

static inline
int tasklet_hrtimer_start(struct tasklet_hrtimer *ttimer, ktime_t time,
			  const enum hrtimer_mode mode)
{
	return hrtimer_start(&ttimer->timer, time, mode);
}

static inline
void tasklet_hrtimer_cancel(struct tasklet_hrtimer *ttimer)
{
	hrtimer_cancel(&ttimer->timer);
	tasklet_kill(&ttimer->tasklet);
}

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

#if defined(CONFIG_GENERIC_HARDIRQS) && !defined(CONFIG_GENERIC_IRQ_PROBE) 
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

struct seq_file;
int show_interrupts(struct seq_file *p, void *v);

extern int early_irq_init(void);
extern int arch_probe_nr_irqs(void);
extern int arch_early_irq_init(void);

#endif
