/*
 * net/tipc/netlink.h: Include file for TIPC netlink code
 *
 * Copyright (c) 2014, Ericsson AB
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

#ifndef _TIPC_NETLINK_H
#define _TIPC_NETLINK_H
#include <net/netlink.h>

extern struct genl_family tipc_genl_family;
int tipc_nlmsg_parse(const struct nlmsghdr *nlh, struct nlattr ***buf);

struct tipc_nl_msg {
	struct sk_buff *skb;
	u32 portid;
	u32 seq;
};

extern const struct nla_policy tipc_nl_name_table_policy[];
extern const struct nla_policy tipc_nl_sock_policy[];
extern const struct nla_policy tipc_nl_net_policy[];
extern const struct nla_policy tipc_nl_link_policy[];
extern const struct nla_policy tipc_nl_node_policy[];
extern const struct nla_policy tipc_nl_prop_policy[];
extern const struct nla_policy tipc_nl_bearer_policy[];
extern const struct nla_policy tipc_nl_media_policy[];
extern const struct nla_policy tipc_nl_udp_policy[];
extern const struct nla_policy tipc_nl_monitor_policy[];

int tipc_netlink_start(void);
int tipc_netlink_compat_start(void);
void tipc_netlink_stop(void);
void tipc_netlink_compat_stop(void);

#endif
