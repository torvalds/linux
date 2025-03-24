// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include <linux/module.h>

#include "dlm_internal.h"
#include "lockspace.h"
#include "lock.h"
#include "user.h"
#include "memory.h"
#include "config.h"
#include "midcomms.h"

#define CREATE_TRACE_POINTS
#include <trace/events/dlm.h>

struct workqueue_struct *dlm_wq;

static int __init init_dlm(void)
{
	int error;

	error = dlm_memory_init();
	if (error)
		goto out;

	dlm_midcomms_init();

	error = dlm_lockspace_init();
	if (error)
		goto out_mem;

	error = dlm_config_init();
	if (error)
		goto out_lockspace;

	dlm_register_debugfs();

	error = dlm_user_init();
	if (error)
		goto out_debug;

	error = dlm_plock_init();
	if (error)
		goto out_user;

	dlm_wq = alloc_workqueue("dlm_wq", 0, 0);
	if (!dlm_wq) {
		error = -ENOMEM;
		goto out_plock;
	}

	printk("DLM installed\n");

	return 0;

 out_plock:
	dlm_plock_exit();
 out_user:
	dlm_user_exit();
 out_debug:
	dlm_unregister_debugfs();
	dlm_config_exit();
 out_lockspace:
	dlm_lockspace_exit();
 out_mem:
	dlm_midcomms_exit();
	dlm_memory_exit();
 out:
	return error;
}

static void __exit exit_dlm(void)
{
	/* be sure every pending work e.g. freeing is done */
	destroy_workqueue(dlm_wq);
	dlm_plock_exit();
	dlm_user_exit();
	dlm_config_exit();
	dlm_lockspace_exit();
	dlm_midcomms_exit();
	dlm_unregister_debugfs();
	dlm_memory_exit();
}

module_init(init_dlm);
module_exit(exit_dlm);

MODULE_DESCRIPTION("Distributed Lock Manager");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(dlm_new_lockspace);
EXPORT_SYMBOL_GPL(dlm_release_lockspace);
EXPORT_SYMBOL_GPL(dlm_lock);
EXPORT_SYMBOL_GPL(dlm_unlock);

