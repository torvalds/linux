// SPDX-License-Identifier: GPL-2.0
/* MPTCP socket monitoring support
 *
 * Copyright (c) 2020 Red Hat
 *
 * Author: Paolo Abeni <pabeni@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/inet_diag.h>
#include <net/netlink.h>
#include "protocol.h"

static int sk_diag_dump(struct sock *sk, struct sk_buff *skb,
			struct netlink_callback *cb,
			const struct inet_diag_req_v2 *req,
			struct nlattr *bc, bool net_admin)
{
	if (!inet_diag_bc_sk(bc, sk))
		return 0;

	return inet_sk_diag_fill(sk, inet_csk(sk), skb, cb, req, NLM_F_MULTI,
				 net_admin);
}

static int mptcp_diag_dump_one(struct netlink_callback *cb,
			       const struct inet_diag_req_v2 *req)
{
	struct sk_buff *in_skb = cb->skb;
	struct mptcp_sock *msk = NULL;
	struct sk_buff *rep;
	int err = -ENOENT;
	struct net *net;
	struct sock *sk;

	net = sock_net(in_skb->sk);
	msk = mptcp_token_get_sock(net, req->id.idiag_cookie[0]);
	if (!msk)
		goto out_nosk;

	err = -ENOMEM;
	sk = (struct sock *)msk;
	rep = nlmsg_new(nla_total_size(sizeof(struct inet_diag_msg)) +
			inet_diag_msg_attrs_size() +
			nla_total_size(sizeof(struct mptcp_info)) +
			nla_total_size(sizeof(struct inet_diag_meminfo)) + 64,
			GFP_KERNEL);
	if (!rep)
		goto out;

	err = inet_sk_diag_fill(sk, inet_csk(sk), rep, cb, req, 0,
				netlink_net_capable(in_skb, CAP_NET_ADMIN));
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(rep);
		goto out;
	}
	err = nlmsg_unicast(net->diag_nlsk, rep, NETLINK_CB(in_skb).portid);

out:
	sock_put(sk);

out_nosk:
	return err;
}

struct mptcp_diag_ctx {
	long s_slot;
	long s_num;
	unsigned int l_slot;
	unsigned int l_num;
};

static void mptcp_diag_dump_listeners(struct sk_buff *skb, struct netlink_callback *cb,
				      const struct inet_diag_req_v2 *r,
				      bool net_admin)
{
	struct inet_diag_dump_data *cb_data = cb->data;
	struct mptcp_diag_ctx *diag_ctx = (void *)cb->ctx;
	struct nlattr *bc = cb_data->inet_diag_nla_bc;
	struct net *net = sock_net(skb->sk);
	struct inet_hashinfo *hinfo;
	int i;

	hinfo = net->ipv4.tcp_death_row.hashinfo;

	for (i = diag_ctx->l_slot; i <= hinfo->lhash2_mask; i++) {
		struct inet_listen_hashbucket *ilb;
		struct hlist_nulls_node *node;
		struct sock *sk;
		int num = 0;

		ilb = &hinfo->lhash2[i];

		rcu_read_lock();
		spin_lock(&ilb->lock);
		sk_nulls_for_each(sk, node, &ilb->nulls_head) {
			const struct mptcp_subflow_context *ctx = mptcp_subflow_ctx(sk);
			struct inet_sock *inet = inet_sk(sk);
			int ret;

			if (num < diag_ctx->l_num)
				goto next_listen;

			if (!ctx || strcmp(inet_csk(sk)->icsk_ulp_ops->name, "mptcp"))
				goto next_listen;

			sk = ctx->conn;
			if (!sk || !net_eq(sock_net(sk), net))
				goto next_listen;

			if (r->sdiag_family != AF_UNSPEC &&
			    sk->sk_family != r->sdiag_family)
				goto next_listen;

			if (r->id.idiag_sport != inet->inet_sport &&
			    r->id.idiag_sport)
				goto next_listen;

			if (!refcount_inc_not_zero(&sk->sk_refcnt))
				goto next_listen;

			ret = sk_diag_dump(sk, skb, cb, r, bc, net_admin);

			sock_put(sk);

			if (ret < 0) {
				spin_unlock(&ilb->lock);
				rcu_read_unlock();
				diag_ctx->l_slot = i;
				diag_ctx->l_num = num;
				return;
			}
			diag_ctx->l_num = num + 1;
			num = 0;
next_listen:
			++num;
		}
		spin_unlock(&ilb->lock);
		rcu_read_unlock();

		cond_resched();
		diag_ctx->l_num = 0;
	}

	diag_ctx->l_num = 0;
	diag_ctx->l_slot = i;
}

static void mptcp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			    const struct inet_diag_req_v2 *r)
{
	bool net_admin = netlink_net_capable(cb->skb, CAP_NET_ADMIN);
	struct mptcp_diag_ctx *diag_ctx = (void *)cb->ctx;
	struct net *net = sock_net(skb->sk);
	struct inet_diag_dump_data *cb_data;
	struct mptcp_sock *msk;
	struct nlattr *bc;

	BUILD_BUG_ON(sizeof(cb->ctx) < sizeof(*diag_ctx));

	cb_data = cb->data;
	bc = cb_data->inet_diag_nla_bc;

	while ((msk = mptcp_token_iter_next(net, &diag_ctx->s_slot,
					    &diag_ctx->s_num)) != NULL) {
		struct inet_sock *inet = (struct inet_sock *)msk;
		struct sock *sk = (struct sock *)msk;
		int ret = 0;

		if (!(r->idiag_states & (1 << sk->sk_state)))
			goto next;
		if (r->sdiag_family != AF_UNSPEC &&
		    sk->sk_family != r->sdiag_family)
			goto next;
		if (r->id.idiag_sport != inet->inet_sport &&
		    r->id.idiag_sport)
			goto next;
		if (r->id.idiag_dport != inet->inet_dport &&
		    r->id.idiag_dport)
			goto next;

		ret = sk_diag_dump(sk, skb, cb, r, bc, net_admin);
next:
		sock_put(sk);
		if (ret < 0) {
			/* will retry on the same position */
			diag_ctx->s_num--;
			break;
		}
		cond_resched();
	}

	if ((r->idiag_states & TCPF_LISTEN) && r->id.idiag_dport == 0)
		mptcp_diag_dump_listeners(skb, cb, r, net_admin);
}

