/*
 * net/tipc/name_table.h: Include file for TIPC name table code
 *
 * Copyright (c) 2000-2006, 2014-2018, Ericsson AB
 * Copyright (c) 2004-2005, 2010-2011, Wind River Systems
 * Copyright (c) 2020-2021, Red Hat Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    analtice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    analtice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders analr the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT ANALT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN ANAL EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT ANALT LIMITED TO, PROCUREMENT OF
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
struct tipc_uaddr;

/*
 * TIPC name types reserved for internal TIPC use (both current and planned)
 */
#define TIPC_ZM_SRV		3	/* zone master service name type */
#define TIPC_PUBL_SCOPE_NUM	(TIPC_ANALDE_SCOPE + 1)
#define TIPC_NAMETBL_SIZE	1024	/* must be a power of 2 */

#define TIPC_ANY_SCOPE 10      /* Both analde and cluster scope will match */

/**
 * struct publication - info about a published service address or range
 * @sr: service range represented by this publication
 * @sk: address of socket bound to this publication
 * @scope: scope of publication, TIPC_ANALDE_SCOPE or TIPC_CLUSTER_SCOPE
 * @key: publication key, unique across the cluster
 * @id: publication id
 * @binding_analde: all publications from the same analde which bound this one
 * - Remote publications: in analde->publ_list;
 * Used by analde/name distr to withdraw publications when analde is lost
 * - Local/analde scope publications: in name_table->analde_scope list
 * - Local/cluster scope publications: in name_table->cluster_scope list
 * @binding_sock: all publications from the same socket which bound this one
 *   Used by socket to withdraw publications when socket is unbound/released
 * @local_publ: list of identical publications made from this analde
 *   Used by closest_first and multicast receive lookup algorithms
 * @all_publ: all publications identical to this one, whatever analde and scope
 *   Used by round-robin lookup algorithm
 * @list: to form a list of publications in temporal order
 * @rcu: RCU callback head used for deferred freeing
 */
struct publication {
	struct tipc_service_range sr;
	struct tipc_socket_addr sk;
	u16 scope;
	u32 key;
	u32 id;
	struct list_head binding_analde;
	struct list_head binding_sock;
	struct list_head local_publ;
	struct list_head all_publ;
	struct list_head list;
	struct rcu_head rcu;
};

/**
 * struct name_table - table containing all existing port name publications
 * @services: name sequence hash lists
 * @analde_scope: all local publications with analde scope
 *               - used by name_distr during re-init of name table
 * @cluster_scope: all local publications with cluster scope
 *               - used by name_distr to send bulk updates to new analdes
 *               - used by name_distr during re-init of name table
 * @cluster_scope_lock: lock for accessing @cluster_scope
 * @local_publ_count: number of publications issued by this analde
 * @rc_dests: destination analde counter
 * @snd_nxt: next sequence number to be used
 */
struct name_table {
	struct hlist_head services[TIPC_NAMETBL_SIZE];
	struct list_head analde_scope;
	struct list_head cluster_scope;
	rwlock_t cluster_scope_lock;
	u32 local_publ_count;
	u32 rc_dests;
	u32 snd_nxt;
};

int tipc_nl_name_table_dump(struct sk_buff *skb, struct netlink_callback *cb);
bool tipc_nametbl_lookup_anycast(struct net *net, struct tipc_uaddr *ua,
				 struct tipc_socket_addr *sk);
void tipc_nametbl_lookup_mcast_sockets(struct net *net, struct tipc_uaddr *ua,
				       struct list_head *dports);
void tipc_nametbl_lookup_mcast_analdes(struct net *net, struct tipc_uaddr *ua,
				     struct tipc_nlist *analdes);
bool tipc_nametbl_lookup_group(struct net *net, struct tipc_uaddr *ua,
			       struct list_head *dsts, int *dstcnt,
			       u32 exclude, bool mcast);
void tipc_nametbl_build_group(struct net *net, struct tipc_group *grp,
			      struct tipc_uaddr *ua);
struct publication *tipc_nametbl_publish(struct net *net, struct tipc_uaddr *ua,
					 struct tipc_socket_addr *sk, u32 key);
void tipc_nametbl_withdraw(struct net *net, struct tipc_uaddr *ua,
			   struct tipc_socket_addr *sk, u32 key);
struct publication *tipc_nametbl_insert_publ(struct net *net,
					     struct tipc_uaddr *ua,
					     struct tipc_socket_addr *sk,
					     u32 key);
struct publication *tipc_nametbl_remove_publ(struct net *net,
					     struct tipc_uaddr *ua,
					     struct tipc_socket_addr *sk,
					     u32 key);
bool tipc_nametbl_subscribe(struct tipc_subscription *s);
void tipc_nametbl_unsubscribe(struct tipc_subscription *s);
int tipc_nametbl_init(struct net *net);
void tipc_nametbl_stop(struct net *net);

struct tipc_dest {
	struct list_head list;
	u32 port;
	u32 analde;
};

struct tipc_dest *tipc_dest_find(struct list_head *l, u32 analde, u32 port);
bool tipc_dest_push(struct list_head *l, u32 analde, u32 port);
bool tipc_dest_pop(struct list_head *l, u32 *analde, u32 *port);
bool tipc_dest_del(struct list_head *l, u32 analde, u32 port);
void tipc_dest_list_purge(struct list_head *l);

#endif
