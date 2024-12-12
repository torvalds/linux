// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <linux/module.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/bc_xprt.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include "internal.h"
#include "callback.h"
#include "delegation.h"
#include "nfs4session.h"
#include "nfs4idmap.h"
#include "pnfs.h"
#include "netns.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

/*
 * Get a unique NFSv4.0 callback identifier which will be used
 * by the V4.0 callback service to lookup the nfs_client struct
 */
static int nfs_get_cb_ident_idr(struct nfs_client *clp, int minorversion)
{
	int ret = 0;
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);

	if (clp->rpc_ops->version != 4 || minorversion != 0)
		return ret;
	idr_preload(GFP_KERNEL);
	spin_lock(&nn->nfs_client_lock);
	ret = idr_alloc(&nn->cb_ident_idr, clp, 1, 0, GFP_NOWAIT);
	if (ret >= 0)
		clp->cl_cb_ident = ret;
	spin_unlock(&nn->nfs_client_lock);
	idr_preload_end();
	return ret < 0 ? ret : 0;
}

#ifdef CONFIG_NFS_V4_1
/*
 * Per auth flavor data server rpc clients
 */
struct nfs4_ds_server {
	struct list_head	list;   /* ds_clp->cl_ds_clients */
	struct rpc_clnt		*rpc_clnt;
};

/**
 * nfs4_find_ds_client - Common lookup case for DS I/O
 * @ds_clp: pointer to the DS's nfs_client
 * @flavor: rpc auth flavour to match
 */
static struct nfs4_ds_server *
nfs4_find_ds_client(struct nfs_client *ds_clp, rpc_authflavor_t flavor)
{
	struct nfs4_ds_server *dss;

	rcu_read_lock();
	list_for_each_entry_rcu(dss, &ds_clp->cl_ds_clients, list) {
		if (dss->rpc_clnt->cl_auth->au_flavor != flavor)
			continue;
		goto out;
	}
	dss = NULL;
out:
	rcu_read_unlock();
	return dss;
}

static struct nfs4_ds_server *
nfs4_add_ds_client(struct nfs_client *ds_clp, rpc_authflavor_t flavor,
			   struct nfs4_ds_server *new)
{
	struct nfs4_ds_server *dss;

	spin_lock(&ds_clp->cl_lock);
	list_for_each_entry(dss, &ds_clp->cl_ds_clients, list) {
		if (dss->rpc_clnt->cl_auth->au_flavor != flavor)
			continue;
		goto out;
	}
	if (new)
		list_add_rcu(&new->list, &ds_clp->cl_ds_clients);
	dss = new;
out:
	spin_unlock(&ds_clp->cl_lock); /* need some lock to protect list */
	return dss;
}

static struct nfs4_ds_server *
nfs4_alloc_ds_server(struct nfs_client *ds_clp, rpc_authflavor_t flavor)
{
	struct nfs4_ds_server *dss;

	dss = kmalloc(sizeof(*dss), GFP_NOFS);
	if (dss == NULL)
		return ERR_PTR(-ENOMEM);

	dss->rpc_clnt = rpc_clone_client_set_auth(ds_clp->cl_rpcclient, flavor);
	if (IS_ERR(dss->rpc_clnt)) {
		int err = PTR_ERR(dss->rpc_clnt);
		kfree (dss);
		return ERR_PTR(err);
	}
	INIT_LIST_HEAD(&dss->list);

	return dss;
}

static void
nfs4_free_ds_server(struct nfs4_ds_server *dss)
{
	rpc_release_client(dss->rpc_clnt);
	kfree(dss);
}

/**
 * nfs4_find_or_create_ds_client - Find or create a DS rpc client
 * @ds_clp: pointer to the DS's nfs_client
 * @inode: pointer to the inode
 *
 * Find or create a DS rpc client with th MDS server rpc client auth flavor
 * in the nfs_client cl_ds_clients list.
 */
struct rpc_clnt *
nfs4_find_or_create_ds_client(struct nfs_client *ds_clp, struct inode *inode)
{
	struct nfs4_ds_server *dss, *new;
	rpc_authflavor_t flavor = NFS_SERVER(inode)->client->cl_auth->au_flavor;

	dss = nfs4_find_ds_client(ds_clp, flavor);
	if (dss != NULL)
		goto out;
	new = nfs4_alloc_ds_server(ds_clp, flavor);
	if (IS_ERR(new))
		return ERR_CAST(new);
	dss = nfs4_add_ds_client(ds_clp, flavor, new);
	if (dss != new)
		nfs4_free_ds_server(new);
out:
	return dss->rpc_clnt;
}
EXPORT_SYMBOL_GPL(nfs4_find_or_create_ds_client);

static void
nfs4_shutdown_ds_clients(struct nfs_client *clp)
{
	struct nfs4_ds_server *dss;

	while (!list_empty(&clp->cl_ds_clients)) {
		dss = list_entry(clp->cl_ds_clients.next,
					struct nfs4_ds_server, list);
		list_del(&dss->list);
		rpc_shutdown_client(dss->rpc_clnt);
		kfree (dss);
	}
}

static void
nfs4_cleanup_callback(struct nfs_client *clp)
{
	struct nfs4_copy_state *cp_state;

	while (!list_empty(&clp->pending_cb_stateids)) {
		cp_state = list_entry(clp->pending_cb_stateids.next,
					struct nfs4_copy_state, copies);
		list_del(&cp_state->copies);
		kfree(cp_state);
	}
}

