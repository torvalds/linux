/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "lock.h"
#include "user.h"
#include "memory.h"
#include "config.h"

#ifdef CONFIG_DLM_DEBUG
int dlm_register_debugfs(void);
void dlm_unregister_debugfs(void);
#else
static inline int dlm_register_debugfs(void) { return 0; }
static inline void dlm_unregister_debugfs(void) { }
#endif
int dlm_netlink_init(void);
void dlm_netlink_exit(void);

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

	error = dlm_register_debugfs();
	if (error)
		goto out_config;

	error = dlm_user_init();
	if (error)
		goto out_debug;

	error = dlm_netlink_init();
	if (error)
		goto out_user;

	printk("DLM (built %s %s) installed\n", __DATE__, __TIME__);

	return 0;

 out_user:
	dlm_user_exit();
 out_debug:
	dlm_unregister_debugfs();
 out_config:
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
	dlm_netlink_exit();
	dlm_user_exit();
	dlm_config_exit();
	dlm_memory_exit();
	dlm_lockspace_exit();
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

