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

#include <linux/security.h>
#include <linux/export.h>

static __ro_after_init bool kernel_locked_down;

/*
 * Put the kernel into lock-down mode.
 */
static void __init lock_kernel_down(const char *where)
{
	if (!kernel_locked_down) {
		kernel_locked_down = true;
		pr_notice("Kernel is locked down from %s; see man kernel_lockdown.7\n",
			  where);
	}
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
		pr_notice("Lockdown: %s is restricted; see man kernel_lockdown.7\n",
			  what);
	return kernel_locked_down;
}
EXPORT_SYMBOL(__kernel_is_locked_down);
