// SPDX-License-Identifier: GPL-2.0
/*
 * trace context switch
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/kmemleak.h>
#include <linux/ftrace.h>
#include <trace/events/sched.h>

#include "trace.h"

#define RECORD_CMDLINE	1
#define RECORD_TGID	2

static int		sched_cmdline_ref;
static int		sched_tgid_ref;
static DEFINE_MUTEX(sched_register_mutex);

static void
probe_sched_switch(void *ignore, bool preempt,
		   struct task_struct *prev, struct task_struct *next,
		   unsigned int prev_state)
{
	int flags;

	flags = (RECORD_TGID * !!sched_tgid_ref) +
		(RECORD_CMDLINE * !!sched_cmdline_ref);

	if (!flags)
		return;
	tracing_record_taskinfo_sched_switch(prev, next, flags);
}

static void
probe_sched_wakeup(void *ignore, struct task_struct *wakee)
{
	int flags;

	flags = (RECORD_TGID * !!sched_tgid_ref) +
		(RECORD_CMDLINE * !!sched_cmdline_ref);

	if (!flags)
		return;
	tracing_record_taskinfo_sched_switch(current, wakee, flags);
}

static int tracing_sched_register(void)
{
	int ret;

	ret = register_trace_sched_wakeup(probe_sched_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup\n");
		return ret;
	}

	ret = register_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = register_trace_sched_switch(probe_sched_switch, NULL);
	if (ret) {
		pr_info("sched trace: Couldn't activate tracepoint"
			" probe to kernel_sched_switch\n");
		goto fail_deprobe_wake_new;
	}

	return ret;
fail_deprobe_wake_new:
	unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
fail_deprobe:
	unregister_trace_sched_wakeup(probe_sched_wakeup, NULL);
	return ret;
}

static void tracing_sched_unregister(void)
{
	unregister_trace_sched_switch(probe_sched_switch, NULL);
	unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
	unregister_trace_sched_wakeup(probe_sched_wakeup, NULL);
}

static void tracing_start_sched_switch(int ops)
{
	bool sched_register;

	mutex_lock(&sched_register_mutex);
	sched_register = (!sched_cmdline_ref && !sched_tgid_ref);

	switch (ops) {
	case RECORD_CMDLINE:
		sched_cmdline_ref++;
		break;

	case RECORD_TGID:
		sched_tgid_ref++;
		break;
	}

	if (sched_register && (sched_cmdline_ref || sched_tgid_ref))
		tracing_sched_register();
	mutex_unlock(&sched_register_mutex);
}

static void tracing_stop_sched_switch(int ops)
{
	mutex_lock(&sched_register_mutex);

	switch (ops) {
	case RECORD_CMDLINE:
		sched_cmdline_ref--;
		break;

	case RECORD_TGID:
		sched_tgid_ref--;
		break;
	}

	if (!sched_cmdline_ref && !sched_tgid_ref)
		tracing_sched_unregister();
	mutex_unlock(&sched_register_mutex);
}

void tracing_start_cmdline_record(void)
{
	tracing_start_sched_switch(RECORD_CMDLINE);
}

void tracing_stop_cmdline_record(void)
{
	tracing_stop_sched_switch(RECORD_CMDLINE);
}

void tracing_start_tgid_record(void)
{
	tracing_start_sched_switch(RECORD_TGID);
}

void tracing_stop_tgid_record(void)
{
	tracing_stop_sched_switch(RECORD_TGID);
}

/*
 * The tgid_map array maps from pid to tgid; i.e. the value stored at index i
 * is the tgid last observed corresponding to pid=i.
 */
static int *tgid_map;

/* The maximum valid index into tgid_map. */
static size_t tgid_map_max;

#define SAVED_CMDLINES_DEFAULT 128
#define NO_CMDLINE_MAP UINT_MAX
/*
 * Preemption must be disabled before acquiring trace_cmdline_lock.
 * The various trace_arrays' max_lock must be acquired in a context
 * where interrupt is disabled.
 */
static arch_spinlock_t trace_cmdline_lock = __ARCH_SPIN_LOCK_UNLOCKED;
struct saved_cmdlines_buffer {
	unsigned map_pid_to_cmdline[PID_MAX_DEFAULT+1];
	unsigned *map_cmdline_to_pid;
	unsigned cmdline_num;
	int cmdline_idx;
	char saved_cmdlines[];
};
static struct saved_cmdlines_buffer *savedcmd;

