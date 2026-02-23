/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RSEQ_ENTRY_H
#define _LINUX_RSEQ_ENTRY_H

/* Must be outside the CONFIG_RSEQ guard to resolve the stubs */
#ifdef CONFIG_RSEQ_STATS
#include <linux/percpu.h>

struct rseq_stats {
	unsigned long	exit;
	unsigned long	signal;
	unsigned long	slowpath;
	unsigned long	fastpath;
	unsigned long	ids;
	unsigned long	cs;
	unsigned long	clear;
	unsigned long	fixup;
	unsigned long	s_granted;
	unsigned long	s_expired;
	unsigned long	s_revoked;
	unsigned long	s_yielded;
	unsigned long	s_aborted;
};

DECLARE_PER_CPU(struct rseq_stats, rseq_stats);

/*
 * Slow path has interrupts and preemption enabled, but the fast path
 * runs with interrupts disabled so there is no point in having the
 * preemption checks implied in __this_cpu_inc() for every operation.
 */
#ifdef RSEQ_BUILD_SLOW_PATH
#define rseq_stat_inc(which)	this_cpu_inc((which))
#else
#define rseq_stat_inc(which)	raw_cpu_inc((which))
#endif

#else /* CONFIG_RSEQ_STATS */
#define rseq_stat_inc(x)	do { } while (0)
#endif /* !CONFIG_RSEQ_STATS */

#ifdef CONFIG_RSEQ
#include <linux/jump_label.h>
#include <linux/rseq.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

#include <linux/tracepoint-defs.h>

#ifdef CONFIG_TRACEPOINTS
DECLARE_TRACEPOINT(rseq_update);
DECLARE_TRACEPOINT(rseq_ip_fixup);
void __rseq_trace_update(struct task_struct *t);
void __rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
			   unsigned long offset, unsigned long abort_ip);

static inline void rseq_trace_update(struct task_struct *t, struct rseq_ids *ids)
{
	if (tracepoint_enabled(rseq_update) && ids)
		__rseq_trace_update(t);
}

static inline void rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
				       unsigned long offset, unsigned long abort_ip)
{
	if (tracepoint_enabled(rseq_ip_fixup))
		__rseq_trace_ip_fixup(ip, start_ip, offset, abort_ip);
}

#else /* CONFIG_TRACEPOINT */
static inline void rseq_trace_update(struct task_struct *t, struct rseq_ids *ids) { }
static inline void rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
				       unsigned long offset, unsigned long abort_ip) { }
#endif /* !CONFIG_TRACEPOINT */

DECLARE_STATIC_KEY_MAYBE(CONFIG_RSEQ_DEBUG_DEFAULT_ENABLE, rseq_debug_enabled);

#ifdef RSEQ_BUILD_SLOW_PATH
#define rseq_inline
#else
#define rseq_inline __always_inline
#endif

#ifdef CONFIG_RSEQ_SLICE_EXTENSION
DECLARE_STATIC_KEY_TRUE(rseq_slice_extension_key);

static __always_inline bool rseq_slice_extension_enabled(void)
{
	return static_branch_likely(&rseq_slice_extension_key);
}

extern unsigned int rseq_slice_ext_nsecs;
bool __rseq_arm_slice_extension_timer(void);

static __always_inline bool rseq_arm_slice_extension_timer(void)
{
	if (!rseq_slice_extension_enabled())
		return false;

	if (likely(!current->rseq.slice.state.granted))
		return false;

	return __rseq_arm_slice_extension_timer();
}

static __always_inline void rseq_slice_clear_grant(struct task_struct *t)
{
	if (IS_ENABLED(CONFIG_RSEQ_STATS) && t->rseq.slice.state.granted)
		rseq_stat_inc(rseq_stats.s_revoked);
	t->rseq.slice.state.granted = false;
}

