/*
 * net/tipc/name_table.h: Include file for TIPC name table code
 *
 * Copyright (c) 2000-2006, 2014-2018, Ericsson AB
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

#ifndef _TIPC_NAME_TABLE_H
#define _TIPC_NAME_TABLE_H

struct tipc_subscription;
struct tipc_plist;
struct tipc_nlist;
struct tipc_group;

/*
 * TIPC name types reserved for internal TIPC use (both current and planned)
 */
#define TIPC_ZM_SRV		3	/* zone master service name type */
#define TIPC_PUBL_SCOPE_NUM	(TIPC_NODE_SCOPE + 1)
#define TIPC_NAMETBL_SIZE	1024	/* must be a power of 2 */

/**
 * struct publication - info about a published (name or) name sequence
 * @type: name sequence type
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @scope: scope of publication, TIPC_NODE_SCOPE or TIPC_CLUSTER_SCOPE
 * @node: network address of publishing socket's node
 * @port: publishing port
 * @key: publication key, unique across the cluster
 * @id: publication id
 * @binding_node: all publications from the same node which bound this one
 * - Remote publications: in node->publ_list
 *   Used by node/name distr to withdraw publications when node is lost
 * - Local/node scope publications: in name_table->node_scope list
 * - Local/cluster scope publications: in name_table->cluster_scope list
 * @binding_sock: all publications from the same socket which bound this one
 *   Used by socket to withdraw publications when socket is unbound/released
 * @local_publ: list of identical publications made from this node
 *   Used by closest_first and multicast receive lookup algorithms
 * @all_publ: all publications identical to this one, whatever node and scope
 *   Used by round-robin lookup algorithm
 * @list: to form a list of publications in temporal order
 * @rcu: RCU callback head used for deferred freeing
 */
struct publication {
	u32 type;
	u32 lower;
	u32 upper;
	u32 scope;
	u32 node;
	u32 port;
	u32 key;
	u32 id;
	struct list_head binding_node;
	struct list_head binding_sock;
	struct list_head local_publ;
	struct list_head all_publ;
	struct list_head list;
	struct rcu_head rcu;
};

/**
 * struct name_table - table containing all existing port name publications
 * @seq_hlist: name sequence hash lists
 * @node_scope: all local publications with node scope
 *               - used by name_distr during re-init of name table
 * @cluster_scope: all local publications with cluster scope
 *               - used by name_distr to send bulk updates to new nodes
 *               - used by name_distr during re-init of name table
 * @local_publ_count: number of publications issued by this node
 */
struct name_table {
	struct hlist_head services[TIPC_NAMETBL_SIZE];
	struct list_head node_scope;
	struct list_head cluster_scope;
	rwlock_t cluster_scope_lock;
	u32 local_publ_count;
};

int tipc_nl_name_table_dump(struct sk_buff *skb, struct netlink_callback *cb);

u32 tipc_nametbl_translate(struct net *net, u32 type, u32 instance, u32 *node);
void tipc_nametbl_mc_lookup(struct net *net, u32 type, u32 lower, u32 upper,
			    u32 scope, bool exact, struct list_head *dports);
void tipc_nametbl_build_group(struct net *net, struct tipc_group *grp,
			      u32 type, u32 domain);
void tipc_nametbl_lookup_dst_nodes(struct net *net, u32 type, u32 lower,
				   u32 upper, struct tipc_nlist *nodes);
bool tipc_nametbl_lookup(struct net *net, u32 type, u32 instance, u32 domain,
			 struct list_head *dsts, int *dstcnt, u32 exclude,
			 bool all);
struct publication *tipc_nametbl_publish(struct net *net, u32 type, u32 lower,
					 u32 upper, u32 scope, u32 port,
					 u32 key);
int tipc_nametbl_withdraw(struct net *net, u32 type, u32 lower, u32 upper,
			  u32 key);
struct publication *tipc_nametbl_insert_publ(struct net *net, u32 type,
					     u32 lower, u32 upper, u32 scope,
					     u32 node, u32 ref, u32 key);
struct publication *tipc_nametbl_remove_publ(struct net *net, u32 type,
					     u32 lower, u32 upper,
					     u32 node, u32 key);
bool tipc_nametbl_subscribe(struct tipc_subscription *s);
void tipc_nametbl_unsubscribe(struct tipc_subscription *s);
int tipc_nametbl_init(struct net *net);
void tipc_nametbl_stop(struct net *net);

struct tipc_dest {
	struct list_head list;
	u32 port;
	u32 node;
};

struct tipc_dest *tipc_dest_find(struct list_head *l, u32 node, u32 port);
bool tipc_dest_push(struct list_head *l, u32 node, u32 port);
bool tipc_dest_pop(struct list_head *l, u32 *node, u32 *port);
bool tipc_dest_del(struct list_head *l, u32 node, u32 port);
void tipc_dest_list_purge(struct list_head *l);
int tipc_dest_list_len(struct list_head *l);

#endif
