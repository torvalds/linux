/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "lowcomms.h"
#include "midcomms.h"
#include "rcom.h"
#include "recover.h"
#include "dir.h"
#include "config.h"
#include "memory.h"
#include "lock.h"
#include "util.h"

static int rcom_response(struct dlm_ls *ls)
{
	return test_bit(LSFL_RCOM_READY, &ls->ls_flags);
}

static int create_rcom(struct dlm_ls *ls, int to_nodeid, int type, int len,
		       struct dlm_rcom **rc_ret, struct dlm_mhandle **mh_ret)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	char *mb;
	int mb_len = sizeof(struct dlm_rcom) + len;

	mh = dlm_lowcomms_get_buffer(to_nodeid, mb_len, GFP_NOFS, &mb);
	if (!mh) {
		log_print("create_rcom to %d type %d len %d ENOBUFS",
			  to_nodeid, type, len);
		return -ENOBUFS;
	}
	memset(mb, 0, mb_len);

	rc = (struct dlm_rcom *) mb;

	rc->rc_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	rc->rc_header.h_lockspace = ls->ls_global_id;
	rc->rc_header.h_nodeid = dlm_our_nodeid();
	rc->rc_header.h_length = mb_len;
	rc->rc_header.h_cmd = DLM_RCOM;

	rc->rc_type = type;

	spin_lock(&ls->ls_recover_lock);
	rc->rc_seq = ls->ls_recover_seq;
	spin_unlock(&ls->ls_recover_lock);

	*mh_ret = mh;
	*rc_ret = rc;
	return 0;
}

static void send_rcom(struct dlm_ls *ls, struct dlm_mhandle *mh,
		      struct dlm_rcom *rc)
{
	dlm_rcom_out(rc);
	dlm_lowcomms_commit_buffer(mh);
}

static void set_rcom_status(struct dlm_ls *ls, struct rcom_status *rs,
			    uint32_t flags)
{
	rs->rs_flags = cpu_to_le32(flags);
}

/* When replying to a status request, a node also sends back its
   configuration values.  The requesting node then checks that the remote
   node is configured the same way as itself. */

static void set_rcom_config(struct dlm_ls *ls, struct rcom_config *rf,
			    uint32_t num_slots)
{
	rf->rf_lvblen = cpu_to_le32(ls->ls_lvblen);
	rf->rf_lsflags = cpu_to_le32(ls->ls_exflags);

	rf->rf_our_slot = cpu_to_le16(ls->ls_slot);
	rf->rf_num_slots = cpu_to_le16(num_slots);
	rf->rf_generation =  cpu_to_le32(ls->ls_generation);
}

static int check_rcom_config(struct dlm_ls *ls, struct dlm_rcom *rc, int nodeid)
{
	struct rcom_config *rf = (struct rcom_config *) rc->rc_buf;

	if ((rc->rc_header.h_version & 0xFFFF0000) != DLM_HEADER_MAJOR) {
		log_error(ls, "version mismatch: %x nodeid %d: %x",
			  DLM_HEADER_MAJOR | DLM_HEADER_MINOR, nodeid,
			  rc->rc_header.h_version);
		return -EPROTO;
	}

	if (le32_to_cpu(rf->rf_lvblen) != ls->ls_lvblen ||
	    le32_to_cpu(rf->rf_lsflags) != ls->ls_exflags) {
		log_error(ls, "config mismatch: %d,%x nodeid %d: %d,%x",
			  ls->ls_lvblen, ls->ls_exflags, nodeid,
			  le32_to_cpu(rf->rf_lvblen),
			  le32_to_cpu(rf->rf_lsflags));
		return -EPROTO;
	}
	return 0;
}

static void allow_sync_reply(struct dlm_ls *ls, uint64_t *new_seq)
{
	spin_lock(&ls->ls_rcom_spin);
	*new_seq = ++ls->ls_rcom_seq;
	set_bit(LSFL_RCOM_WAIT, &ls->ls_flags);
	spin_unlock(&ls->ls_rcom_spin);
}

static void disallow_sync_reply(struct dlm_ls *ls)
{
	spin_lock(&ls->ls_rcom_spin);
	clear_bit(LSFL_RCOM_WAIT, &ls->ls_flags);
	clear_bit(LSFL_RCOM_READY, &ls->ls_flags);
	spin_unlock(&ls->ls_rcom_spin);
}

