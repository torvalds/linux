/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * vote.c
 *
 * description here
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>

#include <cluster/heartbeat.h>
#include <cluster/nodemanager.h>
#include <cluster/tcp.h>

#include <dlm/dlmapi.h>

#define MLOG_MASK_PREFIX ML_VOTE
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "slot_map.h"
#include "vote.h"

#include "buffer_head_io.h"

#define OCFS2_MESSAGE_TYPE_VOTE     (0x1)
#define OCFS2_MESSAGE_TYPE_RESPONSE (0x2)
struct ocfs2_msg_hdr
{
	__be32 h_response_id; /* used to lookup message handle on sending
			    * node. */
	__be32 h_request;
	__be64 h_blkno;
	__be32 h_generation;
	__be32 h_node_num;    /* node sending this particular message. */
};

struct ocfs2_vote_msg
{
	struct ocfs2_msg_hdr v_hdr;
	__be32 v_reserved1;
};

/* Responses are given these values to maintain backwards
 * compatibility with older ocfs2 versions */
#define OCFS2_RESPONSE_OK		(0)
#define OCFS2_RESPONSE_BUSY		(-16)
#define OCFS2_RESPONSE_BAD_MSG		(-22)

struct ocfs2_response_msg
{
	struct ocfs2_msg_hdr r_hdr;
	__be32 r_response;
};

struct ocfs2_vote_work {
	struct list_head   w_list;
	struct ocfs2_vote_msg w_msg;
};

enum ocfs2_vote_request {
	OCFS2_VOTE_REQ_INVALID = 0,
	OCFS2_VOTE_REQ_MOUNT,
	OCFS2_VOTE_REQ_UMOUNT,
	OCFS2_VOTE_REQ_LAST
};

static inline int ocfs2_is_valid_vote_request(int request)
{
	return OCFS2_VOTE_REQ_INVALID < request &&
		request < OCFS2_VOTE_REQ_LAST;
}

typedef void (*ocfs2_net_response_callback)(void *priv,
					    struct ocfs2_response_msg *resp);
struct ocfs2_net_response_cb {
	ocfs2_net_response_callback	rc_cb;
	void				*rc_priv;
};

struct ocfs2_net_wait_ctxt {
	struct list_head        n_list;
	u32                     n_response_id;
	wait_queue_head_t       n_event;
	struct ocfs2_node_map   n_node_map;
	int                     n_response; /* an agreggate response. 0 if
					     * all nodes are go, < 0 on any
					     * negative response from any
					     * node or network error. */
	struct ocfs2_net_response_cb *n_callback;
};

static void ocfs2_process_mount_request(struct ocfs2_super *osb,
					unsigned int node_num)
{
	mlog(0, "MOUNT vote from node %u\n", node_num);
	/* The other node only sends us this message when he has an EX
	 * on the superblock, so our recovery threads (if having been
	 * launched) are waiting on it.*/
	ocfs2_recovery_map_clear(osb, node_num);
	ocfs2_node_map_set_bit(osb, &osb->mounted_map, node_num);

	/* We clear the umount map here because a node may have been
	 * previously mounted, safely unmounted but never stopped
	 * heartbeating - in which case we'd have a stale entry. */
	ocfs2_node_map_clear_bit(osb, &osb->umount_map, node_num);
}

static void ocfs2_process_umount_request(struct ocfs2_super *osb,
					 unsigned int node_num)
{
	mlog(0, "UMOUNT vote from node %u\n", node_num);
	ocfs2_node_map_clear_bit(osb, &osb->mounted_map, node_num);
	ocfs2_node_map_set_bit(osb, &osb->umount_map, node_num);
}

static void ocfs2_process_vote(struct ocfs2_super *osb,
			       struct ocfs2_vote_msg *msg)
{
	int net_status, vote_response;
	unsigned int node_num;
	u64 blkno;
	enum ocfs2_vote_request request;
	struct ocfs2_msg_hdr *hdr = &msg->v_hdr;
	struct ocfs2_response_msg response;

	/* decode the network mumbo jumbo into local variables. */
	request = be32_to_cpu(hdr->h_request);
	blkno = be64_to_cpu(hdr->h_blkno);
	node_num = be32_to_cpu(hdr->h_node_num);

	mlog(0, "processing vote: request = %u, blkno = %llu, node_num = %u\n",
	     request, (unsigned long long)blkno, node_num);