/* Holds the size of a cmdline and pid element */
#define SAVED_CMDLINE_MAP_ELEMENT_SIZE(s)			\
	(TASK_COMM_LEN + sizeof((s)->map_cmdline_to_pid[0]))

static inline char *get_saved_cmdlines(int idx)
{
	return &savedcmd->saved_cmdlines[idx * TASK_COMM_LEN];
}

static inline void set_cmdline(int idx, const char *cmdline)
{
	strncpy(get_saved_cmdlines(idx), cmdline, TASK_COMM_LEN);
}

static void free_saved_cmdlines_buffer(struct saved_cmdlines_buffer *s)
{
	int order = get_order(sizeof(*s) + s->cmdline_num * TASK_COMM_LEN);

	kmemleak_free(s);
	free_pages((unsigned long)s, order);
}

static struct saved_cmdlines_buffer *allocate_cmdlines_buffer(unsigned int val)
{
	struct saved_cmdlines_buffer *s;
	struct page *page;
	int orig_size, size;
	int order;

	/* Figure out how much is needed to hold the given number of cmdlines */
	orig_size = sizeof(*s) + val * SAVED_CMDLINE_MAP_ELEMENT_SIZE(s);
	order = get_order(orig_size);
	size = 1 << (order + PAGE_SHIFT);
	page = alloc_pages(GFP_KERNEL, order);
	if (!page)
		return NULL;

	s = page_address(page);
	kmemleak_alloc(s, size, 1, GFP_KERNEL);
	memset(s, 0, sizeof(*s));

	/* Round up to actual allocation */
	val = (size - sizeof(*s)) / SAVED_CMDLINE_MAP_ELEMENT_SIZE(s);
	s->cmdline_num = val;

	/* Place map_cmdline_to_pid array right after saved_cmdlines */
	s->map_cmdline_to_pid = (unsigned *)&s->saved_cmdlines[val * TASK_COMM_LEN];

	s->cmdline_idx = 0;
	memset(&s->map_pid_to_cmdline, NO_CMDLINE_MAP,
	       sizeof(s->map_pid_to_cmdline));
	memset(s->map_cmdline_to_pid, NO_CMDLINE_MAP,
	       val * sizeof(*s->map_cmdline_to_pid));

	return s;
}

int trace_create_savedcmd(void)
{
	savedcmd = allocate_cmdlines_buffer(SAVED_CMDLINES_DEFAULT);

	return savedcmd ? 0 : -ENOMEM;
}

int trace_save_cmdline(struct task_struct *tsk)
{
	unsigned tpid, idx;

	/* treat recording of idle task as a success */
	if (!tsk->pid)
		return 1;

	tpid = tsk->pid & (PID_MAX_DEFAULT - 1);

	/*
	 * It's not the end of the world if we don't get
	 * the lock, but we also don't want to spin
	 * nor do we want to disable interrupts,
	 * so if we miss here, then better luck next time.
	 *
	 * This is called within the scheduler and wake up, so interrupts
	 * had better been disabled and run queue lock been held.
	 */
	lockdep_assert_preemption_disabled();
	if (!arch_spin_trylock(&trace_cmdline_lock))
		return 0;

	idx = savedcmd->map_pid_to_cmdline[tpid];
	if (idx == NO_CMDLINE_MAP) {
		idx = (savedcmd->cmdline_idx + 1) % savedcmd->cmdline_num;

		savedcmd->map_pid_to_cmdline[tpid] = idx;
		savedcmd->cmdline_idx = idx;
	}

	savedcmd->map_cmdline_to_pid[idx] = tsk->pid;
	set_cmdline(idx, tsk->comm);

	arch_spin_unlock(&trace_cmdline_lock);

	return 1;
}

static void __trace_find_cmdline(int pid, char comm[])
{
	unsigned map;
	int tpid;

	if (!pid) {
		strcpy(comm, "<idle>");
		return;
	}

	if (WARN_ON_ONCE(pid < 0)) {
		strcpy(comm, "<XXX>");
		return;
	}

	tpid = pid & (PID_MAX_DEFAULT - 1);
	map = savedcmd->map_pid_to_cmdline[tpid];
	if (map != NO_CMDLINE_MAP) {
		tpid = savedcmd->map_cmdline_to_pid[map];
		if (tpid == pid) {
			strscpy(comm, get_saved_cmdlines(map), TASK_COMM_LEN);
			return;
		}
	}
	strcpy(comm, "<...>");
}

