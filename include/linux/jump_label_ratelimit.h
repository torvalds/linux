/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_JUMP_LABEL_RATELIMIT_H
#define _LINUX_JUMP_LABEL_RATELIMIT_H

#include <linux/jump_label.h>
#include <linux/workqueue.h>

#if defined(CONFIG_JUMP_LABEL)
struct static_key_deferred {
	struct static_key key;
	unsigned long timeout;
	struct delayed_work work;
};

struct static_key_true_deferred {
	struct static_key_true key;
	unsigned long timeout;
	struct delayed_work work;
};

struct static_key_false_deferred {
	struct static_key_false key;
	unsigned long timeout;
	struct delayed_work work;
};

#define static_key_slow_dec_deferred(x)					\
	__static_key_slow_dec_deferred(&(x)->key, &(x)->work, (x)->timeout)
#define static_branch_slow_dec_deferred(x)				\
	__static_key_slow_dec_deferred(&(x)->key.key, &(x)->work, (x)->timeout)

#define static_key_deferred_flush(x)					\
	__static_key_deferred_flush((x), &(x)->work)

extern void
__static_key_slow_dec_deferred(struct static_key *key,
			       struct delayed_work *work,
			       unsigned long timeout);
extern void __static_key_deferred_flush(void *key, struct delayed_work *work);
extern void
jump_label_rate_limit(struct static_key_deferred *key, unsigned long rl);

extern void jump_label_update_timeout(struct work_struct *work);

#define DEFINE_STATIC_KEY_DEFERRED_TRUE(name, rl)			\
	struct static_key_true_deferred name = {			\
		.key =		{ STATIC_KEY_INIT_TRUE },		\
		.timeout =	(rl),					\
		.work =	__DELAYED_WORK_INITIALIZER((name).work,		\
						   jump_label_update_timeout, \
						   0),			\
	}

#define DEFINE_STATIC_KEY_DEFERRED_FALSE(name, rl)			\
	struct static_key_false_deferred name = {			\
		.key =		{ STATIC_KEY_INIT_FALSE },		\
		.timeout =	(rl),					\
		.work =	__DELAYED_WORK_INITIALIZER((name).work,		\
						   jump_label_update_timeout, \
						   0),			\
	}

#define static_branch_deferred_inc(x)	static_branch_inc(&(x)->key)

#else	/* !CONFIG_JUMP_LABEL */
struct static_key_deferred {
	struct static_key  key;
};
struct static_key_true_deferred {
	struct static_key_true key;
};
struct static_key_false_deferred {
	struct static_key_false key;
};
#define DEFINE_STATIC_KEY_DEFERRED_TRUE(name, rl)	\
	struct static_key_true_deferred name = { STATIC_KEY_TRUE_INIT }
#define DEFINE_STATIC_KEY_DEFERRED_FALSE(name, rl)	\
	struct static_key_false_deferred name = { STATIC_KEY_FALSE_INIT }

#define static_branch_slow_dec_deferred(x)	static_branch_dec(&(x)->key)

static inline void static_key_slow_dec_deferred(struct static_key_deferred *key)
{
	STATIC_KEY_CHECK_USE(key);
	static_key_slow_dec(&key->key);
}
static inline void static_key_deferred_flush(void *key)
{
	STATIC_KEY_CHECK_USE(key);
}
static inline void
jump_label_rate_limit(struct static_key_deferred *key,
		unsigned long rl)
{
	STATIC_KEY_CHECK_USE(key);
}
#endif	/* CONFIG_JUMP_LABEL */
#endif	/* _LINUX_JUMP_LABEL_RATELIMIT_H */
