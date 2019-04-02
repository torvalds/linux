/*
 * sysctl.c - Code for sysctl handling in NTFS Linux kernel driver. Part of
 *	      the Linux-NTFS project. Adapted from the old NTFS driver,
 *	      Copyright (C) 1997 Martin von Löwis, Régis Duchesne
 *
 * Copyright (c) 2002-2005 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef DE

#include <linux/module.h>

#ifdef CONFIG_SYSCTL

#include <linux/proc_fs.h>
#include <linux/sysctl.h>

#include "sysctl.h"
#include "de.h"

/* Definition of the ntfs sysctl. */
static struct ctl_table ntfs_sysctls[] = {
	{
		.procname	= "ntfs-de",
		.data		= &de_msgs,		/* Data pointer and size. */
		.maxlen		= sizeof(de_msgs),
		.mode		= 0644,			/* Mode, proc handler. */
		.proc_handler	= proc_dointvec
	},
	{}
};

/* Define the parent directory /proc/sys/fs. */
static struct ctl_table sysctls_root[] = {
	{
		.procname	= "fs",
		.mode		= 0555,
		.child		= ntfs_sysctls
	},
	{}
};

/* Storage for the sysctls header. */
static struct ctl_table_header *sysctls_root_table;

/**
 * ntfs_sysctl - add or remove the de sysctl
 * @add:	add (1) or remove (0) the sysctl
 *
 * Add or remove the de sysctl. Return 0 on success or -errno on error.
 */
int ntfs_sysctl(int add)
{
	if (add) {
		_ON(sysctls_root_table);
		sysctls_root_table = register_sysctl_table(sysctls_root);
		if (!sysctls_root_table)
			return -ENOMEM;
	} else {
		_ON(!sysctls_root_table);
		unregister_sysctl_table(sysctls_root_table);
		sysctls_root_table = NULL;
	}
	return 0;
}

#endif /* CONFIG_SYSCTL */
#endif /* DE */
