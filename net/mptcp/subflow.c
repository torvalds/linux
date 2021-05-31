// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <crypto/algapi.h>
#include <crypto/sha2.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/protocol.h>
#include <net/tcp.h>
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
#include <net/ip6_route.h>
#include <net/transp_v6.h>
#endif
#include <net/mptcp.h>
#include <uapi/linux/mptcp.h>
#include "protocol.h"
#include "mib.h"

#include <trace/events/mptcp.h>

static void mptcp_subflow_ops_undo_override(struct sock *ssk);

static void SUBFLOW_REQ_INC_STATS(struct request_sock *req,
				  enum linux_mptcp_mib_field field)
{
	MPTCP_INC_STATS(sock_net(req_to_sk(req)), field);
}

static void subflow_req_destructor(struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);

	pr_debug("subflow_req=%p", subflow_req);

	if (subflow_req->msk)
		sock_put((struct sock *)subflow_req->msk);

	mptcp_token_destroy_request(req);
	tcp_request_sock_ops.destructor(req);
}

static void subflow_generate_hmac(u64 key1, u64 key2, u32 nonce1, u32 nonce2,
				  void *hmac)
{
	u8 msg[8];

	put_unaligned_be32(nonce1, &msg[0]);
	put_unaligned_be32(nonce2, &msg[4]);

	mptcp_crypto_hmac_sha(key1, key2, msg, 8, hmac);
}

static bool mptcp_can_accept_new_subflow(const struct mptcp_sock *msk)
{
	return mptcp_is_fully_established((void *)msk) &&
	       READ_ONCE(msk->pm.accept_subflow);
}

/* validate received token and create truncated hmac and nonce for SYN-ACK */
static void subflow_req_create_thmac(struct mptcp_subflow_request_sock *subflow_req)
{
	struct mptcp_sock *msk = subflow_req->msk;
	u8 hmac[SHA256_DIGEST_SIZE];

	get_random_bytes(&subflow_req->local_nonce, sizeof(u32));

	subflow_generate_hmac(msk->local_key, msk->remote_key,
			      subflow_req->local_nonce,
			      subflow_req->remote_nonce, hmac);

	subflow_req->thmac = get_unaligned_be64(hmac);
}

static struct mptcp_sock *subflow_token_join_request(struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_sock *msk;
	int local_id;

	msk = mptcp_token_get_sock(subflow_req->token);
	if (!msk) {
		SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINNOTOKEN);
		return NULL;
	}

	local_id = mptcp_pm_get_local_id(msk, (struct sock_common *)req);
	if (local_id < 0) {
		sock_put((struct sock *)msk);
		return NULL;
	}
	subflow_req->local_id = local_id;

	return msk;
}

static void subflow_init_req(struct request_sock *req, const struct sock *sk_listener)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);

	subflow_req->mp_capable = 0;
	subflow_req->mp_join = 0;
	subflow_req->msk = NULL;
	mptcp_token_init_request(req);
}

static bool subflow_use_different_sport(struct mptcp_sock *msk, const struct sock *sk)
{
	return inet_sk(sk)->inet_sport != inet_sk((struct sock *)msk)->inet_sport;
}

static void subflow_add_reset_reason(struct sk_buff *skb, u8 reason)
{
	struct mptcp_ext *mpext = skb_ext_add(skb, SKB_EXT_MPTCP);

	if (mpext) {
		memset(mpext, 0, sizeof(*mpext));
		mpext->reset_reason = reason;
	}
}

/* Init mptcp request socket.
 *
 * Returns an error code if a JOIN has failed and a TCP reset
 * should be sent.
 */
static int subflow_check_req(struct request_sock *req,
			     const struct sock *sk_listener,
			     struct sk_buff *skb)
{
	struct mptcp_subflow_context *listener = mptcp_subflow_ctx(sk_listener);
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_options_received mp_opt;

	pr_debug("subflow_req=%p, listener=%p", subflow_req, listener);

#ifdef CONFIG_TCP_MD5SIG
	/* no MPTCP if MD5SIG is enabled on this socket or we may run out of
	 * TCP option space.
	 */
	if (rcu_access_pointer(tcp_sk(sk_listener)->md5sig_info))
		return -EINVAL;
#endif

	mptcp_get_options(skb, &mp_opt);

	if (mp_opt.mp_capable) {
		SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_MPCAPABLEPASSIVE);

		if (mp_opt.mp_join)
			return 0;
	} else if (mp_opt.mp_join) {
		SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINSYNRX);
	}

	if (mp_opt.mp_capable && listener->request_mptcp) {
		int err, retries = 4;

		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq;
again:
		do {
			get_random_bytes(&subflow_req->local_key, sizeof(subflow_req->local_key));
		} while (subflow_req->local_key == 0);

		if (unlikely(req->syncookie)) {
			mptcp_crypto_key_sha(subflow_req->local_key,
					     &subflow_req->token,
					     &subflow_req->idsn);
			if (mptcp_token_exists(subflow_req->token)) {
				if (retries-- > 0)
					goto again;
				SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_TOKENFALLBACKINIT);
			} else {
				subflow_req->mp_capable = 1;
			}
			return 0;
		}

		err = mptcp_token_new_request(req);
		if (err == 0)
			subflow_req->mp_capable = 1;
		else if (retries-- > 0)
			goto again;
		else
			SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_TOKENFALLBACKINIT);

	} else if (mp_opt.mp_join && listener->request_mptcp) {
		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq;
		subflow_req->mp_join = 1;
		subflow_req->backup = mp_opt.backup;
		subflow_req->remote_id = mp_opt.join_id;
		subflow_req->token = mp_opt.token;
		subflow_req->remote_nonce = mp_opt.nonce;
		subflow_req->msk = subflow_token_join_request(req);

		/* Can't fall back to TCP in this case. */
		if (!subflow_req->msk) {
			subflow_add_reset_reason(skb, MPTCP_RST_EMPTCP);
			return -EPERM;
		}

		if (subflow_use_different_sport(subflow_req->msk, sk_listener)) {
			pr_debug("syn inet_sport=%d %d",
				 ntohs(inet_sk(sk_listener)->inet_sport),
				 ntohs(inet_sk((struct sock *)subflow_req->msk)->inet_sport));
			if (!mptcp_pm_sport_in_anno_list(subflow_req->msk, sk_listener)) {
				sock_put((struct sock *)subflow_req->msk);
				mptcp_token_destroy_request(req);
				tcp_request_sock_ops.destructor(req);
				subflow_req->msk = NULL;
				subflow_req->mp_join = 0;
				SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_MISMATCHPORTSYNRX);
				return -EPERM;
			}
			SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINPORTSYNRX);
		}

		subflow_req_create_thmac(subflow_req);

		if (unlikely(req->syncookie)) {
			if (mptcp_can_accept_new_subflow(subflow_req->msk))
				subflow_init_req_cookie_join_save(subflow_req, skb);
		}

		pr_debug("token=%u, remote_nonce=%u msk=%p", subflow_req->token,
			 subflow_req->remote_nonce, subflow_req->msk);
	}

	return 0;
}

