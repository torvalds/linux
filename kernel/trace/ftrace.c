/*
 * Infrastructure for profiling code inserted by 'gcc -pg'.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2004-2008 Ingo Molnar <mingo@redhat.com>
 *
 * Originally ported from the -rt patch by:
 *   Copyright (C) 2007 Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Based on code in the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */

#include <linux/stop_machine.h>
#include <linux/clocksource.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/hash.h>
#include <linux/list.h>

#include "trace.h"

/* ftrace_enabled is a method to turn ftrace on or off */
int ftrace_enabled __read_mostly;
static int last_ftrace_enabled;

/*
 * ftrace_disabled is set when an anomaly is discovered.
 * ftrace_disabled is much stronger than ftrace_enabled.
 */
static int ftrace_disabled __read_mostly;

static DEFINE_SPINLOCK(ftrace_lock);
static DEFINE_MUTEX(ftrace_sysctl_lock);

static struct ftrace_ops ftrace_list_end __read_mostly =
{
	.func = ftrace_stub,
};

static struct ftrace_ops *ftrace_list __read_mostly = &ftrace_list_end;
ftrace_func_t ftrace_trace_function __read_mostly = ftrace_stub;

void ftrace_list_func(unsigned long ip, unsigned long parent_ip)
{
	struct ftrace_ops *op = ftrace_list;

	/* in case someone actually ports this to alpha! */
	read_barrier_depends();

	while (op != &ftrace_list_end) {
		/* silly alpha */
		read_barrier_depends();
		op->func(ip, parent_ip);
		op = op->next;
	};
}

/**
 * clear_ftrace_function - reset the ftrace function
 *
 * This NULLs the ftrace function and in essence stops
 * tracing.  There may be lag
 */
void clear_ftrace_function(void)
{
	ftrace_trace_function = ftrace_stub;
}

static int __register_ftrace_function(struct ftrace_ops *ops)
{
	/* Should never be called by interrupts */
	spin_lock(&ftrace_lock);

	ops->next = ftrace_list;
	/*
	 * We are entering ops into the ftrace_list but another
	 * CPU might be walking that list. We need to make sure
	 * the ops->next pointer is valid before another CPU sees
	 * the ops pointer included into the ftrace_list.
	 */
	smp_wmb();
	ftrace_list = ops;

	if (ftrace_enabled) {
		/*
		 * For one func, simply call it directly.
		 * For more than one func, call the chain.
		 */
		if (ops->next == &ftrace_list_end)
			ftrace_trace_function = ops->func;
		else
			ftrace_trace_function = ftrace_list_func;
	}

	spin_unlock(&ftrace_lock);

	return 0;
}

static int __unregister_ftrace_function(struct ftrace_ops *ops)
{
	struct ftrace_ops **p;
	int ret = 0;

	spin_lock(&ftrace_lock);

	/*
	 * If we are removing the last function, then simply point
	 * to the ftrace_stub.
	 */
	if (ftrace_list == ops && ops->next == &ftrace_list_end) {
		ftrace_trace_function = ftrace_stub;
		ftrace_list = &ftrace_list_end;
		goto out;
	}

	for (p = &ftrace_list; *p != &ftrace_list_end; p = &(*p)->next)
		if (*p == ops)
			break;

	if (*p != ops) {
		ret = -1;
		goto out;
	}

	*p = (*p)->next;

	if (ftrace_enabled) {
		/* If we only have one func left, then call that directly */
		if (ftrace_list == &ftrace_list_end ||
		    ftrace_list->next == &ftrace_list_end)
			ftrace_trace_function = ftrace_list->func;
	}

 out:
	spin_unlock(&ftrace_lock);

	return ret;
}

#ifdef CONFIG_DYNAMIC_FTRACE

static struct task_struct *ftraced_task;
static DECLARE_WAIT_QUEUE_HEAD(ftraced_waiters);
static unsigned long ftraced_iteration_counter;

enum {
	FTRACE_ENABLE_CALLS		= (1 << 0),
	FTRACE_DISABLE_CALLS		= (1 << 1),
	FTRACE_UPDATE_TRACE_FUNC	= (1 << 2),
	FTRACE_ENABLE_MCOUNT		= (1 << 3),
	FTRACE_DISABLE_MCOUNT		= (1 << 4),
};

static int ftrace_filtered;