void nfs41_shutdown_client(struct nfs_client *clp)
{
	if (nfs4_has_session(clp)) {
		nfs4_cleanup_callback(clp);
		nfs4_shutdown_ds_clients(clp);
		nfs4_destroy_session(clp->cl_session);
		nfs4_destroy_clientid(clp);
	}

}
#endif	/* CONFIG_NFS_V4_1 */

void nfs40_shutdown_client(struct nfs_client *clp)
{
	if (clp->cl_slot_tbl) {
		nfs4_shutdown_slot_table(clp->cl_slot_tbl);
		kfree(clp->cl_slot_tbl);
	}
}

struct nfs_client *nfs4_alloc_client(const struct nfs_client_initdata *cl_init)
{
	char buf[INET6_ADDRSTRLEN + 1];
	const char *ip_addr = cl_init->ip_addr;
	struct nfs_client *clp = nfs_alloc_client(cl_init);
	int err;

	if (IS_ERR(clp))
		return clp;

	err = nfs_get_cb_ident_idr(clp, cl_init->minorversion);
	if (err)
		goto error;

	if (cl_init->minorversion > NFS4_MAX_MINOR_VERSION) {
		err = -EINVAL;
		goto error;
	}

	spin_lock_init(&clp->cl_lock);
	INIT_DELAYED_WORK(&clp->cl_renewd, nfs4_renew_state);
	INIT_LIST_HEAD(&clp->cl_ds_clients);
	rpc_init_wait_queue(&clp->cl_rpcwaitq, "NFS client");
	clp->cl_state = 1 << NFS4CLNT_LEASE_EXPIRED;
	clp->cl_mvops = nfs_v4_minor_ops[cl_init->minorversion];
	clp->cl_mig_gen = 1;
#if IS_ENABLED(CONFIG_NFS_V4_1)
	init_waitqueue_head(&clp->cl_lock_waitq);
#endif
	INIT_LIST_HEAD(&clp->pending_cb_stateids);

	if (cl_init->minorversion != 0)
		__set_bit(NFS_CS_INFINITE_SLOTS, &clp->cl_flags);
	__set_bit(NFS_CS_DISCRTRY, &clp->cl_flags);
	__set_bit(NFS_CS_NO_RETRANS_TIMEOUT, &clp->cl_flags);
	if (test_bit(NFS_CS_PNFS, &cl_init->init_flags))
		__set_bit(NFS_CS_PNFS, &clp->cl_flags);
	/*
	 * Set up the connection to the server before we add add to the
	 * global list.
	 */
	err = nfs_create_rpc_client(clp, cl_init, RPC_AUTH_GSS_KRB5I);
	if (err == -EINVAL)
		err = nfs_create_rpc_client(clp, cl_init, RPC_AUTH_UNIX);
	if (err < 0)
		goto error;

	/* If no clientaddr= option was specified, find a usable cb address */
	if (ip_addr == NULL) {
		struct sockaddr_storage cb_addr;
		struct sockaddr *sap = (struct sockaddr *)&cb_addr;

		err = rpc_localaddr(clp->cl_rpcclient, sap, sizeof(cb_addr));
		if (err < 0)
			goto error;
		err = rpc_ntop(sap, buf, sizeof(buf));
		if (err < 0)
			goto error;
		ip_addr = (const char *)buf;
	}
	strscpy(clp->cl_ipaddr, ip_addr, sizeof(clp->cl_ipaddr));

	err = nfs_idmap_new(clp);
	if (err < 0) {
		dprintk("%s: failed to create idmapper. Error = %d\n",
			__func__, err);
		goto error;
	}
	__set_bit(NFS_CS_IDMAP, &clp->cl_res_state);
	return clp;

error:
	nfs_free_client(clp);
	return ERR_PTR(err);
}

/*
 * Destroy the NFS4 callback service
 */
static void nfs4_destroy_callback(struct nfs_client *clp)
{
	if (__test_and_clear_bit(NFS_CS_CALLBACK, &clp->cl_res_state))
		nfs_callback_down(clp->cl_mvops->minor_version, clp->cl_net);
}

static void nfs4_shutdown_client(struct nfs_client *clp)
{
	if (__test_and_clear_bit(NFS_CS_RENEWD, &clp->cl_res_state))
		nfs4_kill_renewd(clp);
	clp->cl_mvops->shutdown_client(clp);
	nfs4_destroy_callback(clp);
	if (__test_and_clear_bit(NFS_CS_IDMAP, &clp->cl_res_state))
		nfs_idmap_delete(clp);

	rpc_destroy_wait_queue(&clp->cl_rpcwaitq);
	kfree(clp->cl_serverowner);
	kfree(clp->cl_serverscope);
	kfree(clp->cl_implid);
	kfree(clp->cl_owner_id);
}

void nfs4_free_client(struct nfs_client *clp)
{
	nfs4_shutdown_client(clp);
	nfs_free_client(clp);
}

/*
 * Initialize the NFS4 callback service
 */
static int nfs4_init_callback(struct nfs_client *clp)
{
	struct rpc_xprt *xprt;
	int error;

	xprt = rcu_dereference_raw(clp->cl_rpcclient->cl_xprt);

	if (nfs4_has_session(clp)) {
		error = xprt_setup_backchannel(xprt, NFS41_BC_MIN_CALLBACKS);
		if (error < 0)
			return error;
	}

	error = nfs_callback_up(clp->cl_mvops->minor_version, xprt);
	if (error < 0) {
		dprintk("%s: failed to start callback. Error = %d\n",
			__func__, error);
		return error;
	}
	__set_bit(NFS_CS_CALLBACK, &clp->cl_res_state);

	return 0;
}

