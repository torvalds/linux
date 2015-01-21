/*
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <linux/module.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
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
	ret = idr_alloc(&nn->cb_ident_idr, clp, 0, 0, GFP_NOWAIT);
	if (ret >= 0)
		clp->cl_cb_ident = ret;
	spin_unlock(&nn->nfs_client_lock);
	idr_preload_end();
	return ret < 0 ? ret : 0;
}

#ifdef CONFIG_NFS_V4_1
/**
 * Per auth flavor data server rpc clients
 */
struct nfs4_ds_server {
	struct list_head	list;   /* ds_clp->cl_ds_clients */
	struct rpc_clnt		*rpc_clnt;
};

/**
 * Common lookup case for DS I/O
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
	LIST_HEAD(shutdown_list);

	while (!list_empty(&clp->cl_ds_clients)) {
		dss = list_entry(clp->cl_ds_clients.next,
					struct nfs4_ds_server, list);
		list_del(&dss->list);
		rpc_shutdown_client(dss->rpc_clnt);
		kfree (dss);
	}
}

void nfs41_shutdown_client(struct nfs_client *clp)
{
	if (nfs4_has_session(clp)) {
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
	int err;
	struct nfs_client *clp = nfs_alloc_client(cl_init);
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
	clp->cl_minorversion = cl_init->minorversion;
	clp->cl_mvops = nfs_v4_minor_ops[cl_init->minorversion];
	clp->cl_mig_gen = 1;
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
	int error;

	if (clp->rpc_ops->version == 4) {
		struct rpc_xprt *xprt;

		xprt = rcu_dereference_raw(clp->cl_rpcclient->cl_xprt);

		if (nfs4_has_session(clp)) {
			error = xprt_setup_backchannel(xprt,
						NFS41_BC_MIN_CALLBACKS);
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
	}
	return 0;
}

/**
 * nfs40_init_client - nfs_client initialization tasks for NFSv4.0
 * @clp - nfs_client to initialize
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
		kfree(tbl);
		return ret;
	}

	clp->cl_slot_tbl = tbl;
	return 0;
}

#if defined(CONFIG_NFS_V4_1)

/**
 * nfs41_init_client - nfs_client initialization tasks for NFSv4.1+
 * @clp - nfs_client to initialize
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

/**
 * nfs4_init_client - Initialise an NFS4 client record
 *
 * @clp: nfs_client to initialise
 * @timeparms: timeout parameters for underlying RPC transport
 * @ip_addr: callback IP address in presentation format
 * @authflavor: authentication flavor for underlying RPC transport
 *
 * Returns pointer to an NFS client, or an ERR_PTR value.
 */
