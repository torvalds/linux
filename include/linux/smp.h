#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	Generic SMP support
 *		Alan Cox. <alan@redhat.com>
 */

#include <linux/config.h>

extern void cpu_idle(void);

#ifdef CONFIG_SMP

#include <linux/preempt.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <asm/smp.h>
#include <asm/bug.h>

/*
 * main cross-CPU interfaces, handles INIT, TLB flush, STOP, etc.
 * (defined in asm header):
 */ 

/*
 * stops all CPUs but the current one:
 */
extern void smp_send_stop(void);

/*
 * sends a 'reschedule' event to another CPU:
 */
extern void smp_send_reschedule(int cpu);


/*
 * Prepare machine for booting other CPUs.
 */
extern void smp_prepare_cpus(unsigned int max_cpus);

/*
 * Bring a CPU up
 */
extern int __cpu_up(unsigned int cpunum);

/*
 * Final polishing of CPUs
 */
extern void smp_cpus_done(unsigned int max_cpus);

/*
 * Call a function on all other processors
 */
extern int smp_call_function (void (*func) (void *info), void *info,
			      int retry, int wait);

/*
 * Call a function on all processors
 */
static inline int on_each_cpu(void (*func) (void *info), void *info,
			      int retry, int wait)
{
	int ret = 0;

	preempt_disable();
	ret = smp_call_function(func, info, retry, wait);
	func(info);
	preempt_enable();
	return ret;
}

#define MSG_ALL_BUT_SELF	0x8000	/* Assume <32768 CPU's */
#define MSG_ALL			0x8001

#define MSG_INVALIDATE_TLB	0x0001	/* Remote processor TLB invalidate */
#define MSG_STOP_CPU		0x0002	/* Sent to shut down slave CPU's
					 * when rebooting
					 */
#define MSG_RESCHEDULE		0x0003	/* Reschedule request from master CPU*/
#define MSG_CALL_FUNCTION       0x0004  /* Call function on all other CPUs */

/*
 * Mark the boot cpu "online" so that it can call console drivers in
 * printk() and can access its per-cpu storage.
 */
void smp_prepare_boot_cpu(void);

#else /* !SMP */

/*
 *	These macros fold the SMP functionality into a single CPU system
 */

#if !defined(__smp_processor_id) || !defined(CONFIG_PREEMPT)
# define smp_processor_id()			0
#endif
#define hard_smp_processor_id()			0
#define smp_call_function(func,info,retry,wait)	({ 0; })
#define on_each_cpu(func,info,retry,wait)	({ func(info); 0; })
static inline void smp_send_reschedule(int cpu) { }
#define num_booting_cpus()			1
#define smp_prepare_boot_cpu()			do {} while (0)

#endif /* !SMP */

/*
 * DEBUG_PREEMPT support: check whether smp_processor_id() is being
 * used in a preemption-safe way.
 *
 * An architecture has to enable this debugging code explicitly.
 * It can do so by renaming the smp_processor_id() macro to
 * __smp_processor_id().  This should only be done after some minimal
 * testing, because usually there are a number of false positives
 * that an architecture will trigger.
 *
 * To fix a false positive (i.e. smp_processor_id() use that the
 * debugging code reports but which use for some reason is legal),
 * change the smp_processor_id() reference to _smp_processor_id(),
 * which is the nondebug variant.  NOTE: don't use this to hack around
 * real bugs.
 */
#ifdef __smp_processor_id
# if defined(CONFIG_PREEMPT) && defined(CONFIG_DEBUG_PREEMPT)
   extern unsigned int smp_processor_id(void);
# else
#  define smp_processor_id() __smp_processor_id()
# endif
# define _smp_processor_id() __smp_processor_id()
#else
# define _smp_processor_id() smp_processor_id()
#endif

#define get_cpu()		({ preempt_disable(); smp_processor_id(); })
#define put_cpu()		preempt_enable()
#define put_cpu_no_resched()	preempt_enable_no_resched()

#endif /* __LINUX_SMP_H */
