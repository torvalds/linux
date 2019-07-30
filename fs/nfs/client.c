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
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/xprtrdma.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <net/ipv6.h>
#include <linux/nfs_xdr.h>
#include <linux/sunrpc/bc_xprt.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>


#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"
#include "pnfs.h"
#include "nfs.h"
#include "netns.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

static DECLARE_WAIT_QUEUE_HEAD(nfs_client_active_wq);
static DEFINE_SPINLOCK(nfs_version_lock);
static DEFINE_MUTEX(nfs_version_mutex);
static LIST_HEAD(nfs_versions);

/*
 * RPC cruft for NFS
 */
static const struct rpc_version *nfs_version[5] = {
	[2] = NULL,
	[3] = NULL,
	[4] = NULL,
};

const struct rpc_program nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfs_version),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
	.pipe_dir_name		= NFS_PIPE_DIRNAME,
};

struct rpc_stat nfs_rpcstat = {
	.program		= &nfs_program
};

static struct nfs_subversion *find_nfs_version(unsigned int version)
{
	struct nfs_subversion *nfs;
	spin_lock(&nfs_version_lock);

	list_for_each_entry(nfs, &nfs_versions, list) {
		if (nfs->rpc_ops->version == version) {
			spin_unlock(&nfs_version_lock);
			return nfs;
		}
	}

	spin_unlock(&nfs_version_lock);
	return ERR_PTR(-EPROTONOSUPPORT);
}

struct nfs_subversion *get_nfs_version(unsigned int version)
{
	struct nfs_subversion *nfs = find_nfs_version(version);

	if (IS_ERR(nfs)) {
		mutex_lock(&nfs_version_mutex);
		request_module("nfsv%d", version);
		nfs = find_nfs_version(version);
		mutex_unlock(&nfs_version_mutex);
	}

	if (!IS_ERR(nfs) && !try_module_get(nfs->owner))
		return ERR_PTR(-EAGAIN);
	return nfs;
}

void put_nfs_version(struct nfs_subversion *nfs)
{
	module_put(nfs->owner);
}

void register_nfs_version(struct nfs_subversion *nfs)
{
	spin_lock(&nfs_version_lock);

	list_add(&nfs->list, &nfs_versions);
	nfs_version[nfs->rpc_ops->version] = nfs->rpc_vers;

	spin_unlock(&nfs_version_lock);
}
EXPORT_SYMBOL_GPL(register_nfs_version);

void unregister_nfs_version(struct nfs_subversion *nfs)
{
	spin_lock(&nfs_version_lock);

	nfs_version[nfs->rpc_ops->version] = NULL;
	list_del(&nfs->list);

	spin_unlock(&nfs_version_lock);
}
EXPORT_SYMBOL_GPL(unregister_nfs_version);

/*
 * Allocate a shared client record
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
struct nfs_client *nfs_alloc_client(const struct nfs_client_initdata *cl_init)
{
	struct nfs_client *clp;
	int err = -ENOMEM;

	if ((clp = kzalloc(sizeof(*clp), GFP_KERNEL)) == NULL)
		goto error_0;

	clp->cl_nfs_mod = cl_init->nfs_mod;
	if (!try_module_get(clp->cl_nfs_mod->owner))
		goto error_dealloc;

	clp->rpc_ops = clp->cl_nfs_mod->rpc_ops;

	refcount_set(&clp->cl_count, 1);
	clp->cl_cons_state = NFS_CS_INITING;

	memcpy(&clp->cl_addr, cl_init->addr, cl_init->addrlen);
	clp->cl_addrlen = cl_init->addrlen;

	if (cl_init->hostname) {
		err = -ENOMEM;
		clp->cl_hostname = kstrdup(cl_init->hostname, GFP_KERNEL);
		if (!clp->cl_hostname)
			goto error_cleanup;
	}

	INIT_LIST_HEAD(&clp->cl_superblocks);
	clp->cl_rpcclient = ERR_PTR(-EINVAL);

	clp->cl_proto = cl_init->proto;
	clp->cl_net = get_net(cl_init->net);

	clp->cl_principal = "*";
	nfs_fscache_get_client_cookie(clp);

	return clp;

error_cleanup:
	put_nfs_version(clp->cl_nfs_mod);
error_dealloc:
	kfree(clp);
error_0:
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(nfs_alloc_client);

#if IS_ENABLED(CONFIG_NFS_V4)
void nfs_cleanup_cb_ident_idr(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	idr_destroy(&nn->cb_ident_idr);
}

/* nfs_client_lock held */
static void nfs_cb_idr_remove_locked(struct nfs_client *clp)
{
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);

	if (clp->cl_cb_ident)
		idr_remove(&nn->cb_ident_idr, clp->cl_cb_ident);
}

static void pnfs_init_server(struct nfs_server *server)
{
	rpc_init_wait_queue(&server->roc_rpcwaitq, "pNFS ROC");
}