	if (!ocfs2_is_valid_vote_request(request)) {
		mlog(ML_ERROR, "Invalid vote request %d from node %u\n",
		     request, node_num);
		vote_response = OCFS2_RESPONSE_BAD_MSG;
		goto respond;
	}

	vote_response = OCFS2_RESPONSE_OK;

	switch (request) {
	case OCFS2_VOTE_REQ_UMOUNT:
		ocfs2_process_umount_request(osb, node_num);
		goto respond;
	case OCFS2_VOTE_REQ_MOUNT:
		ocfs2_process_mount_request(osb, node_num);
		goto respond;
	default:
		/* avoids a gcc warning */
		break;
	}

respond:
	/* Response struture is small so we just put it on the stack
	 * and stuff it inline. */
	memset(&response, 0, sizeof(struct ocfs2_response_msg));
	response.r_hdr.h_response_id = hdr->h_response_id;
	response.r_hdr.h_blkno = hdr->h_blkno;
	response.r_hdr.h_generation = hdr->h_generation;
	response.r_hdr.h_node_num = cpu_to_be32(osb->node_num);
	response.r_response = cpu_to_be32(vote_response);

	net_status = o2net_send_message(OCFS2_MESSAGE_TYPE_RESPONSE,
					osb->net_key,
					&response,
					sizeof(struct ocfs2_response_msg),
					node_num,
					NULL);
	/* We still want to error print for ENOPROTOOPT here. The
	 * sending node shouldn't have unregistered his net handler
	 * without sending an unmount vote 1st */
	if (net_status < 0
	    && net_status != -ETIMEDOUT
	    && net_status != -ENOTCONN)
		mlog(ML_ERROR, "message to node %u fails with error %d!\n",
		     node_num, net_status);
}

static void ocfs2_vote_thread_do_work(struct ocfs2_super *osb)
{
	unsigned long processed;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_vote_work *work;

	mlog_entry_void();

	spin_lock(&osb->vote_task_lock);
	/* grab this early so we know to try again if a state change and
	 * wake happens part-way through our work  */
	osb->vote_work_sequence = osb->vote_wake_sequence;

	processed = osb->blocked_lock_count;
	while (processed) {
		BUG_ON(list_empty(&osb->blocked_lock_list));

		lockres = list_entry(osb->blocked_lock_list.next,
				     struct ocfs2_lock_res, l_blocked_list);
		list_del_init(&lockres->l_blocked_list);
		osb->blocked_lock_count--;
		spin_unlock(&osb->vote_task_lock);

		BUG_ON(!processed);
		processed--;

		ocfs2_process_blocked_lock(osb, lockres);

		spin_lock(&osb->vote_task_lock);
	}

	while (osb->vote_count) {
		BUG_ON(list_empty(&osb->vote_list));
		work = list_entry(osb->vote_list.next,
				  struct ocfs2_vote_work, w_list);
		list_del(&work->w_list);
		osb->vote_count--;
		spin_unlock(&osb->vote_task_lock);

		ocfs2_process_vote(osb, &work->w_msg);
		kfree(work);

		spin_lock(&osb->vote_task_lock);
	}
	spin_unlock(&osb->vote_task_lock);

	mlog_exit_void();
}

static int ocfs2_vote_thread_lists_empty(struct ocfs2_super *osb)
{
	int empty = 0;

	spin_lock(&osb->vote_task_lock);
	if (list_empty(&osb->blocked_lock_list) &&
	    list_empty(&osb->vote_list))
		empty = 1;

	spin_unlock(&osb->vote_task_lock);
	return empty;
}

static int ocfs2_vote_thread_should_wake(struct ocfs2_super *osb)
{
	int should_wake = 0;

	spin_lock(&osb->vote_task_lock);
	if (osb->vote_work_sequence != osb->vote_wake_sequence)
		should_wake = 1;
	spin_unlock(&osb->vote_task_lock);

	return should_wake;
}

int ocfs2_vote_thread(void *arg)
{
	int status = 0;
	struct ocfs2_super *osb = arg;

	/* only quit once we've been asked to stop and there is no more
	 * work available */
	while (!(kthread_should_stop() &&
		 ocfs2_vote_thread_lists_empty(osb))) {

		wait_event_interruptible(osb->vote_event,
					 ocfs2_vote_thread_should_wake(osb) ||
					 kthread_should_stop());

		mlog(0, "vote_thread: awoken\n");

		ocfs2_vote_thread_do_work(osb);
	}

	osb->vote_task = NULL;
	return status;
}