int mptcp_subflow_init_cookie_req(struct request_sock *req,
				  const struct sock *sk_listener,
				  struct sk_buff *skb)
{
	struct mptcp_subflow_context *listener = mptcp_subflow_ctx(sk_listener);
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_options_received mp_opt;
	int err;

	subflow_init_req(req, sk_listener);
	mptcp_get_options(skb, &mp_opt);

	if (mp_opt.mp_capable && mp_opt.mp_join)
		return -EINVAL;

	if (mp_opt.mp_capable && listener->request_mptcp) {
		if (mp_opt.sndr_key == 0)
			return -EINVAL;

		subflow_req->local_key = mp_opt.rcvr_key;
		err = mptcp_token_new_request(req);
		if (err)
			return err;

		subflow_req->mp_capable = 1;
		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq - 1;
	} else if (mp_opt.mp_join && listener->request_mptcp) {
		if (!mptcp_token_join_cookie_init_state(subflow_req, skb))
			return -EINVAL;

		if (mptcp_can_accept_new_subflow(subflow_req->msk))
			subflow_req->mp_join = 1;

		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq - 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mptcp_subflow_init_cookie_req);

static struct dst_entry *subflow_v4_route_req(const struct sock *sk,
					      struct sk_buff *skb,
					      struct flowi *fl,
					      struct request_sock *req)
{
	struct dst_entry *dst;
	int err;

	tcp_rsk(req)->is_mptcp = 1;
	subflow_init_req(req, sk);

	dst = tcp_request_sock_ipv4_ops.route_req(sk, skb, fl, req);
	if (!dst)
		return NULL;

	err = subflow_check_req(req, sk, skb);
	if (err == 0)
		return dst;

	dst_release(dst);
	if (!req->syncookie)
		tcp_request_sock_ops.send_reset(sk, skb);
	return NULL;
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct dst_entry *subflow_v6_route_req(const struct sock *sk,
					      struct sk_buff *skb,
					      struct flowi *fl,
					      struct request_sock *req)
{
	struct dst_entry *dst;
	int err;

	tcp_rsk(req)->is_mptcp = 1;
	subflow_init_req(req, sk);

	dst = tcp_request_sock_ipv6_ops.route_req(sk, skb, fl, req);
	if (!dst)
		return NULL;

	err = subflow_check_req(req, sk, skb);
	if (err == 0)
		return dst;

	dst_release(dst);
	if (!req->syncookie)
		tcp6_request_sock_ops.send_reset(sk, skb);
	return NULL;
}
#endif

/* validate received truncated hmac and create hmac for third ACK */
static bool subflow_thmac_valid(struct mptcp_subflow_context *subflow)
{
	u8 hmac[SHA256_DIGEST_SIZE];
	u64 thmac;

	subflow_generate_hmac(subflow->remote_key, subflow->local_key,
			      subflow->remote_nonce, subflow->local_nonce,
			      hmac);

	thmac = get_unaligned_be64(hmac);
	pr_debug("subflow=%p, token=%u, thmac=%llu, subflow->thmac=%llu\n",
		 subflow, subflow->token,
		 (unsigned long long)thmac,
		 (unsigned long long)subflow->thmac);

	return thmac == subflow->thmac;
}

void mptcp_subflow_reset(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = subflow->conn;

	/* must hold: tcp_done() could drop last reference on parent */
	sock_hold(sk);

	tcp_set_state(ssk, TCP_CLOSE);
	tcp_send_active_reset(ssk, GFP_ATOMIC);
	tcp_done(ssk);
	if (!test_and_set_bit(MPTCP_WORK_CLOSE_SUBFLOW, &mptcp_sk(sk)->flags) &&
	    schedule_work(&mptcp_sk(sk)->work))
		return; /* worker will put sk for us */

	sock_put(sk);
}

static bool subflow_use_different_dport(struct mptcp_sock *msk, const struct sock *sk)
{
	return inet_sk(sk)->inet_dport != inet_sk((struct sock *)msk)->inet_dport;
}

static void subflow_finish_connect(struct sock *sk, const struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_options_received mp_opt;
	struct sock *parent = subflow->conn;

	subflow->icsk_af_ops->sk_rx_dst_set(sk, skb);

	if (inet_sk_state_load(parent) == TCP_SYN_SENT) {
		inet_sk_state_store(parent, TCP_ESTABLISHED);
		parent->sk_state_change(parent);
	}

	/* be sure no special action on any packet other than syn-ack */
	if (subflow->conn_finished)
		return;

	mptcp_propagate_sndbuf(parent, sk);
	subflow->rel_write_seq = 1;
	subflow->conn_finished = 1;
	subflow->ssn_offset = TCP_SKB_CB(skb)->seq;
	pr_debug("subflow=%p synack seq=%x", subflow, subflow->ssn_offset);

	mptcp_get_options(skb, &mp_opt);
	if (subflow->request_mptcp) {
		if (!mp_opt.mp_capable) {
			MPTCP_INC_STATS(sock_net(sk),
					MPTCP_MIB_MPCAPABLEACTIVEFALLBACK);
			mptcp_do_fallback(sk);
			pr_fallback(mptcp_sk(subflow->conn));
			goto fallback;
		}

		subflow->mp_capable = 1;
		subflow->can_ack = 1;
		subflow->remote_key = mp_opt.sndr_key;
		pr_debug("subflow=%p, remote_key=%llu", subflow,
			 subflow->remote_key);
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPCAPABLEACTIVEACK);
		mptcp_finish_connect(sk);
	} else if (subflow->request_join) {
		u8 hmac[SHA256_DIGEST_SIZE];

		if (!mp_opt.mp_join) {
			subflow->reset_reason = MPTCP_RST_EMPTCP;
			goto do_reset;
		}

		subflow->thmac = mp_opt.thmac;
		subflow->remote_nonce = mp_opt.nonce;
		pr_debug("subflow=%p, thmac=%llu, remote_nonce=%u", subflow,
			 subflow->thmac, subflow->remote_nonce);

		if (!subflow_thmac_valid(subflow)) {
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_JOINACKMAC);
			subflow->reset_reason = MPTCP_RST_EMPTCP;
			goto do_reset;
		}

