/*
 * net/tipc/netlink.c: TIPC configuration handling
 *
 * Copyright (c) 2005-2006, 2014, Ericsson AB
 * Copyright (c) 2005-2007, Wind River Systems
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
#include "socket.h"
#include "name_table.h"
#include "bearer.h"
#include "link.h"
#include "node.h"
#include "net.h"
#include "udp_media.h"
#include <net/genetlink.h>

static const struct nla_policy tipc_nl_policy[TIPC_NLA_MAX + 1] = {
	[TIPC_NLA_UNSPEC]	= { .type = NLA_UNSPEC, },
	[TIPC_NLA_BEARER]	= { .type = NLA_NESTED, },
	[TIPC_NLA_SOCK]		= { .type = NLA_NESTED, },
	[TIPC_NLA_PUBL]		= { .type = NLA_NESTED, },
	[TIPC_NLA_LINK]		= { .type = NLA_NESTED, },
	[TIPC_NLA_MEDIA]	= { .type = NLA_NESTED, },
	[TIPC_NLA_NODE]		= { .type = NLA_NESTED, },
	[TIPC_NLA_NET]		= { .type = NLA_NESTED, },
	[TIPC_NLA_NAME_TABLE]	= { .type = NLA_NESTED, },
	[TIPC_NLA_MON]		= { .type = NLA_NESTED, },
};

const struct nla_policy
tipc_nl_name_table_policy[TIPC_NLA_NAME_TABLE_MAX + 1] = {
	[TIPC_NLA_NAME_TABLE_UNSPEC]	= { .type = NLA_UNSPEC },
	[TIPC_NLA_NAME_TABLE_PUBL]	= { .type = NLA_NESTED }
};

const struct nla_policy tipc_nl_monitor_policy[TIPC_NLA_MON_MAX + 1] = {
	[TIPC_NLA_MON_UNSPEC]			= { .type = NLA_UNSPEC },
	[TIPC_NLA_MON_REF]			= { .type = NLA_U32 },
	[TIPC_NLA_MON_ACTIVATION_THRESHOLD]	= { .type = NLA_U32 },
};

const struct nla_policy tipc_nl_sock_policy[TIPC_NLA_SOCK_MAX + 1] = {
	[TIPC_NLA_SOCK_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_SOCK_ADDR]		= { .type = NLA_U32 },
	[TIPC_NLA_SOCK_REF]		= { .type = NLA_U32 },
	[TIPC_NLA_SOCK_CON]		= { .type = NLA_NESTED },
	[TIPC_NLA_SOCK_HAS_PUBL]	= { .type = NLA_FLAG }
};

const struct nla_policy tipc_nl_net_policy[TIPC_NLA_NET_MAX + 1] = {
	[TIPC_NLA_NET_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_NET_ID]		= { .type = NLA_U32 },
	[TIPC_NLA_NET_ADDR]		= { .type = NLA_U32 },
	[TIPC_NLA_NET_NODEID]		= { .type = NLA_U64 },
	[TIPC_NLA_NET_NODEID_W1]	= { .type = NLA_U64 },
};

const struct nla_policy tipc_nl_link_policy[TIPC_NLA_LINK_MAX + 1] = {
	[TIPC_NLA_LINK_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_LINK_NAME]		= { .type = NLA_STRING,
					    .len = TIPC_MAX_LINK_NAME },
	[TIPC_NLA_LINK_MTU]		= { .type = NLA_U32 },
	[TIPC_NLA_LINK_BROADCAST]	= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_UP]		= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_ACTIVE]		= { .type = NLA_FLAG },
	[TIPC_NLA_LINK_PROP]		= { .type = NLA_NESTED },
	[TIPC_NLA_LINK_STATS]		= { .type = NLA_NESTED },
	[TIPC_NLA_LINK_RX]		= { .type = NLA_U32 },
	[TIPC_NLA_LINK_TX]		= { .type = NLA_U32 }
};

const struct nla_policy tipc_nl_node_policy[TIPC_NLA_NODE_MAX + 1] = {
	[TIPC_NLA_NODE_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_NODE_ADDR]		= { .type = NLA_U32 },
	[TIPC_NLA_NODE_UP]		= { .type = NLA_FLAG }
};

/* Properties valid for media, bearer and link */
const struct nla_policy tipc_nl_prop_policy[TIPC_NLA_PROP_MAX + 1] = {
	[TIPC_NLA_PROP_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_PROP_PRIO]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_TOL]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_WIN]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_MTU]		= { .type = NLA_U32 }
};