/**
 * nfs40_init_client - nfs_client initialization tasks for NFSv4.0
 * @clp: nfs_client to initialize
 *
 * Returns zero on success, or a negative errno if some error occurred.
 */
int nfs40_init_client(struct nfs_client *clp)
{
	struct nfs4_slot_table *tbl;
	int ret;

	tbl = kzalloc(sizeof(*tbl), GFP_NOFS);
	if (tbl == NULL)
		return -ENOMEM;

	ret = nfs4_setup_slot_table(tbl, NFS4_MAX_SLOT_TABLE,
					"NFSv4.0 transport Slot table");
	if (ret) {
		nfs4_shutdown_slot_table(tbl);
		kfree(tbl);
		return ret;
	}

	clp->cl_slot_tbl = tbl;
	return 0;
}

#if defined(CONFIG_NFS_V4_1)

/**
 * nfs41_init_client - nfs_client initialization tasks for NFSv4.1+
 * @clp: nfs_client to initialize
 *
 * Returns zero on success, or a negative errno if some error occurred.
 */
int nfs41_init_client(struct nfs_client *clp)
{
	struct nfs4_session *session = NULL;

	/*
	 * Create the session and mark it expired.
	 * When a SEQUENCE operation encounters the expired session
	 * it will do session recovery to initialize it.
	 */
	session = nfs4_alloc_session(clp);
	if (!session)
		return -ENOMEM;

	clp->cl_session = session;

	/*
	 * The create session reply races with the server back
	 * channel probe. Mark the client NFS_CS_SESSION_INITING
	 * so that the client back channel can find the
	 * nfs_client struct
	 */
	nfs_mark_client_ready(clp, NFS_CS_SESSION_INITING);
	return 0;
}

#endif	/* CONFIG_NFS_V4_1 */

/*
 * Initialize the minor version specific parts of an NFS4 client record
 */
static int nfs4_init_client_minor_version(struct nfs_client *clp)
{
	int ret;

	ret = clp->cl_mvops->init_client(clp);
	if (ret)
		return ret;
	return nfs4_init_callback(clp);
}

static void nfs4_add_trunk(struct nfs_client *clp, struct nfs_client *old)
{
	struct sockaddr_storage clp_addr, old_addr;
	struct sockaddr *clp_sap = (struct sockaddr *)&clp_addr;
	struct sockaddr *old_sap = (struct sockaddr *)&old_addr;
	size_t clp_salen;
	struct xprt_create xprt_args = {
		.ident = old->cl_proto,
		.net = old->cl_net,
		.servername = old->cl_hostname,
	};
	int max_connect = test_bit(NFS_CS_PNFS, &clp->cl_flags) ?
		clp->cl_max_connect : old->cl_max_connect;

	if (clp->cl_proto != old->cl_proto)
		return;
	clp_salen = rpc_peeraddr(clp->cl_rpcclient, clp_sap, sizeof(clp_addr));
	rpc_peeraddr(old->cl_rpcclient, old_sap, sizeof(old_addr));

	if (clp_addr.ss_family != old_addr.ss_family)
		return;

	xprt_args.dstaddr = clp_sap;
	xprt_args.addrlen = clp_salen;

	rpc_clnt_add_xprt(old->cl_rpcclient, &xprt_args,
			  rpc_clnt_test_and_add_xprt, &max_connect);
}

/**
 * nfs4_init_client - Initialise an NFS4 client record
 *
 * @clp: nfs_client to initialise
 * @cl_init: pointer to nfs_client_initdata
 *
 * Returns pointer to an NFS client, or an ERR_PTR value.
 */
struct nfs_client *nfs4_init_client(struct nfs_client *clp,
				    const struct nfs_client_initdata *cl_init)
{
	struct nfs_client *old;
	int error;

	if (clp->cl_cons_state == NFS_CS_READY)
		/* the client is initialised already */
		return clp;

	error = nfs4_init_client_minor_version(clp);
	if (error < 0)
		goto error;

	error = nfs4_discover_server_trunking(clp, &old);
	if (error < 0)
		goto error;

	if (clp != old) {
		clp->cl_preserve_clid = true;
		/*
		 * Mark the client as having failed initialization so other
		 * processes walking the nfs_client_list in nfs_match_client()
		 * won't try to use it.
		 */
		nfs_mark_client_ready(clp, -EPERM);
		if (old->cl_mvops->session_trunk)
			nfs4_add_trunk(clp, old);
	}
	clear_bit(NFS_CS_TSM_POSSIBLE, &clp->cl_flags);
	nfs_put_client(clp);
	return old;

error:
	nfs_mark_client_ready(clp, error);
	nfs_put_client(clp);
	return ERR_PTR(error);
}

/*
 * SETCLIENTID just did a callback update with the callback ident in
 * "drop," but server trunking discovery claims "drop" and "keep" are
 * actually the same server.  Swap the callback IDs so that "keep"
 * will continue to use the callback ident the server now knows about,
 * and so that "keep"'s original callback ident is destroyed when
 * "drop" is freed.
 */
static void nfs4_swap_callback_idents(struct nfs_client *keep,
				      struct nfs_client *drop)
{
	struct nfs_net *nn = net_generic(keep->cl_net, nfs_net_id);
	unsigned int save = keep->cl_cb_ident;

	if (keep->cl_cb_ident == drop->cl_cb_ident)
		return;

	dprintk("%s: keeping callback ident %u and dropping ident %u\n",
		__func__, keep->cl_cb_ident, drop->cl_cb_ident);

	spin_lock(&nn->nfs_client_lock);

	idr_replace(&nn->cb_ident_idr, keep, drop->cl_cb_ident);
	keep->cl_cb_ident = drop->cl_cb_ident;

