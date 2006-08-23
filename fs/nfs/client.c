/* client.c: NFS client sharing and management code
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/metrics.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/nfs_xdr.h>

#include <asm/system.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

static DEFINE_SPINLOCK(nfs_client_lock);
static LIST_HEAD(nfs_client_list);
static DECLARE_WAIT_QUEUE_HEAD(nfs_client_active_wq);

/*
 * Allocate a shared client record
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
static struct nfs_client *nfs_alloc_client(const char *hostname,
					   const struct sockaddr_in *addr,
					   int nfsversion)
{
	struct nfs_client *clp;
	int error;

	if ((clp = kzalloc(sizeof(*clp), GFP_KERNEL)) == NULL)
		goto error_0;

	error = rpciod_up();
	if (error < 0) {
		dprintk("%s: couldn't start rpciod! Error = %d\n",
				__FUNCTION__, error);
		__set_bit(NFS_CS_RPCIOD, &clp->cl_res_state);
		goto error_1;
	}

	if (nfsversion == 4) {
		if (nfs_callback_up() < 0)
			goto error_2;
		__set_bit(NFS_CS_CALLBACK, &clp->cl_res_state);
	}

	atomic_set(&clp->cl_count, 1);
	clp->cl_cons_state = NFS_CS_INITING;

	clp->cl_nfsversion = nfsversion;
	memcpy(&clp->cl_addr, addr, sizeof(clp->cl_addr));

	if (hostname) {
		clp->cl_hostname = kstrdup(hostname, GFP_KERNEL);
		if (!clp->cl_hostname)
			goto error_3;
	}

	INIT_LIST_HEAD(&clp->cl_superblocks);
	clp->cl_rpcclient = ERR_PTR(-EINVAL);

#ifdef CONFIG_NFS_V4
	init_rwsem(&clp->cl_sem);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_state_owners);
	INIT_LIST_HEAD(&clp->cl_unused);
	spin_lock_init(&clp->cl_lock);
	INIT_WORK(&clp->cl_renewd, nfs4_renew_state, clp);
	rpc_init_wait_queue(&clp->cl_rpcwaitq, "NFS client");
	clp->cl_boot_time = CURRENT_TIME;
	clp->cl_state = 1 << NFS4CLNT_LEASE_EXPIRED;
#endif

	return clp;

error_3:
	nfs_callback_down();
	__clear_bit(NFS_CS_CALLBACK, &clp->cl_res_state);
error_2:
	rpciod_down();
	__clear_bit(NFS_CS_RPCIOD, &clp->cl_res_state);
error_1:
	kfree(clp);
error_0:
	return NULL;
}

/*
 * Destroy a shared client record
 */
static void nfs_free_client(struct nfs_client *clp)
{
	dprintk("--> nfs_free_client(%d)\n", clp->cl_nfsversion);

#ifdef CONFIG_NFS_V4
	if (__test_and_clear_bit(NFS_CS_IDMAP, &clp->cl_res_state)) {
		while (!list_empty(&clp->cl_unused)) {
			struct nfs4_state_owner *sp;

			sp = list_entry(clp->cl_unused.next,
					struct nfs4_state_owner,
					so_list);
			list_del(&sp->so_list);
			kfree(sp);
		}
		BUG_ON(!list_empty(&clp->cl_state_owners));
		nfs_idmap_delete(clp);
	}
#endif

	/* -EIO all pending I/O */
	if (!IS_ERR(clp->cl_rpcclient))
		rpc_shutdown_client(clp->cl_rpcclient);

	if (__test_and_clear_bit(NFS_CS_CALLBACK, &clp->cl_res_state))
		nfs_callback_down();

	if (__test_and_clear_bit(NFS_CS_RPCIOD, &clp->cl_res_state))
	rpciod_down();

	kfree(clp->cl_hostname);
	kfree(clp);

	dprintk("<-- nfs_free_client()\n");
}

/*
 * Release a reference to a shared client record
 */
