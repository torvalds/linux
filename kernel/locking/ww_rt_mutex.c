// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtmutex API
 */
#include <linux/spinlock.h>
#include <linux/export.h>

#define RT_MUTEX_BUILD_MUTEX
#define WW_RT
#include "rtmutex.c"

int ww_mutex_trylock(struct ww_mutex *lock, struct ww_acquire_ctx *ww_ctx)
{
	struct rt_mutex *rtm = &lock->base;

	if (!ww_ctx)
		return rt_mutex_trylock(rtm);

	/*
	 * Reset the wounded flag after a kill. No other process can
	 * race and wound us here, since they can't have a valid owner
	 * pointer if we don't have any locks held.
	 */
	if (ww_ctx->acquired == 0)
		ww_ctx->wounded = 0;

	if (__rt_mutex_trylock(&rtm->rtmutex)) {
		ww_mutex_set_context_fastpath(lock, ww_ctx);
		mutex_acquire_nest(&rtm->dep_map, 0, 1, ww_ctx->dep_map, _RET_IP_);
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(ww_mutex_trylock);

static int __sched
__ww_rt_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ww_ctx,
		   unsigned int state, unsigned long ip)
{
	struct lockdep_map __maybe_unused *nest_lock = NULL;
	struct rt_mutex *rtm = &lock->base;
	int ret;

	might_sleep();

	if (ww_ctx) {
		if (unlikely(ww_ctx == READ_ONCE(lock->ctx)))
			return -EALREADY;

		/*
		 * Reset the wounded flag after a kill. No other process can
		 * race and wound us here, since they can't have a valid owner
		 * pointer if we don't have any locks held.
		 */
		if (ww_ctx->acquired == 0)
			ww_ctx->wounded = 0;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
		nest_lock = &ww_ctx->dep_map;
#endif
	}
	mutex_acquire_nest(&rtm->dep_map, 0, 0, nest_lock, ip);

	if (likely(rt_mutex_cmpxchg_acquire(&rtm->rtmutex, NULL, current))) {
		if (ww_ctx)
			ww_mutex_set_context_fastpath(lock, ww_ctx);
		return 0;
	}

	ret = rt_mutex_slowlock(&rtm->rtmutex, ww_ctx, state);

	if (ret)
		mutex_release(&rtm->dep_map, ip);
	return ret;
}

int __sched
ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	return __ww_rt_mutex_lock(lock, ctx, TASK_UNINTERRUPTIBLE, _RET_IP_);
}
EXPORT_SYMBOL(ww_mutex_lock);

int __sched
ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	return __ww_rt_mutex_lock(lock, ctx, TASK_INTERRUPTIBLE, _RET_IP_);
}
EXPORT_SYMBOL(ww_mutex_lock_interruptible);

void __sched ww_mutex_unlock(struct ww_mutex *lock)
{
	struct rt_mutex *rtm = &lock->base;

	__ww_mutex_unlock(lock);

	mutex_release(&rtm->dep_map, _RET_IP_);
	__rt_mutex_unlock(&rtm->rtmutex);
}
EXPORT_SYMBOL(ww_mutex_unlock);
