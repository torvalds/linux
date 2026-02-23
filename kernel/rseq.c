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
#include <linux/hrtimer.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/ratelimit.h>
#include <linux/rseq_entry.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <asm/ptrace.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rseq.h>

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
		stats.fastpath	+= data_race(per_cpu(rseq_stats.fastpath, cpu));
		stats.ids	+= data_race(per_cpu(rseq_stats.ids, cpu));
		stats.cs	+= data_race(per_cpu(rseq_stats.cs, cpu));
		stats.clear	+= data_race(per_cpu(rseq_stats.clear, cpu));
		stats.fixup	+= data_race(per_cpu(rseq_stats.fixup, cpu));
		if (IS_ENABLED(CONFIG_RSEQ_SLICE_EXTENSION)) {
			stats.s_granted	+= data_race(per_cpu(rseq_stats.s_granted, cpu));
			stats.s_expired	+= data_race(per_cpu(rseq_stats.s_expired, cpu));
			stats.s_revoked	+= data_race(per_cpu(rseq_stats.s_revoked, cpu));
			stats.s_yielded	+= data_race(per_cpu(rseq_stats.s_yielded, cpu));
			stats.s_aborted	+= data_race(per_cpu(rseq_stats.s_aborted, cpu));
		}
	}

	seq_printf(m, "exit:   %16lu\n", stats.exit);
	seq_printf(m, "signal: %16lu\n", stats.signal);
	seq_printf(m, "slowp:  %16lu\n", stats.slowpath);
	seq_printf(m, "fastp:  %16lu\n", stats.fastpath);
	seq_printf(m, "ids:    %16lu\n", stats.ids);
	seq_printf(m, "cs:     %16lu\n", stats.cs);
	seq_printf(m, "clear:  %16lu\n", stats.clear);
	seq_printf(m, "fixup:  %16lu\n", stats.fixup);
	if (IS_ENABLED(CONFIG_RSEQ_SLICE_EXTENSION)) {
		seq_printf(m, "sgrant: %16lu\n", stats.s_granted);
		seq_printf(m, "sexpir: %16lu\n", stats.s_expired);
		seq_printf(m, "srevok: %16lu\n", stats.s_revoked);
		seq_printf(m, "syield: %16lu\n", stats.s_yielded);
		seq_printf(m, "sabort: %16lu\n", stats.s_aborted);
	}
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

static void rseq_slice_ext_init(struct dentry *root_dir);

static int __init rseq_debugfs_init(void)
{
	struct dentry *root_dir = debugfs_create_dir("rseq", NULL);

	debugfs_create_file("debug", 0644, root_dir, NULL, &debug_ops);
	rseq_stats_init(root_dir);
	if (IS_ENABLED(CONFIG_RSEQ_SLICE_EXTENSION))
		rseq_slice_ext_init(root_dir);
	return 0;
}
__initcall(rseq_debugfs_init);