struct nfs_client *nfs4_init_client(struct nfs_client *clp,
				    const struct rpc_timeout *timeparms,
				    const char *ip_addr)
{
	char buf[INET6_ADDRSTRLEN + 1];
	struct nfs_client *old;
	int error;

	if (clp->cl_cons_state == NFS_CS_READY) {
		/* the client is initialised already */
		dprintk("<-- nfs4_init_client() = 0 [already %p]\n", clp);
		return clp;
	}

	/* Check NFS protocol revision and initialize RPC op vector */
	clp->rpc_ops = &nfs_v4_clientops;

	if (clp->cl_minorversion != 0)
		__set_bit(NFS_CS_INFINITE_SLOTS, &clp->cl_flags);
	__set_bit(NFS_CS_DISCRTRY, &clp->cl_flags);
	__set_bit(NFS_CS_NO_RETRANS_TIMEOUT, &clp->cl_flags);

	error = nfs_create_rpc_client(clp, timeparms, RPC_AUTH_GSS_KRB5I);
	if (error == -EINVAL)
		error = nfs_create_rpc_client(clp, timeparms, RPC_AUTH_UNIX);
	if (error < 0)
		goto error;

	/* If no clientaddr= option was specified, find a usable cb address */
	if (ip_addr == NULL) {
		struct sockaddr_storage cb_addr;
		struct sockaddr *sap = (struct sockaddr *)&cb_addr;

		error = rpc_localaddr(clp->cl_rpcclient, sap, sizeof(cb_addr));
		if (error < 0)
			goto error;
		error = rpc_ntop(sap, buf, sizeof(buf));
		if (error < 0)
			goto error;
		ip_addr = (const char *)buf;
	}
	strlcpy(clp->cl_ipaddr, ip_addr, sizeof(clp->cl_ipaddr));

	error = nfs_idmap_new(clp);
	if (error < 0) {
		dprintk("%s: failed to create idmapper. Error = %d\n",
			__func__, error);
		goto error;
	}
	__set_bit(NFS_CS_IDMAP, &clp->cl_res_state);

	error = nfs4_init_client_minor_version(clp);
	if (error < 0)
		goto error;

	if (!nfs4_has_session(clp))
		nfs_mark_client_ready(clp, NFS_CS_READY);

	error = nfs4_discover_server_trunking(clp, &old);
	if (error < 0)
		goto error;

	if (clp != old)
		clp->cl_preserve_clid = true;
	nfs_put_client(clp);
	return old;

error:
	nfs_mark_client_ready(clp, error);
	nfs_put_client(clp);
	dprintk("<-- nfs4_init_client() = xerror %d\n", error);
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
			   struct rpc_cred *cred)
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

		if (pos->rpc_ops != new->rpc_ops)
			continue;

		if (pos->cl_proto != new->cl_proto)
			continue;

		if (pos->cl_minorversion != new->cl_minorversion)
			continue;

		/* If "pos" isn't marked ready, we can't trust the
		 * remaining fields in "pos" */
		if (pos->cl_cons_state > NFS_CS_READY) {
			atomic_inc(&pos->cl_count);
			spin_unlock(&nn->nfs_client_lock);

			if (prev)
				nfs_put_client(prev);
			prev = pos;

			status = nfs_wait_client_init_complete(pos);
			if (status < 0)
				goto out;
			status = -NFS4ERR_STALE_CLIENTID;
			spin_lock(&nn->nfs_client_lock);
		}
		if (pos->cl_cons_state != NFS_CS_READY)
			continue;

		if (pos->cl_clientid != new->cl_clientid)
			continue;

		atomic_inc(&pos->cl_count);
		spin_unlock(&nn->nfs_client_lock);

		if (prev)
			nfs_put_client(prev);
		prev = pos;

		status = nfs4_proc_setclientid_confirm(pos, &clid, cred);
		switch (status) {
		case -NFS4ERR_STALE_CLIENTID:
			break;
		case 0:
			nfs4_swap_callback_idents(pos, new);

			prev = NULL;
			*result = pos;
			dprintk("NFS: <-- %s using nfs_client = %p ({%d})\n",
				__func__, pos, atomic_read(&pos->cl_count));
			goto out;
		case -ERESTARTSYS:
		case -ETIMEDOUT:
			/* The callback path may have been inadvertently
			 * changed. Schedule recovery!
			 */
			nfs4_schedule_path_down_recovery(pos);
		default:
			goto out;
		}

		spin_lock(&nn->nfs_client_lock);
	}
	spin_unlock(&nn->nfs_client_lock);

	/* No match found. The server lost our clientid */
out:
	if (prev)
		nfs_put_client(prev);
	dprintk("NFS: <-- %s status = %d\n", __func__, status);
	return status;
}

#ifdef CONFIG_NFS_V4_1
/*
 * Returns true if the client IDs match
 */
static bool nfs4_match_clientids(struct nfs_client *a, struct nfs_client *b)
{
	if (a->cl_clientid != b->cl_clientid) {
		dprintk("NFS: --> %s client ID %llx does not match %llx\n",
			__func__, a->cl_clientid, b->cl_clientid);
		return false;
	}
	dprintk("NFS: --> %s client ID %llx matches %llx\n",
		__func__, a->cl_clientid, b->cl_clientid);
	return true;
}

