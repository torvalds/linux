/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal definitions for FS-Cache
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "FS-Cache: " fmt

#include <linux/slab.h>
#include <linux/fscache-cache.h>
#include <trace/events/fscache.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

/*
 * cache.c
 */
#ifdef CONFIG_PROC_FS
extern const struct seq_operations fscache_caches_seq_ops;
#endif
bool fscache_begin_cache_access(struct fscache_cache *cache, enum fscache_access_trace why);
void fscache_end_cache_access(struct fscache_cache *cache, enum fscache_access_trace why);
struct fscache_cache *fscache_lookup_cache(const char *name, bool is_cache);
void fscache_put_cache(struct fscache_cache *cache, enum fscache_cache_trace where);

static inline enum fscache_cache_state fscache_cache_state(const struct fscache_cache *cache)
{
	return smp_load_acquire(&cache->state);
}

static inline bool fscache_cache_is_live(const struct fscache_cache *cache)
{
	return fscache_cache_state(cache) == FSCACHE_CACHE_IS_ACTIVE;
}

static inline void fscache_set_cache_state(struct fscache_cache *cache,
					   enum fscache_cache_state new_state)
{
	smp_store_release(&cache->state, new_state);

}

static inline bool fscache_set_cache_state_maybe(struct fscache_cache *cache,
						 enum fscache_cache_state old_state,
						 enum fscache_cache_state new_state)
{
	return try_cmpxchg_release(&cache->state, &old_state, new_state);
}

/*
 * cookie.c
 */
extern struct kmem_cache *fscache_cookie_jar;
extern const struct seq_operations fscache_cookies_seq_ops;

extern void fscache_print_cookie(struct fscache_cookie *cookie, char prefix);
static inline void fscache_see_cookie(struct fscache_cookie *cookie,
				      enum fscache_cookie_trace where)
{
	trace_fscache_cookie(cookie->debug_id, refcount_read(&cookie->ref),
			     where);
}

/*
 * main.c
 */
extern unsigned fscache_debug;

extern unsigned int fscache_hash(unsigned int salt, const void *data, size_t len);

/*
 * proc.c
 */
#ifdef CONFIG_PROC_FS
extern int __init fscache_proc_init(void);
extern void fscache_proc_cleanup(void);
#else
#define fscache_proc_init()	(0)
#define fscache_proc_cleanup()	do {} while (0)
#endif

/*
 * stats.c
 */
#ifdef CONFIG_FSCACHE_STATS
extern atomic_t fscache_n_volumes;
extern atomic_t fscache_n_volumes_collision;
extern atomic_t fscache_n_volumes_nomem;
extern atomic_t fscache_n_cookies;

extern atomic_t fscache_n_acquires;
extern atomic_t fscache_n_acquires_ok;
extern atomic_t fscache_n_acquires_oom;

extern atomic_t fscache_n_relinquishes;
extern atomic_t fscache_n_relinquishes_retire;
extern atomic_t fscache_n_relinquishes_dropped;

static inline void fscache_stat(atomic_t *stat)
{
	atomic_inc(stat);
}

static inline void fscache_stat_d(atomic_t *stat)
{
	atomic_dec(stat);
}

#define __fscache_stat(stat) (stat)

int fscache_stats_show(struct seq_file *m, void *v);
#else

#define __fscache_stat(stat) (NULL)
#define fscache_stat(stat) do {} while (0)
#define fscache_stat_d(stat) do {} while (0)
#endif

/*
 * volume.c
 */
extern const struct seq_operations fscache_volumes_seq_ops;

struct fscache_volume *fscache_get_volume(struct fscache_volume *volume,
					  enum fscache_volume_trace where);
void fscache_put_volume(struct fscache_volume *volume,
			enum fscache_volume_trace where);
void fscache_create_volume(struct fscache_volume *volume, bool wait);


/*****************************************************************************/
/*
 * debug tracing
 */
#define dbgprintk(FMT, ...) \
	printk("[%-6.6s] "FMT"\n", current->comm, ##__VA_ARGS__)

#define kenter(FMT, ...) dbgprintk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) dbgprintk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) dbgprintk(FMT, ##__VA_ARGS__)

#define kjournal(FMT, ...) no_printk(FMT, ##__VA_ARGS__)

#ifdef __KDEBUG
#define _enter(FMT, ...) kenter(FMT, ##__VA_ARGS__)
#define _leave(FMT, ...) kleave(FMT, ##__VA_ARGS__)
#define _debug(FMT, ...) kdebug(FMT, ##__VA_ARGS__)

#elif defined(CONFIG_FSCACHE_DEBUG)
#define _enter(FMT, ...)			\
do {						\
	if (__do_kdebug(ENTER))			\
		kenter(FMT, ##__VA_ARGS__);	\
} while (0)

#define _leave(FMT, ...)			\
do {						\
	if (__do_kdebug(LEAVE))			\
		kleave(FMT, ##__VA_ARGS__);	\
} while (0)

#define _debug(FMT, ...)			\
do {						\
	if (__do_kdebug(DEBUG))			\
		kdebug(FMT, ##__VA_ARGS__);	\
} while (0)

#else
#define _enter(FMT, ...) no_printk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define _leave(FMT, ...) no_printk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define _debug(FMT, ...) no_printk(FMT, ##__VA_ARGS__)
#endif

/*
 * determine whether a particular optional debugging point should be logged
 * - we need to go through three steps to persuade cpp to correctly join the
 *   shorthand in FSCACHE_DEBUG_LEVEL with its prefix
 */
#define ____do_kdebug(LEVEL, POINT) \
	unlikely((fscache_debug & \
		  (FSCACHE_POINT_##POINT << (FSCACHE_DEBUG_ ## LEVEL * 3))))
#define ___do_kdebug(LEVEL, POINT) \
	____do_kdebug(LEVEL, POINT)
#define __do_kdebug(POINT) \
	___do_kdebug(FSCACHE_DEBUG_LEVEL, POINT)

#define FSCACHE_DEBUG_CACHE	0
#define FSCACHE_DEBUG_COOKIE	1
#define FSCACHE_DEBUG_OBJECT	2
#define FSCACHE_DEBUG_OPERATION	3

#define FSCACHE_POINT_ENTER	1
#define FSCACHE_POINT_LEAVE	2
#define FSCACHE_POINT_DEBUG	4

#ifndef FSCACHE_DEBUG_LEVEL
#define FSCACHE_DEBUG_LEVEL CACHE
#endif

/*
 * assertions
 */
#if 1 /* defined(__KDEBUGALL) */

#define ASSERT(X)							\
do {									\
	if (unlikely(!(X))) {						\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		BUG();							\
	}								\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		pr_err("%lx " #OP " %lx is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)							\
do {									\
	if (unlikely((C) && !(X))) {					\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		BUG();							\
	}								\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		pr_err("\n");					\
		pr_err("Assertion failed\n");	\
		pr_err("%lx " #OP " %lx is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#else

#define ASSERT(X)			do {} while (0)
#define ASSERTCMP(X, OP, Y)		do {} while (0)
#define ASSERTIF(C, X)			do {} while (0)
#define ASSERTIFCMP(C, X, OP, Y)	do {} while (0)

#endif /* assert or not */
