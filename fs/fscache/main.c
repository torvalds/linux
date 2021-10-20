// SPDX-License-Identifier: GPL-2.0-or-later
/* General filesystem local caching manager
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/init.h>
#define CREATE_TRACE_POINTS
#include "internal.h"

MODULE_DESCRIPTION("FS Cache Manager");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned fscache_debug;
module_param_named(debug, fscache_debug, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_debug,
		 "FS-Cache debugging mask");

EXPORT_TRACEPOINT_SYMBOL(fscache_access_cache);
EXPORT_TRACEPOINT_SYMBOL(fscache_access_volume);

struct workqueue_struct *fscache_wq;
EXPORT_SYMBOL(fscache_wq);

/*
 * Mixing scores (in bits) for (7,20):
 * Input delta: 1-bit      2-bit
 * 1 round:     330.3     9201.6
 * 2 rounds:   1246.4    25475.4
 * 3 rounds:   1907.1    31295.1
 * 4 rounds:   2042.3    31718.6
 * Perfect:    2048      31744
 *            (32*64)   (32*31/2 * 64)
 */
#define HASH_MIX(x, y, a)	\
	(	x ^= (a),	\
	y ^= x,	x = rol32(x, 7),\
	x += y,	y = rol32(y,20),\
	y *= 9			)

static inline unsigned int fold_hash(unsigned long x, unsigned long y)
{
	/* Use arch-optimized multiply if one exists */
	return __hash_32(y ^ __hash_32(x));
}

/*
 * Generate a hash.  This is derived from full_name_hash(), but we want to be
 * sure it is arch independent and that it doesn't change as bits of the
 * computed hash value might appear on disk.  The caller must guarantee that
 * the source data is a multiple of four bytes in size.
 */
unsigned int fscache_hash(unsigned int salt, const void *data, size_t len)
{
	const __le32 *p = data;
	unsigned int a, x = 0, y = salt, n = len / sizeof(__le32);

	for (; n; n--) {
		a = le32_to_cpu(*p++);
		HASH_MIX(x, y, a);
	}
	return fold_hash(x, y);
}

/*
 * initialise the fs caching module
 */
static int __init fscache_init(void)
{
	int ret = -ENOMEM;

	fscache_wq = alloc_workqueue("fscache", WQ_UNBOUND | WQ_FREEZABLE, 0);
	if (!fscache_wq)
		goto error_wq;

	ret = fscache_proc_init();
	if (ret < 0)
		goto error_proc;

	fscache_cookie_jar = kmem_cache_create("fscache_cookie_jar",
					       sizeof(struct fscache_cookie),
					       0, 0, NULL);
	if (!fscache_cookie_jar) {
		pr_notice("Failed to allocate a cookie jar\n");
		ret = -ENOMEM;
		goto error_cookie_jar;
	}

	pr_notice("Loaded\n");
	return 0;

error_cookie_jar:
	fscache_proc_cleanup();
error_proc:
	destroy_workqueue(fscache_wq);
error_wq:
	return ret;
}

fs_initcall(fscache_init);

/*
 * clean up on module removal
 */
static void __exit fscache_exit(void)
{
	_enter("");

	kmem_cache_destroy(fscache_cookie_jar);
	fscache_proc_cleanup();
	destroy_workqueue(fscache_wq);
	pr_notice("Unloaded\n");
}

module_exit(fscache_exit);