/*
 * Returns true if the server major ids match
 */
static bool
nfs4_check_clientid_trunking(struct nfs_client *a, struct nfs_client *b)
{
	struct nfs41_server_owner *o1 = a->cl_serverowner;
	struct nfs41_server_owner *o2 = b->cl_serverowner;

	if (o1->major_id_sz != o2->major_id_sz)
		goto out_major_mismatch;
	if (memcmp(o1->major_id, o2->major_id, o1->major_id_sz) != 0)
		goto out_major_mismatch;

	dprintk("NFS: --> %s server owners match\n", __func__);
	return true;

out_major_mismatch:
	dprintk("NFS: --> %s server owner major IDs do not match\n",
		__func__);
	return false;
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
			   struct rpc_cred *cred)
{
	struct nfs_net *nn = net_generic(new->cl_net, nfs_net_id);
	struct nfs_client *pos, *prev = NULL;
	int status = -NFS4ERR_STALE_CLIENTID;

	spin_lock(&nn->nfs_client_lock);
	list_for_each_entry(pos, &nn->nfs_client_list, cl_share_link) {

		if (pos->rpc_ops != new->rpc_ops)
			continue;

		if (pos->cl_proto != new->cl_proto)
			continue;

		if (pos->cl_minorversion != new->cl_minorversion)
			continue;

		/* If "pos" isn't marked ready, we can't trust the
		 * remaining fields in "pos", especially the client
		 * ID and serverowner fields.  Wait for CREATE_SESSION
		 * to finish. */
		if (pos->cl_cons_state > NFS_CS_READY) {
			atomic_inc(&pos->cl_count);
			spin_unlock(&nn->nfs_client_lock);

			if (prev)
				nfs_put_client(prev);
			prev = pos;

			status = nfs_wait_client_init_complete(pos);
			if (pos->cl_cons_state == NFS_CS_SESSION_INITING) {
				nfs4_schedule_lease_recovery(pos);
				status = nfs4_wait_clnt_recover(pos);
			}
			spin_lock(&nn->nfs_client_lock);
			if (status < 0)
				break;
			status = -NFS4ERR_STALE_CLIENTID;
		}
		if (pos->cl_cons_state != NFS_CS_READY)
			continue;

		if (!nfs4_match_clientids(pos, new))
			continue;

		/*
		 * Note that session trunking is just a special subcase of
		 * client id trunking. In either case, we want to fall back
		 * to using the existing nfs_client.
		 */
		if (!nfs4_check_clientid_trunking(pos, new))
			continue;

		atomic_inc(&pos->cl_count);
		*result = pos;
		status = 0;
		dprintk("NFS: <-- %s using nfs_client = %p ({%d})\n",
			__func__, pos, atomic_read(&pos->cl_count));
		break;
	}

	/* No matching nfs_client found. */
	spin_unlock(&nn->nfs_client_lock);
	dprintk("NFS: <-- %s status = %d\n", __func__, status);
	if (prev)
		nfs_put_client(prev);
	return status;
}
#endif	/* CONFIG_NFS_V4_1 */

