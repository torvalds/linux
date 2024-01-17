// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sysctl.c - Code for sysctl handling in NTFS Linux kernel driver. Part of
 *	      the Linux-NTFS project. Adapted from the old NTFS driver,
 *	      Copyright (C) 1997 Martin von Löwis, Régis Duchesne
 *
 * Copyright (c) 2002-2005 Anton Altaparmakov
 */

#ifdef DEBUG

#include <linux/module.h>

#ifdef CONFIG_SYSCTL

#include <linux/proc_fs.h>
#include <linux/sysctl.h>

#include "sysctl.h"
#include "debug.h"

/* Definition of the ntfs sysctl. */
static struct ctl_table ntfs_sysctls[] = {
	{
		.procname	= "ntfs-debug",
		.data		= &debug_msgs,		/* Data pointer and size. */
		.maxlen		= sizeof(debug_msgs),
		.mode		= 0644,			/* Mode, proc handler. */
		.proc_handler	= proc_dointvec
	},
};

/* Storage for the sysctls header. */
static struct ctl_table_header *sysctls_root_table;

/**
 * ntfs_sysctl - add or remove the debug sysctl
 * @add:	add (1) or remove (0) the sysctl
 *
 * Add or remove the debug sysctl. Return 0 on success or -errno on error.
 */
int ntfs_sysctl(int add)
{
	if (add) {
		BUG_ON(sysctls_root_table);
		sysctls_root_table = register_sysctl("fs", ntfs_sysctls);
		if (!sysctls_root_table)
			return -ENOMEM;
	} else {
		BUG_ON(!sysctls_root_table);
		unregister_sysctl_table(sysctls_root_table);
		sysctls_root_table = NULL;
	}
	return 0;
}

#endif /* CONFIG_SYSCTL */
#endif /* DEBUG */
