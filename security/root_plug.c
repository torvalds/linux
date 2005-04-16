/*
 * Root Plug sample LSM module
 *
 * Originally written for a Linux Journal.
 *
 * Copyright (C) 2002 Greg Kroah-Hartman <greg@kroah.com>
 *
 * Prevents any programs running with egid == 0 if a specific USB device
 * is not present in the system.  Yes, it can be gotten around, but is a
 * nice starting point for people to play with, and learn the LSM
 * interface.
 *
 * If you want to turn this into something with a semblance of security,
 * you need to hook the task_* functions also.
 *
 * See http://www.linuxjournal.com/article.php?sid=6279 for more information
 * about this code.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/usb.h>

/* flag to keep track of how we were registered */
static int secondary;

/* default is a generic type of usb to serial converter */
static int vendor_id = 0x0557;
static int product_id = 0x2008;

module_param(vendor_id, uint, 0400);
MODULE_PARM_DESC(vendor_id, "USB Vendor ID of device to look for");

module_param(product_id, uint, 0400);
MODULE_PARM_DESC(product_id, "USB Product ID of device to look for");

/* should we print out debug messages */
static int debug = 0;

module_param(debug, bool, 0600);
MODULE_PARM_DESC(debug, "Debug enabled or not");

#if defined(CONFIG_SECURITY_ROOTPLUG_MODULE)
#define MY_NAME THIS_MODULE->name
#else
#define MY_NAME "root_plug"
#endif

#define root_dbg(fmt, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: %s: " fmt ,	\
				MY_NAME , __FUNCTION__ , 	\
				## arg);			\
	} while (0)

static int rootplug_bprm_check_security (struct linux_binprm *bprm)
{
	struct usb_device *dev;

	root_dbg("file %s, e_uid = %d, e_gid = %d\n",
		 bprm->filename, bprm->e_uid, bprm->e_gid);

	if (bprm->e_gid == 0) {
		dev = usb_find_device(vendor_id, product_id);
		if (!dev) {
			root_dbg("e_gid = 0, and device not found, "
				 "task not allowed to run...\n");
			return -EPERM;
		}
		usb_put_dev(dev);
	}

	return 0;
}

static struct security_operations rootplug_security_ops = {
	/* Use the capability functions for some of the hooks */
	.ptrace =			cap_ptrace,
	.capget =			cap_capget,
	.capset_check =			cap_capset_check,
	.capset_set =			cap_capset_set,
	.capable =			cap_capable,

	.bprm_apply_creds =		cap_bprm_apply_creds,
	.bprm_set_security =		cap_bprm_set_security,

	.task_post_setuid =		cap_task_post_setuid,
	.task_reparent_to_init =	cap_task_reparent_to_init,

	.bprm_check_security =		rootplug_bprm_check_security,
};

static int __init rootplug_init (void)
{
	/* register ourselves with the security framework */
	if (register_security (&rootplug_security_ops)) {
		printk (KERN_INFO 
			"Failure registering Root Plug module with the kernel\n");
		/* try registering with primary module */
		if (mod_reg_security (MY_NAME, &rootplug_security_ops)) {
			printk (KERN_INFO "Failure registering Root Plug "
				" module with primary security module.\n");
			return -EINVAL;
		}
		secondary = 1;
	}
	printk (KERN_INFO "Root Plug module initialized, "
		"vendor_id = %4.4x, product id = %4.4x\n", vendor_id, product_id);
	return 0;
}

static void __exit rootplug_exit (void)
{
	/* remove ourselves from the security framework */
	if (secondary) {
		if (mod_unreg_security (MY_NAME, &rootplug_security_ops))
			printk (KERN_INFO "Failure unregistering Root Plug "
				" module with primary module.\n");
	} else { 
		if (unregister_security (&rootplug_security_ops)) {
			printk (KERN_INFO "Failure unregistering Root Plug "
				"module with the kernel\n");
		}
	}
	printk (KERN_INFO "Root Plug module removed\n");
}

security_initcall (rootplug_init);
module_exit (rootplug_exit);

MODULE_DESCRIPTION("Root Plug sample LSM module, written for Linux Journal article");
MODULE_LICENSE("GPL");

