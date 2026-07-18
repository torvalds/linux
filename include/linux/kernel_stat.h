/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/vtime.h>
#include <asm/irq.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

enum cpu_usage_stat {
	CPUTIME_USER,
	CPUTIME_NICE,
	CPUTIME_SYSTEM,
	CPUTIME_SOFTIRQ,
	CPUTIME_IRQ,
	CPUTIME_IDLE,
	CPUTIME_IOWAIT,
	CPUTIME_STEAL,
	CPUTIME_GUEST,
	CPUTIME_GUEST_NICE,
#ifdef CONFIG_SCHED_CORE
	CPUTIME_FORCEIDLE,
#endif
	NR_STATS,
};

struct kernel_cpustat {
#ifdef CONFIG_NO_HZ_COMMON
	bool		idle_dyntick;
	bool		idle_elapse;
	seqcount_t	idle_sleeptime_seq;
	u64		idle_entrytime;
	u64		idle_stealtime[2];
#endif
	u64		cpustat[NR_STATS];
};

struct kernel_stat {
	unsigned long irqs_sum;
	unsigned int softirqs[NR_SOFTIRQS];
};

DECLARE_PER_CPU(struct kernel_stat, kstat);
DECLARE_PER_CPU(struct kernel_cpustat, kernel_cpustat);

/* Must have preemption disabled for this to be meaningful. */
#define kstat_this_cpu this_cpu_ptr(&kstat)
#define kcpustat_this_cpu this_cpu_ptr(&kernel_cpustat)
#define kstat_cpu(cpu) per_cpu(kstat, cpu)
#define kcpustat_cpu(cpu) per_cpu(kernel_cpustat, cpu)

extern unsigned long long nr_context_switches_cpu(int cpu);
extern unsigned long long nr_context_switches(void);

extern unsigned int kstat_irqs_cpu(unsigned int irq, int cpu);
extern void kstat_incr_irq_this_cpu(unsigned int irq);

static inline void kstat_incr_softirqs_this_cpu(unsigned int irq)
{
	__this_cpu_inc(kstat.softirqs[irq]);
}

static inline unsigned int kstat_softirqs_cpu(unsigned int irq, int cpu)
{
       return kstat_cpu(cpu).softirqs[irq];
}

static inline unsigned int kstat_cpu_softirqs_sum(int cpu)
{
	int i;
	unsigned int sum = 0;

	for (i = 0; i < NR_SOFTIRQS; i++)
		sum += kstat_softirqs_cpu(i, cpu);

	return sum;
}

#ifdef CONFIG_GENERIC_IRQ_STAT_SNAPSHOT
extern void kstat_snapshot_irqs(void);
extern unsigned int kstat_get_irq_since_snapshot(unsigned int irq);
#else
static inline void kstat_snapshot_irqs(void) { }
static inline unsigned int kstat_get_irq_since_snapshot(unsigned int irq) { return 0; }
#endif

/*
 * Number of interrupts per specific IRQ source, since bootup
 */
extern unsigned int kstat_irqs_usr(unsigned int irq);

/*
 * Number of interrupts per cpu, since bootup
 */
static inline unsigned long kstat_cpu_irqs_sum(unsigned int cpu)
{
	return kstat_cpu(cpu).irqs_sum;
}

#ifdef CONFIG_NO_HZ_COMMON

#ifdef CONFIG_HAVE_VIRT_CPU_ACCOUNTING_IDLE

static inline void kcpustat_dyntick_start(u64 now) { }
static inline void kcpustat_dyntick_stop(u64 now) { }
static inline void kcpustat_irq_enter(u64 now) { }
static inline void kcpustat_irq_exit(u64 now) { }
static inline bool kcpustat_idle_dyntick(void) { return false; }

extern u64 arch_kcpustat_field_idle(int cpu);
extern u64 arch_kcpustat_field_iowait(int cpu);

static inline u64 kcpustat_field_idle(int cpu)
{
	return arch_kcpustat_field_idle(cpu);
}

static inline u64 kcpustat_field_iowait(int cpu)
{
	return arch_kcpustat_field_iowait(cpu);
}

#else /* !CONFIG_HAVE_VIRT_CPU_ACCOUNTING_IDLE */

extern void kcpustat_dyntick_start(u64 now);
extern void kcpustat_dyntick_stop(u64 now);
extern void kcpustat_irq_enter(u64 now);
extern void kcpustat_irq_exit(u64 now);
extern u64 kcpustat_field_idle(int cpu);
extern u64 kcpustat_field_iowait(int cpu);

static inline bool kcpustat_idle_dyntick(void)
{
	return __this_cpu_read(kernel_cpustat.idle_dyntick);
}

#endif /* !CONFIG_HAVE_VIRT_CPU_ACCOUNTING_IDLE */

#else
static inline u64 kcpustat_field_idle(int cpu)
{
	return kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
}
static inline u64 kcpustat_field_iowait(int cpu)
{
	return kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
}

static inline bool kcpustat_idle_dyntick(void)
{
	return false;
}
#endif /* CONFIG_NO_HZ_COMMON */

extern u64 get_cpu_idle_time_us(int cpu, u64 *last_update_time);
extern u64 get_cpu_iowait_time_us(int cpu, u64 *last_update_time);

/* Fetch cputime values when vtime is disabled on a CPU */
static inline u64 kcpustat_field_default(enum cpu_usage_stat usage, int cpu)
{
	if (usage == CPUTIME_IDLE)
		return kcpustat_field_idle(cpu);
	if (usage == CPUTIME_IOWAIT)
		return kcpustat_field_iowait(cpu);
	return kcpustat_cpu(cpu).cpustat[usage];
}

static inline void kcpustat_cpu_fetch_default(struct kernel_cpustat *dst, int cpu)
{
	*dst = kcpustat_cpu(cpu);
	dst->cpustat[CPUTIME_IDLE] = kcpustat_field_idle(cpu);
	dst->cpustat[CPUTIME_IOWAIT] = kcpustat_field_iowait(cpu);
}

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
extern u64 kcpustat_field(enum cpu_usage_stat usage, int cpu);
extern void kcpustat_cpu_fetch(struct kernel_cpustat *dst, int cpu);
#else
static inline u64 kcpustat_field(enum cpu_usage_stat usage, int cpu)
{
	return kcpustat_field_default(usage, cpu);
}

static inline void kcpustat_cpu_fetch(struct kernel_cpustat *dst, int cpu)
{
	kcpustat_cpu_fetch_default(dst, cpu);
}
#endif /* !CONFIG_VIRT_CPU_ACCOUNTING_GEN */

extern void account_user_time(struct task_struct *, u64);
extern void account_guest_time(struct task_struct *, u64);
extern void account_system_time(struct task_struct *, int, u64);
extern void account_system_index_time(struct task_struct *, u64,
				      enum cpu_usage_stat);
extern void account_steal_time(u64);
extern void account_idle_time(u64);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
static inline void account_process_tick(struct task_struct *tsk, int user)
{
	if (!kcpustat_idle_dyntick())
		vtime_flush(tsk);
}
#else
extern void account_process_tick(struct task_struct *, int user);
#endif

#ifdef CONFIG_SCHED_CORE
extern void __account_forceidle_time(struct task_struct *tsk, u64 delta);
#endif

#endif /* _LINUX_KERNEL_STAT_H */
