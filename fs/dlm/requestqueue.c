// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "member.h"
#include "lock.h"
#include "dir.h"
#include "config.h"
#include "requestqueue.h"
#include "util.h"

struct rq_entry {
	struct list_head list;
	uint32_t recover_seq;
	int nodeid;
	struct dlm_message request;
};

/*
 * Requests received while the lockspace is in recovery get added to the
 * request queue and processed when recovery is complete.  This happens when
 * the lockspace is suspended on some nodes before it is on others, or the
 * lockspace is enabled on some while still suspended on others.
 */

void dlm_add_requestqueue(struct dlm_ls *ls, int nodeid,
			  const struct dlm_message *ms)
{
	struct rq_entry *e;
	int length = le16_to_cpu(ms->m_header.h_length) -
		sizeof(struct dlm_message);

	e = kmalloc(sizeof(struct rq_entry) + length, GFP_ATOMIC);
	if (!e) {
		log_print("dlm_add_requestqueue: out of memory len %d", length);
		return;
	}

	e->recover_seq = ls->ls_recover_seq & 0xFFFFFFFF;
	e->nodeid = nodeid;
	memcpy(&e->request, ms, sizeof(*ms));
	memcpy(&e->request.m_extra, ms->m_extra, length);

	list_add_tail(&e->list, &ls->ls_requestqueue);
}

/*
 * Called by dlm_recoverd to process normal messages saved while recovery was
 * happening.  Normal locking has been enabled before this is called.  dlm_recv
 * upon receiving a message, will wait for all saved messages to be drained
 * here before processing the message it got.  If a new dlm_ls_stop() arrives
 * while we're processing these saved messages, it may block trying to suspend
 * dlm_recv if dlm_recv is waiting for us in dlm_wait_requestqueue.  In that
 * case, we don't abort since locking_stopped is still 0.  If dlm_recv is not
 * waiting for us, then this processing may be aborted due to locking_stopped.
 */

int dlm_process_requestqueue(struct dlm_ls *ls)
{
	struct rq_entry *e;
	struct dlm_message *ms;
	int error = 0;

	write_lock_bh(&ls->ls_requestqueue_lock);
	for (;;) {
		if (list_empty(&ls->ls_requestqueue)) {
			clear_bit(LSFL_RECV_MSG_BLOCKED, &ls->ls_flags);
			error = 0;
			break;
		}
		e = list_first_entry(&ls->ls_requestqueue, struct rq_entry, list);

		ms = &e->request;

		log_limit(ls, "dlm_process_requestqueue msg %d from %d "
			  "lkid %x remid %x result %d seq %u",
			  le32_to_cpu(ms->m_type),
			  le32_to_cpu(ms->m_header.h_nodeid),
			  le32_to_cpu(ms->m_lkid), le32_to_cpu(ms->m_remid),
			  from_dlm_errno(le32_to_cpu(ms->m_result)),
			  e->recover_seq);

		dlm_receive_message_saved(ls, &e->request, e->recover_seq);
		list_del(&e->list);
		kfree(e);

		if (dlm_locking_stopped(ls)) {
			log_debug(ls, "process_requestqueue abort running");
			error = -EINTR;
			break;
		}
		write_unlock_bh(&ls->ls_requestqueue_lock);
		schedule();
		write_lock_bh(&ls->ls_requestqueue_lock);
	}
	write_unlock_bh(&ls->ls_requestqueue_lock);

	return error;
}

static int purge_request(struct dlm_ls *ls, struct dlm_message *ms, int nodeid)
{
	__le32 type = ms->m_type;

	/* the ls is being cleaned up and freed by release_lockspace */
	if (!atomic_read(&ls->ls_count))
		return 1;

	if (dlm_is_removed(ls, nodeid))
		return 1;

	/* directory operations are always purged because the directory is
	   always rebuilt during recovery and the lookups resent */

	if (type == cpu_to_le32(DLM_MSG_REMOVE) ||
	    type == cpu_to_le32(DLM_MSG_LOOKUP) ||
	    type == cpu_to_le32(DLM_MSG_LOOKUP_REPLY))
		return 1;

	if (!dlm_no_directory(ls))
		return 0;

	return 1;
}

void dlm_purge_requestqueue(struct dlm_ls *ls)
{
	struct dlm_message *ms;
	struct rq_entry *e, *safe;

	write_lock_bh(&ls->ls_requestqueue_lock);
	list_for_each_entry_safe(e, safe, &ls->ls_requestqueue, list) {
		ms =  &e->request;

		if (purge_request(ls, ms, e->nodeid)) {
			list_del(&e->list);
			kfree(e);
		}
	}
	write_unlock_bh(&ls->ls_requestqueue_lock);
}