void nfs_put_client(struct nfs_client *clp)
{
	dprintk("--> nfs_put_client({%d})\n", atomic_read(&clp->cl_count));

	if (atomic_dec_and_lock(&clp->cl_count, &nfs_client_lock)) {
		list_del(&clp->cl_share_link);
		spin_unlock(&nfs_client_lock);

		BUG_ON(!list_empty(&clp->cl_superblocks));

		nfs_free_client(clp);
	}
}

/*
 * Find a client by address
 * - caller must hold nfs_client_lock
 */
static struct nfs_client *__nfs_find_client(const struct sockaddr_in *addr, int nfsversion)
{
	struct nfs_client *clp;

	list_for_each_entry(clp, &nfs_client_list, cl_share_link) {
		/* Different NFS versions cannot share the same nfs_client */
		if (clp->cl_nfsversion != nfsversion)
			continue;

		if (memcmp(&clp->cl_addr.sin_addr, &addr->sin_addr,
			   sizeof(clp->cl_addr.sin_addr)) != 0)
			continue;

		if (clp->cl_addr.sin_port == addr->sin_port)
			goto found;
	}

	return NULL;

found:
	atomic_inc(&clp->cl_count);
	return clp;
}

/*
 * Find a client by IP address and protocol version
 * - returns NULL if no such client
 */
struct nfs_client *nfs_find_client(const struct sockaddr_in *addr, int nfsversion)
{
	struct nfs_client *clp;

	spin_lock(&nfs_client_lock);
	clp = __nfs_find_client(addr, nfsversion);
	spin_unlock(&nfs_client_lock);

	BUG_ON(clp->cl_cons_state == 0);

	return clp;
}

/*
 * Look up a client by IP address and protocol version
 * - creates a new record if one doesn't yet exist
 */
struct nfs_client *nfs_get_client(const char *hostname,
				  const struct sockaddr_in *addr,
				  int nfsversion)
{
	struct nfs_client *clp, *new = NULL;
	int error;

	dprintk("--> nfs_get_client(%s,"NIPQUAD_FMT":%d,%d)\n",
		hostname ?: "", NIPQUAD(addr->sin_addr),
		addr->sin_port, nfsversion);

	/* see if the client already exists */
	do {
		spin_lock(&nfs_client_lock);

		clp = __nfs_find_client(addr, nfsversion);
		if (clp)
			goto found_client;
		if (new)
			goto install_client;

		spin_unlock(&nfs_client_lock);

		new = nfs_alloc_client(hostname, addr, nfsversion);
	} while (new);

	return ERR_PTR(-ENOMEM);

	/* install a new client and return with it unready */
install_client:
	clp = new;
	list_add(&clp->cl_share_link, &nfs_client_list);
	spin_unlock(&nfs_client_lock);
	dprintk("--> nfs_get_client() = %p [new]\n", clp);
	return clp;

	/* found an existing client
	 * - make sure it's ready before returning
	 */
found_client:
	spin_unlock(&nfs_client_lock);

	if (new)
		nfs_free_client(new);

	if (clp->cl_cons_state == NFS_CS_INITING) {
		DECLARE_WAITQUEUE(myself, current);

		add_wait_queue(&nfs_client_active_wq, &myself);

		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (signal_pending(current) ||
			    clp->cl_cons_state > NFS_CS_READY)
				break;
			schedule();
		}

		remove_wait_queue(&nfs_client_active_wq, &myself);

		if (signal_pending(current)) {
			nfs_put_client(clp);
			return ERR_PTR(-ERESTARTSYS);
		}
	}

	if (clp->cl_cons_state < NFS_CS_READY) {
		error = clp->cl_cons_state;
		nfs_put_client(clp);
		return ERR_PTR(error);
	}

	dprintk("--> nfs_get_client() = %p [share]\n", clp);
	return clp;
}

/*
 * Mark a server as ready or failed
 */
void nfs_mark_client_ready(struct nfs_client *clp, int state)
{
	clp->cl_cons_state = state;
	wake_up_all(&nfs_client_active_wq);
}
