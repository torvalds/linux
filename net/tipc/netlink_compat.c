/*
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

#include "core.h"
#include "config.h"
#include <net/genetlink.h>
#include <linux/tipc_config.h>

static int handle_cmd(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct sk_buff *rep_buf;
	struct nlmsghdr *rep_nlh;
	struct nlmsghdr *req_nlh = info->nlhdr;
	struct tipc_genlmsghdr *req_userhdr = info->userhdr;
	int hdr_space = nlmsg_total_size(GENL_HDRLEN + TIPC_GENL_HDRLEN);
	u16 cmd;

	if ((req_userhdr->cmd & 0xC000) &&
	    (!netlink_net_capable(skb, CAP_NET_ADMIN)))
		cmd = TIPC_CMD_NOT_NET_ADMIN;
	else
		cmd = req_userhdr->cmd;

	rep_buf = tipc_cfg_do_cmd(net, req_userhdr->dest, cmd,
				  nlmsg_data(req_nlh) + GENL_HDRLEN +
				  TIPC_GENL_HDRLEN,
				  nlmsg_attrlen(req_nlh, GENL_HDRLEN +
				  TIPC_GENL_HDRLEN), hdr_space);

	if (rep_buf) {
		skb_push(rep_buf, hdr_space);
		rep_nlh = nlmsg_hdr(rep_buf);
		memcpy(rep_nlh, req_nlh, hdr_space);
		rep_nlh->nlmsg_len = rep_buf->len;
		genlmsg_unicast(net, rep_buf, NETLINK_CB(skb).portid);
	}

	return 0;
}

static struct genl_family tipc_genl_compat_family = {
	.id		= GENL_ID_GENERATE,
	.name		= TIPC_GENL_NAME,
	.version	= TIPC_GENL_VERSION,
	.hdrsize	= TIPC_GENL_HDRLEN,
	.maxattr	= 0,
	.netnsok	= true,
};

static struct genl_ops tipc_genl_compat_ops[] = {
	{
		.cmd		= TIPC_GENL_CMD,
		.doit		= handle_cmd,
	},
};

int tipc_netlink_compat_start(void)
{
	int res;

	res = genl_register_family_with_ops(&tipc_genl_compat_family,
					    tipc_genl_compat_ops);
	if (res) {
		pr_err("Failed to register legacy compat interface\n");
		return res;
	}

	return 0;
}

void tipc_netlink_compat_stop(void)
{
	genl_unregister_family(&tipc_genl_compat_family);
}
