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
static LIST_HEAD(nfs_volume_list);
static DECLARE_WAIT_QUEUE_HEAD(nfs_client_active_wq);

/*
 * RPC cruft for NFS
 */
static struct rpc_version *nfs_version[5] = {
	[2]			= &nfs_version2,
#ifdef CONFIG_NFS_V3
	[3]			= &nfs_version3,
#endif
#ifdef CONFIG_NFS_V4
	[4]			= &nfs_version4,
#endif
};

struct rpc_program nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfs_version),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
	.pipe_dir_name		= "/nfs",
};

struct rpc_stat nfs_rpcstat = {
	.program		= &nfs_program
};


#ifdef CONFIG_NFS_V3_ACL
static struct rpc_stat		nfsacl_rpcstat = { &nfsacl_program };
static struct rpc_version *	nfsacl_version[] = {
	[3]			= &nfsacl_version3,
};

struct rpc_program		nfsacl_program = {
	.name			= "nfsacl",
	.number			= NFS_ACL_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfsacl_version),
	.version		= nfsacl_version,
	.stats			= &nfsacl_rpcstat,
};
#endif  /* CONFIG_NFS_V3_ACL */

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
		goto error_1;
	}
	__set_bit(NFS_CS_RPCIOD, &clp->cl_res_state);

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
	INIT_DELAYED_WORK(&clp->cl_renewd, nfs4_renew_state);
	rpc_init_wait_queue(&clp->cl_rpcwaitq, "NFS client");
	clp->cl_boot_time = CURRENT_TIME;
	clp->cl_state = 1 << NFS4CLNT_LEASE_EXPIRED;
#endif

	return clp;

error_3:
	if (__test_and_clear_bit(NFS_CS_CALLBACK, &clp->cl_res_state))
		nfs_callback_down();
error_2:
	rpciod_down();
	__clear_bit(NFS_CS_RPCIOD, &clp->cl_res_state);
error_1:
	kfree(clp);
error_0:
	return NULL;
}

static void nfs4_shutdown_client(struct nfs_client *clp)
{
#ifdef CONFIG_NFS_V4
	if (__test_and_clear_bit(NFS_CS_RENEWD, &clp->cl_res_state))
		nfs4_kill_renewd(clp);
	while (!list_empty(&clp->cl_unused)) {
		struct nfs4_state_owner *sp;

		sp = list_entry(clp->cl_unused.next,
				struct nfs4_state_owner,
				so_list);
		list_del(&sp->so_list);
		kfree(sp);
	}
	BUG_ON(!list_empty(&clp->cl_state_owners));
	if (__test_and_clear_bit(NFS_CS_IDMAP, &clp->cl_res_state))
		nfs_idmap_delete(clp);
#endif
}

/*
 * Destroy a shared client record
 */