static __always_inline bool rseq_grant_slice_extension(bool work_pending)
{
	struct task_struct *curr = current;
	struct rseq_slice_ctrl usr_ctrl;
	union rseq_slice_state state;
	struct rseq __user *rseq;

	if (!rseq_slice_extension_enabled())
		return false;

	/* If not enabled or not a return from interrupt, nothing to do. */
	state = curr->rseq.slice.state;
	state.enabled &= curr->rseq.event.user_irq;
	if (likely(!state.state))
		return false;

	rseq = curr->rseq.usrptr;
	scoped_user_rw_access(rseq, efault) {

		/*
		 * Quick check conditions where a grant is not possible or
		 * needs to be revoked.
		 *
		 *  1) Any TIF bit which needs to do extra work aside of
		 *     rescheduling prevents a grant.
		 *
		 *  2) A previous rescheduling request resulted in a slice
		 *     extension grant.
		 */
		if (unlikely(work_pending || state.granted)) {
			/* Clear user control unconditionally. No point for checking */
			unsafe_put_user(0U, &rseq->slice_ctrl.all, efault);
			rseq_slice_clear_grant(curr);
			return false;
		}

		unsafe_get_user(usr_ctrl.all, &rseq->slice_ctrl.all, efault);
		if (likely(!(usr_ctrl.request)))
			return false;

		/* Grant the slice extention */
		usr_ctrl.request = 0;
		usr_ctrl.granted = 1;
		unsafe_put_user(usr_ctrl.all, &rseq->slice_ctrl.all, efault);
	}

	rseq_stat_inc(rseq_stats.s_granted);

	curr->rseq.slice.state.granted = true;
	/* Store expiry time for arming the timer on the way out */
	curr->rseq.slice.expires = data_race(rseq_slice_ext_nsecs) + ktime_get_mono_fast_ns();
	/*
	 * This is racy against a remote CPU setting TIF_NEED_RESCHED in
	 * several ways:
	 *
	 * 1)
	 *	CPU0			CPU1
	 *	clear_tsk()
	 *				set_tsk()
	 *	clear_preempt()
	 *				Raise scheduler IPI on CPU0
	 *	--> IPI
	 *	    fold_need_resched() -> Folds correctly
	 * 2)
	 *	CPU0			CPU1
	 *				set_tsk()
	 *	clear_tsk()
	 *	clear_preempt()
	 *				Raise scheduler IPI on CPU0
	 *	--> IPI
	 *	    fold_need_resched() <- NOOP as TIF_NEED_RESCHED is false
	 *
	 * #1 is not any different from a regular remote reschedule as it
	 *    sets the previously not set bit and then raises the IPI which
	 *    folds it into the preempt counter
	 *
	 * #2 is obviously incorrect from a scheduler POV, but it's not
	 *    differently incorrect than the code below clearing the
	 *    reschedule request with the safety net of the timer.
	 *
	 * The important part is that the clearing is protected against the
	 * scheduler IPI and also against any other interrupt which might
	 * end up waking up a task and setting the bits in the middle of
	 * the operation:
	 *
	 *	clear_tsk()
	 *	---> Interrupt
	 *		wakeup_on_this_cpu()
	 *		set_tsk()
	 *		set_preempt()
	 *	clear_preempt()
	 *
	 * which would be inconsistent state.
	 */
	scoped_guard(irq) {
		clear_tsk_need_resched(curr);
		clear_preempt_need_resched();
	}
	return true;

efault:
	force_sig(SIGSEGV);
	return false;
}

#else /* CONFIG_RSEQ_SLICE_EXTENSION */
static inline bool rseq_slice_extension_enabled(void) { return false; }
static inline bool rseq_arm_slice_extension_timer(void) { return false; }
static inline void rseq_slice_clear_grant(struct task_struct *t) { }
static inline bool rseq_grant_slice_extension(bool work_pending) { return false; }
#endif /* !CONFIG_RSEQ_SLICE_EXTENSION */

bool rseq_debug_update_user_cs(struct task_struct *t, struct pt_regs *regs, unsigned long csaddr);
bool rseq_debug_validate_ids(struct task_struct *t);

static __always_inline void rseq_note_user_irq_entry(void)
{
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY))
		current->rseq.event.user_irq = true;
}

