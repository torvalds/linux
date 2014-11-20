/*
 * net/tipc/discover.c
 *
 * Copyright (c) 2003-2006, 2014, Ericsson AB
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
#include "link.h"
#include "discover.h"

#define TIPC_LINK_REQ_INIT	125	/* min delay during bearer start up */
#define TIPC_LINK_REQ_FAST	1000	/* max delay if bearer has no links */
#define TIPC_LINK_REQ_SLOW	60000	/* max delay if bearer has links */
#define TIPC_LINK_REQ_INACTIVE	0xffffffff /* indicates no timer in use */


/**
 * struct tipc_link_req - information about an ongoing link setup request
 * @bearer_id: identity of bearer issuing requests
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
	u32 domain;
	int num_nodes;
	spinlock_t lock;
	struct sk_buff *buf;
	struct timer_list timer;
	unsigned int timer_intv;
};

/**
 * tipc_disc_init_msg - initialize a link setup message
 * @type: message type (request or response)
 * @b_ptr: ptr to bearer issuing message
 */
static void tipc_disc_init_msg(struct sk_buff *buf, u32 type,
			       struct tipc_bearer *b_ptr)
{
	struct tipc_msg *msg;
	u32 dest_domain = b_ptr->domain;

	msg = buf_msg(buf);
	tipc_msg_init(msg, LINK_CONFIG, type, INT_H_SIZE, dest_domain);
	msg_set_non_seq(msg, 1);
	msg_set_node_sig(msg, tipc_random);
	msg_set_dest_domain(msg, dest_domain);
	msg_set_bc_netid(msg, tipc_net_id);
	b_ptr->media->addr2msg(msg_media_addr(msg), &b_ptr->addr);
}

/**
 * disc_dupl_alert - issue node address duplication alert
 * @b_ptr: pointer to bearer detecting duplication
 * @node_addr: duplicated node address
 * @media_addr: media address advertised by duplicated node
 */
static void disc_dupl_alert(struct tipc_bearer *b_ptr, u32 node_addr,
			    struct tipc_media_addr *media_addr)
{
	char node_addr_str[16];
	char media_addr_str[64];

	tipc_addr_string_fill(node_addr_str, node_addr);
	tipc_media_addr_printf(media_addr_str, sizeof(media_addr_str),
			       media_addr);
	pr_warn("Duplicate %s using %s seen on <%s>\n", node_addr_str,
		media_addr_str, b_ptr->name);
}

/**
 * tipc_disc_rcv - handle incoming discovery message (request or response)
 * @buf: buffer containing message
 * @bearer: bearer that message arrived on
 */
