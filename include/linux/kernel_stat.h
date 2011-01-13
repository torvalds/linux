#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/cputime.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

struct cpu_usage_stat {
	cputime64_t user;
	cputime64_t nice;
	cputime64_t system;
	cputime64_t softirq;
	cputime64_t irq;
	cputime64_t idle;
	cputime64_t iowait;
	cputime64_t steal;
	cputime64_t guest;
	cputime64_t guest_nice;
};

struct kernel_stat {
	struct cpu_usage_stat	cpustat;
#ifndef CONFIG_GENERIC_HARDIRQS
       unsigned int irqs[NR_IRQS];
#endif
	unsigned long irqs_sum;
	unsigned int softirqs[NR_SOFTIRQS];
};

DECLARE_PER_CPU(struct kernel_stat, kstat);

#define kstat_cpu(cpu)	per_cpu(kstat, cpu)
/* Must have preemption disabled for this to be meaningful. */
#define kstat_this_cpu	__get_cpu_var(kstat)

extern unsigned long long nr_context_switches(void);

#ifndef CONFIG_GENERIC_HARDIRQS
#define kstat_irqs_this_cpu(irq) \
	(this_cpu_read(kstat.irqs[irq])

struct irq_desc;

static inline void kstat_incr_irqs_this_cpu(unsigned int irq,
					    struct irq_desc *desc)
{
	kstat_this_cpu.irqs[irq]++;
	kstat_this_cpu.irqs_sum++;
}

static inline unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
       return kstat_cpu(cpu).irqs[irq];
}
#else
#include <linux/irq.h>
extern unsigned int kstat_irqs_cpu(unsigned int irq, int cpu);
#define kstat_irqs_this_cpu(DESC) \
	((DESC)->kstat_irqs[smp_processor_id()])
#define kstat_incr_irqs_this_cpu(irqno, DESC) do {\
	((DESC)->kstat_irqs[smp_processor_id()]++);\
	kstat_this_cpu.irqs_sum++; } while (0)

#endif

static inline void kstat_incr_softirqs_this_cpu(unsigned int irq)
{
	kstat_this_cpu.softirqs[irq]++;
}

static inline unsigned int kstat_softirqs_cpu(unsigned int irq, int cpu)
{
       return kstat_cpu(cpu).softirqs[irq];
}

/*
 * Number of interrupts per specific IRQ source, since bootup
 */
#ifndef CONFIG_GENERIC_HARDIRQS
static inline unsigned int kstat_irqs(unsigned int irq)
{
	unsigned int sum = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		sum += kstat_irqs_cpu(irq, cpu);

	return sum;
}
#else
extern unsigned int kstat_irqs(unsigned int irq);
#endif

/*
 * Number of interrupts per cpu, since bootup
 */
static inline unsigned int kstat_cpu_irqs_sum(unsigned int cpu)
{
	return kstat_cpu(cpu).irqs_sum;
}

/*
 * Lock/unlock the current runqueue - to extract task statistics:
 */
extern unsigned long long task_delta_exec(struct task_struct *);

extern void account_user_time(struct task_struct *, cputime_t, cputime_t);
extern void account_system_time(struct task_struct *, int, cputime_t, cputime_t);
extern void account_steal_time(cputime_t);
extern void account_idle_time(cputime_t);

extern void account_process_tick(struct task_struct *, int user);
extern void account_steal_ticks(unsigned long ticks);
extern void account_idle_ticks(unsigned long ticks);

#endif /* _LINUX_KERNEL_STAT_H */