/*
 * Check whether there is a valid critical section and whether the
 * instruction pointer in @regs is inside the critical section.
 *
 *  - If the critical section is invalid, terminate the task.
 *
 *  - If valid and the instruction pointer is inside, set it to the abort IP.
 *
 *  - If valid and the instruction pointer is outside, clear the critical
 *    section address.
 *
 * Returns true, if the section was valid and either fixup or clear was
 * done, false otherwise.
 *
 * In the failure case task::rseq_event::fatal is set when a invalid
 * section was found. It's clear when the failure was an unresolved page
 * fault.
 *
 * If inlined into the exit to user path with interrupts disabled, the
 * caller has to protect against page faults with pagefault_disable().
 *
 * In preemptible task context this would be counterproductive as the page
 * faults could not be fully resolved. As a consequence unresolved page
 * faults in task context are fatal too.
 */

#ifdef RSEQ_BUILD_SLOW_PATH
/*
 * The debug version is put out of line, but kept here so the code stays
 * together.
 *
 * @csaddr has already been checked by the caller to be in user space
 */
bool rseq_debug_update_user_cs(struct task_struct *t, struct pt_regs *regs,
			       unsigned long csaddr)
{
	struct rseq_cs __user *ucs = (struct rseq_cs __user *)(unsigned long)csaddr;
	u64 start_ip, abort_ip, offset, cs_end, head, tasksize = TASK_SIZE;
	unsigned long ip = instruction_pointer(regs);
	u64 __user *uc_head = (u64 __user *) ucs;
	u32 usig, __user *uc_sig;

	scoped_user_rw_access(ucs, efault) {
		/*
		 * Evaluate the user pile and exit if one of the conditions
		 * is not fulfilled.
		 */
		unsafe_get_user(start_ip, &ucs->start_ip, efault);
		if (unlikely(start_ip >= tasksize))
			goto die;
		/* If outside, just clear the critical section. */
		if (ip < start_ip)
			goto clear;

		unsafe_get_user(offset, &ucs->post_commit_offset, efault);
		cs_end = start_ip + offset;
		/* Check for overflow and wraparound */
		if (unlikely(cs_end >= tasksize || cs_end < start_ip))
			goto die;

		/* If not inside, clear it. */
		if (ip >= cs_end)
			goto clear;

		unsafe_get_user(abort_ip, &ucs->abort_ip, efault);
		/* Ensure it's "valid" */
		if (unlikely(abort_ip >= tasksize || abort_ip < sizeof(*uc_sig)))
			goto die;
		/* Validate that the abort IP is not in the critical section */
		if (unlikely(abort_ip - start_ip < offset))
			goto die;

		/*
		 * Check version and flags for 0. No point in emitting
		 * deprecated warnings before dying. That could be done in
		 * the slow path eventually, but *shrug*.
		 */
		unsafe_get_user(head, uc_head, efault);
		if (unlikely(head))
			goto die;

		/* abort_ip - 4 is >= 0. See abort_ip check above */
		uc_sig = (u32 __user *)(unsigned long)(abort_ip - sizeof(*uc_sig));
		unsafe_get_user(usig, uc_sig, efault);
		if (unlikely(usig != t->rseq.sig))
			goto die;

		/* rseq_event.user_irq is only valid if CONFIG_GENERIC_IRQ_ENTRY=y */
		if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY)) {
			/* If not in interrupt from user context, let it die */
			if (unlikely(!t->rseq.event.user_irq))
				goto die;
		}
		unsafe_put_user(0ULL, &t->rseq.usrptr->rseq_cs, efault);
		instruction_pointer_set(regs, (unsigned long)abort_ip);
		rseq_stat_inc(rseq_stats.fixup);
		break;
	clear:
		unsafe_put_user(0ULL, &t->rseq.usrptr->rseq_cs, efault);
		rseq_stat_inc(rseq_stats.clear);
		abort_ip = 0ULL;
	}

	if (unlikely(abort_ip))
		rseq_trace_ip_fixup(ip, start_ip, offset, abort_ip);
	return true;