#else
void nfs_cleanup_cb_ident_idr(struct net *net)
{
}

static void nfs_cb_idr_remove_locked(struct nfs_client *clp)
{
}

static void pnfs_init_server(struct nfs_server *server)
{
}

#endif /* CONFIG_NFS_V4 */

/*
 * Destroy a shared client record
 */
void nfs_free_client(struct nfs_client *clp)
{
	nfs_fscache_release_client_cookie(clp);

	/* -EIO all pending I/O */
	if (!IS_ERR(clp->cl_rpcclient))
		rpc_shutdown_client(clp->cl_rpcclient);

	put_net(clp->cl_net);
	put_nfs_version(clp->cl_nfs_mod);
	kfree(clp->cl_hostname);
	kfree(clp->cl_acceptor);
	kfree(clp);
}
EXPORT_SYMBOL_GPL(nfs_free_client);

/*
 * Release a reference to a shared client record
 */
void nfs_put_client(struct nfs_client *clp)
{
	struct nfs_net *nn;

	if (!clp)
		return;

	nn = net_generic(clp->cl_net, nfs_net_id);

	if (refcount_dec_and_lock(&clp->cl_count, &nn->nfs_client_lock)) {
		list_del(&clp->cl_share_link);
		nfs_cb_idr_remove_locked(clp);
		spin_unlock(&nn->nfs_client_lock);

		WARN_ON_ONCE(!list_empty(&clp->cl_superblocks));

		clp->rpc_ops->free_client(clp);
	}
}
EXPORT_SYMBOL_GPL(nfs_put_client);

/*
 * Find an nfs_client on the list that matches the initialisation data
 * that is supplied.
 */
static struct nfs_client *nfs_match_client(const struct nfs_client_initdata *data)
{
	struct nfs_client *clp;
	const struct sockaddr *sap = data->addr;
	struct nfs_net *nn = net_generic(data->net, nfs_net_id);

again:
	list_for_each_entry(clp, &nn->nfs_client_list, cl_share_link) {
	        const struct sockaddr *clap = (struct sockaddr *)&clp->cl_addr;
		/* Don't match clients that failed to initialise properly */
		if (clp->cl_cons_state < 0)
			continue;

		/* If a client is still initializing then we need to wait */
		if (clp->cl_cons_state > NFS_CS_READY) {
			refcount_inc(&clp->cl_count);
			spin_unlock(&nn->nfs_client_lock);
			nfs_wait_client_init_complete(clp);
			nfs_put_client(clp);
			spin_lock(&nn->nfs_client_lock);
			goto again;
		}

		/* Different NFS versions cannot share the same nfs_client */
		if (clp->rpc_ops != data->nfs_mod->rpc_ops)
			continue;

		if (clp->cl_proto != data->proto)
			continue;
		/* Match nfsv4 minorversion */
		if (clp->cl_minorversion != data->minorversion)
			continue;
		/* Match the full socket address */
		if (!rpc_cmp_addr_port(sap, clap))
			/* Match all xprt_switch full socket addresses */
			if (IS_ERR(clp->cl_rpcclient) ||
                            !rpc_clnt_xprt_switch_has_addr(clp->cl_rpcclient,
							   sap))
				continue;

		refcount_inc(&clp->cl_count);
		return clp;
	}
	return NULL;
}

/*
 * Return true if @clp is done initializing, false if still working on it.
 *
 * Use nfs_client_init_status to check if it was successful.
 */
bool nfs_client_init_is_complete(const struct nfs_client *clp)
{
	return clp->cl_cons_state <= NFS_CS_READY;
}
EXPORT_SYMBOL_GPL(nfs_client_init_is_complete);

/*
 * Return 0 if @clp was successfully initialized, -errno otherwise.
 *
 * This must be called *after* nfs_client_init_is_complete() returns true,
 * otherwise it will pop WARN_ON_ONCE and return -EINVAL
 */
