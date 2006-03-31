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
int smp_call_function(void(*func)(void *info), void *info, int retry, int wait);

/*
 * Call a function on all processors
 */
int on_each_cpu(void (*func) (void *info), void *info, int retry, int wait);

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
#define raw_smp_processor_id()			0
#define hard_smp_processor_id()			0
static inline int up_smp_call_function(void)
{
	return 0;
}
#define smp_call_function(func,info,retry,wait)	(up_smp_call_function())
#define on_each_cpu(func,info,retry,wait)	\
	({					\
		local_irq_disable();		\
		func(info);			\
		local_irq_enable();		\
		0;				\
	})
static inline void smp_send_reschedule(int cpu) { }
#define num_booting_cpus()			1
#define smp_prepare_boot_cpu()			do {} while (0)

#endif /* !SMP */

/*
 * smp_processor_id(): get the current CPU ID.
 *
 * if DEBUG_PREEMPT is enabled the we check whether it is
 * used in a preemption-safe way. (smp_processor_id() is safe
 * if it's used in a preemption-off critical section, or in
 * a thread that is bound to the current CPU.)
 *
 * NOTE: raw_smp_processor_id() is for internal use only
 * (smp_processor_id() is the preferred variant), but in rare
 * instances it might also be used to turn off false positives
 * (i.e. smp_processor_id() use that the debugging code reports but
 * which use for some reason is legal). Don't use this to hack around
 * the warning message, as your code might not work under PREEMPT.
 */
#ifdef CONFIG_DEBUG_PREEMPT
  extern unsigned int debug_smp_processor_id(void);
# define smp_processor_id() debug_smp_processor_id()
#else
# define smp_processor_id() raw_smp_processor_id()
#endif

#define get_cpu()		({ preempt_disable(); smp_processor_id(); })
#define put_cpu()		preempt_enable()
#define put_cpu_no_resched()	preempt_enable_no_resched()

#endif /* __LINUX_SMP_H */
