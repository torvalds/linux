/*
 * net/tipc/diag.c: TIPC socket diag
 *
 * Copyright (c) 2018, Ericsson AB
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "ASIS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
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
#include <linux/sock_diag.h>
#include <linux/tipc_sockets_diag.h>

static u64 __tipc_diag_gen_cookie(struct sock *sk)
{
	u32 res[2];

	sock_diag_save_cookie(sk, res);
	return *((u64 *)res);
}

static int __tipc_add_sock_diag(struct sk_buff *skb,
				struct netlink_callback *cb,
				struct tipc_sock *tsk)
{
	struct tipc_sock_diag_req *req = nlmsg_data(cb->nlh);
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put_answer(skb, cb, SOCK_DIAG_BY_FAMILY, 0,
			       NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	err = tipc_sk_fill_sock_diag(skb, cb, tsk, req->tidiag_states,
				     __tipc_diag_gen_cookie);
	if (err)
		return err;

	nlmsg_end(skb, nlh);
	return 0;
}

static int tipc_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	return tipc_nl_sk_walk(skb, cb, __tipc_add_sock_diag);
}

static int tipc_sock_diag_handler_dump(struct sk_buff *skb,
				       struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct tipc_sock_diag_req);
	struct net *net = sock_net(skb->sk);

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	if (h->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = tipc_diag_dump,
		};
		netlink_dump_start(net->diag_nlsk, skb, h, &c);
		return 0;
	}
	return -EOPNOTSUPP;
}

static const struct sock_diag_handler tipc_sock_diag_handler = {
	.family = AF_TIPC,
	.dump = tipc_sock_diag_handler_dump,
};

static int __init tipc_diag_init(void)
{
	return sock_diag_register(&tipc_sock_diag_handler);
}

static void __exit tipc_diag_exit(void)
{
	sock_diag_unregister(&tipc_sock_diag_handler);
}

module_init(tipc_diag_init);
module_exit(tipc_diag_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, AF_TIPC);
