#ifndef _LINUX_CONTEXT_TRACKING_STATE_H
#define _LINUX_CONTEXT_TRACKING_STATE_H

#include <linux/percpu.h>
#include <linux/static_key.h>

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

#ifdef CONFIG_CONTEXT_TRACKING
extern struct static_key context_tracking_enabled;
DECLARE_PER_CPU(struct context_tracking, context_tracking);

static inline bool context_tracking_is_enabled(void)
{
	return static_key_false(&context_tracking_enabled);
}

static inline bool context_tracking_cpu_is_enabled(void)
{
	return __this_cpu_read(context_tracking.active);
}

static inline bool context_tracking_in_user(void)
{
	return __this_cpu_read(context_tracking.state) == IN_USER;
}
#else
static inline bool context_tracking_in_user(void) { return false; }
static inline bool context_tracking_active(void) { return false; }
#endif /* CONFIG_CONTEXT_TRACKING */

#endif