die:
	t->rseq.event.fatal = true;
efault:
	return false;
}

/*
 * On debug kernels validate that user space did not mess with it if the
 * debug branch is enabled.
 */
bool rseq_debug_validate_ids(struct task_struct *t)
{
	struct rseq __user *rseq = t->rseq.usrptr;
	u32 cpu_id, uval, node_id;

	/*
	 * On the first exit after registering the rseq region CPU ID is
	 * RSEQ_CPU_ID_UNINITIALIZED and node_id in user space is 0!
	 */
	node_id = t->rseq.ids.cpu_id != RSEQ_CPU_ID_UNINITIALIZED ?
		  cpu_to_node(t->rseq.ids.cpu_id) : 0;

	scoped_user_read_access(rseq, efault) {
		unsafe_get_user(cpu_id, &rseq->cpu_id_start, efault);
		if (cpu_id != t->rseq.ids.cpu_id)
			goto die;
		unsafe_get_user(uval, &rseq->cpu_id, efault);
		if (uval != cpu_id)
			goto die;
		unsafe_get_user(uval, &rseq->node_id, efault);
		if (uval != node_id)
			goto die;
		unsafe_get_user(uval, &rseq->mm_cid, efault);
		if (uval != t->rseq.ids.mm_cid)
			goto die;
	}
	return true;
die:
	t->rseq.event.fatal = true;
efault:
	return false;
}

#endif /* RSEQ_BUILD_SLOW_PATH */

/*
 * This only ensures that abort_ip is in the user address space and
 * validates that it is preceded by the signature.
 *
 * No other sanity checks are done here, that's what the debug code is for.
 */
static rseq_inline bool
rseq_update_user_cs(struct task_struct *t, struct pt_regs *regs, unsigned long csaddr)
{
	struct rseq_cs __user *ucs = (struct rseq_cs __user *)(unsigned long)csaddr;
	unsigned long ip = instruction_pointer(regs);
	unsigned long tasksize = TASK_SIZE;
	u64 start_ip, abort_ip, offset;
	u32 usig, __user *uc_sig;

	rseq_stat_inc(rseq_stats.cs);

	if (unlikely(csaddr >= tasksize)) {
		t->rseq.event.fatal = true;
		return false;
	}

	if (static_branch_unlikely(&rseq_debug_enabled))
		return rseq_debug_update_user_cs(t, regs, csaddr);

	scoped_user_rw_access(ucs, efault) {
		unsafe_get_user(start_ip, &ucs->start_ip, efault);
		unsafe_get_user(offset, &ucs->post_commit_offset, efault);
		unsafe_get_user(abort_ip, &ucs->abort_ip, efault);

		/*
		 * No sanity checks. If user space screwed it up, it can
		 * keep the pieces. That's what debug code is for.
		 *
		 * If outside, just clear the critical section.
		 */
		if (ip - start_ip >= offset)
			goto clear;

		/*
		 * Two requirements for @abort_ip:
		 *   - Must be in user space as x86 IRET would happily return to
		 *     the kernel.
		 *   - The four bytes preceding the instruction at @abort_ip must
		 *     contain the signature.
		 *
		 * The latter protects against the following attack vector:
		 *
		 * An attacker with limited abilities to write, creates a critical
		 * section descriptor, sets the abort IP to a library function or
		 * some other ROP gadget and stores the address of the descriptor
		 * in TLS::rseq::rseq_cs. An RSEQ abort would then evade ROP
		 * protection.
		 */
		if (unlikely(abort_ip >= tasksize || abort_ip < sizeof(*uc_sig)))
			goto die;

		/* The address is guaranteed to be >= 0 and < TASK_SIZE */
		uc_sig = (u32 __user *)(unsigned long)(abort_ip - sizeof(*uc_sig));
		unsafe_get_user(usig, uc_sig, efault);
		if (unlikely(usig != t->rseq.sig))
			goto die;

		/* Invalidate the critical section */
		unsafe_put_user(0ULL, &t->rseq.usrptr->rseq_cs, efault);
		/* Update the instruction pointer */
		instruction_pointer_set(regs, (unsigned long)abort_ip);
		rseq_stat_inc(rseq_stats.fixup);
		break;
	clear:
		unsafe_put_user(0ULL, &t->rseq.usrptr->rseq_cs, efault);
		rseq_stat_inc(rseq_stats.clear);
		abort_ip = 0ULL;
	}

	if (unlikely(abort_ip))
		rseq_trace_ip_fixup(ip, start_ip, offset, abort_ip);
	return true;
die:
	t->rseq.event.fatal = true;
efault:
	return false;
}

