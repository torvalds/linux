/*
 * net/tipc/udp_media.h: Include file for UDP bearer media
 *
 * Copyright (c) 1996-2006, 2013-2016, Ericsson AB
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

#ifdef CONFIG_TIPC_MEDIA_UDP
#ifndef _TIPC_UDP_MEDIA_H
#define _TIPC_UDP_MEDIA_H

#include <linux/ip.h>
#include <linux/udp.h>

int tipc_udp_nl_bearer_add(struct tipc_bearer *b, struct nlattr *attr);
int tipc_udp_nl_add_bearer_data(struct tipc_nl_msg *msg, struct tipc_bearer *b);
int tipc_udp_nl_dump_remoteip(struct sk_buff *skb, struct netlink_callback *cb);

/* check if configured MTU is too low for tipc headers */
static inline bool tipc_udp_mtu_bad(u32 mtu)
{
	if (mtu >= (TIPC_MIN_BEARER_MTU + sizeof(struct iphdr) +
	    sizeof(struct udphdr)))
		return false;

	pr_warn("MTU too low for tipc bearer\n");
	return true;
}

#endif
#endif