static struct hlist_head ftrace_hash[FTRACE_HASHSIZE];

static DEFINE_PER_CPU(int, ftrace_shutdown_disable_cpu);

static DEFINE_SPINLOCK(ftrace_shutdown_lock);
static DEFINE_MUTEX(ftraced_lock);
static DEFINE_MUTEX(ftrace_filter_lock);

struct ftrace_page {
	struct ftrace_page	*next;
	unsigned long		index;
	struct dyn_ftrace	records[];
};

#define ENTRIES_PER_PAGE \
  ((PAGE_SIZE - sizeof(struct ftrace_page)) / sizeof(struct dyn_ftrace))

/* estimate from running different kernels */
#define NR_TO_INIT		10000

static struct ftrace_page	*ftrace_pages_start;
static struct ftrace_page	*ftrace_pages;

static int ftraced_trigger;
static int ftraced_suspend;

static int ftrace_record_suspend;

static struct dyn_ftrace *ftrace_free_records;

static inline int
ftrace_ip_in_hash(unsigned long ip, unsigned long key)
{
	struct dyn_ftrace *p;
	struct hlist_node *t;
	int found = 0;

	hlist_for_each_entry(p, t, &ftrace_hash[key], node) {
		if (p->ip == ip) {
			found = 1;
			break;
		}
	}

	return found;
}

static inline void
ftrace_add_hash(struct dyn_ftrace *node, unsigned long key)
{
	hlist_add_head(&node->node, &ftrace_hash[key]);
}

static void ftrace_free_rec(struct dyn_ftrace *rec)
{
	/* no locking, only called from kstop_machine */

	rec->ip = (unsigned long)ftrace_free_records;
	ftrace_free_records = rec;
	rec->flags |= FTRACE_FL_FREE;
}

static struct dyn_ftrace *ftrace_alloc_dyn_node(unsigned long ip)
{
	struct dyn_ftrace *rec;

	/* First check for freed records */
	if (ftrace_free_records) {
		rec = ftrace_free_records;

		if (unlikely(!(rec->flags & FTRACE_FL_FREE))) {
			WARN_ON_ONCE(1);
			ftrace_free_records = NULL;
			ftrace_disabled = 1;
			ftrace_enabled = 0;
			return NULL;
		}

		ftrace_free_records = (void *)rec->ip;
		memset(rec, 0, sizeof(*rec));
		return rec;
	}

	if (ftrace_pages->index == ENTRIES_PER_PAGE) {
		if (!ftrace_pages->next)
			return NULL;
		ftrace_pages = ftrace_pages->next;
	}

	return &ftrace_pages->records[ftrace_pages->index++];
}

static void
ftrace_record_ip(unsigned long ip)
{
	struct dyn_ftrace *node;
	unsigned long flags;
	unsigned long key;
	int resched;
	int atomic;
	int cpu;

	if (!ftrace_enabled || ftrace_disabled)
		return;

	resched = need_resched();
	preempt_disable_notrace();

	/*
	 * We simply need to protect against recursion.
	 * Use the the raw version of smp_processor_id and not
	 * __get_cpu_var which can call debug hooks that can
	 * cause a recursive crash here.
	 */
	cpu = raw_smp_processor_id();
	per_cpu(ftrace_shutdown_disable_cpu, cpu)++;
	if (per_cpu(ftrace_shutdown_disable_cpu, cpu) != 1)
		goto out;

	if (unlikely(ftrace_record_suspend))
		goto out;

	key = hash_long(ip, FTRACE_HASHBITS);

	WARN_ON_ONCE(key >= FTRACE_HASHSIZE);

	if (ftrace_ip_in_hash(ip, key))
		goto out;

	atomic = irqs_disabled();

	spin_lock_irqsave(&ftrace_shutdown_lock, flags);

	/* This ip may have hit the hash before the lock */
	if (ftrace_ip_in_hash(ip, key))
		goto out_unlock;

	/*
	 * There's a slight race that the ftraced will update the
	 * hash and reset here. If it is already converted, skip it.
	 */
	if (ftrace_ip_converted(ip))
		goto out_unlock;

	node = ftrace_alloc_dyn_node(ip);
	if (!node)
		goto out_unlock;

	node->ip = ip;

	ftrace_add_hash(node, key);

	ftraced_trigger = 1;

 out_unlock:
	spin_unlock_irqrestore(&ftrace_shutdown_lock, flags);
 out:
	per_cpu(ftrace_shutdown_disable_cpu, cpu)--;

	/* prevent recursion with scheduler */
	if (resched)
		preempt_enable_no_resched_notrace();
	else
		preempt_enable_notrace();
}

