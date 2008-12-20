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
#include <linux/kprobes.h>
#include <linux/ftrace.h>
#include <linux/sysctl.h>
#include <linux/ctype.h>
#include <linux/list.h>

#include <asm/ftrace.h>

#include "trace.h"

#define FTRACE_WARN_ON(cond)			\
	do {					\
		if (WARN_ON(cond))		\
			ftrace_kill();		\
	} while (0)

#define FTRACE_WARN_ON_ONCE(cond)		\
	do {					\
		if (WARN_ON_ONCE(cond))		\
			ftrace_kill();		\
	} while (0)

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

static void ftrace_list_func(unsigned long ip, unsigned long parent_ip)
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
	/* should not be called from interrupt context */
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

	/* should not be called from interrupt context */
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
#ifndef CONFIG_FTRACE_MCOUNT_RECORD
# error Dynamic ftrace depends on MCOUNT_RECORD
#endif

/*
 * Since MCOUNT_ADDR may point to mcount itself, we do not want
 * to get it confused by reading a reference in the code as we
 * are parsing on objcopy output of text. Use a variable for
 * it instead.
 */
static unsigned long mcount_addr = MCOUNT_ADDR;

enum {
	FTRACE_ENABLE_CALLS		= (1 << 0),
	FTRACE_DISABLE_CALLS		= (1 << 1),
	FTRACE_UPDATE_TRACE_FUNC	= (1 << 2),
	FTRACE_ENABLE_MCOUNT		= (1 << 3),
	FTRACE_DISABLE_MCOUNT		= (1 << 4),
};

static int ftrace_filtered;

static LIST_HEAD(ftrace_new_addrs);

static DEFINE_MUTEX(ftrace_regex_lock);

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

static struct dyn_ftrace *ftrace_free_records;


#ifdef CONFIG_KPROBES

static int frozen_record_count;

static inline void freeze_record(struct dyn_ftrace *rec)
{
	if (!(rec->flags & FTRACE_FL_FROZEN)) {
		rec->flags |= FTRACE_FL_FROZEN;
		frozen_record_count++;
	}
}

static inline void unfreeze_record(struct dyn_ftrace *rec)
{
	if (rec->flags & FTRACE_FL_FROZEN) {
		rec->flags &= ~FTRACE_FL_FROZEN;
		frozen_record_count--;
	}
}

static inline int record_frozen(struct dyn_ftrace *rec)
{
	return rec->flags & FTRACE_FL_FROZEN;
}
#else
# define freeze_record(rec)			({ 0; })
# define unfreeze_record(rec)			({ 0; })
# define record_frozen(rec)			({ 0; })
#endif /* CONFIG_KPROBES */

static void ftrace_free_rec(struct dyn_ftrace *rec)
{
	rec->ip = (unsigned long)ftrace_free_records;
	ftrace_free_records = rec;
	rec->flags |= FTRACE_FL_FREE;
}

void ftrace_release(void *start, unsigned long size)
{
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;
	unsigned long s = (unsigned long)start;
	unsigned long e = s + size;
	int i;

	if (ftrace_disabled || !start)
		return;

	/* should not be called from interrupt context */
	spin_lock(&ftrace_lock);

	for (pg = ftrace_pages_start; pg; pg = pg->next) {
		for (i = 0; i < pg->index; i++) {
			rec = &pg->records[i];

			if ((rec->ip >= s) && (rec->ip < e))
				ftrace_free_rec(rec);
		}
	}
	spin_unlock(&ftrace_lock);
}

static struct dyn_ftrace *ftrace_alloc_dyn_node(unsigned long ip)
{
	struct dyn_ftrace *rec;

	/* First check for freed records */
	if (ftrace_free_records) {
		rec = ftrace_free_records;

		if (unlikely(!(rec->flags & FTRACE_FL_FREE))) {
			FTRACE_WARN_ON_ONCE(1);
			ftrace_free_records = NULL;
			return NULL;
		}

		ftrace_free_records = (void *)rec->ip;
		memset(rec, 0, sizeof(*rec));
		return rec;
	}

