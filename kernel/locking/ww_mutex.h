/* SPDX-License-Identifier: GPL-2.0-only */

#define MUTEX		mutex
#define MUTEX_WAITER	mutex_waiter

static inline struct mutex_waiter *
__ww_waiter_first(struct mutex *lock)
{
	struct mutex_waiter *w;

	w = list_first_entry(&lock->wait_list, struct mutex_waiter, list);
	if (list_entry_is_head(w, &lock->wait_list, list))
		return NULL;

	return w;
}

static inline struct mutex_waiter *
__ww_waiter_next(struct mutex *lock, struct mutex_waiter *w)
{
	w = list_next_entry(w, list);
	if (list_entry_is_head(w, &lock->wait_list, list))
		return NULL;

	return w;
}

static inline struct mutex_waiter *
__ww_waiter_prev(struct mutex *lock, struct mutex_waiter *w)
{
	w = list_prev_entry(w, list);
	if (list_entry_is_head(w, &lock->wait_list, list))
		return NULL;

	return w;
}

static inline struct mutex_waiter *
__ww_waiter_last(struct mutex *lock)
{
	struct mutex_waiter *w;

	w = list_last_entry(&lock->wait_list, struct mutex_waiter, list);
	if (list_entry_is_head(w, &lock->wait_list, list))
		return NULL;

	return w;
}

static inline void
__ww_waiter_add(struct mutex *lock, struct mutex_waiter *waiter, struct mutex_waiter *pos)
{
	struct list_head *p = &lock->wait_list;
	if (pos)
		p = &pos->list;
	__mutex_add_waiter(lock, waiter, p);
}

static inline struct task_struct *
__ww_mutex_owner(struct mutex *lock)
{
	return __mutex_owner(lock);
}

static inline bool
__ww_mutex_has_waiters(struct mutex *lock)
{
	return atomic_long_read(&lock->owner) & MUTEX_FLAG_WAITERS;
}

/*
 * Wait-Die:
 *   The newer transactions are killed when:
 *     It (the new transaction) makes a request for a lock being held
 *     by an older transaction.
 *
 * Wound-Wait:
 *   The newer transactions are wounded when:
 *     An older transaction makes a request for a lock being held by
 *     the newer transaction.
 */

/*
 * Associate the ww_mutex @ww with the context @ww_ctx under which we acquired
 * it.
 */
static __always_inline void
ww_mutex_lock_acquired(struct ww_mutex *ww, struct ww_acquire_ctx *ww_ctx)
{
#ifdef CONFIG_DEBUG_MUTEXES
	/*
	 * If this WARN_ON triggers, you used ww_mutex_lock to acquire,
	 * but released with a normal mutex_unlock in this call.
	 *
	 * This should never happen, always use ww_mutex_unlock.
	 */
	DEBUG_LOCKS_WARN_ON(ww->ctx);

	/*
	 * Not quite done after calling ww_acquire_done() ?
	 */
	DEBUG_LOCKS_WARN_ON(ww_ctx->done_acquire);

	if (ww_ctx->contending_lock) {
		/*
		 * After -EDEADLK you tried to
		 * acquire a different ww_mutex? Bad!
		 */
		DEBUG_LOCKS_WARN_ON(ww_ctx->contending_lock != ww);

		/*
		 * You called ww_mutex_lock after receiving -EDEADLK,
		 * but 'forgot' to unlock everything else first?
		 */
		DEBUG_LOCKS_WARN_ON(ww_ctx->acquired > 0);
		ww_ctx->contending_lock = NULL;
	}

	/*
	 * Naughty, using a different class will lead to undefined behavior!
	 */
	DEBUG_LOCKS_WARN_ON(ww_ctx->ww_class != ww->ww_class);
#endif
	ww_ctx->acquired++;
	ww->ctx = ww_ctx;
}

/*
 * Determine if context @a is 'after' context @b. IOW, @a is a younger
 * transaction than @b and depending on algorithm either needs to wait for
 * @b or die.
 */
static inline bool
__ww_ctx_stamp_after(struct ww_acquire_ctx *a, struct ww_acquire_ctx *b)
{

	return (signed long)(a->stamp - b->stamp) > 0;
}

/*
 * Wait-Die; wake a younger waiter context (when locks held) such that it can
 * die.
 *
 * Among waiters with context, only the first one can have other locks acquired
 * already (ctx->acquired > 0), because __ww_mutex_add_waiter() and
 * __ww_mutex_check_kill() wake any but the earliest context.
 */
static bool
__ww_mutex_die(struct MUTEX *lock, struct MUTEX_WAITER *waiter,
	       struct ww_acquire_ctx *ww_ctx)
{
	if (!ww_ctx->is_wait_die)
		return false;