static struct ocfs2_net_wait_ctxt *ocfs2_new_net_wait_ctxt(unsigned int response_id)
{
	struct ocfs2_net_wait_ctxt *w;

	w = kzalloc(sizeof(*w), GFP_NOFS);
	if (!w) {
		mlog_errno(-ENOMEM);
		goto bail;
	}

	INIT_LIST_HEAD(&w->n_list);
	init_waitqueue_head(&w->n_event);
	ocfs2_node_map_init(&w->n_node_map);
	w->n_response_id = response_id;
	w->n_callback = NULL;
bail:
	return w;
}

static unsigned int ocfs2_new_response_id(struct ocfs2_super *osb)
{
	unsigned int ret;

	spin_lock(&osb->net_response_lock);
	ret = ++osb->net_response_ids;
	spin_unlock(&osb->net_response_lock);

	return ret;
}

static void ocfs2_dequeue_net_wait_ctxt(struct ocfs2_super *osb,
					struct ocfs2_net_wait_ctxt *w)
{
	spin_lock(&osb->net_response_lock);
	list_del(&w->n_list);
	spin_unlock(&osb->net_response_lock);
}

static void ocfs2_queue_net_wait_ctxt(struct ocfs2_super *osb,
				      struct ocfs2_net_wait_ctxt *w)
{
	spin_lock(&osb->net_response_lock);
	list_add_tail(&w->n_list,
		      &osb->net_response_list);
	spin_unlock(&osb->net_response_lock);
}

static void __ocfs2_mark_node_responded(struct ocfs2_super *osb,
					struct ocfs2_net_wait_ctxt *w,
					int node_num)
{
	assert_spin_locked(&osb->net_response_lock);

	ocfs2_node_map_clear_bit(osb, &w->n_node_map, node_num);
	if (ocfs2_node_map_is_empty(osb, &w->n_node_map))
		wake_up(&w->n_event);
}

/* Intended to be called from the node down callback, we fake remove
 * the node from all our response contexts */
void ocfs2_remove_node_from_vote_queues(struct ocfs2_super *osb,
					int node_num)
{
	struct list_head *p;
	struct ocfs2_net_wait_ctxt *w = NULL;

	spin_lock(&osb->net_response_lock);

	list_for_each(p, &osb->net_response_list) {
		w = list_entry(p, struct ocfs2_net_wait_ctxt, n_list);

		__ocfs2_mark_node_responded(osb, w, node_num);
	}

	spin_unlock(&osb->net_response_lock);
}

static int ocfs2_broadcast_vote(struct ocfs2_super *osb,
				struct ocfs2_vote_msg *request,
				unsigned int response_id,
				int *response,
				struct ocfs2_net_response_cb *callback)
{
	int status, i, remote_err;
	struct ocfs2_net_wait_ctxt *w = NULL;
	int dequeued = 0;

	mlog_entry_void();

	w = ocfs2_new_net_wait_ctxt(response_id);
	if (!w) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}
	w->n_callback = callback;

	/* we're pretty much ready to go at this point, and this fills
	 * in n_response which we need anyway... */
	ocfs2_queue_net_wait_ctxt(osb, w);

	i = ocfs2_node_map_iterate(osb, &osb->mounted_map, 0);

	while (i != O2NM_INVALID_NODE_NUM) {
		if (i != osb->node_num) {
			mlog(0, "trying to send request to node %i\n", i);
			ocfs2_node_map_set_bit(osb, &w->n_node_map, i);

			remote_err = 0;
			status = o2net_send_message(OCFS2_MESSAGE_TYPE_VOTE,
						    osb->net_key,
						    request,
						    sizeof(*request),
						    i,
						    &remote_err);
			if (status == -ETIMEDOUT) {
				mlog(0, "remote node %d timed out!\n", i);
				status = -EAGAIN;
				goto bail;
			}
			if (remote_err < 0) {
				status = remote_err;
				mlog(0, "remote error %d on node %d!\n",
				     remote_err, i);
				mlog_errno(status);
				goto bail;
			}
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
		}
		i++;
		i = ocfs2_node_map_iterate(osb, &osb->mounted_map, i);
		mlog(0, "next is %d, i am %d\n", i, osb->node_num);
	}
	mlog(0, "done sending, now waiting on responses...\n");

	wait_event(w->n_event, ocfs2_node_map_is_empty(osb, &w->n_node_map));

	ocfs2_dequeue_net_wait_ctxt(osb, w);
	dequeued = 1;

	*response = w->n_response;
	status = 0;
