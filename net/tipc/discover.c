/*
 * net/tipc/discover.c
 * 
 * Copyright (c) 2003-2006, Ericsson AB
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
#include "dbg.h"
#include "link.h"
#include "zone.h"
#include "discover.h"
#include "port.h"
#include "name_table.h"

#define TIPC_LINK_REQ_INIT	125	/* min delay during bearer start up */
#define TIPC_LINK_REQ_FAST	2000	/* normal delay if bearer has no links */
#define TIPC_LINK_REQ_SLOW	600000	/* normal delay if bearer has links */

#if 0
#define  GET_NODE_INFO         300
#define  GET_NODE_INFO_RESULT  301
#define  FORWARD_LINK_PROBE    302
#define  LINK_REQUEST_REJECTED 303
#define  LINK_REQUEST_ACCEPTED 304
#define  DROP_LINK_REQUEST     305
#define  CHECK_LINK_COUNT      306
#endif

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


#if 0
int disc_create_link(const struct tipc_link_create *argv) 
{
	/* 
	 * Code for inter cluster link setup here 
	 */
	return TIPC_OK;
}
#endif

/*
 * disc_lost_link(): A link has lost contact
 */

void tipc_disc_link_event(u32 addr, char *name, int up) 
{
	if (in_own_cluster(addr))
		return;
	/* 
	 * Code for inter cluster link setup here 
	 */
}

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
	struct sk_buff *buf = buf_acquire(DSC_H_SIZE);
	struct tipc_msg *msg;

	if (buf) {
		msg = buf_msg(buf);
		msg_init(msg, LINK_CONFIG, type, TIPC_OK, DSC_H_SIZE,
			 dest_domain);
		msg_set_non_seq(msg);
		msg_set_req_links(msg, req_links);
		msg_set_dest_domain(msg, dest_domain);
		msg_set_bc_netid(msg, tipc_net_id);
		msg_set_media_addr(msg, &b_ptr->publ.addr);
	}
	return buf;
}

/**
 * tipc_disc_recv_msg - handle incoming link setup message (request or response)
 * @buf: buffer containing message
 */

void tipc_disc_recv_msg(struct sk_buff *buf)
{
	struct bearer *b_ptr = (struct bearer *)TIPC_SKB_CB(buf)->handle;
	struct link *link;
	struct tipc_media_addr media_addr;
	struct tipc_msg *msg = buf_msg(buf);
	u32 dest = msg_dest_domain(msg);
	u32 orig = msg_prevnode(msg);
	u32 net_id = msg_bc_netid(msg);
	u32 type = msg_type(msg);

	msg_get_media_addr(msg,&media_addr);
	msg_dbg(msg, "RECV:");
	buf_discard(buf);

	if (net_id != tipc_net_id)
		return;
	if (!tipc_addr_domain_valid(dest))
		return;
	if (!tipc_addr_node_valid(orig))
		return;
	if (orig == tipc_own_addr)
		return;
	if (!in_scope(dest, tipc_own_addr))
		return;
	if (is_slave(tipc_own_addr) && is_slave(orig))
		return;
	if (is_slave(orig) && !in_own_cluster(orig))
		return;
	if (in_own_cluster(orig)) {
		/* Always accept link here */
		struct sk_buff *rbuf;
		struct tipc_media_addr *addr;
		struct node *n_ptr = tipc_node_find(orig);
		int link_up;
		dbg(" in own cluster\n");
		if (n_ptr == NULL) {
			n_ptr = tipc_node_create(orig);
		}
		if (n_ptr == NULL) {
			warn("Memory squeeze; Failed to create node\n");
			return;
		}
		spin_lock_bh(&n_ptr->lock);
		link = n_ptr->links[b_ptr->identity];
		if (!link) {
			dbg("creating link\n");
			link = tipc_link_create(b_ptr, orig, &media_addr);
			if (!link) {
				spin_unlock_bh(&n_ptr->lock);                
				return;
			}
		}
		addr = &link->media_addr;
		if (memcmp(addr, &media_addr, sizeof(*addr))) {
			char addr_string[16];

			warn("New bearer address for %s\n", 
			     addr_string_fill(addr_string, orig));
			memcpy(addr, &media_addr, sizeof(*addr));
			tipc_link_reset(link);     
		}
		link_up = tipc_link_is_up(link);
		spin_unlock_bh(&n_ptr->lock);                
		if ((type == DSC_RESP_MSG) || link_up)
			return;
		rbuf = tipc_disc_init_msg(DSC_RESP_MSG, 1, orig, b_ptr);
		if (rbuf != NULL) {
			msg_dbg(buf_msg(rbuf),"SEND:");
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
		if (req->timer_intv > TIPC_LINK_REQ_SLOW)
			req->timer_intv = TIPC_LINK_REQ_SLOW;
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

	req = (struct link_req *)kmalloc(sizeof(*req), GFP_ATOMIC);
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

