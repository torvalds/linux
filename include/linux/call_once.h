#ifndef _LINUX_CALL_ONCE_H
#define _LINUX_CALL_ONCE_H

#include <linux/types.h>
#include <linux/mutex.h>

#define ONCE_NOT_STARTED 0
#define ONCE_RUNNING     1
#define ONCE_COMPLETED   2

struct once {
        atomic_t state;
        struct mutex lock;
};

static inline void __once_init(struct once *once, const char *name,
			       struct lock_class_key *key)
{
        atomic_set(&once->state, ONCE_NOT_STARTED);
        __mutex_init(&once->lock, name, key);
}

#define once_init(once)							\
do {									\
	static struct lock_class_key __key;				\
	__once_init((once), #once, &__key);				\
} while (0)

static inline void call_once(struct once *once, void (*cb)(struct once *))
{
        /* Pairs with atomic_set_release() below.  */
        if (atomic_read_acquire(&once->state) == ONCE_COMPLETED)
                return;

        guard(mutex)(&once->lock);
        WARN_ON(atomic_read(&once->state) == ONCE_RUNNING);
        if (atomic_read(&once->state) != ONCE_NOT_STARTED)
                return;

        atomic_set(&once->state, ONCE_RUNNING);
        cb(once);
        atomic_set_release(&once->state, ONCE_COMPLETED);
}

#endif /* _LINUX_CALL_ONCE_H */
