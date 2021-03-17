/*
 * net/tipc/name_distr.h: Include file for TIPC name distribution code
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

#ifndef _TIPC_NAME_DISTR_H
#define _TIPC_NAME_DISTR_H

#include "name_table.h"

#define ITEM_SIZE sizeof(struct distr_item)

/**
 * struct distr_item - publication info distributed to other nodes
 * @type: name sequence type
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @ref: publishing port reference
 * @key: publication key
 *
 * ===> All fields are stored in network byte order. <===
 *
 * First 3 fields identify (name or) name sequence being published.
 * Reference field uniquely identifies port that published name sequence.
 * Key field uniquely identifies publication, in the event a port has
 * multiple publications of the same name sequence.
 *
 * Note: There is no field that identifies the publishing node because it is
 * the same for all items contained within a publication message.
 */
struct distr_item {
	__be32 type;
	__be32 lower;
	__be32 upper;
	__be32 port;
	__be32 key;
};

void tipc_named_bcast(struct net *net, struct sk_buff *skb);
struct sk_buff *tipc_named_publish(struct net *net, struct publication *publ);
struct sk_buff *tipc_named_withdraw(struct net *net, struct publication *publ);
void tipc_named_node_up(struct net *net, u32 dnode, u16 capabilities);
void tipc_named_rcv(struct net *net, struct sk_buff_head *namedq,
		    u16 *rcv_nxt, bool *open);
void tipc_named_reinit(struct net *net);
void tipc_publ_notify(struct net *net, struct list_head *nsub_list,
		      u32 addr, u16 capabilities);

#endif