static void nfs4_destroy_server(struct nfs_server *server)
{
	nfs_server_return_all_delegations(server);
	unset_pnfs_layoutdriver(server);
	nfs4_purge_state_owners(server);
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
		atomic_inc(&clp->cl_count);
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
	if (!nfs_sockaddr_match_ipaddr(addr, clap))
		return false;

	return true;
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
		if (nfs4_cb_match_client(addr, clp, minorversion) == false)
			continue;

		if (!nfs4_has_session(clp))
			continue;

		/* Match sessionid*/
		if (memcmp(clp->cl_session->sess_id.data,
		    sid->data, NFS4_MAX_SESSIONID_LEN) != 0)
			continue;

		atomic_inc(&clp->cl_count);
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
		const struct sockaddr *addr,
		const size_t addrlen,
		const char *ip_addr,
		rpc_authflavor_t authflavour,
		int proto, const struct rpc_timeout *timeparms,
		u32 minorversion, struct net *net)
{
	struct nfs_client_initdata cl_init = {
		.hostname = hostname,
		.addr = addr,
		.addrlen = addrlen,
		.nfs_mod = &nfs_v4,
		.proto = proto,
		.minorversion = minorversion,
		.net = net,
	};
	struct nfs_client *clp;
	int error;

	dprintk("--> nfs4_set_client()\n");

	if (server->flags & NFS_MOUNT_NORESVPORT)
		set_bit(NFS_CS_NORESVPORT, &cl_init.init_flags);
	if (server->options & NFS_OPTION_MIGRATION)
		set_bit(NFS_CS_MIGRATION, &cl_init.init_flags);

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(&cl_init, timeparms, ip_addr, authflavour);
	if (IS_ERR(clp)) {
		error = PTR_ERR(clp);
		goto error;
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
	dprintk("<-- nfs4_set_client() = 0 [new %p]\n", clp);
	return 0;
error:
	dprintk("<-- nfs4_set_client() = xerror %d\n", error);
	return error;
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
struct nfs_client *nfs4_set_ds_client(struct nfs_client* mds_clp,
		const struct sockaddr *ds_addr, int ds_addrlen,
		int ds_proto, unsigned int ds_timeo, unsigned int ds_retrans)
{
	struct nfs_client_initdata cl_init = {
		.addr = ds_addr,
		.addrlen = ds_addrlen,
		.nfs_mod = &nfs_v4,
		.proto = ds_proto,
		.minorversion = mds_clp->cl_minorversion,
		.net = mds_clp->cl_net,
	};
	struct rpc_timeout ds_timeout;
	struct nfs_client *clp;
	char buf[INET6_ADDRSTRLEN + 1];

	if (rpc_ntop(ds_addr, buf, sizeof(buf)) <= 0)
		return ERR_PTR(-EINVAL);
	cl_init.hostname = buf;

	/*
	 * Set an authflavor equual to the MDS value. Use the MDS nfs_client
	 * cl_ipaddr so as to use the same EXCHANGE_ID co_ownerid as the MDS
	 * (section 13.1 RFC 5661).
	 */
	nfs_init_timeout_values(&ds_timeout, ds_proto, ds_timeo, ds_retrans);
	clp = nfs_get_client(&cl_init, &ds_timeout, mds_clp->cl_ipaddr,
			     mds_clp->cl_rpcclient->cl_auth->au_flavor);

	dprintk("<-- %s %p\n", __func__, clp);
	return clp;
}
EXPORT_SYMBOL_GPL(nfs4_set_ds_client);

/*
 * Session has been established, and the client marked ready.
 * Set the mount rsize and wsize with negotiated fore channel
 * attributes which will be bound checked in nfs_server_set_fsinfo.
 */
static void nfs4_session_set_rwsize(struct nfs_server *server)
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

	if (server->rsize > server_resp_sz)
		server->rsize = server_resp_sz;
	if (server->wsize > server_rqst_sz)
		server->wsize = server_rqst_sz;
#endif /* CONFIG_NFS_V4_1 */
}

static int nfs4_server_common_setup(struct nfs_server *server,
		struct nfs_fh *mntfh, bool auth_probe)
{
	struct nfs_fattr *fattr;
	int error;

	/* data servers support only a subset of NFSv4.1 */
	if (is_ds_only_client(server->nfs_client))
		return -EPROTONOSUPPORT;

	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		return -ENOMEM;

	/* We must ensure the session is initialised first */
	error = nfs4_init_session(server->nfs_client);
	if (error < 0)
		goto out;

	/* Set the basic capabilities */
	server->caps |= server->nfs_client->cl_mvops->init_caps;
	if (server->flags & NFS_MOUNT_NORDIRPLUS)
			server->caps &= ~NFS_CAP_READDIRPLUS;
	/*
	 * Don't use NFS uid/gid mapping if we're using AUTH_SYS or lower
	 * authentication.
	 */
	if (nfs4_disable_idmapping &&
			server->client->cl_auth->au_flavor == RPC_AUTH_UNIX)
		server->caps |= NFS_CAP_UIDGID_NOMAP;


	/* Probe the root fh to retrieve its FSID and filehandle */
	error = nfs4_get_rootfh(server, mntfh, auth_probe);
	if (error < 0)
		goto out;

	dprintk("Server FSID: %llx:%llx\n",
			(unsigned long long) server->fsid.major,
			(unsigned long long) server->fsid.minor);
	nfs_display_fhandle(mntfh, "Pseudo-fs root FH");

	nfs4_session_set_rwsize(server);

	error = nfs_probe_fsinfo(server, mntfh, fattr);
	if (error < 0)
		goto out;

	if (server->namelen == 0 || server->namelen > NFS4_MAXNAMLEN)
		server->namelen = NFS4_MAXNAMLEN;

	nfs_server_insert_lists(server);
	server->mount_time = jiffies;
	server->destroy = nfs4_destroy_server;
out:
	nfs_free_fattr(fattr);
	return error;
}

/*
 * Create a version 4 volume record
 */
static int nfs4_init_server(struct nfs_server *server,
		struct nfs_parsed_mount_data *data)
{
	struct rpc_timeout timeparms;
	int error;

	dprintk("--> nfs4_init_server()\n");

	nfs_init_timeout_values(&timeparms, data->nfs_server.protocol,
			data->timeo, data->retrans);

	/* Initialise the client representation from the mount data */
	server->flags = data->flags;
	server->options = data->options;
	server->auth_info = data->auth_info;

	/* Use the first specified auth flavor. If this flavor isn't
	 * allowed by the server, use the SECINFO path to try the
	 * other specified flavors */
	if (data->auth_info.flavor_len >= 1)
		data->selected_flavor = data->auth_info.flavors[0];
	else
		data->selected_flavor = RPC_AUTH_UNIX;

	/* Get a client record */
	error = nfs4_set_client(server,
			data->nfs_server.hostname,
			(const struct sockaddr *)&data->nfs_server.address,
			data->nfs_server.addrlen,
			data->client_address,
			data->selected_flavor,
			data->nfs_server.protocol,
			&timeparms,
			data->minorversion,
			data->net);
	if (error < 0)
		goto error;

	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);

	server->acregmin = data->acregmin * HZ;
	server->acregmax = data->acregmax * HZ;
	server->acdirmin = data->acdirmin * HZ;
	server->acdirmax = data->acdirmax * HZ;

	server->port = data->nfs_server.port;

	error = nfs_init_server_rpcclient(server, &timeparms,
					  data->selected_flavor);

error:
	/* Done */
	dprintk("<-- nfs4_init_server() = %d\n", error);
	return error;
}

