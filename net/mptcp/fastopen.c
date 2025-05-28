// SPDX-License-Identifier: GPL-2.0
/* MPTCP Fast Open Mechanism
 *
 * Copyright (c) 2021-2022, Dmytro SHYTYI
 */

#include "protocol.h"

void mptcp_fastopen_subflow_synack_set_params(struct mptcp_subflow_context *subflow,
					      struct request_sock *req)
{
	struct sock *sk, *ssk;
	struct sk_buff *skb;
	struct tcp_sock *tp;

	/* on early fallback the subflow context is deleted by
	 * subflow_syn_recv_sock()
	 */
	if (!subflow)
		return;

	ssk = subflow->tcp_sock;
	sk = subflow->conn;
	tp = tcp_sk(ssk);

	subflow->is_mptfo = 1;

	skb = skb_peek(&ssk->sk_receive_queue);
	if (WARN_ON_ONCE(!skb))
		return;

	/* dequeue the skb from sk receive queue */
	__skb_unlink(skb, &ssk->sk_receive_queue);
	skb_ext_reset(skb);
	skb_orphan(skb);

	/* We copy the fastopen data, but that don't belong to the mptcp sequence
	 * space, need to offset it in the subflow sequence, see mptcp_subflow_get_map_offset()
	 */
	tp->copied_seq += skb->len;
	subflow->ssn_offset += skb->len;

	/* Only the sequence delta is relevant */
	MPTCP_SKB_CB(skb)->map_seq = -skb->len;
	MPTCP_SKB_CB(skb)->end_seq = 0;
	MPTCP_SKB_CB(skb)->offset = 0;
	MPTCP_SKB_CB(skb)->has_rxtstamp = TCP_SKB_CB(skb)->has_rxtstamp;
	MPTCP_SKB_CB(skb)->cant_coalesce = 1;

	mptcp_data_lock(sk);
	DEBUG_NET_WARN_ON_ONCE(sock_owned_by_user_nocheck(sk));

	skb_set_owner_r(skb, sk);
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	mptcp_sk(sk)->bytes_received += skb->len;

	sk->sk_data_ready(sk);

	mptcp_data_unlock(sk);
}