	if (ftrace_pages->index == ENTRIES_PER_PAGE) {
		if (!ftrace_pages->next) {
			/* allocate another page */
			ftrace_pages->next =
				(void *)get_zeroed_page(GFP_KERNEL);
			if (!ftrace_pages->next)
				return NULL;
		}
		ftrace_pages = ftrace_pages->next;
	}

	return &ftrace_pages->records[ftrace_pages->index++];
}

static struct dyn_ftrace *
ftrace_record_ip(unsigned long ip)
{
	struct dyn_ftrace *rec;

	if (!ftrace_enabled || ftrace_disabled)
		return NULL;

	rec = ftrace_alloc_dyn_node(ip);
	if (!rec)
		return NULL;

	rec->ip = ip;

	list_add(&rec->list, &ftrace_new_addrs);

	return rec;
}

#define FTRACE_ADDR ((long)(ftrace_caller))

static int
__ftrace_replace_code(struct dyn_ftrace *rec,
		      unsigned char *nop, int enable)
{
	unsigned long ip, fl;
	unsigned char *call, *old, *new;

	ip = rec->ip;

	/*
	 * If this record is not to be traced and
	 * it is not enabled then do nothing.
	 *
	 * If this record is not to be traced and
	 * it is enabled then disabled it.
	 *
	 */
	if (rec->flags & FTRACE_FL_NOTRACE) {
		if (rec->flags & FTRACE_FL_ENABLED)
			rec->flags &= ~FTRACE_FL_ENABLED;
		else
			return 0;

	} else if (ftrace_filtered && enable) {
		/*
		 * Filtering is on:
		 */

		fl = rec->flags & (FTRACE_FL_FILTER | FTRACE_FL_ENABLED);

		/* Record is filtered and enabled, do nothing */
		if (fl == (FTRACE_FL_FILTER | FTRACE_FL_ENABLED))
			return 0;

		/* Record is not filtered and is not enabled do nothing */
		if (!fl)
			return 0;

		/* Record is not filtered but enabled, disable it */
		if (fl == FTRACE_FL_ENABLED)
			rec->flags &= ~FTRACE_FL_ENABLED;
		else
		/* Otherwise record is filtered but not enabled, enable it */
			rec->flags |= FTRACE_FL_ENABLED;
	} else {
		/* Disable or not filtered */

		if (enable) {
			/* if record is enabled, do nothing */
			if (rec->flags & FTRACE_FL_ENABLED)
				return 0;

			rec->flags |= FTRACE_FL_ENABLED;

		} else {

			/* if record is not enabled do nothing */
			if (!(rec->flags & FTRACE_FL_ENABLED))
				return 0;

			rec->flags &= ~FTRACE_FL_ENABLED;
		}
	}

	call = ftrace_call_replace(ip, FTRACE_ADDR);

	if (rec->flags & FTRACE_FL_ENABLED) {
		old = nop;
		new = call;
	} else {
		old = call;
		new = nop;
	}

	return ftrace_modify_code(ip, old, new);
}

static void ftrace_replace_code(int enable)
{
	int i, failed;
	unsigned char *nop = NULL;
	struct dyn_ftrace *rec;
	struct ftrace_page *pg;

	nop = ftrace_nop_replace();

	for (pg = ftrace_pages_start; pg; pg = pg->next) {
		for (i = 0; i < pg->index; i++) {
			rec = &pg->records[i];

			/* don't modify code that has already faulted */
			if (rec->flags & FTRACE_FL_FAILED)
				continue;

			/* ignore updates to this record's mcount site */
			if (get_kprobe((void *)rec->ip)) {
				freeze_record(rec);
				continue;
			} else {
				unfreeze_record(rec);
			}

			failed = __ftrace_replace_code(rec, nop, enable);
			if (failed && (rec->flags & FTRACE_FL_CONVERTED)) {
				rec->flags |= FTRACE_FL_FAILED;
				if ((system_state == SYSTEM_BOOTING) ||
				    !core_kernel_text(rec->ip)) {
					ftrace_free_rec(rec);
				}
			}
		}
	}
}

static void print_ip_ins(const char *fmt, unsigned char *p)
{
	int i;

	printk(KERN_CONT "%s", fmt);

	for (i = 0; i < MCOUNT_INSN_SIZE; i++)
		printk(KERN_CONT "%s%02x", i ? ":" : "", p[i]);
}

