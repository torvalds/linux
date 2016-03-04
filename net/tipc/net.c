/*
 * net/tipc/net.c: TIPC network routing code
 *
 * Copyright (c) 1995-2006, 2014, Ericsson AB
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
#include "socket.h"
#include "node.h"
#include "bcast.h"
#include "netlink.h"

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

int tipc_net_start(struct net *net, u32 addr)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	char addr_string[16];

	tn->own_addr = addr;
	tipc_named_reinit(net);
	tipc_sk_reinit(net);

	tipc_nametbl_publish(net, TIPC_CFG_SRV, tn->own_addr, tn->own_addr,
			     TIPC_ZONE_SCOPE, 0, tn->own_addr);

	pr_info("Started in network mode\n");
	pr_info("Own node address %s, network identity %u\n",
		tipc_addr_string_fill(addr_string, tn->own_addr),
		tn->net_id);
	return 0;
}

void tipc_net_stop(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);

	if (!tn->own_addr)
		return;

	tipc_nametbl_withdraw(net, TIPC_CFG_SRV, tn->own_addr, 0,
			      tn->own_addr);
	rtnl_lock();
	tipc_bearer_stop(net);
	tipc_node_stop(net);
	rtnl_unlock();

	pr_info("Left network mode\n");
}

static int __tipc_nl_add_net(struct net *net, struct tipc_nl_msg *msg)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	void *hdr;
	struct nlattr *attrs;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_NET_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_NET);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_NET_ID, tn->net_id))
		goto attr_msg_full;

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_net_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int err;
	int done = cb->args[0];
	struct tipc_nl_msg msg;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	err = __tipc_nl_add_net(net, &msg);
	if (err)
		goto out;

	done = 1;
out:
	cb->args[0] = done;

	return skb->len;
}

int tipc_nl_net_set(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct nlattr *attrs[TIPC_NLA_NET_MAX + 1];
	int err;

	if (!info->attrs[TIPC_NLA_NET])
		return -EINVAL;

	err = nla_parse_nested(attrs, TIPC_NLA_NET_MAX,
			       info->attrs[TIPC_NLA_NET],
			       tipc_nl_net_policy);
	if (err)
		return err;

	if (attrs[TIPC_NLA_NET_ID]) {
		u32 val;

		/* Can't change net id once TIPC has joined a network */
		if (tn->own_addr)
			return -EPERM;

		val = nla_get_u32(attrs[TIPC_NLA_NET_ID]);
		if (val < 1 || val > 9999)
			return -EINVAL;

		tn->net_id = val;
	}

	if (attrs[TIPC_NLA_NET_ADDR]) {
		u32 addr;

		/* Can't change net addr once TIPC has joined a network */
		if (tn->own_addr)
			return -EPERM;

		addr = nla_get_u32(attrs[TIPC_NLA_NET_ADDR]);
		if (!tipc_addr_node_valid(addr))
			return -EINVAL;

		rtnl_lock();
		tipc_net_start(net, addr);
		rtnl_unlock();
	}

	return 0;
}
