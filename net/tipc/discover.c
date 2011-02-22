/*
 * net/tipc/discover.c
 *
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005-2006, Wind River Systems
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
#define TIPC_LINK_REQ_FAST	2000	/* normal delay if bearer has no links */
#define TIPC_LINK_REQ_SLOW	600000	/* normal delay if bearer has links */

/*
 * TODO: Most of the inter-cluster setup stuff should be
 * rewritten, and be made conformant with specification.
 */


/**
 * struct link_req - information about an ongoing link setup request
 * @bearer: bearer issuing requests
 * @dest: destination address for request messages
 * @buf: request message to be (repeatedly) sent
 * @timer: timer governing period between requests
 * @timer_intv: current interval between requests (in ms)
 */
struct link_req {
	struct bearer *bearer;
	struct tipc_media_addr dest;
	struct sk_buff *buf;
	struct timer_list timer;
	unsigned int timer_intv;
};

/**
 * tipc_disc_init_msg - initialize a link setup message
 * @type: message type (request or response)
 * @req_links: number of links associated with message
 * @dest_domain: network domain of node(s) which should respond to message
 * @b_ptr: ptr to bearer issuing message
 */

static struct sk_buff *tipc_disc_init_msg(u32 type,
					  u32 req_links,
					  u32 dest_domain,
					  struct bearer *b_ptr)
{
	struct sk_buff *buf = tipc_buf_acquire(DSC_H_SIZE);
	struct tipc_msg *msg;

	if (buf) {
		msg = buf_msg(buf);
		tipc_msg_init(msg, LINK_CONFIG, type, DSC_H_SIZE, dest_domain);
		msg_set_non_seq(msg, 1);
		msg_set_req_links(msg, req_links);
		msg_set_dest_domain(msg, dest_domain);
		msg_set_bc_netid(msg, tipc_net_id);
		msg_set_media_addr(msg, &b_ptr->publ.addr);
	}
	return buf;
}

/**
 * disc_dupl_alert - issue node address duplication alert
 * @b_ptr: pointer to bearer detecting duplication
 * @node_addr: duplicated node address
 * @media_addr: media address advertised by duplicated node
 */

static void disc_dupl_alert(struct bearer *b_ptr, u32 node_addr,
			    struct tipc_media_addr *media_addr)
{
	char node_addr_str[16];
	char media_addr_str[64];
	struct print_buf pb;

	tipc_addr_string_fill(node_addr_str, node_addr);
	tipc_printbuf_init(&pb, media_addr_str, sizeof(media_addr_str));
	tipc_media_addr_printf(&pb, media_addr);
	tipc_printbuf_validate(&pb);
	warn("Duplicate %s using %s seen on <%s>\n",
	     node_addr_str, media_addr_str, b_ptr->publ.name);
}

/**
 * tipc_disc_recv_msg - handle incoming link setup message (request or response)
 * @buf: buffer containing message
 * @b_ptr: bearer that message arrived on
 */

void tipc_disc_recv_msg(struct sk_buff *buf, struct bearer *b_ptr)
{
	struct link *link;
	struct tipc_media_addr media_addr;
	struct tipc_msg *msg = buf_msg(buf);
	u32 dest = msg_dest_domain(msg);
	u32 orig = msg_prevnode(msg);
	u32 net_id = msg_bc_netid(msg);
	u32 type = msg_type(msg);

	msg_get_media_addr(msg, &media_addr);
	buf_discard(buf);

	if (net_id != tipc_net_id)
		return;
	if (!tipc_addr_domain_valid(dest))
		return;
	if (!tipc_addr_node_valid(orig))
		return;
	if (orig == tipc_own_addr) {
		if (memcmp(&media_addr, &b_ptr->publ.addr, sizeof(media_addr)))
			disc_dupl_alert(b_ptr, tipc_own_addr, &media_addr);
		return;
	}
	if (!tipc_in_scope(dest, tipc_own_addr))
		return;
	if (in_own_cluster(orig)) {
		/* Always accept link here */
		struct sk_buff *rbuf;
		struct tipc_media_addr *addr;
		struct tipc_node *n_ptr = tipc_node_find(orig);
		int link_fully_up;

		if (n_ptr == NULL) {
			n_ptr = tipc_node_create(orig);
			if (!n_ptr)
				return;
		}
		spin_lock_bh(&n_ptr->lock);

		/* Don't talk to neighbor during cleanup after last session */

		if (n_ptr->cleanup_required) {
			spin_unlock_bh(&n_ptr->lock);
			return;
		}

		link = n_ptr->links[b_ptr->identity];
		if (!link) {
			link = tipc_link_create(b_ptr, orig, &media_addr);
			if (!link) {
				spin_unlock_bh(&n_ptr->lock);
				return;
			}
		}
		addr = &link->media_addr;
		if (memcmp(addr, &media_addr, sizeof(*addr))) {
			if (tipc_link_is_up(link) || (!link->started)) {
				disc_dupl_alert(b_ptr, orig, &media_addr);
				spin_unlock_bh(&n_ptr->lock);
				return;
			}
			warn("Resetting link <%s>, peer interface address changed\n",
			     link->name);
			memcpy(addr, &media_addr, sizeof(*addr));
			tipc_link_reset(link);
		}
		link_fully_up = link_working_working(link);
		spin_unlock_bh(&n_ptr->lock);
		if ((type == DSC_RESP_MSG) || link_fully_up)
			return;
		rbuf = tipc_disc_init_msg(DSC_RESP_MSG, 1, orig, b_ptr);
		if (rbuf != NULL) {
			b_ptr->media->send_msg(rbuf, &b_ptr->publ, &media_addr);
			buf_discard(rbuf);
		}
	}
}