/*
 * low nodeid gathers one slot value at a time from each node.
 * it sets need_slots=0, and saves rf_our_slot returned from each
 * rcom_config.
 *
 * other nodes gather all slot values at once from the low nodeid.
 * they set need_slots=1, and ignore the rf_our_slot returned from each
 * rcom_config.  they use the rf_num_slots returned from the low
 * node's rcom_config.
 */

int dlm_rcom_status(struct dlm_ls *ls, int nodeid, uint32_t status_flags)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error = 0;

	ls->ls_recover_nodeid = nodeid;

	if (nodeid == dlm_our_nodeid()) {
		rc = ls->ls_recover_buf;
		rc->rc_result = dlm_recover_status(ls);
		goto out;
	}

retry:
	error = create_rcom(ls, nodeid, DLM_RCOM_STATUS,
			    sizeof(struct rcom_status), &rc, &mh);
	if (error)
		goto out;

	set_rcom_status(ls, (struct rcom_status *)rc->rc_buf, status_flags);

	allow_sync_reply(ls, &rc->rc_id);
	memset(ls->ls_recover_buf, 0, dlm_config.ci_buffer_size);

	send_rcom(ls, mh, rc);

	error = dlm_wait_function(ls, &rcom_response);
	disallow_sync_reply(ls);
	if (error == -ETIMEDOUT)
		goto retry;
	if (error)
		goto out;

	rc = ls->ls_recover_buf;

	if (rc->rc_result == -ESRCH) {
		/* we pretend the remote lockspace exists with 0 status */
		log_debug(ls, "remote node %d not ready", nodeid);
		rc->rc_result = 0;
		error = 0;
	} else {
		error = check_rcom_config(ls, rc, nodeid);
	}

	/* the caller looks at rc_result for the remote recovery status */
 out:
	return error;
}

static void receive_rcom_status(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	struct rcom_status *rs;
	uint32_t status;
	int nodeid = rc_in->rc_header.h_nodeid;
	int len = sizeof(struct rcom_config);
	int num_slots = 0;
	int error;

	if (!dlm_slots_version(&rc_in->rc_header)) {
		status = dlm_recover_status(ls);
		goto do_create;
	}

	rs = (struct rcom_status *)rc_in->rc_buf;

	if (!(le32_to_cpu(rs->rs_flags) & DLM_RSF_NEED_SLOTS)) {
		status = dlm_recover_status(ls);
		goto do_create;
	}

	spin_lock(&ls->ls_recover_lock);
	status = ls->ls_recover_status;
	num_slots = ls->ls_num_slots;
	spin_unlock(&ls->ls_recover_lock);
	len += num_slots * sizeof(struct rcom_slot);

 do_create:
	error = create_rcom(ls, nodeid, DLM_RCOM_STATUS_REPLY,
			    len, &rc, &mh);
	if (error)
		return;

	rc->rc_id = rc_in->rc_id;
	rc->rc_seq_reply = rc_in->rc_seq;
	rc->rc_result = status;

	set_rcom_config(ls, (struct rcom_config *)rc->rc_buf, num_slots);

	if (!num_slots)
		goto do_send;

	spin_lock(&ls->ls_recover_lock);
	if (ls->ls_num_slots != num_slots) {
		spin_unlock(&ls->ls_recover_lock);
		log_debug(ls, "receive_rcom_status num_slots %d to %d",
			  num_slots, ls->ls_num_slots);
		rc->rc_result = 0;
		set_rcom_config(ls, (struct rcom_config *)rc->rc_buf, 0);
		goto do_send;
	}

	dlm_slots_copy_out(ls, rc);
	spin_unlock(&ls->ls_recover_lock);

 do_send:
	send_rcom(ls, mh, rc);
}

