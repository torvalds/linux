// SPDX-License-Identifier: GPL-2.0-or-later
/* General filesystem local caching manager
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#define CREATE_TRACE_POINTS
#include "internal.h"

MODULE_DESCRIPTION("FS Cache Manager");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

unsigned fscache_defer_lookup = 1;
module_param_named(defer_lookup, fscache_defer_lookup, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_defer_lookup,
		 "Defer cookie lookup to background thread");

unsigned fscache_defer_create = 1;
module_param_named(defer_create, fscache_defer_create, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_defer_create,
		 "Defer cookie creation to background thread");

unsigned fscache_debug;
module_param_named(debug, fscache_debug, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(fscache_debug,
		 "FS-Cache debugging mask");

struct kobject *fscache_root;
struct workqueue_struct *fscache_object_wq;
struct workqueue_struct *fscache_op_wq;

DEFINE_PER_CPU(wait_queue_head_t, fscache_object_cong_wait);

/* these values serve as lower bounds, will be adjusted in fscache_init() */
static unsigned fscache_object_max_active = 4;
static unsigned fscache_op_max_active = 2;

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *fscache_sysctl_header;

static int fscache_max_active_sysctl(struct ctl_table *table, int write,
				     void *buffer, size_t *lenp, loff_t *ppos)
{
	struct workqueue_struct **wqp = table->extra1;
	unsigned int *datap = table->data;
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret == 0)
		workqueue_set_max_active(*wqp, *datap);
	return ret;
}

static struct ctl_table fscache_sysctls[] = {
	{
		.procname	= "object_max_active",
		.data		= &fscache_object_max_active,
		.maxlen		= sizeof(unsigned),
		.mode		= 0644,
		.proc_handler	= fscache_max_active_sysctl,
		.extra1		= &fscache_object_wq,
	},
	{
		.procname	= "operation_max_active",
		.data		= &fscache_op_max_active,
		.maxlen		= sizeof(unsigned),
		.mode		= 0644,
		.proc_handler	= fscache_max_active_sysctl,
		.extra1		= &fscache_op_wq,
	},
	{}
};

static struct ctl_table fscache_sysctls_root[] = {
	{
		.procname	= "fscache",
		.mode		= 0555,
		.child		= fscache_sysctls,
	},
	{}
};
#endif

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
 * computed hash value might appear on disk.  The caller also guarantees that
 * the hashed data will be a series of aligned 32-bit words.
 */
unsigned int fscache_hash(unsigned int salt, unsigned int *data, unsigned int n)
{
	unsigned int a, x = 0, y = salt;

	for (; n; n--) {
		a = *data++;
		HASH_MIX(x, y, a);
	}
	return fold_hash(x, y);
}

/*
 * initialise the fs caching module
 */
static int __init fscache_init(void)
{
	unsigned int nr_cpus = num_possible_cpus();
	unsigned int cpu;
	int ret;

	fscache_object_max_active =
		clamp_val(nr_cpus,
			  fscache_object_max_active, WQ_UNBOUND_MAX_ACTIVE);

	ret = -ENOMEM;
	fscache_object_wq = alloc_workqueue("fscache_object", WQ_UNBOUND,
					    fscache_object_max_active);
	if (!fscache_object_wq)
		goto error_object_wq;

	fscache_op_max_active =
		clamp_val(fscache_object_max_active / 2,
			  fscache_op_max_active, WQ_UNBOUND_MAX_ACTIVE);

	ret = -ENOMEM;
	fscache_op_wq = alloc_workqueue("fscache_operation", WQ_UNBOUND,
					fscache_op_max_active);
	if (!fscache_op_wq)
		goto error_op_wq;

	for_each_possible_cpu(cpu)
		init_waitqueue_head(&per_cpu(fscache_object_cong_wait, cpu));

	ret = fscache_proc_init();
	if (ret < 0)
		goto error_proc;

#ifdef CONFIG_SYSCTL
	ret = -ENOMEM;
	fscache_sysctl_header = register_sysctl_table(fscache_sysctls_root);
	if (!fscache_sysctl_header)
		goto error_sysctl;
#endif

	fscache_cookie_jar = kmem_cache_create("fscache_cookie_jar",
					       sizeof(struct fscache_cookie),
					       0, 0, NULL);
	if (!fscache_cookie_jar) {
		pr_notice("Failed to allocate a cookie jar\n");
		ret = -ENOMEM;
		goto error_cookie_jar;
	}

	fscache_root = kobject_create_and_add("fscache", kernel_kobj);
	if (!fscache_root)
		goto error_kobj;

	pr_notice("Loaded\n");
	return 0;

error_kobj:
	kmem_cache_destroy(fscache_cookie_jar);
error_cookie_jar:
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(fscache_sysctl_header);
error_sysctl:
#endif
	fscache_proc_cleanup();
error_proc:
	destroy_workqueue(fscache_op_wq);
error_op_wq:
	destroy_workqueue(fscache_object_wq);
error_object_wq:
	return ret;
}

fs_initcall(fscache_init);

/*
 * clean up on module removal
 */
static void __exit fscache_exit(void)
{
	_enter("");

	kobject_put(fscache_root);
	kmem_cache_destroy(fscache_cookie_jar);
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(fscache_sysctl_header);
#endif
	fscache_proc_cleanup();
	destroy_workqueue(fscache_op_wq);
	destroy_workqueue(fscache_object_wq);
	pr_notice("Unloaded\n");
}

module_exit(fscache_exit);