static int
ftrace_code_disable(struct dyn_ftrace *rec)
{
	unsigned long ip;
	unsigned char *nop, *call;
	int ret;

	ip = rec->ip;

	nop = ftrace_nop_replace();
	call = ftrace_call_replace(ip, mcount_addr);

	ret = ftrace_modify_code(ip, call, nop);
	if (ret) {
		switch (ret) {
		case -EFAULT:
			FTRACE_WARN_ON_ONCE(1);
			pr_info("ftrace faulted on modifying ");
			print_ip_sym(ip);
			break;
		case -EINVAL:
			FTRACE_WARN_ON_ONCE(1);
			pr_info("ftrace failed to modify ");
			print_ip_sym(ip);
			print_ip_ins(" expected: ", call);
			print_ip_ins(" actual: ", (unsigned char *)ip);
			print_ip_ins(" replace: ", nop);
			printk(KERN_CONT "\n");
			break;
		case -EPERM:
			FTRACE_WARN_ON_ONCE(1);
			pr_info("ftrace faulted on writing ");
			print_ip_sym(ip);
			break;
		default:
			FTRACE_WARN_ON_ONCE(1);
			pr_info("ftrace faulted on unknown error ");
			print_ip_sym(ip);
		}

		rec->flags |= FTRACE_FL_FAILED;
		return 0;
	}
	return 1;
}

static int __ftrace_modify_code(void *data)
{
	int *command = data;

	if (*command & FTRACE_ENABLE_CALLS)
		ftrace_replace_code(1);
	else if (*command & FTRACE_DISABLE_CALLS)
		ftrace_replace_code(0);

	if (*command & FTRACE_UPDATE_TRACE_FUNC)
		ftrace_update_ftrace_func(ftrace_trace_function);

	return 0;
}

static void ftrace_run_update_code(int command)
{
	stop_machine(__ftrace_modify_code, &command, NULL);
}

static ftrace_func_t saved_ftrace_func;
static int ftrace_start;
static DEFINE_MUTEX(ftrace_start_lock);

static void ftrace_startup(void)
{
	int command = 0;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_start_lock);
	ftrace_start++;
	command |= FTRACE_ENABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		goto out;

	ftrace_run_update_code(command);
 out:
	mutex_unlock(&ftrace_start_lock);
}

static void ftrace_shutdown(void)
{
	int command = 0;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_start_lock);
	ftrace_start--;
	if (!ftrace_start)
		command |= FTRACE_DISABLE_CALLS;

	if (saved_ftrace_func != ftrace_trace_function) {
		saved_ftrace_func = ftrace_trace_function;
		command |= FTRACE_UPDATE_TRACE_FUNC;
	}

	if (!command || !ftrace_enabled)
		goto out;

	ftrace_run_update_code(command);
 out:
	mutex_unlock(&ftrace_start_lock);
}

static void ftrace_startup_sysctl(void)
{
	int command = FTRACE_ENABLE_MCOUNT;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_start_lock);
	/* Force update next time */
	saved_ftrace_func = NULL;
	/* ftrace_start is true if we want ftrace running */
	if (ftrace_start)
		command |= FTRACE_ENABLE_CALLS;

	ftrace_run_update_code(command);
	mutex_unlock(&ftrace_start_lock);
}

static void ftrace_shutdown_sysctl(void)
{
	int command = FTRACE_DISABLE_MCOUNT;

	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_start_lock);
	/* ftrace_start is true if ftrace is running */
	if (ftrace_start)
		command |= FTRACE_DISABLE_CALLS;

	ftrace_run_update_code(command);
	mutex_unlock(&ftrace_start_lock);
}

static cycle_t		ftrace_update_time;
static unsigned long	ftrace_update_cnt;
unsigned long		ftrace_update_tot_cnt;

