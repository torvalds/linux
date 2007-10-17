/*
 *  Capabilities Linux Security Module
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/ptrace.h>
#include <linux/moduleparam.h>

static struct security_operations capability_ops = {
	.ptrace =			cap_ptrace,
	.capget =			cap_capget,
	.capset_check =			cap_capset_check,
	.capset_set =			cap_capset_set,
	.capable =			cap_capable,
	.settime =			cap_settime,
	.netlink_send =			cap_netlink_send,
	.netlink_recv =			cap_netlink_recv,

	.bprm_apply_creds =		cap_bprm_apply_creds,
	.bprm_set_security =		cap_bprm_set_security,
	.bprm_secureexec =		cap_bprm_secureexec,

	.inode_setxattr =		cap_inode_setxattr,
	.inode_removexattr =		cap_inode_removexattr,
	.inode_need_killpriv =		cap_inode_need_killpriv,
	.inode_killpriv =		cap_inode_killpriv,

	.task_kill =			cap_task_kill,
	.task_setscheduler =		cap_task_setscheduler,
	.task_setioprio =		cap_task_setioprio,
	.task_setnice =			cap_task_setnice,
	.task_post_setuid =		cap_task_post_setuid,
	.task_reparent_to_init =	cap_task_reparent_to_init,

	.syslog =                       cap_syslog,

	.vm_enough_memory =             cap_vm_enough_memory,
};

/* flag to keep track of how we were registered */
static int secondary;

static int capability_disable;
module_param_named(disable, capability_disable, int, 0);

static int __init capability_init (void)
{
	if (capability_disable) {
		printk(KERN_INFO "Capabilities disabled at initialization\n");
		return 0;
	}
	/* register ourselves with the security framework */
	if (register_security (&capability_ops)) {
		/* try registering with primary module */
		if (mod_reg_security (KBUILD_MODNAME, &capability_ops)) {
			printk (KERN_INFO "Failure registering capabilities "
				"with primary security module.\n");
			return -EINVAL;
		}
		secondary = 1;
	}
	printk (KERN_INFO "Capability LSM initialized%s\n",
		secondary ? " as secondary" : "");
	return 0;
}

security_initcall (capability_init);
