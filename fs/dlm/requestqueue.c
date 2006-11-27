/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
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
	int nodeid;
	char request[1];
};

/*
 * Requests received while the lockspace is in recovery get added to the
 * request queue and processed when recovery is complete.  This happens when
 * the lockspace is suspended on some nodes before it is on others, or the
 * lockspace is enabled on some while still suspended on others.
 */

int dlm_add_requestqueue(struct dlm_ls *ls, int nodeid, struct dlm_header *hd)
{
	struct rq_entry *e;
	int length = hd->h_length;
	int rv = 0;

	e = kmalloc(sizeof(struct rq_entry) + length, GFP_KERNEL);
	if (!e) {
		log_print("dlm_add_requestqueue: out of memory\n");
		return 0;
	}

	e->nodeid = nodeid;
	memcpy(e->request, hd, length);

	/* We need to check dlm_locking_stopped() after taking the mutex to
	   avoid a race where dlm_recoverd enables locking and runs
	   process_requestqueue between our earlier dlm_locking_stopped check
	   and this addition to the requestqueue. */

	mutex_lock(&ls->ls_requestqueue_mutex);
	if (dlm_locking_stopped(ls))
		list_add_tail(&e->list, &ls->ls_requestqueue);
	else {
		log_debug(ls, "dlm_add_requestqueue skip from %d", nodeid);
		kfree(e);
		rv = -EAGAIN;
	}
	mutex_unlock(&ls->ls_requestqueue_mutex);
	return rv;
}

int dlm_process_requestqueue(struct dlm_ls *ls)
{
	struct rq_entry *e;
	struct dlm_header *hd;
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

		hd = (struct dlm_header *) e->request;
		error = dlm_receive_message(hd, e->nodeid, 1);

		if (error == -EINTR) {
			/* entry is left on requestqueue */
			log_debug(ls, "process_requestqueue abort eintr");
			break;
		}

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
 * saved requests and processes them as they would have been by dlm_recvd.  At
 * the same time, dlm_recvd will start receiving new requests from remote
 * nodes.  We want to delay dlm_recvd processing new requests until
 * dlm_recoverd has finished processing the old saved requests.
 */

void dlm_wait_requestqueue(struct dlm_ls *ls)
{
	for (;;) {
		mutex_lock(&ls->ls_requestqueue_mutex);
		if (list_empty(&ls->ls_requestqueue))
			break;
		if (dlm_locking_stopped(ls))
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

	/* with no directory, the master is likely to change as a part of
	   recovery; requests to/from the defunct master need to be purged */

	switch (type) {
	case DLM_MSG_REQUEST:
	case DLM_MSG_CONVERT:
	case DLM_MSG_UNLOCK:
	case DLM_MSG_CANCEL:
		/* we're no longer the master of this resource, the sender
		   will resend to the new master (see waiter_needs_recovery) */

		if (dlm_hash2nodeid(ls, ms->m_hash) != dlm_our_nodeid())
			return 1;
		break;

	case DLM_MSG_REQUEST_REPLY:
	case DLM_MSG_CONVERT_REPLY:
	case DLM_MSG_UNLOCK_REPLY:
	case DLM_MSG_CANCEL_REPLY:
	case DLM_MSG_GRANT:
		/* this reply is from the former master of the resource,
		   we'll resend to the new master if needed */

		if (dlm_hash2nodeid(ls, ms->m_hash) != nodeid)
			return 1;
		break;
	}

	return 0;
}

void dlm_purge_requestqueue(struct dlm_ls *ls)
{
	struct dlm_message *ms;
	struct rq_entry *e, *safe;

	mutex_lock(&ls->ls_requestqueue_mutex);
	list_for_each_entry_safe(e, safe, &ls->ls_requestqueue, list) {
		ms = (struct dlm_message *) e->request;

		if (purge_request(ls, ms, e->nodeid)) {
			list_del(&e->list);
			kfree(e);
		}
	}
	mutex_unlock(&ls->ls_requestqueue_mutex);
}

