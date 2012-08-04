/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "member.h"
#include "lock.h"
#include "dir.h"
#include "config.h"
#include "requestqueue.h"

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

void dlm_add_requestqueue(struct dlm_ls *ls, int nodeid, struct dlm_message *ms)
{
	struct rq_entry *e;
	int length = ms->m_header.h_length - sizeof(struct dlm_message);

	e = kmalloc(sizeof(struct rq_entry) + length, GFP_NOFS);
	if (!e) {
		log_print("dlm_add_requestqueue: out of memory len %d", length);
		return;
	}

	e->recover_seq = ls->ls_recover_seq & 0xFFFFFFFF;
	e->nodeid = nodeid;
	memcpy(&e->request, ms, ms->m_header.h_length);

	mutex_lock(&ls->ls_requestqueue_mutex);
	list_add_tail(&e->list, &ls->ls_requestqueue);
	mutex_unlock(&ls->ls_requestqueue_mutex);
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

	mutex_lock(&ls->ls_requestqueue_mutex);

	for (;;) {
		if (list_empty(&ls->ls_requestqueue)) {
			mutex_unlock(&ls->ls_requestqueue_mutex);
			error = 0;
			break;
		}
		e = list_entry(ls->ls_requestqueue.next, struct rq_entry, list);
		mutex_unlock(&ls->ls_requestqueue_mutex);

		ms = &e->request;

		log_limit(ls, "dlm_process_requestqueue msg %d from %d "
			  "lkid %x remid %x result %d seq %u",
			  ms->m_type, ms->m_header.h_nodeid,
			  ms->m_lkid, ms->m_remid, ms->m_result,
			  e->recover_seq);

		dlm_receive_message_saved(ls, &e->request, e->recover_seq);

		mutex_lock(&ls->ls_requestqueue_mutex);
		list_del(&e->list);
		kfree(e);

		if (dlm_locking_stopped(ls)) {
			log_debug(ls, "process_requestqueue abort running");
			mutex_unlock(&ls->ls_requestqueue_mutex);
			error = -EINTR;
			break;
		}
		schedule();
	}

	return error;
}

/*
 * After recovery is done, locking is resumed and dlm_recoverd takes all the
 * saved requests and processes them as they would have been by dlm_recv.  At
 * the same time, dlm_recv will start receiving new requests from remote nodes.
 * We want to delay dlm_recv processing new requests until dlm_recoverd has
 * finished processing the old saved requests.  We don't check for locking
 * stopped here because dlm_ls_stop won't stop locking until it's suspended us
 * (dlm_recv).
 */

void dlm_wait_requestqueue(struct dlm_ls *ls)
{
	for (;;) {
		mutex_lock(&ls->ls_requestqueue_mutex);
		if (list_empty(&ls->ls_requestqueue))
			break;
		mutex_unlock(&ls->ls_requestqueue_mutex);
		schedule();
	}
	mutex_unlock(&ls->ls_requestqueue_mutex);
}

static int purge_request(struct dlm_ls *ls, struct dlm_message *ms, int nodeid)
{
	uint32_t type = ms->m_type;

	/* the ls is being cleaned up and freed by release_lockspace */
	if (!ls->ls_count)
		return 1;

	if (dlm_is_removed(ls, nodeid))
		return 1;

	/* directory operations are always purged because the directory is
	   always rebuilt during recovery and the lookups resent */

	if (type == DLM_MSG_REMOVE ||
	    type == DLM_MSG_LOOKUP ||
	    type == DLM_MSG_LOOKUP_REPLY)
		return 1;

	if (!dlm_no_directory(ls))
		return 0;

	return 1;
}

void dlm_purge_requestqueue(struct dlm_ls *ls)
{
	struct dlm_message *ms;
	struct rq_entry *e, *safe;

	mutex_lock(&ls->ls_requestqueue_mutex);
	list_for_each_entry_safe(e, safe, &ls->ls_requestqueue, list) {
		ms =  &e->request;

		if (purge_request(ls, ms, e->nodeid)) {
			list_del(&e->list);
			kfree(e);
		}
	}
	mutex_unlock(&ls->ls_requestqueue_mutex);
}

