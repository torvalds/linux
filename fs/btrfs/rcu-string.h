/*
 * Copyright (C) 2012 Red Hat.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

struct rcu_string {
	struct rcu_head rcu;
	char str[0];
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
