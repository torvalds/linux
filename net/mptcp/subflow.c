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
		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq;
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

		if (skb) {
			pr_debug("synack seq=%u", TCP_SKB_CB(skb)->seq);
			subflow->ssn_offset = TCP_SKB_CB(skb)->seq;
		}
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

enum mapping_status {
	MAPPING_OK,
	MAPPING_INVALID,
	MAPPING_EMPTY,
	MAPPING_DATA_FIN
};

static u64 expand_seq(u64 old_seq, u16 old_data_len, u64 seq)
{
	if ((u32)seq == (u32)old_seq)
		return old_seq;

	/* Assume map covers data not mapped yet. */
	return seq | ((old_seq + old_data_len + 1) & GENMASK_ULL(63, 32));
}

static void warn_bad_map(struct mptcp_subflow_context *subflow, u32 ssn)
{
	WARN_ONCE(1, "Bad mapping: ssn=%d map_seq=%d map_data_len=%d",
		  ssn, subflow->map_subflow_seq, subflow->map_data_len);
}

static bool skb_is_fully_mapped(struct sock *ssk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	unsigned int skb_consumed;

	skb_consumed = tcp_sk(ssk)->copied_seq - TCP_SKB_CB(skb)->seq;
	if (WARN_ON_ONCE(skb_consumed >= skb->len))
		return true;

	return skb->len - skb_consumed <= subflow->map_data_len -
					  mptcp_subflow_get_map_offset(subflow);
}

static bool validate_mapping(struct sock *ssk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	u32 ssn = tcp_sk(ssk)->copied_seq - subflow->ssn_offset;

	if (unlikely(before(ssn, subflow->map_subflow_seq))) {
		/* Mapping covers data later in the subflow stream,
		 * currently unsupported.
		 */
		warn_bad_map(subflow, ssn);
		return false;
	}
	if (unlikely(!before(ssn, subflow->map_subflow_seq +
				  subflow->map_data_len))) {
		/* Mapping does covers past subflow data, invalid */
		warn_bad_map(subflow, ssn + skb->len);
		return false;
	}
	return true;
}

static enum mapping_status get_mapping_status(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_ext *mpext;
	struct sk_buff *skb;
	u16 data_len;
	u64 map_seq;

	skb = skb_peek(&ssk->sk_receive_queue);
	if (!skb)
		return MAPPING_EMPTY;

	mpext = mptcp_get_ext(skb);
	if (!mpext || !mpext->use_map) {
		if (!subflow->map_valid && !skb->len) {
			/* the TCP stack deliver 0 len FIN pkt to the receive
			 * queue, that is the only 0len pkts ever expected here,
			 * and we can admit no mapping only for 0 len pkts
			 */
			if (!(TCP_SKB_CB(skb)->tcp_flags & TCPHDR_FIN))
				WARN_ONCE(1, "0len seq %d:%d flags %x",
					  TCP_SKB_CB(skb)->seq,
					  TCP_SKB_CB(skb)->end_seq,
					  TCP_SKB_CB(skb)->tcp_flags);
			sk_eat_skb(ssk, skb);
			return MAPPING_EMPTY;
		}

		if (!subflow->map_valid)
			return MAPPING_INVALID;

		goto validate_seq;
	}

	pr_debug("seq=%llu is64=%d ssn=%u data_len=%u data_fin=%d",
		 mpext->data_seq, mpext->dsn64, mpext->subflow_seq,
		 mpext->data_len, mpext->data_fin);

	data_len = mpext->data_len;
	if (data_len == 0) {
		pr_err("Infinite mapping not handled");
		return MAPPING_INVALID;
	}

	if (mpext->data_fin == 1) {
		if (data_len == 1) {
			pr_debug("DATA_FIN with no payload");
			if (subflow->map_valid) {
				/* A DATA_FIN might arrive in a DSS
				 * option before the previous mapping
				 * has been fully consumed. Continue
				 * handling the existing mapping.
				 */
				skb_ext_del(skb, SKB_EXT_MPTCP);
				return MAPPING_OK;
			} else {
				return MAPPING_DATA_FIN;
			}
		}

		/* Adjust for DATA_FIN using 1 byte of sequence space */
		data_len--;
	}

	if (!mpext->dsn64) {
		map_seq = expand_seq(subflow->map_seq, subflow->map_data_len,
				     mpext->data_seq);
		pr_debug("expanded seq=%llu", subflow->map_seq);
	} else {
		map_seq = mpext->data_seq;
	}

	if (subflow->map_valid) {
		/* Allow replacing only with an identical map */
		if (subflow->map_seq == map_seq &&
		    subflow->map_subflow_seq == mpext->subflow_seq &&
		    subflow->map_data_len == data_len) {
			skb_ext_del(skb, SKB_EXT_MPTCP);
			return MAPPING_OK;
		}

		/* If this skb data are fully covered by the current mapping,
		 * the new map would need caching, which is not supported
		 */
		if (skb_is_fully_mapped(ssk, skb))
			return MAPPING_INVALID;

		/* will validate the next map after consuming the current one */
		return MAPPING_OK;
	}

	subflow->map_seq = map_seq;
	subflow->map_subflow_seq = mpext->subflow_seq;
	subflow->map_data_len = data_len;
	subflow->map_valid = 1;
	pr_debug("new map seq=%llu subflow_seq=%u data_len=%u",
		 subflow->map_seq, subflow->map_subflow_seq,
		 subflow->map_data_len);

validate_seq:
	/* we revalidate valid mapping on new skb, because we must ensure
	 * the current skb is completely covered by the available mapping
	 */
	if (!validate_mapping(ssk, skb))
		return MAPPING_INVALID;

	skb_ext_del(skb, SKB_EXT_MPTCP);
	return MAPPING_OK;
}