	if (waiter->ww_ctx->acquired > 0 &&
			__ww_ctx_stamp_after(waiter->ww_ctx, ww_ctx)) {
		debug_mutex_wake_waiter(lock, waiter);
		wake_up_process(waiter->task);
	}

	return true;
}

/*
 * Wound-Wait; wound a younger @hold_ctx if it holds the lock.
 *
 * Wound the lock holder if there are waiters with older transactions than
 * the lock holders. Even if multiple waiters may wound the lock holder,
 * it's sufficient that only one does.
 */
static bool __ww_mutex_wound(struct MUTEX *lock,
			     struct ww_acquire_ctx *ww_ctx,
			     struct ww_acquire_ctx *hold_ctx)
{
	struct task_struct *owner = __ww_mutex_owner(lock);

	lockdep_assert_held(&lock->wait_lock);

	/*
	 * Possible through __ww_mutex_add_waiter() when we race with
	 * ww_mutex_set_context_fastpath(). In that case we'll get here again
	 * through __ww_mutex_check_waiters().
	 */
	if (!hold_ctx)
		return false;

	/*
	 * Can have !owner because of __mutex_unlock_slowpath(), but if owner,
	 * it cannot go away because we'll have FLAG_WAITERS set and hold
	 * wait_lock.
	 */
	if (!owner)
		return false;

	if (ww_ctx->acquired > 0 && __ww_ctx_stamp_after(hold_ctx, ww_ctx)) {
		hold_ctx->wounded = 1;

		/*
		 * wake_up_process() paired with set_current_state()
		 * inserts sufficient barriers to make sure @owner either sees
		 * it's wounded in __ww_mutex_check_kill() or has a
		 * wakeup pending to re-read the wounded state.
		 */
		if (owner != current)
			wake_up_process(owner);

		return true;
	}

	return false;
}

/*
 * We just acquired @lock under @ww_ctx, if there are later contexts waiting
 * behind us on the wait-list, check if they need to die, or wound us.
 *
 * See __ww_mutex_add_waiter() for the list-order construction; basically the
 * list is ordered by stamp, smallest (oldest) first.
 *
 * This relies on never mixing wait-die/wound-wait on the same wait-list;
 * which is currently ensured by that being a ww_class property.
 *
 * The current task must not be on the wait list.
 */
static void
__ww_mutex_check_waiters(struct MUTEX *lock, struct ww_acquire_ctx *ww_ctx)
{
	struct MUTEX_WAITER *cur;

	lockdep_assert_held(&lock->wait_lock);

	for (cur = __ww_waiter_first(lock); cur;
	     cur = __ww_waiter_next(lock, cur)) {

		if (!cur->ww_ctx)
			continue;

		if (__ww_mutex_die(lock, cur, ww_ctx) ||
		    __ww_mutex_wound(lock, cur->ww_ctx, ww_ctx))
			break;
	}
}

/*
 * After acquiring lock with fastpath, where we do not hold wait_lock, set ctx
 * and wake up any waiters so they can recheck.
 */
static __always_inline void
ww_mutex_set_context_fastpath(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	ww_mutex_lock_acquired(lock, ctx);

	/*
	 * The lock->ctx update should be visible on all cores before
	 * the WAITERS check is done, otherwise contended waiters might be
	 * missed. The contended waiters will either see ww_ctx == NULL
	 * and keep spinning, or it will acquire wait_lock, add itself
	 * to waiter list and sleep.
	 */
	smp_mb(); /* See comments above and below. */

	/*
	 * [W] ww->ctx = ctx	    [W] MUTEX_FLAG_WAITERS
	 *     MB		        MB
	 * [R] MUTEX_FLAG_WAITERS   [R] ww->ctx
	 *
	 * The memory barrier above pairs with the memory barrier in
	 * __ww_mutex_add_waiter() and makes sure we either observe ww->ctx
	 * and/or !empty list.
	 */
	if (likely(!__ww_mutex_has_waiters(&lock->base)))
		return;

	/*
	 * Uh oh, we raced in fastpath, check if any of the waiters need to
	 * die or wound us.
	 */
	raw_spin_lock(&lock->base.wait_lock);
	__ww_mutex_check_waiters(&lock->base, ctx);
	raw_spin_unlock(&lock->base.wait_lock);
}

static __always_inline int
__ww_mutex_kill(struct MUTEX *lock, struct ww_acquire_ctx *ww_ctx)
{
	if (ww_ctx->acquired > 0) {
#ifdef CONFIG_DEBUG_MUTEXES
		struct ww_mutex *ww;

		ww = container_of(lock, struct ww_mutex, base);
		DEBUG_LOCKS_WARN_ON(ww_ctx->contending_lock);
		ww_ctx->contending_lock = ww;
#endif
		return -EDEADLK;
	}

	return 0;
}