static int ftrace_update_code(void)
{
	struct dyn_ftrace *p, *t;
	cycle_t start, stop;

	start = ftrace_now(raw_smp_processor_id());
	ftrace_update_cnt = 0;

	list_for_each_entry_safe(p, t, &ftrace_new_addrs, list) {

		/* If something went wrong, bail without enabling anything */
		if (unlikely(ftrace_disabled))
			return -1;

		list_del_init(&p->list);

		/* convert record (i.e, patch mcount-call with NOP) */
		if (ftrace_code_disable(p)) {
			p->flags |= FTRACE_FL_CONVERTED;
			ftrace_update_cnt++;
		} else
			ftrace_free_rec(p);
	}

	stop = ftrace_now(raw_smp_processor_id());
	ftrace_update_time = stop - start;
	ftrace_update_tot_cnt += ftrace_update_cnt;

	return 0;
}

static int __init ftrace_dyn_table_alloc(unsigned long num_to_init)
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

	cnt = num_to_init / ENTRIES_PER_PAGE;
	pr_info("ftrace: allocating %ld entries in %d pages\n",
		num_to_init, cnt + 1);

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
	FTRACE_ITER_NOTRACE	= (1 << 2),
	FTRACE_ITER_FAILURES	= (1 << 3),
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

	/* should not be called from interrupt context */
	spin_lock(&ftrace_lock);
 retry:
	if (iter->idx >= iter->pg->index) {
		if (iter->pg->next) {
			iter->pg = iter->pg->next;
			iter->idx = 0;
			goto retry;
		}
	} else {
		rec = &iter->pg->records[iter->idx++];
		if ((rec->flags & FTRACE_FL_FREE) ||

		    (!(iter->flags & FTRACE_ITER_FAILURES) &&
		     (rec->flags & FTRACE_FL_FAILED)) ||

		    ((iter->flags & FTRACE_ITER_FAILURES) &&
		     !(rec->flags & FTRACE_FL_FAILED)) ||

		    ((iter->flags & FTRACE_ITER_FILTER) &&
		     !(rec->flags & FTRACE_FL_FILTER)) ||

		    ((iter->flags & FTRACE_ITER_NOTRACE) &&
		     !(rec->flags & FTRACE_FL_NOTRACE))) {
			rec = NULL;
			goto retry;
		}
	}
	spin_unlock(&ftrace_lock);

	iter->pos = *pos;

	return rec;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_iterator *iter = m->private;
	void *p = NULL;
	loff_t l = -1;

	if (*pos > iter->pos)
		*pos = iter->pos;

	l = *pos;
	p = t_next(m, p, &l);

	return p;
}

static void t_stop(struct seq_file *m, void *p)
{
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_iterator *iter = m->private;
	struct dyn_ftrace *rec = v;
	char str[KSYM_SYMBOL_LEN];
	int ret = 0;

	if (!rec)
		return 0;

	kallsyms_lookup(rec->ip, NULL, NULL, NULL, str);

	ret = seq_printf(m, "%s\n", str);
	if (ret < 0) {
		iter->pos--;
		iter->idx--;
	}

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
	iter->pos = 0;

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

static int
ftrace_failures_open(struct inode *inode, struct file *file)
{
	int ret;
	struct seq_file *m;
	struct ftrace_iterator *iter;

	ret = ftrace_avail_open(inode, file);
	if (!ret) {
		m = (struct seq_file *)file->private_data;
		iter = (struct ftrace_iterator *)m->private;
		iter->flags = FTRACE_ITER_FAILURES;
	}

	return ret;
}


static void ftrace_filter_reset(int enable)
{
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	unsigned long type = enable ? FTRACE_FL_FILTER : FTRACE_FL_NOTRACE;
	unsigned i;

	/* should not be called from interrupt context */
	spin_lock(&ftrace_lock);
	if (enable)
		ftrace_filtered = 0;
	pg = ftrace_pages_start;
	while (pg) {
		for (i = 0; i < pg->index; i++) {
			rec = &pg->records[i];
			if (rec->flags & FTRACE_FL_FAILED)
				continue;
			rec->flags &= ~type;
		}
		pg = pg->next;
	}
	spin_unlock(&ftrace_lock);
}

static int
ftrace_regex_open(struct inode *inode, struct file *file, int enable)
{
	struct ftrace_iterator *iter;
	int ret = 0;

	if (unlikely(ftrace_disabled))
		return -ENODEV;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	mutex_lock(&ftrace_regex_lock);
	if ((file->f_mode & FMODE_WRITE) &&
	    !(file->f_flags & O_APPEND))
		ftrace_filter_reset(enable);

	if (file->f_mode & FMODE_READ) {
		iter->pg = ftrace_pages_start;
		iter->pos = 0;
		iter->flags = enable ? FTRACE_ITER_FILTER :
			FTRACE_ITER_NOTRACE;

		ret = seq_open(file, &show_ftrace_seq_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = iter;
		} else
			kfree(iter);
	} else
		file->private_data = iter;
	mutex_unlock(&ftrace_regex_lock);

	return ret;
}

static int
ftrace_filter_open(struct inode *inode, struct file *file)
{
	return ftrace_regex_open(inode, file, 1);
}

static int
ftrace_notrace_open(struct inode *inode, struct file *file)
{
	return ftrace_regex_open(inode, file, 0);
}

static ssize_t
ftrace_regex_read(struct file *file, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	if (file->f_mode & FMODE_READ)
		return seq_read(file, ubuf, cnt, ppos);
	else
		return -EPERM;
}

static loff_t
ftrace_regex_lseek(struct file *file, loff_t offset, int origin)
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
ftrace_match(unsigned char *buff, int len, int enable)
{
	char str[KSYM_SYMBOL_LEN];
	char *search = NULL;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	int type = MATCH_FULL;
	unsigned long flag = enable ? FTRACE_FL_FILTER : FTRACE_FL_NOTRACE;
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

	/* should not be called from interrupt context */
	spin_lock(&ftrace_lock);
	if (enable)
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
				rec->flags |= flag;
		}
		pg = pg->next;
	}
	spin_unlock(&ftrace_lock);
}

