// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/protocol.h>
#include <net/tcp.h>
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
#include <net/ip6_route.h>
#endif
#include <net/mptcp.h>
#include "protocol.h"

static int subflow_rebuild_header(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	int err = 0;

	if (subflow->request_mptcp && !subflow->token) {
		pr_debug("subflow=%p", sk);
		err = mptcp_token_new_connect(sk);
	}

	if (err)
		return err;

	return subflow->icsk_af_ops->rebuild_header(sk);
}

static void subflow_req_destructor(struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);

	pr_debug("subflow_req=%p", subflow_req);

	if (subflow_req->mp_capable)
		mptcp_token_destroy_request(subflow_req->token);
	tcp_request_sock_ops.destructor(req);
}

static void subflow_init_req(struct request_sock *req,
			     const struct sock *sk_listener,
			     struct sk_buff *skb)
{
	struct mptcp_subflow_context *listener = mptcp_subflow_ctx(sk_listener);
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct tcp_options_received rx_opt;

	pr_debug("subflow_req=%p, listener=%p", subflow_req, listener);

	memset(&rx_opt.mptcp, 0, sizeof(rx_opt.mptcp));
	mptcp_get_options(skb, &rx_opt);

	subflow_req->mp_capable = 0;

#ifdef CONFIG_TCP_MD5SIG
	/* no MPTCP if MD5SIG is enabled on this socket or we may run out of
	 * TCP option space.
	 */
	if (rcu_access_pointer(tcp_sk(sk_listener)->md5sig_info))
		return;
#endif

	if (rx_opt.mptcp.mp_capable && listener->request_mptcp) {
		int err;

		err = mptcp_token_new_request(req);
		if (err == 0)
			subflow_req->mp_capable = 1;

		subflow_req->remote_key = rx_opt.mptcp.sndr_key;
	}
}

static void subflow_v4_init_req(struct request_sock *req,
				const struct sock *sk_listener,
				struct sk_buff *skb)
{
	tcp_rsk(req)->is_mptcp = 1;

	tcp_request_sock_ipv4_ops.init_req(req, sk_listener, skb);

	subflow_init_req(req, sk_listener, skb);
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static void subflow_v6_init_req(struct request_sock *req,
				const struct sock *sk_listener,
				struct sk_buff *skb)
{
	tcp_rsk(req)->is_mptcp = 1;

	tcp_request_sock_ipv6_ops.init_req(req, sk_listener, skb);

	subflow_init_req(req, sk_listener, skb);
}
#endif

static void subflow_finish_connect(struct sock *sk, const struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	subflow->icsk_af_ops->sk_rx_dst_set(sk, skb);

	if (subflow->conn && !subflow->conn_finished) {
		pr_debug("subflow=%p, remote_key=%llu", mptcp_subflow_ctx(sk),
			 subflow->remote_key);
		mptcp_finish_connect(sk);
		subflow->conn_finished = 1;
	}
}

static struct request_sock_ops subflow_request_sock_ops;
static struct tcp_request_sock_ops subflow_request_sock_ipv4_ops;

static int subflow_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	pr_debug("subflow=%p", subflow);

	/* Never answer to SYNs sent to broadcast or multicast */
	if (skb_rtable(skb)->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST))
		goto drop;

	return tcp_conn_request(&subflow_request_sock_ops,
				&subflow_request_sock_ipv4_ops,
				sk, skb);
drop:
	tcp_listendrop(sk);
	return 0;
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct tcp_request_sock_ops subflow_request_sock_ipv6_ops;
static struct inet_connection_sock_af_ops subflow_v6_specific;
static struct inet_connection_sock_af_ops subflow_v6m_specific;

static int subflow_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	pr_debug("subflow=%p", subflow);

	if (skb->protocol == htons(ETH_P_IP))
		return subflow_v4_conn_request(sk, skb);

	if (!ipv6_unicast_destination(skb))
		goto drop;

	return tcp_conn_request(&subflow_request_sock_ops,
				&subflow_request_sock_ipv6_ops, sk, skb);

drop:
	tcp_listendrop(sk);
	return 0; /* don't send reset */
}
#endif

