/*
 * net/tipc/group.h: Include file for TIPC group unicast/multicast functions
 *
 * Copyright (c) 2017, Ericsson AB
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

#ifndef _TIPC_GROUP_H
#define _TIPC_GROUP_H

#include "core.h"

struct tipc_group;
struct tipc_member;
struct tipc_msg;

struct tipc_group *tipc_group_create(struct net *net, u32 portid,
				     struct tipc_group_req *mreq,
				     bool *group_is_open);
void tipc_group_join(struct net *net, struct tipc_group *grp, int *sk_rcv_buf);
void tipc_group_delete(struct net *net, struct tipc_group *grp);
void tipc_group_add_member(struct tipc_group *grp, u32 node,
			   u32 port, u32 instance);
struct tipc_nlist *tipc_group_dests(struct tipc_group *grp);
void tipc_group_self(struct tipc_group *grp, struct tipc_name_seq *seq,
		     int *scope);
u32 tipc_group_exclude(struct tipc_group *grp);
void tipc_group_filter_msg(struct tipc_group *grp,
			   struct sk_buff_head *inputq,
			   struct sk_buff_head *xmitq);
void tipc_group_member_evt(struct tipc_group *grp, bool *wakeup,
			   int *sk_rcvbuf, struct tipc_msg *hdr,
			   struct sk_buff_head *inputq,
			   struct sk_buff_head *xmitq);
void tipc_group_proto_rcv(struct tipc_group *grp, bool *wakeup,
			  struct tipc_msg *hdr,
			  struct sk_buff_head *inputq,
			  struct sk_buff_head *xmitq);
void tipc_group_update_bc_members(struct tipc_group *grp, int len, bool ack);
bool tipc_group_cong(struct tipc_group *grp, u32 dnode, u32 dport,
		     int len, struct tipc_member **m);
bool tipc_group_bc_cong(struct tipc_group *grp, int len);
void tipc_group_update_rcv_win(struct tipc_group *grp, int blks, u32 node,
			       u32 port, struct sk_buff_head *xmitq);
u16 tipc_group_bc_snd_nxt(struct tipc_group *grp);
void tipc_group_update_member(struct tipc_member *m, int len);
#endif
