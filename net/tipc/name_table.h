/*
 * net/tipc/name_table.h: Include file for TIPC name table code
 * 
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
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

#ifndef _TIPC_NAME_TABLE_H
#define _TIPC_NAME_TABLE_H

#include "node_subscr.h"

struct subscription;
struct port_list;

/*
 * TIPC name types reserved for internal TIPC use (both current and planned)
 */

#define TIPC_ZM_SRV 3  		/* zone master service name type */


/**
 * struct publication - info about a published (name or) name sequence
 * @type: name sequence type
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @scope: scope of publication
 * @node: network address of publishing port's node
 * @ref: publishing port
 * @key: publication key
 * @subscr: subscription to "node down" event (for off-node publications only)
 * @local_list: adjacent entries in list of publications made by this node
 * @pport_list: adjacent entries in list of publications made by this port
 * @node_list: next matching name seq publication with >= node scope
 * @cluster_list: next matching name seq publication with >= cluster scope
 * @zone_list: next matching name seq publication with >= zone scope
 * 
 * Note that the node list, cluster list, and zone list are circular lists.
 */

struct publication {
	u32 type;
	u32 lower;
	u32 upper;
	u32 scope;
	u32 node;
	u32 ref;
	u32 key;
	struct node_subscr subscr;
	struct list_head local_list;
	struct list_head pport_list;
	struct publication *node_list_next;
	struct publication *cluster_list_next;
	struct publication *zone_list_next;
};


extern rwlock_t nametbl_lock;

struct sk_buff *nametbl_get(const void *req_tlv_area, int req_tlv_space);
u32 nametbl_translate(u32 type, u32 instance, u32 *node);
int nametbl_mc_translate(u32 type, u32 lower, u32 upper, u32 limit, 
			 struct port_list *dports);
int nametbl_publish_rsv(u32 ref, unsigned int scope, 
			struct tipc_name_seq const *seq);
struct publication *nametbl_publish(u32 type, u32 lower, u32 upper,
				    u32 scope, u32 port_ref, u32 key);
int nametbl_withdraw(u32 type, u32 lower, u32 ref, u32 key);
struct publication *nametbl_insert_publ(u32 type, u32 lower, u32 upper,
					u32 scope, u32 node, u32 ref, u32 key);
struct publication *nametbl_remove_publ(u32 type, u32 lower, 
					u32 node, u32 ref, u32 key);
void nametbl_subscribe(struct subscription *s);
void nametbl_unsubscribe(struct subscription *s);
int nametbl_init(void);
void nametbl_stop(void);

#endif