static void receive_sync_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	spin_lock(&ls->ls_rcom_spin);
	if (!test_bit(LSFL_RCOM_WAIT, &ls->ls_flags) ||
	    rc_in->rc_id != ls->ls_rcom_seq) {
		log_debug(ls, "reject reply %d from %d seq %llx expect %llx",
			  rc_in->rc_type, rc_in->rc_header.h_nodeid,
			  (unsigned long long)rc_in->rc_id,
			  (unsigned long long)ls->ls_rcom_seq);
		goto out;
	}
	memcpy(ls->ls_recover_buf, rc_in, rc_in->rc_header.h_length);
	set_bit(LSFL_RCOM_READY, &ls->ls_flags);
	clear_bit(LSFL_RCOM_WAIT, &ls->ls_flags);
	wake_up(&ls->ls_wait_general);
 out:
	spin_unlock(&ls->ls_rcom_spin);
}

int dlm_rcom_names(struct dlm_ls *ls, int nodeid, char *last_name, int last_len)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error = 0;

	ls->ls_recover_nodeid = nodeid;

retry:
	error = create_rcom(ls, nodeid, DLM_RCOM_NAMES, last_len, &rc, &mh);
	if (error)
		goto out;
	memcpy(rc->rc_buf, last_name, last_len);

	allow_sync_reply(ls, &rc->rc_id);
	memset(ls->ls_recover_buf, 0, dlm_config.ci_buffer_size);

	send_rcom(ls, mh, rc);

	error = dlm_wait_function(ls, &rcom_response);
	disallow_sync_reply(ls);
	if (error == -ETIMEDOUT)
		goto retry;
 out:
	return error;
}

static void receive_rcom_names(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, inlen, outlen, nodeid;

	nodeid = rc_in->rc_header.h_nodeid;
	inlen = rc_in->rc_header.h_length - sizeof(struct dlm_rcom);
	outlen = dlm_config.ci_buffer_size - sizeof(struct dlm_rcom);

	error = create_rcom(ls, nodeid, DLM_RCOM_NAMES_REPLY, outlen, &rc, &mh);
	if (error)
		return;
	rc->rc_id = rc_in->rc_id;
	rc->rc_seq_reply = rc_in->rc_seq;

	dlm_copy_master_names(ls, rc_in->rc_buf, inlen, rc->rc_buf, outlen,
			      nodeid);
	send_rcom(ls, mh, rc);
}

int dlm_send_rcom_lookup(struct dlm_rsb *r, int dir_nodeid)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	struct dlm_ls *ls = r->res_ls;
	int error;

	error = create_rcom(ls, dir_nodeid, DLM_RCOM_LOOKUP, r->res_length,
			    &rc, &mh);
	if (error)
		goto out;
	memcpy(rc->rc_buf, r->res_name, r->res_length);
	rc->rc_id = (unsigned long) r->res_id;

	send_rcom(ls, mh, rc);
 out:
	return error;
}

static void receive_rcom_lookup(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, ret_nodeid, nodeid = rc_in->rc_header.h_nodeid;
	int len = rc_in->rc_header.h_length - sizeof(struct dlm_rcom);

	error = create_rcom(ls, nodeid, DLM_RCOM_LOOKUP_REPLY, 0, &rc, &mh);
	if (error)
		return;

	/* Old code would send this special id to trigger a debug dump. */
	if (rc_in->rc_id == 0xFFFFFFFF) {
		log_error(ls, "receive_rcom_lookup dump from %d", nodeid);
		dlm_dump_rsb_name(ls, rc_in->rc_buf, len);
		return;
	}

	error = dlm_master_lookup(ls, nodeid, rc_in->rc_buf, len,
				  DLM_LU_RECOVER_MASTER, &ret_nodeid, NULL);
	if (error)
		ret_nodeid = error;
	rc->rc_result = ret_nodeid;
	rc->rc_id = rc_in->rc_id;
	rc->rc_seq_reply = rc_in->rc_seq;

	send_rcom(ls, mh, rc);
}

static void receive_rcom_lookup_reply(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	dlm_recover_master_reply(ls, rc_in);
}

static void pack_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb,
			   struct rcom_lock *rl)
{
	memset(rl, 0, sizeof(*rl));

