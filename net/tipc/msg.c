/*
 * net/tipc/msg.c: TIPC message header routines
 *
 * Copyright (c) 2000-2006, 2014-2015, Ericsson AB
 * Copyright (c) 2005, 2010-2011, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <net/sock.h>
#include "core.h"
#include "msg.h"
#include "addr.h"
#include "name_table.h"

#define MAX_FORWARD_SIZE 1024

static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

/**
 * tipc_buf_acquire - creates a TIPC message buffer
 * @size: message size (including TIPC header)
 *
 * Returns a new buffer with data pointers set to the specified size.
 *
 * NOTE: Headroom is reserved to allow prepending of a data link header.
 *       There may also be unrequested tailroom present at the buffer's end.
 */
struct sk_buff *tipc_buf_acquire(u32 size)
{
	struct sk_buff *skb;
	unsigned int buf_size = (BUF_HEADROOM + size + 3) & ~3u;

	skb = alloc_skb_fclone(buf_size, GFP_ATOMIC);
	if (skb) {
		skb_reserve(skb, BUF_HEADROOM);
		skb_put(skb, size);
		skb->next = NULL;
	}
	return skb;
}

void tipc_msg_init(u32 own_node, struct tipc_msg *m, u32 user, u32 type,
		   u32 hsize, u32 dnode)
{
	memset(m, 0, hsize);
	msg_set_version(m);
	msg_set_user(m, user);
	msg_set_hdr_sz(m, hsize);
	msg_set_size(m, hsize);
	msg_set_prevnode(m, own_node);
	msg_set_type(m, type);
	if (hsize > SHORT_H_SIZE) {
		msg_set_orignode(m, own_node);
		msg_set_destnode(m, dnode);
	}
}

struct sk_buff *tipc_msg_create(uint user, uint type,
				uint hdr_sz, uint data_sz, u32 dnode,
				u32 onode, u32 dport, u32 oport, int errcode)
{
	struct tipc_msg *msg;
	struct sk_buff *buf;

	buf = tipc_buf_acquire(hdr_sz + data_sz);
	if (unlikely(!buf))
		return NULL;

	msg = buf_msg(buf);
	tipc_msg_init(onode, msg, user, type, hdr_sz, dnode);
	msg_set_size(msg, hdr_sz + data_sz);
	msg_set_origport(msg, oport);
	msg_set_destport(msg, dport);
	msg_set_errcode(msg, errcode);
	if (hdr_sz > SHORT_H_SIZE) {
		msg_set_orignode(msg, onode);
		msg_set_destnode(msg, dnode);
	}
	return buf;
}

/* tipc_buf_append(): Append a buffer to the fragment list of another buffer
 * @*headbuf: in:  NULL for first frag, otherwise value returned from prev call
 *            out: set when successful non-complete reassembly, otherwise NULL
 * @*buf:     in:  the buffer to append. Always defined
 *            out: head buf after successful complete reassembly, otherwise NULL
 * Returns 1 when reassembly complete, otherwise 0
 */
int tipc_buf_append(struct sk_buff **headbuf, struct sk_buff **buf)
{
	struct sk_buff *head = *headbuf;
	struct sk_buff *frag = *buf;
	struct sk_buff *tail;
	struct tipc_msg *msg;
	u32 fragid;
	int delta;
	bool headstolen;

	if (!frag)
		goto err;

	msg = buf_msg(frag);
	fragid = msg_type(msg);
	frag->next = NULL;
	skb_pull(frag, msg_hdr_sz(msg));

	if (fragid == FIRST_FRAGMENT) {
		if (unlikely(head))
			goto err;
		if (unlikely(skb_unclone(frag, GFP_ATOMIC)))
			goto err;
		head = *headbuf = frag;
		skb_frag_list_init(head);
		TIPC_SKB_CB(head)->tail = NULL;
		*buf = NULL;
		return 0;
	}

	if (!head)
		goto err;

	if (skb_try_coalesce(head, frag, &headstolen, &delta)) {
		kfree_skb_partial(frag, headstolen);
	} else {
		tail = TIPC_SKB_CB(head)->tail;
		if (!skb_has_frag_list(head))
			skb_shinfo(head)->frag_list = frag;
		else
			tail->next = frag;
		head->truesize += frag->truesize;
		head->data_len += frag->len;
		head->len += frag->len;
		TIPC_SKB_CB(head)->tail = frag;
	}

	if (fragid == LAST_FRAGMENT) {
		*buf = head;
		TIPC_SKB_CB(head)->tail = NULL;
		*headbuf = NULL;
		return 1;
	}
	*buf = NULL;
	return 0;

err:
	pr_warn_ratelimited("Unable to build fragment list\n");
	kfree_skb(*buf);
	kfree_skb(*headbuf);
	*buf = *headbuf = NULL;
	return 0;
}

