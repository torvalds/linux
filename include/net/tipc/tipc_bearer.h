/*
 * include/net/tipc/tipc_bearer.h: Include file for privileged access to TIPC bearers
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

#ifndef _NET_TIPC_BEARER_H_
#define _NET_TIPC_BEARER_H_

#ifdef __KERNEL__

#include <linux/tipc_config.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

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
	__u32  type;			/* bearer type (network byte order) */
	union {
		__u8   eth_addr[6];	/* 48 bit Ethernet addr (byte array) */ 
#if 0
		/* Prototypes for other possible bearer types */

		struct {
			__u16 sin_family;
			__u16 sin_port;
			struct {
				__u32 s_addr;
			} sin_addr;
			char pad[4];
		} addr_in;		/* IP-based bearer */
		__u16  sock_descr;	/* generic socket bearer */
#endif
	} dev_addr;
};

/**
 * struct tipc_bearer - TIPC bearer info available to privileged users
 * @usr_handle: pointer to additional user-defined information about bearer
 * @mtu: max packet size bearer can support
 * @blocked: non-zero if bearer is blocked
 * @lock: spinlock for controlling access to bearer
 * @addr: media-specific address associated with bearer
 * @name: bearer name (format = media:interface)
 * 
 * Note: TIPC initializes "name" and "lock" fields; user is responsible for
 * initialization all other fields when a bearer is enabled.
 */

struct tipc_bearer {
	void *usr_handle;
	u32 mtu;
	int blocked;
	spinlock_t lock;
	struct tipc_media_addr addr;
	char name[TIPC_MAX_BEARER_NAME];
};


int  tipc_register_media(u32 media_type,
			 char *media_name, 
			 int (*enable)(struct tipc_bearer *), 
			 void (*disable)(struct tipc_bearer *), 
			 int (*send_msg)(struct sk_buff *, 
					 struct tipc_bearer *,
					 struct tipc_media_addr *), 
			 char *(*addr2str)(struct tipc_media_addr *a,
					   char *str_buf,
					   int str_size),
			 struct tipc_media_addr *bcast_addr,
			 const u32 bearer_priority,
			 const u32 link_tolerance,  /* [ms] */
			 const u32 send_window_limit); 

void tipc_recv_msg(struct sk_buff *buf, struct tipc_bearer *tb_ptr);

int  tipc_block_bearer(const char *name);
void tipc_continue(struct tipc_bearer *tb_ptr); 

int tipc_enable_bearer(const char *bearer_name, u32 bcast_scope, u32 priority);
int tipc_disable_bearer(const char *name);


#endif

#endif
