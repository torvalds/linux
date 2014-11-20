/*
 * net/tipc/msg.h: Include file for TIPC message header routines
 *
 * Copyright (c) 2000-2007, 2014, Ericsson AB
 * Copyright (c) 2005-2008, 2010-2011, Wind River Systems
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

#ifndef _TIPC_MSG_H
#define _TIPC_MSG_H

#include "bearer.h"

/*
 * Constants and routines used to read and write TIPC payload message headers
 *
 * Note: Some items are also used with TIPC internal message headers
 */
#define TIPC_VERSION              2

/*
 * Payload message users are defined in TIPC's public API:
 * - TIPC_LOW_IMPORTANCE
 * - TIPC_MEDIUM_IMPORTANCE
 * - TIPC_HIGH_IMPORTANCE
 * - TIPC_CRITICAL_IMPORTANCE
 */

/*
 * Payload message types
 */
#define TIPC_CONN_MSG		0
#define TIPC_MCAST_MSG		1
#define TIPC_NAMED_MSG		2
#define TIPC_DIRECT_MSG		3

/*
 * Message header sizes
 */
#define SHORT_H_SIZE              24	/* In-cluster basic payload message */
#define BASIC_H_SIZE              32	/* Basic payload message */
#define NAMED_H_SIZE              40	/* Named payload message */
#define MCAST_H_SIZE              44	/* Multicast payload message */
#define INT_H_SIZE                40	/* Internal messages */
#define MIN_H_SIZE                24	/* Smallest legal TIPC header size */
#define MAX_H_SIZE                60	/* Largest possible TIPC header size */

#define MAX_MSG_SIZE (MAX_H_SIZE + TIPC_MAX_USER_MSG_SIZE)

#define TIPC_MEDIA_ADDR_OFFSET	5


struct tipc_msg {
	__be32 hdr[15];
};


static inline u32 msg_word(struct tipc_msg *m, u32 pos)
{
	return ntohl(m->hdr[pos]);
}

static inline void msg_set_word(struct tipc_msg *m, u32 w, u32 val)
{
	m->hdr[w] = htonl(val);
}

static inline u32 msg_bits(struct tipc_msg *m, u32 w, u32 pos, u32 mask)
{
	return (msg_word(m, w) >> pos) & mask;
}

static inline void msg_set_bits(struct tipc_msg *m, u32 w,
				u32 pos, u32 mask, u32 val)
{
	val = (val & mask) << pos;
	mask = mask << pos;
	m->hdr[w] &= ~htonl(mask);
	m->hdr[w] |= htonl(val);
}

static inline void msg_swap_words(struct tipc_msg *msg, u32 a, u32 b)
{
	u32 temp = msg->hdr[a];

	msg->hdr[a] = msg->hdr[b];
	msg->hdr[b] = temp;
}

/*
 * Word 0
 */
static inline u32 msg_version(struct tipc_msg *m)
{
	return msg_bits(m, 0, 29, 7);
}

static inline void msg_set_version(struct tipc_msg *m)
{
	msg_set_bits(m, 0, 29, 7, TIPC_VERSION);
}

static inline u32 msg_user(struct tipc_msg *m)
{
	return msg_bits(m, 0, 25, 0xf);
}

static inline u32 msg_isdata(struct tipc_msg *m)
{
	return msg_user(m) <= TIPC_CRITICAL_IMPORTANCE;
}

static inline void msg_set_user(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 0, 25, 0xf, n);
}

static inline u32 msg_importance(struct tipc_msg *m)
{
	return msg_bits(m, 0, 25, 0xf);
}

static inline void msg_set_importance(struct tipc_msg *m, u32 i)
{
	msg_set_user(m, i);
}

static inline u32 msg_hdr_sz(struct tipc_msg *m)
{
	return msg_bits(m, 0, 21, 0xf) << 2;
}

static inline void msg_set_hdr_sz(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 0, 21, 0xf, n>>2);
}

static inline u32 msg_size(struct tipc_msg *m)
{
	return msg_bits(m, 0, 0, 0x1ffff);
}

static inline u32 msg_data_sz(struct tipc_msg *m)
{
	return msg_size(m) - msg_hdr_sz(m);
}

static inline int msg_non_seq(struct tipc_msg *m)
{
	return msg_bits(m, 0, 20, 1);
}

static inline void msg_set_non_seq(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 0, 20, 1, n);
}

static inline int msg_dest_droppable(struct tipc_msg *m)
{
	return msg_bits(m, 0, 19, 1);
}

