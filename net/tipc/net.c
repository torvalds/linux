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
#include "monitor.h"

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

static void tipc_net_finalize(struct net *net, u32 addr);

int tipc_net_init(struct net *net, u8 *node_id, u32 addr)
{
	if (tipc_own_id(net)) {
		pr_info("Cannot configure node identity twice\n");
		return -1;
	}
	pr_info("Started in network mode\n");

	if (node_id)
		tipc_set_node_id(net, node_id);
	if (addr)
		tipc_net_finalize(net, addr);
	return 0;
}

static void tipc_net_finalize(struct net *net, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);

	if (cmpxchg(&tn->node_addr, 0, addr))
		return;
	tipc_set_node_addr(net, addr);
	tipc_named_reinit(net);
	tipc_sk_reinit(net);
	tipc_mon_reinit_self(net);
	tipc_nametbl_publish(net, TIPC_NODE_STATE, addr, addr,
			     TIPC_CLUSTER_SCOPE, 0, addr);
}

void tipc_net_finalize_work(struct work_struct *work)
{
	struct tipc_net_work *fwork;

	fwork = container_of(work, struct tipc_net_work, work);
	tipc_net_finalize(fwork->net, fwork->addr);
}

void tipc_sched_net_finalize(struct net *net, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);

	tn->final_work.net = net;
	tn->final_work.addr = addr;
	schedule_work(&tn->final_work.work);
}

void tipc_net_stop(struct net *net)
{
	if (!tipc_own_id(net))
		return;

	rtnl_lock();
	tipc_bearer_stop(net);
	tipc_node_stop(net);
	rtnl_unlock();

	pr_info("Left network mode\n");
}

static int __tipc_nl_add_net(struct net *net, struct tipc_nl_msg *msg)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	u64 *w0 = (u64 *)&tn->node_id[0];
	u64 *w1 = (u64 *)&tn->node_id[8];
	struct nlattr *attrs;
	void *hdr;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_NET_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start_noflag(msg->skb, TIPC_NLA_NET);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_NET_ID, tn->net_id))
		goto attr_msg_full;
	if (nla_put_u64_64bit(msg->skb, TIPC_NLA_NET_NODEID, *w0, 0))
		goto attr_msg_full;
	if (nla_put_u64_64bit(msg->skb, TIPC_NLA_NET_NODEID_W1, *w1, 0))
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

int __tipc_nl_net_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[TIPC_NLA_NET_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct tipc_net *tn = tipc_net(net);
	int err;

	if (!info->attrs[TIPC_NLA_NET])
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, TIPC_NLA_NET_MAX,
					  info->attrs[TIPC_NLA_NET],
					  tipc_nl_net_policy, info->extack);

	if (err)
		return err;

	/* Can't change net id once TIPC has joined a network */
	if (tipc_own_addr(net))
		return -EPERM;

	if (attrs[TIPC_NLA_NET_ID]) {
		u32 val;

		val = nla_get_u32(attrs[TIPC_NLA_NET_ID]);
		if (val < 1 || val > 9999)
			return -EINVAL;

		tn->net_id = val;
	}

	if (attrs[TIPC_NLA_NET_ADDR]) {
		u32 addr;

		addr = nla_get_u32(attrs[TIPC_NLA_NET_ADDR]);
		if (!addr)
			return -EINVAL;
		tn->legacy_addr_format = true;
		tipc_net_init(net, NULL, addr);
	}

	if (attrs[TIPC_NLA_NET_NODEID]) {
		u8 node_id[NODE_ID_LEN];
		u64 *w0 = (u64 *)&node_id[0];
		u64 *w1 = (u64 *)&node_id[8];

		if (!attrs[TIPC_NLA_NET_NODEID_W1])
			return -EINVAL;
		*w0 = nla_get_u64(attrs[TIPC_NLA_NET_NODEID]);
		*w1 = nla_get_u64(attrs[TIPC_NLA_NET_NODEID_W1]);
		tipc_net_init(net, node_id, 0);
	}
	return 0;
}

int tipc_nl_net_set(struct sk_buff *skb, struct genl_info *info)
{
	int err;

	rtnl_lock();
	err = __tipc_nl_net_set(skb, info);
	rtnl_unlock();

	return err;
}

static int __tipc_nl_addr_legacy_get(struct net *net, struct tipc_nl_msg *msg)
{
	struct tipc_net *tn = tipc_net(net);
	struct nlattr *attrs;
	void *hdr;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  0, TIPC_NL_ADDR_LEGACY_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_NET);
	if (!attrs)
		goto msg_full;

	if (tn->legacy_addr_format)
		if (nla_put_flag(msg->skb, TIPC_NLA_NET_ADDR_LEGACY))
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

int tipc_nl_net_addr_legacy_get(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct tipc_nl_msg msg;
	struct sk_buff *rep;
	int err;

	rep = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!rep)
		return -ENOMEM;

	msg.skb = rep;
	msg.portid = info->snd_portid;
	msg.seq = info->snd_seq;

	err = __tipc_nl_addr_legacy_get(net, &msg);
	if (err) {
		nlmsg_free(msg.skb);
		return err;
	}

	return genlmsg_reply(msg.skb, info);
}