/*
 * Check the wound condition for the current lock acquire.
 *
 * Wound-Wait: If we're wounded, kill ourself.
 *
 * Wait-Die: If we're trying to acquire a lock already held by an older
 *           context, kill ourselves.
 *
 * Since __ww_mutex_add_waiter() orders the wait-list on stamp, we only have to
 * look at waiters before us in the wait-list.
 */
static inline int
__ww_mutex_check_kill(struct MUTEX *lock, struct MUTEX_WAITER *waiter,
		      struct ww_acquire_ctx *ctx)
{
	struct ww_mutex *ww = container_of(lock, struct ww_mutex, base);
	struct ww_acquire_ctx *hold_ctx = READ_ONCE(ww->ctx);
	struct MUTEX_WAITER *cur;

	if (ctx->acquired == 0)
		return 0;

	if (!ctx->is_wait_die) {
		if (ctx->wounded)
			return __ww_mutex_kill(lock, ctx);

		return 0;
	}

	if (hold_ctx && __ww_ctx_stamp_after(ctx, hold_ctx))
		return __ww_mutex_kill(lock, ctx);

	/*
	 * If there is a waiter in front of us that has a context, then its
	 * stamp is earlier than ours and we must kill ourself.
	 */
	for (cur = __ww_waiter_prev(lock, waiter); cur;
	     cur = __ww_waiter_prev(lock, cur)) {

		if (!cur->ww_ctx)
			continue;

		return __ww_mutex_kill(lock, ctx);
	}

	return 0;
}

/*
 * Add @waiter to the wait-list, keep the wait-list ordered by stamp, smallest
 * first. Such that older contexts are preferred to acquire the lock over
 * younger contexts.
 *
 * Waiters without context are interspersed in FIFO order.
 *
 * Furthermore, for Wait-Die kill ourself immediately when possible (there are
 * older contexts already waiting) to avoid unnecessary waiting and for
 * Wound-Wait ensure we wound the owning context when it is younger.
 */
static inline int
__ww_mutex_add_waiter(struct MUTEX_WAITER *waiter,
		      struct MUTEX *lock,
		      struct ww_acquire_ctx *ww_ctx)
{
	struct MUTEX_WAITER *cur, *pos = NULL;
	bool is_wait_die;

	if (!ww_ctx) {
		__ww_waiter_add(lock, waiter, NULL);
		return 0;
	}

	is_wait_die = ww_ctx->is_wait_die;

	/*
	 * Add the waiter before the first waiter with a higher stamp.
	 * Waiters without a context are skipped to avoid starving
	 * them. Wait-Die waiters may die here. Wound-Wait waiters
	 * never die here, but they are sorted in stamp order and
	 * may wound the lock holder.
	 */
	for (cur = __ww_waiter_last(lock); cur;
	     cur = __ww_waiter_prev(lock, cur)) {

		if (!cur->ww_ctx)
			continue;

		if (__ww_ctx_stamp_after(ww_ctx, cur->ww_ctx)) {
			/*
			 * Wait-Die: if we find an older context waiting, there
			 * is no point in queueing behind it, as we'd have to
			 * die the moment it would acquire the lock.
			 */
			if (is_wait_die) {
				int ret = __ww_mutex_kill(lock, ww_ctx);

				if (ret)
					return ret;
			}

			break;
		}

		pos = cur;

		/* Wait-Die: ensure younger waiters die. */
		__ww_mutex_die(lock, cur, ww_ctx);
	}

	__ww_waiter_add(lock, waiter, pos);

	/*
	 * Wound-Wait: if we're blocking on a mutex owned by a younger context,
	 * wound that such that we might proceed.
	 */
	if (!is_wait_die) {
		struct ww_mutex *ww = container_of(lock, struct ww_mutex, base);

		/*
		 * See ww_mutex_set_context_fastpath(). Orders setting
		 * MUTEX_FLAG_WAITERS vs the ww->ctx load,
		 * such that either we or the fastpath will wound @ww->ctx.
		 */
		smp_mb();
		__ww_mutex_wound(lock, ww_ctx, ww->ctx);
	}

	return 0;
}

static inline void __ww_mutex_unlock(struct ww_mutex *lock)
{
	if (lock->ctx) {
#ifdef CONFIG_DEBUG_MUTEXES
		DEBUG_LOCKS_WARN_ON(!lock->ctx->acquired);
#endif
		if (lock->ctx->acquired > 0)
			lock->ctx->acquired--;
		lock->ctx = NULL;
	}
}
