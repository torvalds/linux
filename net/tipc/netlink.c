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
	[TIPC_NLA_NAME_TABLE]	= { .type = NLA_NESTED, }
};

/* Users of the legacy API (tipc-config) can't handle that we add operations,
 * so we have a separate genl handling for the new API.
 */
struct genl_family tipc_genl_family = {
	.id		= GENL_ID_GENERATE,
	.name		= TIPC_GENL_V2_NAME,
	.version	= TIPC_GENL_V2_VERSION,
	.hdrsize	= 0,
	.maxattr	= TIPC_NLA_MAX,
	.netnsok	= true,
};

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
		.cmd	= TIPC_NL_BEARER_SET,
		.doit	= tipc_nl_bearer_set,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_SOCK_GET,
		.dumpit	= tipc_nl_sk_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_PUBL_GET,
		.dumpit	= tipc_nl_publ_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_LINK_GET,
		.doit   = tipc_nl_link_get,
		.dumpit	= tipc_nl_link_dump,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_LINK_SET,
		.doit	= tipc_nl_link_set,
		.policy = tipc_nl_policy,
	},
	{
		.cmd	= TIPC_NL_LINK_RESET_STATS,
		.doit   = tipc_nl_link_reset_stats,
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
	}
};

int tipc_nlmsg_parse(const struct nlmsghdr *nlh, struct nlattr ***attr)
{
	u32 maxattr = tipc_genl_family.maxattr;

	*attr = tipc_genl_family.attrbuf;
	if (!*attr)
		return -EOPNOTSUPP;

	return nlmsg_parse(nlh, GENL_HDRLEN, *attr, maxattr, tipc_nl_policy);
}

int tipc_netlink_start(void)
{
	int res;

	res = genl_register_family_with_ops(&tipc_genl_family,
					    tipc_genl_v2_ops);
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