	idr_replace(&nn->cb_ident_idr, drop, save);
	drop->cl_cb_ident = save;

	spin_unlock(&nn->nfs_client_lock);
}

static bool nfs4_match_client_owner_id(const struct nfs_client *clp1,
		const struct nfs_client *clp2)
{
	if (clp1->cl_owner_id == NULL || clp2->cl_owner_id == NULL)
		return true;
	return strcmp(clp1->cl_owner_id, clp2->cl_owner_id) == 0;
}

static bool nfs4_same_verifier(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return memcmp(v1->data, v2->data, sizeof(v1->data)) == 0;
}

static int nfs4_match_client(struct nfs_client  *pos,  struct nfs_client *new,
			     struct nfs_client **prev, struct nfs_net *nn)
{
	int status;

	if (pos->rpc_ops != new->rpc_ops)
		return 1;

	if (pos->cl_minorversion != new->cl_minorversion)
		return 1;

	/* If "pos" isn't marked ready, we can't trust the
	 * remaining fields in "pos", especially the client
	 * ID and serverowner fields.  Wait for CREATE_SESSION
	 * to finish. */
	if (pos->cl_cons_state > NFS_CS_READY) {
		refcount_inc(&pos->cl_count);
		spin_unlock(&nn->nfs_client_lock);

		nfs_put_client(*prev);
		*prev = pos;

		status = nfs_wait_client_init_complete(pos);
		spin_lock(&nn->nfs_client_lock);

		if (status < 0)
			return status;
	}

	if (pos->cl_cons_state != NFS_CS_READY)
		return 1;

	if (pos->cl_clientid != new->cl_clientid)
		return 1;

	/* NFSv4.1 always uses the uniform string, however someone
	 * might switch the uniquifier string on us.
	 */
	if (!nfs4_match_client_owner_id(pos, new))
		return 1;

	return 0;
}

/**
 * nfs40_walk_client_list - Find server that recognizes a client ID
 *
 * @new: nfs_client with client ID to test
 * @result: OUT: found nfs_client, or new
 * @cred: credential to use for trunking test
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status.
 * If zero is returned, an nfs_client pointer is planted in "result."
 *
 * NB: nfs40_walk_client_list() relies on the new nfs_client being
 *     the last nfs_client on the list.
 */
int nfs40_walk_client_list(struct nfs_client *new,
			   struct nfs_client **result,
			   const struct cred *cred)
{
	struct nfs_net *nn = net_generic(new->cl_net, nfs_net_id);
	struct nfs_client *pos, *prev = NULL;
	struct nfs4_setclientid_res clid = {
		.clientid	= new->cl_clientid,
		.confirm	= new->cl_confirm,
	};
	int status = -NFS4ERR_STALE_CLIENTID;

	spin_lock(&nn->nfs_client_lock);
	list_for_each_entry(pos, &nn->nfs_client_list, cl_share_link) {

		if (pos == new)
			goto found;

		status = nfs4_match_client(pos, new, &prev, nn);
		if (status < 0)
			goto out_unlock;
		if (status != 0)
			continue;
		/*
		 * We just sent a new SETCLIENTID, which should have
		 * caused the server to return a new cl_confirm.  So if
		 * cl_confirm is the same, then this is a different
		 * server that just returned the same cl_confirm by
		 * coincidence:
		 */
		if ((new != pos) && nfs4_same_verifier(&pos->cl_confirm,
						       &new->cl_confirm))
			continue;
		/*
		 * But if the cl_confirm's are different, then the only
		 * way that a SETCLIENTID_CONFIRM to pos can succeed is
		 * if new and pos point to the same server:
		 */
found:
		refcount_inc(&pos->cl_count);
		spin_unlock(&nn->nfs_client_lock);

		nfs_put_client(prev);
		prev = pos;

		status = nfs4_proc_setclientid_confirm(pos, &clid, cred);
		switch (status) {
		case -NFS4ERR_STALE_CLIENTID:
			break;
		case 0:
			nfs4_swap_callback_idents(pos, new);
			pos->cl_confirm = new->cl_confirm;
			nfs_mark_client_ready(pos, NFS_CS_READY);

			prev = NULL;
			*result = pos;
			goto out;
		case -ERESTARTSYS:
		case -ETIMEDOUT:
			/* The callback path may have been inadvertently
			 * changed. Schedule recovery!
			 */
			nfs4_schedule_path_down_recovery(pos);
			goto out;
		default:
			goto out;
		}

		spin_lock(&nn->nfs_client_lock);
	}
out_unlock:
	spin_unlock(&nn->nfs_client_lock);

	/* No match found. The server lost our clientid */
out:
	nfs_put_client(prev);
	return status;
}

#ifdef CONFIG_NFS_V4_1
/*
 * Returns true if the server major ids match
 */
bool
nfs4_check_serverowner_major_id(struct nfs41_server_owner *o1,
				struct nfs41_server_owner *o2)
{
	if (o1->major_id_sz != o2->major_id_sz)
		return false;
	return memcmp(o1->major_id, o2->major_id, o1->major_id_sz) == 0;
}

/*
 * Returns true if the server scopes match
 */
static bool
nfs4_check_server_scope(struct nfs41_server_scope *s1,
			struct nfs41_server_scope *s2)
{
	if (s1->server_scope_sz != s2->server_scope_sz)
		return false;
	return memcmp(s1->server_scope, s2->server_scope,
					s1->server_scope_sz) == 0;
}

