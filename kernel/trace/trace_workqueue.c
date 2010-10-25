/*
 * Workqueue statistical tracer.
 *
 * Copyright (C) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 *
 */


#include <trace/events/workqueue.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include "trace_stat.h"
#include "trace.h"


/* A cpu workqueue thread */
struct cpu_workqueue_stats {
	struct list_head            list;
	struct kref                 kref;
	int		            cpu;
	pid_t			    pid;
/* Can be inserted from interrupt or user context, need to be atomic */
	atomic_t	            inserted;
/*
 *  Don't need to be atomic, works are serialized in a single workqueue thread
 *  on a single CPU.
 */
	unsigned int		    executed;
};

/* List of workqueue threads on one cpu */
struct workqueue_global_stats {
	struct list_head	list;
	spinlock_t		lock;
};

/* Don't need a global lock because allocated before the workqueues, and
 * never freed.
 */
static DEFINE_PER_CPU(struct workqueue_global_stats, all_workqueue_stat);
#define workqueue_cpu_stat(cpu) (&per_cpu(all_workqueue_stat, cpu))

static void cpu_workqueue_stat_free(struct kref *kref)
{
	kfree(container_of(kref, struct cpu_workqueue_stats, kref));
}

/* Insertion of a work */
static void
probe_workqueue_insertion(void *ignore,
			  struct task_struct *wq_thread,
			  struct work_struct *work)
{
	int cpu = cpumask_first(&wq_thread->cpus_allowed);
	struct cpu_workqueue_stats *node;
	unsigned long flags;

	spin_lock_irqsave(&workqueue_cpu_stat(cpu)->lock, flags);
	list_for_each_entry(node, &workqueue_cpu_stat(cpu)->list, list) {
		if (node->pid == wq_thread->pid) {
			atomic_inc(&node->inserted);
			goto found;
		}
	}
	pr_debug("trace_workqueue: entry not found\n");
found:
	spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);
}

/* Execution of a work */
static void
probe_workqueue_execution(void *ignore,
			  struct task_struct *wq_thread,
			  struct work_struct *work)
{
	int cpu = cpumask_first(&wq_thread->cpus_allowed);
	struct cpu_workqueue_stats *node;
	unsigned long flags;

	spin_lock_irqsave(&workqueue_cpu_stat(cpu)->lock, flags);
	list_for_each_entry(node, &workqueue_cpu_stat(cpu)->list, list) {
		if (node->pid == wq_thread->pid) {
			node->executed++;
			goto found;
		}
	}
	pr_debug("trace_workqueue: entry not found\n");
found:
	spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);
}

/* Creation of a cpu workqueue thread */
static void probe_workqueue_creation(void *ignore,
				     struct task_struct *wq_thread, int cpu)
{
	struct cpu_workqueue_stats *cws;
	unsigned long flags;

	WARN_ON(cpu < 0);

	/* Workqueues are sometimes created in atomic context */
	cws = kzalloc(sizeof(struct cpu_workqueue_stats), GFP_ATOMIC);
	if (!cws) {
		pr_warning("trace_workqueue: not enough memory\n");
		return;
	}
	INIT_LIST_HEAD(&cws->list);
	kref_init(&cws->kref);
	cws->cpu = cpu;
	cws->pid = wq_thread->pid;

	spin_lock_irqsave(&workqueue_cpu_stat(cpu)->lock, flags);
	list_add_tail(&cws->list, &workqueue_cpu_stat(cpu)->list);
	spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);
}

/* Destruction of a cpu workqueue thread */
static void
probe_workqueue_destruction(void *ignore, struct task_struct *wq_thread)
{
	/* Workqueue only execute on one cpu */
	int cpu = cpumask_first(&wq_thread->cpus_allowed);
	struct cpu_workqueue_stats *node, *next;
	unsigned long flags;

	spin_lock_irqsave(&workqueue_cpu_stat(cpu)->lock, flags);
	list_for_each_entry_safe(node, next, &workqueue_cpu_stat(cpu)->list,
							list) {
		if (node->pid == wq_thread->pid) {
			list_del(&node->list);
			kref_put(&node->kref, cpu_workqueue_stat_free);
			goto found;
		}
	}

	pr_debug("trace_workqueue: don't find workqueue to destroy\n");
found:
	spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);

}

static struct cpu_workqueue_stats *workqueue_stat_start_cpu(int cpu)
{
	unsigned long flags;
	struct cpu_workqueue_stats *ret = NULL;


	spin_lock_irqsave(&workqueue_cpu_stat(cpu)->lock, flags);