/* tipc_msg_validate - validate basic format of received message
 *
 * This routine ensures a TIPC message has an acceptable header, and at least
 * as much data as the header indicates it should.  The routine also ensures
 * that the entire message header is stored in the main fragment of the message
 * buffer, to simplify future access to message header fields.
 *
 * Note: Having extra info present in the message header or data areas is OK.
 * TIPC will ignore the excess, under the assumption that it is optional info
 * introduced by a later release of the protocol.
 */
bool tipc_msg_validate(struct sk_buff *skb)
{
	struct tipc_msg *msg;
	int msz, hsz;

	if (unlikely(TIPC_SKB_CB(skb)->validated))
		return true;
	if (unlikely(!pskb_may_pull(skb, MIN_H_SIZE)))
		return false;

	hsz = msg_hdr_sz(buf_msg(skb));
	if (unlikely(hsz < MIN_H_SIZE) || (hsz > MAX_H_SIZE))
		return false;
	if (unlikely(!pskb_may_pull(skb, hsz)))
		return false;

	msg = buf_msg(skb);
	if (unlikely(msg_version(msg) != TIPC_VERSION))
		return false;

	msz = msg_size(msg);
	if (unlikely(msz < hsz))
		return false;
	if (unlikely((msz - hsz) > TIPC_MAX_USER_MSG_SIZE))
		return false;
	if (unlikely(skb->len < msz))
		return false;

	TIPC_SKB_CB(skb)->validated = true;
	return true;
}

/**
 * tipc_msg_build - create buffer chain containing specified header and data
 * @mhdr: Message header, to be prepended to data
 * @m: User message
 * @dsz: Total length of user data
 * @pktmax: Max packet size that can be used
 * @list: Buffer or chain of buffers to be returned to caller
 *
 * Returns message data size or errno: -ENOMEM, -EFAULT
 */
int tipc_msg_build(struct tipc_msg *mhdr, struct msghdr *m,
		   int offset, int dsz, int pktmax, struct sk_buff_head *list)
{
	int mhsz = msg_hdr_sz(mhdr);
	int msz = mhsz + dsz;
	int pktno = 1;
	int pktsz;
	int pktrem = pktmax;
	int drem = dsz;
	struct tipc_msg pkthdr;
	struct sk_buff *skb;
	char *pktpos;
	int rc;

	msg_set_size(mhdr, msz);

	/* No fragmentation needed? */
	if (likely(msz <= pktmax)) {
		skb = tipc_buf_acquire(msz);
		if (unlikely(!skb))
			return -ENOMEM;
		skb_orphan(skb);
		__skb_queue_tail(list, skb);
		skb_copy_to_linear_data(skb, mhdr, mhsz);
		pktpos = skb->data + mhsz;
		if (copy_from_iter(pktpos, dsz, &m->msg_iter) == dsz)
			return dsz;
		rc = -EFAULT;
		goto error;
	}

	/* Prepare reusable fragment header */
	tipc_msg_init(msg_prevnode(mhdr), &pkthdr, MSG_FRAGMENTER,
		      FIRST_FRAGMENT, INT_H_SIZE, msg_destnode(mhdr));
	msg_set_size(&pkthdr, pktmax);
	msg_set_fragm_no(&pkthdr, pktno);

	/* Prepare first fragment */
	skb = tipc_buf_acquire(pktmax);
	if (!skb)
		return -ENOMEM;
	skb_orphan(skb);
	__skb_queue_tail(list, skb);
	pktpos = skb->data;
	skb_copy_to_linear_data(skb, &pkthdr, INT_H_SIZE);
	pktpos += INT_H_SIZE;
	pktrem -= INT_H_SIZE;
	skb_copy_to_linear_data_offset(skb, INT_H_SIZE, mhdr, mhsz);
	pktpos += mhsz;
	pktrem -= mhsz;

	do {
		if (drem < pktrem)
			pktrem = drem;

		if (copy_from_iter(pktpos, pktrem, &m->msg_iter) != pktrem) {
			rc = -EFAULT;
			goto error;
		}
		drem -= pktrem;

		if (!drem)
			break;

		/* Prepare new fragment: */
		if (drem < (pktmax - INT_H_SIZE))
			pktsz = drem + INT_H_SIZE;
		else
			pktsz = pktmax;
		skb = tipc_buf_acquire(pktsz);
		if (!skb) {
			rc = -ENOMEM;
			goto error;
		}
		skb_orphan(skb);
		__skb_queue_tail(list, skb);
		msg_set_type(&pkthdr, FRAGMENT);
		msg_set_size(&pkthdr, pktsz);
		msg_set_fragm_no(&pkthdr, ++pktno);
		skb_copy_to_linear_data(skb, &pkthdr, INT_H_SIZE);
		pktpos = skb->data + INT_H_SIZE;
		pktrem = pktsz - INT_H_SIZE;

	} while (1);
	msg_set_type(buf_msg(skb), LAST_FRAGMENT);
	return dsz;
error:
	__skb_queue_purge(list);
	__skb_queue_head_init(list);
	return rc;
}

