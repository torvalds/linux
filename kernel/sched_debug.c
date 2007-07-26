/*
 * kernel/time/sched_debug.c
 *
 * Print the CFS rbtree
 *
 * Copyright(C) 2007, Red Hat, Inc., Ingo Molnar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/utsname.h>

/*
 * This allows printing both to /proc/sched_debug and
 * to the console
 */
#define SEQ_printf(m, x...)			\
 do {						\
	if (m)					\
		seq_printf(m, x);		\
	else					\
		printk(x);			\
 } while (0)

static void
print_task(struct seq_file *m, struct rq *rq, struct task_struct *p, u64 now)
{
	if (rq->curr == p)
		SEQ_printf(m, "R");
	else
		SEQ_printf(m, " ");

	SEQ_printf(m, "%15s %5d %15Ld %13Ld %13Ld %9Ld %5d "
		      "%15Ld %15Ld %15Ld %15Ld %15Ld\n",
		p->comm, p->pid,
		(long long)p->se.fair_key,
		(long long)(p->se.fair_key - rq->cfs.fair_clock),
		(long long)p->se.wait_runtime,
		(long long)(p->nvcsw + p->nivcsw),
		p->prio,
		(long long)p->se.sum_exec_runtime,
		(long long)p->se.sum_wait_runtime,
		(long long)p->se.sum_sleep_runtime,
		(long long)p->se.wait_runtime_overruns,
		(long long)p->se.wait_runtime_underruns);
}

static void print_rq(struct seq_file *m, struct rq *rq, int rq_cpu, u64 now)
{
	struct task_struct *g, *p;

	SEQ_printf(m,
	"\nrunnable tasks:\n"
	"            task   PID        tree-key         delta       waiting"
	"  switches  prio"
	"        sum-exec        sum-wait       sum-sleep"
	"    wait-overrun   wait-underrun\n"
	"------------------------------------------------------------------"
	"----------------"
	"------------------------------------------------"
	"--------------------------------\n");

	read_lock_irq(&tasklist_lock);

	do_each_thread(g, p) {
		if (!p->se.on_rq || task_cpu(p) != rq_cpu)
			continue;

		print_task(m, rq, p, now);
	} while_each_thread(g, p);

	read_unlock_irq(&tasklist_lock);
}

static void
print_cfs_rq_runtime_sum(struct seq_file *m, int cpu, struct cfs_rq *cfs_rq)
{
	s64 wait_runtime_rq_sum = 0;
	struct task_struct *p;
	struct rb_node *curr;
	unsigned long flags;
	struct rq *rq = &per_cpu(runqueues, cpu);

	spin_lock_irqsave(&rq->lock, flags);
	curr = first_fair(cfs_rq);
	while (curr) {
		p = rb_entry(curr, struct task_struct, se.run_node);
		wait_runtime_rq_sum += p->se.wait_runtime;

		curr = rb_next(curr);
	}
	spin_unlock_irqrestore(&rq->lock, flags);

	SEQ_printf(m, "  .%-30s: %Ld\n", "wait_runtime_rq_sum",
		(long long)wait_runtime_rq_sum);
}