	if (!list_empty(&workqueue_cpu_stat(cpu)->list)) {
		ret = list_entry(workqueue_cpu_stat(cpu)->list.next,
				 struct cpu_workqueue_stats, list);
		kref_get(&ret->kref);
	}

	spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);

	return ret;
}

static void *workqueue_stat_start(struct tracer_stat *trace)
{
	int cpu;
	void *ret = NULL;

	for_each_possible_cpu(cpu) {
		ret = workqueue_stat_start_cpu(cpu);
		if (ret)
			return ret;
	}
	return NULL;
}

static void *workqueue_stat_next(void *prev, int idx)
{
	struct cpu_workqueue_stats *prev_cws = prev;
	struct cpu_workqueue_stats *ret;
	int cpu = prev_cws->cpu;
	unsigned long flags;

	spin_lock_irqsave(&workqueue_cpu_stat(cpu)->lock, flags);
	if (list_is_last(&prev_cws->list, &workqueue_cpu_stat(cpu)->list)) {
		spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);
		do {
			cpu = cpumask_next(cpu, cpu_possible_mask);
			if (cpu >= nr_cpu_ids)
				return NULL;
		} while (!(ret = workqueue_stat_start_cpu(cpu)));
		return ret;
	} else {
		ret = list_entry(prev_cws->list.next,
				 struct cpu_workqueue_stats, list);
		kref_get(&ret->kref);
	}
	spin_unlock_irqrestore(&workqueue_cpu_stat(cpu)->lock, flags);

	return ret;
}

static int workqueue_stat_show(struct seq_file *s, void *p)
{
	struct cpu_workqueue_stats *cws = p;
	struct pid *pid;
	struct task_struct *tsk;

	pid = find_get_pid(cws->pid);
	if (pid) {
		tsk = get_pid_task(pid, PIDTYPE_PID);
		if (tsk) {
			seq_printf(s, "%3d %6d     %6u       %s\n", cws->cpu,
				   atomic_read(&cws->inserted), cws->executed,
				   tsk->comm);
			put_task_struct(tsk);
		}
		put_pid(pid);
	}

	return 0;
}

static void workqueue_stat_release(void *stat)
{
	struct cpu_workqueue_stats *node = stat;

	kref_put(&node->kref, cpu_workqueue_stat_free);
}

static int workqueue_stat_headers(struct seq_file *s)
{
	seq_printf(s, "# CPU  INSERTED  EXECUTED   NAME\n");
	seq_printf(s, "# |      |         |          |\n");
	return 0;
}

struct tracer_stat workqueue_stats __read_mostly = {
	.name = "workqueues",
	.stat_start = workqueue_stat_start,
	.stat_next = workqueue_stat_next,
	.stat_show = workqueue_stat_show,
	.stat_release = workqueue_stat_release,
	.stat_headers = workqueue_stat_headers
};


int __init stat_workqueue_init(void)
{
	if (register_stat_tracer(&workqueue_stats)) {
		pr_warning("Unable to register workqueue stat tracer\n");
		return 1;
	}

	return 0;
}
fs_initcall(stat_workqueue_init);

/*
 * Workqueues are created very early, just after pre-smp initcalls.
 * So we must register our tracepoints at this stage.
 */
int __init trace_workqueue_early_init(void)
{
	int ret, cpu;

	for_each_possible_cpu(cpu) {
		spin_lock_init(&workqueue_cpu_stat(cpu)->lock);
		INIT_LIST_HEAD(&workqueue_cpu_stat(cpu)->list);
	}

	ret = register_trace_workqueue_insertion(probe_workqueue_insertion, NULL);
	if (ret)
		goto out;

	ret = register_trace_workqueue_execution(probe_workqueue_execution, NULL);
	if (ret)
		goto no_insertion;

	ret = register_trace_workqueue_creation(probe_workqueue_creation, NULL);
	if (ret)
		goto no_execution;

	ret = register_trace_workqueue_destruction(probe_workqueue_destruction, NULL);
	if (ret)
		goto no_creation;

	return 0;

no_creation:
	unregister_trace_workqueue_creation(probe_workqueue_creation, NULL);
no_execution:
	unregister_trace_workqueue_execution(probe_workqueue_execution, NULL);
no_insertion:
	unregister_trace_workqueue_insertion(probe_workqueue_insertion, NULL);
out:
	pr_warning("trace_workqueue: unable to trace workqueues\n");

	return 1;
}
early_initcall(trace_workqueue_early_init);
