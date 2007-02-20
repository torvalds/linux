/*
 * net/tipc/net.c: TIPC network routing code
 *
 * Copyright (c) 1995-2006, Ericsson AB
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

#include "core.h"
#include "bearer.h"
#include "net.h"
#include "zone.h"
#include "addr.h"
#include "name_table.h"
#include "name_distr.h"
#include "subscr.h"
#include "link.h"
#include "msg.h"
#include "port.h"
#include "bcast.h"
#include "discover.h"
#include "config.h"

/*
 * The TIPC locking policy is designed to ensure a very fine locking
 * granularity, permitting complete parallel access to individual
 * port and node/link instances. The code consists of three major
 * locking domains, each protected with their own disjunct set of locks.
 *
 * 1: The routing hierarchy.
 *    Comprises the structures 'zone', 'cluster', 'node', 'link'
 *    and 'bearer'. The whole hierarchy is protected by a big
 *    read/write lock, tipc_net_lock, to enssure that nothing is added
 *    or removed while code is accessing any of these structures.
 *    This layer must not be called from the two others while they
 *    hold any of their own locks.
 *    Neither must it itself do any upcalls to the other two before
 *    it has released tipc_net_lock and other protective locks.
 *
 *   Within the tipc_net_lock domain there are two sub-domains;'node' and
 *   'bearer', where local write operations are permitted,
 *   provided that those are protected by individual spin_locks
 *   per instance. Code holding tipc_net_lock(read) and a node spin_lock
 *   is permitted to poke around in both the node itself and its
 *   subordinate links. I.e, it can update link counters and queues,
 *   change link state, send protocol messages, and alter the
 *   "active_links" array in the node; but it can _not_ remove a link
 *   or a node from the overall structure.
 *   Correspondingly, individual bearers may change status within a
 *   tipc_net_lock(read), protected by an individual spin_lock ber bearer
 *   instance, but it needs tipc_net_lock(write) to remove/add any bearers.
 *
 *
 *  2: The transport level of the protocol.
 *     This consists of the structures port, (and its user level
 *     representations, such as user_port and tipc_sock), reference and
 *     tipc_user (port.c, reg.c, socket.c).
 *
 *     This layer has four different locks:
 *     - The tipc_port spin_lock. This is protecting each port instance
 *       from parallel data access and removal. Since we can not place
 *       this lock in the port itself, it has been placed in the
 *       corresponding reference table entry, which has the same life
 *       cycle as the module. This entry is difficult to access from
 *       outside the TIPC core, however, so a pointer to the lock has
 *       been added in the port instance, -to be used for unlocking
 *       only.
 *     - A read/write lock to protect the reference table itself (teg.c).
 *       (Nobody is using read-only access to this, so it can just as
 *       well be changed to a spin_lock)
 *     - A spin lock to protect the registry of kernel/driver users (reg.c)
 *     - A global spin_lock (tipc_port_lock), which only task is to ensure
 *       consistency where more than one port is involved in an operation,
 *       i.e., whe a port is part of a linked list of ports.
 *       There are two such lists; 'port_list', which is used for management,
 *       and 'wait_list', which is used to queue ports during congestion.
 *
 *  3: The name table (name_table.c, name_distr.c, subscription.c)
 *     - There is one big read/write-lock (tipc_nametbl_lock) protecting the
 *       overall name table structure. Nothing must be added/removed to
 *       this structure without holding write access to it.
 *     - There is one local spin_lock per sub_sequence, which can be seen
 *       as a sub-domain to the tipc_nametbl_lock domain. It is used only
 *       for translation operations, and is needed because a translation
 *       steps the root of the 'publication' linked list between each lookup.
 *       This is always used within the scope of a tipc_nametbl_lock(read).
 *     - A local spin_lock protecting the queue of subscriber events.
*/

DEFINE_RWLOCK(tipc_net_lock);
struct network tipc_net = { NULL };

struct node *tipc_net_select_remote_node(u32 addr, u32 ref)
{
	return tipc_zone_select_remote_node(tipc_net.zones[tipc_zone(addr)], addr, ref);
}

u32 tipc_net_select_router(u32 addr, u32 ref)
{
	return tipc_zone_select_router(tipc_net.zones[tipc_zone(addr)], addr, ref);
}

#if 0
u32 tipc_net_next_node(u32 a)
{
	if (tipc_net.zones[tipc_zone(a)])
		return tipc_zone_next_node(a);
	return 0;
}
#endif

void tipc_net_remove_as_router(u32 router)
{
	u32 z_num;

	for (z_num = 1; z_num <= tipc_max_zones; z_num++) {
		if (!tipc_net.zones[z_num])
			continue;
		tipc_zone_remove_as_router(tipc_net.zones[z_num], router);
	}
}

