/*
 * linux/fs/nfs/callback_proc.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback procedures
 */
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/slab.h>
#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "internal.h"

#ifdef NFS_DEBUG
#define NFSDBG_FACILITY NFSDBG_CALLBACK
#endif

__be32 nfs4_callback_getattr(struct cb_getattrargs *args,
			     struct cb_getattrres *res,
			     struct cb_process_state *cps)
{
	struct nfs_delegation *delegation;
	struct nfs_inode *nfsi;
	struct inode *inode;

	res->status = htonl(NFS4ERR_OP_NOT_IN_SESSION);
	if (!cps->clp) /* Always set for v4.0. Set in cb_sequence for v4.1 */
		goto out;

	res->bitmap[0] = res->bitmap[1] = 0;
	res->status = htonl(NFS4ERR_BADHANDLE);

	dprintk("NFS: GETATTR callback request from %s\n",
		rpc_peeraddr2str(cps->clp->cl_rpcclient, RPC_DISPLAY_ADDR));

	inode = nfs_delegation_find_inode(cps->clp, &args->fh);
	if (inode == NULL)
		goto out;
	nfsi = NFS_I(inode);
	rcu_read_lock();
	delegation = rcu_dereference(nfsi->delegation);
	if (delegation == NULL || (delegation->type & FMODE_WRITE) == 0)
		goto out_iput;
	res->size = i_size_read(inode);
	res->change_attr = delegation->change_attr;
	if (nfsi->npages != 0)
		res->change_attr++;
	res->ctime = inode->i_ctime;
	res->mtime = inode->i_mtime;
	res->bitmap[0] = (FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE) &
		args->bitmap[0];
	res->bitmap[1] = (FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY) &
		args->bitmap[1];
	res->status = 0;
out_iput:
	rcu_read_unlock();
	iput(inode);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(res->status));
	return res->status;
}

__be32 nfs4_callback_recall(struct cb_recallargs *args, void *dummy,
			    struct cb_process_state *cps)
{
	struct inode *inode;
	__be32 res;
	
	res = htonl(NFS4ERR_OP_NOT_IN_SESSION);
	if (!cps->clp) /* Always set for v4.0. Set in cb_sequence for v4.1 */
		goto out;

	dprintk("NFS: RECALL callback request from %s\n",
		rpc_peeraddr2str(cps->clp->cl_rpcclient, RPC_DISPLAY_ADDR));

	res = htonl(NFS4ERR_BADHANDLE);
	inode = nfs_delegation_find_inode(cps->clp, &args->fh);
	if (inode == NULL)
		goto out;
	/* Set up a helper thread to actually return the delegation */
	switch (nfs_async_inode_return_delegation(inode, &args->stateid)) {
	case 0:
		res = 0;
		break;
	case -ENOENT:
		if (res != 0)
			res = htonl(NFS4ERR_BAD_STATEID);
		break;
	default:
		res = htonl(NFS4ERR_RESOURCE);
	}
	iput(inode);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(res));
	return res;
}

int nfs4_validate_delegation_stateid(struct nfs_delegation *delegation, const nfs4_stateid *stateid)
{
	if (delegation == NULL || memcmp(delegation->stateid.data, stateid->data,
					 sizeof(delegation->stateid.data)) != 0)
		return 0;
	return 1;
}

#if defined(CONFIG_NFS_V4_1)

int nfs41_validate_delegation_stateid(struct nfs_delegation *delegation, const nfs4_stateid *stateid)
{
	if (delegation == NULL)
		return 0;

	if (stateid->stateid.seqid != 0)
		return 0;
	if (memcmp(&delegation->stateid.stateid.other,
		   &stateid->stateid.other,
		   NFS4_STATEID_OTHER_SIZE))
		return 0;

	return 1;
}

/*
 * Validate the sequenceID sent by the server.
 * Return success if the sequenceID is one more than what we last saw on
 * this slot, accounting for wraparound.  Increments the slot's sequence.
 *
 * We don't yet implement a duplicate request cache, instead we set the
 * back channel ca_maxresponsesize_cached to zero. This is OK for now
 * since we only currently implement idempotent callbacks anyway.
 *
 * We have a single slot backchannel at this time, so we don't bother
 * checking the used_slots bit array on the table.  The lower layer guarantees
 * a single outstanding callback request at a time.
 */