/*
 * Updates CPU ID, Node ID and MM CID and reads the critical section
 * address, when @csaddr != NULL. This allows to put the ID update and the
 * read under the same uaccess region to spare a separate begin/end.
 *
 * As this is either invoked from a C wrapper with @csaddr = NULL or from
 * the fast path code with a valid pointer, a clever compiler should be
 * able to optimize the read out. Spares a duplicate implementation.
 *
 * Returns true, if the operation was successful, false otherwise.
 *
 * In the failure case task::rseq_event::fatal is set when invalid data
 * was found on debug kernels. It's clear when the failure was an unresolved page
 * fault.
 *
 * If inlined into the exit to user path with interrupts disabled, the
 * caller has to protect against page faults with pagefault_disable().
 *
 * In preemptible task context this would be counterproductive as the page
 * faults could not be fully resolved. As a consequence unresolved page
 * faults in task context are fatal too.
 */
static rseq_inline
bool rseq_set_ids_get_csaddr(struct task_struct *t, struct rseq_ids *ids,
			     u32 node_id, u64 *csaddr)
{
	struct rseq __user *rseq = t->rseq.usrptr;

	if (static_branch_unlikely(&rseq_debug_enabled)) {
		if (!rseq_debug_validate_ids(t))
			return false;
	}

	scoped_user_rw_access(rseq, efault) {
		unsafe_put_user(ids->cpu_id, &rseq->cpu_id_start, efault);
		unsafe_put_user(ids->cpu_id, &rseq->cpu_id, efault);
		unsafe_put_user(node_id, &rseq->node_id, efault);
		unsafe_put_user(ids->mm_cid, &rseq->mm_cid, efault);
		if (csaddr)
			unsafe_get_user(*csaddr, &rseq->rseq_cs, efault);

		/* Open coded, so it's in the same user access region */
		if (rseq_slice_extension_enabled()) {
			/* Unconditionally clear it, no point in conditionals */
			unsafe_put_user(0U, &rseq->slice_ctrl.all, efault);
		}
	}

	rseq_slice_clear_grant(t);
	/* Cache the new values */
	t->rseq.ids.cpu_cid = ids->cpu_cid;
	rseq_stat_inc(rseq_stats.ids);
	rseq_trace_update(t, ids);
	return true;
efault:
	return false;
}

/*
 * Update user space with new IDs and conditionally check whether the task
 * is in a critical section.
 */
static rseq_inline bool rseq_update_usr(struct task_struct *t, struct pt_regs *regs,
					struct rseq_ids *ids, u32 node_id)
{
	u64 csaddr;

	if (!rseq_set_ids_get_csaddr(t, ids, node_id, &csaddr))
		return false;

	/*
	 * On architectures which utilize the generic entry code this
	 * allows to skip the critical section when the entry was not from
	 * a user space interrupt, unless debug mode is enabled.
	 */
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY)) {
		if (!static_branch_unlikely(&rseq_debug_enabled)) {
			if (likely(!t->rseq.event.user_irq))
				return true;
		}
	}
	if (likely(!csaddr))
		return true;
	/* Sigh, this really needs to do work */
	return rseq_update_user_cs(t, regs, csaddr);
}