		subflow_generate_hmac(subflow->local_key, subflow->remote_key,
				      subflow->local_nonce,
				      subflow->remote_nonce,
				      hmac);
		memcpy(subflow->hmac, hmac, MPTCPOPT_HMAC_LEN);

		if (!mptcp_finish_join(sk))
			goto do_reset;

		subflow->mp_join = 1;
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_JOINSYNACKRX);

		if (subflow_use_different_dport(mptcp_sk(parent), sk)) {
			pr_debug("synack inet_dport=%d %d",
				 ntohs(inet_sk(sk)->inet_dport),
				 ntohs(inet_sk(parent)->inet_dport));
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_JOINPORTSYNACKRX);
		}
	} else if (mptcp_check_fallback(sk)) {
fallback:
		mptcp_rcv_space_init(mptcp_sk(parent), sk);
	}
	return;

do_reset:
	subflow->reset_transient = 0;
	mptcp_subflow_reset(sk);
}

struct request_sock_ops mptcp_subflow_request_sock_ops;
EXPORT_SYMBOL_GPL(mptcp_subflow_request_sock_ops);
static struct tcp_request_sock_ops subflow_request_sock_ipv4_ops;

static int subflow_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	pr_debug("subflow=%p", subflow);

	/* Never answer to SYNs sent to broadcast or multicast */
	if (skb_rtable(skb)->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST))
		goto drop;

	return tcp_conn_request(&mptcp_subflow_request_sock_ops,
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
static struct proto tcpv6_prot_override;

static int subflow_v6_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	pr_debug("subflow=%p", subflow);

	if (skb->protocol == htons(ETH_P_IP))
		return subflow_v4_conn_request(sk, skb);

	if (!ipv6_unicast_destination(skb))
		goto drop;

	if (ipv6_addr_v4mapped(&ipv6_hdr(skb)->saddr)) {
		__IP6_INC_STATS(sock_net(sk), NULL, IPSTATS_MIB_INHDRERRORS);
		return 0;
	}

	return tcp_conn_request(&mptcp_subflow_request_sock_ops,
				&subflow_request_sock_ipv6_ops, sk, skb);

drop:
	tcp_listendrop(sk);
	return 0; /* don't send reset */
}
#endif

/* validate hmac received in third ACK */
static bool subflow_hmac_valid(const struct request_sock *req,
			       const struct mptcp_options_received *mp_opt)
{
	const struct mptcp_subflow_request_sock *subflow_req;
	u8 hmac[SHA256_DIGEST_SIZE];
	struct mptcp_sock *msk;

	subflow_req = mptcp_subflow_rsk(req);
	msk = subflow_req->msk;
	if (!msk)
		return false;

	subflow_generate_hmac(msk->remote_key, msk->local_key,
			      subflow_req->remote_nonce,
			      subflow_req->local_nonce, hmac);

	return !crypto_memneq(hmac, mp_opt->hmac, MPTCPOPT_HMAC_LEN);
}

static void mptcp_sock_destruct(struct sock *sk)
{
	/* if new mptcp socket isn't accepted, it is free'd
	 * from the tcp listener sockets request queue, linked
	 * from req->sk.  The tcp socket is released.
	 * This calls the ULP release function which will
	 * also remove the mptcp socket, via
	 * sock_put(ctx->conn).
	 *
	 * Problem is that the mptcp socket will be in
	 * ESTABLISHED state and will not have the SOCK_DEAD flag.
	 * Both result in warnings from inet_sock_destruct.
	 */
	if ((1 << sk->sk_state) & (TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		sk->sk_state = TCP_CLOSE;
		WARN_ON_ONCE(sk->sk_socket);
		sock_orphan(sk);
	}

	mptcp_destroy_common(mptcp_sk(sk));
	inet_sock_destruct(sk);
}

static void mptcp_force_close(struct sock *sk)
{
	inet_sk_state_store(sk, TCP_CLOSE);
	sk_common_release(sk);
}

static void subflow_ulp_fallback(struct sock *sk,
				 struct mptcp_subflow_context *old_ctx)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	mptcp_subflow_tcp_fallback(sk, old_ctx);
	icsk->icsk_ulp_ops = NULL;
	rcu_assign_pointer(icsk->icsk_ulp_data, NULL);
	tcp_sk(sk)->is_mptcp = 0;

	mptcp_subflow_ops_undo_override(sk);
}

static void subflow_drop_ctx(struct sock *ssk)
{
	struct mptcp_subflow_context *ctx = mptcp_subflow_ctx(ssk);

	if (!ctx)
		return;

	subflow_ulp_fallback(ssk, ctx);
	if (ctx->conn)
		sock_put(ctx->conn);

	kfree_rcu(ctx, rcu);
}

void mptcp_subflow_fully_established(struct mptcp_subflow_context *subflow,
				     struct mptcp_options_received *mp_opt)
{
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);

	subflow->remote_key = mp_opt->sndr_key;
	subflow->fully_established = 1;
	subflow->can_ack = 1;
	WRITE_ONCE(msk->fully_established, true);
}

