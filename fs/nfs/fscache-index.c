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

	key->nfsversion = clp->rpc_ops->version;
	key->family = clp->cl_addr.ss_family;

	memset(key, 0, len);

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