static void nfs_free_client(struct nfs_client *clp)
{
	dprintk("--> nfs_free_client(%d)\n", clp->cl_nfsversion);

	nfs4_shutdown_client(clp);

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
	if (!clp)
		return;

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
static struct nfs_client *__nfs_find_client(const struct sockaddr_in *addr, int nfsversion, int match_port)
{
	struct nfs_client *clp;

	list_for_each_entry(clp, &nfs_client_list, cl_share_link) {
		/* Don't match clients that failed to initialise properly */
		if (clp->cl_cons_state < 0)
			continue;

		/* Different NFS versions cannot share the same nfs_client */
		if (clp->cl_nfsversion != nfsversion)
			continue;

		if (memcmp(&clp->cl_addr.sin_addr, &addr->sin_addr,
			   sizeof(clp->cl_addr.sin_addr)) != 0)
			continue;

		if (!match_port || clp->cl_addr.sin_port == addr->sin_port)
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
	clp = __nfs_find_client(addr, nfsversion, 0);
	spin_unlock(&nfs_client_lock);
	if (clp != NULL && clp->cl_cons_state != NFS_CS_READY) {
		nfs_put_client(clp);
		clp = NULL;
	}
	return clp;
}

/*
 * Look up a client by IP address and protocol version
 * - creates a new record if one doesn't yet exist
 */
static struct nfs_client *nfs_get_client(const char *hostname,
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

		clp = __nfs_find_client(addr, nfsversion, 1);
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

	error = wait_event_interruptible(nfs_client_active_wq,
				clp->cl_cons_state != NFS_CS_INITING);
	if (error < 0) {
		nfs_put_client(clp);
		return ERR_PTR(-ERESTARTSYS);
	}

	if (clp->cl_cons_state < NFS_CS_READY) {
		error = clp->cl_cons_state;
		nfs_put_client(clp);
		return ERR_PTR(error);
	}

	BUG_ON(clp->cl_cons_state != NFS_CS_READY);

	dprintk("--> nfs_get_client() = %p [share]\n", clp);
	return clp;
}

/*
 * Mark a server as ready or failed
 */
static void nfs_mark_client_ready(struct nfs_client *clp, int state)
{
	clp->cl_cons_state = state;
	wake_up_all(&nfs_client_active_wq);
}

/*
 * Initialise the timeout values for a connection
 */
static void nfs_init_timeout_values(struct rpc_timeout *to, int proto,
				    unsigned int timeo, unsigned int retrans)
{
	to->to_initval = timeo * HZ / 10;
	to->to_retries = retrans;
	if (!to->to_retries)
		to->to_retries = 2;

	switch (proto) {
	case IPPROTO_TCP:
		if (!to->to_initval)
			to->to_initval = 60 * HZ;
		if (to->to_initval > NFS_MAX_TCP_TIMEOUT)
			to->to_initval = NFS_MAX_TCP_TIMEOUT;
		to->to_increment = to->to_initval;
		to->to_maxval = to->to_initval + (to->to_increment * to->to_retries);
		to->to_exponential = 0;
		break;
	case IPPROTO_UDP:
	default:
		if (!to->to_initval)
			to->to_initval = 11 * HZ / 10;
		if (to->to_initval > NFS_MAX_UDP_TIMEOUT)
			to->to_initval = NFS_MAX_UDP_TIMEOUT;
		to->to_maxval = NFS_MAX_UDP_TIMEOUT;
		to->to_exponential = 1;
		break;
	}
}

/*
 * Create an RPC client handle
 */
static int nfs_create_rpc_client(struct nfs_client *clp, int proto,
						unsigned int timeo,
						unsigned int retrans,
						rpc_authflavor_t flavor)
{
	struct rpc_timeout	timeparms;
	struct rpc_clnt		*clnt = NULL;
	struct rpc_create_args args = {
		.protocol	= proto,
		.address	= (struct sockaddr *)&clp->cl_addr,
		.addrsize	= sizeof(clp->cl_addr),
		.timeout	= &timeparms,
		.servername	= clp->cl_hostname,
		.program	= &nfs_program,
		.version	= clp->rpc_ops->version,
		.authflavor	= flavor,
	};

	if (!IS_ERR(clp->cl_rpcclient))
		return 0;

	nfs_init_timeout_values(&timeparms, proto, timeo, retrans);
	clp->retrans_timeo = timeparms.to_initval;
	clp->retrans_count = timeparms.to_retries;

	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		dprintk("%s: cannot create RPC client. Error = %ld\n",
				__FUNCTION__, PTR_ERR(clnt));
		return PTR_ERR(clnt);
	}

	clp->cl_rpcclient = clnt;
	return 0;
}

/*
 * Version 2 or 3 client destruction
 */
static void nfs_destroy_server(struct nfs_server *server)
{
	if (!IS_ERR(server->client_acl))
		rpc_shutdown_client(server->client_acl);

	if (!(server->flags & NFS_MOUNT_NONLM))
		lockd_down();	/* release rpc.lockd */
}

/*
 * Version 2 or 3 lockd setup
 */
static int nfs_start_lockd(struct nfs_server *server)
{
	int error = 0;

	if (server->nfs_client->cl_nfsversion > 3)
		goto out;
	if (server->flags & NFS_MOUNT_NONLM)
		goto out;
	error = lockd_up((server->flags & NFS_MOUNT_TCP) ?
			IPPROTO_TCP : IPPROTO_UDP);
	if (error < 0)
		server->flags |= NFS_MOUNT_NONLM;
	else
		server->destroy = nfs_destroy_server;
out:
	return error;
}

/*
 * Initialise an NFSv3 ACL client connection
 */
#ifdef CONFIG_NFS_V3_ACL
static void nfs_init_server_aclclient(struct nfs_server *server)
{
	if (server->nfs_client->cl_nfsversion != 3)
		goto out_noacl;
	if (server->flags & NFS_MOUNT_NOACL)
		goto out_noacl;

	server->client_acl = rpc_bind_new_program(server->client, &nfsacl_program, 3);
	if (IS_ERR(server->client_acl))
		goto out_noacl;

	/* No errors! Assume that Sun nfsacls are supported */
	server->caps |= NFS_CAP_ACLS;
	return;

out_noacl:
	server->caps &= ~NFS_CAP_ACLS;
}
#else
static inline void nfs_init_server_aclclient(struct nfs_server *server)
{
	server->flags &= ~NFS_MOUNT_NOACL;
	server->caps &= ~NFS_CAP_ACLS;
}
#endif

/*
 * Create a general RPC client
 */
static int nfs_init_server_rpcclient(struct nfs_server *server, rpc_authflavor_t pseudoflavour)
{
	struct nfs_client *clp = server->nfs_client;

	server->client = rpc_clone_client(clp->cl_rpcclient);
	if (IS_ERR(server->client)) {
		dprintk("%s: couldn't create rpc_client!\n", __FUNCTION__);
		return PTR_ERR(server->client);
	}

	if (pseudoflavour != clp->cl_rpcclient->cl_auth->au_flavor) {
		struct rpc_auth *auth;

		auth = rpcauth_create(pseudoflavour, server->client);
		if (IS_ERR(auth)) {
			dprintk("%s: couldn't create credcache!\n", __FUNCTION__);
			return PTR_ERR(auth);
		}
	}
	server->client->cl_softrtry = 0;
	if (server->flags & NFS_MOUNT_SOFT)
		server->client->cl_softrtry = 1;

	server->client->cl_intr = 0;
	if (server->flags & NFS4_MOUNT_INTR)
		server->client->cl_intr = 1;

	return 0;
}

/*
 * Initialise an NFS2 or NFS3 client
 */
static int nfs_init_client(struct nfs_client *clp, const struct nfs_mount_data *data)
{
	int proto = (data->flags & NFS_MOUNT_TCP) ? IPPROTO_TCP : IPPROTO_UDP;
	int error;

	if (clp->cl_cons_state == NFS_CS_READY) {
		/* the client is already initialised */
		dprintk("<-- nfs_init_client() = 0 [already %p]\n", clp);
		return 0;
	}

	/* Check NFS protocol revision and initialize RPC op vector */
	clp->rpc_ops = &nfs_v2_clientops;
#ifdef CONFIG_NFS_V3
	if (clp->cl_nfsversion == 3)
		clp->rpc_ops = &nfs_v3_clientops;
#endif
	/*
	 * Create a client RPC handle for doing FSSTAT with UNIX auth only
	 * - RFC 2623, sec 2.3.2
	 */
	error = nfs_create_rpc_client(clp, proto, data->timeo, data->retrans,
			RPC_AUTH_UNIX);
	if (error < 0)
		goto error;
	nfs_mark_client_ready(clp, NFS_CS_READY);
	return 0;

error:
	nfs_mark_client_ready(clp, error);
	dprintk("<-- nfs_init_client() = xerror %d\n", error);
	return error;
}

/*
 * Create a version 2 or 3 client
 */
static int nfs_init_server(struct nfs_server *server, const struct nfs_mount_data *data)
{
	struct nfs_client *clp;
	int error, nfsvers = 2;

	dprintk("--> nfs_init_server()\n");

#ifdef CONFIG_NFS_V3
	if (data->flags & NFS_MOUNT_VER3)
		nfsvers = 3;
#endif

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(data->hostname, &data->addr, nfsvers);
	if (IS_ERR(clp)) {
		dprintk("<-- nfs_init_server() = error %ld\n", PTR_ERR(clp));
		return PTR_ERR(clp);
	}

	error = nfs_init_client(clp, data);
	if (error < 0)
		goto error;

	server->nfs_client = clp;

	/* Initialise the client representation from the mount data */
	server->flags = data->flags & NFS_MOUNT_FLAGMASK;

	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);

	server->acregmin = data->acregmin * HZ;
	server->acregmax = data->acregmax * HZ;
	server->acdirmin = data->acdirmin * HZ;
	server->acdirmax = data->acdirmax * HZ;

	/* Start lockd here, before we might error out */
	error = nfs_start_lockd(server);
	if (error < 0)
		goto error;

	error = nfs_init_server_rpcclient(server, data->pseudoflavor);
	if (error < 0)
		goto error;

	server->namelen  = data->namlen;
	/* Create a client RPC handle for the NFSv3 ACL management interface */
	nfs_init_server_aclclient(server);
	if (clp->cl_nfsversion == 3) {
		if (server->namelen == 0 || server->namelen > NFS3_MAXNAMLEN)
			server->namelen = NFS3_MAXNAMLEN;
		server->caps |= NFS_CAP_READDIRPLUS;
	} else {
		if (server->namelen == 0 || server->namelen > NFS2_MAXNAMLEN)
			server->namelen = NFS2_MAXNAMLEN;
	}

	dprintk("<-- nfs_init_server() = 0 [new %p]\n", clp);
	return 0;

error:
	server->nfs_client = NULL;
	nfs_put_client(clp);
	dprintk("<-- nfs_init_server() = xerror %d\n", error);
	return error;
}