static struct sock *subflow_syn_recv_sock(const struct sock *sk,
					  struct sk_buff *skb,
					  struct request_sock *req,
					  struct dst_entry *dst,
					  struct request_sock *req_unhash,
					  bool *own_req)
{
	struct mptcp_subflow_context *listener = mptcp_subflow_ctx(sk);
	struct mptcp_subflow_request_sock *subflow_req;
	struct mptcp_options_received mp_opt;
	bool fallback, fallback_is_fatal;
	struct sock *new_msk = NULL;
	struct sock *child;

	pr_debug("listener=%p, req=%p, conn=%p", listener, req, listener->conn);

	/* After child creation we must look for 'mp_capable' even when options
	 * are not parsed
	 */
	mp_opt.mp_capable = 0;

	/* hopefully temporary handling for MP_JOIN+syncookie */
	subflow_req = mptcp_subflow_rsk(req);
	fallback_is_fatal = tcp_rsk(req)->is_mptcp && subflow_req->mp_join;
	fallback = !tcp_rsk(req)->is_mptcp;
	if (fallback)
		goto create_child;

	/* if the sk is MP_CAPABLE, we try to fetch the client key */
	if (subflow_req->mp_capable) {
		if (TCP_SKB_CB(skb)->seq != subflow_req->ssn_offset + 1) {
			/* here we can receive and accept an in-window,
			 * out-of-order pkt, which will not carry the MP_CAPABLE
			 * opt even on mptcp enabled paths
			 */
			goto create_msk;
		}

		mptcp_get_options(skb, &mp_opt);
		if (!mp_opt.mp_capable) {
			fallback = true;
			goto create_child;
		}

create_msk:
		new_msk = mptcp_sk_clone(listener->conn, &mp_opt, req);
		if (!new_msk)
			fallback = true;
	} else if (subflow_req->mp_join) {
		mptcp_get_options(skb, &mp_opt);
		if (!mp_opt.mp_join || !subflow_hmac_valid(req, &mp_opt) ||
		    !mptcp_can_accept_new_subflow(subflow_req->msk)) {
			SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINACKMAC);
			fallback = true;
		}
	}

create_child:
	child = listener->icsk_af_ops->syn_recv_sock(sk, skb, req, dst,
						     req_unhash, own_req);

	if (child && *own_req) {
		struct mptcp_subflow_context *ctx = mptcp_subflow_ctx(child);

		tcp_rsk(req)->drop_req = false;

		/* we need to fallback on ctx allocation failure and on pre-reqs
		 * checking above. In the latter scenario we additionally need
		 * to reset the context to non MPTCP status.
		 */
		if (!ctx || fallback) {
			if (fallback_is_fatal) {
				subflow_add_reset_reason(skb, MPTCP_RST_EMPTCP);
				goto dispose_child;
			}

			subflow_drop_ctx(child);
			goto out;
		}

		/* ssk inherits options of listener sk */
		ctx->setsockopt_seq = listener->setsockopt_seq;

		if (ctx->mp_capable) {
			/* this can't race with mptcp_close(), as the msk is
			 * not yet exposted to user-space
			 */
			inet_sk_state_store((void *)new_msk, TCP_ESTABLISHED);

			/* record the newly created socket as the first msk
			 * subflow, but don't link it yet into conn_list
			 */
			WRITE_ONCE(mptcp_sk(new_msk)->first, child);

			/* new mpc subflow takes ownership of the newly
			 * created mptcp socket
			 */
			new_msk->sk_destruct = mptcp_sock_destruct;
			mptcp_sk(new_msk)->setsockopt_seq = ctx->setsockopt_seq;
			mptcp_pm_new_connection(mptcp_sk(new_msk), child, 1);
			mptcp_token_accept(subflow_req, mptcp_sk(new_msk));
			ctx->conn = new_msk;
			new_msk = NULL;

			/* with OoO packets we can reach here without ingress
			 * mpc option
			 */
			if (mp_opt.mp_capable)
				mptcp_subflow_fully_established(ctx, &mp_opt);
		} else if (ctx->mp_join) {
			struct mptcp_sock *owner;

			owner = subflow_req->msk;
			if (!owner) {
				subflow_add_reset_reason(skb, MPTCP_RST_EPROHIBIT);
				goto dispose_child;
			}

			/* move the msk reference ownership to the subflow */
			subflow_req->msk = NULL;
			ctx->conn = (struct sock *)owner;

			if (subflow_use_different_sport(owner, sk)) {
				pr_debug("ack inet_sport=%d %d",
					 ntohs(inet_sk(sk)->inet_sport),
					 ntohs(inet_sk((struct sock *)owner)->inet_sport));
				if (!mptcp_pm_sport_in_anno_list(owner, sk)) {
					SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_MISMATCHPORTACKRX);
					goto dispose_child;
				}
				SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINPORTACKRX);
			}

			if (!mptcp_finish_join(child))
				goto dispose_child;

			SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINACKRX);
			tcp_rsk(req)->drop_req = true;
		}
	}

out:
	/* dispose of the left over mptcp master, if any */
	if (unlikely(new_msk))
		mptcp_force_close(new_msk);

	/* check for expected invariant - should never trigger, just help
	 * catching eariler subtle bugs
	 */
	WARN_ON_ONCE(child && *own_req && tcp_sk(child)->is_mptcp &&
		     (!mptcp_subflow_ctx(child) ||
		      !mptcp_subflow_ctx(child)->conn));
	return child;

dispose_child:
	subflow_drop_ctx(child);
	tcp_rsk(req)->drop_req = true;
	inet_csk_prepare_for_destroy_sock(child);
	tcp_done(child);
	req->rsk_ops->send_reset(sk, skb);

	/* The last child reference will be released by the caller */
	return child;
}

static struct inet_connection_sock_af_ops subflow_specific;
static struct proto tcp_prot_override;