bail:
	if (w) {
		if (!dequeued)
			ocfs2_dequeue_net_wait_ctxt(osb, w);
		kfree(w);
	}

	mlog_exit(status);
	return status;
}

static struct ocfs2_vote_msg * ocfs2_new_vote_request(struct ocfs2_super *osb,
						      u64 blkno,
						      unsigned int generation,
						      enum ocfs2_vote_request type)
{
	struct ocfs2_vote_msg *request;
	struct ocfs2_msg_hdr *hdr;

	BUG_ON(!ocfs2_is_valid_vote_request(type));

	request = kzalloc(sizeof(*request), GFP_NOFS);
	if (!request) {
		mlog_errno(-ENOMEM);
	} else {
		hdr = &request->v_hdr;
		hdr->h_node_num = cpu_to_be32(osb->node_num);
		hdr->h_request = cpu_to_be32(type);
		hdr->h_blkno = cpu_to_be64(blkno);
		hdr->h_generation = cpu_to_be32(generation);
	}

	return request;
}

/* Complete the buildup of a new vote request and process the
 * broadcast return value. */
static int ocfs2_do_request_vote(struct ocfs2_super *osb,
				 struct ocfs2_vote_msg *request,
				 struct ocfs2_net_response_cb *callback)
{
	int status, response = -EBUSY;
	unsigned int response_id;
	struct ocfs2_msg_hdr *hdr;

	response_id = ocfs2_new_response_id(osb);

	hdr = &request->v_hdr;
	hdr->h_response_id = cpu_to_be32(response_id);

	status = ocfs2_broadcast_vote(osb, request, response_id, &response,
				      callback);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = response;
bail:

	return status;
}

int ocfs2_request_mount_vote(struct ocfs2_super *osb)
{
	int status;
	struct ocfs2_vote_msg *request = NULL;

	request = ocfs2_new_vote_request(osb, 0ULL, 0, OCFS2_VOTE_REQ_MOUNT);
	if (!request) {
		status = -ENOMEM;
		goto bail;
	}

	status = -EAGAIN;
	while (status == -EAGAIN) {
		if (!(osb->s_mount_opt & OCFS2_MOUNT_NOINTR) &&
		    signal_pending(current)) {
			status = -ERESTARTSYS;
			goto bail;
		}

		if (ocfs2_node_map_is_only(osb, &osb->mounted_map,
					   osb->node_num)) {
			status = 0;
			goto bail;
		}

		status = ocfs2_do_request_vote(osb, request, NULL);
	}

bail:
	kfree(request);
	return status;
}

int ocfs2_request_umount_vote(struct ocfs2_super *osb)
{
	int status;
	struct ocfs2_vote_msg *request = NULL;

	request = ocfs2_new_vote_request(osb, 0ULL, 0, OCFS2_VOTE_REQ_UMOUNT);
	if (!request) {
		status = -ENOMEM;
		goto bail;
	}

	status = -EAGAIN;
	while (status == -EAGAIN) {
		/* Do not check signals on this vote... We really want
		 * this one to go all the way through. */

		if (ocfs2_node_map_is_only(osb, &osb->mounted_map,
					   osb->node_num)) {
			status = 0;
			goto bail;
		}

		status = ocfs2_do_request_vote(osb, request, NULL);
	}

bail:
	kfree(request);
	return status;
}

/* TODO: This should eventually be a hash table! */
static struct ocfs2_net_wait_ctxt * __ocfs2_find_net_wait_ctxt(struct ocfs2_super *osb,
							       u32 response_id)
{
	struct list_head *p;
	struct ocfs2_net_wait_ctxt *w = NULL;

	list_for_each(p, &osb->net_response_list) {
		w = list_entry(p, struct ocfs2_net_wait_ctxt, n_list);
		if (response_id == w->n_response_id)
			break;
		w = NULL;
	}

	return w;
}