/**
 * nfs4_detect_session_trunking - Checks for session trunking.
 * @clp:    original mount nfs_client
 * @res:    result structure from an exchange_id using the original mount
 *          nfs_client with a new multi_addr transport
 * @xprt:   pointer to the transport to add.
 *
 * Called after a successful EXCHANGE_ID on a multi-addr connection.
 * Upon success, add the transport.
 *
 * Returns zero on success, otherwise -EINVAL
 *
 * Note: since the exchange_id for the new multi_addr transport uses the
 * same nfs_client from the original mount, the cl_owner_id is reused,
 * so eir_clientowner is the same.
 */
int nfs4_detect_session_trunking(struct nfs_client *clp,
				 struct nfs41_exchange_id_res *res,
				 struct rpc_xprt *xprt)
{
	/* Check eir_clientid */
	if (clp->cl_clientid != res->clientid)
		goto out_err;

	/* Check eir_server_owner so_major_id */
	if (!nfs4_check_serverowner_major_id(clp->cl_serverowner,
					     res->server_owner))
		goto out_err;

	/* Check eir_server_owner so_minor_id */
	if (clp->cl_serverowner->minor_id != res->server_owner->minor_id)
		goto out_err;

	/* Check eir_server_scope */
	if (!nfs4_check_server_scope(clp->cl_serverscope, res->server_scope))
		goto out_err;

	pr_info("NFS:  %s: Session trunking succeeded for %s\n",
		clp->cl_hostname,
		xprt->address_strings[RPC_DISPLAY_ADDR]);

	return 0;
out_err:
	pr_info("NFS:  %s: Session trunking failed for %s\n", clp->cl_hostname,
		xprt->address_strings[RPC_DISPLAY_ADDR]);

	return -EINVAL;
}

/**
 * nfs41_walk_client_list - Find nfs_client that matches a client/server owner
 *
 * @new: nfs_client with client ID to test
 * @result: OUT: found nfs_client, or new
 * @cred: credential to use for trunking test
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status.
 * If zero is returned, an nfs_client pointer is planted in "result."
 *
 * NB: nfs41_walk_client_list() relies on the new nfs_client being
 *     the last nfs_client on the list.
 */
int nfs41_walk_client_list(struct nfs_client *new,
			   struct nfs_client **result,
			   const struct cred *cred)
{
	struct nfs_net *nn = net_generic(new->cl_net, nfs_net_id);
	struct nfs_client *pos, *prev = NULL;
	int status = -NFS4ERR_STALE_CLIENTID;

	spin_lock(&nn->nfs_client_lock);
	list_for_each_entry(pos, &nn->nfs_client_list, cl_share_link) {

		if (pos == new)
			goto found;

		status = nfs4_match_client(pos, new, &prev, nn);
		if (status < 0)
			goto out;
		if (status != 0)
			continue;

		/*
		 * Note that session trunking is just a special subcase of
		 * client id trunking. In either case, we want to fall back
		 * to using the existing nfs_client.
		 */
		if (!nfs4_check_serverowner_major_id(pos->cl_serverowner,
						     new->cl_serverowner))
			continue;

found:
		refcount_inc(&pos->cl_count);
		*result = pos;
		status = 0;
		break;
	}

out:
	spin_unlock(&nn->nfs_client_lock);
	nfs_put_client(prev);
	return status;
}
#endif	/* CONFIG_NFS_V4_1 */

static void nfs4_destroy_server(struct nfs_server *server)
{
	LIST_HEAD(freeme);

	nfs_server_return_all_delegations(server);
	unset_pnfs_layoutdriver(server);
	nfs4_purge_state_owners(server, &freeme);
	nfs4_free_state_owners(&freeme);
}

/*
 * NFSv4.0 callback thread helper
 *
 * Find a client by callback identifier
 */
struct nfs_client *
nfs4_find_client_ident(struct net *net, int cb_ident)
{
	struct nfs_client *clp;
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	spin_lock(&nn->nfs_client_lock);
	clp = idr_find(&nn->cb_ident_idr, cb_ident);
	if (clp)
		refcount_inc(&clp->cl_count);
	spin_unlock(&nn->nfs_client_lock);
	return clp;
}

#if defined(CONFIG_NFS_V4_1)
/* Common match routine for v4.0 and v4.1 callback services */
static bool nfs4_cb_match_client(const struct sockaddr *addr,
		struct nfs_client *clp, u32 minorversion)
{
	struct sockaddr *clap = (struct sockaddr *)&clp->cl_addr;

	/* Don't match clients that failed to initialise */
	if (!(clp->cl_cons_state == NFS_CS_READY ||
	    clp->cl_cons_state == NFS_CS_SESSION_INITING))
		return false;

	smp_rmb();

	/* Match the version and minorversion */
	if (clp->rpc_ops->version != 4 ||
	    clp->cl_minorversion != minorversion)
		return false;

	/* Match only the IP address, not the port number */
	return rpc_cmp_addr(addr, clap);
}

/*
 * NFSv4.1 callback thread helper
 * For CB_COMPOUND calls, find a client by IP address, protocol version,
 * minorversion, and sessionID
 *
 * Returns NULL if no such client
 */
struct nfs_client *
nfs4_find_client_sessionid(struct net *net, const struct sockaddr *addr,
			   struct nfs4_sessionid *sid, u32 minorversion)
{
	struct nfs_client *clp;
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	spin_lock(&nn->nfs_client_lock);
	list_for_each_entry(clp, &nn->nfs_client_list, cl_share_link) {
		if (!nfs4_cb_match_client(addr, clp, minorversion))
			continue;

		if (!nfs4_has_session(clp))
			continue;

		/* Match sessionid*/
		if (memcmp(clp->cl_session->sess_id.data,
		    sid->data, NFS4_MAX_SESSIONID_LEN) != 0)
			continue;

		refcount_inc(&clp->cl_count);
		spin_unlock(&nn->nfs_client_lock);
		return clp;
	}
	spin_unlock(&nn->nfs_client_lock);
	return NULL;
}