enum mapping_status {
	MAPPING_OK,
	MAPPING_INVALID,
	MAPPING_EMPTY,
	MAPPING_DATA_FIN,
	MAPPING_DUMMY
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

static enum mapping_status get_mapping_status(struct sock *ssk,
					      struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_ext *mpext;
	struct sk_buff *skb;
	u16 data_len;
	u64 map_seq;

	skb = skb_peek(&ssk->sk_receive_queue);
	if (!skb)
		return MAPPING_EMPTY;

	if (mptcp_check_fallback(ssk))
		return MAPPING_DUMMY;

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

	trace_get_mapping_status(mpext);

	data_len = mpext->data_len;
	if (data_len == 0) {
		MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_INFINITEMAPRX);
		return MAPPING_INVALID;
	}

	if (mpext->data_fin == 1) {
		if (data_len == 1) {
			bool updated = mptcp_update_rcv_data_fin(msk, mpext->data_seq,
								 mpext->dsn64);
			pr_debug("DATA_FIN with no payload seq=%llu", mpext->data_seq);
			if (subflow->map_valid) {
				/* A DATA_FIN might arrive in a DSS
				 * option before the previous mapping
				 * has been fully consumed. Continue
				 * handling the existing mapping.
				 */
				skb_ext_del(skb, SKB_EXT_MPTCP);
				return MAPPING_OK;
			} else {
				if (updated && schedule_work(&msk->work))
					sock_hold((struct sock *)msk);

				return MAPPING_DATA_FIN;
			}
		} else {
			u64 data_fin_seq = mpext->data_seq + data_len - 1;

			/* If mpext->data_seq is a 32-bit value, data_fin_seq
			 * must also be limited to 32 bits.
			 */
			if (!mpext->dsn64)
				data_fin_seq &= GENMASK_ULL(31, 0);

			mptcp_update_rcv_data_fin(msk, data_fin_seq, mpext->dsn64);
			pr_debug("DATA_FIN with mapping seq=%llu dsn64=%d",
				 data_fin_seq, mpext->dsn64);
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
	WRITE_ONCE(mptcp_sk(subflow->conn)->use_64bit_ack, !!mpext->dsn64);

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
		if (skb_is_fully_mapped(ssk, skb)) {
			MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_DSSNOMATCH);
			return MAPPING_INVALID;
		}

		/* will validate the next map after consuming the current one */
		return MAPPING_OK;
	}

	subflow->map_seq = map_seq;
	subflow->map_subflow_seq = mpext->subflow_seq;
	subflow->map_data_len = data_len;
	subflow->map_valid = 1;
	subflow->mpc_map = mpext->mpc_map;
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

static void mptcp_subflow_discard_data(struct sock *ssk, struct sk_buff *skb,
				       u64 limit)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	bool fin = TCP_SKB_CB(skb)->tcp_flags & TCPHDR_FIN;
	u32 incr;

	incr = limit >= skb->len ? skb->len + fin : limit;

	pr_debug("discarding=%d len=%d seq=%d", incr, skb->len,
		 subflow->map_subflow_seq);
	MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_DUPDATA);
	tcp_sk(ssk)->copied_seq += incr;
	if (!before(tcp_sk(ssk)->copied_seq, TCP_SKB_CB(skb)->end_seq))
		sk_eat_skb(ssk, skb);
	if (mptcp_subflow_get_map_offset(subflow) >= subflow->map_data_len)
		subflow->map_valid = 0;
}

/* sched mptcp worker to remove the subflow if no more data is pending */
static void subflow_sched_work_if_closed(struct mptcp_sock *msk, struct sock *ssk)
{
	struct sock *sk = (struct sock *)msk;

	if (likely(ssk->sk_state != TCP_CLOSE))
		return;

	if (skb_queue_empty(&ssk->sk_receive_queue) &&
	    !test_and_set_bit(MPTCP_WORK_CLOSE_SUBFLOW, &msk->flags)) {
		sock_hold(sk);
		if (!schedule_work(&msk->work))
			sock_put(sk);
	}
}

static bool subflow_check_data_avail(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	enum mapping_status status;
	struct mptcp_sock *msk;
	struct sk_buff *skb;

	if (!skb_peek(&ssk->sk_receive_queue))
		subflow->data_avail = 0;
	if (subflow->data_avail)
		return true;

	msk = mptcp_sk(subflow->conn);
	for (;;) {
		u64 ack_seq;
		u64 old_ack;

		status = get_mapping_status(ssk, msk);
		trace_subflow_check_data_avail(status, skb_peek(&ssk->sk_receive_queue));
		if (status == MAPPING_INVALID) {
			ssk->sk_err = EBADMSG;
			goto fatal;
		}
		if (status == MAPPING_DUMMY) {
			__mptcp_do_fallback(msk);
			skb = skb_peek(&ssk->sk_receive_queue);
			subflow->map_valid = 1;
			subflow->map_seq = READ_ONCE(msk->ack_seq);
			subflow->map_data_len = skb->len;
			subflow->map_subflow_seq = tcp_sk(ssk)->copied_seq -
						   subflow->ssn_offset;
			subflow->data_avail = MPTCP_SUBFLOW_DATA_AVAIL;
			return true;
		}

		if (status != MAPPING_OK)
			goto no_data;

		skb = skb_peek(&ssk->sk_receive_queue);
		if (WARN_ON_ONCE(!skb))
			goto no_data;

		/* if msk lacks the remote key, this subflow must provide an
		 * MP_CAPABLE-based mapping
		 */
		if (unlikely(!READ_ONCE(msk->can_ack))) {
			if (!subflow->mpc_map) {
				ssk->sk_err = EBADMSG;
				goto fatal;
			}
			WRITE_ONCE(msk->remote_key, subflow->remote_key);
			WRITE_ONCE(msk->ack_seq, subflow->map_seq);
			WRITE_ONCE(msk->can_ack, true);
		}

		old_ack = READ_ONCE(msk->ack_seq);
		ack_seq = mptcp_subflow_get_mapped_dsn(subflow);
		pr_debug("msk ack_seq=%llx subflow ack_seq=%llx", old_ack,
			 ack_seq);
		if (ack_seq == old_ack) {
			subflow->data_avail = MPTCP_SUBFLOW_DATA_AVAIL;
			break;
		} else if (after64(ack_seq, old_ack)) {
			subflow->data_avail = MPTCP_SUBFLOW_OOO_DATA;
			break;
		}

		/* only accept in-sequence mapping. Old values are spurious
		 * retransmission
		 */
		mptcp_subflow_discard_data(ssk, skb, old_ack - ack_seq);
	}
	return true;

no_data:
	subflow_sched_work_if_closed(msk, ssk);
	return false;
fatal:
	/* fatal protocol error, close the socket */
	/* This barrier is coupled with smp_rmb() in tcp_poll() */
	smp_wmb();
	ssk->sk_error_report(ssk);
	tcp_set_state(ssk, TCP_CLOSE);
	subflow->reset_transient = 0;
	subflow->reset_reason = MPTCP_RST_EMPTCP;
	tcp_send_active_reset(ssk, GFP_ATOMIC);
	subflow->data_avail = 0;
	return false;
}

