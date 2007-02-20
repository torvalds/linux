/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>

#define SECURITY_FRAMEWORK_VERSION	"1.0.0"

/* things that live in dummy.c */
extern struct security_operations dummy_security_ops;
extern void security_fixup_ops(struct security_operations *ops);

struct security_operations *security_ops;	/* Initialized to NULL */

static inline int verify(struct security_operations *ops)
{
	/* verify the security_operations structure exists */
	if (!ops)
		return -EINVAL;
	security_fixup_ops(ops);
	return 0;
}

static void __init do_security_initcalls(void)
{
	initcall_t *call;
	call = __security_initcall_start;
	while (call < __security_initcall_end) {
		(*call) ();
		call++;
	}
}

/**
 * security_init - initializes the security framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int __init security_init(void)
{
	printk(KERN_INFO "Security Framework v" SECURITY_FRAMEWORK_VERSION
	       " initialized\n");

	if (verify(&dummy_security_ops)) {
		printk(KERN_ERR "%s could not verify "
		       "dummy_security_ops structure.\n", __FUNCTION__);
		return -EIO;
	}

	security_ops = &dummy_security_ops;
	do_security_initcalls();

	return 0;
}

/**
 * register_security - registers a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function is to allow a security module to register itself with the
 * kernel security subsystem.  Some rudimentary checking is done on the @ops
 * value passed to this function.  A call to unregister_security() should be
 * done to remove this security_options structure from the kernel.
 *
 * If there is already a security module registered with the kernel,
 * an error will be returned.  Otherwise 0 is returned on success.
 */
int register_security(struct security_operations *ops)
{
	if (verify(ops)) {
		printk(KERN_DEBUG "%s could not verify "
		       "security_operations structure.\n", __FUNCTION__);
		return -EINVAL;
	}

	if (security_ops != &dummy_security_ops)
		return -EAGAIN;

	security_ops = ops;

	return 0;
}

/**
 * unregister_security - unregisters a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function removes a struct security_operations variable that had
 * previously been registered with a successful call to register_security().
 *
 * If @ops does not match the valued previously passed to register_security()
 * an error is returned.  Otherwise the default security options is set to the
 * the dummy_security_ops structure, and 0 is returned.
 */
int unregister_security(struct security_operations *ops)
{
	if (ops != security_ops) {
		printk(KERN_INFO "%s: trying to unregister "
		       "a security_opts structure that is not "
		       "registered, failing.\n", __FUNCTION__);
		return -EINVAL;
	}

	security_ops = &dummy_security_ops;

	return 0;
}

/**
 * mod_reg_security - allows security modules to be "stacked"
 * @name: a pointer to a string with the name of the security_options to be registered
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function allows security modules to be stacked if the currently loaded
 * security module allows this to happen.  It passes the @name and @ops to the
 * register_security function of the currently loaded security module.
 *
 * The return value depends on the currently loaded security module, with 0 as
 * success.
 */
int mod_reg_security(const char *name, struct security_operations *ops)
{
	if (verify(ops)) {
		printk(KERN_INFO "%s could not verify "
		       "security operations.\n", __FUNCTION__);
		return -EINVAL;
	}

	if (ops == security_ops) {
		printk(KERN_INFO "%s security operations "
		       "already registered.\n", __FUNCTION__);
		return -EINVAL;
	}

	return security_ops->register_security(name, ops);
}

/**
 * mod_unreg_security - allows a security module registered with mod_reg_security() to be unloaded
 * @name: a pointer to a string with the name of the security_options to be removed
 * @ops: a pointer to the struct security_options that is to be removed
 *
 * This function allows security modules that have been successfully registered
 * with a call to mod_reg_security() to be unloaded from the system.
 * This calls the currently loaded security module's unregister_security() call
 * with the @name and @ops variables.
 *
 * The return value depends on the currently loaded security module, with 0 as
 * success.
 */
int mod_unreg_security(const char *name, struct security_operations *ops)
{
	if (ops == security_ops) {
		printk(KERN_INFO "%s invalid attempt to unregister "
		       " primary security ops.\n", __FUNCTION__);
		return -EINVAL;
	}

	return security_ops->unregister_security(name, ops);
}

EXPORT_SYMBOL_GPL(register_security);
EXPORT_SYMBOL_GPL(unregister_security);
EXPORT_SYMBOL_GPL(mod_reg_security);
EXPORT_SYMBOL_GPL(mod_unreg_security);
EXPORT_SYMBOL(security_ops);