static ssize_t
ftrace_regex_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos, int enable)
{
	struct ftrace_iterator *iter;
	char ch;
	size_t read = 0;
	ssize_t ret;

	if (!cnt || cnt < 0)
		return 0;

	mutex_lock(&ftrace_regex_lock);

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
		ftrace_match(iter->buffer, iter->buffer_idx, enable);
		iter->buffer_idx = 0;
	} else
		iter->flags |= FTRACE_ITER_CONT;


	file->f_pos += read;

	ret = read;
 out:
	mutex_unlock(&ftrace_regex_lock);

	return ret;
}

static ssize_t
ftrace_filter_write(struct file *file, const char __user *ubuf,
		    size_t cnt, loff_t *ppos)
{
	return ftrace_regex_write(file, ubuf, cnt, ppos, 1);
}

static ssize_t
ftrace_notrace_write(struct file *file, const char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	return ftrace_regex_write(file, ubuf, cnt, ppos, 0);
}

static void
ftrace_set_regex(unsigned char *buf, int len, int reset, int enable)
{
	if (unlikely(ftrace_disabled))
		return;

	mutex_lock(&ftrace_regex_lock);
	if (reset)
		ftrace_filter_reset(enable);
	if (buf)
		ftrace_match(buf, len, enable);
	mutex_unlock(&ftrace_regex_lock);
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
	ftrace_set_regex(buf, len, reset, 1);
}

/**
 * ftrace_set_notrace - set a function to not trace in ftrace
 * @buf - the string that holds the function notrace text.
 * @len - the length of the string.
 * @reset - non zero to reset all filters before applying this filter.
 *
 * Notrace Filters denote which functions should not be enabled when tracing
 * is enabled. If @buf is NULL and reset is set, all functions will be enabled
 * for tracing.
 */
void ftrace_set_notrace(unsigned char *buf, int len, int reset)
{
	ftrace_set_regex(buf, len, reset, 0);
}

static int
ftrace_regex_release(struct inode *inode, struct file *file, int enable)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct ftrace_iterator *iter;

	mutex_lock(&ftrace_regex_lock);
	if (file->f_mode & FMODE_READ) {
		iter = m->private;

		seq_release(inode, file);
	} else
		iter = file->private_data;

	if (iter->buffer_idx) {
		iter->filtered++;
		iter->buffer[iter->buffer_idx] = 0;
		ftrace_match(iter->buffer, iter->buffer_idx, enable);
	}

	mutex_lock(&ftrace_sysctl_lock);
	mutex_lock(&ftrace_start_lock);
	if (ftrace_start && ftrace_enabled)
		ftrace_run_update_code(FTRACE_ENABLE_CALLS);
	mutex_unlock(&ftrace_start_lock);
	mutex_unlock(&ftrace_sysctl_lock);

	kfree(iter);
	mutex_unlock(&ftrace_regex_lock);
	return 0;
}