#define FTRACE_ADDR ((long)(ftrace_caller))
#define MCOUNT_ADDR ((long)(mcount))

static void
__ftrace_replace_code(struct dyn_ftrace *rec,
		      unsigned char *old, unsigned char *new, int enable)
{
	unsigned long ip;
	int failed;

	ip = rec->ip;

	if (ftrace_filtered && enable) {
		unsigned long fl;
		/*
		 * If filtering is on:
		 *
		 * If this record is set to be filtered and
		 * is enabled then do nothing.
		 *
		 * If this record is set to be filtered and
		 * it is not enabled, enable it.
		 *
		 * If this record is not set to be filtered
		 * and it is not enabled do nothing.
		 *
		 * If this record is not set to be filtered and
		 * it is enabled, disable it.
		 */
		fl = rec->flags & (FTRACE_FL_FILTER | FTRACE_FL_ENABLED);

		if ((fl ==  (FTRACE_FL_FILTER | FTRACE_FL_ENABLED)) ||
		    (fl == 0))
			return;

		/*
		 * If it is enabled disable it,
		 * otherwise enable it!
		 */
		if (fl == FTRACE_FL_ENABLED) {
			/* swap new and old */
			new = old;
			old = ftrace_call_replace(ip, FTRACE_ADDR);
			rec->flags &= ~FTRACE_FL_ENABLED;
		} else {
			new = ftrace_call_replace(ip, FTRACE_ADDR);
			rec->flags |= FTRACE_FL_ENABLED;
		}
	} else {

		if (enable)
			new = ftrace_call_replace(ip, FTRACE_ADDR);
		else
			old = ftrace_call_replace(ip, FTRACE_ADDR);

		if (enable) {
			if (rec->flags & FTRACE_FL_ENABLED)
				return;
			rec->flags |= FTRACE_FL_ENABLED;
		} else {
			if (!(rec->flags & FTRACE_FL_ENABLED))
				return;
			rec->flags &= ~FTRACE_FL_ENABLED;
		}
	}

	failed = ftrace_modify_code(ip, old, new);
	if (failed) {
		unsigned long key;
		/* It is possible that the function hasn't been converted yet */
		key = hash_long(ip, FTRACE_HASHBITS);
		if (!ftrace_ip_in_hash(ip, key)) {
			rec->flags |= FTRACE_FL_FAILED;
			ftrace_free_rec(rec);
		}

	}
}

static void ftrace_replace_code(int enable)
{
	unsigned char *new = NULL, *old = NULL;
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	int i;

	if (enable)
		old = ftrace_nop_replace();
	else
		new = ftrace_nop_replace();

	for (pg = ftrace_pages_start; pg; pg = pg->next) {
		for (i = 0; i < pg->index; i++) {
			rec = &pg->records[i];

			/* don't modify code that has already faulted */
			if (rec->flags & FTRACE_FL_FAILED)
				continue;

			__ftrace_replace_code(rec, old, new, enable);
		}
	}
}

static void ftrace_shutdown_replenish(void)
{
	if (ftrace_pages->next)
		return;

	/* allocate another page */
	ftrace_pages->next = (void *)get_zeroed_page(GFP_KERNEL);
}

static void
ftrace_code_disable(struct dyn_ftrace *rec)
{
	unsigned long ip;
	unsigned char *nop, *call;
	int failed;

	ip = rec->ip;

	nop = ftrace_nop_replace();
	call = ftrace_call_replace(ip, MCOUNT_ADDR);

	failed = ftrace_modify_code(ip, call, nop);
	if (failed) {
		rec->flags |= FTRACE_FL_FAILED;
		ftrace_free_rec(rec);
	}
}