int nfs_client_init_status(const struct nfs_client *clp)
{
	/* called without checking nfs_client_init_is_complete */
	if (clp->cl_cons_state > NFS_CS_READY) {
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
	return clp->cl_cons_state;
}
EXPORT_SYMBOL_GPL(nfs_client_init_status);

int nfs_wait_client_init_complete(const struct nfs_client *clp)
{
	return wait_event_killable(nfs_client_active_wq,
			nfs_client_init_is_complete(clp));
}
EXPORT_SYMBOL_GPL(nfs_wait_client_init_complete);

/*
 * Found an existing client.  Make sure it's ready before returning.
 */
static struct nfs_client *
nfs_found_client(const struct nfs_client_initdata *cl_init,
		 struct nfs_client *clp)
{
	int error;

	error = nfs_wait_client_init_complete(clp);
	if (error < 0) {
		nfs_put_client(clp);
		return ERR_PTR(-ERESTARTSYS);
	}

	if (clp->cl_cons_state < NFS_CS_READY) {
		error = clp->cl_cons_state;
		nfs_put_client(clp);
		return ERR_PTR(error);
	}

	smp_rmb();
	return clp;
}

/*
 * Look up a client by IP address and protocol version
 * - creates a new record if one doesn't yet exist
 */
struct nfs_client *nfs_get_client(const struct nfs_client_initdata *cl_init)
{
	struct nfs_client *clp, *new = NULL;
	struct nfs_net *nn = net_generic(cl_init->net, nfs_net_id);
	const struct nfs_rpc_ops *rpc_ops = cl_init->nfs_mod->rpc_ops;

	if (cl_init->hostname == NULL) {
		WARN_ON(1);
		return NULL;
	}

	/* see if the client already exists */
	do {
		spin_lock(&nn->nfs_client_lock);

		clp = nfs_match_client(cl_init);
		if (clp) {
			spin_unlock(&nn->nfs_client_lock);
			if (new)
				new->rpc_ops->free_client(new);
			return nfs_found_client(cl_init, clp);
		}
		if (new) {
			list_add_tail(&new->cl_share_link,
					&nn->nfs_client_list);
			spin_unlock(&nn->nfs_client_lock);
			new->cl_flags = cl_init->init_flags;
			return rpc_ops->init_client(new, cl_init);
		}

		spin_unlock(&nn->nfs_client_lock);

		new = rpc_ops->alloc_client(cl_init);
	} while (!IS_ERR(new));

	return new;
}
EXPORT_SYMBOL_GPL(nfs_get_client);

/*
 * Mark a server as ready or failed
 */
void nfs_mark_client_ready(struct nfs_client *clp, int state)
{
	smp_wmb();
	clp->cl_cons_state = state;
	wake_up_all(&nfs_client_active_wq);
}
EXPORT_SYMBOL_GPL(nfs_mark_client_ready);

/*
 * Initialise the timeout values for a connection
 */
void nfs_init_timeout_values(struct rpc_timeout *to, int proto,
				    int timeo, int retrans)
{
	to->to_initval = timeo * HZ / 10;
	to->to_retries = retrans;

	switch (proto) {
	case XPRT_TRANSPORT_TCP:
	case XPRT_TRANSPORT_RDMA:
		if (retrans == NFS_UNSPEC_RETRANS)
			to->to_retries = NFS_DEF_TCP_RETRANS;
		if (timeo == NFS_UNSPEC_TIMEO || to->to_initval == 0)
			to->to_initval = NFS_DEF_TCP_TIMEO * HZ / 10;
		if (to->to_initval > NFS_MAX_TCP_TIMEOUT)
			to->to_initval = NFS_MAX_TCP_TIMEOUT;
		to->to_increment = to->to_initval;
		to->to_maxval = to->to_initval + (to->to_increment * to->to_retries);
		if (to->to_maxval > NFS_MAX_TCP_TIMEOUT)
			to->to_maxval = NFS_MAX_TCP_TIMEOUT;
		if (to->to_maxval < to->to_initval)
			to->to_maxval = to->to_initval;
		to->to_exponential = 0;
		break;
	case XPRT_TRANSPORT_UDP:
		if (retrans == NFS_UNSPEC_RETRANS)
			to->to_retries = NFS_DEF_UDP_RETRANS;
		if (timeo == NFS_UNSPEC_TIMEO || to->to_initval == 0)
			to->to_initval = NFS_DEF_UDP_TIMEO * HZ / 10;
		if (to->to_initval > NFS_MAX_UDP_TIMEOUT)
			to->to_initval = NFS_MAX_UDP_TIMEOUT;
		to->to_maxval = NFS_MAX_UDP_TIMEOUT;
		to->to_exponential = 1;
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL_GPL(nfs_init_timeout_values);

/*
 * Create an RPC client handle
 */
int nfs_create_rpc_client(struct nfs_client *clp,
			  const struct nfs_client_initdata *cl_init,
			  rpc_authflavor_t flavor)
{
	struct rpc_clnt		*clnt = NULL;
	struct rpc_create_args args = {
		.net		= clp->cl_net,
		.protocol	= clp->cl_proto,
		.address	= (struct sockaddr *)&clp->cl_addr,
		.addrsize	= clp->cl_addrlen,
		.timeout	= cl_init->timeparms,
		.servername	= clp->cl_hostname,
		.nodename	= cl_init->nodename,
		.program	= &nfs_program,
		.version	= clp->rpc_ops->version,
		.authflavor	= flavor,
	};

	if (test_bit(NFS_CS_DISCRTRY, &clp->cl_flags))
		args.flags |= RPC_CLNT_CREATE_DISCRTRY;
	if (test_bit(NFS_CS_NO_RETRANS_TIMEOUT, &clp->cl_flags))
		args.flags |= RPC_CLNT_CREATE_NO_RETRANS_TIMEOUT;
	if (test_bit(NFS_CS_NORESVPORT, &clp->cl_flags))
		args.flags |= RPC_CLNT_CREATE_NONPRIVPORT;
	if (test_bit(NFS_CS_INFINITE_SLOTS, &clp->cl_flags))
		args.flags |= RPC_CLNT_CREATE_INFINITE_SLOTS;

	if (!IS_ERR(clp->cl_rpcclient))
		return 0;

	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		dprintk("%s: cannot create RPC client. Error = %ld\n",
				__func__, PTR_ERR(clnt));
		return PTR_ERR(clnt);
	}

	clnt->cl_principal = clp->cl_principal;
	clp->cl_rpcclient = clnt;
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_create_rpc_client);

/*
 * Version 2 or 3 client destruction
 */
static void nfs_destroy_server(struct nfs_server *server)
{
	if (server->nlm_host)
		nlmclnt_done(server->nlm_host);
}

/*
 * Version 2 or 3 lockd setup
 */
static int nfs_start_lockd(struct nfs_server *server)
{
	struct nlm_host *host;
	struct nfs_client *clp = server->nfs_client;
	struct nlmclnt_initdata nlm_init = {
		.hostname	= clp->cl_hostname,
		.address	= (struct sockaddr *)&clp->cl_addr,
		.addrlen	= clp->cl_addrlen,
		.nfs_version	= clp->rpc_ops->version,
		.noresvport	= server->flags & NFS_MOUNT_NORESVPORT ?
					1 : 0,
		.net		= clp->cl_net,
		.nlmclnt_ops 	= clp->cl_nfs_mod->rpc_ops->nlmclnt_ops,
	};

	if (nlm_init.nfs_version > 3)
		return 0;
	if ((server->flags & NFS_MOUNT_LOCAL_FLOCK) &&
			(server->flags & NFS_MOUNT_LOCAL_FCNTL))
		return 0;

	switch (clp->cl_proto) {
		default:
			nlm_init.protocol = IPPROTO_TCP;
			break;
		case XPRT_TRANSPORT_UDP:
			nlm_init.protocol = IPPROTO_UDP;
	}

	host = nlmclnt_init(&nlm_init);
	if (IS_ERR(host))
		return PTR_ERR(host);

	server->nlm_host = host;
	server->destroy = nfs_destroy_server;
	return 0;
}

/*
 * Create a general RPC client
 */
int nfs_init_server_rpcclient(struct nfs_server *server,
		const struct rpc_timeout *timeo,
		rpc_authflavor_t pseudoflavour)
{
	struct nfs_client *clp = server->nfs_client;

	server->client = rpc_clone_client_set_auth(clp->cl_rpcclient,
							pseudoflavour);
	if (IS_ERR(server->client)) {
		dprintk("%s: couldn't create rpc_client!\n", __func__);
		return PTR_ERR(server->client);
	}

	memcpy(&server->client->cl_timeout_default,
			timeo,
			sizeof(server->client->cl_timeout_default));
	server->client->cl_timeout = &server->client->cl_timeout_default;
	server->client->cl_softrtry = 0;
	if (server->flags & NFS_MOUNT_SOFT)
		server->client->cl_softrtry = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(nfs_init_server_rpcclient);

/**
 * nfs_init_client - Initialise an NFS2 or NFS3 client
 *
 * @clp: nfs_client to initialise
 * @cl_init: Initialisation parameters
 *
 * Returns pointer to an NFS client, or an ERR_PTR value.
 */
struct nfs_client *nfs_init_client(struct nfs_client *clp,
				   const struct nfs_client_initdata *cl_init)
{
	int error;

	/* the client is already initialised */
	if (clp->cl_cons_state == NFS_CS_READY)
		return clp;

	/*
	 * Create a client RPC handle for doing FSSTAT with UNIX auth only
	 * - RFC 2623, sec 2.3.2
	 */
	error = nfs_create_rpc_client(clp, cl_init, RPC_AUTH_UNIX);
	nfs_mark_client_ready(clp, error == 0 ? NFS_CS_READY : error);
	if (error < 0) {
		nfs_put_client(clp);
		clp = ERR_PTR(error);
	}
	return clp;
}
EXPORT_SYMBOL_GPL(nfs_init_client);

/*
 * Create a version 2 or 3 client
 */
static int nfs_init_server(struct nfs_server *server,
			   const struct nfs_parsed_mount_data *data,
			   struct nfs_subversion *nfs_mod)
{
	struct rpc_timeout timeparms;
	struct nfs_client_initdata cl_init = {
		.hostname = data->nfs_server.hostname,
		.addr = (const struct sockaddr *)&data->nfs_server.address,
		.addrlen = data->nfs_server.addrlen,
		.nfs_mod = nfs_mod,
		.proto = data->nfs_server.protocol,
		.net = data->net,
		.timeparms = &timeparms,
	};
	struct nfs_client *clp;
	int error;

	nfs_init_timeout_values(&timeparms, data->nfs_server.protocol,
			data->timeo, data->retrans);
	if (data->flags & NFS_MOUNT_NORESVPORT)
		set_bit(NFS_CS_NORESVPORT, &cl_init.init_flags);

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(&cl_init);
	if (IS_ERR(clp))
		return PTR_ERR(clp);

	server->nfs_client = clp;

	/* Initialise the client representation from the mount data */
	server->flags = data->flags;
	server->options = data->options;
	server->caps |= NFS_CAP_HARDLINKS|NFS_CAP_SYMLINKS|NFS_CAP_FILEID|
		NFS_CAP_MODE|NFS_CAP_NLINK|NFS_CAP_OWNER|NFS_CAP_OWNER_GROUP|
		NFS_CAP_ATIME|NFS_CAP_CTIME|NFS_CAP_MTIME;

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

	server->port = data->nfs_server.port;
	server->auth_info = data->auth_info;

	error = nfs_init_server_rpcclient(server, &timeparms,
					  data->selected_flavor);
	if (error < 0)
		goto error;

	/* Preserve the values of mount_server-related mount options */
	if (data->mount_server.addrlen) {
		memcpy(&server->mountd_address, &data->mount_server.address,
			data->mount_server.addrlen);
		server->mountd_addrlen = data->mount_server.addrlen;
	}
	server->mountd_version = data->mount_server.version;
	server->mountd_port = data->mount_server.port;
	server->mountd_protocol = data->mount_server.protocol;

	server->namelen  = data->namlen;
	return 0;

error:
	server->nfs_client = NULL;
	nfs_put_client(clp);
	return error;
}

/*
 * Load up the server record from information gained in an fsinfo record
 */
static void nfs_server_set_fsinfo(struct nfs_server *server,
				  struct nfs_fsinfo *fsinfo)
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
	server->rpages = (server->rsize + PAGE_SIZE - 1) >> PAGE_SHIFT;

	if (server->wsize > max_rpc_payload)
		server->wsize = max_rpc_payload;
	if (server->wsize > NFS_MAX_FILE_IO_SIZE)
		server->wsize = NFS_MAX_FILE_IO_SIZE;
	server->wpages = (server->wsize + PAGE_SIZE - 1) >> PAGE_SHIFT;

	server->wtmult = nfs_block_bits(fsinfo->wtmult, NULL);

	server->dtsize = nfs_block_size(fsinfo->dtpref, NULL);
	if (server->dtsize > PAGE_SIZE * NFS_MAX_READDIR_PAGES)
		server->dtsize = PAGE_SIZE * NFS_MAX_READDIR_PAGES;
	if (server->dtsize > server->rsize)
		server->dtsize = server->rsize;

	if (server->flags & NFS_MOUNT_NOAC) {
		server->acregmin = server->acregmax = 0;
		server->acdirmin = server->acdirmax = 0;
	}

	server->maxfilesize = fsinfo->maxfilesize;

	server->time_delta = fsinfo->time_delta;

	server->clone_blksize = fsinfo->clone_blksize;
	/* We're airborne Set socket buffersize */
	rpc_setbufsize(server->client, server->wsize + 100, server->rsize + 100);
}

/*
 * Probe filesystem information, including the FSID on v2/v3
 */
int nfs_probe_fsinfo(struct nfs_server *server, struct nfs_fh *mntfh, struct nfs_fattr *fattr)
{
	struct nfs_fsinfo fsinfo;
	struct nfs_client *clp = server->nfs_client;
	int error;

	if (clp->rpc_ops->set_capabilities != NULL) {
		error = clp->rpc_ops->set_capabilities(server, mntfh);
		if (error < 0)
			return error;
	}

	fsinfo.fattr = fattr;
	fsinfo.nlayouttypes = 0;
	memset(fsinfo.layouttype, 0, sizeof(fsinfo.layouttype));
	error = clp->rpc_ops->fsinfo(server, mntfh, &fsinfo);
	if (error < 0)
		return error;

	nfs_server_set_fsinfo(server, &fsinfo);

	/* Get some general file system info */
	if (server->namelen == 0) {
		struct nfs_pathconf pathinfo;

		pathinfo.fattr = fattr;
		nfs_fattr_init(fattr);

		if (clp->rpc_ops->pathconf(server, mntfh, &pathinfo) >= 0)
			server->namelen = pathinfo.max_namelen;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nfs_probe_fsinfo);

/*
 * Copy useful information when duplicating a server record
 */
void nfs_server_copy_userdata(struct nfs_server *target, struct nfs_server *source)
{
	target->flags = source->flags;
	target->rsize = source->rsize;
	target->wsize = source->wsize;
	target->acregmin = source->acregmin;
	target->acregmax = source->acregmax;
	target->acdirmin = source->acdirmin;
	target->acdirmax = source->acdirmax;
	target->caps = source->caps;
	target->options = source->options;
	target->auth_info = source->auth_info;
	target->port = source->port;
}
EXPORT_SYMBOL_GPL(nfs_server_copy_userdata);

void nfs_server_insert_lists(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);

	spin_lock(&nn->nfs_client_lock);
	list_add_tail_rcu(&server->client_link, &clp->cl_superblocks);
	list_add_tail(&server->master_link, &nn->nfs_volume_list);
	clear_bit(NFS_CS_STOP_RENEW, &clp->cl_res_state);
	spin_unlock(&nn->nfs_client_lock);

}
EXPORT_SYMBOL_GPL(nfs_server_insert_lists);

void nfs_server_remove_lists(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;
	struct nfs_net *nn;

	if (clp == NULL)
		return;
	nn = net_generic(clp->cl_net, nfs_net_id);
	spin_lock(&nn->nfs_client_lock);
	list_del_rcu(&server->client_link);
	if (list_empty(&clp->cl_superblocks))
		set_bit(NFS_CS_STOP_RENEW, &clp->cl_res_state);
	list_del(&server->master_link);
	spin_unlock(&nn->nfs_client_lock);

	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nfs_server_remove_lists);

/*
 * Allocate and initialise a server record
 */
struct nfs_server *nfs_alloc_server(void)
{
	struct nfs_server *server;

	server = kzalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return NULL;

	server->client = server->client_acl = ERR_PTR(-EINVAL);

	/* Zero out the NFS state stuff */
	INIT_LIST_HEAD(&server->client_link);
	INIT_LIST_HEAD(&server->master_link);
	INIT_LIST_HEAD(&server->delegations);
	INIT_LIST_HEAD(&server->layouts);
	INIT_LIST_HEAD(&server->state_owners_lru);
	INIT_LIST_HEAD(&server->ss_copies);

	atomic_set(&server->active, 0);

	server->io_stats = nfs_alloc_iostats();
	if (!server->io_stats) {
		kfree(server);
		return NULL;
	}

	ida_init(&server->openowner_id);
	ida_init(&server->lockowner_id);
	pnfs_init_server(server);
	rpc_init_wait_queue(&server->uoc_rpcwaitq, "NFS UOC");

	return server;
}
EXPORT_SYMBOL_GPL(nfs_alloc_server);

/*
 * Free up a server record
 */
void nfs_free_server(struct nfs_server *server)
{
	nfs_server_remove_lists(server);

	if (server->destroy != NULL)
		server->destroy(server);

	if (!IS_ERR(server->client_acl))
		rpc_shutdown_client(server->client_acl);
	if (!IS_ERR(server->client))
		rpc_shutdown_client(server->client);

	nfs_put_client(server->nfs_client);

	ida_destroy(&server->lockowner_id);
	ida_destroy(&server->openowner_id);
	nfs_free_iostats(server->io_stats);
	kfree(server);
	nfs_release_automount_timer();
}
EXPORT_SYMBOL_GPL(nfs_free_server);

/*
 * Create a version 2 or 3 volume record
 * - keyed on server and FSID
 */
struct nfs_server *nfs_create_server(struct nfs_mount_info *mount_info,
				     struct nfs_subversion *nfs_mod)
{
	struct nfs_server *server;
	struct nfs_fattr *fattr;
	int error;

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	error = -ENOMEM;
	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto error;

	/* Get a client representation */
	error = nfs_init_server(server, mount_info->parsed, nfs_mod);
	if (error < 0)
		goto error;

	/* Probe the root fh to retrieve its FSID */
	error = nfs_probe_fsinfo(server, mount_info->mntfh, fattr);
	if (error < 0)
		goto error;
	if (server->nfs_client->rpc_ops->version == 3) {
		if (server->namelen == 0 || server->namelen > NFS3_MAXNAMLEN)
			server->namelen = NFS3_MAXNAMLEN;
		if (!(mount_info->parsed->flags & NFS_MOUNT_NORDIRPLUS))
			server->caps |= NFS_CAP_READDIRPLUS;
	} else {
		if (server->namelen == 0 || server->namelen > NFS2_MAXNAMLEN)
			server->namelen = NFS2_MAXNAMLEN;
	}

	if (!(fattr->valid & NFS_ATTR_FATTR)) {
		error = nfs_mod->rpc_ops->getattr(server, mount_info->mntfh,
				fattr, NULL, NULL);
		if (error < 0) {
			dprintk("nfs_create_server: getattr error = %d\n", -error);
			goto error;
		}
	}
	memcpy(&server->fsid, &fattr->fsid, sizeof(server->fsid));

	dprintk("Server FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	nfs_server_insert_lists(server);
	server->mount_time = jiffies;
	nfs_free_fattr(fattr);
	return server;

error:
	nfs_free_fattr(fattr);
	nfs_free_server(server);
	return ERR_PTR(error);
}
EXPORT_SYMBOL_GPL(nfs_create_server);

/*
 * Clone an NFS2, NFS3 or NFS4 server record
 */
struct nfs_server *nfs_clone_server(struct nfs_server *source,
				    struct nfs_fh *fh,
				    struct nfs_fattr *fattr,
				    rpc_authflavor_t flavor)
{
	struct nfs_server *server;
	struct nfs_fattr *fattr_fsinfo;
	int error;

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	error = -ENOMEM;
	fattr_fsinfo = nfs_alloc_fattr();
	if (fattr_fsinfo == NULL)
		goto out_free_server;

	/* Copy data from the source */
	server->nfs_client = source->nfs_client;
	server->destroy = source->destroy;
	refcount_inc(&server->nfs_client->cl_count);
	nfs_server_copy_userdata(server, source);

	server->fsid = fattr->fsid;

	error = nfs_init_server_rpcclient(server,
			source->client->cl_timeout,
			flavor);
	if (error < 0)
		goto out_free_server;

	/* probe the filesystem info for this server filesystem */
	error = nfs_probe_fsinfo(server, fh, fattr_fsinfo);
	if (error < 0)
		goto out_free_server;

	if (server->namelen == 0 || server->namelen > NFS4_MAXNAMLEN)
		server->namelen = NFS4_MAXNAMLEN;

	error = nfs_start_lockd(server);
	if (error < 0)
		goto out_free_server;

	nfs_server_insert_lists(server);
	server->mount_time = jiffies;

	nfs_free_fattr(fattr_fsinfo);
	return server;

out_free_server:
	nfs_free_fattr(fattr_fsinfo);
	nfs_free_server(server);
	return ERR_PTR(error);
}
EXPORT_SYMBOL_GPL(nfs_clone_server);

void nfs_clients_init(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	INIT_LIST_HEAD(&nn->nfs_client_list);
	INIT_LIST_HEAD(&nn->nfs_volume_list);
#if IS_ENABLED(CONFIG_NFS_V4)
	idr_init(&nn->cb_ident_idr);
#endif
	spin_lock_init(&nn->nfs_client_lock);
	nn->boot_time = ktime_get_real();
}

#ifdef CONFIG_PROC_FS
static void *nfs_server_list_start(struct seq_file *p, loff_t *pos);
static void *nfs_server_list_next(struct seq_file *p, void *v, loff_t *pos);
static void nfs_server_list_stop(struct seq_file *p, void *v);
static int nfs_server_list_show(struct seq_file *m, void *v);

static const struct seq_operations nfs_server_list_ops = {
	.start	= nfs_server_list_start,
	.next	= nfs_server_list_next,
	.stop	= nfs_server_list_stop,
	.show	= nfs_server_list_show,
};

static void *nfs_volume_list_start(struct seq_file *p, loff_t *pos);
static void *nfs_volume_list_next(struct seq_file *p, void *v, loff_t *pos);
static void nfs_volume_list_stop(struct seq_file *p, void *v);
static int nfs_volume_list_show(struct seq_file *m, void *v);

static const struct seq_operations nfs_volume_list_ops = {
	.start	= nfs_volume_list_start,
	.next	= nfs_volume_list_next,
	.stop	= nfs_volume_list_stop,
	.show	= nfs_volume_list_show,
};

/*
 * set up the iterator to start reading from the server list and return the first item
 */
static void *nfs_server_list_start(struct seq_file *m, loff_t *_pos)
				__acquires(&nn->nfs_client_lock)
{
	struct nfs_net *nn = net_generic(seq_file_net(m), nfs_net_id);

	/* lock the list against modification */
	spin_lock(&nn->nfs_client_lock);
	return seq_list_start_head(&nn->nfs_client_list, *_pos);
}

/*
 * move to next server
 */
static void *nfs_server_list_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct nfs_net *nn = net_generic(seq_file_net(p), nfs_net_id);

	return seq_list_next(v, &nn->nfs_client_list, pos);
}

/*
 * clean up after reading from the transports list
 */
static void nfs_server_list_stop(struct seq_file *p, void *v)
				__releases(&nn->nfs_client_lock)
{
	struct nfs_net *nn = net_generic(seq_file_net(p), nfs_net_id);

	spin_unlock(&nn->nfs_client_lock);
}

/*
 * display a header line followed by a load of call lines
 */
static int nfs_server_list_show(struct seq_file *m, void *v)
{
	struct nfs_client *clp;
	struct nfs_net *nn = net_generic(seq_file_net(m), nfs_net_id);

	/* display header on line 1 */
	if (v == &nn->nfs_client_list) {
		seq_puts(m, "NV SERVER   PORT USE HOSTNAME\n");
		return 0;
	}

	/* display one transport per line on subsequent lines */
	clp = list_entry(v, struct nfs_client, cl_share_link);

	/* Check if the client is initialized */
	if (clp->cl_cons_state != NFS_CS_READY)
		return 0;

	rcu_read_lock();
	seq_printf(m, "v%u %s %s %3d %s\n",
		   clp->rpc_ops->version,
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_ADDR),
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_PORT),
		   refcount_read(&clp->cl_count),
		   clp->cl_hostname);
	rcu_read_unlock();

	return 0;
}

