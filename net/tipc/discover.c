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
#define TIPC_DISC_INIT	msecs_to_jiffies(125)
/* max delay if bearer has no links */
#define TIPC_DISC_FAST	msecs_to_jiffies(1000)
/* max delay if bearer has links */
#define TIPC_DISC_SLOW	msecs_to_jiffies(60000)
/* indicates no timer in use */
#define TIPC_DISC_INACTIVE	0xffffffff

/**
 * struct tipc_discoverer - information about an ongoing link setup request
 * @bearer_id: identity of bearer issuing requests
 * @net: network namespace instance
 * @dest: destination address for request messages
 * @domain: network domain to which links can be established
 * @num_nodes: number of nodes currently discovered (i.e. with an active link)
 * @lock: spinlock for controlling access to requests
 * @skb: request message to be (repeatedly) sent
 * @timer: timer governing period between requests
 * @timer_intv: current interval between requests (in ms)
 */
struct tipc_discoverer {
	u32 bearer_id;
	struct tipc_media_addr dest;
	struct net *net;
	u32 domain;
	int num_nodes;
	spinlock_t lock;
	struct sk_buff *skb;
	struct timer_list timer;
	unsigned long timer_intv;
};

/**
 * tipc_disc_init_msg - initialize a link setup message
 * @net: the applicable net namespace
 * @type: message type (request or response)
 * @b: ptr to bearer issuing message
 */
static void tipc_disc_init_msg(struct net *net, struct sk_buff *skb,
			       u32 mtyp, struct tipc_bearer *b)
{
	struct tipc_net *tn = tipc_net(net);
	u32 self = tipc_own_addr(net);
	u32 dest_domain = b->domain;
	struct tipc_msg *hdr;

	hdr = buf_msg(skb);
	tipc_msg_init(self, hdr, LINK_CONFIG, mtyp,
		      MAX_H_SIZE, dest_domain);
	msg_set_non_seq(hdr, 1);
	msg_set_node_sig(hdr, tn->random);
	msg_set_node_capabilities(hdr, TIPC_NODE_CAPABILITIES);
	msg_set_dest_domain(hdr, dest_domain);
	msg_set_bc_netid(hdr, tn->net_id);
	b->media->addr2msg(msg_media_addr(hdr), &b->addr);
}

static void tipc_disc_msg_xmit(struct net *net, u32 mtyp, u32 dst, u32 src,
			       struct tipc_media_addr *maddr,
			       struct tipc_bearer *b)
{
	struct sk_buff *skb;

	skb = tipc_buf_acquire(MAX_H_SIZE, GFP_ATOMIC);
	if (!skb)
		return;
	tipc_disc_init_msg(net, skb, mtyp, b);
	tipc_bearer_xmit_skb(net, b->identity, skb, maddr);
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
 * @net: applicable net namespace
 * @skb: buffer containing message
 * @b: bearer that message arrived on
 */
void tipc_disc_rcv(struct net *net, struct sk_buff *skb,
		   struct tipc_bearer *b)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_msg *hdr = buf_msg(skb);
	u16 caps = msg_node_capabilities(hdr);
	bool legacy = tn->legacy_addr_format;
	u32 signature = msg_node_sig(hdr);
	u32 dst = msg_dest_domain(hdr);
	u32 net_id = msg_bc_netid(hdr);
	u32 self = tipc_own_addr(net);
	struct tipc_media_addr maddr;
	u32 src = msg_prevnode(hdr);
	u32 mtyp = msg_type(hdr);
	bool dupl_addr = false;
	bool respond = false;
	int err;

	err = b->media->msg2addr(b, &maddr, msg_media_addr(hdr));
	kfree_skb(skb);
	if (err || maddr.broadcast) {
		pr_warn_ratelimited("Rcv corrupt discovery message\n");
		return;
	}
	/* Ignore discovery messages from own node */
	if (!memcmp(&maddr, &b->addr, sizeof(maddr)))
		return;
	if (net_id != tn->net_id)
		return;
	if (in_own_node(net, src)) {
		disc_dupl_alert(b, self, &maddr);
		return;
	}
	if (!tipc_in_scope(legacy, dst, self))
		return;
	if (!tipc_in_scope(legacy, b->domain, src))
		return;

	tipc_node_check_dest(net, src, b, caps, signature,
			     &maddr, &respond, &dupl_addr);
	if (dupl_addr)
		disc_dupl_alert(b, src, &maddr);
	if (!respond)
		return;
	if (mtyp != DSC_REQ_MSG)
		return;
	tipc_disc_msg_xmit(net, DSC_RESP_MSG, src, self, &maddr, b);
}

/* tipc_disc_add_dest - increment set of discovered nodes
 */
