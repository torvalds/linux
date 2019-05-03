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

extern void static_key_slow_dec_deferred(struct static_key_deferred *key);
extern void static_key_deferred_flush(struct static_key_deferred *key);
extern void
jump_label_rate_limit(struct static_key_deferred *key, unsigned long rl);

#else	/* !CONFIG_JUMP_LABEL */
struct static_key_deferred {
	struct static_key  key;
};
static inline void static_key_slow_dec_deferred(struct static_key_deferred *key)
{
	STATIC_KEY_CHECK_USE(key);
	static_key_slow_dec(&key->key);
}
static inline void static_key_deferred_flush(struct static_key_deferred *key)
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