static int __ftrace_modify_code(void *data)
{
	unsigned long addr;
	int *command = data;

	if (*command & FTRACE_ENABLE_CALLS)
		ftrace_replace_code(1);
	else if (*command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(0);

	if (*command & FTRACE_UPDATE_TRACE_FUNC)
		ftrace_update_ftrace_func(ftrace_trace_function);

	if (*command & FTRACE_ENABLE_MCOUNT) {
		addr = (unsigned long)ftrace_record_ip;
		ftrace_mcount_set(&addr);
	} else if (*command & FTRACE_DISABLE_MCOUNT) {
		addr = (unsigned long)ftrace_stub;
		ftrace_mcount_set(&addr);
	}

	return 0;
}

static void ftrace_run_update_code(int command)
{
	stop_machine_run(__ftrace_modify_code, &command, NR_CPUS);
}

static ftrace_func_t saved_ftrace_func;

static void ftrace_startup(void)
{
	int command = 0;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftraced_lock);
	ftraced_suspend++;
	if (ftraced_suspend == 1)
		command |= FTRACE_ENABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		goto out;

	ftrace_run_update_code(command);
 out:
	mutex_unlock(&ftraced_lock);
}

static void ftrace_shutdown(void)
{
	int command = 0;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftraced_lock);
	ftraced_suspend--;
	if (!ftraced_suspend)
		command |= FTRACE_DISABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		goto out;

	ftrace_run_update_code(command);
 out:
	mutex_unlock(&ftraced_lock);
}

static void ftrace_startup_sysctl(void)
{
	int command = FTRACE_ENABLE_MCOUNT;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftraced_lock);
	/* Force update next time */
	saved_ftrace_func = NULL;
	/* ftraced_suspend is true if we want ftrace running */
	if (ftraced_suspend)
		command |= FTRACE_ENABLE_CALLS;

	ftrace_run_update_code(command);
	mutex_unlock(&ftraced_lock);
}

static void ftrace_shutdown_sysctl(void)
{
	int command = FTRACE_DISABLE_MCOUNT;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftraced_lock);
	/* ftraced_suspend is true if ftrace is running */
	if (ftraced_suspend)
		command |= FTRACE_DISABLE_CALLS;

	ftrace_run_update_code(command);
	mutex_unlock(&ftraced_lock);
}

static cycle_t		ftrace_update_time;
static unsigned long	ftrace_update_cnt;
unsigned long		ftrace_update_tot_cnt;

static int __ftrace_update_code(void *ignore)
{
	struct dyn_ftrace *p;
	struct hlist_head head;
	struct hlist_node *t;
	int save_ftrace_enabled;
	cycle_t start, stop;
	int i;

	/* Don't be recording funcs now */
	save_ftrace_enabled = ftrace_enabled;
	ftrace_enabled = 0;

	start = ftrace_now(raw_smp_processor_id());
	ftrace_update_cnt = 0;

	/* No locks needed, the machine is stopped! */
	for (i = 0; i < FTRACE_HASHSIZE; i++) {
		if (hlist_empty(&ftrace_hash[i]))
			continue;

		head = ftrace_hash[i];
		INIT_HLIST_HEAD(&ftrace_hash[i]);

		/* all CPUS are stopped, we are safe to modify code */
		hlist_for_each_entry(p, t, &head, node) {
			ftrace_code_disable(p);
			ftrace_update_cnt++;
		}

	}

	stop = ftrace_now(raw_smp_processor_id());
	ftrace_update_time = stop - start;
	ftrace_update_tot_cnt += ftrace_update_cnt;

	ftrace_enabled = save_ftrace_enabled;

	return 0;
}

static void ftrace_update_code(void)
{
	if (unlikely(ftrace_disabled))
		return;

	stop_machine_run(__ftrace_update_code, NULL, NR_CPUS);
}

static int ftraced(void *ignore)
{
	unsigned long usecs;

	while (!kthread_should_stop()) {

		set_current_state(TASK_INTERRUPTIBLE);

		/* check once a second */
		schedule_timeout(HZ);

		if (unlikely(ftrace_disabled))
			continue;

		mutex_lock(&ftrace_sysctl_lock);
		mutex_lock(&ftraced_lock);
		if (ftrace_enabled && ftraced_trigger && !ftraced_suspend) {
			ftrace_record_suspend++;
			ftrace_update_code();
			usecs = nsecs_to_usecs(ftrace_update_time);
			if (ftrace_update_tot_cnt > 100000) {
				ftrace_update_tot_cnt = 0;
				pr_info("hm, dftrace overflow: %lu change%s"
					 " (%lu total) in %lu usec%s\n",
					ftrace_update_cnt,
					ftrace_update_cnt != 1 ? "s" : "",
					ftrace_update_tot_cnt,
					usecs, usecs != 1 ? "s" : "");
				ftrace_disabled = 1;
				WARN_ON_ONCE(1);
			}
			ftraced_trigger = 0;
			ftrace_record_suspend--;
		}
		ftraced_iteration_counter++;
		mutex_unlock(&ftraced_lock);
		mutex_unlock(&ftrace_sysctl_lock);

		wake_up_interruptible(&ftraced_waiters);

		ftrace_shutdown_replenish();
	}
	__set_current_state(TASK_RUNNING);
	return 0;
}