static __be32
validate_seqid(struct nfs4_slot_table *tbl, struct cb_sequenceargs * args)
{
	struct nfs4_slot *slot;

	dprintk("%s enter. slotid %d seqid %d\n",
		__func__, args->csa_slotid, args->csa_sequenceid);

	if (args->csa_slotid > NFS41_BC_MAX_CALLBACKS)
		return htonl(NFS4ERR_BADSLOT);

	slot = tbl->slots + args->csa_slotid;
	dprintk("%s slot table seqid: %d\n", __func__, slot->seq_nr);

	/* Normal */
	if (likely(args->csa_sequenceid == slot->seq_nr + 1)) {
		slot->seq_nr++;
		return htonl(NFS4_OK);
	}

	/* Replay */
	if (args->csa_sequenceid == slot->seq_nr) {
		dprintk("%s seqid %d is a replay\n",
			__func__, args->csa_sequenceid);
		/* Signal process_op to set this error on next op */
		if (args->csa_cachethis == 0)
			return htonl(NFS4ERR_RETRY_UNCACHED_REP);

		/* The ca_maxresponsesize_cached is 0 with no DRC */
		else if (args->csa_cachethis == 1)
			return htonl(NFS4ERR_REP_TOO_BIG_TO_CACHE);
	}

	/* Wraparound */
	if (args->csa_sequenceid == 1 && (slot->seq_nr + 1) == 0) {
		slot->seq_nr = 1;
		return htonl(NFS4_OK);
	}

	/* Misordered request */
	return htonl(NFS4ERR_SEQ_MISORDERED);
}

/*
 * For each referring call triple, check the session's slot table for
 * a match.  If the slot is in use and the sequence numbers match, the
 * client is still waiting for a response to the original request.
 */
static bool referring_call_exists(struct nfs_client *clp,
				  uint32_t nrclists,
				  struct referring_call_list *rclists)
{
	bool status = 0;
	int i, j;
	struct nfs4_session *session;
	struct nfs4_slot_table *tbl;
	struct referring_call_list *rclist;
	struct referring_call *ref;

	/*
	 * XXX When client trunking is implemented, this becomes
	 * a session lookup from within the loop
	 */
	session = clp->cl_session;
	tbl = &session->fc_slot_table;

	for (i = 0; i < nrclists; i++) {
		rclist = &rclists[i];
		if (memcmp(session->sess_id.data,
			   rclist->rcl_sessionid.data,
			   NFS4_MAX_SESSIONID_LEN) != 0)
			continue;

		for (j = 0; j < rclist->rcl_nrefcalls; j++) {
			ref = &rclist->rcl_refcalls[j];

			dprintk("%s: sessionid %x:%x:%x:%x sequenceid %u "
				"slotid %u\n", __func__,
				((u32 *)&rclist->rcl_sessionid.data)[0],
				((u32 *)&rclist->rcl_sessionid.data)[1],
				((u32 *)&rclist->rcl_sessionid.data)[2],
				((u32 *)&rclist->rcl_sessionid.data)[3],
				ref->rc_sequenceid, ref->rc_slotid);

			spin_lock(&tbl->slot_tbl_lock);
			status = (test_bit(ref->rc_slotid, tbl->used_slots) &&
				  tbl->slots[ref->rc_slotid].seq_nr ==
					ref->rc_sequenceid);
			spin_unlock(&tbl->slot_tbl_lock);
			if (status)
				goto out;
		}
	}

out:
	return status;
}

__be32 nfs4_callback_sequence(struct cb_sequenceargs *args,
			      struct cb_sequenceres *res,
			      struct cb_process_state *cps)
{
	struct nfs_client *clp;
	int i;
	__be32 status;

	cps->clp = NULL;