#else /* CONFIG_NFS_V4_1 */

struct nfs_client *
nfs4_find_client_sessionid(struct net *net, const struct sockaddr *addr,
			   struct nfs4_sessionid *sid, u32 minorversion)
{
	return NULL;
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Set up an NFS4 client
 */
static int nfs4_set_client(struct nfs_server *server,
		const char *hostname,
		const struct sockaddr_storage *addr,
		const size_t addrlen,
		const char *ip_addr,
		int proto, const struct rpc_timeout *timeparms,
		u32 minorversion, unsigned int nconnect,
		unsigned int max_connect,
		struct net *net)
{
	struct nfs_client_initdata cl_init = {
		.hostname = hostname,
		.addr = addr,
		.addrlen = addrlen,
		.ip_addr = ip_addr,
		.nfs_mod = &nfs_v4,
		.proto = proto,
		.minorversion = minorversion,
		.net = net,
		.timeparms = timeparms,
		.cred = server->cred,
	};
	struct nfs_client *clp;

	if (minorversion == 0)
		__set_bit(NFS_CS_REUSEPORT, &cl_init.init_flags);
	else
		cl_init.max_connect = max_connect;
	if (proto == XPRT_TRANSPORT_TCP)
		cl_init.nconnect = nconnect;

	if (server->flags & NFS_MOUNT_NORESVPORT)
		__set_bit(NFS_CS_NORESVPORT, &cl_init.init_flags);
	if (server->options & NFS_OPTION_MIGRATION)
		__set_bit(NFS_CS_MIGRATION, &cl_init.init_flags);
	if (test_bit(NFS_MIG_TSM_POSSIBLE, &server->mig_status))
		__set_bit(NFS_CS_TSM_POSSIBLE, &cl_init.init_flags);
	server->port = rpc_get_port((struct sockaddr *)addr);

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(&cl_init);
	if (IS_ERR(clp))
		return PTR_ERR(clp);

	if (server->nfs_client == clp) {
		nfs_put_client(clp);
		return -ELOOP;
	}

	/*
	 * Query for the lease time on clientid setup or renewal
	 *
	 * Note that this will be set on nfs_clients that were created
	 * only for the DS role and did not set this bit, but now will
	 * serve a dual role.
	 */
	set_bit(NFS_CS_CHECK_LEASE_TIME, &clp->cl_res_state);

	server->nfs_client = clp;
	return 0;
}

/*
 * Set up a pNFS Data Server client.
 *
 * Return any existing nfs_client that matches server address,port,version
 * and minorversion.
 *
 * For a new nfs_client, use a soft mount (default), a low retrans and a
 * low timeout interval so that if a connection is lost, we retry through
 * the MDS.
 */
struct nfs_client *nfs4_set_ds_client(struct nfs_server *mds_srv,
		const struct sockaddr_storage *ds_addr, int ds_addrlen,
		int ds_proto, unsigned int ds_timeo, unsigned int ds_retrans,
		u32 minor_version)
{
	struct rpc_timeout ds_timeout;
	struct nfs_client *mds_clp = mds_srv->nfs_client;
	struct nfs_client_initdata cl_init = {
		.addr = ds_addr,
		.addrlen = ds_addrlen,
		.nodename = mds_clp->cl_rpcclient->cl_nodename,
		.ip_addr = mds_clp->cl_ipaddr,
		.nfs_mod = &nfs_v4,
		.proto = ds_proto,
		.minorversion = minor_version,
		.net = mds_clp->cl_net,
		.timeparms = &ds_timeout,
		.cred = mds_srv->cred,
	};
	char buf[INET6_ADDRSTRLEN + 1];

	if (rpc_ntop((struct sockaddr *)ds_addr, buf, sizeof(buf)) <= 0)
		return ERR_PTR(-EINVAL);
	cl_init.hostname = buf;

	if (mds_clp->cl_nconnect > 1 && ds_proto == XPRT_TRANSPORT_TCP) {
		cl_init.nconnect = mds_clp->cl_nconnect;
		cl_init.max_connect = NFS_MAX_TRANSPORTS;
	}

	if (mds_srv->flags & NFS_MOUNT_NORESVPORT)
		__set_bit(NFS_CS_NORESVPORT, &cl_init.init_flags);

	__set_bit(NFS_CS_PNFS, &cl_init.init_flags);
	cl_init.max_connect = NFS_MAX_TRANSPORTS;
	/*
	 * Set an authflavor equual to the MDS value. Use the MDS nfs_client
	 * cl_ipaddr so as to use the same EXCHANGE_ID co_ownerid as the MDS
	 * (section 13.1 RFC 5661).
	 */
	nfs_init_timeout_values(&ds_timeout, ds_proto, ds_timeo, ds_retrans);
	return nfs_get_client(&cl_init);
}
EXPORT_SYMBOL_GPL(nfs4_set_ds_client);

/*
 * Session has been established, and the client marked ready.
 * Limit the mount rsize, wsize and dtsize using negotiated fore
 * channel attributes.
 */
static void nfs4_session_limit_rwsize(struct nfs_server *server)
{
#ifdef CONFIG_NFS_V4_1
	struct nfs4_session *sess;
	u32 server_resp_sz;
	u32 server_rqst_sz;

	if (!nfs4_has_session(server->nfs_client))
		return;
	sess = server->nfs_client->cl_session;
	server_resp_sz = sess->fc_attrs.max_resp_sz - nfs41_maxread_overhead;
	server_rqst_sz = sess->fc_attrs.max_rqst_sz - nfs41_maxwrite_overhead;

	if (server->dtsize > server_resp_sz)
		server->dtsize = server_resp_sz;
	if (server->rsize > server_resp_sz)
		server->rsize = server_resp_sz;
	if (server->wsize > server_rqst_sz)
		server->wsize = server_rqst_sz;
#endif /* CONFIG_NFS_V4_1 */
}

/*
 * Limit xattr sizes using the channel attributes.
 */
static void nfs4_session_limit_xasize(struct nfs_server *server)
{
#ifdef CONFIG_NFS_V4_2
	struct nfs4_session *sess;
	u32 server_gxa_sz;
	u32 server_sxa_sz;
	u32 server_lxa_sz;

	if (!nfs4_has_session(server->nfs_client))
		return;

	sess = server->nfs_client->cl_session;

	server_gxa_sz = sess->fc_attrs.max_resp_sz - nfs42_maxgetxattr_overhead;
	server_sxa_sz = sess->fc_attrs.max_rqst_sz - nfs42_maxsetxattr_overhead;
	server_lxa_sz = sess->fc_attrs.max_resp_sz -
	    nfs42_maxlistxattrs_overhead;

	if (server->gxasize > server_gxa_sz)
		server->gxasize = server_gxa_sz;
	if (server->sxasize > server_sxa_sz)
		server->sxasize = server_sxa_sz;
	if (server->lxasize > server_lxa_sz)
		server->lxasize = server_lxa_sz;
#endif
}

void nfs4_server_set_init_caps(struct nfs_server *server)
{
	/* Set the basic capabilities */
	server->caps |= server->nfs_client->cl_mvops->init_caps;
	if (server->flags & NFS_MOUNT_NORDIRPLUS)
			server->caps &= ~NFS_CAP_READDIRPLUS;
	if (server->nfs_client->cl_proto == XPRT_TRANSPORT_RDMA)
		server->caps &= ~NFS_CAP_READ_PLUS;

	/*
	 * Don't use NFS uid/gid mapping if we're using AUTH_SYS or lower
	 * authentication.
	 */
	if (nfs4_disable_idmapping &&
			server->client->cl_auth->au_flavor == RPC_AUTH_UNIX)
		server->caps |= NFS_CAP_UIDGID_NOMAP;
}

static int nfs4_server_common_setup(struct nfs_server *server,
		struct nfs_fh *mntfh, bool auth_probe)
{
	int error;

