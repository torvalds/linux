/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/init.h>

#include "lock_dlm.h"

extern int gdlm_drop_count;
extern int gdlm_drop_period;

extern struct lm_lockops gdlm_ops;

int __init init_lock_dlm(void)
{
	int error;

	error = lm_register_proto(&gdlm_ops);
	if (error) {
		printk("lock_dlm:  can't register protocol: %d\n", error);
		return error;
	}

	error = gdlm_sysfs_init();
	if (error) {
		lm_unregister_proto(&gdlm_ops);
		return error;
	}

	gdlm_drop_count = GDLM_DROP_COUNT;
	gdlm_drop_period = GDLM_DROP_PERIOD;

	printk("Lock_DLM (built %s %s) installed\n", __DATE__, __TIME__);
	return 0;
}

void __exit exit_lock_dlm(void)
{
	lm_unregister_proto(&gdlm_ops);
	gdlm_sysfs_exit();
}

module_init(init_lock_dlm);
module_exit(exit_lock_dlm);

MODULE_DESCRIPTION("GFS DLM Locking Module");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

