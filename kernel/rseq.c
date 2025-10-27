// SPDX-License-Identifier: GPL-2.0+
/*
 * Restartable sequences system call
 *
 * Copyright (C) 2015, Google, Inc.,
 * Paul Turner <pjt@google.com> and Andrew Hunter <ahh@google.com>
 * Copyright (C) 2015-2018, EfficiOS Inc.,
 * Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

/*
 * Restartable sequences are a lightweight interface that allows
 * user-level code to be executed atomically relative to scheduler
 * preemption and signal delivery. Typically used for implementing
 * per-cpu operations.
 *
 * It allows user-space to perform update operations on per-cpu data
 * without requiring heavy-weight atomic operations.
 *
 * Detailed algorithm of rseq user-space assembly sequences:
 *
 *                     init(rseq_cs)
 *                     cpu = TLS->rseq::cpu_id_start
 *   [1]               TLS->rseq::rseq_cs = rseq_cs
 *   [start_ip]        ----------------------------
 *   [2]               if (cpu != TLS->rseq::cpu_id)
 *                             goto abort_ip;
 *   [3]               <last_instruction_in_cs>
 *   [post_commit_ip]  ----------------------------
 *
 *   The address of jump target abort_ip must be outside the critical
 *   region, i.e.:
 *
 *     [abort_ip] < [start_ip]  || [abort_ip] >= [post_commit_ip]
 *
 *   Steps [2]-[3] (inclusive) need to be a sequence of instructions in
 *   userspace that can handle being interrupted between any of those
 *   instructions, and then resumed to the abort_ip.
 *
 *   1.  Userspace stores the address of the struct rseq_cs assembly
 *       block descriptor into the rseq_cs field of the registered
 *       struct rseq TLS area. This update is performed through a single
 *       store within the inline assembly instruction sequence.
 *       [start_ip]
 *
 *   2.  Userspace tests to check whether the current cpu_id field match
 *       the cpu number loaded before start_ip, branching to abort_ip
 *       in case of a mismatch.
 *
 *       If the sequence is preempted or interrupted by a signal
 *       at or after start_ip and before post_commit_ip, then the kernel
 *       clears TLS->__rseq_abi::rseq_cs, and sets the user-space return
 *       ip to abort_ip before returning to user-space, so the preempted
 *       execution resumes at abort_ip.
 *
 *   3.  Userspace critical section final instruction before
 *       post_commit_ip is the commit. The critical section is
 *       self-terminating.
 *       [post_commit_ip]
 *
 *   4.  <success>
 *
 *   On failure at [2], or if interrupted by preempt or signal delivery
 *   between [1] and [3]:
 *
 *       [abort_ip]
 *   F1. <failure>
 */

/* Required to select the proper per_cpu ops for rseq_stats_inc() */
#define RSEQ_BUILD_SLOW_PATH

#include <linux/debugfs.h>
#include <linux/ratelimit.h>
#include <linux/rseq_entry.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <asm/ptrace.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rseq.h>

#ifdef CONFIG_MEMBARRIER
# define RSEQ_EVENT_GUARD	irq
#else
# define RSEQ_EVENT_GUARD	preempt
#endif

/* The original rseq structure size (including padding) is 32 bytes. */
#define ORIG_RSEQ_SIZE		32

#define RSEQ_CS_NO_RESTART_FLAGS (RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT | \
				  RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL | \
				  RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE)

DEFINE_STATIC_KEY_MAYBE(CONFIG_RSEQ_DEBUG_DEFAULT_ENABLE, rseq_debug_enabled);

static inline void rseq_control_debug(bool on)
{
	if (on)
		static_branch_enable(&rseq_debug_enabled);
	else
		static_branch_disable(&rseq_debug_enabled);
}

static int __init rseq_setup_debug(char *str)
{
	bool on;

	if (kstrtobool(str, &on))
		return -EINVAL;
	rseq_control_debug(on);
	return 1;
}
__setup("rseq_debug=", rseq_setup_debug);