static int __init ftrace_dyn_table_alloc(void)
{
	struct ftrace_page *pg;
	int cnt;
	int i;

	/* allocate a few pages */
	ftrace_pages_start = (void *)get_zeroed_page(GFP_KERNEL);
	if (!ftrace_pages_start)
		return -1;

	/*
	 * Allocate a few more pages.
	 *
	 * TODO: have some parser search vmlinux before
	 *   final linking to find all calls to ftrace.
	 *   Then we can:
	 *    a) know how many pages to allocate.
	 *     and/or
	 *    b) set up the table then.
	 *
	 *  The dynamic code is still necessary for
	 *  modules.
	 */

	pg = ftrace_pages = ftrace_pages_start;

	cnt = NR_TO_INIT / ENTRIES_PER_PAGE;

	for (i = 0; i < cnt; i++) {
		pg->next = (void *)get_zeroed_page(GFP_KERNEL);

		/* If we fail, we'll try later anyway */
		if (!pg->next)
			break;

		pg = pg->next;
	}

	return 0;
}

enum {
	FTRACE_ITER_FILTER	= (1 << 0),
	FTRACE_ITER_CONT	= (1 << 1),
};

#define FTRACE_BUFF_MAX (KSYM_SYMBOL_LEN+4) /* room for wildcards */

struct ftrace_iterator {
	loff_t			pos;
	struct ftrace_page	*pg;
	unsigned		idx;
	unsigned		flags;
	unsigned char		buffer[FTRACE_BUFF_MAX+1];
	unsigned		buffer_idx;
	unsigned		filtered;
};

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	struct dyn_ftrace *rec = NULL;

	(*pos)++;

 retry:
	if (iter->idx >= iter->pg->index) {
		if (iter->pg->next) {
			iter->pg = iter->pg->next;
			iter->idx = 0;
			goto retry;
		}
	} else {
		rec = &iter->pg->records[iter->idx++];
		if ((rec->flags & FTRACE_FL_FAILED) ||
		    ((iter->flags & FTRACE_ITER_FILTER) &&
		     !(rec->flags & FTRACE_FL_FILTER))) {
			rec = NULL;
			goto retry;
		}
	}

	iter->pos = *pos;

	return rec;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l = -1;

	if (*pos != iter->pos) {
		for (p = t_next(m, p, &l); p && l < *pos; p = t_next(m, p, &l))
			;
	} else {
		l = *pos;
		p = t_next(m, p, &l);
	}

	return p;
}

static void t_stop(struct seq_file *m, void *p)
{
}

static int t_show(struct seq_file *m, void *v)
{
	struct dyn_ftrace *rec = v;
	char str[KSYM_SYMBOL_LEN];

	if (!rec)
		return 0;

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);

	seq_printf(m, "%s\n", str);

	return 0;
}

static struct seq_operations show_ftrace_seq_ops = {
	.start = t_start,
	.next = t_next,
	.stop = t_stop,
	.show = t_show,
};

static int
ftrace_avail_open(struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	iter->pg = ftrace_pages_start;
	iter->pos = -1;

	ret = seq_open(file, &show_ftrace_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;

		m->private = iter;
	} else {
		kfree(iter);
	}

	return ret;
}

int ftrace_avail_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct ftrace_iterator *iter = m->private;

	seq_release(inode, file);
	kfree(iter);

	return 0;
}

static void ftrace_filter_reset(void)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	unsigned i;

	/* keep kstop machine from running */
	preempt_disable();
	ftrace_filtered = 0;
	pg = ftrace_pages_start;
	while (pg) {
		for (i = 0; i < pg->index; i++) {
			rec = &pg->records[i];
			if (rec->flags & FTRACE_FL_FAILED)
				continue;
			rec->flags &= ~FTRACE_FL_FILTER;
		}
		pg = pg->next;
	}
	preempt_enable();
}

