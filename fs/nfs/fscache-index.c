/* NFS FS-Cache index structure definition
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/in6.h>

#include "internal.h"
#include "fscache.h"

#define NFSDBG_FACILITY		NFSDBG_FSCACHE

/*
 * Define the NFS filesystem for FS-Cache.  Upon registration FS-Cache sticks
 * the cookie for the top-level index object for NFS into here.  The top-level
 * index can than have other cache objects inserted into it.
 */
struct fscache_netfs nfs_fscache_netfs = {
	.name		= "nfs",
	.version	= 0,
};

/*
 * Register NFS for caching
 */
int nfs_fscache_register(void)
{
	return fscache_register_netfs(&nfs_fscache_netfs);
}

/*
 * Unregister NFS for caching
 */
void nfs_fscache_unregister(void)
{
	fscache_unregister_netfs(&nfs_fscache_netfs);
}
