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
#include <linux/iversion.h>

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

/*
 * Define the server object for FS-Cache.  This is used to describe a server
 * object to fscache_acquire_cookie().  It is keyed by the NFS protocol and
 * server address parameters.
 */
const struct fscache_cookie_def nfs_fscache_server_index_def = {
	.name		= "NFS.server",
	.type 		= FSCACHE_COOKIE_TYPE_INDEX,
};

/*
 * Define the superblock object for FS-Cache.  This is used to describe a
 * superblock object to fscache_acquire_cookie().  It is keyed by all the NFS
 * parameters that might cause a separate superblock.
 */
const struct fscache_cookie_def nfs_fscache_super_index_def = {
	.name		= "NFS.super",
	.type 		= FSCACHE_COOKIE_TYPE_INDEX,
};

/*
 * Consult the netfs about the state of an object
 * - This function can be absent if the index carries no state data
 * - The netfs data from the cookie being used as the target is
 *   presented, as is the auxiliary data
 */
static
enum fscache_checkaux nfs_fscache_inode_check_aux(void *cookie_netfs_data,
						  const void *data,
						  uint16_t datalen,
						  loff_t object_size)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = cookie_netfs_data;

	if (datalen != sizeof(auxdata))
		return FSCACHE_CHECKAUX_OBSOLETE;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.mtime = nfsi->vfs_inode.i_mtime;
	auxdata.ctime = nfsi->vfs_inode.i_ctime;

	if (NFS_SERVER(&nfsi->vfs_inode)->nfs_client->rpc_ops->version == 4)
		auxdata.change_attr = inode_peek_iversion_raw(&nfsi->vfs_inode);

	if (memcmp(data, &auxdata, datalen) != 0)
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

/*
 * Get an extra reference on a read context.
 * - This function can be absent if the completion function doesn't require a
 *   context.
 * - The read context is passed back to NFS in the event that a data read on the
 *   cache fails with EIO - in which case the server must be contacted to
 *   retrieve the data, which requires the read context for security.
 */
static void nfs_fh_get_context(void *cookie_netfs_data, void *context)
{
	get_nfs_open_context(context);
}

/*
 * Release an extra reference on a read context.
 * - This function can be absent if the completion function doesn't require a
 *   context.
 */
static void nfs_fh_put_context(void *cookie_netfs_data, void *context)
{
	if (context)
		put_nfs_open_context(context);
}

/*
 * Define the inode object for FS-Cache.  This is used to describe an inode
 * object to fscache_acquire_cookie().  It is keyed by the NFS file handle for
 * an inode.
 *
 * Coherency is managed by comparing the copies of i_size, i_mtime and i_ctime
 * held in the cache auxiliary data for the data storage object with those in
 * the inode struct in memory.
 */
const struct fscache_cookie_def nfs_fscache_inode_object_def = {
	.name		= "NFS.fh",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.check_aux	= nfs_fscache_inode_check_aux,
	.get_context	= nfs_fh_get_context,
	.put_context	= nfs_fh_put_context,
};
