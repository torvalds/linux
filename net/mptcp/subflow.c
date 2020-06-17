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
#include <crypto/sha.h>
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
#include "mib.h"

static void SUBFLOW_REQ_INC_STATS(struct request_sock *req,
				  enum linux_mptcp_mib_field field)
{
	MPTCP_INC_STATS(sock_net(req_to_sk(req)), field);
}

static int subflow_rebuild_header(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	int local_id, err = 0;

	if (subflow->request_mptcp && !subflow->token) {
		pr_debug("subflow=%p", sk);
		err = mptcp_token_new_connect(sk);
	} else if (subflow->request_join && !subflow->local_nonce) {
		struct mptcp_sock *msk = (struct mptcp_sock *)subflow->conn;

		pr_debug("subflow=%p", sk);

		do {
			get_random_bytes(&subflow->local_nonce, sizeof(u32));
		} while (!subflow->local_nonce);

		if (subflow->local_id)
			goto out;

		local_id = mptcp_pm_get_local_id(msk, (struct sock_common *)sk);
		if (local_id < 0)
			return -EINVAL;

		subflow->local_id = local_id;
	}

out:
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

static void subflow_generate_hmac(u64 key1, u64 key2, u32 nonce1, u32 nonce2,
				  void *hmac)
{
	u8 msg[8];

	put_unaligned_be32(nonce1, &msg[0]);
	put_unaligned_be32(nonce2, &msg[4]);

	mptcp_crypto_hmac_sha(key1, key2, msg, 8, hmac);
}

/* validate received token and create truncated hmac and nonce for SYN-ACK */
static bool subflow_token_join_request(struct request_sock *req,
				       const struct sk_buff *skb)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	u8 hmac[SHA256_DIGEST_SIZE];
	struct mptcp_sock *msk;
	int local_id;

	msk = mptcp_token_get_sock(subflow_req->token);
	if (!msk) {
		SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINNOTOKEN);
		return false;
	}

	local_id = mptcp_pm_get_local_id(msk, (struct sock_common *)req);
	if (local_id < 0) {
		sock_put((struct sock *)msk);
		return false;
	}
	subflow_req->local_id = local_id;

	get_random_bytes(&subflow_req->local_nonce, sizeof(u32));

	subflow_generate_hmac(msk->local_key, msk->remote_key,
			      subflow_req->local_nonce,
			      subflow_req->remote_nonce, hmac);

	subflow_req->thmac = get_unaligned_be64(hmac);

	sock_put((struct sock *)msk);
	return true;
}

static void subflow_init_req(struct request_sock *req,
			     const struct sock *sk_listener,
			     struct sk_buff *skb)
{
	struct mptcp_subflow_context *listener = mptcp_subflow_ctx(sk_listener);
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct mptcp_options_received mp_opt;

	pr_debug("subflow_req=%p, listener=%p", subflow_req, listener);

	mptcp_get_options(skb, &mp_opt);

	subflow_req->mp_capable = 0;
	subflow_req->mp_join = 0;

#ifdef CONFIG_TCP_MD5SIG
	/* no MPTCP if MD5SIG is enabled on this socket or we may run out of
	 * TCP option space.
	 */
	if (rcu_access_pointer(tcp_sk(sk_listener)->md5sig_info))
		return;
#endif

	if (mp_opt.mp_capable) {
		SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_MPCAPABLEPASSIVE);