/**
 * tipc_msg_bundle(): Append contents of a buffer to tail of an existing one
 * @list: the buffer chain of the existing buffer ("bundle")
 * @skb:  buffer to be appended
 * @mtu:  max allowable size for the bundle buffer
 * Consumes buffer if successful
 * Returns true if bundling could be performed, otherwise false
 */
bool tipc_msg_bundle(struct sk_buff_head *list, struct sk_buff *skb, u32 mtu)
{
	struct sk_buff *bskb = skb_peek_tail(list);
	struct tipc_msg *bmsg = buf_msg(bskb);
	struct tipc_msg *msg = buf_msg(skb);
	unsigned int bsz = msg_size(bmsg);
	unsigned int msz = msg_size(msg);
	u32 start = align(bsz);
	u32 max = mtu - INT_H_SIZE;
	u32 pad = start - bsz;

	if (likely(msg_user(msg) == MSG_FRAGMENTER))
		return false;
	if (unlikely(msg_user(msg) == CHANGEOVER_PROTOCOL))
		return false;
	if (unlikely(msg_user(msg) == BCAST_PROTOCOL))
		return false;
	if (likely(msg_user(bmsg) != MSG_BUNDLER))
		return false;
	if (likely(!TIPC_SKB_CB(bskb)->bundling))
		return false;
	if (unlikely(skb_tailroom(bskb) < (pad + msz)))
		return false;
	if (unlikely(max < (start + msz)))
		return false;

	skb_put(bskb, pad + msz);
	skb_copy_to_linear_data_offset(bskb, start, skb->data, msz);
	msg_set_size(bmsg, start + msz);
	msg_set_msgcnt(bmsg, msg_msgcnt(bmsg) + 1);
	kfree_skb(skb);
	return true;
}

/**
 *  tipc_msg_extract(): extract bundled inner packet from buffer
 *  @skb: linear outer buffer, to be extracted from.
 *  @iskb: extracted inner buffer, to be returned
 *  @pos: position of msg to be extracted. Returns with pointer of next msg
 *  Consumes outer buffer when last packet extracted
 *  Returns true when when there is an extracted buffer, otherwise false
 */
bool tipc_msg_extract(struct sk_buff *skb, struct sk_buff **iskb, int *pos)
{
	struct tipc_msg *msg = buf_msg(skb);
	int imsz;
	struct tipc_msg *imsg = (struct tipc_msg *)(msg_data(msg) + *pos);

	/* Is there space left for shortest possible message? */
	if (*pos > (msg_data_sz(msg) - SHORT_H_SIZE))
		goto none;
	imsz = msg_size(imsg);

	/* Is there space left for current message ? */
	if ((*pos + imsz) > msg_data_sz(msg))
		goto none;
	*iskb = tipc_buf_acquire(imsz);
	if (!*iskb)
		goto none;
	skb_copy_to_linear_data(*iskb, imsg, imsz);
	*pos += align(imsz);
	return true;
none:
	kfree_skb(skb);
	*iskb = NULL;
	return false;
}

/**
 * tipc_msg_make_bundle(): Create bundle buf and append message to its tail
 * @list: the buffer chain
 * @skb: buffer to be appended and replaced
 * @mtu: max allowable size for the bundle buffer, inclusive header
 * @dnode: destination node for message. (Not always present in header)
 * Replaces buffer if successful
 * Returns true if success, otherwise false
 */
bool tipc_msg_make_bundle(struct sk_buff_head *list,
			  struct sk_buff *skb, u32 mtu, u32 dnode)
{
	struct sk_buff *bskb;
	struct tipc_msg *bmsg;
	struct tipc_msg *msg = buf_msg(skb);
	u32 msz = msg_size(msg);
	u32 max = mtu - INT_H_SIZE;

	if (msg_user(msg) == MSG_FRAGMENTER)
		return false;
	if (msg_user(msg) == CHANGEOVER_PROTOCOL)
		return false;
	if (msg_user(msg) == BCAST_PROTOCOL)
		return false;
	if (msz > (max / 2))
		return false;