void tipc_disc_add_dest(struct tipc_discoverer *d)
{
	spin_lock_bh(&d->lock);
	d->num_nodes++;
	spin_unlock_bh(&d->lock);
}

/* tipc_disc_remove_dest - decrement set of discovered nodes
 */
void tipc_disc_remove_dest(struct tipc_discoverer *d)
{
	int intv, num;

	spin_lock_bh(&d->lock);
	d->num_nodes--;
	num = d->num_nodes;
	intv = d->timer_intv;
	if (!num && (intv == TIPC_DISC_INACTIVE || intv > TIPC_DISC_FAST))  {
		d->timer_intv = TIPC_DISC_INIT;
		mod_timer(&d->timer, jiffies + d->timer_intv);
	}
	spin_unlock_bh(&d->lock);
}

/* tipc_disc_timeout - send a periodic link setup request
 * Called whenever a link setup request timer associated with a bearer expires.
 * - Keep doubling time between sent request until limit is reached;
 * - Hold at fast polling rate if we don't have any associated nodes
 * - Otherwise hold at slow polling rate
 */
static void tipc_disc_timeout(struct timer_list *t)
{
	struct tipc_discoverer *d = from_timer(d, t, timer);
	struct tipc_media_addr maddr;
	struct sk_buff *skb = NULL;
	struct net *net;
	u32 bearer_id;

	spin_lock_bh(&d->lock);

	/* Stop searching if only desired node has been found */
	if (tipc_node(d->domain) && d->num_nodes) {
		d->timer_intv = TIPC_DISC_INACTIVE;
		goto exit;
	}
	/* Adjust timeout interval according to discovery phase */
	d->timer_intv *= 2;
	if (d->num_nodes && d->timer_intv > TIPC_DISC_SLOW)
		d->timer_intv = TIPC_DISC_SLOW;
	else if (!d->num_nodes && d->timer_intv > TIPC_DISC_FAST)
		d->timer_intv = TIPC_DISC_FAST;
	mod_timer(&d->timer, jiffies + d->timer_intv);
	memcpy(&maddr, &d->dest, sizeof(maddr));
	skb = skb_clone(d->skb, GFP_ATOMIC);
	net = d->net;
	bearer_id = d->bearer_id;
exit:
	spin_unlock_bh(&d->lock);
	if (skb)
		tipc_bearer_xmit_skb(net, bearer_id, skb, &maddr);
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
	struct tipc_discoverer *d;

	d = kmalloc(sizeof(*d), GFP_ATOMIC);
	if (!d)
		return -ENOMEM;
	d->skb = tipc_buf_acquire(MAX_H_SIZE, GFP_ATOMIC);
	if (!d->skb) {
		kfree(d);
		return -ENOMEM;
	}

	tipc_disc_init_msg(net, d->skb, DSC_REQ_MSG, b);
	memcpy(&d->dest, dest, sizeof(*dest));
	d->net = net;
	d->bearer_id = b->identity;
	d->domain = b->domain;
	d->num_nodes = 0;
	d->timer_intv = TIPC_DISC_INIT;
	spin_lock_init(&d->lock);
	timer_setup(&d->timer, tipc_disc_timeout, 0);
	mod_timer(&d->timer, jiffies + d->timer_intv);
	b->disc = d;
	*skb = skb_clone(d->skb, GFP_ATOMIC);
	return 0;
}

/**
 * tipc_disc_delete - destroy object sending periodic link setup requests
 * @d: ptr to link duest structure
 */
void tipc_disc_delete(struct tipc_discoverer *d)
{
	del_timer_sync(&d->timer);
	kfree_skb(d->skb);
	kfree(d);
}

/**
 * tipc_disc_reset - reset object to send periodic link setup requests
 * @net: the applicable net namespace
 * @b: ptr to bearer issuing requests
 * @dest_domain: network domain to which links can be established
 */
void tipc_disc_reset(struct net *net, struct tipc_bearer *b)
{
	struct tipc_discoverer *d = b->disc;
	struct tipc_media_addr maddr;
	struct sk_buff *skb;

	spin_lock_bh(&d->lock);
	tipc_disc_init_msg(net, d->skb, DSC_REQ_MSG, b);
	d->net = net;
	d->bearer_id = b->identity;
	d->domain = b->domain;
	d->num_nodes = 0;
	d->timer_intv = TIPC_DISC_INIT;
	memcpy(&maddr, &d->dest, sizeof(maddr));
	mod_timer(&d->timer, jiffies + d->timer_intv);
	skb = skb_clone(d->skb, GFP_ATOMIC);
	spin_unlock_bh(&d->lock);
	if (skb)
		tipc_bearer_xmit_skb(net, b->identity, skb, &maddr);
}