	rl->rl_ownpid = cpu_to_le32(lkb->lkb_ownpid);
	rl->rl_lkid = cpu_to_le32(lkb->lkb_id);
	rl->rl_exflags = cpu_to_le32(lkb->lkb_exflags);
	rl->rl_flags = cpu_to_le32(lkb->lkb_flags);
	rl->rl_lvbseq = cpu_to_le32(lkb->lkb_lvbseq);
	rl->rl_rqmode = lkb->lkb_rqmode;
	rl->rl_grmode = lkb->lkb_grmode;
	rl->rl_status = lkb->lkb_status;
	rl->rl_wait_type = cpu_to_le16(lkb->lkb_wait_type);

	if (lkb->lkb_bastfn)
		rl->rl_asts |= DLM_CB_BAST;
	if (lkb->lkb_astfn)
		rl->rl_asts |= DLM_CB_CAST;

	rl->rl_namelen = cpu_to_le16(r->res_length);
	memcpy(rl->rl_name, r->res_name, r->res_length);

	/* FIXME: might we have an lvb without DLM_LKF_VALBLK set ?
	   If so, receive_rcom_lock_args() won't take this copy. */

	if (lkb->lkb_lvbptr)
		memcpy(rl->rl_lvb, lkb->lkb_lvbptr, r->res_ls->ls_lvblen);
}

int dlm_send_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb)
{
	struct dlm_ls *ls = r->res_ls;
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	struct rcom_lock *rl;
	int error, len = sizeof(struct rcom_lock);

	if (lkb->lkb_lvbptr)
		len += ls->ls_lvblen;

	error = create_rcom(ls, r->res_nodeid, DLM_RCOM_LOCK, len, &rc, &mh);
	if (error)
		goto out;

	rl = (struct rcom_lock *) rc->rc_buf;
	pack_rcom_lock(r, lkb, rl);
	rc->rc_id = (unsigned long) r;

	send_rcom(ls, mh, rc);
 out:
	return error;
}

/* needs at least dlm_rcom + rcom_lock */
static void receive_rcom_lock(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, nodeid = rc_in->rc_header.h_nodeid;

	dlm_recover_master_copy(ls, rc_in);

	error = create_rcom(ls, nodeid, DLM_RCOM_LOCK_REPLY,
			    sizeof(struct rcom_lock), &rc, &mh);
	if (error)
		return;

	/* We send back the same rcom_lock struct we received, but
	   dlm_recover_master_copy() has filled in rl_remid and rl_result */

	memcpy(rc->rc_buf, rc_in->rc_buf, sizeof(struct rcom_lock));
	rc->rc_id = rc_in->rc_id;
	rc->rc_seq_reply = rc_in->rc_seq;

	send_rcom(ls, mh, rc);
}

/* If the lockspace doesn't exist then still send a status message
   back; it's possible that it just doesn't have its global_id yet. */

int dlm_send_ls_not_ready(int nodeid, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct rcom_config *rf;
	struct dlm_mhandle *mh;
	char *mb;
	int mb_len = sizeof(struct dlm_rcom) + sizeof(struct rcom_config);

	mh = dlm_lowcomms_get_buffer(nodeid, mb_len, GFP_NOFS, &mb);
	if (!mh)
		return -ENOBUFS;
	memset(mb, 0, mb_len);

	rc = (struct dlm_rcom *) mb;

	rc->rc_header.h_version = (DLM_HEADER_MAJOR | DLM_HEADER_MINOR);
	rc->rc_header.h_lockspace = rc_in->rc_header.h_lockspace;
	rc->rc_header.h_nodeid = dlm_our_nodeid();
	rc->rc_header.h_length = mb_len;
	rc->rc_header.h_cmd = DLM_RCOM;

	rc->rc_type = DLM_RCOM_STATUS_REPLY;
	rc->rc_id = rc_in->rc_id;
	rc->rc_seq_reply = rc_in->rc_seq;
	rc->rc_result = -ESRCH;

	rf = (struct rcom_config *) rc->rc_buf;
	rf->rf_lvblen = cpu_to_le32(~0U);

	dlm_rcom_out(rc);
	dlm_lowcomms_commit_buffer(mh);

	return 0;
}