/*
 * Create a version 4 volume record
 * - keyed on server and FSID
 */
/*struct nfs_server *nfs4_create_server(const struct nfs_parsed_mount_data *data,
				      struct nfs_fh *mntfh)*/
struct nfs_server *nfs4_create_server(struct nfs_mount_info *mount_info,
				      struct nfs_subversion *nfs_mod)
{
	struct nfs_server *server;
	bool auth_probe;
	int error;

	dprintk("--> nfs4_create_server()\n");

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	auth_probe = mount_info->parsed->auth_info.flavor_len < 1;

	/* set up the general RPC client */
	error = nfs4_init_server(server, mount_info->parsed);
	if (error < 0)
		goto error;

	error = nfs4_server_common_setup(server, mount_info->mntfh, auth_probe);
	if (error < 0)
		goto error;

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
					       struct nfs_fh *mntfh)
{
	struct nfs_client *parent_client;
	struct nfs_server *server, *parent_server;
	bool auth_probe;
	int error;

	dprintk("--> nfs4_create_referral_server()\n");

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	parent_server = NFS_SB(data->sb);
	parent_client = parent_server->nfs_client;

	/* Initialise the client representation from the parent server */
	nfs_server_copy_userdata(server, parent_server);

	/* Get a client representation.
	 * Note: NFSv4 always uses TCP, */
	error = nfs4_set_client(server, data->hostname,
				data->addr,
				data->addrlen,
				parent_client->cl_ipaddr,
				data->authflavor,
				rpc_protocol(parent_server->client),
				parent_server->client->cl_timeout,
				parent_client->cl_mvops->minor_version,
				parent_client->cl_net);
	if (error < 0)
		goto error;

