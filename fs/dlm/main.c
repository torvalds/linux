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
#include "lowcomms.h"

static int __init init_dlm(void)
{
	int error;

	error = dlm_memory_init();
	if (error)
		goto out;

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

	error = dlm_netlink_init();
	if (error)
		goto out_user;

	error = dlm_plock_init();
	if (error)
		goto out_netlink;

	printk("DLM installed\n");

	return 0;

 out_netlink:
	dlm_netlink_exit();
 out_user:
	dlm_user_exit();
 out_debug:
	dlm_unregister_debugfs();
	dlm_config_exit();
 out_lockspace:
	dlm_lockspace_exit();
 out_mem:
	dlm_memory_exit();
 out:
	return error;
}

static void __exit exit_dlm(void)
{
	dlm_plock_exit();
	dlm_netlink_exit();
	dlm_user_exit();
	dlm_config_exit();
	dlm_memory_exit();
	dlm_lockspace_exit();
	dlm_lowcomms_exit();
	dlm_unregister_debugfs();
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

