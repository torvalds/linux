// SPDX-License-Identifier: GPL-2.0
/* MPTCP Fast Open Mechanism
 *
 * Copyright (c) 2021-2022, Dmytro SHYTYI
 */

#include "protocol.h"

void mptcp_fastopen_gen_msk_ackseq(struct mptcp_sock *msk, struct mptcp_subflow_context *subflow,
				   const struct mptcp_options_received *mp_opt)
{
	struct sock *sk = (struct sock *)msk;
	struct sk_buff *skb;

	mptcp_data_lock(sk);
	skb = skb_peek_tail(&sk->sk_receive_queue);
	if (skb) {
		WARN_ON_ONCE(MPTCP_SKB_CB(skb)->end_seq);
		pr_debug("msk %p moving seq %llx -> %llx end_seq %llx -> %llx", sk,
			 MPTCP_SKB_CB(skb)->map_seq, MPTCP_SKB_CB(skb)->map_seq + msk->ack_seq,
			 MPTCP_SKB_CB(skb)->end_seq, MPTCP_SKB_CB(skb)->end_seq + msk->ack_seq);
		MPTCP_SKB_CB(skb)->map_seq += msk->ack_seq;
		MPTCP_SKB_CB(skb)->end_seq += msk->ack_seq;
	}

	pr_debug("msk=%p ack_seq=%llx", msk, msk->ack_seq);
	mptcp_data_unlock(sk);
}