void trace_find_cmdline(int pid, char comm[])
{
	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);

	__trace_find_cmdline(pid, comm);

	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
}

static int *trace_find_tgid_ptr(int pid)
{
	/*
	 * Pairs with the smp_store_release in set_tracer_flag() to ensure that
	 * if we observe a non-NULL tgid_map then we also observe the correct
	 * tgid_map_max.
	 */
	int *map = smp_load_acquire(&tgid_map);

	if (unlikely(!map || pid > tgid_map_max))
		return NULL;

	return &map[pid];
}

int trace_find_tgid(int pid)
{
	int *ptr = trace_find_tgid_ptr(pid);

	return ptr ? *ptr : 0;
}

static int trace_save_tgid(struct task_struct *tsk)
{
	int *ptr;

	/* treat recording of idle task as a success */
	if (!tsk->pid)
		return 1;

	ptr = trace_find_tgid_ptr(tsk->pid);
	if (!ptr)
		return 0;

	*ptr = tsk->tgid;
	return 1;
}

static bool tracing_record_taskinfo_skip(int flags)
{
	if (unlikely(!(flags & (TRACE_RECORD_CMDLINE | TRACE_RECORD_TGID))))
		return true;
	if (!__this_cpu_read(trace_taskinfo_save))
		return true;
	return false;
}

/**
 * tracing_record_taskinfo - record the task info of a task
 *
 * @task:  task to record
 * @flags: TRACE_RECORD_CMDLINE for recording comm
 *         TRACE_RECORD_TGID for recording tgid
 */
void tracing_record_taskinfo(struct task_struct *task, int flags)
{
	bool done;

	if (tracing_record_taskinfo_skip(flags))
		return;

	/*
	 * Record as much task information as possible. If some fail, continue
	 * to try to record the others.
	 */
	done = !(flags & TRACE_RECORD_CMDLINE) || trace_save_cmdline(task);
	done &= !(flags & TRACE_RECORD_TGID) || trace_save_tgid(task);

	/* If recording any information failed, retry again soon. */
	if (!done)
		return;

	__this_cpu_write(trace_taskinfo_save, false);
}

/**
 * tracing_record_taskinfo_sched_switch - record task info for sched_switch
 *
 * @prev: previous task during sched_switch
 * @next: next task during sched_switch
 * @flags: TRACE_RECORD_CMDLINE for recording comm
 *         TRACE_RECORD_TGID for recording tgid
 */
void tracing_record_taskinfo_sched_switch(struct task_struct *prev,
					  struct task_struct *next, int flags)
{
	bool done;

	if (tracing_record_taskinfo_skip(flags))
		return;

	/*
	 * Record as much task information as possible. If some fail, continue
	 * to try to record the others.
	 */
	done  = !(flags & TRACE_RECORD_CMDLINE) || trace_save_cmdline(prev);
	done &= !(flags & TRACE_RECORD_CMDLINE) || trace_save_cmdline(next);
	done &= !(flags & TRACE_RECORD_TGID) || trace_save_tgid(prev);
	done &= !(flags & TRACE_RECORD_TGID) || trace_save_tgid(next);

	/* If recording any information failed, retry again soon. */
	if (!done)
		return;

	__this_cpu_write(trace_taskinfo_save, false);
}

/* Helpers to record a specific task information */
void tracing_record_cmdline(struct task_struct *task)
{
	tracing_record_taskinfo(task, TRACE_RECORD_CMDLINE);
}

void tracing_record_tgid(struct task_struct *task)
{
	tracing_record_taskinfo(task, TRACE_RECORD_TGID);
}

