/*
 * net/tipc/discover.c
 *
 * Copyright (c) 2003-2006, 2014-2015, Ericsson AB
 * Copyright (c) 2005-2006, 2010-2011, Wind River Systems
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
#include "node.h"
#include "discover.h"

/* min delay during bearer start up */
#define TIPC_LINK_REQ_INIT	msecs_to_jiffies(125)
/* max delay if bearer has no links */
#define TIPC_LINK_REQ_FAST	msecs_to_jiffies(1000)
/* max delay if bearer has links */
#define TIPC_LINK_REQ_SLOW	msecs_to_jiffies(60000)
/* indicates no timer in use */
#define TIPC_LINK_REQ_INACTIVE	0xffffffff

/**
 * struct tipc_link_req - information about an ongoing link setup request
 * @bearer_id: identity of bearer issuing requests
 * @net: network namespace instance
 * @dest: destination address for request messages
 * @domain: network domain to which links can be established
 * @num_nodes: number of nodes currently discovered (i.e. with an active link)
 * @lock: spinlock for controlling access to requests
 * @buf: request message to be (repeatedly) sent
 * @timer: timer governing period between requests
 * @timer_intv: current interval between requests (in ms)
 */
struct tipc_link_req {
	u32 bearer_id;
	struct tipc_media_addr dest;
	struct net *net;
	u32 domain;
	int num_nodes;
	spinlock_t lock;
	struct sk_buff *buf;
	struct timer_list timer;
	unsigned long timer_intv;
};

/**
 * tipc_disc_init_msg - initialize a link setup message
 * @net: the applicable net namespace
 * @type: message type (request or response)
 * @b: ptr to bearer issuing message
 */
static void tipc_disc_init_msg(struct net *net, struct sk_buff *buf, u32 type,
			       struct tipc_bearer *b)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_msg *msg;
	u32 dest_domain = b->domain;

	msg = buf_msg(buf);
	tipc_msg_init(tn->own_addr, msg, LINK_CONFIG, type,
		      MAX_H_SIZE, dest_domain);
	msg_set_non_seq(msg, 1);
	msg_set_node_sig(msg, tn->random);
	msg_set_node_capabilities(msg, TIPC_NODE_CAPABILITIES);
	msg_set_dest_domain(msg, dest_domain);
	msg_set_bc_netid(msg, tn->net_id);
	b->media->addr2msg(msg_media_addr(msg), &b->addr);
}

/**
 * disc_dupl_alert - issue node address duplication alert
 * @b: pointer to bearer detecting duplication
 * @node_addr: duplicated node address
 * @media_addr: media address advertised by duplicated node
 */
static void disc_dupl_alert(struct tipc_bearer *b, u32 node_addr,
			    struct tipc_media_addr *media_addr)
{
	char node_addr_str[16];
	char media_addr_str[64];

	tipc_addr_string_fill(node_addr_str, node_addr);
	tipc_media_addr_printf(media_addr_str, sizeof(media_addr_str),
			       media_addr);
	pr_warn("Duplicate %s using %s seen on <%s>\n", node_addr_str,
		media_addr_str, b->name);
}

/**
 * tipc_disc_rcv - handle incoming discovery message (request or response)
 * @net: the applicable net namespace
 * @buf: buffer containing message
 * @bearer: bearer that message arrived on
 */
void tipc_disc_rcv(struct net *net, struct sk_buff *skb,
		   struct tipc_bearer *bearer)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_media_addr maddr;
	struct sk_buff *rskb;
	struct tipc_msg *hdr = buf_msg(skb);
	u32 ddom = msg_dest_domain(hdr);
	u32 onode = msg_prevnode(hdr);
	u32 net_id = msg_bc_netid(hdr);
	u32 mtyp = msg_type(hdr);
	u32 signature = msg_node_sig(hdr);
	u16 caps = msg_node_capabilities(hdr);
	bool respond = false;
	bool dupl_addr = false;
	int err;

	err = bearer->media->msg2addr(bearer, &maddr, msg_media_addr(hdr));
	kfree_skb(skb);
	if (err)
		return;

	/* Ensure message from node is valid and communication is permitted */
	if (net_id != tn->net_id)
		return;
	if (maddr.broadcast)
		return;
	if (!tipc_addr_domain_valid(ddom))
		return;
	if (!tipc_addr_node_valid(onode))
		return;

	if (in_own_node(net, onode)) {
		if (memcmp(&maddr, &bearer->addr, sizeof(maddr)))
			disc_dupl_alert(bearer, tn->own_addr, &maddr);
		return;
	}
	if (!tipc_in_scope(ddom, tn->own_addr))
		return;
	if (!tipc_in_scope(bearer->domain, onode))
		return;

	tipc_node_check_dest(net, onode, bearer, caps, signature,
			     &maddr, &respond, &dupl_addr);
	if (dupl_addr)
		disc_dupl_alert(bearer, onode, &maddr);

	/* Send response, if necessary */
	if (respond && (mtyp == DSC_REQ_MSG)) {
		rskb = tipc_buf_acquire(MAX_H_SIZE, GFP_ATOMIC);
		if (!rskb)
			return;
		tipc_disc_init_msg(net, rskb, DSC_RESP_MSG, bearer);
		tipc_bearer_xmit_skb(net, bearer->identity, rskb, &maddr);
	}
}