static bool subflow_check_data_avail(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	enum mapping_status status;
	struct mptcp_sock *msk;
	struct sk_buff *skb;

	pr_debug("msk=%p ssk=%p data_avail=%d skb=%p", subflow->conn, ssk,
		 subflow->data_avail, skb_peek(&ssk->sk_receive_queue));
	if (subflow->data_avail)
		return true;

	if (!subflow->conn)
		return false;

	msk = mptcp_sk(subflow->conn);
	for (;;) {
		u32 map_remaining;
		size_t delta;
		u64 ack_seq;
		u64 old_ack;

		status = get_mapping_status(ssk);
		pr_debug("msk=%p ssk=%p status=%d", msk, ssk, status);
		if (status == MAPPING_INVALID) {
			ssk->sk_err = EBADMSG;
			goto fatal;
		}

		if (status != MAPPING_OK)
			return false;

		skb = skb_peek(&ssk->sk_receive_queue);
		if (WARN_ON_ONCE(!skb))
			return false;

		old_ack = READ_ONCE(msk->ack_seq);
		ack_seq = mptcp_subflow_get_mapped_dsn(subflow);
		pr_debug("msk ack_seq=%llx subflow ack_seq=%llx", old_ack,
			 ack_seq);
		if (ack_seq == old_ack)
			break;

		/* only accept in-sequence mapping. Old values are spurious
		 * retransmission; we can hit "future" values on active backup
		 * subflow switch, we relay on retransmissions to get
		 * in-sequence data.
		 * Cuncurrent subflows support will require subflow data
		 * reordering
		 */
		map_remaining = subflow->map_data_len -
				mptcp_subflow_get_map_offset(subflow);
		if (before64(ack_seq, old_ack))
			delta = min_t(size_t, old_ack - ack_seq, map_remaining);
		else
			delta = min_t(size_t, ack_seq - old_ack, map_remaining);

		/* discard mapped data */
		pr_debug("discarding %zu bytes, current map len=%d", delta,
			 map_remaining);
		if (delta) {
			struct mptcp_read_arg arg = {
				.msg = NULL,
			};
			read_descriptor_t desc = {
				.count = delta,
				.arg.data = &arg,
			};
			int ret;

			ret = tcp_read_sock(ssk, &desc, mptcp_read_actor);
			if (ret < 0) {
				ssk->sk_err = -ret;
				goto fatal;
			}
			if (ret < delta)
				return false;
			if (delta == map_remaining)
				subflow->map_valid = 0;
		}
	}
	return true;

fatal:
	/* fatal protocol error, close the socket */
	/* This barrier is coupled with smp_rmb() in tcp_poll() */
	smp_wmb();
	ssk->sk_error_report(ssk);
	tcp_set_state(ssk, TCP_CLOSE);
	tcp_send_active_reset(ssk, GFP_ATOMIC);
	return false;
}

