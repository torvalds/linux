/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HW_BREAKPOINT_H
#define _LINUX_HW_BREAKPOINT_H

#include <linux/perf_event.h>
#include <uapi/linux/hw_breakpoint.h>

#ifdef CONFIG_HAVE_HW_BREAKPOINT

extern int __init init_hw_breakpoint(void);

static inline void hw_breakpoint_init(struct perf_event_attr *attr)
{
	memset(attr, 0, sizeof(*attr));

	attr->type = PERF_TYPE_BREAKPOINT;
	attr->size = sizeof(*attr);
	/*
	 * As it's for in-kernel or ptrace use, we want it to be pinned
	 * and to call its callback every hits.
	 */
	attr->pinned = 1;
	attr->sample_period = 1;
}

static inline void ptrace_breakpoint_init(struct perf_event_attr *attr)
{
	hw_breakpoint_init(attr);
	attr->exclude_kernel = 1;
}

static inline unsigned long hw_breakpoint_addr(struct perf_event *bp)
{
	return bp->attr.bp_addr;
}

static inline int hw_breakpoint_type(struct perf_event *bp)
{
	return bp->attr.bp_type;
}

static inline unsigned long hw_breakpoint_len(struct perf_event *bp)
{
	return bp->attr.bp_len;
}

extern struct perf_event *
register_user_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context,
			    struct task_struct *tsk);

/* FIXME: only change from the attr, and don't unregister */
extern int
modify_user_hw_breakpoint(struct perf_event *bp, struct perf_event_attr *attr);
extern int
modify_user_hw_breakpoint_check(struct perf_event *bp, struct perf_event_attr *attr,
				bool check);

/*
 * Kernel breakpoints are not associated with any particular thread.
 */
extern struct perf_event *
register_wide_hw_breakpoint_cpu(struct perf_event_attr *attr,
				perf_overflow_handler_t	triggered,
				void *context,
				int cpu);

extern struct perf_event * __percpu *
register_wide_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context);

extern int register_perf_hw_breakpoint(struct perf_event *bp);
extern void unregister_hw_breakpoint(struct perf_event *bp);
extern void unregister_wide_hw_breakpoint(struct perf_event * __percpu *cpu_events);
extern bool hw_breakpoint_is_used(void);

extern int dbg_reserve_bp_slot(struct perf_event *bp);
extern int dbg_release_bp_slot(struct perf_event *bp);
extern int reserve_bp_slot(struct perf_event *bp);
extern void release_bp_slot(struct perf_event *bp);
int hw_breakpoint_weight(struct perf_event *bp);
int arch_reserve_bp_slot(struct perf_event *bp);
void arch_release_bp_slot(struct perf_event *bp);
void arch_unregister_hw_breakpoint(struct perf_event *bp);

extern void flush_ptrace_hw_breakpoint(struct task_struct *tsk);

static inline struct arch_hw_breakpoint *counter_arch_bp(struct perf_event *bp)
{
	return &bp->hw.info;
}

#else /* !CONFIG_HAVE_HW_BREAKPOINT */

static inline int __init init_hw_breakpoint(void) { return 0; }

static inline struct perf_event *
register_user_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context,
			    struct task_struct *tsk)	{ return NULL; }
static inline int
modify_user_hw_breakpoint(struct perf_event *bp,
			  struct perf_event_attr *attr)	{ return -ENOSYS; }
static inline int
modify_user_hw_breakpoint_check(struct perf_event *bp, struct perf_event_attr *attr,
				bool check)	{ return -ENOSYS; }

static inline struct perf_event *
register_wide_hw_breakpoint_cpu(struct perf_event_attr *attr,
				perf_overflow_handler_t	 triggered,
				void *context,
				int cpu)		{ return NULL; }
static inline struct perf_event * __percpu *
register_wide_hw_breakpoint(struct perf_event_attr *attr,
			    perf_overflow_handler_t triggered,
			    void *context)		{ return NULL; }
static inline int
register_perf_hw_breakpoint(struct perf_event *bp)	{ return -ENOSYS; }
static inline void unregister_hw_breakpoint(struct perf_event *bp)	{ }
static inline void
unregister_wide_hw_breakpoint(struct perf_event * __percpu *cpu_events)	{ }
static inline bool hw_breakpoint_is_used(void)		{ return false; }

static inline int
reserve_bp_slot(struct perf_event *bp)			{return -ENOSYS; }
static inline void release_bp_slot(struct perf_event *bp) 		{ }

static inline void flush_ptrace_hw_breakpoint(struct task_struct *tsk)	{ }

static inline struct arch_hw_breakpoint *counter_arch_bp(struct perf_event *bp)
{
	return NULL;
}

#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#endif /* _LINUX_HW_BREAKPOINT_H */
