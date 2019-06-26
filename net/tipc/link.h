/*
 * net/tipc/link.h: Include file for TIPC link code
 *
 * Copyright (c) 1995-2006, 2013-2014, Ericsson AB
 * Copyright (c) 2004-2005, 2010-2011, Wind River Systems
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

#ifndef _TIPC_LINK_H
#define _TIPC_LINK_H

#include <net/genetlink.h>
#include "msg.h"
#include "node.h"

/* TIPC-specific error codes
*/
#define ELINKCONG EAGAIN	/* link congestion <=> resource unavailable */

/* Link FSM events:
 */
enum {
	LINK_ESTABLISH_EVT       = 0xec1ab1e,
	LINK_PEER_RESET_EVT      = 0x9eed0e,
	LINK_FAILURE_EVT         = 0xfa110e,
	LINK_RESET_EVT           = 0x10ca1d0e,
	LINK_FAILOVER_BEGIN_EVT  = 0xfa110bee,
	LINK_FAILOVER_END_EVT    = 0xfa110ede,
	LINK_SYNCH_BEGIN_EVT     = 0xc1ccbee,
	LINK_SYNCH_END_EVT       = 0xc1ccede
};

/* Events returned from link at packet reception or at timeout
 */
enum {
	TIPC_LINK_UP_EVT       = 1,
	TIPC_LINK_DOWN_EVT     = (1 << 1),
	TIPC_LINK_SND_STATE    = (1 << 2)
};

/* Starting value for maximum packet size negotiation on unicast links
 * (unless bearer MTU is less)
 */
#define MAX_PKT_DEFAULT 1500

bool tipc_link_create(struct net *net, char *if_name, int bearer_id,
		      int tolerance, char net_plane, u32 mtu, int priority,
		      int window, u32 session, u32 ownnode,
		      u32 peer, u8 *peer_id, u16 peer_caps,
		      struct tipc_link *bc_sndlink,
		      struct tipc_link *bc_rcvlink,
		      struct sk_buff_head *inputq,
		      struct sk_buff_head *namedq,
		      struct tipc_link **link);
bool tipc_link_bc_create(struct net *net, u32 ownnode, u32 peer,
			 int mtu, int window, u16 peer_caps,
			 struct sk_buff_head *inputq,
			 struct sk_buff_head *namedq,
			 struct tipc_link *bc_sndlink,
			 struct tipc_link **link);
void tipc_link_tnl_prepare(struct tipc_link *l, struct tipc_link *tnl,
			   int mtyp, struct sk_buff_head *xmitq);
void tipc_link_create_dummy_tnl_msg(struct tipc_link *tnl,
				    struct sk_buff_head *xmitq);
void tipc_link_failover_prepare(struct tipc_link *l, struct tipc_link *tnl,
				struct sk_buff_head *xmitq);
void tipc_link_build_reset_msg(struct tipc_link *l, struct sk_buff_head *xmitq);
int tipc_link_fsm_evt(struct tipc_link *l, int evt);
bool tipc_link_is_up(struct tipc_link *l);
bool tipc_link_peer_is_down(struct tipc_link *l);
bool tipc_link_is_reset(struct tipc_link *l);
bool tipc_link_is_establishing(struct tipc_link *l);
bool tipc_link_is_synching(struct tipc_link *l);
bool tipc_link_is_failingover(struct tipc_link *l);
bool tipc_link_is_blocked(struct tipc_link *l);
void tipc_link_set_active(struct tipc_link *l, bool active);
void tipc_link_reset(struct tipc_link *l);
void tipc_link_reset_stats(struct tipc_link *l);
int tipc_link_xmit(struct tipc_link *link, struct sk_buff_head *list,
		   struct sk_buff_head *xmitq);
struct sk_buff_head *tipc_link_inputq(struct tipc_link *l);
u16 tipc_link_rcv_nxt(struct tipc_link *l);
u16 tipc_link_acked(struct tipc_link *l);
u32 tipc_link_id(struct tipc_link *l);
char *tipc_link_name(struct tipc_link *l);
char *tipc_link_name_ext(struct tipc_link *l, char *buf);
u32 tipc_link_state(struct tipc_link *l);
char tipc_link_plane(struct tipc_link *l);
int tipc_link_prio(struct tipc_link *l);
int tipc_link_window(struct tipc_link *l);
void tipc_link_update_caps(struct tipc_link *l, u16 capabilities);
bool tipc_link_validate_msg(struct tipc_link *l, struct tipc_msg *hdr);
unsigned long tipc_link_tolerance(struct tipc_link *l);
void tipc_link_set_tolerance(struct tipc_link *l, u32 tol,
			     struct sk_buff_head *xmitq);
void tipc_link_set_prio(struct tipc_link *l, u32 prio,
			struct sk_buff_head *xmitq);
void tipc_link_set_abort_limit(struct tipc_link *l, u32 limit);
void tipc_link_set_queue_limits(struct tipc_link *l, u32 window);
int __tipc_nl_add_link(struct net *net, struct tipc_nl_msg *msg,
		       struct tipc_link *link, int nlflags);
int tipc_nl_parse_link_prop(struct nlattr *prop, struct nlattr *props[]);
int tipc_link_timeout(struct tipc_link *l, struct sk_buff_head *xmitq);
int tipc_link_rcv(struct tipc_link *l, struct sk_buff *skb,
		  struct sk_buff_head *xmitq);
int tipc_link_build_state_msg(struct tipc_link *l, struct sk_buff_head *xmitq);
void tipc_link_add_bc_peer(struct tipc_link *snd_l,
			   struct tipc_link *uc_l,
			   struct sk_buff_head *xmitq);
void tipc_link_remove_bc_peer(struct tipc_link *snd_l,
			      struct tipc_link *rcv_l,
			      struct sk_buff_head *xmitq);
int tipc_link_bc_peers(struct tipc_link *l);
void tipc_link_set_mtu(struct tipc_link *l, int mtu);
int tipc_link_mtu(struct tipc_link *l);
void tipc_link_bc_ack_rcv(struct tipc_link *l, u16 acked,
			  struct sk_buff_head *xmitq);
void tipc_link_build_bc_sync_msg(struct tipc_link *l,
				 struct sk_buff_head *xmitq);
void tipc_link_bc_init_rcv(struct tipc_link *l, struct tipc_msg *hdr);
int tipc_link_bc_sync_rcv(struct tipc_link *l,   struct tipc_msg *hdr,
			  struct sk_buff_head *xmitq);
int tipc_link_bc_nack_rcv(struct tipc_link *l, struct sk_buff *skb,
			  struct sk_buff_head *xmitq);
bool tipc_link_too_silent(struct tipc_link *l);
#endif