bool mptcp_subflow_data_available(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	/* check if current mapping is still valid */
	if (subflow->map_valid &&
	    mptcp_subflow_get_map_offset(subflow) >= subflow->map_data_len) {
		subflow->map_valid = 0;
		subflow->data_avail = 0;

		pr_debug("Done with mapping: seq=%u data_len=%u",
			 subflow->map_subflow_seq,
			 subflow->map_data_len);
	}

	return subflow_check_data_avail(sk);
}

/* If ssk has an mptcp parent socket, use the mptcp rcvbuf occupancy,
 * not the ssk one.
 *
 * In mptcp, rwin is about the mptcp-level connection data.
 *
 * Data that is still on the ssk rx queue can thus be ignored,
 * as far as mptcp peer is concerned that data is still inflight.
 * DSS ACK is updated when skb is moved to the mptcp rx queue.
 */
void mptcp_space(const struct sock *ssk, int *space, int *full_space)
{
	const struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	const struct sock *sk = subflow->conn;

	*space = __mptcp_space(sk);
	*full_space = tcp_full_space(sk);
}

static void subflow_data_ready(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	u16 state = 1 << inet_sk_state_load(sk);
	struct sock *parent = subflow->conn;
	struct mptcp_sock *msk;

	msk = mptcp_sk(parent);
	if (state & TCPF_LISTEN) {
		/* MPJ subflow are removed from accept queue before reaching here,
		 * avoid stray wakeups
		 */
		if (reqsk_queue_empty(&inet_csk(sk)->icsk_accept_queue))
			return;

		set_bit(MPTCP_DATA_READY, &msk->flags);
		parent->sk_data_ready(parent);
		return;
	}

	WARN_ON_ONCE(!__mptcp_check_fallback(msk) && !subflow->mp_capable &&
		     !subflow->mp_join && !(state & TCPF_CLOSE));

	if (mptcp_subflow_data_available(sk))
		mptcp_data_ready(parent, sk);
}

static void subflow_write_space(struct sock *ssk)
{
	struct sock *sk = mptcp_subflow_ctx(ssk)->conn;

	mptcp_propagate_sndbuf(sk, ssk);
	mptcp_write_space(sk);
}

void __mptcp_error_report(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		int err = sock_error(ssk);

		if (!err)
			continue;

		/* only propagate errors on fallen-back sockets or
		 * on MPC connect
		 */
		if (sk->sk_state != TCP_SYN_SENT && !__mptcp_check_fallback(msk))
			continue;

		inet_sk_state_store(sk, inet_sk_state_load(ssk));
		sk->sk_err = -err;

		/* This barrier is coupled with smp_rmb() in mptcp_poll() */
		smp_wmb();
		sk->sk_error_report(sk);
		break;
	}
}

static void subflow_error_report(struct sock *ssk)
{
	struct sock *sk = mptcp_subflow_ctx(ssk)->conn;

	mptcp_data_lock(sk);
	if (!sock_owned_by_user(sk))
		__mptcp_error_report(sk);
	else
		set_bit(MPTCP_ERROR_REPORT,  &mptcp_sk(sk)->flags);
	mptcp_data_unlock(sk);
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

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
void mptcpv6_handle_mapped(struct sock *sk, bool mapped)
{
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
}
#endif

void mptcp_info2sockaddr(const struct mptcp_addr_info *info,
			 struct sockaddr_storage *addr,
			 unsigned short family)
{
	memset(addr, 0, sizeof(*addr));
	addr->ss_family = family;
	if (addr->ss_family == AF_INET) {
		struct sockaddr_in *in_addr = (struct sockaddr_in *)addr;

		if (info->family == AF_INET)
			in_addr->sin_addr = info->addr;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		else if (ipv6_addr_v4mapped(&info->addr6))
			in_addr->sin_addr.s_addr = info->addr6.s6_addr32[3];
#endif
		in_addr->sin_port = info->port;
	}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (addr->ss_family == AF_INET6) {
		struct sockaddr_in6 *in6_addr = (struct sockaddr_in6 *)addr;

		if (info->family == AF_INET)
			ipv6_addr_set_v4mapped(info->addr.s_addr,
					       &in6_addr->sin6_addr);
		else
			in6_addr->sin6_addr = info->addr6;
		in6_addr->sin6_port = info->port;
	}
#endif
}

int __mptcp_subflow_connect(struct sock *sk, const struct mptcp_addr_info *loc,
			    const struct mptcp_addr_info *remote,
			    u8 flags, int ifindex)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_subflow_context *subflow;
	struct sockaddr_storage addr;
	int remote_id = remote->id;
	int local_id = loc->id;
	struct socket *sf;
	struct sock *ssk;
	u32 remote_token;
	int addrlen;
	int err;

	if (!mptcp_is_fully_established(sk))
		return -ENOTCONN;

	err = mptcp_subflow_create_socket(sk, &sf);
	if (err)
		return err;

	ssk = sf->sk;
	subflow = mptcp_subflow_ctx(ssk);
	do {
		get_random_bytes(&subflow->local_nonce, sizeof(u32));
	} while (!subflow->local_nonce);

	if (!local_id) {
		err = mptcp_pm_get_local_id(msk, (struct sock_common *)ssk);
		if (err < 0)
			goto failed;

		local_id = err;
	}

	subflow->remote_key = msk->remote_key;
	subflow->local_key = msk->local_key;
	subflow->token = msk->token;
	mptcp_info2sockaddr(loc, &addr, ssk->sk_family);

	addrlen = sizeof(struct sockaddr_in);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (addr.ss_family == AF_INET6)
		addrlen = sizeof(struct sockaddr_in6);
#endif
	ssk->sk_bound_dev_if = ifindex;
	err = kernel_bind(sf, (struct sockaddr *)&addr, addrlen);
	if (err)
		goto failed;

	mptcp_crypto_key_sha(subflow->remote_key, &remote_token, NULL);
	pr_debug("msk=%p remote_token=%u local_id=%d remote_id=%d", msk,
		 remote_token, local_id, remote_id);
	subflow->remote_token = remote_token;
	subflow->local_id = local_id;
	subflow->remote_id = remote_id;
	subflow->request_join = 1;
	subflow->request_bkup = !!(flags & MPTCP_PM_ADDR_FLAG_BACKUP);
	mptcp_info2sockaddr(remote, &addr, ssk->sk_family);

	mptcp_add_pending_subflow(msk, subflow);
	mptcp_sockopt_sync(msk, ssk);
	err = kernel_connect(sf, (struct sockaddr *)&addr, addrlen, O_NONBLOCK);
	if (err && err != -EINPROGRESS)
		goto failed_unlink;

	/* discard the subflow socket */
	mptcp_sock_graft(ssk, sk->sk_socket);
	iput(SOCK_INODE(sf));
	return err;

failed_unlink:
	spin_lock_bh(&msk->join_list_lock);
	list_del(&subflow->node);
	spin_unlock_bh(&msk->join_list_lock);
	sock_put(mptcp_subflow_tcp_sock(subflow));

failed:
	subflow->disposable = 1;
	sock_release(sf);
	return err;
}

