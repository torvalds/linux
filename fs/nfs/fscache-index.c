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

/*
 * Layout of the key for an NFS server cache object.
 */
struct nfs_server_key {
	uint16_t	nfsversion;		/* NFS protocol version */
	uint16_t	family;			/* address family */
	uint16_t	port;			/* IP port */
	union {
		struct in_addr	ipv4_addr;	/* IPv4 address */
		struct in6_addr ipv6_addr;	/* IPv6 address */
	} addr[0];
};

/*
 * Generate a key to describe a server in the main NFS index
 * - We return the length of the key, or 0 if we can't generate one
 */
static uint16_t nfs_server_get_key(const void *cookie_netfs_data,
				   void *buffer, uint16_t bufmax)
{
	const struct nfs_client *clp = cookie_netfs_data;
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &clp->cl_addr;
	const struct sockaddr_in *sin = (struct sockaddr_in *) &clp->cl_addr;
	struct nfs_server_key *key = buffer;
	uint16_t len = sizeof(struct nfs_server_key);

	memset(key, 0, len);
	key->nfsversion = clp->rpc_ops->version;
	key->family = clp->cl_addr.ss_family;

	switch (clp->cl_addr.ss_family) {
	case AF_INET:
		key->port = sin->sin_port;
		key->addr[0].ipv4_addr = sin->sin_addr;
		len += sizeof(key->addr[0].ipv4_addr);
		break;

	case AF_INET6:
		key->port = sin6->sin6_port;
		key->addr[0].ipv6_addr = sin6->sin6_addr;
		len += sizeof(key->addr[0].ipv6_addr);
		break;

	default:
		printk(KERN_WARNING "NFS: Unknown network family '%d'\n",
		       clp->cl_addr.ss_family);
		len = 0;
		break;
	}

	return len;
}

/*
 * Define the server object for FS-Cache.  This is used to describe a server
 * object to fscache_acquire_cookie().  It is keyed by the NFS protocol and
 * server address parameters.
 */
const struct fscache_cookie_def nfs_fscache_server_index_def = {
	.name		= "NFS.server",
	.type 		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= nfs_server_get_key,
};

/*
 * Generate a key to describe a superblock key in the main NFS index
 */
static uint16_t nfs_super_get_key(const void *cookie_netfs_data,
				  void *buffer, uint16_t bufmax)
{
	const struct nfs_fscache_key *key;
	const struct nfs_server *nfss = cookie_netfs_data;
	uint16_t len;

	key = nfss->fscache_key;
	len = sizeof(key->key) + key->key.uniq_len;
	if (len > bufmax) {
		len = 0;
	} else {
		memcpy(buffer, &key->key, sizeof(key->key));
		memcpy(buffer + sizeof(key->key),
		       key->key.uniquifier, key->key.uniq_len);
	}

	return len;
}

/*
 * Define the superblock object for FS-Cache.  This is used to describe a
 * superblock object to fscache_acquire_cookie().  It is keyed by all the NFS
 * parameters that might cause a separate superblock.
 */
const struct fscache_cookie_def nfs_fscache_super_index_def = {
	.name		= "NFS.super",
	.type 		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= nfs_super_get_key,
};

/*
 * Definition of the auxiliary data attached to NFS inode storage objects
 * within the cache.
 *
 * The contents of this struct are recorded in the on-disk local cache in the
 * auxiliary data attached to the data storage object backing an inode.  This
 * permits coherency to be managed when a new inode binds to an already extant
 * cache object.
 */
struct nfs_fscache_inode_auxdata {
	struct timespec	mtime;
	struct timespec	ctime;
	loff_t		size;
	u64		change_attr;
};

/*
 * Generate a key to describe an NFS inode in an NFS server's index
 */
static uint16_t nfs_fscache_inode_get_key(const void *cookie_netfs_data,
					  void *buffer, uint16_t bufmax)
{
	const struct nfs_inode *nfsi = cookie_netfs_data;
	uint16_t nsize;

	/* use the inode's NFS filehandle as the key */
	nsize = nfsi->fh.size;
	memcpy(buffer, nfsi->fh.data, nsize);
	return nsize;
}