static struct sock *subflow_syn_recv_sock(const struct sock *sk,
					  struct sk_buff *skb,
					  struct request_sock *req,
					  struct dst_entry *dst,
					  struct request_sock *req_unhash,
					  bool *own_req)
{
	struct mptcp_subflow_context *listener = mptcp_subflow_ctx(sk);
	struct sock *child;

	pr_debug("listener=%p, req=%p, conn=%p", listener, req, listener->conn);

	/* if the sk is MP_CAPABLE, we already received the client key */

	child = listener->icsk_af_ops->syn_recv_sock(sk, skb, req, dst,
						     req_unhash, own_req);

	if (child && *own_req) {
		struct mptcp_subflow_context *ctx = mptcp_subflow_ctx(child);

		/* we have null ctx on TCP fallback, not fatal on MPC
		 * handshake
		 */
		if (!ctx)
			return child;

		if (ctx->mp_capable) {
			if (mptcp_token_new_accept(ctx->token))
				goto close_child;
		}
	}

	return child;

close_child:
	pr_debug("closing child socket");
	tcp_send_active_reset(child, GFP_ATOMIC);
	inet_csk_prepare_forced_close(child);
	tcp_done(child);
	return NULL;
}

static struct inet_connection_sock_af_ops subflow_specific;

static struct inet_connection_sock_af_ops *
subflow_default_af_ops(struct sock *sk)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (sk->sk_family == AF_INET6)
		return &subflow_v6_specific;
#endif
	return &subflow_specific;
}

void mptcp_handle_ipv6_mapped(struct sock *sk, bool mapped)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_connection_sock_af_ops *target;

	target = mapped ? &subflow_v6m_specific : subflow_default_af_ops(sk);

	pr_debug("subflow=%p family=%d ops=%p target=%p mapped=%d",
	         subflow, sk->sk_family, icsk->icsk_af_ops, target, mapped);

	if (likely(icsk->icsk_af_ops == target))
		return;

	subflow->icsk_af_ops = icsk->icsk_af_ops;
	icsk->icsk_af_ops = target;
#endif
}

int mptcp_subflow_create_socket(struct sock *sk, struct socket **new_sock)
{
	struct mptcp_subflow_context *subflow;
	struct net *net = sock_net(sk);
	struct socket *sf;
	int err;

	err = sock_create_kern(net, sk->sk_family, SOCK_STREAM, IPPROTO_TCP,
			       &sf);
	if (err)
		return err;

	lock_sock(sf->sk);

	/* kernel sockets do not by default acquire net ref, but TCP timer
	 * needs it.
	 */
	sf->sk->sk_net_refcnt = 1;
	get_net(net);
	this_cpu_add(*net->core.sock_inuse, 1);
	err = tcp_set_ulp(sf->sk, "mptcp");
	release_sock(sf->sk);

	if (err)
		return err;

	subflow = mptcp_subflow_ctx(sf->sk);
	pr_debug("subflow=%p", subflow);

	*new_sock = sf;
	sock_hold(sk);
	subflow->conn = sk;

	return 0;
}

static struct mptcp_subflow_context *subflow_create_ctx(struct sock *sk,
							gfp_t priority)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct mptcp_subflow_context *ctx;

	ctx = kzalloc(sizeof(*ctx), priority);
	if (!ctx)
		return NULL;

	rcu_assign_pointer(icsk->icsk_ulp_data, ctx);
	INIT_LIST_HEAD(&ctx->node);

	pr_debug("subflow=%p", ctx);

	ctx->tcp_sock = sk;

	return ctx;
}

static int subflow_ulp_init(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct mptcp_subflow_context *ctx;
	struct tcp_sock *tp = tcp_sk(sk);
	int err = 0;

	/* disallow attaching ULP to a socket unless it has been
	 * created with sock_create_kern()
	 */
	if (!sk->sk_kern_sock) {
		err = -EOPNOTSUPP;
		goto out;
	}

	ctx = subflow_create_ctx(sk, GFP_KERNEL);
	if (!ctx) {
		err = -ENOMEM;
		goto out;
	}

	pr_debug("subflow=%p, family=%d", ctx, sk->sk_family);

	tp->is_mptcp = 1;
	ctx->icsk_af_ops = icsk->icsk_af_ops;
	icsk->icsk_af_ops = subflow_default_af_ops(sk);
out:
	return err;
}

