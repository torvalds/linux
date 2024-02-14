/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SEQMUTEX_H
#define _BCACHEFS_SEQMUTEX_H

#include <linux/mutex.h>

struct seqmutex {
	struct mutex	lock;
	u32		seq;
};

#define seqmutex_init(_lock)	mutex_init(&(_lock)->lock)

static inline bool seqmutex_trylock(struct seqmutex *lock)
{
	return mutex_trylock(&lock->lock);
}

static inline void seqmutex_lock(struct seqmutex *lock)
{
	mutex_lock(&lock->lock);
}

static inline void seqmutex_unlock(struct seqmutex *lock)
{
	lock->seq++;
	mutex_unlock(&lock->lock);
}

static inline u32 seqmutex_seq(struct seqmutex *lock)
{
	return lock->seq;
}

static inline bool seqmutex_relock(struct seqmutex *lock, u32 seq)
{
	if (lock->seq != seq || !mutex_trylock(&lock->lock))
		return false;

	if (lock->seq != seq) {
		mutex_unlock(&lock->lock);
		return false;
	}

	return true;
}

#endif /* _BCACHEFS_SEQMUTEX_H */
