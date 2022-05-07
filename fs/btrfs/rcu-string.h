/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Red Hat.  All rights reserved.
 */

#ifndef BTRFS_RCU_STRING_H
#define BTRFS_RCU_STRING_H

struct rcu_string {
	struct rcu_head rcu;
	char str[];
};

static inline struct rcu_string *rcu_string_strdup(const char *src, gfp_t mask)
{
	size_t len = strlen(src) + 1;
	struct rcu_string *ret = kzalloc(sizeof(struct rcu_string) +
					 (len * sizeof(char)), mask);
	if (!ret)
		return ret;
	strncpy(ret->str, src, len);
	return ret;
}

static inline void rcu_string_free(struct rcu_string *str)
{
	if (str)
		kfree_rcu(str, rcu);
}

#define printk_in_rcu(fmt, ...) do {	\
	rcu_read_lock();		\
	printk(fmt, __VA_ARGS__);	\
	rcu_read_unlock();		\
} while (0)

#define printk_ratelimited_in_rcu(fmt, ...) do {	\
	rcu_read_lock();				\
	printk_ratelimited(fmt, __VA_ARGS__);		\
	rcu_read_unlock();				\
} while (0)

#define rcu_str_deref(rcu_str) ({				\
	struct rcu_string *__str = rcu_dereference(rcu_str);	\
	__str->str;						\
})

#endif