static inline void msg_set_dest_droppable(struct tipc_msg *m, u32 d)
{
	msg_set_bits(m, 0, 19, 1, d);
}

static inline int msg_src_droppable(struct tipc_msg *m)
{
	return msg_bits(m, 0, 18, 1);
}

static inline void msg_set_src_droppable(struct tipc_msg *m, u32 d)
{
	msg_set_bits(m, 0, 18, 1, d);
}

static inline void msg_set_size(struct tipc_msg *m, u32 sz)
{
	m->hdr[0] = htonl((msg_word(m, 0) & ~0x1ffff) | sz);
}


/*
 * Word 1
 */
static inline u32 msg_type(struct tipc_msg *m)
{
	return msg_bits(m, 1, 29, 0x7);
}

static inline void msg_set_type(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 1, 29, 0x7, n);
}

static inline u32 msg_named(struct tipc_msg *m)
{
	return msg_type(m) == TIPC_NAMED_MSG;
}

static inline u32 msg_mcast(struct tipc_msg *m)
{
	return msg_type(m) == TIPC_MCAST_MSG;
}

static inline u32 msg_connected(struct tipc_msg *m)
{
	return msg_type(m) == TIPC_CONN_MSG;
}

static inline u32 msg_errcode(struct tipc_msg *m)
{
	return msg_bits(m, 1, 25, 0xf);
}

static inline void msg_set_errcode(struct tipc_msg *m, u32 err)
{
	msg_set_bits(m, 1, 25, 0xf, err);
}

static inline u32 msg_reroute_cnt(struct tipc_msg *m)
{
	return msg_bits(m, 1, 21, 0xf);
}

static inline void msg_incr_reroute_cnt(struct tipc_msg *m)
{
	msg_set_bits(m, 1, 21, 0xf, msg_reroute_cnt(m) + 1);
}

static inline void msg_reset_reroute_cnt(struct tipc_msg *m)
{
	msg_set_bits(m, 1, 21, 0xf, 0);
}

static inline u32 msg_lookup_scope(struct tipc_msg *m)
{
	return msg_bits(m, 1, 19, 0x3);
}

static inline void msg_set_lookup_scope(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 1, 19, 0x3, n);
}

static inline u32 msg_bcast_ack(struct tipc_msg *m)
{
	return msg_bits(m, 1, 0, 0xffff);
}

static inline void msg_set_bcast_ack(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 1, 0, 0xffff, n);
}


/*
 * Word 2
 */
static inline u32 msg_ack(struct tipc_msg *m)
{
	return msg_bits(m, 2, 16, 0xffff);
}

static inline void msg_set_ack(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 2, 16, 0xffff, n);
}

static inline u32 msg_seqno(struct tipc_msg *m)
{
	return msg_bits(m, 2, 0, 0xffff);
}

static inline void msg_set_seqno(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 2, 0, 0xffff, n);
}

/*
 * Words 3-10
 */
static inline u32 msg_prevnode(struct tipc_msg *m)
{
	return msg_word(m, 3);
}

static inline void msg_set_prevnode(struct tipc_msg *m, u32 a)
{
	msg_set_word(m, 3, a);
}

static inline u32 msg_origport(struct tipc_msg *m)
{
	return msg_word(m, 4);
}

static inline void msg_set_origport(struct tipc_msg *m, u32 p)
{
	msg_set_word(m, 4, p);
}

static inline u32 msg_destport(struct tipc_msg *m)
{
	return msg_word(m, 5);
}

static inline void msg_set_destport(struct tipc_msg *m, u32 p)
{
	msg_set_word(m, 5, p);
}

static inline u32 msg_mc_netid(struct tipc_msg *m)
{
	return msg_word(m, 5);
}

static inline void msg_set_mc_netid(struct tipc_msg *m, u32 p)
{
	msg_set_word(m, 5, p);
}

static inline int msg_short(struct tipc_msg *m)
{
	return msg_hdr_sz(m) == SHORT_H_SIZE;
}

static inline u32 msg_orignode(struct tipc_msg *m)
{
	if (likely(msg_short(m)))
		return msg_prevnode(m);
	return msg_word(m, 6);
}

static inline void msg_set_orignode(struct tipc_msg *m, u32 a)
{
	msg_set_word(m, 6, a);
}

static inline u32 msg_destnode(struct tipc_msg *m)
{
	return msg_word(m, 7);
}

static inline void msg_set_destnode(struct tipc_msg *m, u32 a)
{
	msg_set_word(m, 7, a);
}

static inline u32 msg_nametype(struct tipc_msg *m)
{
	return msg_word(m, 8);
}

