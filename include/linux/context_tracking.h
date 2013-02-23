#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H

#include <linux/sched.h>
#include <linux/percpu.h>
#include <asm/ptrace.h>

#ifdef CONFIG_CONTEXT_TRACKING
struct context_tracking {
	/*
	 * When active is false, probes are unset in order
	 * to minimize overhead: TIF flags are cleared
	 * and calls to user_enter/exit are ignored. This
	 * may be further optimized using static keys.
	 */
	bool active;
	enum {
		IN_KERNEL = 0,
		IN_USER,
	} state;
};

DECLARE_PER_CPU(struct context_tracking, context_tracking);

static inline bool context_tracking_in_user(void)
{
	return __this_cpu_read(context_tracking.state) == IN_USER;
}

static inline bool context_tracking_active(void)
{
	return __this_cpu_read(context_tracking.active);
}

extern void user_enter(void);
extern void user_exit(void);

static inline void exception_enter(struct pt_regs *regs)
{
	user_exit();
}

static inline void exception_exit(struct pt_regs *regs)
{
	if (user_mode(regs))
		user_enter();
}

extern void context_tracking_task_switch(struct task_struct *prev,
					 struct task_struct *next);
#else
static inline bool context_tracking_in_user(void) { return false; }
static inline void user_enter(void) { }
static inline void user_exit(void) { }
static inline void exception_enter(struct pt_regs *regs) { }
static inline void exception_exit(struct pt_regs *regs) { }
static inline void context_tracking_task_switch(struct task_struct *prev,
						struct task_struct *next) { }
#endif /* !CONFIG_CONTEXT_TRACKING */

#endif
