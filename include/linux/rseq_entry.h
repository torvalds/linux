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
	unsigned long	ids;
	unsigned long	cs;
	unsigned long	clear;
	unsigned long	fixup;
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

bool rseq_debug_update_user_cs(struct task_struct *t, struct pt_regs *regs, unsigned long csaddr);

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
	u64 start_ip, abort_ip, offset;
	u32 usig, __user *uc_sig;

	rseq_stat_inc(rseq_stats.cs);

	if (unlikely(csaddr >= TASK_SIZE)) {
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
		if (abort_ip >= TASK_SIZE || abort_ip < sizeof(*uc_sig))
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

static __always_inline void rseq_exit_to_user_mode(void)
{
	struct rseq_event *ev = &current->rseq.event;

	rseq_stat_inc(rseq_stats.exit);

	if (IS_ENABLED(CONFIG_DEBUG_RSEQ))
		WARN_ON_ONCE(ev->sched_switch);

	/*
	 * Ensure that event (especially user_irq) is cleared when the
	 * interrupt did not result in a schedule and therefore the
	 * rseq processing did not clear it.
	 */
	ev->events = 0;
}

#else /* CONFIG_RSEQ */
static inline void rseq_note_user_irq_entry(void) { }
static inline void rseq_exit_to_user_mode(void) { }
#endif /* !CONFIG_RSEQ */

#endif /* _LINUX_RSEQ_ENTRY_H */
