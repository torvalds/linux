/*
 * net/tipc/bearer.h: Include file for TIPC bearer code
 *
 * Copyright (c) 1996-2006, 2013, Ericsson AB
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
 * Identifiers associated with TIPC message header media address info
 *
 * - address info field is 20 bytes long
 * - media type identifier located at offset 3
 * - remaining bytes vary according to media type
 */
#define TIPC_MEDIA_ADDR_SIZE	20
#define TIPC_MEDIA_TYPE_OFFSET	3

/*
 * Identifiers of supported TIPC media types
 */
#define TIPC_MEDIA_TYPE_ETH	1
#define TIPC_MEDIA_TYPE_IB	2

/**
 * struct tipc_media_addr - destination address used by TIPC bearers
 * @value: address info (format defined by media)
 * @media_id: TIPC media type identifier
 * @broadcast: non-zero if address is a broadcast address
 */
struct tipc_media_addr {
	u8 value[TIPC_MEDIA_ADDR_SIZE];
	u8 media_id;
	u8 broadcast;
};

struct tipc_bearer;

/**
 * struct tipc_media - Media specific info exposed to generic bearer layer
 * @send_msg: routine which handles buffer transmission
 * @enable_media: routine which enables a media
 * @disable_media: routine which disables a media
 * @addr2str: routine which converts media address to string
 * @addr2msg: routine which converts media address to protocol message area
 * @msg2addr: routine which converts media address from protocol message area
 * @priority: default link (and bearer) priority
 * @tolerance: default time (in ms) before declaring link failure
 * @window: default window (in packets) before declaring link congestion
 * @type_id: TIPC media identifier
 * @hwaddr_len: TIPC media address len
 * @name: media name
 */
struct tipc_media {
	int (*send_msg)(struct sk_buff *buf,
			struct tipc_bearer *b_ptr,
			struct tipc_media_addr *dest);
	int (*enable_media)(struct tipc_bearer *b_ptr);
	void (*disable_media)(struct tipc_bearer *b_ptr);
	int (*addr2str)(struct tipc_media_addr *a, char *str_buf, int str_size);
	int (*addr2msg)(struct tipc_media_addr *a, char *msg_area);
	int (*msg2addr)(const struct tipc_bearer *b_ptr,
			struct tipc_media_addr *a, char *msg_area);
	u32 priority;
	u32 tolerance;
	u32 window;
	u32 type_id;
	u32 hwaddr_len;
	char name[TIPC_MAX_MEDIA_NAME];
};

/**
 * struct tipc_bearer - Generic TIPC bearer structure
 * @dev: ptr to associated network device
 * @usr_handle: pointer to additional media-specific information about bearer
 * @mtu: max packet size bearer can support
 * @lock: spinlock for controlling access to bearer
 * @addr: media-specific address associated with bearer
 * @name: bearer name (format = media:interface)
 * @media: ptr to media structure associated with bearer
 * @bcast_addr: media address used in broadcasting
 * @priority: default link priority for bearer
 * @window: default window size for bearer
 * @tolerance: default link tolerance for bearer
 * @identity: array index of this bearer within TIPC bearer array
 * @link_req: ptr to (optional) structure making periodic link setup requests
 * @links: list of non-congested links associated with bearer
 * @active: non-zero if bearer structure is represents a bearer
 * @net_plane: network plane ('A' through 'H') currently associated with bearer
 * @nodes: indicates which nodes in cluster can be reached through bearer
 *
 * Note: media-specific code is responsible for initialization of the fields
 * indicated below when a bearer is enabled; TIPC's generic bearer code takes
 * care of initializing all other fields.
 */
struct tipc_bearer {
	void *media_ptr;			/* initalized by media */
	u32 mtu;				/* initalized by media */
	struct tipc_media_addr addr;		/* initalized by media */
	char name[TIPC_MAX_BEARER_NAME];
	spinlock_t lock;
	struct tipc_media *media;
	struct tipc_media_addr bcast_addr;
	u32 priority;
	u32 window;
	u32 tolerance;
	u32 identity;
	struct tipc_link_req *link_req;
	struct list_head links;
	int active;
	char net_plane;
	struct tipc_node_map nodes;
};

struct tipc_bearer_names {
	char media_name[TIPC_MAX_MEDIA_NAME];
	char if_name[TIPC_MAX_IF_NAME];
};

struct tipc_link;

extern struct tipc_bearer tipc_bearers[];

/*
 * TIPC routines available to supported media types
 */

void tipc_rcv(struct sk_buff *buf, struct tipc_bearer *tb_ptr);
int tipc_enable_bearer(const char *bearer_name, u32 disc_domain, u32 priority);
int tipc_disable_bearer(const char *name);

/*
 * Routines made available to TIPC by supported media types
 */
extern struct tipc_media eth_media_info;

#ifdef CONFIG_TIPC_MEDIA_IB
extern struct tipc_media ib_media_info;
#endif

int tipc_media_set_priority(const char *name, u32 new_value);
int tipc_media_set_window(const char *name, u32 new_value);
void tipc_media_addr_printf(char *buf, int len, struct tipc_media_addr *a);
struct sk_buff *tipc_media_get_names(void);
void tipc_l2_media_addr_set(const struct tipc_bearer *b,
			    struct tipc_media_addr *a, char *mac);
int tipc_enable_l2_media(struct tipc_bearer *b);
void tipc_disable_l2_media(struct tipc_bearer *b);
int tipc_l2_send_msg(struct sk_buff *buf, struct tipc_bearer *b,
		     struct tipc_media_addr *dest);

struct sk_buff *tipc_bearer_get_names(void);
void tipc_bearer_add_dest(struct tipc_bearer *b_ptr, u32 dest);
void tipc_bearer_remove_dest(struct tipc_bearer *b_ptr, u32 dest);
struct tipc_bearer *tipc_bearer_find(const char *name);
struct tipc_media *tipc_media_find(const char *name);
int tipc_bearer_setup(void);
void tipc_bearer_cleanup(void);
void tipc_bearer_stop(void);
void tipc_bearer_send(struct tipc_bearer *b, struct sk_buff *buf,
		      struct tipc_media_addr *dest);

#endif	/* _TIPC_BEARER_H */