/*
 * Ignore messages for stage Y before we set
 * recover_status bit for stage X:
 *
 * recover_status = 0
 *
 * dlm_recover_members()
 * - send nothing
 * - recv nothing
 * - ignore NAMES, NAMES_REPLY
 * - ignore LOOKUP, LOOKUP_REPLY
 * - ignore LOCK, LOCK_REPLY
 *
 * recover_status |= NODES
 *
 * dlm_recover_members_wait()
 *
 * dlm_recover_directory()
 * - send NAMES
 * - recv NAMES_REPLY
 * - ignore LOOKUP, LOOKUP_REPLY
 * - ignore LOCK, LOCK_REPLY
 *
 * recover_status |= DIR
 *
 * dlm_recover_directory_wait()
 *
 * dlm_recover_masters()
 * - send LOOKUP
 * - recv LOOKUP_REPLY
 *
 * dlm_recover_locks()
 * - send LOCKS
 * - recv LOCKS_REPLY
 *
 * recover_status |= LOCKS
 *
 * dlm_recover_locks_wait()
 *
 * recover_status |= DONE
 */

/* Called by dlm_recv; corresponds to dlm_receive_message() but special
   recovery-only comms are sent through here. */

void dlm_receive_rcom(struct dlm_ls *ls, struct dlm_rcom *rc, int nodeid)
{
	int lock_size = sizeof(struct dlm_rcom) + sizeof(struct rcom_lock);
	int stop, reply = 0, names = 0, lookup = 0, lock = 0;
	uint32_t status;
	uint64_t seq;

	switch (rc->rc_type) {
	case DLM_RCOM_STATUS_REPLY:
		reply = 1;
		break;
	case DLM_RCOM_NAMES:
		names = 1;
		break;
	case DLM_RCOM_NAMES_REPLY:
		names = 1;
		reply = 1;
		break;
	case DLM_RCOM_LOOKUP:
		lookup = 1;
		break;
	case DLM_RCOM_LOOKUP_REPLY:
		lookup = 1;
		reply = 1;
		break;
	case DLM_RCOM_LOCK:
		lock = 1;
		break;
	case DLM_RCOM_LOCK_REPLY:
		lock = 1;
		reply = 1;
		break;
	};

	spin_lock(&ls->ls_recover_lock);
	status = ls->ls_recover_status;
	stop = test_bit(LSFL_RECOVER_STOP, &ls->ls_flags);
	seq = ls->ls_recover_seq;
	spin_unlock(&ls->ls_recover_lock);

	if (stop && (rc->rc_type != DLM_RCOM_STATUS))
		goto ignore;

	if (reply && (rc->rc_seq_reply != seq))
		goto ignore;

	if (!(status & DLM_RS_NODES) && (names || lookup || lock))
		goto ignore;

	if (!(status & DLM_RS_DIR) && (lookup || lock))
		goto ignore;

	switch (rc->rc_type) {
	case DLM_RCOM_STATUS:
		receive_rcom_status(ls, rc);
		break;

	case DLM_RCOM_NAMES:
		receive_rcom_names(ls, rc);
		break;

	case DLM_RCOM_LOOKUP:
		receive_rcom_lookup(ls, rc);
		break;

	case DLM_RCOM_LOCK:
		if (rc->rc_header.h_length < lock_size)
			goto Eshort;
		receive_rcom_lock(ls, rc);
		break;

	case DLM_RCOM_STATUS_REPLY:
		receive_sync_reply(ls, rc);
		break;

	case DLM_RCOM_NAMES_REPLY:
		receive_sync_reply(ls, rc);
		break;

	case DLM_RCOM_LOOKUP_REPLY:
		receive_rcom_lookup_reply(ls, rc);
		break;

	case DLM_RCOM_LOCK_REPLY:
		if (rc->rc_header.h_length < lock_size)
			goto Eshort;
		dlm_recover_process_copy(ls, rc);
		break;

	default:
		log_error(ls, "receive_rcom bad type %d", rc->rc_type);
	}
	return;

ignore:
	log_limit(ls, "dlm_receive_rcom ignore msg %d "
		  "from %d %llu %llu recover seq %llu sts %x gen %u",
		   rc->rc_type,
		   nodeid,
		   (unsigned long long)rc->rc_seq,
		   (unsigned long long)rc->rc_seq_reply,
		   (unsigned long long)seq,
		   status, ls->ls_generation);
	return;
Eshort:
	log_error(ls, "recovery message %d from %d is too short",
		  rc->rc_type, nodeid);
}