/**
 * tipc_disc_stop_link_req - stop sending periodic link setup requests
 * @req: ptr to link request structure
 */

void tipc_disc_stop_link_req(struct link_req *req)
{
	if (!req)
		return;

	k_cancel_timer(&req->timer);
	k_term_timer(&req->timer);
	buf_discard(req->buf);
	kfree(req);
}

/**
 * tipc_disc_update_link_req - update frequency of periodic link setup requests
 * @req: ptr to link request structure
 */

void tipc_disc_update_link_req(struct link_req *req)
{
	if (!req)
		return;

	if (req->timer_intv == TIPC_LINK_REQ_SLOW) {
		if (!req->bearer->nodes.count) {
			req->timer_intv = TIPC_LINK_REQ_FAST;
			k_start_timer(&req->timer, req->timer_intv);
		}
	} else if (req->timer_intv == TIPC_LINK_REQ_FAST) {
		if (req->bearer->nodes.count) {
			req->timer_intv = TIPC_LINK_REQ_SLOW;
			k_start_timer(&req->timer, req->timer_intv);
		}
	} else {
		/* leave timer "as is" if haven't yet reached a "normal" rate */
	}
}

/**
 * disc_timeout - send a periodic link setup request
 * @req: ptr to link request structure
 *
 * Called whenever a link setup request timer associated with a bearer expires.
 */

static void disc_timeout(struct link_req *req)
{
	spin_lock_bh(&req->bearer->publ.lock);

	req->bearer->media->send_msg(req->buf, &req->bearer->publ, &req->dest);

	if ((req->timer_intv == TIPC_LINK_REQ_SLOW) ||
	    (req->timer_intv == TIPC_LINK_REQ_FAST)) {
		/* leave timer interval "as is" if already at a "normal" rate */
	} else {
		req->timer_intv *= 2;
		if (req->timer_intv > TIPC_LINK_REQ_FAST)
			req->timer_intv = TIPC_LINK_REQ_FAST;
		if ((req->timer_intv == TIPC_LINK_REQ_FAST) &&
		    (req->bearer->nodes.count))
			req->timer_intv = TIPC_LINK_REQ_SLOW;
	}
	k_start_timer(&req->timer, req->timer_intv);

	spin_unlock_bh(&req->bearer->publ.lock);
}

/**
 * tipc_disc_init_link_req - start sending periodic link setup requests
 * @b_ptr: ptr to bearer issuing requests
 * @dest: destination address for request messages
 * @dest_domain: network domain of node(s) which should respond to message
 * @req_links: max number of desired links
 *
 * Returns pointer to link request structure, or NULL if unable to create.
 */

struct link_req *tipc_disc_init_link_req(struct bearer *b_ptr,
					 const struct tipc_media_addr *dest,
					 u32 dest_domain,
					 u32 req_links)
{
	struct link_req *req;

	req = kmalloc(sizeof(*req), GFP_ATOMIC);
	if (!req)
		return NULL;

	req->buf = tipc_disc_init_msg(DSC_REQ_MSG, req_links, dest_domain, b_ptr);
	if (!req->buf) {
		kfree(req);
		return NULL;
	}

	memcpy(&req->dest, dest, sizeof(*dest));
	req->bearer = b_ptr;
	req->timer_intv = TIPC_LINK_REQ_INIT;
	k_init_timer(&req->timer, (Handler)disc_timeout, (unsigned long)req);
	k_start_timer(&req->timer, req->timer_intv);
	return req;
}

