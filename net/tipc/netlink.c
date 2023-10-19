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
	[TIPC_NLA_NET_ADDR_LEGACY]	= { .type = NLA_FLAG }
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
	[TIPC_NLA_NODE_UP]		= { .type = NLA_FLAG },
	[TIPC_NLA_NODE_ID]		= { .type = NLA_BINARY,
					    .len = TIPC_NODEID_LEN},
	[TIPC_NLA_NODE_KEY]		= { .type = NLA_BINARY,
					    .len = TIPC_AEAD_KEY_SIZE_MAX},
	[TIPC_NLA_NODE_KEY_MASTER]	= { .type = NLA_FLAG },
	[TIPC_NLA_NODE_REKEYING]	= { .type = NLA_U32 },
};

/* Properties valid for media, bearer and link */
const struct nla_policy tipc_nl_prop_policy[TIPC_NLA_PROP_MAX + 1] = {
	[TIPC_NLA_PROP_UNSPEC]		= { .type = NLA_UNSPEC },
	[TIPC_NLA_PROP_PRIO]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_TOL]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_WIN]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_MTU]		= { .type = NLA_U32 },
	[TIPC_NLA_PROP_BROADCAST]	= { .type = NLA_U32 },
	[TIPC_NLA_PROP_BROADCAST_RATIO]	= { .type = NLA_U32 }
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
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_bearer_disable,
	},
	{
		.cmd	= TIPC_NL_BEARER_ENABLE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_bearer_enable,
	},
	{
		.cmd	= TIPC_NL_BEARER_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_bearer_get,
		.dumpit	= tipc_nl_bearer_dump,
	},
	{
		.cmd	= TIPC_NL_BEARER_ADD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_bearer_add,
	},
	{
		.cmd	= TIPC_NL_BEARER_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_bearer_set,
	},
	{
		.cmd	= TIPC_NL_SOCK_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.start = tipc_dump_start,
		.dumpit	= tipc_nl_sk_dump,
		.done	= tipc_dump_done,
	},
	{
		.cmd	= TIPC_NL_PUBL_GET,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit	= tipc_nl_publ_dump,
	},
	{
		.cmd	= TIPC_NL_LINK_GET,
		.validate = GENL_DONT_VALIDATE_STRICT,
		.doit   = tipc_nl_node_get_link,
		.dumpit	= tipc_nl_node_dump_link,
	},
	{
		.cmd	= TIPC_NL_LINK_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_node_set_link,
	},
	{
		.cmd	= TIPC_NL_LINK_RESET_STATS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit   = tipc_nl_node_reset_link_stats,
	},
	{
		.cmd	= TIPC_NL_MEDIA_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_media_get,
		.dumpit	= tipc_nl_media_dump,
	},
	{
		.cmd	= TIPC_NL_MEDIA_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_media_set,
	},
	{
		.cmd	= TIPC_NL_NODE_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit	= tipc_nl_node_dump,
	},
	{
		.cmd	= TIPC_NL_NET_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit	= tipc_nl_net_dump,
	},
	{
		.cmd	= TIPC_NL_NET_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_net_set,
	},
	{
		.cmd	= TIPC_NL_NAME_TABLE_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit	= tipc_nl_name_table_dump,
	},
	{
		.cmd	= TIPC_NL_MON_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_node_set_monitor,
	},
	{
		.cmd	= TIPC_NL_MON_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_node_get_monitor,
		.dumpit	= tipc_nl_node_dump_monitor,
	},
	{
		.cmd	= TIPC_NL_MON_PEER_GET,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit	= tipc_nl_node_dump_monitor_peer,
	},
	{
		.cmd	= TIPC_NL_PEER_REMOVE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_peer_rm,
	},
#ifdef CONFIG_TIPC_MEDIA_UDP
	{
		.cmd	= TIPC_NL_UDP_GET_REMOTEIP,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit	= tipc_udp_nl_dump_remoteip,
	},
#endif
#ifdef CONFIG_TIPC_CRYPTO
	{
		.cmd	= TIPC_NL_KEY_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_node_set_key,
	},
	{
		.cmd	= TIPC_NL_KEY_FLUSH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_node_flush_key,
	},
#endif
	{
		.cmd	= TIPC_NL_ADDR_LEGACY_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= tipc_nl_net_addr_legacy_get,
	},
};

struct genl_family tipc_genl_family __ro_after_init = {
	.name		= TIPC_GENL_V2_NAME,
	.version	= TIPC_GENL_V2_VERSION,
	.hdrsize	= 0,
	.maxattr	= TIPC_NLA_MAX,
	.policy		= tipc_nl_policy,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.ops		= tipc_genl_v2_ops,
	.n_ops		= ARRAY_SIZE(tipc_genl_v2_ops),
	.resv_start_op	= TIPC_NL_ADDR_LEGACY_GET + 1,
};

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