	bskb = tipc_buf_acquire(max);
	if (!bskb)
		return false;

	skb_trim(bskb, INT_H_SIZE);
	bmsg = buf_msg(bskb);
	tipc_msg_init(msg_prevnode(msg), bmsg, MSG_BUNDLER, 0,
		      INT_H_SIZE, dnode);
	msg_set_seqno(bmsg, msg_seqno(msg));
	msg_set_ack(bmsg, msg_ack(msg));
	msg_set_bcast_ack(bmsg, msg_bcast_ack(msg));
	TIPC_SKB_CB(bskb)->bundling = true;
	__skb_queue_tail(list, bskb);
	return tipc_msg_bundle(list, skb, mtu);
}

/**
 * tipc_msg_reverse(): swap source and destination addresses and add error code
 * @buf:  buffer containing message to be reversed
 * @dnode: return value: node where to send message after reversal
 * @err:  error code to be set in message
 * Consumes buffer if failure
 * Returns true if success, otherwise false
 */
bool tipc_msg_reverse(u32 own_addr,  struct sk_buff *buf, u32 *dnode,
		      int err)
{
	struct tipc_msg *msg = buf_msg(buf);
	uint imp = msg_importance(msg);
	struct tipc_msg ohdr;
	uint rdsz = min_t(uint, msg_data_sz(msg), MAX_FORWARD_SIZE);

	if (skb_linearize(buf))
		goto exit;
	if (msg_dest_droppable(msg))
		goto exit;
	if (msg_errcode(msg))
		goto exit;

	memcpy(&ohdr, msg, msg_hdr_sz(msg));
	imp = min_t(uint, imp + 1, TIPC_CRITICAL_IMPORTANCE);
	if (msg_isdata(msg))
		msg_set_importance(msg, imp);
	msg_set_errcode(msg, err);
	msg_set_origport(msg, msg_destport(&ohdr));
	msg_set_destport(msg, msg_origport(&ohdr));
	msg_set_prevnode(msg, own_addr);
	if (!msg_short(msg)) {
		msg_set_orignode(msg, msg_destnode(&ohdr));
		msg_set_destnode(msg, msg_orignode(&ohdr));
	}
	msg_set_size(msg, msg_hdr_sz(msg) + rdsz);
	skb_trim(buf, msg_size(msg));
	skb_orphan(buf);
	*dnode = msg_orignode(&ohdr);
	return true;
exit:
	kfree_skb(buf);
	*dnode = 0;
	return false;
}

/**
 * tipc_msg_lookup_dest(): try to find new destination for named message
 * @skb: the buffer containing the message.
 * @dnode: return value: next-hop node, if destination found
 * @err: return value: error code to use, if message to be rejected
 * Does not consume buffer
 * Returns true if a destination is found, false otherwise
 */
bool tipc_msg_lookup_dest(struct net *net, struct sk_buff *skb,
			  u32 *dnode, int *err)
{
	struct tipc_msg *msg = buf_msg(skb);
	u32 dport;

	if (!msg_isdata(msg))
		return false;
	if (!msg_named(msg))
		return false;
	*err = -TIPC_ERR_NO_NAME;
	if (skb_linearize(skb))
		return false;
	if (msg_reroute_cnt(msg) > 0)
		return false;
	*dnode = addr_domain(net, msg_lookup_scope(msg));
	dport = tipc_nametbl_translate(net, msg_nametype(msg),
				       msg_nameinst(msg), dnode);
	if (!dport)
		return false;
	msg_incr_reroute_cnt(msg);
	msg_set_destnode(msg, *dnode);
	msg_set_destport(msg, dport);
	*err = TIPC_OK;
	return true;
}

/* tipc_msg_reassemble() - clone a buffer chain of fragments and
 *                         reassemble the clones into one message
 */
struct sk_buff *tipc_msg_reassemble(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	struct sk_buff *frag = NULL;
	struct sk_buff *head = NULL;
	int hdr_sz;

	/* Copy header if single buffer */
	if (skb_queue_len(list) == 1) {
		skb = skb_peek(list);
		hdr_sz = skb_headroom(skb) + msg_hdr_sz(buf_msg(skb));
		return __pskb_copy(skb, hdr_sz, GFP_ATOMIC);
	}

	/* Clone all fragments and reassemble */
	skb_queue_walk(list, skb) {
		frag = skb_clone(skb, GFP_ATOMIC);
		if (!frag)
			goto error;
		frag->next = NULL;
		if (tipc_buf_append(&head, &frag))
			break;
		if (!head)
			goto error;
	}
	return frag;
error:
	pr_warn("Failed do clone local mcast rcv buffer\n");
	kfree_skb(head);
	return NULL;
}