static bool rseq_set_ids(struct task_struct *t, struct rseq_ids *ids, u32 node_id)
{
	return rseq_set_ids_get_csaddr(t, ids, node_id, NULL);
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

static void rseq_slowpath_update_usr(struct pt_regs *regs)
{
	/*
	 * Preserve rseq state and user_irq state. The generic entry code
	 * clears user_irq on the way out, the non-generic entry
	 * architectures are not having user_irq.
	 */
	const struct rseq_event evt_mask = { .has_rseq = true, .user_irq = true, };
	struct task_struct *t = current;
	struct rseq_ids ids;
	u32 node_id;
	bool event;

	if (unlikely(t->flags & PF_EXITING))
		return;

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
	scoped_guard(irq) {
		event = t->rseq.event.sched_switch;
		t->rseq.event.all &= evt_mask.all;
		ids.cpu_id = task_cpu(t);
		ids.mm_cid = task_mm_cid(t);
	}

	if (!event)
		return;

	node_id = cpu_to_node(ids.cpu_id);

	if (unlikely(!rseq_update_usr(t, regs, &ids, node_id))) {
		/*
		 * Clear the errors just in case this might survive magically, but
		 * leave the rest intact.
		 */
		t->rseq.event.error = 0;
		force_sig(SIGSEGV);
	}
}

void __rseq_handle_slowpath(struct pt_regs *regs)
{
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

	rseq_slowpath_update_usr(regs);
}

void __rseq_signal_deliver(int sig, struct pt_regs *regs)
{
	rseq_stat_inc(rseq_stats.signal);
	/*
	 * Don't update IDs, they are handled on exit to user if
	 * necessary. The important thing is to abort a critical section of
	 * the interrupted context as after this point the instruction
	 * pointer in @regs points to the signal handler.
	 */
	if (unlikely(!rseq_handle_cs(current, regs))) {
		/*
		 * Clear the errors just in case this might survive
		 * magically, but leave the rest intact.
		 */
		current->rseq.event.error = 0;
		force_sigsegv(sig);
	}
}

/*
 * Terminate the process if a syscall is issued within a restartable
 * sequence.
 */
void __rseq_debug_syscall_return(struct pt_regs *regs)
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

#ifdef CONFIG_DEBUG_RSEQ
/* Kept around to keep GENERIC_ENTRY=n architectures supported. */
void rseq_syscall(struct pt_regs *regs)
{
	__rseq_debug_syscall_return(regs);
}
#endif

static bool rseq_reset_ids(void)
{
	struct rseq_ids ids = {
		.cpu_id		= RSEQ_CPU_ID_UNINITIALIZED,
		.mm_cid		= 0,
	};

	/*
	 * If this fails, terminate it because this leaves the kernel in
	 * stupid state as exit to user space will try to fixup the ids
	 * again.
	 */
	if (rseq_set_ids(current, &ids, 0))
		return true;

	force_sig(SIGSEGV);
	return false;
}

/* The original rseq structure size (including padding) is 32 bytes. */
#define ORIG_RSEQ_SIZE		32

/*
 * sys_rseq - setup restartable sequences for caller thread.
 */
SYSCALL_DEFINE4(rseq, struct rseq __user *, rseq, u32, rseq_len, int, flags, u32, sig)
{
	u32 rseqfl = 0;

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
		if (!rseq_reset_ids())
			return -EFAULT;
		rseq_reset(current);
		return 0;
	}

	if (unlikely(flags & ~(RSEQ_FLAG_SLICE_EXT_DEFAULT_ON)))
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

	if (IS_ENABLED(CONFIG_RSEQ_SLICE_EXTENSION)) {
		rseqfl |= RSEQ_CS_FLAG_SLICE_EXT_AVAILABLE;
		if (rseq_slice_extension_enabled() &&
		    (flags & RSEQ_FLAG_SLICE_EXT_DEFAULT_ON))
			rseqfl |= RSEQ_CS_FLAG_SLICE_EXT_ENABLED;
	}

	scoped_user_write_access(rseq, efault) {
		/*
		 * If the rseq_cs pointer is non-NULL on registration, clear it to
		 * avoid a potential segfault on return to user-space. The proper thing
		 * to do would have been to fail the registration but this would break
		 * older libcs that reuse the rseq area for new threads without
		 * clearing the fields. Don't bother reading it, just reset it.
		 */
		unsafe_put_user(0UL, &rseq->rseq_cs, efault);
		unsafe_put_user(rseqfl, &rseq->flags, efault);
		/* Initialize IDs in user space */
		unsafe_put_user(RSEQ_CPU_ID_UNINITIALIZED, &rseq->cpu_id_start, efault);
		unsafe_put_user(RSEQ_CPU_ID_UNINITIALIZED, &rseq->cpu_id, efault);
		unsafe_put_user(0U, &rseq->node_id, efault);
		unsafe_put_user(0U, &rseq->mm_cid, efault);
		unsafe_put_user(0U, &rseq->slice_ctrl.all, efault);
	}

	/*
	 * Activate the registration by setting the rseq area address, length
	 * and signature in the task struct.
	 */
	current->rseq.usrptr = rseq;
	current->rseq.len = rseq_len;
	current->rseq.sig = sig;

#ifdef CONFIG_RSEQ_SLICE_EXTENSION
	current->rseq.slice.state.enabled = !!(rseqfl & RSEQ_CS_FLAG_SLICE_EXT_ENABLED);
#endif

	/*
	 * If rseq was previously inactive, and has just been
	 * registered, ensure the cpu_id_start and cpu_id fields
	 * are updated before returning to user-space.
	 */
	current->rseq.event.has_rseq = true;
	rseq_force_update();
	return 0;

efault:
	return -EFAULT;
}