	error = nfs_init_server_rpcclient(server, parent_server->client->cl_timeout, data->authflavor);
	if (error < 0)
		goto error;

	auth_probe = parent_server->auth_info.flavor_len < 1;

	error = nfs4_server_common_setup(server, mntfh, auth_probe);
	if (error < 0)
		goto error;

	dprintk("<-- nfs_create_referral_server() = %p\n", server);
	return server;

error:
	nfs_free_server(server);
	dprintk("<-- nfs4_create_referral_server() = error %d\n", error);
	return ERR_PTR(error);
}

/*
 * Grab the destination's particulars, including lease expiry time.
 *
 * Returns zero if probe succeeded and retrieved FSID matches the FSID
 * we have cached.
 */
static int nfs_probe_destination(struct nfs_server *server)
{
	struct inode *inode = server->super->s_root->d_inode;
	struct nfs_fattr *fattr;
	int error;

	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		return -ENOMEM;

	/* Sanity: the probe won't work if the destination server
	 * does not recognize the migrated FH. */
	error = nfs_probe_fsinfo(server, NFS_FH(inode), fattr);

	nfs_free_fattr(fattr);
	return error;
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
		       struct sockaddr *sap, size_t salen, struct net *net)
{
	struct nfs_client *clp = server->nfs_client;
	struct rpc_clnt *clnt = server->client;
	struct xprt_create xargs = {
		.ident		= clp->cl_proto,
		.net		= net,
		.dstaddr	= sap,
		.addrlen	= salen,
		.servername	= hostname,
	};
	char buf[INET6_ADDRSTRLEN + 1];
	struct sockaddr_storage address;
	struct sockaddr *localaddr = (struct sockaddr *)&address;
	int error;

	dprintk("--> %s: move FSID %llx:%llx to \"%s\")\n", __func__,
			(unsigned long long)server->fsid.major,
			(unsigned long long)server->fsid.minor,
			hostname);

	error = rpc_switch_client_transport(clnt, &xargs, clnt->cl_timeout);
	if (error != 0) {
		dprintk("<-- %s(): rpc_switch_client_transport returned %d\n",
			__func__, error);
		goto out;
	}

	error = rpc_localaddr(clnt, localaddr, sizeof(address));
	if (error != 0) {
		dprintk("<-- %s(): rpc_localaddr returned %d\n",
			__func__, error);
		goto out;
	}

	error = -EAFNOSUPPORT;
	if (rpc_ntop(localaddr, buf, sizeof(buf)) == 0) {
		dprintk("<-- %s(): rpc_ntop returned %d\n",
			__func__, error);
		goto out;
	}

	nfs_server_remove_lists(server);
	error = nfs4_set_client(server, hostname, sap, salen, buf,
				clp->cl_rpcclient->cl_auth->au_flavor,
				clp->cl_proto, clnt->cl_timeout,
				clp->cl_minorversion, net);
	nfs_put_client(clp);
	if (error != 0) {
		nfs_server_insert_lists(server);
		dprintk("<-- %s(): nfs4_set_client returned %d\n",
			__func__, error);
		goto out;
	}

	if (server->nfs_client->cl_hostname == NULL)
		server->nfs_client->cl_hostname = kstrdup(hostname, GFP_KERNEL);
	nfs_server_insert_lists(server);

	error = nfs_probe_destination(server);
	if (error < 0)
		goto out;

	dprintk("<-- %s() succeeded\n", __func__);

out:
	return error;
}