bool mptcp_subflow_data_available(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sk_buff *skb;

	/* check if current mapping is still valid */
	if (subflow->map_valid &&
	    mptcp_subflow_get_map_offset(subflow) >= subflow->map_data_len) {
		subflow->map_valid = 0;
		subflow->data_avail = 0;

		pr_debug("Done with mapping: seq=%u data_len=%u",
			 subflow->map_subflow_seq,
			 subflow->map_data_len);
	}

	if (!subflow_check_data_avail(sk)) {
		subflow->data_avail = 0;
		return false;
	}

	skb = skb_peek(&sk->sk_receive_queue);
	subflow->data_avail = skb &&
		       before(tcp_sk(sk)->copied_seq, TCP_SKB_CB(skb)->end_seq);
	return subflow->data_avail;
}

static void subflow_data_ready(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sock *parent = subflow->conn;

	if (!parent || !subflow->mp_capable) {
		subflow->tcp_data_ready(sk);

		if (parent)
			parent->sk_data_ready(parent);
		return;
	}

	if (mptcp_subflow_data_available(sk)) {
		set_bit(MPTCP_DATA_READY, &mptcp_sk(parent)->flags);

		parent->sk_data_ready(parent);
	}
}

static void subflow_write_space(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sock *parent = subflow->conn;

	sk_stream_write_space(sk);
	if (parent && sk_stream_is_writeable(sk)) {
		set_bit(MPTCP_SEND_SPACE, &mptcp_sk(parent)->flags);
		smp_mb__after_atomic();
		/* set SEND_SPACE before sk_stream_write_space clears NOSPACE */
		sk_stream_write_space(parent);
	}
}

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

static void __subflow_state_change(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_all(&wq->wait);
	rcu_read_unlock();
}

static bool subflow_is_done(const struct sock *sk)
{
	return sk->sk_shutdown & RCV_SHUTDOWN || sk->sk_state == TCP_CLOSE;
}

static void subflow_state_change(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sock *parent = READ_ONCE(subflow->conn);

	__subflow_state_change(sk);

	/* as recvmsg() does not acquire the subflow socket for ssk selection
	 * a fin packet carrying a DSS can be unnoticed if we don't trigger
	 * the data available machinery here.
	 */
	if (parent && subflow->mp_capable && mptcp_subflow_data_available(sk)) {
		set_bit(MPTCP_DATA_READY, &mptcp_sk(parent)->flags);

		parent->sk_data_ready(parent);
	}

	if (parent && !(parent->sk_shutdown & RCV_SHUTDOWN) &&
	    !subflow->rx_eof && subflow_is_done(sk)) {
		subflow->rx_eof = 1;
		parent->sk_shutdown |= RCV_SHUTDOWN;
		__subflow_state_change(parent);
	}
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
	ctx->tcp_data_ready = sk->sk_data_ready;
	ctx->tcp_state_change = sk->sk_state_change;
	ctx->tcp_write_space = sk->sk_write_space;
	sk->sk_data_ready = subflow_data_ready;
	sk->sk_write_space = subflow_write_space;
	sk->sk_state_change = subflow_state_change;
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

static void subflow_ulp_fallback(struct sock *sk,
				 struct mptcp_subflow_context *old_ctx)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	mptcp_subflow_tcp_fallback(sk, old_ctx);
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
		subflow_ulp_fallback(newsk, old_ctx);
		return;
	}

	new_ctx = subflow_create_ctx(newsk, priority);
	if (new_ctx == NULL) {
		subflow_ulp_fallback(newsk, old_ctx);
		return;
	}

	new_ctx->conn_finished = 1;
	new_ctx->icsk_af_ops = old_ctx->icsk_af_ops;
	new_ctx->tcp_data_ready = old_ctx->tcp_data_ready;
	new_ctx->tcp_state_change = old_ctx->tcp_state_change;
	new_ctx->tcp_write_space = old_ctx->tcp_write_space;
	new_ctx->mp_capable = 1;
	new_ctx->fourth_ack = 1;
	new_ctx->remote_key = subflow_req->remote_key;
	new_ctx->local_key = subflow_req->local_key;
	new_ctx->token = subflow_req->token;
	new_ctx->ssn_offset = subflow_req->ssn_offset;
	new_ctx->idsn = subflow_req->idsn;
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