/**
 * disc_update - update frequency of periodic link setup requests
 * @req: ptr to link request structure
 *
 * Reinitiates discovery process if discovery object has no associated nodes
 * and is either not currently searching or is searching at a slow rate
 */
static void disc_update(struct tipc_link_req *req)
{
	if (!req->num_nodes) {
		if ((req->timer_intv == TIPC_LINK_REQ_INACTIVE) ||
		    (req->timer_intv > TIPC_LINK_REQ_FAST)) {
			req->timer_intv = TIPC_LINK_REQ_INIT;
			mod_timer(&req->timer, jiffies + req->timer_intv);
		}
	}
}

/**
 * tipc_disc_add_dest - increment set of discovered nodes
 * @req: ptr to link request structure
 */
void tipc_disc_add_dest(struct tipc_link_req *req)
{
	spin_lock_bh(&req->lock);
	req->num_nodes++;
	spin_unlock_bh(&req->lock);
}

/**
 * tipc_disc_remove_dest - decrement set of discovered nodes
 * @req: ptr to link request structure
 */
void tipc_disc_remove_dest(struct tipc_link_req *req)
{
	spin_lock_bh(&req->lock);
	req->num_nodes--;
	disc_update(req);
	spin_unlock_bh(&req->lock);
}

/**
 * disc_timeout - send a periodic link setup request
 * @data: ptr to link request structure
 *
 * Called whenever a link setup request timer associated with a bearer expires.
 */
static void disc_timeout(unsigned long data)
{
	struct tipc_link_req *req = (struct tipc_link_req *)data;
	struct sk_buff *skb;
	int max_delay;

	spin_lock_bh(&req->lock);

	/* Stop searching if only desired node has been found */
	if (tipc_node(req->domain) && req->num_nodes) {
		req->timer_intv = TIPC_LINK_REQ_INACTIVE;
		goto exit;
	}

	/*
	 * Send discovery message, then update discovery timer
	 *
	 * Keep doubling time between requests until limit is reached;
	 * hold at fast polling rate if don't have any associated nodes,
	 * otherwise hold at slow polling rate
	 */
	skb = skb_clone(req->buf, GFP_ATOMIC);
	if (skb)
		tipc_bearer_xmit_skb(req->net, req->bearer_id, skb, &req->dest);
	req->timer_intv *= 2;
	if (req->num_nodes)
		max_delay = TIPC_LINK_REQ_SLOW;
	else
		max_delay = TIPC_LINK_REQ_FAST;
	if (req->timer_intv > max_delay)
		req->timer_intv = max_delay;

	mod_timer(&req->timer, jiffies + req->timer_intv);
exit:
	spin_unlock_bh(&req->lock);
}

/**
 * tipc_disc_create - create object to send periodic link setup requests
 * @net: the applicable net namespace
 * @b: ptr to bearer issuing requests
 * @dest: destination address for request messages
 * @dest_domain: network domain to which links can be established
 *
 * Returns 0 if successful, otherwise -errno.
 */
int tipc_disc_create(struct net *net, struct tipc_bearer *b,
		     struct tipc_media_addr *dest, struct sk_buff **skb)
{
	struct tipc_link_req *req;

	req = kmalloc(sizeof(*req), GFP_ATOMIC);
	if (!req)
		return -ENOMEM;
	req->buf = tipc_buf_acquire(MAX_H_SIZE, GFP_ATOMIC);
	if (!req->buf) {
		kfree(req);
		return -ENOMEM;
	}

	tipc_disc_init_msg(net, req->buf, DSC_REQ_MSG, b);
	memcpy(&req->dest, dest, sizeof(*dest));
	req->net = net;
	req->bearer_id = b->identity;
	req->domain = b->domain;
	req->num_nodes = 0;
	req->timer_intv = TIPC_LINK_REQ_INIT;
	spin_lock_init(&req->lock);
	setup_timer(&req->timer, disc_timeout, (unsigned long)req);
	mod_timer(&req->timer, jiffies + req->timer_intv);
	b->link_req = req;
	*skb = skb_clone(req->buf, GFP_ATOMIC);
	return 0;
}

/**
 * tipc_disc_delete - destroy object sending periodic link setup requests
 * @req: ptr to link request structure
 */
void tipc_disc_delete(struct tipc_link_req *req)
{
	del_timer_sync(&req->timer);
	kfree_skb(req->buf);
	kfree(req);
}

/**
 * tipc_disc_reset - reset object to send periodic link setup requests
 * @net: the applicable net namespace
 * @b: ptr to bearer issuing requests
 * @dest_domain: network domain to which links can be established
 */
void tipc_disc_reset(struct net *net, struct tipc_bearer *b)
{
	struct tipc_link_req *req = b->link_req;
	struct sk_buff *skb;

	spin_lock_bh(&req->lock);
	tipc_disc_init_msg(net, req->buf, DSC_REQ_MSG, b);
	req->net = net;
	req->bearer_id = b->identity;
	req->domain = b->domain;
	req->num_nodes = 0;
	req->timer_intv = TIPC_LINK_REQ_INIT;
	mod_timer(&req->timer, jiffies + req->timer_intv);
	skb = skb_clone(req->buf, GFP_ATOMIC);
	if (skb)
		tipc_bearer_xmit_skb(net, req->bearer_id, skb, &req->dest);
	spin_unlock_bh(&req->lock);
}