#ifdef CONFIG_RSEQ_SLICE_EXTENSION
struct slice_timer {
	struct hrtimer	timer;
	void		*cookie;
};

static const unsigned int rseq_slice_ext_nsecs_min =  5 * NSEC_PER_USEC;
static const unsigned int rseq_slice_ext_nsecs_max = 50 * NSEC_PER_USEC;
unsigned int rseq_slice_ext_nsecs __read_mostly = rseq_slice_ext_nsecs_min;
static DEFINE_PER_CPU(struct slice_timer, slice_timer);
DEFINE_STATIC_KEY_TRUE(rseq_slice_extension_key);

/*
 * When the timer expires and the task is still in user space, the return
 * from interrupt will revoke the grant and schedule. If the task already
 * entered the kernel via a syscall and the timer fires before the syscall
 * work was able to cancel it, then depending on the preemption model this
 * will either reschedule on return from interrupt or in the syscall work
 * below.
 */
static enum hrtimer_restart rseq_slice_expired(struct hrtimer *tmr)
{
	struct slice_timer *st = container_of(tmr, struct slice_timer, timer);

	/*
	 * Validate that the task which armed the timer is still on the
	 * CPU. It could have been scheduled out without canceling the
	 * timer.
	 */
	if (st->cookie == current && current->rseq.slice.state.granted) {
		rseq_stat_inc(rseq_stats.s_expired);
		set_need_resched_current();
	}
	return HRTIMER_NORESTART;
}

bool __rseq_arm_slice_extension_timer(void)
{
	struct slice_timer *st = this_cpu_ptr(&slice_timer);
	struct task_struct *curr = current;

	lockdep_assert_irqs_disabled();

	/*
	 * This check prevents a task, which got a time slice extension
	 * granted, from exceeding the maximum scheduling latency when the
	 * grant expired before going out to user space. Don't bother to
	 * clear the grant here, it will be cleaned up automatically before
	 * going out to user space after being scheduled back in.
	 */
	if ((unlikely(curr->rseq.slice.expires < ktime_get_mono_fast_ns()))) {
		set_need_resched_current();
		return true;
	}

	/*
	 * Store the task pointer as a cookie for comparison in the timer
	 * function. This is safe as the timer is CPU local and cannot be
	 * in the expiry function at this point.
	 */
	st->cookie = curr;
	hrtimer_start(&st->timer, curr->rseq.slice.expires, HRTIMER_MODE_ABS_PINNED_HARD);
	/* Arm the syscall entry work */
	set_task_syscall_work(curr, SYSCALL_RSEQ_SLICE);
	return false;
}

static void rseq_cancel_slice_extension_timer(void)
{
	struct slice_timer *st = this_cpu_ptr(&slice_timer);

	/*
	 * st->cookie can be safely read as preemption is disabled and the
	 * timer is CPU local.
	 *
	 * As this is most probably the first expiring timer, the cancel is
	 * expensive as it has to reprogram the hardware, but that's less
	 * expensive than going through a full hrtimer_interrupt() cycle
	 * for nothing.
	 *
	 * hrtimer_try_to_cancel() is sufficient here as the timer is CPU
	 * local and once the hrtimer code disabled interrupts the timer
	 * callback cannot be running.
	 */
	if (st->cookie == current)
		hrtimer_try_to_cancel(&st->timer);
}

