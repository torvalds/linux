/*
 * Copyright 2013-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_ELIDE_H
#define CK_ELIDE_H

/*
 * As RTM is currently only supported on TSO x86 architectures,
 * fences have been omitted. They will be necessary for other
 * non-TSO architectures with TM support.
 */

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_string.h>

/*
 * skip_-prefixed counters represent the number of consecutive
 * elisions to forfeit. retry_-prefixed counters represent the
 * number of elision retries to attempt before forfeit.
 *
 *     _busy: Lock was busy
 *    _other: Unknown explicit abort
 * _conflict: Data conflict in elision section
 */
struct ck_elide_config {
	unsigned short skip_busy;
	short retry_busy;
	unsigned short skip_other;
	short retry_other;
	unsigned short skip_conflict;
	short retry_conflict;
};

#define CK_ELIDE_CONFIG_DEFAULT_INITIALIZER {	\
	.skip_busy = 5,				\
	.retry_busy = 256,			\
	.skip_other = 3,			\
	.retry_other = 3,			\
	.skip_conflict = 2,			\
	.retry_conflict = 5			\
}

struct ck_elide_stat {
	unsigned int n_fallback;
	unsigned int n_elide;
	unsigned short skip;
};
typedef struct ck_elide_stat ck_elide_stat_t;

#define CK_ELIDE_STAT_INITIALIZER { 0, 0, 0 }

CK_CC_INLINE static void
ck_elide_stat_init(ck_elide_stat_t *st)
{

	memset(st, 0, sizeof(*st));
	return;
}

#ifdef CK_F_PR_RTM
enum _ck_elide_hint {
	CK_ELIDE_HINT_RETRY = 0,
	CK_ELIDE_HINT_SPIN,
	CK_ELIDE_HINT_STOP
};

#define CK_ELIDE_LOCK_BUSY 0xFF

static enum _ck_elide_hint
_ck_elide_fallback(int *retry,
    struct ck_elide_stat *st,
    struct ck_elide_config *c,
    unsigned int status)
{

	st->n_fallback++;
	if (*retry > 0)
		return CK_ELIDE_HINT_RETRY;

	if (st->skip != 0)
		return CK_ELIDE_HINT_STOP;

	if (status & CK_PR_RTM_EXPLICIT) {
		if (CK_PR_RTM_CODE(status) == CK_ELIDE_LOCK_BUSY) {
			st->skip = c->skip_busy;
			*retry = c->retry_busy;
			return CK_ELIDE_HINT_SPIN;
		}

		st->skip = c->skip_other;
		return CK_ELIDE_HINT_STOP;
	}

	if ((status & CK_PR_RTM_RETRY) &&
	    (status & CK_PR_RTM_CONFLICT)) {
		st->skip = c->skip_conflict;
		*retry = c->retry_conflict;
		return CK_ELIDE_HINT_RETRY;
	}

	/*
	 * Capacity, debug and nesting abortions are likely to be
	 * invariant conditions for the acquisition, execute regular
	 * path instead. If retry bit is not set, then take the hint.
	 */
	st->skip = USHRT_MAX;
	return CK_ELIDE_HINT_STOP;
}

/*
 * Defines an elision implementation according to the following variables:
 *     N - Namespace of elision implementation.
 *     T - Typename of mutex.
 *   L_P - Lock predicate, returns false if resource is available.
 *     L - Function to call if resource is unavailable of transaction aborts.
 *   U_P - Unlock predicate, returns false if elision failed.
 *     U - Function to call if transaction failed.
 */