void print_cfs_rq(struct seq_file *m, int cpu, struct cfs_rq *cfs_rq, u64 now)
{
	SEQ_printf(m, "\ncfs_rq %p\n", cfs_rq);

#define P(x) \
	SEQ_printf(m, "  .%-30s: %Ld\n", #x, (long long)(cfs_rq->x))

	P(fair_clock);
	P(exec_clock);
	P(wait_runtime);
	P(wait_runtime_overruns);
	P(wait_runtime_underruns);
	P(sleeper_bonus);
#undef P

	print_cfs_rq_runtime_sum(m, cpu, cfs_rq);
}

static void print_cpu(struct seq_file *m, int cpu, u64 now)
{
	struct rq *rq = &per_cpu(runqueues, cpu);

#ifdef CONFIG_X86
	{
		unsigned int freq = cpu_khz ? : 1;

		SEQ_printf(m, "\ncpu#%d, %u.%03u MHz\n",
			   cpu, freq / 1000, (freq % 1000));
	}
#else
	SEQ_printf(m, "\ncpu#%d\n", cpu);
#endif

#define P(x) \
	SEQ_printf(m, "  .%-30s: %Ld\n", #x, (long long)(rq->x))

	P(nr_running);
	SEQ_printf(m, "  .%-30s: %lu\n", "load",
		   rq->ls.load.weight);
	P(ls.delta_fair);
	P(ls.delta_exec);
	P(nr_switches);
	P(nr_load_updates);
	P(nr_uninterruptible);
	SEQ_printf(m, "  .%-30s: %lu\n", "jiffies", jiffies);
	P(next_balance);
	P(curr->pid);
	P(clock);
	P(prev_clock_raw);
	P(clock_warps);
	P(clock_overflows);
	P(clock_unstable_events);
	P(clock_max_delta);
	P(cpu_load[0]);
	P(cpu_load[1]);
	P(cpu_load[2]);
	P(cpu_load[3]);
	P(cpu_load[4]);
#undef P

	print_cfs_stats(m, cpu, now);

	print_rq(m, rq, cpu, now);
}

static int sched_debug_show(struct seq_file *m, void *v)
{
	u64 now = ktime_to_ns(ktime_get());
	int cpu;

	SEQ_printf(m, "Sched Debug Version: v0.05, %s %.*s\n",
		init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version);

	SEQ_printf(m, "now at %Lu nsecs\n", (unsigned long long)now);

	for_each_online_cpu(cpu)
		print_cpu(m, cpu, now);

	SEQ_printf(m, "\n");

	return 0;
}

static void sysrq_sched_debug_show(void)
{
	sched_debug_show(NULL, NULL);
}

static int sched_debug_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sched_debug_show, NULL);
}

static struct file_operations sched_debug_fops = {
	.open		= sched_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init init_sched_debug_procfs(void)
{
	struct proc_dir_entry *pe;

	pe = create_proc_entry("sched_debug", 0644, NULL);
	if (!pe)
		return -ENOMEM;

	pe->proc_fops = &sched_debug_fops;

	return 0;
}

__initcall(init_sched_debug_procfs);

void proc_sched_show_task(struct task_struct *p, struct seq_file *m)
{
	unsigned long flags;
	int num_threads = 1;

	rcu_read_lock();
	if (lock_task_sighand(p, &flags)) {
		num_threads = atomic_read(&p->signal->count);
		unlock_task_sighand(p, &flags);
	}
	rcu_read_unlock();

	SEQ_printf(m, "%s (%d, #threads: %d)\n", p->comm, p->pid, num_threads);
	SEQ_printf(m, "----------------------------------------------\n");
#define P(F) \
	SEQ_printf(m, "%-25s:%20Ld\n", #F, (long long)p->F)

	P(se.wait_start);
	P(se.wait_start_fair);
	P(se.exec_start);
	P(se.sleep_start);
	P(se.sleep_start_fair);
	P(se.block_start);
	P(se.sleep_max);
	P(se.block_max);
	P(se.exec_max);
	P(se.wait_max);
	P(se.wait_runtime);
	P(se.wait_runtime_overruns);
	P(se.wait_runtime_underruns);
	P(se.sum_wait_runtime);
	P(se.sum_exec_runtime);
	SEQ_printf(m, "%-25s:%20Ld\n",
		   "nr_switches", (long long)(p->nvcsw + p->nivcsw));
	P(se.load.weight);
	P(policy);
	P(prio);
#undef P

	{
		u64 t0, t1;

		t0 = sched_clock();
		t1 = sched_clock();
		SEQ_printf(m, "%-25s:%20Ld\n",
			   "clock-delta", (long long)(t1-t0));
	}
}

void proc_sched_set_task(struct task_struct *p)
{
	p->se.sleep_max = p->se.block_max = p->se.exec_max = p->se.wait_max = 0;
	p->se.wait_runtime_overruns = p->se.wait_runtime_underruns = 0;
	p->se.sum_exec_runtime = 0;
}