/* Translate response codes into local node errno values */
static inline int ocfs2_translate_response(int response)
{
	int ret;

	switch (response) {
	case OCFS2_RESPONSE_OK:
		ret = 0;
		break;

	case OCFS2_RESPONSE_BUSY:
		ret = -EBUSY;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ocfs2_handle_response_message(struct o2net_msg *msg,
					 u32 len,
					 void *data, void **ret_data)
{
	unsigned int response_id, node_num;
	int response_status;
	struct ocfs2_super *osb = data;
	struct ocfs2_response_msg *resp;
	struct ocfs2_net_wait_ctxt * w;
	struct ocfs2_net_response_cb *resp_cb;

	resp = (struct ocfs2_response_msg *) msg->buf;

	response_id = be32_to_cpu(resp->r_hdr.h_response_id);
	node_num = be32_to_cpu(resp->r_hdr.h_node_num);
	response_status = 
		ocfs2_translate_response(be32_to_cpu(resp->r_response));

	mlog(0, "received response message:\n");
	mlog(0, "h_response_id = %u\n", response_id);
	mlog(0, "h_request = %u\n", be32_to_cpu(resp->r_hdr.h_request));
	mlog(0, "h_blkno = %llu\n",
	     (unsigned long long)be64_to_cpu(resp->r_hdr.h_blkno));
	mlog(0, "h_generation = %u\n", be32_to_cpu(resp->r_hdr.h_generation));
	mlog(0, "h_node_num = %u\n", node_num);
	mlog(0, "r_response = %d\n", response_status);

	spin_lock(&osb->net_response_lock);
	w = __ocfs2_find_net_wait_ctxt(osb, response_id);
	if (!w) {
		mlog(0, "request not found!\n");
		goto bail;
	}
	resp_cb = w->n_callback;

	if (response_status && (!w->n_response)) {
		/* we only really need one negative response so don't
		 * set it twice. */
		w->n_response = response_status;
	}

	if (resp_cb) {
		spin_unlock(&osb->net_response_lock);

		resp_cb->rc_cb(resp_cb->rc_priv, resp);

		spin_lock(&osb->net_response_lock);
	}

	__ocfs2_mark_node_responded(osb, w, node_num);
bail:
	spin_unlock(&osb->net_response_lock);

	return 0;
}

static int ocfs2_handle_vote_message(struct o2net_msg *msg,
				     u32 len,
				     void *data, void **ret_data)
{
	int status;
	struct ocfs2_super *osb = data;
	struct ocfs2_vote_work *work;

	work = kmalloc(sizeof(struct ocfs2_vote_work), GFP_NOFS);
	if (!work) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	INIT_LIST_HEAD(&work->w_list);
	memcpy(&work->w_msg, msg->buf, sizeof(struct ocfs2_vote_msg));

	mlog(0, "scheduling vote request:\n");
	mlog(0, "h_response_id = %u\n",
	     be32_to_cpu(work->w_msg.v_hdr.h_response_id));
	mlog(0, "h_request = %u\n", be32_to_cpu(work->w_msg.v_hdr.h_request));
	mlog(0, "h_blkno = %llu\n",
	     (unsigned long long)be64_to_cpu(work->w_msg.v_hdr.h_blkno));
	mlog(0, "h_generation = %u\n",
	     be32_to_cpu(work->w_msg.v_hdr.h_generation));
	mlog(0, "h_node_num = %u\n",
	     be32_to_cpu(work->w_msg.v_hdr.h_node_num));

	spin_lock(&osb->vote_task_lock);
	list_add_tail(&work->w_list, &osb->vote_list);
	osb->vote_count++;
	spin_unlock(&osb->vote_task_lock);

	ocfs2_kick_vote_thread(osb);

	status = 0;
bail:
	return status;
}

void ocfs2_unregister_net_handlers(struct ocfs2_super *osb)
{
	if (!osb->net_key)
		return;

	o2net_unregister_handler_list(&osb->osb_net_handlers);

	if (!list_empty(&osb->net_response_list))
		mlog(ML_ERROR, "net response list not empty!\n");

	osb->net_key = 0;
}

int ocfs2_register_net_handlers(struct ocfs2_super *osb)
{
	int status = 0;

	if (ocfs2_mount_local(osb))
		return 0;

	status = o2net_register_handler(OCFS2_MESSAGE_TYPE_RESPONSE,
					osb->net_key,
					sizeof(struct ocfs2_response_msg),
					ocfs2_handle_response_message,
					osb, NULL, &osb->osb_net_handlers);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	status = o2net_register_handler(OCFS2_MESSAGE_TYPE_VOTE,
					osb->net_key,
					sizeof(struct ocfs2_vote_msg),
					ocfs2_handle_vote_message,
					osb, NULL, &osb->osb_net_handlers);
	if (status) {
		mlog_errno(status);
		goto bail;
	}
bail:
	if (status < 0)
		ocfs2_unregister_net_handlers(osb);

	return status;
}