/*
 * set up the iterator to start reading from the volume list and return the first item
 */
static void *nfs_volume_list_start(struct seq_file *m, loff_t *_pos)
				__acquires(&nn->nfs_client_lock)
{
	struct nfs_net *nn = net_generic(seq_file_net(m), nfs_net_id);

	/* lock the list against modification */
	spin_lock(&nn->nfs_client_lock);
	return seq_list_start_head(&nn->nfs_volume_list, *_pos);
}

/*
 * move to next volume
 */
static void *nfs_volume_list_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct nfs_net *nn = net_generic(seq_file_net(p), nfs_net_id);

	return seq_list_next(v, &nn->nfs_volume_list, pos);
}

/*
 * clean up after reading from the transports list
 */
static void nfs_volume_list_stop(struct seq_file *p, void *v)
				__releases(&nn->nfs_client_lock)
{
	struct nfs_net *nn = net_generic(seq_file_net(p), nfs_net_id);

	spin_unlock(&nn->nfs_client_lock);
}

/*
 * display a header line followed by a load of call lines
 */
static int nfs_volume_list_show(struct seq_file *m, void *v)
{
	struct nfs_server *server;
	struct nfs_client *clp;
	char dev[13];	// 8 for 2^24, 1 for ':', 3 for 2^8, 1 for '\0'
	char fsid[34];	// 2 * 16 for %llx, 1 for ':', 1 for '\0'
	struct nfs_net *nn = net_generic(seq_file_net(m), nfs_net_id);

	/* display header on line 1 */
	if (v == &nn->nfs_volume_list) {
		seq_puts(m, "NV SERVER   PORT DEV          FSID"
			    "                              FSC\n");
		return 0;
	}
	/* display one transport per line on subsequent lines */
	server = list_entry(v, struct nfs_server, master_link);
	clp = server->nfs_client;

	snprintf(dev, sizeof(dev), "%u:%u",
		 MAJOR(server->s_dev), MINOR(server->s_dev));

	snprintf(fsid, sizeof(fsid), "%llx:%llx",
		 (unsigned long long) server->fsid.major,
		 (unsigned long long) server->fsid.minor);

	rcu_read_lock();
	seq_printf(m, "v%u %s %s %-12s %-33s %s\n",
		   clp->rpc_ops->version,
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_ADDR),
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_PORT),
		   dev,
		   fsid,
		   nfs_server_fscache_state(server));
	rcu_read_unlock();

	return 0;
}

