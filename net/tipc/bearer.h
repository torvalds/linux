/*
 * net/tipc/bearer.h: Include file for TIPC bearer code
 *
 * Copyright (c) 1996-2006, Ericsson AB
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

#ifndef _TIPC_BEARER_H
#define _TIPC_BEARER_H

#include "bcast.h"

#define MAX_BEARERS	2
#define MAX_MEDIA	2

/*
 * Identifiers of supported TIPC media types
 */
#define TIPC_MEDIA_TYPE_ETH	1

/*
 * Destination address structure used by TIPC bearers when sending messages
 *
 * IMPORTANT: The fields of this structure MUST be stored using the specified
 * byte order indicated below, as the structure is exchanged between nodes
 * as part of a link setup process.
 */
struct tipc_media_addr {
	__be32  type;			/* bearer type (network byte order) */
	union {
		__u8   eth_addr[6];	/* 48 bit Ethernet addr (byte array) */
	} dev_addr;
};

struct tipc_bearer;

/**
 * struct media - TIPC media information available to internal users
 * @send_msg: routine which handles buffer transmission
 * @enable_bearer: routine which enables a bearer
 * @disable_bearer: routine which disables a bearer
 * @addr2str: routine which converts bearer's address to string form
 * @bcast_addr: media address used in broadcasting
 * @priority: default link (and bearer) priority
 * @tolerance: default time (in ms) before declaring link failure
 * @window: default window (in packets) before declaring link congestion
 * @type_id: TIPC media identifier
 * @name: media name
 */

struct media {
	int (*send_msg)(struct sk_buff *buf,
			struct tipc_bearer *b_ptr,
			struct tipc_media_addr *dest);
	int (*enable_bearer)(struct tipc_bearer *b_ptr);
	void (*disable_bearer)(struct tipc_bearer *b_ptr);
	char *(*addr2str)(struct tipc_media_addr *a,
			  char *str_buf, int str_size);
	struct tipc_media_addr bcast_addr;
	u32 priority;
	u32 tolerance;
	u32 window;
	u32 type_id;
	char name[TIPC_MAX_MEDIA_NAME];
};

/**
 * struct tipc_bearer - TIPC bearer structure
 * @usr_handle: pointer to additional media-specific information about bearer
 * @mtu: max packet size bearer can support
 * @blocked: non-zero if bearer is blocked
 * @lock: spinlock for controlling access to bearer
 * @addr: media-specific address associated with bearer
 * @name: bearer name (format = media:interface)
 * @media: ptr to media structure associated with bearer
 * @priority: default link priority for bearer
 * @identity: array index of this bearer within TIPC bearer array
 * @link_req: ptr to (optional) structure making periodic link setup requests
 * @links: list of non-congested links associated with bearer
 * @cong_links: list of congested links associated with bearer
 * @active: non-zero if bearer structure is represents a bearer
 * @net_plane: network plane ('A' through 'H') currently associated with bearer
 * @nodes: indicates which nodes in cluster can be reached through bearer
 *
 * Note: media-specific code is responsible for initialization of the fields
 * indicated below when a bearer is enabled; TIPC's generic bearer code takes
 * care of initializing all other fields.
 */
struct tipc_bearer {
	void *usr_handle;			/* initalized by media */
	u32 mtu;				/* initalized by media */
	int blocked;				/* initalized by media */
	struct tipc_media_addr addr;		/* initalized by media */
	char name[TIPC_MAX_BEARER_NAME];
	spinlock_t lock;
	struct media *media;
	u32 priority;
	u32 identity;
	struct link_req *link_req;
	struct list_head links;
	struct list_head cong_links;
	int active;
	char net_plane;
	struct tipc_node_map nodes;
};

struct bearer_name {
	char media_name[TIPC_MAX_MEDIA_NAME];
	char if_name[TIPC_MAX_IF_NAME];
};

struct link;

extern struct tipc_bearer tipc_bearers[];

/*
 * TIPC routines available to supported media types
 */
int tipc_register_media(struct media *m_ptr);

void tipc_recv_msg(struct sk_buff *buf, struct tipc_bearer *tb_ptr);

int  tipc_block_bearer(const char *name);
void tipc_continue(struct tipc_bearer *tb_ptr);

int tipc_enable_bearer(const char *bearer_name, u32 disc_domain, u32 priority);
int tipc_disable_bearer(const char *name);

/*
 * Routines made available to TIPC by supported media types
 */
int  tipc_eth_media_start(void);
void tipc_eth_media_stop(void);

void tipc_media_addr_printf(struct print_buf *pb, struct tipc_media_addr *a);
struct sk_buff *tipc_media_get_names(void);

struct sk_buff *tipc_bearer_get_names(void);
void tipc_bearer_add_dest(struct tipc_bearer *b_ptr, u32 dest);
void tipc_bearer_remove_dest(struct tipc_bearer *b_ptr, u32 dest);
void tipc_bearer_schedule(struct tipc_bearer *b_ptr, struct link *l_ptr);
struct tipc_bearer *tipc_bearer_find_interface(const char *if_name);
int tipc_bearer_resolve_congestion(struct tipc_bearer *b_ptr, struct link *l_ptr);
int tipc_bearer_congested(struct tipc_bearer *b_ptr, struct link *l_ptr);
void tipc_bearer_stop(void);
void tipc_bearer_lock_push(struct tipc_bearer *b_ptr);


/**
 * tipc_bearer_send- sends buffer to destination over bearer
 *
 * Returns true (1) if successful, or false (0) if unable to send
 *
 * IMPORTANT:
 * The media send routine must not alter the buffer being passed in
 * as it may be needed for later retransmission!
 *
 * If the media send routine returns a non-zero value (indicating that
 * it was unable to send the buffer), it must:
 *   1) mark the bearer as blocked,
 *   2) call tipc_continue() once the bearer is able to send again.
 * Media types that are unable to meet these two critera must ensure their
 * send routine always returns success -- even if the buffer was not sent --
 * and let TIPC's link code deal with the undelivered message.
 */

static inline int tipc_bearer_send(struct tipc_bearer *b_ptr,
				   struct sk_buff *buf,
				   struct tipc_media_addr *dest)
{
	return !b_ptr->media->send_msg(buf, b_ptr, dest);
}

#endif	/* _TIPC_BEARER_H */