#define CK_ELIDE_PROTOTYPE(N, T, L_P, L, U_P, U)					\
	CK_CC_INLINE static void							\
	ck_elide_##N##_lock_adaptive(T *lock,						\
	    struct ck_elide_stat *st,							\
	    struct ck_elide_config *c)							\
	{										\
		enum _ck_elide_hint hint;						\
		int retry;								\
											\
		if (CK_CC_UNLIKELY(st->skip != 0)) {					\
			st->skip--;							\
			goto acquire;							\
		}									\
											\
		retry = c->retry_conflict;						\
		do {									\
			unsigned int status = ck_pr_rtm_begin();			\
			if (status == CK_PR_RTM_STARTED) {				\
				if (L_P(lock) == true)					\
					ck_pr_rtm_abort(CK_ELIDE_LOCK_BUSY);		\
											\
				return;							\
			}								\
											\
			hint = _ck_elide_fallback(&retry, st, c, status);		\
			if (hint == CK_ELIDE_HINT_RETRY)				\
				continue;						\
											\
			if (hint == CK_ELIDE_HINT_SPIN) {				\
				while (--retry != 0) {					\
					if (L_P(lock) == false)				\
						break;					\
											\
					ck_pr_stall();					\
				}							\
											\
				continue;						\
			}								\
											\
			if (hint == CK_ELIDE_HINT_STOP)					\
				break;							\
		} while (CK_CC_LIKELY(--retry > 0));					\
											\
	acquire:									\
		L(lock);								\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_elide_##N##_unlock_adaptive(struct ck_elide_stat *st, T *lock)		\
	{										\
											\
		if (U_P(lock) == false) {						\
			ck_pr_rtm_end();						\
			st->skip = 0;							\
			st->n_elide++;							\
		} else {								\
			U(lock);							\
		}									\
											\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_elide_##N##_lock(T *lock)							\
	{										\
											\
		if (ck_pr_rtm_begin() != CK_PR_RTM_STARTED) {				\
			L(lock);							\
			return;								\
		}									\
											\
		if (L_P(lock) == true)							\
			ck_pr_rtm_abort(CK_ELIDE_LOCK_BUSY);				\
											\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_elide_##N##_unlock(T *lock)							\
	{										\
											\
		if (U_P(lock) == false) {						\
			ck_pr_rtm_end();						\
		} else {								\
			U(lock);							\
		}									\
											\
		return;									\
	}

#define CK_ELIDE_TRYLOCK_PROTOTYPE(N, T, TL_P, TL)			\
	CK_CC_INLINE static bool					\
	ck_elide_##N##_trylock(T *lock)					\
	{								\
									\
		if (ck_pr_rtm_begin() != CK_PR_RTM_STARTED)		\
			return false;					\
									\
		if (TL_P(lock) == true)					\
			ck_pr_rtm_abort(CK_ELIDE_LOCK_BUSY);		\
									\
		return true;						\
	}
#else
/*
 * If RTM is not enabled on the target platform (CK_F_PR_RTM) then these
 * elision wrappers directly calls into the user-specified lock operations.
 * Unfortunately, the storage cost of both ck_elide_config and ck_elide_stat
 * are paid (typically a storage cost that is a function of lock objects and
 * thread count).
 */
#define CK_ELIDE_PROTOTYPE(N, T, L_P, L, U_P, U)			\
	CK_CC_INLINE static void					\
	ck_elide_##N##_lock_adaptive(T *lock,				\
	    struct ck_elide_stat *st,					\
	    struct ck_elide_config *c)					\
	{								\
									\
		(void)st;						\
		(void)c;						\
		L(lock);						\
		return;							\
	}								\
	CK_CC_INLINE static void					\
	ck_elide_##N##_unlock_adaptive(struct ck_elide_stat *st,	\
	    T *lock)							\
	{								\
									\
		(void)st;						\
		U(lock);						\
		return;							\
	}								\
	CK_CC_INLINE static void					\
	ck_elide_##N##_lock(T *lock)					\
	{								\
									\
		L(lock);						\
		return;							\
	}								\
	CK_CC_INLINE static void					\
	ck_elide_##N##_unlock(T *lock)					\
	{								\
									\
		U(lock);						\
		return;							\
	}

#define CK_ELIDE_TRYLOCK_PROTOTYPE(N, T, TL_P, TL)			\
	CK_CC_INLINE static bool					\
	ck_elide_##N##_trylock(T *lock)					\
	{								\
									\
		return TL_P(lock);					\
	}
#endif /* !CK_F_PR_RTM */

/*
 * Best-effort elision lock operations. First argument is name (N)
 * associated with implementation and the second is a pointer to
 * the type specified above (T).
 *
 * Unlike the adaptive variant, this interface does not have any retry
 * semantics. In environments where jitter is low, this may yield a tighter
 * fast path.
 */
#define CK_ELIDE_LOCK(NAME, LOCK)	ck_elide_##NAME##_lock(LOCK)
#define CK_ELIDE_UNLOCK(NAME, LOCK)	ck_elide_##NAME##_unlock(LOCK)
#define CK_ELIDE_TRYLOCK(NAME, LOCK)	ck_elide_##NAME##_trylock(LOCK)

/*
 * Adaptive elision lock operations. In addition to name and pointer
 * to the lock, you must pass in a pointer to an initialized
 * ck_elide_config structure along with a per-thread stat structure.
 */
#define CK_ELIDE_LOCK_ADAPTIVE(NAME, STAT, CONFIG, LOCK) \
	ck_elide_##NAME##_lock_adaptive(LOCK, STAT, CONFIG)

#define CK_ELIDE_UNLOCK_ADAPTIVE(NAME, STAT, LOCK) \
	ck_elide_##NAME##_unlock_adaptive(STAT, LOCK)

#endif /* CK_ELIDE_H */