static inline void msg_set_nametype(struct tipc_msg *m, u32 n)
{
	msg_set_word(m, 8, n);
}

static inline u32 msg_nameinst(struct tipc_msg *m)
{
	return msg_word(m, 9);
}

static inline u32 msg_namelower(struct tipc_msg *m)
{
	return msg_nameinst(m);
}

static inline void msg_set_namelower(struct tipc_msg *m, u32 n)
{
	msg_set_word(m, 9, n);
}

static inline void msg_set_nameinst(struct tipc_msg *m, u32 n)
{
	msg_set_namelower(m, n);
}

static inline u32 msg_nameupper(struct tipc_msg *m)
{
	return msg_word(m, 10);
}

static inline void msg_set_nameupper(struct tipc_msg *m, u32 n)
{
	msg_set_word(m, 10, n);
}

static inline unchar *msg_data(struct tipc_msg *m)
{
	return ((unchar *)m) + msg_hdr_sz(m);
}

static inline struct tipc_msg *msg_get_wrapped(struct tipc_msg *m)
{
	return (struct tipc_msg *)msg_data(m);
}

/*
 * Constants and routines used to read and write TIPC internal message headers
 */

/*
 * Internal message users
 */
#define  BCAST_PROTOCOL       5
#define  MSG_BUNDLER          6
#define  LINK_PROTOCOL        7
#define  CONN_MANAGER         8
#define  ROUTE_DISTRIBUTOR    9		/* obsoleted */
#define  CHANGEOVER_PROTOCOL  10
#define  NAME_DISTRIBUTOR     11
#define  MSG_FRAGMENTER       12
#define  LINK_CONFIG          13
#define  SOCK_WAKEUP          14       /* pseudo user */

/*
 *  Connection management protocol message types
 */
#define CONN_PROBE        0
#define CONN_PROBE_REPLY  1
#define CONN_ACK          2

/*
 * Name distributor message types
 */
#define PUBLICATION       0
#define WITHDRAWAL        1

/*
 * Segmentation message types
 */
#define FIRST_FRAGMENT		0
#define FRAGMENT		1
#define LAST_FRAGMENT		2

/* Bundling protocol message types
 */
#define BUNDLE_OPEN             0
#define BUNDLE_CLOSED           1

/*
 * Link management protocol message types
 */
#define STATE_MSG		0
#define RESET_MSG		1
#define ACTIVATE_MSG		2

/*
 * Changeover tunnel message types
 */
#define DUPLICATE_MSG		0
#define ORIGINAL_MSG		1

/*
 * Config protocol message types
 */
#define DSC_REQ_MSG		0
#define DSC_RESP_MSG		1


/*
 * Word 1
 */
static inline u32 msg_seq_gap(struct tipc_msg *m)
{
	return msg_bits(m, 1, 16, 0x1fff);
}

static inline void msg_set_seq_gap(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 1, 16, 0x1fff, n);
}

static inline u32 msg_node_sig(struct tipc_msg *m)
{
	return msg_bits(m, 1, 0, 0xffff);
}

static inline void msg_set_node_sig(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 1, 0, 0xffff, n);
}


/*
 * Word 2
 */
static inline u32 msg_dest_domain(struct tipc_msg *m)
{
	return msg_word(m, 2);
}

static inline void msg_set_dest_domain(struct tipc_msg *m, u32 n)
{
	msg_set_word(m, 2, n);
}

static inline u32 msg_bcgap_after(struct tipc_msg *m)
{
	return msg_bits(m, 2, 16, 0xffff);
}

static inline void msg_set_bcgap_after(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 2, 16, 0xffff, n);
}

static inline u32 msg_bcgap_to(struct tipc_msg *m)
{
	return msg_bits(m, 2, 0, 0xffff);
}

static inline void msg_set_bcgap_to(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 2, 0, 0xffff, n);
}


/*
 * Word 4
 */
static inline u32 msg_last_bcast(struct tipc_msg *m)
{
	return msg_bits(m, 4, 16, 0xffff);
}

static inline void msg_set_last_bcast(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 4, 16, 0xffff, n);
}

static inline void msg_set_fragm_no(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 4, 16, 0xffff, n);
}


static inline u32 msg_next_sent(struct tipc_msg *m)
{
	return msg_bits(m, 4, 0, 0xffff);
}

static inline void msg_set_next_sent(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 4, 0, 0xffff, n);
}

static inline void msg_set_long_msgno(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 4, 0, 0xffff, n);
}

static inline u32 msg_bc_netid(struct tipc_msg *m)
{
	return msg_word(m, 4);
}

