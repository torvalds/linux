/* net/tipc/socket.h: Include file for TIPC socket code
 *
 * Copyright (c) 2014-2016, Ericsson AB
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

#ifndef _TIPC_SOCK_H
#define _TIPC_SOCK_H

#include <net/sock.h>
#include <net/genetlink.h>

/* Compatibility values for deprecated message based flow control */
#define FLOWCTL_MSG_WIN 512
#define FLOWCTL_MSG_LIM ((FLOWCTL_MSG_WIN * 2 + 1) * SKB_TRUESIZE(MAX_MSG_SIZE))

#define FLOWCTL_BLK_SZ 1024

/* Socket receive buffer sizes */
#define RCVBUF_MIN  (FLOWCTL_BLK_SZ * 512)
#define RCVBUF_DEF  (FLOWCTL_BLK_SZ * 1024 * 2)
#define RCVBUF_MAX  (FLOWCTL_BLK_SZ * 1024 * 16)

struct tipc_sock;

int tipc_socket_init(void);
void tipc_socket_stop(void);
void tipc_sk_rcv(struct net *net, struct sk_buff_head *inputq);
void tipc_sk_mcast_rcv(struct net *net, struct sk_buff_head *arrvq,
		       struct sk_buff_head *inputq);
void tipc_sk_reinit(struct net *net);
int tipc_sk_rht_init(struct net *net);
void tipc_sk_rht_destroy(struct net *net);
int tipc_nl_sk_dump(struct sk_buff *skb, struct netlink_callback *cb);
int tipc_nl_publ_dump(struct sk_buff *skb, struct netlink_callback *cb);
int tipc_sk_fill_sock_diag(struct sk_buff *skb, struct netlink_callback *cb,
			   struct tipc_sock *tsk, u32 sk_filter_state,
			   u64 (*tipc_diag_gen_cookie)(struct sock *sk));
int tipc_nl_sk_walk(struct sk_buff *skb, struct netlink_callback *cb,
		    int (*skb_handler)(struct sk_buff *skb,
				       struct netlink_callback *cb,
				       struct tipc_sock *tsk));
int tipc_dump_start(struct netlink_callback *cb);
int __tipc_dump_start(struct netlink_callback *cb, struct net *net);
int tipc_dump_done(struct netlink_callback *cb);
u32 tipc_sock_get_portid(struct sock *sk);
bool tipc_sk_overlimit1(struct sock *sk, struct sk_buff *skb);
bool tipc_sk_overlimit2(struct sock *sk, struct sk_buff *skb);

int tsk_set_importance(struct sock *sk, int imp);

#endif