static int
ftrace_filter_open(struct inode *inode, struct file *file)
{
	struct ftrace_iterator *iter;
	int ret = 0;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	mutex_lock(&ftrace_filter_lock);
	if ((file->f_mode & FMODE_WRITE) &&
	    !(file->f_flags & O_APPEND))
		ftrace_filter_reset();

	if (file->f_mode & FMODE_READ) {
		iter->pg = ftrace_pages_start;
		iter->pos = -1;
		iter->flags = FTRACE_ITER_FILTER;

		ret = seq_open(file, &show_ftrace_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = iter;
		} else
			kfree(iter);
	} else
		file->private_data = iter;
	mutex_unlock(&ftrace_filter_lock);

	return ret;
}

static ssize_t
ftrace_filter_read(struct file *file, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	if (file->f_mode & FMODE_READ)
		return seq_read(file, ubuf, cnt, ppos);
	else
		return -EPERM;
}

static loff_t
ftrace_filter_lseek(struct file *file, loff_t offset, int origin)
{
	loff_t ret;

	if (file->f_mode & FMODE_READ)
		ret = seq_lseek(file, offset, origin);
	else
		file->f_pos = ret = 1;

	return ret;
}

enum {
	MATCH_FULL,
	MATCH_FRONT_ONLY,
	MATCH_MIDDLE_ONLY,
	MATCH_END_ONLY,
};

static void
ftrace_match(unsigned char *buff, int len)
{
	char str[KSYM_SYMBOL_LEN];
	char *search = NULL;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int type = MATCH_FULL;
	unsigned i, match = 0, search_len = 0;

	for (i = 0; i < len; i++) {
		if (buff[i] == '*') {
			if (!i) {
				search = buff + i + 1;
				type = MATCH_END_ONLY;
				search_len = len - (i + 1);
			} else {
				if (type == MATCH_END_ONLY) {
					type = MATCH_MIDDLE_ONLY;
				} else {
					match = i;
					type = MATCH_FRONT_ONLY;
				}
				buff[i] = 0;
				break;
			}
		}
	}

	/* keep kstop machine from running */
	preempt_disable();
	ftrace_filtered = 1;
	pg = ftrace_pages_start;
	while (pg) {
		for (i = 0; i < pg->index; i++) {
			int matched = 0;
			char *ptr;

			rec = &pg->records[i];
			if (rec->flags & FTRACE_FL_FAILED)
				continue;
			kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);
			switch (type) {
			case MATCH_FULL:
				if (strcmp(str, buff) == 0)
					matched = 1;
				break;
			case MATCH_FRONT_ONLY:
				if (memcmp(str, buff, match) == 0)
					matched = 1;
				break;
			case MATCH_MIDDLE_ONLY:
				if (strstr(str, search))
					matched = 1;
				break;
			case MATCH_END_ONLY:
				ptr = strstr(str, search);
				if (ptr && (ptr[search_len] == 0))
					matched = 1;
				break;
			}
			if (matched)
				rec->flags |= FTRACE_FL_FILTER;
		}
		pg = pg->next;
	}
	preempt_enable();
}

static ssize_t
ftrace_filter_write(struct file *file, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	struct ftrace_iterator *iter;
	char ch;
	size_t read = 0;
	ssize_t ret;

	if (!cnt || cnt < 0)
		return 0;

	mutex_lock(&ftrace_filter_lock);

	if (file->f_mode & FMODE_READ) {
		struct seq_file *m = file->private_data;
		iter = m->private;
	} else
		iter = file->private_data;

	if (!*ppos) {
		iter->flags &= ~FTRACE_ITER_CONT;
		iter->buffer_idx = 0;
	}

	ret = get_user(ch, ubuf++);
	if (ret)
		goto out;
	read++;
	cnt--;

	if (!(iter->flags & ~FTRACE_ITER_CONT)) {
		/* skip white space */
		while (cnt && isspace(ch)) {
			ret = get_user(ch, ubuf++);
			if (ret)
				goto out;
			read++;
			cnt--;
		}


		if (isspace(ch)) {
			file->f_pos += read;
			ret = read;
			goto out;
		}

		iter->buffer_idx = 0;
	}

	while (cnt && !isspace(ch)) {
		if (iter->buffer_idx < FTRACE_BUFF_MAX)
			iter->buffer[iter->buffer_idx++] = ch;
		else {
			ret = -EINVAL;
			goto out;
		}
		ret = get_user(ch, ubuf++);
		if (ret)
			goto out;
		read++;
		cnt--;
	}

	if (isspace(ch)) {
		iter->filtered++;
		iter->buffer[iter->buffer_idx] = 0;
		ftrace_match(iter->buffer, iter->buffer_idx);
		iter->buffer_idx = 0;
	} else
		iter->flags |= FTRACE_ITER_CONT;


	file->f_pos += read;

	ret = read;
 out:
	mutex_unlock(&ftrace_filter_lock);

	return ret;
}

