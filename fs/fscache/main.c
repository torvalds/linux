/* General filesystem local caching manager
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
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
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos)
{
	struct workqueue_struct **wqp = table->extra1;
	unsigned int *datap = table->data;
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret == 0)
		workqueue_set_max_active(*wqp, *datap);
	return ret;
}

ctl_table fscache_sysctls[] = {
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

ctl_table fscache_sysctls_root[] = {
	{
		.procname	= "fscache",
		.mode		= 0555,
		.child		= fscache_sysctls,
	},
	{}
};
#endif

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
					       0,
					       0,
					       fscache_cookie_init_once);
	if (!fscache_cookie_jar) {
		printk(KERN_NOTICE
		       "FS-Cache: Failed to allocate a cookie jar\n");
		ret = -ENOMEM;
		goto error_cookie_jar;
	}

	fscache_root = kobject_create_and_add("fscache", kernel_kobj);
	if (!fscache_root)
		goto error_kobj;

	printk(KERN_NOTICE "FS-Cache: Loaded\n");
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
	printk(KERN_NOTICE "FS-Cache: Unloaded\n");
}

module_exit(fscache_exit);

/*
 * wait_on_bit() sleep function for uninterruptible waiting
 */
int fscache_wait_bit(void *flags)
{
	schedule();
	return 0;
}
EXPORT_SYMBOL(fscache_wait_bit);

/*
 * wait_on_bit() sleep function for interruptible waiting
 */
int fscache_wait_bit_interruptible(void *flags)
{
	schedule();
	return signal_pending(current);
}
EXPORT_SYMBOL(fscache_wait_bit_interruptible);
