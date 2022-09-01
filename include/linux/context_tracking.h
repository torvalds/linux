/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H

#include <linux/sched.h>
#include <linux/vtime.h>
#include <linux/context_tracking_state.h>
#include <linux/instrumentation.h>

#include <asm/ptrace.h>


#ifdef CONFIG_CONTEXT_TRACKING_USER
extern void ct_cpu_track_user(int cpu);

/* Called with interrupts disabled.  */
extern void __ct_user_enter(enum ctx_state state);
extern void __ct_user_exit(enum ctx_state state);

extern void ct_user_enter(enum ctx_state state);
extern void ct_user_exit(enum ctx_state state);

extern void user_enter_callable(void);
extern void user_exit_callable(void);

static inline void user_enter(void)
{
	if (context_tracking_enabled())
		ct_user_enter(CONTEXT_USER);

}
static inline void user_exit(void)
{
	if (context_tracking_enabled())
		ct_user_exit(CONTEXT_USER);
}

/* Called with interrupts disabled.  */
static __always_inline void user_enter_irqoff(void)
{
	if (context_tracking_enabled())
		__ct_user_enter(CONTEXT_USER);

}
static __always_inline void user_exit_irqoff(void)
{
	if (context_tracking_enabled())
		__ct_user_exit(CONTEXT_USER);
}

static inline enum ctx_state exception_enter(void)
{
	enum ctx_state prev_ctx;

	if (IS_ENABLED(CONFIG_HAVE_CONTEXT_TRACKING_USER_OFFSTACK) ||
	    !context_tracking_enabled())
		return 0;

	prev_ctx = __ct_state();
	if (prev_ctx != CONTEXT_KERNEL)
		ct_user_exit(prev_ctx);

	return prev_ctx;
}

static inline void exception_exit(enum ctx_state prev_ctx)
{
	if (!IS_ENABLED(CONFIG_HAVE_CONTEXT_TRACKING_USER_OFFSTACK) &&
	    context_tracking_enabled()) {
		if (prev_ctx != CONTEXT_KERNEL)
			ct_user_enter(prev_ctx);
	}
}

static __always_inline bool context_tracking_guest_enter(void)
{
	if (context_tracking_enabled())
		__ct_user_enter(CONTEXT_GUEST);

	return context_tracking_enabled_this_cpu();
}

static __always_inline void context_tracking_guest_exit(void)
{
	if (context_tracking_enabled())
		__ct_user_exit(CONTEXT_GUEST);
}

#define CT_WARN_ON(cond) WARN_ON(context_tracking_enabled() && (cond))

#else
static inline void user_enter(void) { }
static inline void user_exit(void) { }
static inline void user_enter_irqoff(void) { }
static inline void user_exit_irqoff(void) { }
static inline int exception_enter(void) { return 0; }
static inline void exception_exit(enum ctx_state prev_ctx) { }
static inline int ct_state(void) { return -1; }
static __always_inline bool context_tracking_guest_enter(void) { return false; }
static inline void context_tracking_guest_exit(void) { }
#define CT_WARN_ON(cond) do { } while (0)
#endif /* !CONFIG_CONTEXT_TRACKING_USER */

#ifdef CONFIG_CONTEXT_TRACKING_USER_FORCE
extern void context_tracking_init(void);
#else
static inline void context_tracking_init(void) { }
#endif /* CONFIG_CONTEXT_TRACKING_USER_FORCE */

#ifdef CONFIG_CONTEXT_TRACKING_IDLE
extern void ct_idle_enter(void);
extern void ct_idle_exit(void);

/*
 * Is the current CPU in an extended quiescent state?
 *
 * No ordering, as we are sampling CPU-local information.
 */
static __always_inline bool rcu_dynticks_curr_cpu_in_eqs(void)
{
	return !(arch_atomic_read(this_cpu_ptr(&context_tracking.state)) & RCU_DYNTICKS_IDX);
}

/*
 * Increment the current CPU's context_tracking structure's ->state field
 * with ordering.  Return the new value.
 */
static __always_inline unsigned long ct_state_inc(int incby)
{
	return arch_atomic_add_return(incby, this_cpu_ptr(&context_tracking.state));
}

#else
static inline void ct_idle_enter(void) { }
static inline void ct_idle_exit(void) { }
#endif /* !CONFIG_CONTEXT_TRACKING_IDLE */

#endif