static inline void rseq_slice_set_need_resched(struct task_struct *curr)
{
	/*
	 * The interrupt guard is required to prevent inconsistent state in
	 * this case:
	 *
	 * set_tsk_need_resched()
	 * --> Interrupt
	 *       wakeup()
	 *        set_tsk_need_resched()
	 *	  set_preempt_need_resched()
	 *     schedule_on_return()
	 *        clear_tsk_need_resched()
	 *	  clear_preempt_need_resched()
	 * set_preempt_need_resched()		<- Inconsistent state
	 *
	 * This is safe vs. a remote set of TIF_NEED_RESCHED because that
	 * only sets the already set bit and does not create inconsistent
	 * state.
	 */
	scoped_guard(irq)
		set_need_resched_current();
}

static void rseq_slice_validate_ctrl(u32 expected)
{
	u32 __user *sctrl = &current->rseq.usrptr->slice_ctrl.all;
	u32 uval;

	if (get_user(uval, sctrl) || uval != expected)
		force_sig(SIGSEGV);
}

/*
 * Invoked from syscall entry if a time slice extension was granted and the
 * kernel did not clear it before user space left the critical section.
 *
 * While the recommended way to relinquish the CPU side effect free is
 * rseq_slice_yield(2), any syscall within a granted slice terminates the
 * grant and immediately reschedules if required. This supports onion layer
 * applications, where the code requesting the grant cannot control the
 * code within the critical section.
 */
void rseq_syscall_enter_work(long syscall)
{
	struct task_struct *curr = current;
	struct rseq_slice_ctrl ctrl = { .granted = curr->rseq.slice.state.granted };

	clear_task_syscall_work(curr, SYSCALL_RSEQ_SLICE);

	if (static_branch_unlikely(&rseq_debug_enabled))
		rseq_slice_validate_ctrl(ctrl.all);

	/*
	 * The kernel might have raced, revoked the grant and updated
	 * userspace, but kept the SLICE work set.
	 */
	if (!ctrl.granted)
		return;

	/*
	 * Required to stabilize the per CPU timer pointer and to make
	 * set_tsk_need_resched() correct on PREEMPT[RT] kernels.
	 *
	 * Leaving the scope will reschedule on preemption models FULL,
	 * LAZY and RT if necessary.
	 */
	scoped_guard(preempt) {
		rseq_cancel_slice_extension_timer();
		/*
		 * Now that preemption is disabled, quickly check whether
		 * the task was already rescheduled before arriving here.
		 */
		if (!curr->rseq.event.sched_switch) {
			rseq_slice_set_need_resched(curr);

			if (syscall == __NR_rseq_slice_yield) {
				rseq_stat_inc(rseq_stats.s_yielded);
				/* Update the yielded state for syscall return */
				curr->rseq.slice.yielded = 1;
			} else {
				rseq_stat_inc(rseq_stats.s_aborted);
			}
		}
	}
	/* Reschedule on NONE/VOLUNTARY preemption models */
	cond_resched();

	/* Clear the grant in kernel state and user space */
	curr->rseq.slice.state.granted = false;
	if (put_user(0U, &curr->rseq.usrptr->slice_ctrl.all))
		force_sig(SIGSEGV);
}