static void mptcp_attach_cgroup(struct sock *parent, struct sock *child)
{
#ifdef CONFIG_SOCK_CGROUP_DATA
	struct sock_cgroup_data *parent_skcd = &parent->sk_cgrp_data,
				*child_skcd = &child->sk_cgrp_data;

	/* only the additional subflows created by kworkers have to be modified */
	if (cgroup_id(sock_cgroup_ptr(parent_skcd)) !=
	    cgroup_id(sock_cgroup_ptr(child_skcd))) {
#ifdef CONFIG_MEMCG
		struct mem_cgroup *memcg = parent->sk_memcg;

		mem_cgroup_sk_free(child);
		if (memcg && css_tryget(&memcg->css))
			child->sk_memcg = memcg;
#endif /* CONFIG_MEMCG */

		cgroup_sk_free(child_skcd);
		*child_skcd = *parent_skcd;
		cgroup_sk_clone(child_skcd);
	}
#endif /* CONFIG_SOCK_CGROUP_DATA */
}

static void mptcp_subflow_ops_override(struct sock *ssk)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (ssk->sk_prot == &tcpv6_prot)
		ssk->sk_prot = &tcpv6_prot_override;
	else
#endif
		ssk->sk_prot = &tcp_prot_override;
}

static void mptcp_subflow_ops_undo_override(struct sock *ssk)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (ssk->sk_prot == &tcpv6_prot_override)
		ssk->sk_prot = &tcpv6_prot;
	else