void tipc_net_send_external_routes(u32 dest)
{
	u32 z_num;

	for (z_num = 1; z_num <= tipc_max_zones; z_num++) {
		if (tipc_net.zones[z_num])
			tipc_zone_send_external_routes(tipc_net.zones[z_num], dest);
	}
}

static int net_init(void)
{
	memset(&tipc_net, 0, sizeof(tipc_net));
	tipc_net.zones = kcalloc(tipc_max_zones + 1, sizeof(struct _zone *), GFP_ATOMIC);
	if (!tipc_net.zones) {
		return -ENOMEM;
	}
	return TIPC_OK;
}

static void net_stop(void)
{
	u32 z_num;

	if (!tipc_net.zones)
		return;

	for (z_num = 1; z_num <= tipc_max_zones; z_num++) {
		tipc_zone_delete(tipc_net.zones[z_num]);
	}
	kfree(tipc_net.zones);
	tipc_net.zones = NULL;
}

static void net_route_named_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	u32 dnode;
	u32 dport;

	if (!msg_named(msg)) {
		msg_dbg(msg, "tipc_net->drop_nam:");
		buf_discard(buf);
		return;
	}

	dnode = addr_domain(msg_lookup_scope(msg));
	dport = tipc_nametbl_translate(msg_nametype(msg), msg_nameinst(msg), &dnode);
	dbg("tipc_net->lookup<%u,%u>-><%u,%x>\n",
	    msg_nametype(msg), msg_nameinst(msg), dport, dnode);
	if (dport) {
		msg_set_destnode(msg, dnode);
		msg_set_destport(msg, dport);
		tipc_net_route_msg(buf);
		return;
	}
	msg_dbg(msg, "tipc_net->rej:NO NAME: ");
	tipc_reject_msg(buf, TIPC_ERR_NO_NAME);
}

void tipc_net_route_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg;
	u32 dnode;

	if (!buf)
		return;
	msg = buf_msg(buf);

	msg_incr_reroute_cnt(msg);
	if (msg_reroute_cnt(msg) > 6) {
		if (msg_errcode(msg)) {
			msg_dbg(msg, "NET>DISC>:");
			buf_discard(buf);
		} else {
			msg_dbg(msg, "NET>REJ>:");
			tipc_reject_msg(buf, msg_destport(msg) ?
					TIPC_ERR_NO_PORT : TIPC_ERR_NO_NAME);
		}
		return;
	}

	msg_dbg(msg, "tipc_net->rout: ");

	/* Handle message for this node */
	dnode = msg_short(msg) ? tipc_own_addr : msg_destnode(msg);
	if (in_scope(dnode, tipc_own_addr)) {
		if (msg_isdata(msg)) {
			if (msg_mcast(msg))
				tipc_port_recv_mcast(buf, NULL);
			else if (msg_destport(msg))
				tipc_port_recv_msg(buf);
			else
				net_route_named_msg(buf);
			return;
		}
		switch (msg_user(msg)) {
		case ROUTE_DISTRIBUTOR:
			tipc_cltr_recv_routing_table(buf);
			break;
		case NAME_DISTRIBUTOR:
			tipc_named_recv(buf);
			break;
		case CONN_MANAGER:
			tipc_port_recv_proto_msg(buf);
			break;
		default:
			msg_dbg(msg,"DROP/NET/<REC<");
			buf_discard(buf);
		}
		return;
	}

	/* Handle message for another node */
	msg_dbg(msg, "NET>SEND>: ");
	tipc_link_send(buf, dnode, msg_link_selector(msg));
}

int tipc_net_start(void)
{
	char addr_string[16];
	int res;

	if (tipc_mode != TIPC_NODE_MODE)
		return -ENOPROTOOPT;

	tipc_mode = TIPC_NET_MODE;
	tipc_named_reinit();
	tipc_port_reinit();

	if ((res = tipc_bearer_init()) ||
	    (res = net_init()) ||
	    (res = tipc_cltr_init()) ||
	    (res = tipc_bclink_init())) {
		return res;
	}
	tipc_subscr_stop();
	tipc_cfg_stop();
	tipc_k_signal((Handler)tipc_subscr_start, 0);
	tipc_k_signal((Handler)tipc_cfg_init, 0);
	info("Started in network mode\n");
	info("Own node address %s, network identity %u\n",
	     addr_string_fill(addr_string, tipc_own_addr), tipc_net_id);
	return TIPC_OK;
}

void tipc_net_stop(void)
{
	if (tipc_mode != TIPC_NET_MODE)
		return;
	write_lock_bh(&tipc_net_lock);
	tipc_bearer_stop();
	tipc_mode = TIPC_NODE_MODE;
	tipc_bclink_stop();
	net_stop();
	write_unlock_bh(&tipc_net_lock);
	info("Left network mode \n");
}

