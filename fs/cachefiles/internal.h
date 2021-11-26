/* SPDX-License-Identifier: GPL-2.0-or-later */
/* General netfs cache on cache files internal defs
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) "CacheFiles: " fmt


#include <linux/fscache-cache.h>
#include <linux/cred.h>
#include <linux/security.h>


/*
 * Debug tracing.
 */
extern unsigned cachefiles_debug;
#define CACHEFILES_DEBUG_KENTER	1
#define CACHEFILES_DEBUG_KLEAVE	2
#define CACHEFILES_DEBUG_KDEBUG	4

#define dbgprintk(FMT, ...) \
	printk(KERN_DEBUG "[%-6.6s] "FMT"\n", current->comm, ##__VA_ARGS__)

#define kenter(FMT, ...) dbgprintk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) dbgprintk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) dbgprintk(FMT, ##__VA_ARGS__)


#if defined(__KDEBUG)
#define _enter(FMT, ...) kenter(FMT, ##__VA_ARGS__)
#define _leave(FMT, ...) kleave(FMT, ##__VA_ARGS__)
#define _debug(FMT, ...) kdebug(FMT, ##__VA_ARGS__)

#elif defined(CONFIG_CACHEFILES_DEBUG)
#define _enter(FMT, ...)				\
do {							\
	if (cachefiles_debug & CACHEFILES_DEBUG_KENTER)	\
		kenter(FMT, ##__VA_ARGS__);		\
} while (0)

#define _leave(FMT, ...)				\
do {							\
	if (cachefiles_debug & CACHEFILES_DEBUG_KLEAVE)	\
		kleave(FMT, ##__VA_ARGS__);		\
} while (0)

#define _debug(FMT, ...)				\
do {							\
	if (cachefiles_debug & CACHEFILES_DEBUG_KDEBUG)	\
		kdebug(FMT, ##__VA_ARGS__);		\
} while (0)

#else
#define _enter(FMT, ...) no_printk("==> %s("FMT")", __func__, ##__VA_ARGS__)
#define _leave(FMT, ...) no_printk("<== %s()"FMT"", __func__, ##__VA_ARGS__)
#define _debug(FMT, ...) no_printk(FMT, ##__VA_ARGS__)
#endif

#if 1 /* defined(__KDEBUGALL) */

#define ASSERT(X)							\
do {									\
	if (unlikely(!(X))) {						\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		BUG();							\
	}								\
} while (0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		pr_err("%lx " #OP " %lx is false\n",			\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIF(C, X)							\
do {									\
	if (unlikely((C) && !(X))) {					\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		BUG();							\
	}								\
} while (0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		pr_err("\n");						\
		pr_err("Assertion failed\n");		\
		pr_err("%lx " #OP " %lx is false\n",			\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while (0)

#else

#define ASSERT(X)			do {} while (0)
#define ASSERTCMP(X, OP, Y)		do {} while (0)
#define ASSERTIF(C, X)			do {} while (0)
#define ASSERTIFCMP(C, X, OP, Y)	do {} while (0)

#endif
