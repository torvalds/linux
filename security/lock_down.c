/* Lock down the kernel
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/efi.h>
#include <asm/setup.h>

#ifndef CONFIG_LOCK_DOWN_MANDATORY
#ifdef CONFIG_ALLOW_LOCKDOWN_LIFT_BY_SYSRQ
static __read_mostly bool kernel_locked_down;
#else
static __ro_after_init bool kernel_locked_down;
#endif
#else
#define kernel_locked_down true
#endif

/*
 * Put the kernel into lock-down mode.
 */
static void __init lock_kernel_down(const char *where)
{
#ifndef CONFIG_LOCK_DOWN_MANDATORY
	if (!kernel_locked_down) {
		kernel_locked_down = true;
		pr_notice("Kernel is locked down from %s; see man kernel_lockdown.7\n",
			  where);
	}
#endif
}

static int __init lockdown_param(char *ignored)
{
	lock_kernel_down("command line");
	return 0;
}

early_param("lockdown", lockdown_param);

/*
 * Lock the kernel down from very early in the arch setup.  This must happen
 * prior to things like ACPI being initialised.
 */
void __init init_lockdown(void)
{
#ifdef CONFIG_LOCK_DOWN_MANDATORY
	pr_notice("Kernel is locked down from config; see man kernel_lockdown.7\n");
#endif
#ifdef CONFIG_LOCK_DOWN_IN_EFI_SECURE_BOOT
	if (efi_enabled(EFI_SECURE_BOOT))
		lock_kernel_down("EFI secure boot");
#endif
}

/**
 * kernel_is_locked_down - Find out if the kernel is locked down
 * @what: Tag to use in notice generated if lockdown is in effect
 */
bool __kernel_is_locked_down(const char *what, bool first)
{
	if (what && first && kernel_locked_down)
		pr_notice("Lockdown: %s: %s is restricted; see man kernel_lockdown.7\n",
			  current->comm, what);
	return kernel_locked_down;
}
EXPORT_SYMBOL(__kernel_is_locked_down);

#ifdef CONFIG_ALLOW_LOCKDOWN_LIFT_BY_SYSRQ

/*
 * Take the kernel out of lockdown mode.
 */
static void lift_kernel_lockdown(void)
{
	pr_notice("Lifting lockdown\n");
	kernel_locked_down = false;
}

/*
 * Allow lockdown to be lifted by pressing something like SysRq+x (and not by
 * echoing the appropriate letter into the sysrq-trigger file).
 */
static void sysrq_handle_lockdown_lift(int key)
{
	if (kernel_locked_down)
		lift_kernel_lockdown();
}

static struct sysrq_key_op lockdown_lift_sysrq_op = {
	.handler	= sysrq_handle_lockdown_lift,
	.help_msg	= "unSB(x)",
	.action_msg	= "Disabling Secure Boot restrictions",
	.enable_mask	= SYSRQ_DISABLE_USERSPACE,
};

static int __init lockdown_lift_sysrq(void)
{
	if (kernel_locked_down) {
		lockdown_lift_sysrq_op.help_msg[5] = LOCKDOWN_LIFT_KEY;
		register_sysrq_key(LOCKDOWN_LIFT_KEY, &lockdown_lift_sysrq_op);
	}
	return 0;
}

late_initcall(lockdown_lift_sysrq);

#endif /* CONFIG_ALLOW_LOCKDOWN_LIFT_BY_SYSRQ */