int nfs_fs_proc_net_init(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct proc_dir_entry *p;

	nn->proc_nfsfs = proc_net_mkdir(net, "nfsfs", net->proc_net);
	if (!nn->proc_nfsfs)
		goto error_0;

	/* a file of servers with which we're dealing */
	p = proc_create_net("servers", S_IFREG|S_IRUGO, nn->proc_nfsfs,
			&nfs_server_list_ops, sizeof(struct seq_net_private));
	if (!p)
		goto error_1;

	/* a file of volumes that we have mounted */
	p = proc_create_net("volumes", S_IFREG|S_IRUGO, nn->proc_nfsfs,
			&nfs_volume_list_ops, sizeof(struct seq_net_private));
	if (!p)
		goto error_1;
	return 0;

error_1:
	remove_proc_subtree("nfsfs", net->proc_net);
error_0:
	return -ENOMEM;
}

void nfs_fs_proc_net_exit(struct net *net)
{
	remove_proc_subtree("nfsfs", net->proc_net);
}

/*
 * initialise the /proc/fs/nfsfs/ directory
 */
int __init nfs_fs_proc_init(void)
{
	if (!proc_mkdir("fs/nfsfs", NULL))
		goto error_0;

	/* a file of servers with which we're dealing */
	if (!proc_symlink("fs/nfsfs/servers", NULL, "../../net/nfsfs/servers"))
		goto error_1;

	/* a file of volumes that we have mounted */
	if (!proc_symlink("fs/nfsfs/volumes", NULL, "../../net/nfsfs/volumes"))
		goto error_1;

	return 0;
error_1:
	remove_proc_subtree("fs/nfsfs", NULL);
error_0:
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/nfsfs/ directory
 */
void nfs_fs_proc_exit(void)
{
	remove_proc_subtree("fs/nfsfs", NULL);
}

#endif /* CONFIG_PROC_FS */
