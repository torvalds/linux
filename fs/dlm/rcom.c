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

	mh = dlm_lowcomms_get_buffer(to_nodeid, mb_len, ls->ls_allocation, &mb);
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

/* When replying to a status request, a node also sends back its
   configuration values.  The requesting node then checks that the remote
   node is configured the same way as itself. */

static void make_config(struct dlm_ls *ls, struct rcom_config *rf)
{
	rf->rf_lvblen = cpu_to_le32(ls->ls_lvblen);
	rf->rf_lsflags = cpu_to_le32(ls->ls_exflags);
}

static int check_config(struct dlm_ls *ls, struct dlm_rcom *rc, int nodeid)
{
	struct rcom_config *rf = (struct rcom_config *) rc->rc_buf;
	size_t conf_size = sizeof(struct dlm_rcom) + sizeof(struct rcom_config);

	if ((rc->rc_header.h_version & 0xFFFF0000) != DLM_HEADER_MAJOR) {
		log_error(ls, "version mismatch: %x nodeid %d: %x",
			  DLM_HEADER_MAJOR | DLM_HEADER_MINOR, nodeid,
			  rc->rc_header.h_version);
		return -EPROTO;
	}

	if (rc->rc_header.h_length < conf_size) {
		log_error(ls, "config too short: %d nodeid %d",
			  rc->rc_header.h_length, nodeid);
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

int dlm_rcom_status(struct dlm_ls *ls, int nodeid)
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

	error = create_rcom(ls, nodeid, DLM_RCOM_STATUS, 0, &rc, &mh);
	if (error)
		goto out;

	allow_sync_reply(ls, &rc->rc_id);
	memset(ls->ls_recover_buf, 0, dlm_config.ci_buffer_size);

	send_rcom(ls, mh, rc);

	error = dlm_wait_function(ls, &rcom_response);
	disallow_sync_reply(ls);
	if (error)
		goto out;

	rc = ls->ls_recover_buf;

	if (rc->rc_result == -ESRCH) {
		/* we pretend the remote lockspace exists with 0 status */
		log_debug(ls, "remote node %d not ready", nodeid);
		rc->rc_result = 0;
	} else
		error = check_config(ls, rc, nodeid);
	/* the caller looks at rc_result for the remote recovery status */
 out:
	return error;
}

static void receive_rcom_status(struct dlm_ls *ls, struct dlm_rcom *rc_in)
{
	struct dlm_rcom *rc;
	struct dlm_mhandle *mh;
	int error, nodeid = rc_in->rc_header.h_nodeid;

	error = create_rcom(ls, nodeid, DLM_RCOM_STATUS_REPLY,
			    sizeof(struct rcom_config), &rc, &mh);
	if (error)
		return;
	rc->rc_id = rc_in->rc_id;
	rc->rc_seq_reply = rc_in->rc_seq;
	rc->rc_result = dlm_recover_status(ls);
	make_config(ls, (struct rcom_config *) rc->rc_buf);

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
	int max_size = dlm_config.ci_buffer_size - sizeof(struct dlm_rcom);

	ls->ls_recover_nodeid = nodeid;

	if (nodeid == dlm_our_nodeid()) {
		dlm_copy_master_names(ls, last_name, last_len,
		                      ls->ls_recover_buf->rc_buf,
		                      max_size, nodeid);
		goto out;
	}

	error = create_rcom(ls, nodeid, DLM_RCOM_NAMES, last_len, &rc, &mh);
	if (error)
		goto out;
	memcpy(rc->rc_buf, last_name, last_len);

	allow_sync_reply(ls, &rc->rc_id);
	memset(ls->ls_recover_buf, 0, dlm_config.ci_buffer_size);

	send_rcom(ls, mh, rc);

	error = dlm_wait_function(ls, &rcom_response);
	disallow_sync_reply(ls);
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
	rc->rc_id = (unsigned long) r;

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

	error = dlm_dir_lookup(ls, nodeid, rc_in->rc_buf, len, &ret_nodeid);
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
		rl->rl_asts |= AST_BAST;
	if (lkb->lkb_astfn)
		rl->rl_asts |= AST_COMP;

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

static int is_old_reply(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	uint64_t seq;
	int rv = 0;

	switch (rc->rc_type) {
	case DLM_RCOM_STATUS_REPLY:
	case DLM_RCOM_NAMES_REPLY:
	case DLM_RCOM_LOOKUP_REPLY:
	case DLM_RCOM_LOCK_REPLY:
		spin_lock(&ls->ls_recover_lock);
		seq = ls->ls_recover_seq;
		spin_unlock(&ls->ls_recover_lock);
		if (rc->rc_seq_reply != seq) {
			log_debug(ls, "ignoring old reply %x from %d "
				      "seq_reply %llx expect %llx",
				      rc->rc_type, rc->rc_header.h_nodeid,
				      (unsigned long long)rc->rc_seq_reply,
				      (unsigned long long)seq);
			rv = 1;
		}
	}
	return rv;
}

/* Called by dlm_recv; corresponds to dlm_receive_message() but special
   recovery-only comms are sent through here. */

void dlm_receive_rcom(struct dlm_ls *ls, struct dlm_rcom *rc, int nodeid)
{
	int lock_size = sizeof(struct dlm_rcom) + sizeof(struct rcom_lock);

	if (dlm_recovery_stopped(ls) && (rc->rc_type != DLM_RCOM_STATUS)) {
		log_debug(ls, "ignoring recovery message %x from %d",
			  rc->rc_type, nodeid);
		goto out;
	}

	if (is_old_reply(ls, rc))
		goto out;

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
out:
	return;
Eshort:
	log_error(ls, "recovery message %x from %d is too short",
			  rc->rc_type, nodeid);
}