/*
 * If you want to use this then convert your architecture to the generic
 * entry code. I'm tired of building workarounds for people who can't be
 * bothered to make the maintenance of generic infrastructure less
 * burdensome. Just sucking everything into the architecture code and
 * thereby making others chase the horrible hacks and keep them working is
 * neither acceptable nor sustainable.
 */
#ifdef CONFIG_GENERIC_ENTRY

/*
 * This is inlined into the exit path because:
 *
 * 1) It's a one time comparison in the fast path when there is no event to
 *    handle
 *
 * 2) The access to the user space rseq memory (TLS) is unlikely to fault
 *    so the straight inline operation is:
 *
 *	- Four 32-bit stores only if CPU ID/ MM CID need to be updated
 *	- One 64-bit load to retrieve the critical section address
 *
 * 3) In the unlikely case that the critical section address is != NULL:
 *
 *     - One 64-bit load to retrieve the start IP
 *     - One 64-bit load to retrieve the offset for calculating the end
 *     - One 64-bit load to retrieve the abort IP
 *     - One 64-bit load to retrieve the signature
 *     - One store to clear the critical section address
 *
 * The non-debug case implements only the minimal required checking. It
 * provides protection against a rogue abort IP in kernel space, which
 * would be exploitable at least on x86, and also against a rogue CS
 * descriptor by checking the signature at the abort IP. Any fallout from
 * invalid critical section descriptors is a user space problem. The debug
 * case provides the full set of checks and terminates the task if a
 * condition is not met.
 *
 * In case of a fault or an invalid value, this sets TIF_NOTIFY_RESUME and
 * tells the caller to loop back into exit_to_user_mode_loop(). The rseq
 * slow path there will handle the failure.
 */
static __always_inline bool rseq_exit_user_update(struct pt_regs *regs, struct task_struct *t)
{
	/*
	 * Page faults need to be disabled as this is called with
	 * interrupts disabled
	 */
	guard(pagefault)();
	if (likely(!t->rseq.event.ids_changed)) {
		struct rseq __user *rseq = t->rseq.usrptr;
		/*
		 * If IDs have not changed rseq_event::user_irq must be true
		 * See rseq_sched_switch_event().
		 */
		u64 csaddr;

		scoped_user_rw_access(rseq, efault) {
			unsafe_get_user(csaddr, &rseq->rseq_cs, efault);

			/* Open coded, so it's in the same user access region */
			if (rseq_slice_extension_enabled()) {
				/* Unconditionally clear it, no point in conditionals */
				unsafe_put_user(0U, &rseq->slice_ctrl.all, efault);
			}
		}

		rseq_slice_clear_grant(t);

		if (static_branch_unlikely(&rseq_debug_enabled) || unlikely(csaddr)) {
			if (unlikely(!rseq_update_user_cs(t, regs, csaddr)))
				return false;
		}
		return true;
	}

	struct rseq_ids ids = {
		.cpu_id = task_cpu(t),
		.mm_cid = task_mm_cid(t),
	};
	u32 node_id = cpu_to_node(ids.cpu_id);

	return rseq_update_usr(t, regs, &ids, node_id);
efault:
	return false;
}

static __always_inline bool __rseq_exit_to_user_mode_restart(struct pt_regs *regs)
{
	struct task_struct *t = current;

	/*
	 * If the task did not go through schedule or got the flag enforced
	 * by the rseq syscall or execve, then nothing to do here.
	 *
	 * CPU ID and MM CID can only change when going through a context
	 * switch.
	 *
	 * rseq_sched_switch_event() sets the rseq_event::sched_switch bit
	 * only when rseq_event::has_rseq is true. That conditional is
	 * required to avoid setting the TIF bit if RSEQ is not registered
	 * for a task. rseq_event::sched_switch is cleared when RSEQ is
	 * unregistered by a task so it's sufficient to check for the
	 * sched_switch bit alone.
	 *
	 * A sane compiler requires three instructions for the nothing to do
	 * case including clearing the events, but your mileage might vary.
	 */
	if (unlikely((t->rseq.event.sched_switch))) {
		rseq_stat_inc(rseq_stats.fastpath);

		if (unlikely(!rseq_exit_user_update(regs, t)))
			return true;
	}
	/* Clear state so next entry starts from a clean slate */
	t->rseq.event.events = 0;
	return false;
}