	status = htonl(NFS4ERR_BADSESSION);
	/* Incoming session must match the callback session */
	if (memcmp(&args->csa_sessionid, cps->svc_sid, NFS4_MAX_SESSIONID_LEN))
		goto out;

	clp = nfs4_find_client_sessionid(args->csa_addr,
					 &args->csa_sessionid, 1);
	if (clp == NULL)
		goto out;

	status = validate_seqid(&clp->cl_session->bc_slot_table, args);
	if (status)
		goto out;

	/*
	 * Check for pending referring calls.  If a match is found, a
	 * related callback was received before the response to the original
	 * call.
	 */
	if (referring_call_exists(clp, args->csa_nrclists, args->csa_rclists)) {
		status = htonl(NFS4ERR_DELAY);
		goto out;
	}

	memcpy(&res->csr_sessionid, &args->csa_sessionid,
	       sizeof(res->csr_sessionid));
	res->csr_sequenceid = args->csa_sequenceid;
	res->csr_slotid = args->csa_slotid;
	res->csr_highestslotid = NFS41_BC_MAX_CALLBACKS - 1;
	res->csr_target_highestslotid = NFS41_BC_MAX_CALLBACKS - 1;
	cps->clp = clp; /* put in nfs4_callback_compound */

out:
	for (i = 0; i < args->csa_nrclists; i++)
		kfree(args->csa_rclists[i].rcl_refcalls);
	kfree(args->csa_rclists);

	if (status == htonl(NFS4ERR_RETRY_UNCACHED_REP)) {
		cps->drc_status = status;
		status = 0;
	} else
		res->csr_status = status;

	dprintk("%s: exit with status = %d res->csr_status %d\n", __func__,
		ntohl(status), ntohl(res->csr_status));
	return status;
}

__be32 nfs4_callback_recallany(struct cb_recallanyargs *args, void *dummy,
			       struct cb_process_state *cps)
{
	__be32 status;
	fmode_t flags = 0;

	status = htonl(NFS4ERR_OP_NOT_IN_SESSION);
	if (!cps->clp) /* set in cb_sequence */
		goto out;

	dprintk("NFS: RECALL_ANY callback request from %s\n",
		rpc_peeraddr2str(cps->clp->cl_rpcclient, RPC_DISPLAY_ADDR));

	if (test_bit(RCA4_TYPE_MASK_RDATA_DLG, (const unsigned long *)
		     &args->craa_type_mask))
		flags = FMODE_READ;
	if (test_bit(RCA4_TYPE_MASK_WDATA_DLG, (const unsigned long *)
		     &args->craa_type_mask))
		flags |= FMODE_WRITE;

	if (flags)
		nfs_expire_all_delegation_types(cps->clp, flags);
	status = htonl(NFS4_OK);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}

/* Reduce the fore channel's max_slots to the target value */
__be32 nfs4_callback_recallslot(struct cb_recallslotargs *args, void *dummy,
				struct cb_process_state *cps)
{
	struct nfs4_slot_table *fc_tbl;
	__be32 status;

	status = htonl(NFS4ERR_OP_NOT_IN_SESSION);
	if (!cps->clp) /* set in cb_sequence */
		goto out;

	dprintk("NFS: CB_RECALL_SLOT request from %s target max slots %d\n",
		rpc_peeraddr2str(cps->clp->cl_rpcclient, RPC_DISPLAY_ADDR),
		args->crsa_target_max_slots);

	fc_tbl = &cps->clp->cl_session->fc_slot_table;

	status = htonl(NFS4ERR_BAD_HIGH_SLOT);
	if (args->crsa_target_max_slots > fc_tbl->max_slots ||
	    args->crsa_target_max_slots < 1)
		goto out;

	status = htonl(NFS4_OK);
	if (args->crsa_target_max_slots == fc_tbl->max_slots)
		goto out;

	fc_tbl->target_max_slots = args->crsa_target_max_slots;
	nfs41_handle_recall_slot(cps->clp);
out:
	dprintk("%s: exit with status = %d\n", __func__, ntohl(status));
	return status;
}
#endif /* CONFIG_NFS_V4_1 */
