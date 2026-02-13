/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "nfs4session.h"
#include "callback.h"
#include "delegation.h"
#include "internal.h"
#include "netns.h"
#include "nfs40.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

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

static bool nfs4_same_verifier(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return memcmp(v1->data, v2->data, sizeof(v1->data)) == 0;
}

void nfs40_shutdown_client(struct nfs_client *clp)
{
	if (clp->cl_slot_tbl) {
		nfs4_shutdown_slot_table(clp->cl_slot_tbl);
		kfree(clp->cl_slot_tbl);
	}
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

/*
 * nfs40_handle_cb_pathdown - return all delegations after NFS4ERR_CB_PATH_DOWN
 * @clp: client to process
 *
 * Set the NFS4CLNT_LEASE_EXPIRED state in order to force a
 * resend of the SETCLIENTID and hence re-establish the
 * callback channel. Then return all existing delegations.
 */
void nfs40_handle_cb_pathdown(struct nfs_client *clp)
{
	set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
	nfs_expire_all_delegations(clp);
	dprintk("%s: handling CB_PATHDOWN recovery for server %s\n", __func__,
			clp->cl_hostname);
}

void nfs4_schedule_path_down_recovery(struct nfs_client *clp)
{
	nfs40_handle_cb_pathdown(clp);
	nfs4_schedule_state_manager(clp);
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
static int nfs40_walk_client_list(struct nfs_client *new,
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

/**
 * nfs40_discover_server_trunking - Detect server IP address trunking (mv0)
 *
 * @clp: nfs_client under test
 * @result: OUT: found nfs_client, or clp
 * @cred: credential to use for trunking test
 *
 * Returns zero, a negative errno, or a negative NFS4ERR status.
 * If zero is returned, an nfs_client pointer is planted in
 * "result".
 *
 * Note: The returned client may not yet be marked ready.
 */
int nfs40_discover_server_trunking(struct nfs_client *clp,
				   struct nfs_client **result,
				   const struct cred *cred)
{
	struct nfs4_setclientid_res clid = {
		.clientid = clp->cl_clientid,
		.confirm = clp->cl_confirm,
	};
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
	unsigned short port;
	int status;

	port = nn->nfs_callback_tcpport;
	if (clp->cl_addr.ss_family == AF_INET6)
		port = nn->nfs_callback_tcpport6;

	status = nfs4_proc_setclientid(clp, NFS4_CALLBACK, port, cred, &clid);
	if (status != 0)
		goto out;
	clp->cl_clientid = clid.clientid;
	clp->cl_confirm = clid.confirm;

	status = nfs40_walk_client_list(clp, result, cred);
	if (status == 0) {
		/* Sustain the lease, even if it's empty.  If the clientid4
		 * goes stale it's of no use for trunking discovery. */
		nfs4_schedule_state_renewal(*result);

		/* If the client state need to recover, do it. */
		if (clp->cl_state)
			nfs4_schedule_state_manager(clp);
	}
out:
	return status;
}