/**
 * ftrace_set_filter - set a function to filter on in ftrace
 * @buf - the string that holds the function filter text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Filters denote which functions should be enabled when tracing is enabled.
 * If @buf is NULL and reset is set, all functions will be enabled for tracing.
 */
void ftrace_set_filter(unsigned char *buf, int len, int reset)
{
	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_filter_lock);
	if (reset)
		ftrace_filter_reset();
	if (buf)
		ftrace_match(buf, len);
	mutex_unlock(&ftrace_filter_lock);
}

static int
ftrace_filter_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct ftrace_iterator *iter;

	mutex_lock(&ftrace_filter_lock);
	if (file->f_mode & FMODE_READ) {
		iter = m->private;

		seq_release(inode, file);
	} else
		iter = file->private_data;

	if (iter->buffer_idx) {
		iter->filtered++;
		iter->buffer[iter->buffer_idx] = 0;
		ftrace_match(iter->buffer, iter->buffer_idx);
	}

	mutex_lock(&ftrace_sysctl_lock);
	mutex_lock(&ftraced_lock);
	if (iter->filtered && ftraced_suspend && ftrace_enabled)
		ftrace_run_update_code(FTRACE_ENABLE_CALLS);
	mutex_unlock(&ftraced_lock);
	mutex_unlock(&ftrace_sysctl_lock);

	kfree(iter);
	mutex_unlock(&ftrace_filter_lock);
	return 0;
}

static struct file_operations ftrace_avail_fops = {
	.open = ftrace_avail_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = ftrace_avail_release,
};

static struct file_operations ftrace_filter_fops = {
	.open = ftrace_filter_open,
	.read = ftrace_filter_read,
	.write = ftrace_filter_write,
	.llseek = ftrace_filter_lseek,
	.release = ftrace_filter_release,
};

/**
 * ftrace_force_update - force an update to all recording ftrace functions
 *
 * The ftrace dynamic update daemon only wakes up once a second.
 * There may be cases where an update needs to be done immediately
 * for tests or internal kernel tracing to begin. This function
 * wakes the daemon to do an update and will not return until the
 * update is complete.
 */
int ftrace_force_update(void)
{
	unsigned long last_counter;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	mutex_lock(&ftraced_lock);
	last_counter = ftraced_iteration_counter;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ftraced_waiters, &wait);

	if (unlikely(!ftraced_task)) {
		ret = -ENODEV;
		goto out;
	}

	do {
		mutex_unlock(&ftraced_lock);
		wake_up_process(ftraced_task);
		schedule();
		mutex_lock(&ftraced_lock);
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		set_current_state(TASK_INTERRUPTIBLE);
	} while (last_counter == ftraced_iteration_counter);

 out:
	mutex_unlock(&ftraced_lock);
	remove_wait_queue(&ftraced_waiters, &wait);
	set_current_state(TASK_RUNNING);

	return ret;
}

static void ftrace_force_shutdown(void)
{
	struct task_struct *task;
	int command = FTRACE_DISABLE_CALLS | FTRACE_UPDATE_TRACE_FUNC;

	mutex_lock(&ftraced_lock);
	task = ftraced_task;
	ftraced_task = NULL;
	ftraced_suspend = -1;
	ftrace_run_update_code(command);
	mutex_unlock(&ftraced_lock);

	if (task)
		kthread_stop(task);
}

