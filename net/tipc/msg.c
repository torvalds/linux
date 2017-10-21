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
#define BUF_HEADROOM (LL_MAX_HEADER + 48)
#define BUF_TAILROOM 16

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
struct sk_buff *tipc_buf_acquire(u32 size, gfp_t gfp)
{
	struct sk_buff *skb;
	unsigned int buf_size = (BUF_HEADROOM + size + 3) & ~3u;

	skb = alloc_skb_fclone(buf_size, gfp);
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

	buf = tipc_buf_acquire(hdr_sz + data_sz, GFP_ATOMIC);
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
	struct sk_buff *tail = NULL;
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
		*buf = NULL;
		TIPC_SKB_CB(head)->tail = NULL;
		if (skb_is_nonlinear(head)) {
			skb_walk_frags(head, tail) {
				TIPC_SKB_CB(head)->tail = tail;
			}
		} else {
			skb_frag_list_init(head);
		}
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
		TIPC_SKB_CB(head)->validated = false;
		if (unlikely(!tipc_msg_validate(head)))
			goto err;
		*buf = head;
		TIPC_SKB_CB(head)->tail = NULL;
		*headbuf = NULL;
		return 1;
	}
	*buf = NULL;
	return 0;
err:
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
		skb = tipc_buf_acquire(msz, GFP_KERNEL);
		if (unlikely(!skb))
			return -ENOMEM;
		skb_orphan(skb);
		__skb_queue_tail(list, skb);
		skb_copy_to_linear_data(skb, mhdr, mhsz);
		pktpos = skb->data + mhsz;
		if (copy_from_iter_full(pktpos, dsz, &m->msg_iter))
			return dsz;
		rc = -EFAULT;
		goto error;
	}

	/* Prepare reusable fragment header */
	tipc_msg_init(msg_prevnode(mhdr), &pkthdr, MSG_FRAGMENTER,
		      FIRST_FRAGMENT, INT_H_SIZE, msg_destnode(mhdr));
	msg_set_size(&pkthdr, pktmax);
	msg_set_fragm_no(&pkthdr, pktno);
	msg_set_importance(&pkthdr, msg_importance(mhdr));

	/* Prepare first fragment */
	skb = tipc_buf_acquire(pktmax, GFP_KERNEL);
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

		if (!copy_from_iter_full(pktpos, pktrem, &m->msg_iter)) {
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
		skb = tipc_buf_acquire(pktsz, GFP_KERNEL);
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
 * @skb: the buffer to append to ("bundle")
 * @msg:  message to be appended
 * @mtu:  max allowable size for the bundle buffer
 * Consumes buffer if successful
 * Returns true if bundling could be performed, otherwise false
 */
bool tipc_msg_bundle(struct sk_buff *skb, struct tipc_msg *msg, u32 mtu)
{
	struct tipc_msg *bmsg;
	unsigned int bsz;
	unsigned int msz = msg_size(msg);
	u32 start, pad;
	u32 max = mtu - INT_H_SIZE;

	if (likely(msg_user(msg) == MSG_FRAGMENTER))
		return false;
	if (!skb)
		return false;
	bmsg = buf_msg(skb);
	bsz = msg_size(bmsg);
	start = align(bsz);
	pad = start - bsz;

	if (unlikely(msg_user(msg) == TUNNEL_PROTOCOL))
		return false;
	if (unlikely(msg_user(msg) == BCAST_PROTOCOL))
		return false;
	if (unlikely(msg_user(bmsg) != MSG_BUNDLER))
		return false;
	if (unlikely(skb_tailroom(skb) < (pad + msz)))
		return false;
	if (unlikely(max < (start + msz)))
		return false;
	if ((msg_importance(msg) < TIPC_SYSTEM_IMPORTANCE) &&
	    (msg_importance(bmsg) == TIPC_SYSTEM_IMPORTANCE))
		return false;

	skb_put(skb, pad + msz);
	skb_copy_to_linear_data_offset(skb, start, msg, msz);
	msg_set_size(bmsg, start + msz);
	msg_set_msgcnt(bmsg, msg_msgcnt(bmsg) + 1);
	return true;
}

/**
 *  tipc_msg_extract(): extract bundled inner packet from buffer
 *  @skb: buffer to be extracted from.
 *  @iskb: extracted inner buffer, to be returned
 *  @pos: position in outer message of msg to be extracted.
 *        Returns position of next msg
 *  Consumes outer buffer when last packet extracted
 *  Returns true when when there is an extracted buffer, otherwise false
 */
bool tipc_msg_extract(struct sk_buff *skb, struct sk_buff **iskb, int *pos)
{
	struct tipc_msg *msg;
	int imsz, offset;

	*iskb = NULL;
	if (unlikely(skb_linearize(skb)))
		goto none;

	msg = buf_msg(skb);
	offset = msg_hdr_sz(msg) + *pos;
	if (unlikely(offset > (msg_size(msg) - MIN_H_SIZE)))
		goto none;

	*iskb = skb_clone(skb, GFP_ATOMIC);
	if (unlikely(!*iskb))
		goto none;
	skb_pull(*iskb, offset);
	imsz = msg_size(buf_msg(*iskb));
	skb_trim(*iskb, imsz);
	if (unlikely(!tipc_msg_validate(*iskb)))
		goto none;
	*pos += align(imsz);
	return true;
none:
	kfree_skb(skb);
	kfree_skb(*iskb);
	*iskb = NULL;
	return false;
}

/**
 * tipc_msg_make_bundle(): Create bundle buf and append message to its tail
 * @list: the buffer chain, where head is the buffer to replace/append
 * @skb: buffer to be created, appended to and returned in case of success
 * @msg: message to be appended
 * @mtu: max allowable size for the bundle buffer, inclusive header
 * @dnode: destination node for message. (Not always present in header)
 * Returns true if success, otherwise false
 */
bool tipc_msg_make_bundle(struct sk_buff **skb,  struct tipc_msg *msg,
			  u32 mtu, u32 dnode)
{
	struct sk_buff *_skb;
	struct tipc_msg *bmsg;
	u32 msz = msg_size(msg);
	u32 max = mtu - INT_H_SIZE;

	if (msg_user(msg) == MSG_FRAGMENTER)
		return false;
	if (msg_user(msg) == TUNNEL_PROTOCOL)
		return false;
	if (msg_user(msg) == BCAST_PROTOCOL)
		return false;
	if (msz > (max / 2))
		return false;

	_skb = tipc_buf_acquire(max, GFP_ATOMIC);
	if (!_skb)
		return false;

	skb_trim(_skb, INT_H_SIZE);
	bmsg = buf_msg(_skb);
	tipc_msg_init(msg_prevnode(msg), bmsg, MSG_BUNDLER, 0,
		      INT_H_SIZE, dnode);
	if (msg_isdata(msg))
		msg_set_importance(bmsg, TIPC_CRITICAL_IMPORTANCE);
	else
		msg_set_importance(bmsg, TIPC_SYSTEM_IMPORTANCE);
	msg_set_seqno(bmsg, msg_seqno(msg));
	msg_set_ack(bmsg, msg_ack(msg));
	msg_set_bcast_ack(bmsg, msg_bcast_ack(msg));
	tipc_msg_bundle(_skb, msg, mtu);
	*skb = _skb;
	return true;
}

/**
 * tipc_msg_reverse(): swap source and destination addresses and add error code
 * @own_node: originating node id for reversed message
 * @skb:  buffer containing message to be reversed; may be replaced.
 * @err:  error code to be set in message, if any
 * Consumes buffer at failure
 * Returns true if success, otherwise false
 */
bool tipc_msg_reverse(u32 own_node,  struct sk_buff **skb, int err)
{
	struct sk_buff *_skb = *skb;
	struct tipc_msg *hdr;
	struct tipc_msg ohdr;
	int dlen;

	if (skb_linearize(_skb))
		goto exit;
	hdr = buf_msg(_skb);
	dlen = min_t(uint, msg_data_sz(hdr), MAX_FORWARD_SIZE);
	if (msg_dest_droppable(hdr))
		goto exit;
	if (msg_errcode(hdr))
		goto exit;

	/* Take a copy of original header before altering message */
	memcpy(&ohdr, hdr, msg_hdr_sz(hdr));

	/* Never return SHORT header; expand by replacing buffer if necessary */
	if (msg_short(hdr)) {
		*skb = tipc_buf_acquire(BASIC_H_SIZE + dlen, GFP_ATOMIC);
		if (!*skb)
			goto exit;
		memcpy((*skb)->data + BASIC_H_SIZE, msg_data(hdr), dlen);
		kfree_skb(_skb);
		_skb = *skb;
		hdr = buf_msg(_skb);
		memcpy(hdr, &ohdr, BASIC_H_SIZE);
		msg_set_hdr_sz(hdr, BASIC_H_SIZE);
	}

	if (skb_cloned(_skb) &&
	    pskb_expand_head(_skb, BUF_HEADROOM, BUF_TAILROOM, GFP_ATOMIC))
		goto exit;

	/* reassign after skb header modifications */
	hdr = buf_msg(_skb);
	/* Now reverse the concerned fields */
	msg_set_errcode(hdr, err);
	msg_set_non_seq(hdr, 0);
	msg_set_origport(hdr, msg_destport(&ohdr));
	msg_set_destport(hdr, msg_origport(&ohdr));
	msg_set_destnode(hdr, msg_prevnode(&ohdr));
	msg_set_prevnode(hdr, own_node);
	msg_set_orignode(hdr, own_node);
	msg_set_size(hdr, msg_hdr_sz(hdr) + dlen);
	skb_trim(_skb, msg_size(hdr));
	skb_orphan(_skb);
	return true;
exit:
	kfree_skb(_skb);
	*skb = NULL;
	return false;
}

/**
 * tipc_msg_lookup_dest(): try to find new destination for named message
 * @skb: the buffer containing the message.
 * @err: error code to be used by caller if lookup fails
 * Does not consume buffer
 * Returns true if a destination is found, false otherwise
 */
bool tipc_msg_lookup_dest(struct net *net, struct sk_buff *skb, int *err)
{
	struct tipc_msg *msg = buf_msg(skb);
	u32 dport, dnode;
	u32 onode = tipc_own_addr(net);

	if (!msg_isdata(msg))
		return false;
	if (!msg_named(msg))
		return false;
	if (msg_errcode(msg))
		return false;
	*err = TIPC_ERR_NO_NAME;
	if (skb_linearize(skb))
		return false;
	msg = buf_msg(skb);
	if (msg_reroute_cnt(msg))
		return false;
	dnode = addr_domain(net, msg_lookup_scope(msg));
	dport = tipc_nametbl_translate(net, msg_nametype(msg),
				       msg_nameinst(msg), &dnode);
	if (!dport)
		return false;
	msg_incr_reroute_cnt(msg);
	if (dnode != onode)
		msg_set_prevnode(msg, onode);
	msg_set_destnode(msg, dnode);
	msg_set_destport(msg, dport);
	*err = TIPC_OK;
	return true;
}

/* tipc_msg_reassemble() - clone a buffer chain of fragments and
 *                         reassemble the clones into one message
 */
bool tipc_msg_reassemble(struct sk_buff_head *list, struct sk_buff_head *rcvq)
{
	struct sk_buff *skb, *_skb;
	struct sk_buff *frag = NULL;
	struct sk_buff *head = NULL;
	int hdr_len;

	/* Copy header if single buffer */
	if (skb_queue_len(list) == 1) {
		skb = skb_peek(list);
		hdr_len = skb_headroom(skb) + msg_hdr_sz(buf_msg(skb));
		_skb = __pskb_copy(skb, hdr_len, GFP_ATOMIC);
		if (!_skb)
			return false;
		__skb_queue_tail(rcvq, _skb);
		return true;
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
	__skb_queue_tail(rcvq, frag);
	return true;
error:
	pr_warn("Failed do clone local mcast rcv buffer\n");
	kfree_skb(head);
	return false;
}

bool tipc_msg_pskb_copy(u32 dst, struct sk_buff_head *msg,
			struct sk_buff_head *cpy)
{
	struct sk_buff *skb, *_skb;

	skb_queue_walk(msg, skb) {
		_skb = pskb_copy(skb, GFP_ATOMIC);
		if (!_skb) {
			__skb_queue_purge(cpy);
			return false;
		}
		msg_set_destnode(buf_msg(_skb), dst);
		__skb_queue_tail(cpy, _skb);
	}
	return true;
}

/* tipc_skb_queue_sorted(); sort pkt into list according to sequence number
 * @list: list to be appended to
 * @seqno: sequence number of buffer to add
 * @skb: buffer to add
 */
void __tipc_skb_queue_sorted(struct sk_buff_head *list, u16 seqno,
			     struct sk_buff *skb)
{
	struct sk_buff *_skb, *tmp;

	if (skb_queue_empty(list) || less(seqno, buf_seqno(skb_peek(list)))) {
		__skb_queue_head(list, skb);
		return;
	}

	if (more(seqno, buf_seqno(skb_peek_tail(list)))) {
		__skb_queue_tail(list, skb);
		return;
	}

	skb_queue_walk_safe(list, _skb, tmp) {
		if (more(seqno, buf_seqno(_skb)))
			continue;
		if (seqno == buf_seqno(_skb))
			break;
		__skb_queue_before(list, _skb, skb);
		return;
	}
	kfree_skb(skb);
}