/*
 * Get certain file attributes from the netfs data
 * - This function can be absent for an index
 * - Not permitted to return an error
 * - The netfs data from the cookie being used as the source is presented
 */
static void nfs_fscache_inode_get_attr(const void *cookie_netfs_data,
				       uint64_t *size)
{
	const struct nfs_inode *nfsi = cookie_netfs_data;

	*size = nfsi->vfs_inode.i_size;
}

/*
 * Get the auxiliary data from netfs data
 * - This function can be absent if the index carries no state data
 * - Should store the auxiliary data in the buffer
 * - Should return the amount of amount stored
 * - Not permitted to return an error
 * - The netfs data from the cookie being used as the source is presented
 */
static uint16_t nfs_fscache_inode_get_aux(const void *cookie_netfs_data,
					  void *buffer, uint16_t bufmax)
{
	struct nfs_fscache_inode_auxdata auxdata;
	const struct nfs_inode *nfsi = cookie_netfs_data;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.size = nfsi->vfs_inode.i_size;
	auxdata.mtime = nfsi->vfs_inode.i_mtime;
	auxdata.ctime = nfsi->vfs_inode.i_ctime;

	if (NFS_SERVER(&nfsi->vfs_inode)->nfs_client->rpc_ops->version == 4)
		auxdata.change_attr = nfsi->vfs_inode.i_version;

	if (bufmax > sizeof(auxdata))
		bufmax = sizeof(auxdata);

	memcpy(buffer, &auxdata, bufmax);
	return bufmax;
}

/*
 * Consult the netfs about the state of an object
 * - This function can be absent if the index carries no state data
 * - The netfs data from the cookie being used as the target is
 *   presented, as is the auxiliary data
 */
static
enum fscache_checkaux nfs_fscache_inode_check_aux(void *cookie_netfs_data,
						  const void *data,
						  uint16_t datalen)
{
	struct nfs_fscache_inode_auxdata auxdata;
	struct nfs_inode *nfsi = cookie_netfs_data;

	if (datalen != sizeof(auxdata))
		return FSCACHE_CHECKAUX_OBSOLETE;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.size = nfsi->vfs_inode.i_size;
	auxdata.mtime = nfsi->vfs_inode.i_mtime;
	auxdata.ctime = nfsi->vfs_inode.i_ctime;

	if (NFS_SERVER(&nfsi->vfs_inode)->nfs_client->rpc_ops->version == 4)
		auxdata.change_attr = nfsi->vfs_inode.i_version;

	if (memcmp(data, &auxdata, datalen) != 0)
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

/*
 * Indication from FS-Cache that the cookie is no longer cached
 * - This function is called when the backing store currently caching a cookie
 *   is removed
 * - The netfs should use this to clean up any markers indicating cached pages
 * - This is mandatory for any object that may have data
 */
static void nfs_fscache_inode_now_uncached(void *cookie_netfs_data)
{
	struct nfs_inode *nfsi = cookie_netfs_data;
	struct pagevec pvec;
	pgoff_t first;
	int loop, nr_pages;

	pagevec_init(&pvec, 0);
	first = 0;

	dprintk("NFS: nfs_inode_now_uncached: nfs_inode 0x%p\n", nfsi);

	for (;;) {
		/* grab a bunch of pages to unmark */
		nr_pages = pagevec_lookup(&pvec,
					  nfsi->vfs_inode.i_mapping,
					  first,
					  PAGEVEC_SIZE - pagevec_count(&pvec));
		if (!nr_pages)
			break;

		for (loop = 0; loop < nr_pages; loop++)
			ClearPageFsCache(pvec.pages[loop]);

		first = pvec.pages[nr_pages - 1]->index + 1;

		pvec.nr = nr_pages;
		pagevec_release(&pvec);
		cond_resched();
	}
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
	.get_key	= nfs_fscache_inode_get_key,
	.get_attr	= nfs_fscache_inode_get_attr,
	.get_aux	= nfs_fscache_inode_get_aux,
	.check_aux	= nfs_fscache_inode_check_aux,
	.now_uncached	= nfs_fscache_inode_now_uncached,
	.get_context	= nfs_fh_get_context,
	.put_context	= nfs_fh_put_context,
};