int rseq_slice_extension_prctl(unsigned long arg2, unsigned long arg3)
{
	switch (arg2) {
	case PR_RSEQ_SLICE_EXTENSION_GET:
		if (arg3)
			return -EINVAL;
		return current->rseq.slice.state.enabled ? PR_RSEQ_SLICE_EXT_ENABLE : 0;

	case PR_RSEQ_SLICE_EXTENSION_SET: {
		u32 rflags, valid = RSEQ_CS_FLAG_SLICE_EXT_AVAILABLE;
		bool enable = !!(arg3 & PR_RSEQ_SLICE_EXT_ENABLE);

		if (arg3 & ~PR_RSEQ_SLICE_EXT_ENABLE)
			return -EINVAL;
		if (!rseq_slice_extension_enabled())
			return -ENOTSUPP;
		if (!current->rseq.usrptr)
			return -ENXIO;

		/* No change? */
		if (enable == !!current->rseq.slice.state.enabled)
			return 0;

		if (get_user(rflags, &current->rseq.usrptr->flags))
			goto die;

		if (current->rseq.slice.state.enabled)
			valid |= RSEQ_CS_FLAG_SLICE_EXT_ENABLED;

		if ((rflags & valid) != valid)
			goto die;

		rflags &= ~RSEQ_CS_FLAG_SLICE_EXT_ENABLED;
		rflags |= RSEQ_CS_FLAG_SLICE_EXT_AVAILABLE;
		if (enable)
			rflags |= RSEQ_CS_FLAG_SLICE_EXT_ENABLED;

		if (put_user(rflags, &current->rseq.usrptr->flags))
			goto die;

		current->rseq.slice.state.enabled = enable;
		return 0;
	}
	default:
		return -EINVAL;
	}
die:
	force_sig(SIGSEGV);
	return -EFAULT;
}

/**
 * sys_rseq_slice_yield - yield the current processor side effect free if a
 *			  task granted with a time slice extension is done with
 *			  the critical work before being forced out.
 *
 * Return: 1 if the task successfully yielded the CPU within the granted slice.
 *         0 if the slice extension was either never granted or was revoked by
 *	     going over the granted extension, using a syscall other than this one
 *	     or being scheduled out earlier due to a subsequent interrupt.
 *
 * The syscall does not schedule because the syscall entry work immediately
 * relinquishes the CPU and schedules if required.
 */
SYSCALL_DEFINE0(rseq_slice_yield)
{
	int yielded = !!current->rseq.slice.yielded;

	current->rseq.slice.yielded = 0;
	return yielded;
}

static int rseq_slice_ext_show(struct seq_file *m, void *p)
{
	seq_printf(m, "%d\n", rseq_slice_ext_nsecs);
	return 0;
}

static ssize_t rseq_slice_ext_write(struct file *file, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	unsigned int nsecs;

	if (kstrtouint_from_user(ubuf, count, 10, &nsecs))
		return -EINVAL;

	if (nsecs < rseq_slice_ext_nsecs_min)
		return -ERANGE;

	if (nsecs > rseq_slice_ext_nsecs_max)
		return -ERANGE;

	rseq_slice_ext_nsecs = nsecs;

	return count;
}

static int rseq_slice_ext_open(struct inode *inode, struct file *file)
{
	return single_open(file, rseq_slice_ext_show, inode->i_private);
}

static const struct file_operations slice_ext_ops = {
	.open		= rseq_slice_ext_open,
	.read		= seq_read,
	.write		= rseq_slice_ext_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rseq_slice_ext_init(struct dentry *root_dir)
{
	debugfs_create_file("slice_ext_nsec", 0644, root_dir, NULL, &slice_ext_ops);
}

static int __init rseq_slice_cmdline(char *str)
{
	bool on;

	if (kstrtobool(str, &on))
		return 0;

	if (!on)
		static_branch_disable(&rseq_slice_extension_key);
	return 1;
}
__setup("rseq_slice_ext=", rseq_slice_cmdline);

static int __init rseq_slice_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		hrtimer_setup(per_cpu_ptr(&slice_timer.timer, cpu), rseq_slice_expired,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED_HARD);
	}
	return 0;
}
device_initcall(rseq_slice_init);
#else
static void rseq_slice_ext_init(struct dentry *root_dir) { }
#endif /* CONFIG_RSEQ_SLICE_EXTENSION */