static __init int ftrace_init_debugfs(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	d_tracer = tracing_init_dentry();

	entry = debugfs_create_file("available_filter_functions", 0444,
				    d_tracer, NULL, &ftrace_avail_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'available_filter_functions' entry\n");

	entry = debugfs_create_file("set_ftrace_filter", 0644, d_tracer,
				    NULL, &ftrace_filter_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'set_ftrace_filter' entry\n");
	return 0;
}

fs_initcall(ftrace_init_debugfs);

static int __init ftrace_dynamic_init(void)
{
	struct task_struct *p;
	unsigned long addr;
	int ret;

	addr = (unsigned long)ftrace_record_ip;

	stop_machine_run(ftrace_dyn_arch_init, &addr, NR_CPUS);

	/* ftrace_dyn_arch_init places the return code in addr */
	if (addr) {
		ret = (int)addr;
		goto failed;
	}

	ret = ftrace_dyn_table_alloc();
	if (ret)
		goto failed;

	p = kthread_run(ftraced, NULL, "ftraced");
	if (IS_ERR(p)) {
		ret = -1;
		goto failed;
	}

	last_ftrace_enabled = ftrace_enabled = 1;
	ftraced_task = p;

	return 0;

 failed:
	ftrace_disabled = 1;
	return ret;
}

core_initcall(ftrace_dynamic_init);
#else
# define ftrace_startup()		do { } while (0)
# define ftrace_shutdown()		do { } while (0)
# define ftrace_startup_sysctl()	do { } while (0)
# define ftrace_shutdown_sysctl()	do { } while (0)
# define ftrace_force_shutdown()	do { } while (0)
#endif /* CONFIG_DYNAMIC_FTRACE */

/**
 * ftrace_kill - totally shutdown ftrace
 *
 * This is a safety measure. If something was detected that seems
 * wrong, calling this function will keep ftrace from doing
 * any more modifications, and updates.
 * used when something went wrong.
 */
void ftrace_kill(void)
{
	mutex_lock(&ftrace_sysctl_lock);
	ftrace_disabled = 1;
	ftrace_enabled = 0;

	clear_ftrace_function();
	mutex_unlock(&ftrace_sysctl_lock);

	/* Try to totally disable ftrace */
	ftrace_force_shutdown();
}

/**
 * register_ftrace_function - register a function for profiling
 * @ops - ops structure that holds the function for profiling.
 *
 * Register a function to be called by all functions in the
 * kernel.
 *
 * Note: @ops->func and all the functions it calls must be labeled
 *       with "notrace", otherwise it will go into a
 *       recursive loop.
 */
int register_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	if (unlikely(ftrace_disabled))
		return -1;

	mutex_lock(&ftrace_sysctl_lock);
	ret = __register_ftrace_function(ops);
	ftrace_startup();
	mutex_unlock(&ftrace_sysctl_lock);

	return ret;
}

/**
 * unregister_ftrace_function - unresgister a function for profiling.
 * @ops - ops structure that holds the function to unregister
 *
 * Unregister a function that was added to be called by ftrace profiling.
 */
int unregister_ftrace_function(struct ftrace_ops *ops)
{
	int ret;

	mutex_lock(&ftrace_sysctl_lock);
	ret = __unregister_ftrace_function(ops);
	ftrace_shutdown();
	mutex_unlock(&ftrace_sysctl_lock);

	return ret;
}

int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     struct file *file, void __user *buffer, size_t *lenp,
		     loff_t *ppos)
{
	int ret;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	mutex_lock(&ftrace_sysctl_lock);

	ret  = proc_dointvec(table, write, file, buffer, lenp, ppos);

	if (ret || !write || (last_ftrace_enabled == ftrace_enabled))
		goto out;

	last_ftrace_enabled = ftrace_enabled;

	if (ftrace_enabled) {

		ftrace_startup_sysctl();

		/* we are starting ftrace again */
		if (ftrace_list != &ftrace_list_end) {
			if (ftrace_list->next == &ftrace_list_end)
				ftrace_trace_function = ftrace_list->func;
			else
				ftrace_trace_function = ftrace_list_func;
		}

	} else {
		/* stopping ftrace calls (just send to ftrace_stub) */
		ftrace_trace_function = ftrace_stub;

		ftrace_shutdown_sysctl();
	}

 out:
	mutex_unlock(&ftrace_sysctl_lock);
	return ret;
}