#endif
		ssk->sk_prot = &tcp_prot;
}
int mptcp_subflow_create_socket(struct sock *sk, struct socket **new_sock)
{
	struct mptcp_subflow_context *subflow;
	struct net *net = sock_net(sk);
	struct socket *sf;
	int err;

	/* un-accepted server sockets can reach here - on bad configuration
	 * bail early to avoid greater trouble later
	 */
	if (unlikely(!sk->sk_socket))
		return -EINVAL;

	err = sock_create_kern(net, sk->sk_family, SOCK_STREAM, IPPROTO_TCP,
			       &sf);
	if (err)
		return err;

	lock_sock(sf->sk);

	/* the newly created socket has to be in the same cgroup as its parent */
	mptcp_attach_cgroup(sk, sf->sk);

	/* kernel sockets do not by default acquire net ref, but TCP timer
	 * needs it.
	 */
	sf->sk->sk_net_refcnt = 1;
	get_net(net);
#ifdef CONFIG_PROC_FS
	this_cpu_add(*net->core.sock_inuse, 1);
#endif
	err = tcp_set_ulp(sf->sk, "mptcp");
	release_sock(sf->sk);

	if (err) {
		sock_release(sf);
		return err;
	}

	/* the newly created socket really belongs to the owning MPTCP master
	 * socket, even if for additional subflows the allocation is performed
	 * by a kernel workqueue. Adjust inode references, so that the
	 * procfs/diag interaces really show this one belonging to the correct
	 * user.
	 */
	SOCK_INODE(sf)->i_ino = SOCK_INODE(sk->sk_socket)->i_ino;
	SOCK_INODE(sf)->i_uid = SOCK_INODE(sk->sk_socket)->i_uid;
	SOCK_INODE(sf)->i_gid = SOCK_INODE(sk->sk_socket)->i_gid;

	subflow = mptcp_subflow_ctx(sf->sk);
	pr_debug("subflow=%p", subflow);

	*new_sock = sf;
	sock_hold(sk);
	subflow->conn = sk;
	mptcp_subflow_ops_override(sf->sk);

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
	INIT_LIST_HEAD(&ctx->delegated_node);

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
	struct sock *parent = subflow->conn;

	__subflow_state_change(sk);

	if (subflow_simultaneous_connect(sk)) {
		mptcp_propagate_sndbuf(parent, sk);
		mptcp_do_fallback(sk);
		mptcp_rcv_space_init(mptcp_sk(parent), sk);
		pr_fallback(mptcp_sk(parent));
		subflow->conn_finished = 1;
		if (inet_sk_state_load(parent) == TCP_SYN_SENT) {
			inet_sk_state_store(parent, TCP_ESTABLISHED);
			parent->sk_state_change(parent);
		}
	}

	/* as recvmsg() does not acquire the subflow socket for ssk selection
	 * a fin packet carrying a DSS can be unnoticed if we don't trigger
	 * the data available machinery here.
	 */
	if (mptcp_subflow_data_available(sk))
		mptcp_data_ready(parent, sk);

	subflow_sched_work_if_closed(mptcp_sk(parent), sk);

	if (__mptcp_check_fallback(mptcp_sk(parent)) &&
	    !subflow->rx_eof && subflow_is_done(sk)) {
		subflow->rx_eof = 1;
		mptcp_subflow_eof(parent);
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
	ctx->tcp_error_report = sk->sk_error_report;
	sk->sk_data_ready = subflow_data_ready;
	sk->sk_write_space = subflow_write_space;
	sk->sk_state_change = subflow_state_change;
	sk->sk_error_report = subflow_error_report;
out:
	return err;
}

static void subflow_ulp_release(struct sock *ssk)
{
	struct mptcp_subflow_context *ctx = mptcp_subflow_ctx(ssk);
	bool release = true;
	struct sock *sk;

	if (!ctx)
		return;

	sk = ctx->conn;
	if (sk) {
		/* if the msk has been orphaned, keep the ctx
		 * alive, will be freed by __mptcp_close_ssk(),
		 * when the subflow is still unaccepted
		 */
		release = ctx->disposable || list_empty(&ctx->node);
		sock_put(sk);
	}

	mptcp_subflow_ops_undo_override(ssk);
	if (release)
		kfree_rcu(ctx, rcu);
}

static void subflow_ulp_clone(const struct request_sock *req,
			      struct sock *newsk,
			      const gfp_t priority)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_subflow_context *old_ctx = mptcp_subflow_ctx(newsk);
	struct mptcp_subflow_context *new_ctx;

	if (!tcp_rsk(req)->is_mptcp ||
	    (!subflow_req->mp_capable && !subflow_req->mp_join)) {
		subflow_ulp_fallback(newsk, old_ctx);
		return;
	}

	new_ctx = subflow_create_ctx(newsk, priority);
	if (!new_ctx) {
		subflow_ulp_fallback(newsk, old_ctx);
		return;
	}

	new_ctx->conn_finished = 1;
	new_ctx->icsk_af_ops = old_ctx->icsk_af_ops;
	new_ctx->tcp_data_ready = old_ctx->tcp_data_ready;
	new_ctx->tcp_state_change = old_ctx->tcp_state_change;
	new_ctx->tcp_write_space = old_ctx->tcp_write_space;
	new_ctx->tcp_error_report = old_ctx->tcp_error_report;
	new_ctx->rel_write_seq = 1;
	new_ctx->tcp_sock = newsk;

	if (subflow_req->mp_capable) {
		/* see comments in subflow_syn_recv_sock(), MPTCP connection
		 * is fully established only after we receive the remote key
		 */
		new_ctx->mp_capable = 1;
		new_ctx->local_key = subflow_req->local_key;
		new_ctx->token = subflow_req->token;
		new_ctx->ssn_offset = subflow_req->ssn_offset;
		new_ctx->idsn = subflow_req->idsn;
	} else if (subflow_req->mp_join) {
		new_ctx->ssn_offset = subflow_req->ssn_offset;
		new_ctx->mp_join = 1;
		new_ctx->fully_established = 1;
		new_ctx->backup = subflow_req->backup;
		new_ctx->local_id = subflow_req->local_id;
		new_ctx->remote_id = subflow_req->remote_id;
		new_ctx->token = subflow_req->token;
		new_ctx->thmac = subflow_req->thmac;
	}
}

static void tcp_release_cb_override(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);

	if (mptcp_subflow_has_delegated_action(subflow))
		mptcp_subflow_process_delegated(ssk);

	tcp_release_cb(ssk);
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

void __init mptcp_subflow_init(void)
{
	mptcp_subflow_request_sock_ops = tcp_request_sock_ops;
	if (subflow_ops_init(&mptcp_subflow_request_sock_ops) != 0)
		panic("MPTCP: failed to init subflow request sock ops\n");

	subflow_request_sock_ipv4_ops = tcp_request_sock_ipv4_ops;
	subflow_request_sock_ipv4_ops.route_req = subflow_v4_route_req;

	subflow_specific = ipv4_specific;
	subflow_specific.conn_request = subflow_v4_conn_request;
	subflow_specific.syn_recv_sock = subflow_syn_recv_sock;
	subflow_specific.sk_rx_dst_set = subflow_finish_connect;

	tcp_prot_override = tcp_prot;
	tcp_prot_override.release_cb = tcp_release_cb_override;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	subflow_request_sock_ipv6_ops = tcp_request_sock_ipv6_ops;
	subflow_request_sock_ipv6_ops.route_req = subflow_v6_route_req;

	subflow_v6_specific = ipv6_specific;
	subflow_v6_specific.conn_request = subflow_v6_conn_request;
	subflow_v6_specific.syn_recv_sock = subflow_syn_recv_sock;
	subflow_v6_specific.sk_rx_dst_set = subflow_finish_connect;

	subflow_v6m_specific = subflow_v6_specific;
	subflow_v6m_specific.queue_xmit = ipv4_specific.queue_xmit;
	subflow_v6m_specific.send_check = ipv4_specific.send_check;
	subflow_v6m_specific.net_header_len = ipv4_specific.net_header_len;
	subflow_v6m_specific.mtu_reduced = ipv4_specific.mtu_reduced;
	subflow_v6m_specific.net_frag_header_len = 0;

	tcpv6_prot_override = tcpv6_prot;
	tcpv6_prot_override.release_cb = tcp_release_cb_override;
#endif

	mptcp_diag_subflow_init(&subflow_ulp_ops);

	if (tcp_register_ulp(&subflow_ulp_ops) != 0)
		panic("MPTCP: failed to register subflows to ULP\n");
}
