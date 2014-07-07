/*
 * net/tipc/net.c: TIPC network routing code
 *
 * Copyright (c) 1995-2006, Ericsson AB
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
#include "net.h"
#include "name_distr.h"
#include "subscr.h"
#include "port.h"
#include "socket.h"
#include "node.h"
#include "config.h"

/*
 * The TIPC locking policy is designed to ensure a very fine locking
 * granularity, permitting complete parallel access to individual
 * port and node/link instances. The code consists of four major
 * locking domains, each protected with their own disjunct set of locks.
 *
 * 1: The bearer level.
 *    RTNL lock is used to serialize the process of configuring bearer
 *    on update side, and RCU lock is applied on read side to make
 *    bearer instance valid on both paths of message transmission and
 *    reception.
 *
 * 2: The node and link level.
 *    All node instances are saved into two tipc_node_list and node_htable
 *    lists. The two lists are protected by node_list_lock on write side,
 *    and they are guarded with RCU lock on read side. Especially node
 *    instance is destroyed only when TIPC module is removed, and we can
 *    confirm that there has no any user who is accessing the node at the
 *    moment. Therefore, Except for iterating the two lists within RCU
 *    protection, it's no needed to hold RCU that we access node instance
 *    in other places.
 *
 *    In addition, all members in node structure including link instances
 *    are protected by node spin lock.
 *
 * 3: The transport level of the protocol.
 *    This consists of the structures port, (and its user level
 *    representations, such as user_port and tipc_sock), reference and
 *    tipc_user (port.c, reg.c, socket.c).
 *
 *    This layer has four different locks:
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
 *  4: The name table (name_table.c, name_distr.c, subscription.c)
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

static void net_route_named_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	u32 dnode;
	u32 dport;

	if (!msg_named(msg)) {
		kfree_skb(buf);
		return;
	}

	dnode = addr_domain(msg_lookup_scope(msg));
	dport = tipc_nametbl_translate(msg_nametype(msg), msg_nameinst(msg), &dnode);
	if (dport) {
		msg_set_destnode(msg, dnode);
		msg_set_destport(msg, dport);
		tipc_net_route_msg(buf);
		return;
	}
	tipc_reject_msg(buf, TIPC_ERR_NO_NAME);
}

void tipc_net_route_msg(struct sk_buff *buf)
{
	struct tipc_msg *msg;
	u32 dnode;

	if (!buf)
		return;
	msg = buf_msg(buf);

	/* Handle message for this node */
	dnode = msg_short(msg) ? tipc_own_addr : msg_destnode(msg);
	if (tipc_in_scope(dnode, tipc_own_addr)) {
		if (msg_isdata(msg)) {
			if (msg_mcast(msg))
				tipc_port_mcast_rcv(buf, NULL);
			else if (msg_destport(msg))
				tipc_sk_rcv(buf);
			else
				net_route_named_msg(buf);
			return;
		}
		switch (msg_user(msg)) {
		case NAME_DISTRIBUTOR:
			tipc_named_rcv(buf);
			break;
		case CONN_MANAGER:
			tipc_port_proto_rcv(buf);
			break;
		default:
			kfree_skb(buf);
		}
		return;
	}

	/* Handle message for another node */
	skb_trim(buf, msg_size(msg));
	tipc_link_xmit(buf, dnode, msg_link_selector(msg));
}

int tipc_net_start(u32 addr)
{
	char addr_string[16];
	int res;

	tipc_own_addr = addr;
	tipc_named_reinit();
	tipc_port_reinit();
	res = tipc_bclink_init();
	if (res)
		return res;

	tipc_nametbl_publish(TIPC_CFG_SRV, tipc_own_addr, tipc_own_addr,
			     TIPC_ZONE_SCOPE, 0, tipc_own_addr);

	pr_info("Started in network mode\n");
	pr_info("Own node address %s, network identity %u\n",
		tipc_addr_string_fill(addr_string, tipc_own_addr), tipc_net_id);
	return 0;
}

void tipc_net_stop(void)
{
	if (!tipc_own_addr)
		return;

	tipc_nametbl_withdraw(TIPC_CFG_SRV, tipc_own_addr, 0, tipc_own_addr);
	rtnl_lock();
	tipc_bearer_stop();
	tipc_bclink_stop();
	tipc_node_stop();
	rtnl_unlock();

	pr_info("Left network mode\n");
}
