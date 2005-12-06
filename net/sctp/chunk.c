/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2003, 2004
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This file contains the code relating the the chunk abstraction.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* This file is mostly in anticipation of future work, but initially
 * populate with fragment tracking for an outbound message.
 */

/* Initialize datamsg from memory. */
static void sctp_datamsg_init(struct sctp_datamsg *msg)
{
	atomic_set(&msg->refcnt, 1);
	msg->send_failed = 0;
	msg->send_error = 0;
	msg->can_abandon = 0;
	msg->expires_at = 0;
	INIT_LIST_HEAD(&msg->chunks);
}

/* Allocate and initialize datamsg. */
SCTP_STATIC struct sctp_datamsg *sctp_datamsg_new(gfp_t gfp)
{
	struct sctp_datamsg *msg;
	msg = kmalloc(sizeof(struct sctp_datamsg), gfp);
	if (msg)
		sctp_datamsg_init(msg);
	SCTP_DBG_OBJCNT_INC(datamsg);
	return msg;
}

/* Final destructruction of datamsg memory. */
static void sctp_datamsg_destroy(struct sctp_datamsg *msg)
{
	struct list_head *pos, *temp;
	struct sctp_chunk *chunk;
	struct sctp_sock *sp;
	struct sctp_ulpevent *ev;
	struct sctp_association *asoc = NULL;
	int error = 0, notify;

	/* If we failed, we may need to notify. */
	notify = msg->send_failed ? -1 : 0;

	/* Release all references. */
	list_for_each_safe(pos, temp, &msg->chunks) {
		list_del_init(pos);
		chunk = list_entry(pos, struct sctp_chunk, frag_list);
		/* Check whether we _really_ need to notify. */
		if (notify < 0) {
			asoc = chunk->asoc;
			if (msg->send_error)
				error = msg->send_error;
			else
				error = asoc->outqueue.error;

			sp = sctp_sk(asoc->base.sk);
			notify = sctp_ulpevent_type_enabled(SCTP_SEND_FAILED,
							    &sp->subscribe);
		}

		/* Generate a SEND FAILED event only if enabled. */
		if (notify > 0) {
			int sent;
			if (chunk->has_tsn)
				sent = SCTP_DATA_SENT;
			else
				sent = SCTP_DATA_UNSENT;

			ev = sctp_ulpevent_make_send_failed(asoc, chunk, sent,
							    error, GFP_ATOMIC);
			if (ev)
				sctp_ulpq_tail_event(&asoc->ulpq, ev);
		}

		sctp_chunk_put(chunk);
	}

	SCTP_DBG_OBJCNT_DEC(datamsg);
	kfree(msg);
}

/* Hold a reference. */
static void sctp_datamsg_hold(struct sctp_datamsg *msg)
{
	atomic_inc(&msg->refcnt);
}

/* Release a reference. */
void sctp_datamsg_put(struct sctp_datamsg *msg)
{
	if (atomic_dec_and_test(&msg->refcnt))
		sctp_datamsg_destroy(msg);
}

/* Free a message.  Really just give up a reference, the
 * really free happens in sctp_datamsg_destroy().
 */
void sctp_datamsg_free(struct sctp_datamsg *msg)
{
	sctp_datamsg_put(msg);
}

/* Hold on to all the fragments until all chunks have been sent. */
void sctp_datamsg_track(struct sctp_chunk *chunk)
{
	sctp_chunk_hold(chunk);
}

/* Assign a chunk to this datamsg. */
static void sctp_datamsg_assign(struct sctp_datamsg *msg, struct sctp_chunk *chunk)
{
	sctp_datamsg_hold(msg);
	chunk->msg = msg;
}


/* A data chunk can have a maximum payload of (2^16 - 20).  Break
 * down any such message into smaller chunks.  Opportunistically, fragment
 * the chunks down to the current MTU constraints.  We may get refragmented
 * later if the PMTU changes, but it is _much better_ to fragment immediately
 * with a reasonable guess than always doing our fragmentation on the
 * soft-interrupt.
 */
struct sctp_datamsg *sctp_datamsg_from_user(struct sctp_association *asoc,
					    struct sctp_sndrcvinfo *sinfo,
					    struct msghdr *msgh, int msg_len)
{
	int max, whole, i, offset, over, err;
	int len, first_len;
	struct sctp_chunk *chunk;
	struct sctp_datamsg *msg;
	struct list_head *pos, *temp;
	__u8 frag;

