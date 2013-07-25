#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H

#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/vtime.h>
#include <asm/ptrace.h>

struct context_tracking {
	/*
	 * When active is false, probes are unset in order
	 * to minimize overhead: TIF flags are cleared
	 * and calls to user_enter/exit are ignored. This
	 * may be further optimized using static keys.
	 */
	bool active;
	enum ctx_state {
		IN_KERNEL = 0,
		IN_USER,
	} state;
};

static inline void __guest_enter(void)
{
	/*
	 * This is running in ioctl context so we can avoid
	 * the call to vtime_account() with its unnecessary idle check.
	 */
	vtime_account_system(current);
	current->flags |= PF_VCPU;
}

static inline void __guest_exit(void)
{
	/*
	 * This is running in ioctl context so we can avoid
	 * the call to vtime_account() with its unnecessary idle check.
	 */
	vtime_account_system(current);
	current->flags &= ~PF_VCPU;
}

#ifdef CONFIG_CONTEXT_TRACKING
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

extern void guest_enter(void);
extern void guest_exit(void);

static inline enum ctx_state exception_enter(void)
{
	enum ctx_state prev_ctx;

	prev_ctx = this_cpu_read(context_tracking.state);
	user_exit();

	return prev_ctx;
}

static inline void exception_exit(enum ctx_state prev_ctx)
{
	if (prev_ctx == IN_USER)
		user_enter();
}

extern void context_tracking_task_switch(struct task_struct *prev,
					 struct task_struct *next);
#else
static inline bool context_tracking_in_user(void) { return false; }
static inline void user_enter(void) { }
static inline void user_exit(void) { }

static inline void guest_enter(void)
{
	__guest_enter();
}

static inline void guest_exit(void)
{
	__guest_exit();
}

static inline enum ctx_state exception_enter(void) { return 0; }
static inline void exception_exit(enum ctx_state prev_ctx) { }
static inline void context_tracking_task_switch(struct task_struct *prev,
						struct task_struct *next) { }
#endif /* !CONFIG_CONTEXT_TRACKING */

#endif