	/* data servers support only a subset of NFSv4.1 */
	if (is_ds_only_client(server->nfs_client))
		return -EPROTONOSUPPORT;

	/* We must ensure the session is initialised first */
	error = nfs4_init_session(server->nfs_client);
	if (error < 0)
		goto out;

	nfs4_server_set_init_caps(server);

	/* Probe the root fh to retrieve its FSID and filehandle */
	error = nfs4_get_rootfh(server, mntfh, auth_probe);
	if (error < 0)
		goto out;

	dprintk("Server FSID: %llx:%llx\n",
			(unsigned long long) server->fsid.major,
			(unsigned long long) server->fsid.minor);
	nfs_display_fhandle(mntfh, "Pseudo-fs root FH");

	error = nfs_probe_server(server, mntfh);
	if (error < 0)
		goto out;

	nfs4_session_limit_rwsize(server);
	nfs4_session_limit_xasize(server);

	if (server->namelen == 0 || server->namelen > NFS4_MAXNAMLEN)
		server->namelen = NFS4_MAXNAMLEN;

	nfs_server_insert_lists(server);
	server->mount_time = jiffies;
	server->destroy = nfs4_destroy_server;
out:
	return error;
}

/*
 * Create a version 4 volume record
 */
static int nfs4_init_server(struct nfs_server *server, struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct rpc_timeout timeparms;
	int error;

	nfs_init_timeout_values(&timeparms, ctx->nfs_server.protocol,
				ctx->timeo, ctx->retrans);

	/* Initialise the client representation from the mount data */
	server->flags = ctx->flags;
	server->options = ctx->options;
	server->auth_info = ctx->auth_info;

	/* Use the first specified auth flavor. If this flavor isn't
	 * allowed by the server, use the SECINFO path to try the
	 * other specified flavors */
	if (ctx->auth_info.flavor_len >= 1)
		ctx->selected_flavor = ctx->auth_info.flavors[0];
	else
		ctx->selected_flavor = RPC_AUTH_UNIX;

	/* Get a client record */
	error = nfs4_set_client(server,
				ctx->nfs_server.hostname,
				&ctx->nfs_server._address,
				ctx->nfs_server.addrlen,
				ctx->client_address,
				ctx->nfs_server.protocol,
				&timeparms,
				ctx->minorversion,
				ctx->nfs_server.nconnect,
				ctx->nfs_server.max_connect,
				fc->net_ns);
	if (error < 0)
		return error;

	if (ctx->rsize)
		server->rsize = nfs_io_size(ctx->rsize, server->nfs_client->cl_proto);
	if (ctx->wsize)
		server->wsize = nfs_io_size(ctx->wsize, server->nfs_client->cl_proto);

	server->acregmin = ctx->acregmin * HZ;
	server->acregmax = ctx->acregmax * HZ;
	server->acdirmin = ctx->acdirmin * HZ;
	server->acdirmax = ctx->acdirmax * HZ;
	server->port     = ctx->nfs_server.port;

	return nfs_init_server_rpcclient(server, &timeparms,
					 ctx->selected_flavor);
}

/*
 * Create a version 4 volume record
 * - keyed on server and FSID
 */
struct nfs_server *nfs4_create_server(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct nfs_server *server;
	bool auth_probe;
	int error;

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	server->cred = get_cred(fc->cred);

	auth_probe = ctx->auth_info.flavor_len < 1;

	/* set up the general RPC client */
	error = nfs4_init_server(server, fc);
	if (error < 0)
		goto error;

	error = nfs4_server_common_setup(server, ctx->mntfh, auth_probe);
	if (error < 0)
		goto error;

	return server;

error:
	nfs_free_server(server);
	return ERR_PTR(error);
}

/*
 * Create an NFS4 referral server record
 */
struct nfs_server *nfs4_create_referral_server(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct nfs_client *parent_client;
	struct nfs_server *server, *parent_server;
	bool auth_probe;
	int error;

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	parent_server = NFS_SB(ctx->clone_data.sb);
	parent_client = parent_server->nfs_client;

	server->cred = get_cred(parent_server->cred);

	/* Initialise the client representation from the parent server */
	nfs_server_copy_userdata(server, parent_server);

	/* Get a client representation */
#if IS_ENABLED(CONFIG_SUNRPC_XPRT_RDMA)
	rpc_set_port(&ctx->nfs_server.address, NFS_RDMA_PORT);
	error = nfs4_set_client(server,
				ctx->nfs_server.hostname,
				&ctx->nfs_server._address,
				ctx->nfs_server.addrlen,
				parent_client->cl_ipaddr,
				XPRT_TRANSPORT_RDMA,
				parent_server->client->cl_timeout,
				parent_client->cl_mvops->minor_version,
				parent_client->cl_nconnect,
				parent_client->cl_max_connect,
				parent_client->cl_net);
	if (!error)
		goto init_server;
#endif	/* IS_ENABLED(CONFIG_SUNRPC_XPRT_RDMA) */

	rpc_set_port(&ctx->nfs_server.address, NFS_PORT);
	error = nfs4_set_client(server,
				ctx->nfs_server.hostname,
				&ctx->nfs_server._address,
				ctx->nfs_server.addrlen,
				parent_client->cl_ipaddr,
				XPRT_TRANSPORT_TCP,
				parent_server->client->cl_timeout,
				parent_client->cl_mvops->minor_version,
				parent_client->cl_nconnect,
				parent_client->cl_max_connect,
				parent_client->cl_net);
	if (error < 0)
		goto error;

#if IS_ENABLED(CONFIG_SUNRPC_XPRT_RDMA)
init_server:
#endif
	error = nfs_init_server_rpcclient(server, parent_server->client->cl_timeout,
					  ctx->selected_flavor);
	if (error < 0)
		goto error;

	auth_probe = parent_server->auth_info.flavor_len < 1;

	error = nfs4_server_common_setup(server, ctx->mntfh, auth_probe);
	if (error < 0)
		goto error;

	return server;

error:
	nfs_free_server(server);
	return ERR_PTR(error);
}

/**
 * nfs4_update_server - Move an nfs_server to a different nfs_client
 *
 * @server: represents FSID to be moved
 * @hostname: new end-point's hostname
 * @sap: new end-point's socket address
 * @salen: size of "sap"
 * @net: net namespace
 *
 * The nfs_server must be quiescent before this function is invoked.
 * Either its session is drained (NFSv4.1+), or its transport is
 * plugged and drained (NFSv4.0).
 *
 * Returns zero on success, or a negative errno value.
 */
int nfs4_update_server(struct nfs_server *server, const char *hostname,
		       struct sockaddr_storage *sap, size_t salen, struct net *net)
{
	struct nfs_client *clp = server->nfs_client;
	struct rpc_clnt *clnt = server->client;
	struct xprt_create xargs = {
		.ident		= clp->cl_proto,
		.net		= net,
		.dstaddr	= (struct sockaddr *)sap,
		.addrlen	= salen,
		.servername	= hostname,
	};
	char buf[INET6_ADDRSTRLEN + 1];
	struct sockaddr_storage address;
	struct sockaddr *localaddr = (struct sockaddr *)&address;
	int error;

