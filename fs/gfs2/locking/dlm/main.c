/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/init.h>

#include "lock_dlm.h"

static int __init init_lock_dlm(void)
{
	int error;

	error = gfs2_register_lockproto(&gdlm_ops);
	if (error) {
		printk(KERN_WARNING "lock_dlm:  can't register protocol: %d\n",
		       error);
		return error;
	}

	error = gdlm_sysfs_init();
	if (error) {
		gfs2_unregister_lockproto(&gdlm_ops);
		return error;
	}

	printk(KERN_INFO
	       "Lock_DLM (built %s %s) installed\n", __DATE__, __TIME__);
	return 0;
}

static void __exit exit_lock_dlm(void)
{
	gdlm_sysfs_exit();
	gfs2_unregister_lockproto(&gdlm_ops);
}

module_init(init_lock_dlm);
module_exit(exit_lock_dlm);

MODULE_DESCRIPTION("GFS DLM Locking Module");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