/*
 * Load up the server record from information gained in an fsinfo record
 */
static void nfs_server_set_fsinfo(struct nfs_server *server, struct nfs_fsinfo *fsinfo)
{
	unsigned long max_rpc_payload;

	/* Work out a lot of parameters */
	if (server->rsize == 0)
		server->rsize = nfs_block_size(fsinfo->rtpref, NULL);
	if (server->wsize == 0)
		server->wsize = nfs_block_size(fsinfo->wtpref, NULL);

	if (fsinfo->rtmax >= 512 && server->rsize > fsinfo->rtmax)
		server->rsize = nfs_block_size(fsinfo->rtmax, NULL);
	if (fsinfo->wtmax >= 512 && server->wsize > fsinfo->wtmax)
		server->wsize = nfs_block_size(fsinfo->wtmax, NULL);

	max_rpc_payload = nfs_block_size(rpc_max_payload(server->client), NULL);
	if (server->rsize > max_rpc_payload)
		server->rsize = max_rpc_payload;
	if (server->rsize > NFS_MAX_FILE_IO_SIZE)
		server->rsize = NFS_MAX_FILE_IO_SIZE;
	server->rpages = (server->rsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	server->backing_dev_info.ra_pages = server->rpages * NFS_MAX_READAHEAD;

	if (server->wsize > max_rpc_payload)
		server->wsize = max_rpc_payload;
	if (server->wsize > NFS_MAX_FILE_IO_SIZE)
		server->wsize = NFS_MAX_FILE_IO_SIZE;
	server->wpages = (server->wsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	server->wtmult = nfs_block_bits(fsinfo->wtmult, NULL);

	server->dtsize = nfs_block_size(fsinfo->dtpref, NULL);
	if (server->dtsize > PAGE_CACHE_SIZE)
		server->dtsize = PAGE_CACHE_SIZE;
	if (server->dtsize > server->rsize)
		server->dtsize = server->rsize;

	if (server->flags & NFS_MOUNT_NOAC) {
		server->acregmin = server->acregmax = 0;
		server->acdirmin = server->acdirmax = 0;
	}

	server->maxfilesize = fsinfo->maxfilesize;

	/* We're airborne Set socket buffersize */
	rpc_setbufsize(server->client, server->wsize + 100, server->rsize + 100);
}

/*
 * Probe filesystem information, including the FSID on v2/v3
 */
static int nfs_probe_fsinfo(struct nfs_server *server, struct nfs_fh *mntfh, struct nfs_fattr *fattr)
{
	struct nfs_fsinfo fsinfo;
	struct nfs_client *clp = server->nfs_client;
	int error;

	dprintk("--> nfs_probe_fsinfo()\n");

	if (clp->rpc_ops->set_capabilities != NULL) {
		error = clp->rpc_ops->set_capabilities(server, mntfh);
		if (error < 0)
			goto out_error;
	}

	fsinfo.fattr = fattr;
	nfs_fattr_init(fattr);
	error = clp->rpc_ops->fsinfo(server, mntfh, &fsinfo);
	if (error < 0)
		goto out_error;

	nfs_server_set_fsinfo(server, &fsinfo);

	/* Get some general file system info */
	if (server->namelen == 0) {
		struct nfs_pathconf pathinfo;

		pathinfo.fattr = fattr;
		nfs_fattr_init(fattr);

		if (clp->rpc_ops->pathconf(server, mntfh, &pathinfo) >= 0)
			server->namelen = pathinfo.max_namelen;
	}

	dprintk("<-- nfs_probe_fsinfo() = 0\n");
	return 0;

out_error:
	dprintk("nfs_probe_fsinfo: error = %d\n", -error);
	return error;
}

/*
 * Copy useful information when duplicating a server record
 */
static void nfs_server_copy_userdata(struct nfs_server *target, struct nfs_server *source)
{
	target->flags = source->flags;
	target->acregmin = source->acregmin;
	target->acregmax = source->acregmax;
	target->acdirmin = source->acdirmin;
	target->acdirmax = source->acdirmax;
	target->caps = source->caps;
}

/*
 * Allocate and initialise a server record
 */
static struct nfs_server *nfs_alloc_server(void)
{
	struct nfs_server *server;

	server = kzalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return NULL;

	server->client = server->client_acl = ERR_PTR(-EINVAL);

	/* Zero out the NFS state stuff */
	INIT_LIST_HEAD(&server->client_link);
	INIT_LIST_HEAD(&server->master_link);

	server->io_stats = nfs_alloc_iostats();
	if (!server->io_stats) {
		kfree(server);
		return NULL;
	}

	return server;
}

/*
 * Free up a server record
 */
void nfs_free_server(struct nfs_server *server)
{
	dprintk("--> nfs_free_server()\n");

	spin_lock(&nfs_client_lock);
	list_del(&server->client_link);
	list_del(&server->master_link);
	spin_unlock(&nfs_client_lock);

	if (server->destroy != NULL)
		server->destroy(server);
	if (!IS_ERR(server->client))
		rpc_shutdown_client(server->client);

	nfs_put_client(server->nfs_client);

	nfs_free_iostats(server->io_stats);
	kfree(server);
	nfs_release_automount_timer();
	dprintk("<-- nfs_free_server()\n");
}

/*
 * Create a version 2 or 3 volume record
 * - keyed on server and FSID
 */
struct nfs_server *nfs_create_server(const struct nfs_mount_data *data,
				     struct nfs_fh *mntfh)
{
	struct nfs_server *server;
	struct nfs_fattr fattr;
	int error;

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	/* Get a client representation */
	error = nfs_init_server(server, data);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	/* Probe the root fh to retrieve its FSID */
	error = nfs_probe_fsinfo(server, mntfh, &fattr);
	if (error < 0)
		goto error;
	if (!(fattr.valid & NFS_ATTR_FATTR)) {
		error = server->nfs_client->rpc_ops->getattr(server, mntfh, &fattr);
		if (error < 0) {
			dprintk("nfs_create_server: getattr error = %d\n", -error);
			goto error;
		}
	}
	memcpy(&server->fsid, &fattr.fsid, sizeof(server->fsid));

	dprintk("Server FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;
	return server;

error:
	nfs_free_server(server);
	return ERR_PTR(error);
}

#ifdef CONFIG_NFS_V4
/*
 * Initialise an NFS4 client record
 */
static int nfs4_init_client(struct nfs_client *clp,
		int proto, int timeo, int retrans,
		const char *ip_addr,
		rpc_authflavor_t authflavour)
{
	int error;

	if (clp->cl_cons_state == NFS_CS_READY) {
		/* the client is initialised already */
		dprintk("<-- nfs4_init_client() = 0 [already %p]\n", clp);
		return 0;
	}

	/* Check NFS protocol revision and initialize RPC op vector */
	clp->rpc_ops = &nfs_v4_clientops;

	error = nfs_create_rpc_client(clp, proto, timeo, retrans, authflavour);
	if (error < 0)
		goto error;
	memcpy(clp->cl_ipaddr, ip_addr, sizeof(clp->cl_ipaddr));

	error = nfs_idmap_new(clp);
	if (error < 0) {
		dprintk("%s: failed to create idmapper. Error = %d\n",
			__FUNCTION__, error);
		goto error;
	}
	__set_bit(NFS_CS_IDMAP, &clp->cl_res_state);

	nfs_mark_client_ready(clp, NFS_CS_READY);
	return 0;

error:
	nfs_mark_client_ready(clp, error);
	dprintk("<-- nfs4_init_client() = xerror %d\n", error);
	return error;
}

/*
 * Set up an NFS4 client
 */
static int nfs4_set_client(struct nfs_server *server,
		const char *hostname, const struct sockaddr_in *addr,
		const char *ip_addr,
		rpc_authflavor_t authflavour,
		int proto, int timeo, int retrans)
{
	struct nfs_client *clp;
	int error;

	dprintk("--> nfs4_set_client()\n");

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(hostname, addr, 4);
	if (IS_ERR(clp)) {
		error = PTR_ERR(clp);
		goto error;
	}
	error = nfs4_init_client(clp, proto, timeo, retrans, ip_addr, authflavour);
	if (error < 0)
		goto error_put;

	server->nfs_client = clp;
	dprintk("<-- nfs4_set_client() = 0 [new %p]\n", clp);
	return 0;

error_put:
	nfs_put_client(clp);
error:
	dprintk("<-- nfs4_set_client() = xerror %d\n", error);
	return error;
}

/*
 * Create a version 4 volume record
 */
static int nfs4_init_server(struct nfs_server *server,
		const struct nfs4_mount_data *data, rpc_authflavor_t authflavour)
{
	int error;

	dprintk("--> nfs4_init_server()\n");

	/* Initialise the client representation from the mount data */
	server->flags = data->flags & NFS_MOUNT_FLAGMASK;
	server->caps |= NFS_CAP_ATOMIC_OPEN;

	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);

	server->acregmin = data->acregmin * HZ;
	server->acregmax = data->acregmax * HZ;
	server->acdirmin = data->acdirmin * HZ;
	server->acdirmax = data->acdirmax * HZ;

	error = nfs_init_server_rpcclient(server, authflavour);

	/* Done */
	dprintk("<-- nfs4_init_server() = %d\n", error);
	return error;
}

/*
 * Create a version 4 volume record
 * - keyed on server and FSID
 */
struct nfs_server *nfs4_create_server(const struct nfs4_mount_data *data,
				      const char *hostname,
				      const struct sockaddr_in *addr,
				      const char *mntpath,
				      const char *ip_addr,
				      rpc_authflavor_t authflavour,
				      struct nfs_fh *mntfh)
{
	struct nfs_fattr fattr;
	struct nfs_server *server;
	int error;

	dprintk("--> nfs4_create_server()\n");

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	/* Get a client record */
	error = nfs4_set_client(server, hostname, addr, ip_addr, authflavour,
			data->proto, data->timeo, data->retrans);
	if (error < 0)
		goto error;

	/* set up the general RPC client */
	error = nfs4_init_server(server, data, authflavour);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	/* Probe the root fh to retrieve its FSID */
	error = nfs4_path_walk(server, mntfh, mntpath);
	if (error < 0)
		goto error;

	dprintk("Server FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);
	dprintk("Mount FH: %d\n", mntfh->size);

	error = nfs_probe_fsinfo(server, mntfh, &fattr);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;
	dprintk("<-- nfs4_create_server() = %p\n", server);
	return server;

error:
	nfs_free_server(server);
	dprintk("<-- nfs4_create_server() = error %d\n", error);
	return ERR_PTR(error);
}

/*
 * Create an NFS4 referral server record
 */
struct nfs_server *nfs4_create_referral_server(struct nfs_clone_mount *data,
					       struct nfs_fh *fh)
{
	struct nfs_client *parent_client;
	struct nfs_server *server, *parent_server;
	struct nfs_fattr fattr;
	int error;

	dprintk("--> nfs4_create_referral_server()\n");

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	parent_server = NFS_SB(data->sb);
	parent_client = parent_server->nfs_client;

	/* Get a client representation.
	 * Note: NFSv4 always uses TCP, */
	error = nfs4_set_client(server, data->hostname, data->addr,
			parent_client->cl_ipaddr,
			data->authflavor,
			parent_server->client->cl_xprt->prot,
			parent_client->retrans_timeo,
			parent_client->retrans_count);
	if (error < 0)
		goto error;

	/* Initialise the client representation from the parent server */
	nfs_server_copy_userdata(server, parent_server);
	server->caps |= NFS_CAP_ATOMIC_OPEN;

	error = nfs_init_server_rpcclient(server, data->authflavor);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	/* probe the filesystem info for this server filesystem */
	error = nfs_probe_fsinfo(server, fh, &fattr);
	if (error < 0)
		goto error;

	dprintk("Referral FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;

	dprintk("<-- nfs_create_referral_server() = %p\n", server);
	return server;

error:
	nfs_free_server(server);
	dprintk("<-- nfs4_create_referral_server() = error %d\n", error);
	return ERR_PTR(error);
}

#endif /* CONFIG_NFS_V4 */

/*
 * Clone an NFS2, NFS3 or NFS4 server record
 */
struct nfs_server *nfs_clone_server(struct nfs_server *source,
				    struct nfs_fh *fh,
				    struct nfs_fattr *fattr)
{
	struct nfs_server *server;
	struct nfs_fattr fattr_fsinfo;
	int error;

	dprintk("--> nfs_clone_server(,%llx:%llx,)\n",
		(unsigned long long) fattr->fsid.major,
		(unsigned long long) fattr->fsid.minor);

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	/* Copy data from the source */
	server->nfs_client = source->nfs_client;
	atomic_inc(&server->nfs_client->cl_count);
	nfs_server_copy_userdata(server, source);

	server->fsid = fattr->fsid;

	error = nfs_init_server_rpcclient(server, source->client->cl_auth->au_flavor);
	if (error < 0)
		goto out_free_server;
	if (!IS_ERR(source->client_acl))
		nfs_init_server_aclclient(server);

	/* probe the filesystem info for this server filesystem */
	error = nfs_probe_fsinfo(server, fh, &fattr_fsinfo);
	if (error < 0)
		goto out_free_server;

	dprintk("Cloned FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	error = nfs_start_lockd(server);
	if (error < 0)
		goto out_free_server;

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;

	dprintk("<-- nfs_clone_server() = %p\n", server);
	return server;

out_free_server:
	nfs_free_server(server);
	dprintk("<-- nfs_clone_server() = error %d\n", error);
	return ERR_PTR(error);
}

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_fs_nfs;

static int nfs_server_list_open(struct inode *inode, struct file *file);
static void *nfs_server_list_start(struct seq_file *p, loff_t *pos);
static void *nfs_server_list_next(struct seq_file *p, void *v, loff_t *pos);
static void nfs_server_list_stop(struct seq_file *p, void *v);
static int nfs_server_list_show(struct seq_file *m, void *v);

static struct seq_operations nfs_server_list_ops = {
	.start	= nfs_server_list_start,
	.next	= nfs_server_list_next,
	.stop	= nfs_server_list_stop,
	.show	= nfs_server_list_show,
};

static struct file_operations nfs_server_list_fops = {
	.open		= nfs_server_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int nfs_volume_list_open(struct inode *inode, struct file *file);
static void *nfs_volume_list_start(struct seq_file *p, loff_t *pos);
static void *nfs_volume_list_next(struct seq_file *p, void *v, loff_t *pos);
static void nfs_volume_list_stop(struct seq_file *p, void *v);
static int nfs_volume_list_show(struct seq_file *m, void *v);

static struct seq_operations nfs_volume_list_ops = {
	.start	= nfs_volume_list_start,
	.next	= nfs_volume_list_next,
	.stop	= nfs_volume_list_stop,
	.show	= nfs_volume_list_show,
};

static struct file_operations nfs_volume_list_fops = {
	.open		= nfs_volume_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * open "/proc/fs/nfsfs/servers" which provides a summary of servers with which
 * we're dealing
 */
static int nfs_server_list_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &nfs_server_list_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
}

/*
 * set up the iterator to start reading from the server list and return the first item
 */
static void *nfs_server_list_start(struct seq_file *m, loff_t *_pos)
{
	struct list_head *_p;
	loff_t pos = *_pos;

	/* lock the list against modification */
	spin_lock(&nfs_client_lock);

	/* allow for the header line */
	if (!pos)
		return SEQ_START_TOKEN;
	pos--;

	/* find the n'th element in the list */
	list_for_each(_p, &nfs_client_list)
		if (!pos--)
			break;

	return _p != &nfs_client_list ? _p : NULL;
}

/*
 * move to next server
 */
static void *nfs_server_list_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct list_head *_p;

	(*pos)++;

	_p = v;
	_p = (v == SEQ_START_TOKEN) ? nfs_client_list.next : _p->next;

	return _p != &nfs_client_list ? _p : NULL;
}

/*
 * clean up after reading from the transports list
 */
static void nfs_server_list_stop(struct seq_file *p, void *v)
{
	spin_unlock(&nfs_client_lock);
}

/*
 * display a header line followed by a load of call lines
 */
static int nfs_server_list_show(struct seq_file *m, void *v)
{
	struct nfs_client *clp;

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "NV SERVER   PORT USE HOSTNAME\n");
		return 0;
	}

	/* display one transport per line on subsequent lines */
	clp = list_entry(v, struct nfs_client, cl_share_link);

	seq_printf(m, "v%d %02x%02x%02x%02x %4hx %3d %s\n",
		   clp->cl_nfsversion,
		   NIPQUAD(clp->cl_addr.sin_addr),
		   ntohs(clp->cl_addr.sin_port),
		   atomic_read(&clp->cl_count),
		   clp->cl_hostname);

	return 0;
}

/*
 * open "/proc/fs/nfsfs/volumes" which provides a summary of extant volumes
 */
static int nfs_volume_list_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &nfs_volume_list_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
}

/*
 * set up the iterator to start reading from the volume list and return the first item
 */
static void *nfs_volume_list_start(struct seq_file *m, loff_t *_pos)
{
	struct list_head *_p;
	loff_t pos = *_pos;

	/* lock the list against modification */
	spin_lock(&nfs_client_lock);

	/* allow for the header line */
	if (!pos)
		return SEQ_START_TOKEN;
	pos--;

	/* find the n'th element in the list */
	list_for_each(_p, &nfs_volume_list)
		if (!pos--)
			break;

	return _p != &nfs_volume_list ? _p : NULL;
}

/*
 * move to next volume
 */
static void *nfs_volume_list_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct list_head *_p;

	(*pos)++;

	_p = v;
	_p = (v == SEQ_START_TOKEN) ? nfs_volume_list.next : _p->next;

	return _p != &nfs_volume_list ? _p : NULL;
}

/*
 * clean up after reading from the transports list
 */
static void nfs_volume_list_stop(struct seq_file *p, void *v)
{
	spin_unlock(&nfs_client_lock);
}

/*
 * display a header line followed by a load of call lines
 */
static int nfs_volume_list_show(struct seq_file *m, void *v)
{
	struct nfs_server *server;
	struct nfs_client *clp;
	char dev[8], fsid[17];

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "NV SERVER   PORT DEV     FSID\n");
		return 0;
	}
	/* display one transport per line on subsequent lines */
	server = list_entry(v, struct nfs_server, master_link);
	clp = server->nfs_client;

	snprintf(dev, 8, "%u:%u",
		 MAJOR(server->s_dev), MINOR(server->s_dev));

	snprintf(fsid, 17, "%llx:%llx",
		 (unsigned long long) server->fsid.major,
		 (unsigned long long) server->fsid.minor);

	seq_printf(m, "v%d %02x%02x%02x%02x %4hx %-7s %-17s\n",
		   clp->cl_nfsversion,
		   NIPQUAD(clp->cl_addr.sin_addr),
		   ntohs(clp->cl_addr.sin_port),
		   dev,
		   fsid);

	return 0;
}