	error = rpc_switch_client_transport(clnt, &xargs, clnt->cl_timeout);
	if (error != 0)
		return error;

	error = rpc_localaddr(clnt, localaddr, sizeof(address));
	if (error != 0)
		return error;

	if (rpc_ntop(localaddr, buf, sizeof(buf)) == 0)
		return -EAFNOSUPPORT;

	nfs_server_remove_lists(server);
	set_bit(NFS_MIG_TSM_POSSIBLE, &server->mig_status);
	error = nfs4_set_client(server, hostname, sap, salen, buf,
				clp->cl_proto, clnt->cl_timeout,
				clp->cl_minorversion,
				clp->cl_nconnect, clp->cl_max_connect, net);
	clear_bit(NFS_MIG_TSM_POSSIBLE, &server->mig_status);
	if (error != 0) {
		nfs_server_insert_lists(server);
		return error;
	}
	nfs_put_client(clp);

	if (server->nfs_client->cl_hostname == NULL) {
		server->nfs_client->cl_hostname = kstrdup(hostname, GFP_KERNEL);
		if (server->nfs_client->cl_hostname == NULL)
			return -ENOMEM;
	}
	nfs_server_insert_lists(server);

	return nfs_probe_server(server, NFS_FH(d_inode(server->super->s_root)));
}