/* Required to allow conversion to GENERIC_ENTRY w/o GENERIC_TIF_BITS */
#ifdef CONFIG_HAVE_GENERIC_TIF_BITS
static __always_inline bool test_tif_rseq(unsigned long ti_work)
{
	return ti_work & _TIF_RSEQ;
}

static __always_inline void clear_tif_rseq(void)
{
	static_assert(TIF_RSEQ != TIF_NOTIFY_RESUME);
	clear_thread_flag(TIF_RSEQ);
}
#else
static __always_inline bool test_tif_rseq(unsigned long ti_work) { return true; }
static __always_inline void clear_tif_rseq(void) { }
#endif

static __always_inline bool
rseq_exit_to_user_mode_restart(struct pt_regs *regs, unsigned long ti_work)
{
	if (unlikely(test_tif_rseq(ti_work))) {
		if (unlikely(__rseq_exit_to_user_mode_restart(regs))) {
			current->rseq.event.slowpath = true;
			set_tsk_thread_flag(current, TIF_NOTIFY_RESUME);
			return true;
		}
		clear_tif_rseq();
	}
	/*
	 * Arm the slice extension timer if nothing to do anymore and the
	 * task really goes out to user space.
	 */
	return rseq_arm_slice_extension_timer();
}

#else /* CONFIG_GENERIC_ENTRY */
static inline bool rseq_exit_to_user_mode_restart(struct pt_regs *regs, unsigned long ti_work)
{
	return false;
}
#endif /* !CONFIG_GENERIC_ENTRY */

static __always_inline void rseq_syscall_exit_to_user_mode(void)
{
	struct rseq_event *ev = &current->rseq.event;

	rseq_stat_inc(rseq_stats.exit);

	/* Needed to remove the store for the !lockdep case */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		WARN_ON_ONCE(ev->sched_switch);
		ev->events = 0;
	}
}

static __always_inline void rseq_irqentry_exit_to_user_mode(void)
{
	struct rseq_event *ev = &current->rseq.event;

	rseq_stat_inc(rseq_stats.exit);

	lockdep_assert_once(!ev->sched_switch);

	/*
	 * Ensure that event (especially user_irq) is cleared when the
	 * interrupt did not result in a schedule and therefore the
	 * rseq processing could not clear it.
	 */
	ev->events = 0;
}

/* Required to keep ARM64 working */
static __always_inline void rseq_exit_to_user_mode_legacy(void)
{
	struct rseq_event *ev = &current->rseq.event;

	rseq_stat_inc(rseq_stats.exit);

	if (static_branch_unlikely(&rseq_debug_enabled))
		WARN_ON_ONCE(ev->sched_switch);

	/*
	 * Ensure that event (especially user_irq) is cleared when the
	 * interrupt did not result in a schedule and therefore the
	 * rseq processing did not clear it.
	 */
	ev->events = 0;
}

void __rseq_debug_syscall_return(struct pt_regs *regs);

static __always_inline void rseq_debug_syscall_return(struct pt_regs *regs)
{
	if (static_branch_unlikely(&rseq_debug_enabled))
		__rseq_debug_syscall_return(regs);
}
#else /* CONFIG_RSEQ */
static inline void rseq_note_user_irq_entry(void) { }
static inline bool rseq_exit_to_user_mode_restart(struct pt_regs *regs, unsigned long ti_work)
{
	return false;
}
static inline void rseq_syscall_exit_to_user_mode(void) { }
static inline void rseq_irqentry_exit_to_user_mode(void) { }
static inline void rseq_exit_to_user_mode_legacy(void) { }
static inline void rseq_debug_syscall_return(struct pt_regs *regs) { }
static inline bool rseq_grant_slice_extension(bool work_pending) { return false; }
#endif /* !CONFIG_RSEQ */

#endif /* _LINUX_RSEQ_ENTRY_H */