int trace_alloc_tgid_map(void)
{
	int *map;

	if (tgid_map)
		return 0;

	tgid_map_max = pid_max;
	map = kvcalloc(tgid_map_max + 1, sizeof(*tgid_map),
		       GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	/*
	 * Pairs with smp_load_acquire() in
	 * trace_find_tgid_ptr() to ensure that if it observes
	 * the tgid_map we just allocated then it also observes
	 * the corresponding tgid_map_max value.
	 */
	smp_store_release(&tgid_map, map);
	return 0;
}

static void *saved_tgids_next(struct seq_file *m, void *v, loff_t *pos)
{
	int pid = ++(*pos);

	return trace_find_tgid_ptr(pid);
}

static void *saved_tgids_start(struct seq_file *m, loff_t *pos)
{
	int pid = *pos;

	return trace_find_tgid_ptr(pid);
}

static void saved_tgids_stop(struct seq_file *m, void *v)
{
}

static int saved_tgids_show(struct seq_file *m, void *v)
{
	int *entry = (int *)v;
	int pid = entry - tgid_map;
	int tgid = *entry;

	if (tgid == 0)
		return SEQ_SKIP;

	seq_printf(m, "%d %d\n", pid, tgid);
	return 0;
}

static const struct seq_operations tracing_saved_tgids_seq_ops = {
	.start		= saved_tgids_start,
	.stop		= saved_tgids_stop,
	.next		= saved_tgids_next,
	.show		= saved_tgids_show,
};

static int tracing_saved_tgids_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	return seq_open(filp, &tracing_saved_tgids_seq_ops);
}


const struct file_operations tracing_saved_tgids_fops = {
	.open		= tracing_saved_tgids_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void *saved_cmdlines_next(struct seq_file *m, void *v, loff_t *pos)
{
	unsigned int *ptr = v;

	if (*pos || m->count)
		ptr++;

	(*pos)++;

	for (; ptr < &savedcmd->map_cmdline_to_pid[savedcmd->cmdline_num];
	     ptr++) {
		if (*ptr == -1 || *ptr == NO_CMDLINE_MAP)
			continue;

		return ptr;
	}

	return NULL;
}

static void *saved_cmdlines_start(struct seq_file *m, loff_t *pos)
{
	void *v;
	loff_t l = 0;

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);

	v = &savedcmd->map_cmdline_to_pid[0];
	while (l <= *pos) {
		v = saved_cmdlines_next(m, v, &l);
		if (!v)
			return NULL;
	}

	return v;
}

static void saved_cmdlines_stop(struct seq_file *m, void *v)
{
	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
}

static int saved_cmdlines_show(struct seq_file *m, void *v)
{
	char buf[TASK_COMM_LEN];
	unsigned int *pid = v;

	__trace_find_cmdline(*pid, buf);
	seq_printf(m, "%d %s\n", *pid, buf);
	return 0;
}

static const struct seq_operations tracing_saved_cmdlines_seq_ops = {
	.start		= saved_cmdlines_start,
	.next		= saved_cmdlines_next,
	.stop		= saved_cmdlines_stop,
	.show		= saved_cmdlines_show,
};

static int tracing_saved_cmdlines_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = tracing_check_open_get_tr(NULL);
	if (ret)
		return ret;

	return seq_open(filp, &tracing_saved_cmdlines_seq_ops);
}

const struct file_operations tracing_saved_cmdlines_fops = {
	.open		= tracing_saved_cmdlines_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static ssize_t
tracing_saved_cmdlines_size_read(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);
	r = scnprintf(buf, sizeof(buf), "%u\n", savedcmd->cmdline_num);
	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

void trace_free_saved_cmdlines_buffer(void)
{
	free_saved_cmdlines_buffer(savedcmd);
}

static int tracing_resize_saved_cmdlines(unsigned int val)
{
	struct saved_cmdlines_buffer *s, *savedcmd_temp;

	s = allocate_cmdlines_buffer(val);
	if (!s)
		return -ENOMEM;

	preempt_disable();
	arch_spin_lock(&trace_cmdline_lock);
	savedcmd_temp = savedcmd;
	savedcmd = s;
	arch_spin_unlock(&trace_cmdline_lock);
	preempt_enable();
	free_saved_cmdlines_buffer(savedcmd_temp);

	return 0;
}

static ssize_t
tracing_saved_cmdlines_size_write(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	/* must have at least 1 entry or less than PID_MAX_DEFAULT */
	if (!val || val > PID_MAX_DEFAULT)
		return -EINVAL;

	ret = tracing_resize_saved_cmdlines((unsigned int)val);
	if (ret < 0)
		return ret;

	*ppos += cnt;

	return cnt;
}

const struct file_operations tracing_saved_cmdlines_size_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_saved_cmdlines_size_read,
	.write		= tracing_saved_cmdlines_size_write,
};