/*
 * initialise the /proc/fs/nfsfs/ directory
 */
int __init nfs_fs_proc_init(void)
{
	struct proc_dir_entry *p;

	proc_fs_nfs = proc_mkdir("nfsfs", proc_root_fs);
	if (!proc_fs_nfs)
		goto error_0;

	proc_fs_nfs->owner = THIS_MODULE;

	/* a file of servers with which we're dealing */
	p = create_proc_entry("servers", S_IFREG|S_IRUGO, proc_fs_nfs);
	if (!p)
		goto error_1;

	p->proc_fops = &nfs_server_list_fops;
	p->owner = THIS_MODULE;

	/* a file of volumes that we have mounted */
	p = create_proc_entry("volumes", S_IFREG|S_IRUGO, proc_fs_nfs);
	if (!p)
		goto error_2;

	p->proc_fops = &nfs_volume_list_fops;
	p->owner = THIS_MODULE;
	return 0;

error_2:
	remove_proc_entry("servers", proc_fs_nfs);
error_1:
	remove_proc_entry("nfsfs", proc_root_fs);
error_0:
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/nfsfs/ directory
 */
void nfs_fs_proc_exit(void)
{
	remove_proc_entry("volumes", proc_fs_nfs);
	remove_proc_entry("servers", proc_fs_nfs);
	remove_proc_entry("nfsfs", proc_root_fs);
}

#endif /* CONFIG_PROC_FS */