	msg = sctp_datamsg_new(GFP_KERNEL);
	if (!msg)
		return NULL;

	/* Note: Calculate this outside of the loop, so that all fragments
	 * have the same expiration.
	 */
	if (sinfo->sinfo_timetolive) {
		/* sinfo_timetolive is in milliseconds */
		msg->expires_at = jiffies +
				    msecs_to_jiffies(sinfo->sinfo_timetolive);
		msg->can_abandon = 1;
		SCTP_DEBUG_PRINTK("%s: msg:%p expires_at: %ld jiffies:%ld\n",
				  __FUNCTION__, msg, msg->expires_at, jiffies);
	}

	max = asoc->frag_point;

	whole = 0;
	first_len = max;

	/* Encourage Cookie-ECHO bundling. */
	if (asoc->state < SCTP_STATE_COOKIE_ECHOED) {
		whole = msg_len / (max - SCTP_ARBITRARY_COOKIE_ECHO_LEN);

		/* Account for the DATA to be bundled with the COOKIE-ECHO. */
		if (whole) {
			first_len = max - SCTP_ARBITRARY_COOKIE_ECHO_LEN;
			msg_len -= first_len;
			whole = 1;
		}
	}

	/* How many full sized?  How many bytes leftover? */
	whole += msg_len / max;
	over = msg_len % max;
	offset = 0;

	if ((whole > 1) || (whole && over))
		SCTP_INC_STATS_USER(SCTP_MIB_FRAGUSRMSGS);

	/* Create chunks for all the full sized DATA chunks. */
	for (i=0, len=first_len; i < whole; i++) {
		frag = SCTP_DATA_MIDDLE_FRAG;

		if (0 == i)
			frag |= SCTP_DATA_FIRST_FRAG;

		if ((i == (whole - 1)) && !over)
			frag |= SCTP_DATA_LAST_FRAG;

		chunk = sctp_make_datafrag_empty(asoc, sinfo, len, frag, 0);

		if (!chunk)
			goto errout;
		err = sctp_user_addto_chunk(chunk, offset, len, msgh->msg_iov);
		if (err < 0)
			goto errout;

		offset += len;

		/* Put the chunk->skb back into the form expected by send.  */
		__skb_pull(chunk->skb, (__u8 *)chunk->chunk_hdr
			   - (__u8 *)chunk->skb->data);

		sctp_datamsg_assign(msg, chunk);
		list_add_tail(&chunk->frag_list, &msg->chunks);

		/* The first chunk, the first chunk was likely short
		 * to allow bundling, so reset to full size.
		 */
		if (0 == i)
			len = max;
	}

	/* .. now the leftover bytes. */
	if (over) {
		if (!whole)
			frag = SCTP_DATA_NOT_FRAG;
		else
			frag = SCTP_DATA_LAST_FRAG;

		chunk = sctp_make_datafrag_empty(asoc, sinfo, over, frag, 0);

		if (!chunk)
			goto errout;

		err = sctp_user_addto_chunk(chunk, offset, over,msgh->msg_iov);

		/* Put the chunk->skb back into the form expected by send.  */
		__skb_pull(chunk->skb, (__u8 *)chunk->chunk_hdr
			   - (__u8 *)chunk->skb->data);
		if (err < 0)
			goto errout;

		sctp_datamsg_assign(msg, chunk);
		list_add_tail(&chunk->frag_list, &msg->chunks);
	}

	return msg;

errout:
	list_for_each_safe(pos, temp, &msg->chunks) {
		list_del_init(pos);
		chunk = list_entry(pos, struct sctp_chunk, frag_list);
		sctp_chunk_free(chunk);
	}
	sctp_datamsg_free(msg);
	return NULL;
}

/* Check whether this message has expired. */
int sctp_chunk_abandoned(struct sctp_chunk *chunk)
{
	struct sctp_datamsg *msg = chunk->msg;

	if (!msg->can_abandon)
		return 0;

	if (time_after(jiffies, msg->expires_at))
		return 1;

	return 0;
}

/* This chunk (and consequently entire message) has failed in its sending. */
void sctp_chunk_fail(struct sctp_chunk *chunk, int error)
{
	chunk->msg->send_failed = 1;
	chunk->msg->send_error = error;
}