static inline void msg_set_bc_netid(struct tipc_msg *m, u32 id)
{
	msg_set_word(m, 4, id);
}

static inline u32 msg_link_selector(struct tipc_msg *m)
{
	return msg_bits(m, 4, 0, 1);
}

static inline void msg_set_link_selector(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 4, 0, 1, n);
}

/*
 * Word 5
 */
static inline u32 msg_session(struct tipc_msg *m)
{
	return msg_bits(m, 5, 16, 0xffff);
}

static inline void msg_set_session(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 5, 16, 0xffff, n);
}

static inline u32 msg_probe(struct tipc_msg *m)
{
	return msg_bits(m, 5, 0, 1);
}

static inline void msg_set_probe(struct tipc_msg *m, u32 val)
{
	msg_set_bits(m, 5, 0, 1, val);
}

static inline char msg_net_plane(struct tipc_msg *m)
{
	return msg_bits(m, 5, 1, 7) + 'A';
}

static inline void msg_set_net_plane(struct tipc_msg *m, char n)
{
	msg_set_bits(m, 5, 1, 7, (n - 'A'));
}

static inline u32 msg_linkprio(struct tipc_msg *m)
{
	return msg_bits(m, 5, 4, 0x1f);
}

static inline void msg_set_linkprio(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 5, 4, 0x1f, n);
}

static inline u32 msg_bearer_id(struct tipc_msg *m)
{
	return msg_bits(m, 5, 9, 0x7);
}

static inline void msg_set_bearer_id(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 5, 9, 0x7, n);
}

static inline u32 msg_redundant_link(struct tipc_msg *m)
{
	return msg_bits(m, 5, 12, 0x1);
}

static inline void msg_set_redundant_link(struct tipc_msg *m, u32 r)
{
	msg_set_bits(m, 5, 12, 0x1, r);
}

static inline char *msg_media_addr(struct tipc_msg *m)
{
	return (char *)&m->hdr[TIPC_MEDIA_ADDR_OFFSET];
}

/*
 * Word 9
 */
static inline u32 msg_msgcnt(struct tipc_msg *m)
{
	return msg_bits(m, 9, 16, 0xffff);
}

static inline void msg_set_msgcnt(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 9, 16, 0xffff, n);
}

static inline u32 msg_bcast_tag(struct tipc_msg *m)
{
	return msg_bits(m, 9, 16, 0xffff);
}

static inline void msg_set_bcast_tag(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 9, 16, 0xffff, n);
}

static inline u32 msg_max_pkt(struct tipc_msg *m)
{
	return msg_bits(m, 9, 16, 0xffff) * 4;
}

static inline void msg_set_max_pkt(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 9, 16, 0xffff, (n / 4));
}

static inline u32 msg_link_tolerance(struct tipc_msg *m)
{
	return msg_bits(m, 9, 0, 0xffff);
}

static inline void msg_set_link_tolerance(struct tipc_msg *m, u32 n)
{
	msg_set_bits(m, 9, 0, 0xffff, n);
}

static inline u32 tipc_msg_tot_importance(struct tipc_msg *m)
{
	if ((msg_user(m) == MSG_FRAGMENTER) && (msg_type(m) == FIRST_FRAGMENT))
		return msg_importance(msg_get_wrapped(m));
	return msg_importance(m);
}

static inline u32 msg_tot_origport(struct tipc_msg *m)
{
	if ((msg_user(m) == MSG_FRAGMENTER) && (msg_type(m) == FIRST_FRAGMENT))
		return msg_origport(msg_get_wrapped(m));
	return msg_origport(m);
}

bool tipc_msg_reverse(struct sk_buff *buf, u32 *dnode, int err);

int tipc_msg_eval(struct sk_buff *buf, u32 *dnode);

void tipc_msg_init(struct tipc_msg *m, u32 user, u32 type, u32 hsize,
		   u32 destnode);

struct sk_buff *tipc_msg_create(uint user, uint type, uint hdr_sz,
				uint data_sz, u32 dnode, u32 onode,
				u32 dport, u32 oport, int errcode);

int tipc_buf_append(struct sk_buff **headbuf, struct sk_buff **buf);

bool tipc_msg_bundle(struct sk_buff *bbuf, struct sk_buff *buf, u32 mtu);

bool tipc_msg_make_bundle(struct sk_buff **buf, u32 mtu, u32 dnode);

int tipc_msg_build(struct tipc_msg *mhdr, struct iovec const *iov,
		   int offset, int dsz, int mtu , struct sk_buff **chain);

struct sk_buff *tipc_msg_reassemble(struct sk_buff *chain);

#endif