#ifdef CONFIG_TRACEPOINTS
/*
 * Out of line, so the actual update functions can be in a header to be
 * inlined into the exit to user code.
 */
void __rseq_trace_update(struct task_struct *t)
{
	trace_rseq_update(t);
}

void __rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
			   unsigned long offset, unsigned long abort_ip)
{
	trace_rseq_ip_fixup(ip, start_ip, offset, abort_ip);
}
#endif /* CONFIG_TRACEPOINTS */

#ifdef CONFIG_DEBUG_FS
#ifdef CONFIG_RSEQ_STATS
DEFINE_PER_CPU(struct rseq_stats, rseq_stats);

static int rseq_stats_show(struct seq_file *m, void *p)
{
	struct rseq_stats stats = { };
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		stats.exit	+= data_race(per_cpu(rseq_stats.exit, cpu));
		stats.signal	+= data_race(per_cpu(rseq_stats.signal, cpu));
		stats.slowpath	+= data_race(per_cpu(rseq_stats.slowpath, cpu));
		stats.ids	+= data_race(per_cpu(rseq_stats.ids, cpu));
		stats.cs	+= data_race(per_cpu(rseq_stats.cs, cpu));
		stats.clear	+= data_race(per_cpu(rseq_stats.clear, cpu));
		stats.fixup	+= data_race(per_cpu(rseq_stats.fixup, cpu));
	}

	seq_printf(m, "exit:   %16lu\n", stats.exit);
	seq_printf(m, "signal: %16lu\n", stats.signal);
	seq_printf(m, "slowp:  %16lu\n", stats.slowpath);
	seq_printf(m, "ids:    %16lu\n", stats.ids);
	seq_printf(m, "cs:     %16lu\n", stats.cs);
	seq_printf(m, "clear:  %16lu\n", stats.clear);
	seq_printf(m, "fixup:  %16lu\n", stats.fixup);
	return 0;
}

static int rseq_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, rseq_stats_show, inode->i_private);
}

static const struct file_operations stat_ops = {
	.open		= rseq_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init rseq_stats_init(struct dentry *root_dir)
{
	debugfs_create_file("stats", 0444, root_dir, NULL, &stat_ops);
	return 0;
}
#else
static inline void rseq_stats_init(struct dentry *root_dir) { }
#endif /* CONFIG_RSEQ_STATS */

static int rseq_debug_show(struct seq_file *m, void *p)
{
	bool on = static_branch_unlikely(&rseq_debug_enabled);

	seq_printf(m, "%d\n", on);
	return 0;
}

static ssize_t rseq_debug_write(struct file *file, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	bool on;

	if (kstrtobool_from_user(ubuf, count, &on))
		return -EINVAL;

	rseq_control_debug(on);
	return count;
}

static int rseq_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, rseq_debug_show, inode->i_private);
}