static void mptcp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
				void *_info)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_info *info = _info;

	r->idiag_rqueue = sk_rmem_alloc_get(sk);
	r->idiag_wqueue = sk_wmem_alloc_get(sk);

	if (inet_sk_state_load(sk) == TCP_LISTEN) {
		struct sock *lsk = READ_ONCE(msk->first);

		if (lsk) {
			/* override with settings from tcp listener,
			 * so Send-Q will show accept queue.
			 */
			r->idiag_rqueue = READ_ONCE(lsk->sk_ack_backlog);
			r->idiag_wqueue = READ_ONCE(lsk->sk_max_ack_backlog);
		}
	}

	if (!info)
		return;

	mptcp_diag_fill_info(msk, info);
}

static const struct inet_diag_handler mptcp_diag_handler = {
	.owner		 = THIS_MODULE,
	.dump		 = mptcp_diag_dump,
	.dump_one	 = mptcp_diag_dump_one,
	.idiag_get_info  = mptcp_diag_get_info,
	.idiag_type	 = IPPROTO_MPTCP,
	.idiag_info_size = sizeof(struct mptcp_info),
};

static int __init mptcp_diag_init(void)
{
	return inet_diag_register(&mptcp_diag_handler);
}

static void __exit mptcp_diag_exit(void)
{
	inet_diag_unregister(&mptcp_diag_handler);
}

module_init(mptcp_diag_init);
module_exit(mptcp_diag_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MPTCP socket monitoring via SOCK_DIAG");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-262 /* AF_INET - IPPROTO_MPTCP */);
