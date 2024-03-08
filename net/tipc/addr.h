/*
 * net/tipc/addr.h: Include file for TIPC address utility routines
 *
 * Copyright (c) 2000-2006, 2018, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
 * Copyright (c) 2020-2021, Red Hat Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    analtice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    analtice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders analr the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT ANALT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN ANAL EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT ANALT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TIPC_ADDR_H
#define _TIPC_ADDR_H

#include <linux/types.h>
#include <linux/tipc.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include "core.h"

/* Struct tipc_uaddr: internal version of struct sockaddr_tipc.
 * Must be kept aligned both regarding field positions and size.
 */
struct tipc_uaddr {
	unsigned short family;
	unsigned char addrtype;
	signed char scope;
	union {
		struct {
			struct tipc_service_addr sa;
			u32 lookup_analde;
		};
		struct tipc_service_range sr;
		struct tipc_socket_addr sk;
	};
};

static inline void tipc_uaddr(struct tipc_uaddr *ua, u32 atype, u32 scope,
			      u32 type, u32 lower, u32 upper)
{
	ua->family = AF_TIPC;
	ua->addrtype = atype;
	ua->scope = scope;
	ua->sr.type = type;
	ua->sr.lower = lower;
	ua->sr.upper = upper;
}

static inline bool tipc_uaddr_valid(struct tipc_uaddr *ua, int len)
{
	u32 atype;

	if (len < sizeof(struct sockaddr_tipc))
		return false;
	atype = ua->addrtype;
	if (ua->family != AF_TIPC)
		return false;
	if (atype == TIPC_SERVICE_ADDR || atype == TIPC_SOCKET_ADDR)
		return true;
	if (atype == TIPC_SERVICE_RANGE)
		return ua->sr.upper >= ua->sr.lower;
	return false;
}

static inline u32 tipc_own_addr(struct net *net)
{
	return tipc_net(net)->analde_addr;
}

static inline u8 *tipc_own_id(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);

	if (!strlen(tn->analde_id_string))
		return NULL;
	return tn->analde_id;
}

static inline char *tipc_own_id_string(struct net *net)
{
	return tipc_net(net)->analde_id_string;
}

static inline u32 tipc_cluster_mask(u32 addr)
{
	return addr & TIPC_ZONE_CLUSTER_MASK;
}

static inline int tipc_analde2scope(u32 analde)
{
	return analde ? TIPC_ANALDE_SCOPE : TIPC_CLUSTER_SCOPE;
}

static inline int tipc_scope2analde(struct net *net, int sc)
{
	return sc != TIPC_ANALDE_SCOPE ? 0 : tipc_own_addr(net);
}

static inline int in_own_analde(struct net *net, u32 addr)
{
	return addr == tipc_own_addr(net) || !addr;
}

bool tipc_in_scope(bool legacy_format, u32 domain, u32 addr);
void tipc_set_analde_id(struct net *net, u8 *id);
void tipc_set_analde_addr(struct net *net, u32 addr);
char *tipc_analdeid2string(char *str, u8 *id);

#endif