static int
ftrace_filter_release(struct inode *inode, struct file *file)
{
	return ftrace_regex_release(inode, file, 1);
}

static int
ftrace_notrace_release(struct inode *inode, struct file *file)
{
	return ftrace_regex_release(inode, file, 0);
}

static struct file_operations ftrace_avail_fops = {
	.open = ftrace_avail_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = ftrace_avail_release,
};

static struct file_operations ftrace_failures_fops = {
	.open = ftrace_failures_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = ftrace_avail_release,
};

static struct file_operations ftrace_filter_fops = {
	.open = ftrace_filter_open,
	.read = ftrace_regex_read,
	.write = ftrace_filter_write,
	.llseek = ftrace_regex_lseek,
	.release = ftrace_filter_release,
};

static struct file_operations ftrace_notrace_fops = {
	.open = ftrace_notrace_open,
	.read = ftrace_regex_read,
	.write = ftrace_notrace_write,
	.llseek = ftrace_regex_lseek,
	.release = ftrace_notrace_release,
};

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

	entry = debugfs_create_file("failures", 0444,
				    d_tracer, NULL, &ftrace_failures_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'failures' entry\n");

	entry = debugfs_create_file("set_ftrace_filter", 0644, d_tracer,
				    NULL, &ftrace_filter_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'set_ftrace_filter' entry\n");

	entry = debugfs_create_file("set_ftrace_notrace", 0644, d_tracer,
				    NULL, &ftrace_notrace_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'set_ftrace_notrace' entry\n");

	return 0;
}

fs_initcall(ftrace_init_debugfs);

static int ftrace_convert_nops(unsigned long *start,
			       unsigned long *end)
{
	unsigned long *p;
	unsigned long addr;
	unsigned long flags;

	mutex_lock(&ftrace_start_lock);
	p = start;
	while (p < end) {
		addr = ftrace_call_adjust(*p++);
		ftrace_record_ip(addr);
	}

	/* disable interrupts to prevent kstop machine */
	local_irq_save(flags);
	ftrace_update_code();
	local_irq_restore(flags);
	mutex_unlock(&ftrace_start_lock);

	return 0;
}

void ftrace_init_module(unsigned long *start, unsigned long *end)
{
	if (ftrace_disabled || start == end)
		return;
	ftrace_convert_nops(start, end);
}

extern unsigned long __start_mcount_loc[];
extern unsigned long __stop_mcount_loc[];

void __init ftrace_init(void)
{
	unsigned long count, addr, flags;
	int ret;

	/* Keep the ftrace pointer to the stub */
	addr = (unsigned long)ftrace_stub;

	local_irq_save(flags);
	ftrace_dyn_arch_init(&addr);
	local_irq_restore(flags);

	/* ftrace_dyn_arch_init places the return code in addr */
	if (addr)
		goto failed;

	count = __stop_mcount_loc - __start_mcount_loc;

	ret = ftrace_dyn_table_alloc(count);
	if (ret)
		goto failed;

	last_ftrace_enabled = ftrace_enabled = 1;

	ret = ftrace_convert_nops(__start_mcount_loc,
				  __stop_mcount_loc);

	return;
 failed:
	ftrace_disabled = 1;
}

#else

static int __init ftrace_nodyn_init(void)
{
	ftrace_enabled = 1;
	return 0;
}
device_initcall(ftrace_nodyn_init);

# define ftrace_startup()		do { } while (0)
# define ftrace_shutdown()		do { } while (0)
# define ftrace_startup_sysctl()	do { } while (0)
# define ftrace_shutdown_sysctl()	do { } while (0)
#endif /* CONFIG_DYNAMIC_FTRACE */

/**
 * ftrace_kill - kill ftrace
 *
 * This function should be used by panic code. It stops ftrace
 * but in a not so nice way. If you need to simply kill ftrace
 * from a non-atomic section, use ftrace_kill.
 */
void ftrace_kill(void)
{
	ftrace_disabled = 1;
	ftrace_enabled = 0;
	clear_ftrace_function();
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