static void subflow_ulp_release(struct sock *sk)
{
	struct mptcp_subflow_context *ctx = mptcp_subflow_ctx(sk);

	if (!ctx)
		return;

	if (ctx->conn)
		sock_put(ctx->conn);

	kfree_rcu(ctx, rcu);
}

static void subflow_ulp_fallback(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	icsk->icsk_ulp_ops = NULL;
	rcu_assign_pointer(icsk->icsk_ulp_data, NULL);
	tcp_sk(sk)->is_mptcp = 0;
}

static void subflow_ulp_clone(const struct request_sock *req,
			      struct sock *newsk,
			      const gfp_t priority)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_subflow_context *old_ctx = mptcp_subflow_ctx(newsk);
	struct mptcp_subflow_context *new_ctx;

	if (!subflow_req->mp_capable) {
		subflow_ulp_fallback(newsk);
		return;
	}

	new_ctx = subflow_create_ctx(newsk, priority);
	if (new_ctx == NULL) {
		subflow_ulp_fallback(newsk);
		return;
	}

	new_ctx->conn_finished = 1;
	new_ctx->icsk_af_ops = old_ctx->icsk_af_ops;
	new_ctx->mp_capable = 1;
	new_ctx->fourth_ack = 1;
	new_ctx->remote_key = subflow_req->remote_key;
	new_ctx->local_key = subflow_req->local_key;
	new_ctx->token = subflow_req->token;
}

static struct tcp_ulp_ops subflow_ulp_ops __read_mostly = {
	.name		= "mptcp",
	.owner		= THIS_MODULE,
	.init		= subflow_ulp_init,
	.release	= subflow_ulp_release,
	.clone		= subflow_ulp_clone,
};

static int subflow_ops_init(struct request_sock_ops *subflow_ops)
{
	subflow_ops->obj_size = sizeof(struct mptcp_subflow_request_sock);
	subflow_ops->slab_name = "request_sock_subflow";

	subflow_ops->slab = kmem_cache_create(subflow_ops->slab_name,
					      subflow_ops->obj_size, 0,
					      SLAB_ACCOUNT |
					      SLAB_TYPESAFE_BY_RCU,
					      NULL);
	if (!subflow_ops->slab)
		return -ENOMEM;

	subflow_ops->destructor = subflow_req_destructor;

	return 0;
}

void mptcp_subflow_init(void)
{
	subflow_request_sock_ops = tcp_request_sock_ops;
	if (subflow_ops_init(&subflow_request_sock_ops) != 0)
		panic("MPTCP: failed to init subflow request sock ops\n");

	subflow_request_sock_ipv4_ops = tcp_request_sock_ipv4_ops;
	subflow_request_sock_ipv4_ops.init_req = subflow_v4_init_req;

	subflow_specific = ipv4_specific;
	subflow_specific.conn_request = subflow_v4_conn_request;
	subflow_specific.syn_recv_sock = subflow_syn_recv_sock;
	subflow_specific.sk_rx_dst_set = subflow_finish_connect;
	subflow_specific.rebuild_header = subflow_rebuild_header;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	subflow_request_sock_ipv6_ops = tcp_request_sock_ipv6_ops;
	subflow_request_sock_ipv6_ops.init_req = subflow_v6_init_req;

	subflow_v6_specific = ipv6_specific;
	subflow_v6_specific.conn_request = subflow_v6_conn_request;
	subflow_v6_specific.syn_recv_sock = subflow_syn_recv_sock;
	subflow_v6_specific.sk_rx_dst_set = subflow_finish_connect;
	subflow_v6_specific.rebuild_header = subflow_rebuild_header;

	subflow_v6m_specific = subflow_v6_specific;
	subflow_v6m_specific.queue_xmit = ipv4_specific.queue_xmit;
	subflow_v6m_specific.send_check = ipv4_specific.send_check;
	subflow_v6m_specific.net_header_len = ipv4_specific.net_header_len;
	subflow_v6m_specific.mtu_reduced = ipv4_specific.mtu_reduced;
	subflow_v6m_specific.net_frag_header_len = 0;
#endif

	if (tcp_register_ulp(&subflow_ulp_ops) != 0)
		panic("MPTCP: failed to register subflows to ULP\n");
}
