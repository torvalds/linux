/*
 * net/tipc/msg.c: TIPC message header routines
 *
 * Copyright (c) 2000-2006, 2014, Ericsson AB
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

#include "core.h"
#include "msg.h"

static unsigned int align(unsigned int i)
{
	return (i + 3) & ~3u;
}

void tipc_msg_init(struct tipc_msg *m, u32 user, u32 type, u32 hsize,
		   u32 destnode)
{
	memset(m, 0, hsize);
	msg_set_version(m);
	msg_set_user(m, user);
	msg_set_hdr_sz(m, hsize);
	msg_set_size(m, hsize);
	msg_set_prevnode(m, tipc_own_addr);
	msg_set_type(m, type);
	msg_set_orignode(m, tipc_own_addr);
	msg_set_destnode(m, destnode);
}

/**
 * tipc_msg_build - create message using specified header and data
 *
 * Note: Caller must not hold any locks in case copy_from_user() is interrupted!
 *
 * Returns message data size or errno
 */
int tipc_msg_build(struct tipc_msg *hdr, struct iovec const *msg_sect,
		   unsigned int len, int max_size, struct sk_buff **buf)
{
	int dsz, sz, hsz;
	unsigned char *to;

	dsz = len;
	hsz = msg_hdr_sz(hdr);
	sz = hsz + dsz;
	msg_set_size(hdr, sz);
	if (unlikely(sz > max_size)) {
		*buf = NULL;
		return dsz;
	}

	*buf = tipc_buf_acquire(sz);
	if (!(*buf))
		return -ENOMEM;
	skb_copy_to_linear_data(*buf, hdr, hsz);
	to = (*buf)->data + hsz;
	if (len && memcpy_fromiovecend(to, msg_sect, 0, dsz)) {
		kfree_skb(*buf);
		*buf = NULL;
		return -EFAULT;
	}
	return dsz;
}

/* tipc_buf_append(): Append a buffer to the fragment list of another buffer
 * Let first buffer become head buffer
 * Returns 1 and sets *buf to headbuf if chain is complete, otherwise 0
 * Leaves headbuf pointer at NULL if failure
 */
int tipc_buf_append(struct sk_buff **headbuf, struct sk_buff **buf)
{
	struct sk_buff *head = *headbuf;
	struct sk_buff *frag = *buf;
	struct sk_buff *tail;
	struct tipc_msg *msg = buf_msg(frag);
	u32 fragid = msg_type(msg);
	bool headstolen;
	int delta;

	skb_pull(frag, msg_hdr_sz(msg));

	if (fragid == FIRST_FRAGMENT) {
		if (head || skb_unclone(frag, GFP_ATOMIC))
			goto out_free;
		head = *headbuf = frag;
		skb_frag_list_init(head);
		return 0;
	}
	if (!head)
		goto out_free;
	tail = TIPC_SKB_CB(head)->tail;
	if (skb_try_coalesce(head, frag, &headstolen, &delta)) {
		kfree_skb_partial(frag, headstolen);
	} else {
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
out_free:
	pr_warn_ratelimited("Unable to build fragment list\n");
	kfree_skb(*buf);
	return 0;
}

/**
 * tipc_msg_bundle(): Append contents of a buffer to tail of an existing one
 * @bbuf: the existing buffer ("bundle")
 * @buf:  buffer to be appended
 * @mtu:  max allowable size for the bundle buffer
 * Consumes buffer if successful
 * Returns true if bundling could be performed, otherwise false
 */
bool tipc_msg_bundle(struct sk_buff *bbuf, struct sk_buff *buf, u32 mtu)
{
	struct tipc_msg *bmsg = buf_msg(bbuf);
	struct tipc_msg *msg = buf_msg(buf);
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
	if (likely(msg_type(bmsg) != BUNDLE_OPEN))
		return false;
	if (unlikely(skb_tailroom(bbuf) < (pad + msz)))
		return false;
	if (unlikely(max < (start + msz)))
		return false;

	skb_put(bbuf, pad + msz);
	skb_copy_to_linear_data_offset(bbuf, start, buf->data, msz);
	msg_set_size(bmsg, start + msz);
	msg_set_msgcnt(bmsg, msg_msgcnt(bmsg) + 1);
	bbuf->next = buf->next;
	kfree_skb(buf);
	return true;
}

/**
 * tipc_msg_make_bundle(): Create bundle buf and append message to its tail
 * @buf:  buffer to be appended and replaced
 * @mtu:  max allowable size for the bundle buffer, inclusive header
 * @dnode: destination node for message. (Not always present in header)
 * Replaces buffer if successful
 * Returns true if sucess, otherwise false
 */
bool tipc_msg_make_bundle(struct sk_buff **buf, u32 mtu, u32 dnode)
{
	struct sk_buff *bbuf;
	struct tipc_msg *bmsg;
	struct tipc_msg *msg = buf_msg(*buf);
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

	bbuf = tipc_buf_acquire(max);
	if (!bbuf)
		return false;

	skb_trim(bbuf, INT_H_SIZE);
	bmsg = buf_msg(bbuf);
	tipc_msg_init(bmsg, MSG_BUNDLER, BUNDLE_OPEN, INT_H_SIZE, dnode);
	msg_set_seqno(bmsg, msg_seqno(msg));
	msg_set_ack(bmsg, msg_ack(msg));
	msg_set_bcast_ack(bmsg, msg_bcast_ack(msg));
	bbuf->next = (*buf)->next;
	tipc_msg_bundle(bbuf, *buf, mtu);
	*buf = bbuf;
	return true;
}