static const struct file_operations debug_ops = {
	.open		= rseq_debug_open,
	.read		= seq_read,
	.write		= rseq_debug_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init rseq_debugfs_init(void)
{
	struct dentry *root_dir = debugfs_create_dir("rseq", NULL);

	debugfs_create_file("debug", 0644, root_dir, NULL, &debug_ops);
	rseq_stats_init(root_dir);
	return 0;
}
__initcall(rseq_debugfs_init);
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_DEBUG_RSEQ
static struct rseq *rseq_kernel_fields(struct task_struct *t)
{
	return (struct rseq *) t->rseq_fields;
}

static int rseq_validate_ro_fields(struct task_struct *t)
{
	static DEFINE_RATELIMIT_STATE(_rs,
				      DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	u32 cpu_id_start, cpu_id, node_id, mm_cid;
	struct rseq __user *rseq = t->rseq.usrptr;

	/*
	 * Validate fields which are required to be read-only by
	 * user-space.
	 */
	if (!user_read_access_begin(rseq, t->rseq.len))
		goto efault;
	unsafe_get_user(cpu_id_start, &rseq->cpu_id_start, efault_end);
	unsafe_get_user(cpu_id, &rseq->cpu_id, efault_end);
	unsafe_get_user(node_id, &rseq->node_id, efault_end);
	unsafe_get_user(mm_cid, &rseq->mm_cid, efault_end);
	user_read_access_end();

	if ((cpu_id_start != rseq_kernel_fields(t)->cpu_id_start ||
	    cpu_id != rseq_kernel_fields(t)->cpu_id ||
	    node_id != rseq_kernel_fields(t)->node_id ||
	    mm_cid != rseq_kernel_fields(t)->mm_cid) && __ratelimit(&_rs)) {

		pr_warn("Detected rseq corruption for pid: %d, name: %s\n"
			"\tcpu_id_start: %u ?= %u\n"
			"\tcpu_id:       %u ?= %u\n"
			"\tnode_id:      %u ?= %u\n"
			"\tmm_cid:       %u ?= %u\n",
			t->pid, t->comm,
			cpu_id_start, rseq_kernel_fields(t)->cpu_id_start,
			cpu_id, rseq_kernel_fields(t)->cpu_id,
			node_id, rseq_kernel_fields(t)->node_id,
			mm_cid, rseq_kernel_fields(t)->mm_cid);
	}

	/* For now, only print a console warning on mismatch. */
	return 0;

efault_end:
	user_read_access_end();
efault:
	return -EFAULT;
}

/*
 * Update an rseq field and its in-kernel copy in lock-step to keep a coherent
 * state.
 */
#define rseq_unsafe_put_user(t, value, field, error_label)			\
	do {									\
		unsafe_put_user(value, &t->rseq.usrptr->field, error_label);	\
		rseq_kernel_fields(t)->field = value;				\
	} while (0)

#else
static int rseq_validate_ro_fields(struct task_struct *t)
{
	return 0;
}

#define rseq_unsafe_put_user(t, value, field, error_label)		\
	unsafe_put_user(value, &t->rseq.usrptr->field, error_label)
#endif

static int rseq_update_cpu_node_id(struct task_struct *t)
{
	struct rseq __user *rseq = t->rseq.usrptr;
	u32 cpu_id = raw_smp_processor_id();
	u32 node_id = cpu_to_node(cpu_id);
	u32 mm_cid = task_mm_cid(t);

	rseq_stat_inc(rseq_stats.ids);

	/* Validate read-only rseq fields on debug kernels */
	if (rseq_validate_ro_fields(t))
		goto efault;
	WARN_ON_ONCE((int) mm_cid < 0);

	if (!user_write_access_begin(rseq, t->rseq.len))
		goto efault;

	rseq_unsafe_put_user(t, cpu_id, cpu_id_start, efault_end);
	rseq_unsafe_put_user(t, cpu_id, cpu_id, efault_end);
	rseq_unsafe_put_user(t, node_id, node_id, efault_end);
	rseq_unsafe_put_user(t, mm_cid, mm_cid, efault_end);

	/* Cache the user space values */
	t->rseq.ids.cpu_id = cpu_id;
	t->rseq.ids.mm_cid = mm_cid;

	/*
	 * Additional feature fields added after ORIG_RSEQ_SIZE
	 * need to be conditionally updated only if
	 * t->rseq_len != ORIG_RSEQ_SIZE.
	 */
	user_write_access_end();
	trace_rseq_update(t);
	return 0;

efault_end:
	user_write_access_end();
efault:
	return -EFAULT;
}

static int rseq_reset_rseq_cpu_node_id(struct task_struct *t)
{
	struct rseq __user *rseq = t->rseq.usrptr;
	u32 cpu_id_start = 0, cpu_id = RSEQ_CPU_ID_UNINITIALIZED, node_id = 0,
	    mm_cid = 0;

	/*
	 * Validate read-only rseq fields.
	 */
	if (rseq_validate_ro_fields(t))
		goto efault;

	if (!user_write_access_begin(rseq, t->rseq.len))
		goto efault;

	/*
	 * Reset all fields to their initial state.
	 *
	 * All fields have an initial state of 0 except cpu_id which is set to
	 * RSEQ_CPU_ID_UNINITIALIZED, so that any user coming in after
	 * unregistration can figure out that rseq needs to be registered
	 * again.
	 */
	rseq_unsafe_put_user(t, cpu_id_start, cpu_id_start, efault_end);
	rseq_unsafe_put_user(t, cpu_id, cpu_id, efault_end);
	rseq_unsafe_put_user(t, node_id, node_id, efault_end);
	rseq_unsafe_put_user(t, mm_cid, mm_cid, efault_end);

	/*
	 * Additional feature fields added after ORIG_RSEQ_SIZE
	 * need to be conditionally reset only if
	 * t->rseq_len != ORIG_RSEQ_SIZE.
	 */
	user_write_access_end();
	return 0;

efault_end:
	user_write_access_end();
efault:
	return -EFAULT;
}

static bool rseq_handle_cs(struct task_struct *t, struct pt_regs *regs)
{
	struct rseq __user *urseq = t->rseq.usrptr;
	u64 csaddr;

	scoped_user_read_access(urseq, efault)
		unsafe_get_user(csaddr, &urseq->rseq_cs, efault);
	if (likely(!csaddr))
		return true;
	return rseq_update_user_cs(t, regs, csaddr);
efault:
	return false;
}

/*
 * This resume handler must always be executed between any of:
 * - preemption,
 * - signal delivery,
 * and return to user-space.
 *
 * This is how we can ensure that the entire rseq critical section
 * will issue the commit instruction only if executed atomically with
 * respect to other threads scheduled on the same CPU, and with respect
 * to signal handlers.
 */
void __rseq_handle_notify_resume(struct ksignal *ksig, struct pt_regs *regs)
{
	struct task_struct *t = current;
	bool event;
	int sig;

	/*
	 * If invoked from hypervisors before entering the guest via
	 * resume_user_mode_work(), then @regs is a NULL pointer.
	 *
	 * resume_user_mode_work() clears TIF_NOTIFY_RESUME and re-raises
	 * it before returning from the ioctl() to user space when
	 * rseq_event.sched_switch is set.
	 *
	 * So it's safe to ignore here instead of pointlessly updating it
	 * in the vcpu_run() loop.
	 */
	if (!regs)
		return;

	if (unlikely(t->flags & PF_EXITING))
		return;

	if (ksig)
		rseq_stat_inc(rseq_stats.signal);
	else
		rseq_stat_inc(rseq_stats.slowpath);

	/*
	 * Read and clear the event pending bit first. If the task
	 * was not preempted or migrated or a signal is on the way,
	 * there is no point in doing any of the heavy lifting here
	 * on production kernels. In that case TIF_NOTIFY_RESUME
	 * was raised by some other functionality.
	 *
	 * This is correct because the read/clear operation is
	 * guarded against scheduler preemption, which makes it CPU
	 * local atomic. If the task is preempted right after
	 * re-enabling preemption then TIF_NOTIFY_RESUME is set
	 * again and this function is invoked another time _before_
	 * the task is able to return to user mode.
	 *
	 * On a debug kernel, invoke the fixup code unconditionally
	 * with the result handed in to allow the detection of
	 * inconsistencies.
	 */
	scoped_guard(RSEQ_EVENT_GUARD) {
		event = t->rseq.event.sched_switch;
		t->rseq.event.sched_switch = false;
	}

	if (!IS_ENABLED(CONFIG_DEBUG_RSEQ) && !event)
		return;

	if (!rseq_handle_cs(t, regs))
		goto error;

	if (unlikely(rseq_update_cpu_node_id(t)))
		goto error;
	return;

error:
	sig = ksig ? ksig->sig : 0;
	force_sigsegv(sig);
}

#ifdef CONFIG_DEBUG_RSEQ
/*
 * Terminate the process if a syscall is issued within a restartable
 * sequence.
 */
void rseq_syscall(struct pt_regs *regs)
{
	struct task_struct *t = current;
	u64 csaddr;

	if (!t->rseq.event.has_rseq)
		return;
	if (get_user(csaddr, &t->rseq.usrptr->rseq_cs))
		goto fail;
	if (likely(!csaddr))
		return;
	if (unlikely(csaddr >= TASK_SIZE))
		goto fail;
	if (rseq_debug_update_user_cs(t, regs, csaddr))
		return;
fail:
	force_sig(SIGSEGV);
}
#endif

/*
 * sys_rseq - setup restartable sequences for caller thread.
 */
SYSCALL_DEFINE4(rseq, struct rseq __user *, rseq, u32, rseq_len, int, flags, u32, sig)
{
	int ret;

	if (flags & RSEQ_FLAG_UNREGISTER) {
		if (flags & ~RSEQ_FLAG_UNREGISTER)
			return -EINVAL;
		/* Unregister rseq for current thread. */
		if (current->rseq.usrptr != rseq || !current->rseq.usrptr)
			return -EINVAL;
		if (rseq_len != current->rseq.len)
			return -EINVAL;
		if (current->rseq.sig != sig)
			return -EPERM;
		ret = rseq_reset_rseq_cpu_node_id(current);
		if (ret)
			return ret;
		rseq_reset(current);
		return 0;
	}

	if (unlikely(flags))
		return -EINVAL;

	if (current->rseq.usrptr) {
		/*
		 * If rseq is already registered, check whether
		 * the provided address differs from the prior
		 * one.
		 */
		if (current->rseq.usrptr != rseq || rseq_len != current->rseq.len)
			return -EINVAL;
		if (current->rseq.sig != sig)
			return -EPERM;
		/* Already registered. */
		return -EBUSY;
	}

	/*
	 * If there was no rseq previously registered, ensure the provided rseq
	 * is properly aligned, as communcated to user-space through the ELF
	 * auxiliary vector AT_RSEQ_ALIGN. If rseq_len is the original rseq
	 * size, the required alignment is the original struct rseq alignment.
	 *
	 * In order to be valid, rseq_len is either the original rseq size, or
	 * large enough to contain all supported fields, as communicated to
	 * user-space through the ELF auxiliary vector AT_RSEQ_FEATURE_SIZE.
	 */
	if (rseq_len < ORIG_RSEQ_SIZE ||
	    (rseq_len == ORIG_RSEQ_SIZE && !IS_ALIGNED((unsigned long)rseq, ORIG_RSEQ_SIZE)) ||
	    (rseq_len != ORIG_RSEQ_SIZE && (!IS_ALIGNED((unsigned long)rseq, __alignof__(*rseq)) ||
					    rseq_len < offsetof(struct rseq, end))))
		return -EINVAL;
	if (!access_ok(rseq, rseq_len))
		return -EFAULT;

	/*
	 * If the rseq_cs pointer is non-NULL on registration, clear it to
	 * avoid a potential segfault on return to user-space. The proper thing
	 * to do would have been to fail the registration but this would break
	 * older libcs that reuse the rseq area for new threads without
	 * clearing the fields. Don't bother reading it, just reset it.
	 */
	if (put_user(0UL, &rseq->rseq_cs))
		return -EFAULT;

#ifdef CONFIG_DEBUG_RSEQ
	/*
	 * Initialize the in-kernel rseq fields copy for validation of
	 * read-only fields.
	 */
	if (get_user(rseq_kernel_fields(current)->cpu_id_start, &rseq->cpu_id_start) ||
	    get_user(rseq_kernel_fields(current)->cpu_id, &rseq->cpu_id) ||
	    get_user(rseq_kernel_fields(current)->node_id, &rseq->node_id) ||
	    get_user(rseq_kernel_fields(current)->mm_cid, &rseq->mm_cid))
		return -EFAULT;
#endif
	/*
	 * Activate the registration by setting the rseq area address, length
	 * and signature in the task struct.
	 */
	current->rseq.usrptr = rseq;
	current->rseq.len = rseq_len;
	current->rseq.sig = sig;

	/*
	 * If rseq was previously inactive, and has just been
	 * registered, ensure the cpu_id_start and cpu_id fields
	 * are updated before returning to user-space.
	 */
	current->rseq.event.has_rseq = true;
	rseq_sched_switch_event(current);

	return 0;
}