		if (mp_opt.mp_join)
			return;
	} else if (mp_opt.mp_join) {
		SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINSYNRX);
	}

	if (mp_opt.mp_capable && listener->request_mptcp) {
		int err;

		err = mptcp_token_new_request(req);
		if (err == 0)
			subflow_req->mp_capable = 1;

		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq;
	} else if (mp_opt.mp_join && listener->request_mptcp) {
		subflow_req->ssn_offset = TCP_SKB_CB(skb)->seq;
		subflow_req->mp_join = 1;
		subflow_req->backup = mp_opt.backup;
		subflow_req->remote_id = mp_opt.join_id;
		subflow_req->token = mp_opt.token;
		subflow_req->remote_nonce = mp_opt.nonce;
		pr_debug("token=%u, remote_nonce=%u", subflow_req->token,
			 subflow_req->remote_nonce);
		if (!subflow_token_join_request(req, skb)) {
			subflow_req->mp_join = 0;
			// @@ need to trigger RST
		}
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

static void subflow_finish_connect(struct sock *sk, const struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_options_received mp_opt;
	struct sock *parent = subflow->conn;
	struct tcp_sock *tp = tcp_sk(sk);

	subflow->icsk_af_ops->sk_rx_dst_set(sk, skb);

	if (inet_sk_state_load(parent) == TCP_SYN_SENT) {
		inet_sk_state_store(parent, TCP_ESTABLISHED);
		parent->sk_state_change(parent);
	}

	/* be sure no special action on any packet other than syn-ack */
	if (subflow->conn_finished)
		return;

	subflow->conn_finished = 1;

	mptcp_get_options(skb, &mp_opt);
	if (subflow->request_mptcp && mp_opt.mp_capable) {
		subflow->mp_capable = 1;
		subflow->can_ack = 1;
		subflow->remote_key = mp_opt.sndr_key;
		pr_debug("subflow=%p, remote_key=%llu", subflow,
			 subflow->remote_key);
	} else if (subflow->request_join && mp_opt.mp_join) {
		subflow->mp_join = 1;
		subflow->thmac = mp_opt.thmac;
		subflow->remote_nonce = mp_opt.nonce;
		pr_debug("subflow=%p, thmac=%llu, remote_nonce=%u", subflow,
			 subflow->thmac, subflow->remote_nonce);
	} else if (subflow->request_mptcp) {
		tp->is_mptcp = 0;
	}

	if (!tp->is_mptcp)
		return;

	if (subflow->mp_capable) {
		pr_debug("subflow=%p, remote_key=%llu", mptcp_subflow_ctx(sk),
			 subflow->remote_key);
		mptcp_finish_connect(sk);

		if (skb) {
			pr_debug("synack seq=%u", TCP_SKB_CB(skb)->seq);
			subflow->ssn_offset = TCP_SKB_CB(skb)->seq;
		}
	} else if (subflow->mp_join) {
		u8 hmac[SHA256_DIGEST_SIZE];

		pr_debug("subflow=%p, thmac=%llu, remote_nonce=%u",
			 subflow, subflow->thmac,
			 subflow->remote_nonce);
		if (!subflow_thmac_valid(subflow)) {
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_JOINACKMAC);
			subflow->mp_join = 0;
			goto do_reset;
		}

		subflow_generate_hmac(subflow->local_key, subflow->remote_key,
				      subflow->local_nonce,
				      subflow->remote_nonce,
				      hmac);

		memcpy(subflow->hmac, hmac, MPTCPOPT_HMAC_LEN);

		if (skb)
			subflow->ssn_offset = TCP_SKB_CB(skb)->seq;

		if (!mptcp_finish_join(sk))
			goto do_reset;

		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_JOINSYNACKRX);
	} else {
do_reset:
		tcp_send_active_reset(sk, GFP_ATOMIC);
		tcp_done(sk);
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

/* validate hmac received in third ACK */
static bool subflow_hmac_valid(const struct request_sock *req,
			       const struct mptcp_options_received *mp_opt)
{
	const struct mptcp_subflow_request_sock *subflow_req;
	u8 hmac[SHA256_DIGEST_SIZE];
	struct mptcp_sock *msk;
	bool ret;

	subflow_req = mptcp_subflow_rsk(req);
	msk = mptcp_token_get_sock(subflow_req->token);
	if (!msk)
		return false;

	subflow_generate_hmac(msk->remote_key, msk->local_key,
			      subflow_req->remote_nonce,
			      subflow_req->local_nonce, hmac);

	ret = true;
	if (crypto_memneq(hmac, mp_opt->hmac, MPTCPOPT_HMAC_LEN))
		ret = false;

	sock_put((struct sock *)msk);
	return ret;
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
	 * Problem is that the mptcp socket will not be in
	 * SYN_RECV state and doesn't have SOCK_DEAD flag.
	 * Both result in warnings from inet_sock_destruct.
	 */

	if (sk->sk_state == TCP_SYN_RECV) {
		sk->sk_state = TCP_CLOSE;
		WARN_ON_ONCE(sk->sk_socket);
		sock_orphan(sk);
	}

	mptcp_token_destroy(mptcp_sk(sk)->token);
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
	bool fallback_is_fatal = false;
	struct sock *new_msk = NULL;
	bool fallback = false;
	struct sock *child;

	pr_debug("listener=%p, req=%p, conn=%p", listener, req, listener->conn);

	/* we need later a valid 'mp_capable' value even when options are not
	 * parsed
	 */
	mp_opt.mp_capable = 0;
	if (tcp_rsk(req)->is_mptcp == 0)
		goto create_child;

	/* if the sk is MP_CAPABLE, we try to fetch the client key */
	subflow_req = mptcp_subflow_rsk(req);
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
		fallback_is_fatal = true;
		mptcp_get_options(skb, &mp_opt);
		if (!mp_opt.mp_join ||
		    !subflow_hmac_valid(req, &mp_opt)) {
			SUBFLOW_REQ_INC_STATS(req, MPTCP_MIB_JOINACKMAC);
			return NULL;
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
			if (fallback_is_fatal)
				goto dispose_child;

			subflow_drop_ctx(child);
			goto out;
		}

		if (ctx->mp_capable) {
			/* new mpc subflow takes ownership of the newly
			 * created mptcp socket
			 */
			new_msk->sk_destruct = mptcp_sock_destruct;
			mptcp_pm_new_connection(mptcp_sk(new_msk), 1);
			ctx->conn = new_msk;
			new_msk = NULL;

			/* with OoO packets we can reach here without ingress
			 * mpc option
			 */
			ctx->remote_key = mp_opt.sndr_key;
			ctx->fully_established = mp_opt.mp_capable;
			ctx->can_ack = mp_opt.mp_capable;
		} else if (ctx->mp_join) {
			struct mptcp_sock *owner;

			owner = mptcp_token_get_sock(ctx->token);
			if (!owner)
				goto dispose_child;

			ctx->conn = (struct sock *)owner;
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
	tcp_send_active_reset(child, GFP_ATOMIC);
	inet_csk_prepare_for_destroy_sock(child);
	tcp_done(child);

	/* The last child reference will be released by the caller */
	return child;
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
		MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_INFINITEMAPRX);
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
		subflow->use_64bit_ack = 0;
		pr_debug("expanded seq=%llu", subflow->map_seq);
	} else {
		map_seq = mpext->data_seq;
		subflow->use_64bit_ack = 1;
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

static int subflow_read_actor(read_descriptor_t *desc,
			      struct sk_buff *skb,
			      unsigned int offset, size_t len)
{
	size_t copy_len = min(desc->count, len);

	desc->count -= copy_len;

	pr_debug("flushed %zu bytes, %zu left", copy_len, desc->count);
	return copy_len;
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
			read_descriptor_t desc = {
				.count = delta,
			};
			int ret;

			ret = tcp_read_sock(ssk, &desc, subflow_read_actor);
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

/* If ssk has an mptcp parent socket, use the mptcp rcvbuf occupancy,
 * not the ssk one.
 *
 * In mptcp, rwin is about the mptcp-level connection data.
 *
 * Data that is still on the ssk rx queue can thus be ignored,
 * as far as mptcp peer is concerened that data is still inflight.
 * DSS ACK is updated when skb is moved to the mptcp rx queue.
 */
void mptcp_space(const struct sock *ssk, int *space, int *full_space)
{
	const struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	const struct sock *sk = subflow->conn;

	*space = tcp_space(sk);
	*full_space = tcp_full_space(sk);
}

static void subflow_data_ready(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sock *parent = subflow->conn;

	if (!subflow->mp_capable && !subflow->mp_join) {
		subflow->tcp_data_ready(sk);

		parent->sk_data_ready(parent);
		return;
	}

	if (mptcp_subflow_data_available(sk))
		mptcp_data_ready(parent, sk);
}

static void subflow_write_space(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sock *parent = subflow->conn;

	sk_stream_write_space(sk);
	if (sk_stream_is_writeable(sk)) {
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

static void mptcp_info2sockaddr(const struct mptcp_addr_info *info,
				struct sockaddr_storage *addr)
{
	memset(addr, 0, sizeof(*addr));
	addr->ss_family = info->family;
	if (addr->ss_family == AF_INET) {
		struct sockaddr_in *in_addr = (struct sockaddr_in *)addr;

		in_addr->sin_addr = info->addr;
		in_addr->sin_port = info->port;
	}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (addr->ss_family == AF_INET6) {
		struct sockaddr_in6 *in6_addr = (struct sockaddr_in6 *)addr;

		in6_addr->sin6_addr = info->addr6;
		in6_addr->sin6_port = info->port;
	}
#endif
}

int __mptcp_subflow_connect(struct sock *sk, int ifindex,
			    const struct mptcp_addr_info *loc,
			    const struct mptcp_addr_info *remote)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_subflow_context *subflow;
	struct sockaddr_storage addr;
	struct socket *sf;
	u32 remote_token;
	int addrlen;
	int err;

	if (sk->sk_state != TCP_ESTABLISHED)
		return -ENOTCONN;

	err = mptcp_subflow_create_socket(sk, &sf);
	if (err)
		return err;

	subflow = mptcp_subflow_ctx(sf->sk);
	subflow->remote_key = msk->remote_key;
	subflow->local_key = msk->local_key;
	subflow->token = msk->token;
	mptcp_info2sockaddr(loc, &addr);

	addrlen = sizeof(struct sockaddr_in);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (loc->family == AF_INET6)
		addrlen = sizeof(struct sockaddr_in6);
#endif
	sf->sk->sk_bound_dev_if = ifindex;
	err = kernel_bind(sf, (struct sockaddr *)&addr, addrlen);
	if (err)
		goto failed;

	mptcp_crypto_key_sha(subflow->remote_key, &remote_token, NULL);
	pr_debug("msk=%p remote_token=%u", msk, remote_token);
	subflow->remote_token = remote_token;
	subflow->local_id = loc->id;
	subflow->request_join = 1;
	subflow->request_bkup = 1;
	mptcp_info2sockaddr(remote, &addr);

	err = kernel_connect(sf, (struct sockaddr *)&addr, addrlen, O_NONBLOCK);
	if (err && err != -EINPROGRESS)
		goto failed;

	spin_lock_bh(&msk->join_list_lock);
	list_add_tail(&subflow->node, &msk->join_list);
	spin_unlock_bh(&msk->join_list_lock);

	return err;

failed:
	sock_release(sf);
	return err;
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
#ifdef CONFIG_PROC_FS
	this_cpu_add(*net->core.sock_inuse, 1);
#endif
	err = tcp_set_ulp(sf->sk, "mptcp");
	release_sock(sf->sk);

	if (err)
		return err;

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
	struct sock *parent = subflow->conn;

	__subflow_state_change(sk);

	/* as recvmsg() does not acquire the subflow socket for ssk selection
	 * a fin packet carrying a DSS can be unnoticed if we don't trigger
	 * the data available machinery here.
	 */
	if (subflow->mp_capable && mptcp_subflow_data_available(sk))
		mptcp_data_ready(parent, sk);

	if (!(parent->sk_shutdown & RCV_SHUTDOWN) &&
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
		new_ctx->token = subflow_req->token;
		new_ctx->thmac = subflow_req->thmac;
	}
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

	mptcp_diag_subflow_init(&subflow_ulp_ops);

	if (tcp_register_ulp(&subflow_ulp_ops) != 0)
		panic("MPTCP: failed to register subflows to ULP\n");
}