const struct nla_policy tipc_nl_bearer_policy[TIPC_NLA_BEARER_MAX + 1]	= {
	[TIPC_NLA_BEARER_UNSPEC]	= { .type = NLA_UNSPEC },
	[TIPC_NLA_BEARER_NAME]		= { .type = NLA_STRING,
					    .len = TIPC_MAX_BEARER_NAME },
	[TIPC_NLA_BEARER_PROP]		= { .type = NLA_NESTED },
	[TIPC_NLA_BEARER_DOMAIN]	= { .type = NLA_U32 }
};

const struct nla_policy tipc_nl_media_policy[TIPC_NLA_MEDIA_MAX + 1] = {
	[TIPC_NLA_MEDIA_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_MEDIA_NAME]		= { .type = NLA_STRING },
	[TIPC_NLA_MEDIA_PROP]		= { .type = NLA_NESTED }
};

const struct nla_policy tipc_nl_udp_policy[TIPC_NLA_UDP_MAX + 1] = {
	[TIPC_NLA_UDP_UNSPEC]	= {.type = NLA_UNSPEC},
	[TIPC_NLA_UDP_LOCAL]	= {.type = NLA_BINARY,
				   .len = sizeof(struct sockaddr_storage)},
	[TIPC_NLA_UDP_REMOTE]	= {.type = NLA_BINARY,
				   .len = sizeof(struct sockaddr_storage)},
};

/* Users of the legacy API (tipc-config) can't handle that we add operations,
 * so we have a separate genl handling for the new API.
 */
static const struct genl_ops tipc_genl_v2_ops[] = {
	{
		.cmd	= TIPC_NL_BEARER_DISABLE,
		.doit	= tipc_nl_bearer_disable,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_BEARER_ENABLE,
		.doit	= tipc_nl_bearer_enable,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_BEARER_GET,
		.doit	= tipc_nl_bearer_get,
		.dumpit	= tipc_nl_bearer_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_BEARER_ADD,
		.doit	= tipc_nl_bearer_add,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_BEARER_SET,
		.doit	= tipc_nl_bearer_set,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_SOCK_GET,
		.start = tipc_dump_start,
		.dumpit	= tipc_nl_sk_dump,
		.done	= tipc_dump_done,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_PUBL_GET,
		.dumpit	= tipc_nl_publ_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_LINK_GET,
		.doit   = tipc_nl_node_get_link,
		.dumpit	= tipc_nl_node_dump_link,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_LINK_SET,
		.doit	= tipc_nl_node_set_link,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_LINK_RESET_STATS,
		.doit   = tipc_nl_node_reset_link_stats,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_MEDIA_GET,
		.doit	= tipc_nl_media_get,
		.dumpit	= tipc_nl_media_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_MEDIA_SET,
		.doit	= tipc_nl_media_set,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_NODE_GET,
		.dumpit	= tipc_nl_node_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_NET_GET,
		.dumpit	= tipc_nl_net_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_NET_SET,
		.doit	= tipc_nl_net_set,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_NAME_TABLE_GET,
		.dumpit	= tipc_nl_name_table_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_MON_SET,
		.doit	= tipc_nl_node_set_monitor,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_MON_GET,
		.doit	= tipc_nl_node_get_monitor,
		.dumpit	= tipc_nl_node_dump_monitor,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_MON_PEER_GET,
		.dumpit	= tipc_nl_node_dump_monitor_peer,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_PEER_REMOVE,
		.doit	= tipc_nl_peer_rm,
		.policy = tipc_nl_policy,
	},
#ifdef CONFIG_TIPC_MEDIA_UDP
	{
		.cmd	= TIPC_NL_UDP_GET_REMOTEIP,
		.dumpit	= tipc_udp_nl_dump_remoteip,
		.policy = tipc_nl_policy,
	},
#endif
};

struct genl_family tipc_genl_family __ro_after_init = {
	.name		= TIPC_GENL_V2_NAME,
	.version	= TIPC_GENL_V2_VERSION,
	.hdrsize	= 0,
	.maxattr	= TIPC_NLA_MAX,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.ops		= tipc_genl_v2_ops,
	.n_ops		= ARRAY_SIZE(tipc_genl_v2_ops),
};

int tipc_nlmsg_parse(const struct nlmsghdr *nlh, struct nlattr ***attr)
{
	u32 maxattr = tipc_genl_family.maxattr;

	*attr = genl_family_attrbuf(&tipc_genl_family);
	if (!*attr)
		return -EOPNOTSUPP;

	return nlmsg_parse(nlh, GENL_HDRLEN, *attr, maxattr, tipc_nl_policy,
			   NULL);
}

int __init tipc_netlink_start(void)
{
	int res;

	res = genl_register_family(&tipc_genl_family);
	if (res) {
		pr_err("Failed to register netlink interface\n");
		return res;
	}
	return 0;
}

void tipc_netlink_stop(void)
{
	genl_unregister_family(&tipc_genl_family);
}