void tipc_disc_rcv(struct sk_buff *buf, struct tipc_bearer *bearer)
{
	struct tipc_node *node;
	struct tipc_link *link;
	struct tipc_media_addr maddr;
	struct sk_buff *rbuf;
	struct tipc_msg *msg = buf_msg(buf);
	u32 ddom = msg_dest_domain(msg);
	u32 onode = msg_prevnode(msg);
	u32 net_id = msg_bc_netid(msg);
	u32 mtyp = msg_type(msg);
	u32 signature = msg_node_sig(msg);
	bool addr_match = false;
	bool sign_match = false;
	bool link_up = false;
	bool accept_addr = false;
	bool accept_sign = false;
	bool respond = false;

	bearer->media->msg2addr(bearer, &maddr, msg_media_addr(msg));
	kfree_skb(buf);

	/* Ensure message from node is valid and communication is permitted */
	if (net_id != tipc_net_id)
		return;
	if (maddr.broadcast)
		return;
	if (!tipc_addr_domain_valid(ddom))
		return;
	if (!tipc_addr_node_valid(onode))
		return;

	if (in_own_node(onode)) {
		if (memcmp(&maddr, &bearer->addr, sizeof(maddr)))
			disc_dupl_alert(bearer, tipc_own_addr, &maddr);
		return;
	}
	if (!tipc_in_scope(ddom, tipc_own_addr))
		return;
	if (!tipc_in_scope(bearer->domain, onode))
		return;

	/* Locate, or if necessary, create, node: */
	node = tipc_node_find(onode);
	if (!node)
		node = tipc_node_create(onode);
	if (!node)
		return;

	tipc_node_lock(node);
	link = node->links[bearer->identity];

	/* Prepare to validate requesting node's signature and media address */
	sign_match = (signature == node->signature);
	addr_match = link && !memcmp(&link->media_addr, &maddr, sizeof(maddr));
	link_up = link && tipc_link_is_up(link);


	/* These three flags give us eight permutations: */

	if (sign_match && addr_match && link_up) {
		/* All is fine. Do nothing. */
	} else if (sign_match && addr_match && !link_up) {
		/* Respond. The link will come up in due time */
		respond = true;
	} else if (sign_match && !addr_match && link_up) {
		/* Peer has changed i/f address without rebooting.
		 * If so, the link will reset soon, and the next
		 * discovery will be accepted. So we can ignore it.
		 * It may also be an cloned or malicious peer having
		 * chosen the same node address and signature as an
		 * existing one.
		 * Ignore requests until the link goes down, if ever.
		 */
		disc_dupl_alert(bearer, onode, &maddr);
	} else if (sign_match && !addr_match && !link_up) {
		/* Peer link has changed i/f address without rebooting.
		 * It may also be a cloned or malicious peer; we can't
		 * distinguish between the two.
		 * The signature is correct, so we must accept.
		 */
		accept_addr = true;
		respond = true;
	} else if (!sign_match && addr_match && link_up) {
		/* Peer node rebooted. Two possibilities:
		 *  - Delayed re-discovery; this link endpoint has already
		 *    reset and re-established contact with the peer, before
		 *    receiving a discovery message from that node.
		 *    (The peer happened to receive one from this node first).
		 *  - The peer came back so fast that our side has not
		 *    discovered it yet. Probing from this side will soon
		 *    reset the link, since there can be no working link
		 *    endpoint at the peer end, and the link will re-establish.
		 *  Accept the signature, since it comes from a known peer.
		 */
		accept_sign = true;
	} else if (!sign_match && addr_match && !link_up) {
		/*  The peer node has rebooted.
		 *  Accept signature, since it is a known peer.
		 */
		accept_sign = true;
		respond = true;
	} else if (!sign_match && !addr_match && link_up) {
		/* Peer rebooted with new address, or a new/duplicate peer.
		 * Ignore until the link goes down, if ever.
		 */
		disc_dupl_alert(bearer, onode, &maddr);
	} else if (!sign_match && !addr_match && !link_up) {
		/* Peer rebooted with new address, or it is a new peer.
		 * Accept signature and address.
		*/
		accept_sign = true;
		accept_addr = true;
		respond = true;
	}

	if (accept_sign)
		node->signature = signature;

	if (accept_addr) {
		if (!link)
			link = tipc_link_create(node, bearer, &maddr);
		if (link) {
			memcpy(&link->media_addr, &maddr, sizeof(maddr));
			tipc_link_reset(link);
		} else {
			respond = false;
		}
	}

	/* Send response, if necessary */
	if (respond && (mtyp == DSC_REQ_MSG)) {
		rbuf = tipc_buf_acquire(INT_H_SIZE);
		if (rbuf) {
			tipc_disc_init_msg(rbuf, DSC_RESP_MSG, bearer);
			tipc_bearer_send(bearer->identity, rbuf, &maddr);
			kfree_skb(rbuf);
		}
	}
	tipc_node_unlock(node);
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
			k_start_timer(&req->timer, req->timer_intv);
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
 * @req: ptr to link request structure
 *
 * Called whenever a link setup request timer associated with a bearer expires.
 */
static void disc_timeout(struct tipc_link_req *req)
{
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
	tipc_bearer_send(req->bearer_id, req->buf, &req->dest);


	req->timer_intv *= 2;
	if (req->num_nodes)
		max_delay = TIPC_LINK_REQ_SLOW;
	else
		max_delay = TIPC_LINK_REQ_FAST;
	if (req->timer_intv > max_delay)
		req->timer_intv = max_delay;

	k_start_timer(&req->timer, req->timer_intv);
exit:
	spin_unlock_bh(&req->lock);
}

/**
 * tipc_disc_create - create object to send periodic link setup requests
 * @b_ptr: ptr to bearer issuing requests
 * @dest: destination address for request messages
 * @dest_domain: network domain to which links can be established
 *
 * Returns 0 if successful, otherwise -errno.
 */
int tipc_disc_create(struct tipc_bearer *b_ptr, struct tipc_media_addr *dest)
{
	struct tipc_link_req *req;

	req = kmalloc(sizeof(*req), GFP_ATOMIC);
	if (!req)
		return -ENOMEM;

	req->buf = tipc_buf_acquire(INT_H_SIZE);
	if (!req->buf) {
		kfree(req);
		return -ENOMEM;
	}

	tipc_disc_init_msg(req->buf, DSC_REQ_MSG, b_ptr);
	memcpy(&req->dest, dest, sizeof(*dest));
	req->bearer_id = b_ptr->identity;
	req->domain = b_ptr->domain;
	req->num_nodes = 0;
	req->timer_intv = TIPC_LINK_REQ_INIT;
	spin_lock_init(&req->lock);
	k_init_timer(&req->timer, (Handler)disc_timeout, (unsigned long)req);
	k_start_timer(&req->timer, req->timer_intv);
	b_ptr->link_req = req;
	tipc_bearer_send(req->bearer_id, req->buf, &req->dest);
	return 0;
}

/**
 * tipc_disc_delete - destroy object sending periodic link setup requests
 * @req: ptr to link request structure
 */
void tipc_disc_delete(struct tipc_link_req *req)
{
	k_cancel_timer(&req->timer);
	k_term_timer(&req->timer);
	kfree_skb(req->buf);
	kfree(req);
}

/**
 * tipc_disc_reset - reset object to send periodic link setup requests
 * @b_ptr: ptr to bearer issuing requests
 * @dest_domain: network domain to which links can be established
 */
void tipc_disc_reset(struct tipc_bearer *b_ptr)
{
	struct tipc_link_req *req = b_ptr->link_req;

	spin_lock_bh(&req->lock);
	tipc_disc_init_msg(req->buf, DSC_REQ_MSG, b_ptr);
	req->bearer_id = b_ptr->identity;
	req->domain = b_ptr->domain;
	req->num_nodes = 0;
	req->timer_intv = TIPC_LINK_REQ_INIT;
	k_start_timer(&req->timer, req->timer_intv);
	tipc_bearer_send(req->bearer_id, req->buf, &req->dest);
	spin_unlock_bh(&req->lock);
}
