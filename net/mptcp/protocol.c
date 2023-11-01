// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/tcp_states.h>
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
#include <net/transp_v6.h>
#endif
#include <net/mptcp.h>
#include <net/xfrm.h>
#include <asm/ioctls.h>
#include "protocol.h"
#include "mib.h"

#define CREATE_TRACE_POINTS
#include <trace/events/mptcp.h>

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
struct mptcp6_sock {
	struct mptcp_sock msk;
	struct ipv6_pinfo np;
};
#endif

enum {
	MPTCP_CMSG_TS = BIT(0),
	MPTCP_CMSG_INQ = BIT(1),
};

static struct percpu_counter mptcp_sockets_allocated ____cacheline_aligned_in_smp;

static void __mptcp_destroy_sock(struct sock *sk);
static void mptcp_check_send_data_fin(struct sock *sk);

DEFINE_PER_CPU(struct mptcp_delegated_action, mptcp_delegated_actions);
static struct net_device mptcp_napi_dev;

/* Returns end sequence number of the receiver's advertised window */
static u64 mptcp_wnd_end(const struct mptcp_sock *msk)
{
	return READ_ONCE(msk->wnd_end);
}

static bool mptcp_is_tcpsk(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;

	if (unlikely(sk->sk_prot == &tcp_prot)) {
		/* we are being invoked after mptcp_accept() has
		 * accepted a non-mp-capable flow: sk is a tcp_sk,
		 * not an mptcp one.
		 *
		 * Hand the socket over to tcp so all further socket ops
		 * bypass mptcp.
		 */
		WRITE_ONCE(sock->ops, &inet_stream_ops);
		return true;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	} else if (unlikely(sk->sk_prot == &tcpv6_prot)) {
		WRITE_ONCE(sock->ops, &inet6_stream_ops);
		return true;
#endif
	}

	return false;
}

static int __mptcp_socket_create(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	struct socket *ssock;
	int err;

	err = mptcp_subflow_create_socket(sk, sk->sk_family, &ssock);
	if (err)
		return err;

	msk->scaling_ratio = tcp_sk(ssock->sk)->scaling_ratio;
	WRITE_ONCE(msk->first, ssock->sk);
	subflow = mptcp_subflow_ctx(ssock->sk);
	list_add(&subflow->node, &msk->conn_list);
	sock_hold(ssock->sk);
	subflow->request_mptcp = 1;
	subflow->subflow_id = msk->subflow_id++;

	/* This is the first subflow, always with id 0 */
	subflow->local_id_valid = 1;
	mptcp_sock_graft(msk->first, sk->sk_socket);
	iput(SOCK_INODE(ssock));

	return 0;
}

/* If the MPC handshake is not started, returns the first subflow,
 * eventually allocating it.
 */
struct sock *__mptcp_nmpc_sk(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;
	int ret;

	if (!((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN)))
		return ERR_PTR(-EINVAL);

	if (!msk->first) {
		ret = __mptcp_socket_create(msk);
		if (ret)
			return ERR_PTR(ret);
	}

	return msk->first;
}

static void mptcp_drop(struct sock *sk, struct sk_buff *skb)
{
	sk_drops_add(sk, skb);
	__kfree_skb(skb);
}

static void mptcp_rmem_fwd_alloc_add(struct sock *sk, int size)
{
	WRITE_ONCE(mptcp_sk(sk)->rmem_fwd_alloc,
		   mptcp_sk(sk)->rmem_fwd_alloc + size);
}

static void mptcp_rmem_charge(struct sock *sk, int size)
{
	mptcp_rmem_fwd_alloc_add(sk, -size);
}

static bool mptcp_try_coalesce(struct sock *sk, struct sk_buff *to,
			       struct sk_buff *from)
{
	bool fragstolen;
	int delta;

	if (MPTCP_SKB_CB(from)->offset ||
	    !skb_try_coalesce(to, from, &fragstolen, &delta))
		return false;

	pr_debug("colesced seq %llx into %llx new len %d new end seq %llx",
		 MPTCP_SKB_CB(from)->map_seq, MPTCP_SKB_CB(to)->map_seq,
		 to->len, MPTCP_SKB_CB(from)->end_seq);
	MPTCP_SKB_CB(to)->end_seq = MPTCP_SKB_CB(from)->end_seq;

	/* note the fwd memory can reach a negative value after accounting
	 * for the delta, but the later skb free will restore a non
	 * negative one
	 */
	atomic_add(delta, &sk->sk_rmem_alloc);
	mptcp_rmem_charge(sk, delta);
	kfree_skb_partial(from, fragstolen);

	return true;
}

static bool mptcp_ooo_try_coalesce(struct mptcp_sock *msk, struct sk_buff *to,
				   struct sk_buff *from)
{
	if (MPTCP_SKB_CB(from)->map_seq != MPTCP_SKB_CB(to)->end_seq)
		return false;

	return mptcp_try_coalesce((struct sock *)msk, to, from);
}

static void __mptcp_rmem_reclaim(struct sock *sk, int amount)
{
	amount >>= PAGE_SHIFT;
	mptcp_rmem_charge(sk, amount << PAGE_SHIFT);
	__sk_mem_reduce_allocated(sk, amount);
}

static void mptcp_rmem_uncharge(struct sock *sk, int size)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	int reclaimable;

	mptcp_rmem_fwd_alloc_add(sk, size);
	reclaimable = msk->rmem_fwd_alloc - sk_unused_reserved_mem(sk);

	/* see sk_mem_uncharge() for the rationale behind the following schema */
	if (unlikely(reclaimable >= PAGE_SIZE))
		__mptcp_rmem_reclaim(sk, reclaimable);
}

static void mptcp_rfree(struct sk_buff *skb)
{
	unsigned int len = skb->truesize;
	struct sock *sk = skb->sk;

	atomic_sub(len, &sk->sk_rmem_alloc);
	mptcp_rmem_uncharge(sk, len);
}

void mptcp_set_owner_r(struct sk_buff *skb, struct sock *sk)
{
	skb_orphan(skb);
	skb->sk = sk;
	skb->destructor = mptcp_rfree;
	atomic_add(skb->truesize, &sk->sk_rmem_alloc);
	mptcp_rmem_charge(sk, skb->truesize);
}

/* "inspired" by tcp_data_queue_ofo(), main differences:
 * - use mptcp seqs
 * - don't cope with sacks
 */
static void mptcp_data_queue_ofo(struct mptcp_sock *msk, struct sk_buff *skb)
{
	struct sock *sk = (struct sock *)msk;
	struct rb_node **p, *parent;
	u64 seq, end_seq, max_seq;
	struct sk_buff *skb1;

	seq = MPTCP_SKB_CB(skb)->map_seq;
	end_seq = MPTCP_SKB_CB(skb)->end_seq;
	max_seq = atomic64_read(&msk->rcv_wnd_sent);

	pr_debug("msk=%p seq=%llx limit=%llx empty=%d", msk, seq, max_seq,
		 RB_EMPTY_ROOT(&msk->out_of_order_queue));
	if (after64(end_seq, max_seq)) {
		/* out of window */
		mptcp_drop(sk, skb);
		pr_debug("oow by %lld, rcv_wnd_sent %llu\n",
			 (unsigned long long)end_seq - (unsigned long)max_seq,
			 (unsigned long long)atomic64_read(&msk->rcv_wnd_sent));
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_NODSSWINDOW);
		return;
	}

	p = &msk->out_of_order_queue.rb_node;
	MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_OFOQUEUE);
	if (RB_EMPTY_ROOT(&msk->out_of_order_queue)) {
		rb_link_node(&skb->rbnode, NULL, p);
		rb_insert_color(&skb->rbnode, &msk->out_of_order_queue);
		msk->ooo_last_skb = skb;
		goto end;
	}

	/* with 2 subflows, adding at end of ooo queue is quite likely
	 * Use of ooo_last_skb avoids the O(Log(N)) rbtree lookup.
	 */
	if (mptcp_ooo_try_coalesce(msk, msk->ooo_last_skb, skb)) {
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_OFOMERGE);
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_OFOQUEUETAIL);
		return;
	}

	/* Can avoid an rbtree lookup if we are adding skb after ooo_last_skb */
	if (!before64(seq, MPTCP_SKB_CB(msk->ooo_last_skb)->end_seq)) {
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_OFOQUEUETAIL);
		parent = &msk->ooo_last_skb->rbnode;
		p = &parent->rb_right;
		goto insert;
	}

	/* Find place to insert this segment. Handle overlaps on the way. */
	parent = NULL;
	while (*p) {
		parent = *p;
		skb1 = rb_to_skb(parent);
		if (before64(seq, MPTCP_SKB_CB(skb1)->map_seq)) {
			p = &parent->rb_left;
			continue;
		}
		if (before64(seq, MPTCP_SKB_CB(skb1)->end_seq)) {
			if (!after64(end_seq, MPTCP_SKB_CB(skb1)->end_seq)) {
				/* All the bits are present. Drop. */
				mptcp_drop(sk, skb);
				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_DUPDATA);
				return;
			}
			if (after64(seq, MPTCP_SKB_CB(skb1)->map_seq)) {
				/* partial overlap:
				 *     |     skb      |
				 *  |     skb1    |
				 * continue traversing
				 */
			} else {
				/* skb's seq == skb1's seq and skb covers skb1.
				 * Replace skb1 with skb.
				 */
				rb_replace_node(&skb1->rbnode, &skb->rbnode,
						&msk->out_of_order_queue);
				mptcp_drop(sk, skb1);
				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_DUPDATA);
				goto merge_right;
			}
		} else if (mptcp_ooo_try_coalesce(msk, skb1, skb)) {
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_OFOMERGE);
			return;
		}
		p = &parent->rb_right;
	}

insert:
	/* Insert segment into RB tree. */
	rb_link_node(&skb->rbnode, parent, p);
	rb_insert_color(&skb->rbnode, &msk->out_of_order_queue);

merge_right:
	/* Remove other segments covered by skb. */
	while ((skb1 = skb_rb_next(skb)) != NULL) {
		if (before64(end_seq, MPTCP_SKB_CB(skb1)->end_seq))
			break;
		rb_erase(&skb1->rbnode, &msk->out_of_order_queue);
		mptcp_drop(sk, skb1);
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_DUPDATA);
	}
	/* If there is no skb after us, we are the last_skb ! */
	if (!skb1)
		msk->ooo_last_skb = skb;

end:
	skb_condense(skb);
	mptcp_set_owner_r(skb, sk);
}

static bool mptcp_rmem_schedule(struct sock *sk, struct sock *ssk, int size)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	int amt, amount;

	if (size <= msk->rmem_fwd_alloc)
		return true;

	size -= msk->rmem_fwd_alloc;
	amt = sk_mem_pages(size);
	amount = amt << PAGE_SHIFT;
	if (!__sk_mem_raise_allocated(sk, size, amt, SK_MEM_RECV))
		return false;

	mptcp_rmem_fwd_alloc_add(sk, amount);
	return true;
}

static bool __mptcp_move_skb(struct mptcp_sock *msk, struct sock *ssk,
			     struct sk_buff *skb, unsigned int offset,
			     size_t copy_len)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = (struct sock *)msk;
	struct sk_buff *tail;
	bool has_rxtstamp;

	__skb_unlink(skb, &ssk->sk_receive_queue);

	skb_ext_reset(skb);
	skb_orphan(skb);

	/* try to fetch required memory from subflow */
	if (!mptcp_rmem_schedule(sk, ssk, skb->truesize))
		goto drop;

	has_rxtstamp = TCP_SKB_CB(skb)->has_rxtstamp;

	/* the skb map_seq accounts for the skb offset:
	 * mptcp_subflow_get_mapped_dsn() is based on the current tp->copied_seq
	 * value
	 */
	MPTCP_SKB_CB(skb)->map_seq = mptcp_subflow_get_mapped_dsn(subflow);
	MPTCP_SKB_CB(skb)->end_seq = MPTCP_SKB_CB(skb)->map_seq + copy_len;
	MPTCP_SKB_CB(skb)->offset = offset;
	MPTCP_SKB_CB(skb)->has_rxtstamp = has_rxtstamp;

	if (MPTCP_SKB_CB(skb)->map_seq == msk->ack_seq) {
		/* in sequence */
		msk->bytes_received += copy_len;
		WRITE_ONCE(msk->ack_seq, msk->ack_seq + copy_len);
		tail = skb_peek_tail(&sk->sk_receive_queue);
		if (tail && mptcp_try_coalesce(sk, tail, skb))
			return true;

		mptcp_set_owner_r(skb, sk);
		__skb_queue_tail(&sk->sk_receive_queue, skb);
		return true;
	} else if (after64(MPTCP_SKB_CB(skb)->map_seq, msk->ack_seq)) {
		mptcp_data_queue_ofo(msk, skb);
		return false;
	}

	/* old data, keep it simple and drop the whole pkt, sender
	 * will retransmit as needed, if needed.
	 */
	MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_DUPDATA);
drop:
	mptcp_drop(sk, skb);
	return false;
}

static void mptcp_stop_rtx_timer(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	sk_stop_timer(sk, &icsk->icsk_retransmit_timer);
	mptcp_sk(sk)->timer_ival = 0;
}

static void mptcp_close_wake_up(struct sock *sk)
{
	if (sock_flag(sk, SOCK_DEAD))
		return;

	sk->sk_state_change(sk);
	if (sk->sk_shutdown == SHUTDOWN_MASK ||
	    sk->sk_state == TCP_CLOSE)
		sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
	else
		sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
}

static bool mptcp_pending_data_fin_ack(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	return ((1 << sk->sk_state) &
		(TCPF_FIN_WAIT1 | TCPF_CLOSING | TCPF_LAST_ACK)) &&
	       msk->write_seq == READ_ONCE(msk->snd_una);
}

static void mptcp_check_data_fin_ack(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	/* Look for an acknowledged DATA_FIN */
	if (mptcp_pending_data_fin_ack(sk)) {
		WRITE_ONCE(msk->snd_data_fin_enable, 0);

		switch (sk->sk_state) {
		case TCP_FIN_WAIT1:
			inet_sk_state_store(sk, TCP_FIN_WAIT2);
			break;
		case TCP_CLOSING:
		case TCP_LAST_ACK:
			inet_sk_state_store(sk, TCP_CLOSE);
			break;
		}

		mptcp_close_wake_up(sk);
	}
}

static bool mptcp_pending_data_fin(struct sock *sk, u64 *seq)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (READ_ONCE(msk->rcv_data_fin) &&
	    ((1 << sk->sk_state) &
	     (TCPF_ESTABLISHED | TCPF_FIN_WAIT1 | TCPF_FIN_WAIT2))) {
		u64 rcv_data_fin_seq = READ_ONCE(msk->rcv_data_fin_seq);

		if (msk->ack_seq == rcv_data_fin_seq) {
			if (seq)
				*seq = rcv_data_fin_seq;

			return true;
		}
	}

	return false;
}

static void mptcp_set_datafin_timeout(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	u32 retransmits;

	retransmits = min_t(u32, icsk->icsk_retransmits,
			    ilog2(TCP_RTO_MAX / TCP_RTO_MIN));

	mptcp_sk(sk)->timer_ival = TCP_RTO_MIN << retransmits;
}

static void __mptcp_set_timeout(struct sock *sk, long tout)
{
	mptcp_sk(sk)->timer_ival = tout > 0 ? tout : TCP_RTO_MIN;
}

static long mptcp_timeout_from_subflow(const struct mptcp_subflow_context *subflow)
{
	const struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

	return inet_csk(ssk)->icsk_pending && !subflow->stale_count ?
	       inet_csk(ssk)->icsk_timeout - jiffies : 0;
}

static void mptcp_set_timeout(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	long tout = 0;

	mptcp_for_each_subflow(mptcp_sk(sk), subflow)
		tout = max(tout, mptcp_timeout_from_subflow(subflow));
	__mptcp_set_timeout(sk, tout);
}

static inline bool tcp_can_send_ack(const struct sock *ssk)
{
	return !((1 << inet_sk_state_load(ssk)) &
	       (TCPF_SYN_SENT | TCPF_SYN_RECV | TCPF_TIME_WAIT | TCPF_CLOSE | TCPF_LISTEN));
}

void __mptcp_subflow_send_ack(struct sock *ssk)
{
	if (tcp_can_send_ack(ssk))
		tcp_send_ack(ssk);
}

static void mptcp_subflow_send_ack(struct sock *ssk)
{
	bool slow;

	slow = lock_sock_fast(ssk);
	__mptcp_subflow_send_ack(ssk);
	unlock_sock_fast(ssk, slow);
}

static void mptcp_send_ack(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	mptcp_for_each_subflow(msk, subflow)
		mptcp_subflow_send_ack(mptcp_subflow_tcp_sock(subflow));
}

static void mptcp_subflow_cleanup_rbuf(struct sock *ssk)
{
	bool slow;

	slow = lock_sock_fast(ssk);
	if (tcp_can_send_ack(ssk))
		tcp_cleanup_rbuf(ssk, 1);
	unlock_sock_fast(ssk, slow);
}

static bool mptcp_subflow_could_cleanup(const struct sock *ssk, bool rx_empty)
{
	const struct inet_connection_sock *icsk = inet_csk(ssk);
	u8 ack_pending = READ_ONCE(icsk->icsk_ack.pending);
	const struct tcp_sock *tp = tcp_sk(ssk);

	return (ack_pending & ICSK_ACK_SCHED) &&
		((READ_ONCE(tp->rcv_nxt) - READ_ONCE(tp->rcv_wup) >
		  READ_ONCE(icsk->icsk_ack.rcv_mss)) ||
		 (rx_empty && ack_pending &
			      (ICSK_ACK_PUSHED2 | ICSK_ACK_PUSHED)));
}

static void mptcp_cleanup_rbuf(struct mptcp_sock *msk)
{
	int old_space = READ_ONCE(msk->old_wspace);
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	int space =  __mptcp_space(sk);
	bool cleanup, rx_empty;

	cleanup = (space > 0) && (space >= (old_space << 1));
	rx_empty = !__mptcp_rmem(sk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (cleanup || mptcp_subflow_could_cleanup(ssk, rx_empty))
			mptcp_subflow_cleanup_rbuf(ssk);
	}
}

static bool mptcp_check_data_fin(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	u64 rcv_data_fin_seq;
	bool ret = false;

	/* Need to ack a DATA_FIN received from a peer while this side
	 * of the connection is in ESTABLISHED, FIN_WAIT1, or FIN_WAIT2.
	 * msk->rcv_data_fin was set when parsing the incoming options
	 * at the subflow level and the msk lock was not held, so this
	 * is the first opportunity to act on the DATA_FIN and change
	 * the msk state.
	 *
	 * If we are caught up to the sequence number of the incoming
	 * DATA_FIN, send the DATA_ACK now and do state transition.  If
	 * not caught up, do nothing and let the recv code send DATA_ACK
	 * when catching up.
	 */

	if (mptcp_pending_data_fin(sk, &rcv_data_fin_seq)) {
		WRITE_ONCE(msk->ack_seq, msk->ack_seq + 1);
		WRITE_ONCE(msk->rcv_data_fin, 0);

		WRITE_ONCE(sk->sk_shutdown, sk->sk_shutdown | RCV_SHUTDOWN);
		smp_mb__before_atomic(); /* SHUTDOWN must be visible first */

		switch (sk->sk_state) {
		case TCP_ESTABLISHED:
			inet_sk_state_store(sk, TCP_CLOSE_WAIT);
			break;
		case TCP_FIN_WAIT1:
			inet_sk_state_store(sk, TCP_CLOSING);
			break;
		case TCP_FIN_WAIT2:
			inet_sk_state_store(sk, TCP_CLOSE);
			break;
		default:
			/* Other states not expected */
			WARN_ON_ONCE(1);
			break;
		}

		ret = true;
		if (!__mptcp_check_fallback(msk))
			mptcp_send_ack(msk);
		mptcp_close_wake_up(sk);
	}
	return ret;
}

static bool __mptcp_move_skbs_from_subflow(struct mptcp_sock *msk,
					   struct sock *ssk,
					   unsigned int *bytes)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = (struct sock *)msk;
	unsigned int moved = 0;
	bool more_data_avail;
	struct tcp_sock *tp;
	bool done = false;
	int sk_rbuf;

	sk_rbuf = READ_ONCE(sk->sk_rcvbuf);

	if (!(sk->sk_userlocks & SOCK_RCVBUF_LOCK)) {
		int ssk_rbuf = READ_ONCE(ssk->sk_rcvbuf);

		if (unlikely(ssk_rbuf > sk_rbuf)) {
			WRITE_ONCE(sk->sk_rcvbuf, ssk_rbuf);
			sk_rbuf = ssk_rbuf;
		}
	}

	pr_debug("msk=%p ssk=%p", msk, ssk);
	tp = tcp_sk(ssk);
	do {
		u32 map_remaining, offset;
		u32 seq = tp->copied_seq;
		struct sk_buff *skb;
		bool fin;

		/* try to move as much data as available */
		map_remaining = subflow->map_data_len -
				mptcp_subflow_get_map_offset(subflow);

		skb = skb_peek(&ssk->sk_receive_queue);
		if (!skb) {
			/* With racing move_skbs_to_msk() and __mptcp_move_skbs(),
			 * a different CPU can have already processed the pending
			 * data, stop here or we can enter an infinite loop
			 */
			if (!moved)
				done = true;
			break;
		}

		if (__mptcp_check_fallback(msk)) {
			/* Under fallback skbs have no MPTCP extension and TCP could
			 * collapse them between the dummy map creation and the
			 * current dequeue. Be sure to adjust the map size.
			 */
			map_remaining = skb->len;
			subflow->map_data_len = skb->len;
		}

		offset = seq - TCP_SKB_CB(skb)->seq;
		fin = TCP_SKB_CB(skb)->tcp_flags & TCPHDR_FIN;
		if (fin) {
			done = true;
			seq++;
		}

		if (offset < skb->len) {
			size_t len = skb->len - offset;

			if (tp->urg_data)
				done = true;

			if (__mptcp_move_skb(msk, ssk, skb, offset, len))
				moved += len;
			seq += len;

			if (WARN_ON_ONCE(map_remaining < len))
				break;
		} else {
			WARN_ON_ONCE(!fin);
			sk_eat_skb(ssk, skb);
			done = true;
		}

		WRITE_ONCE(tp->copied_seq, seq);
		more_data_avail = mptcp_subflow_data_available(ssk);

		if (atomic_read(&sk->sk_rmem_alloc) > sk_rbuf) {
			done = true;
			break;
		}
	} while (more_data_avail);

	*bytes += moved;
	return done;
}

static bool __mptcp_ofo_queue(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;
	struct sk_buff *skb, *tail;
	bool moved = false;
	struct rb_node *p;
	u64 end_seq;

	p = rb_first(&msk->out_of_order_queue);
	pr_debug("msk=%p empty=%d", msk, RB_EMPTY_ROOT(&msk->out_of_order_queue));
	while (p) {
		skb = rb_to_skb(p);
		if (after64(MPTCP_SKB_CB(skb)->map_seq, msk->ack_seq))
			break;

		p = rb_next(p);
		rb_erase(&skb->rbnode, &msk->out_of_order_queue);

		if (unlikely(!after64(MPTCP_SKB_CB(skb)->end_seq,
				      msk->ack_seq))) {
			mptcp_drop(sk, skb);
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_DUPDATA);
			continue;
		}

		end_seq = MPTCP_SKB_CB(skb)->end_seq;
		tail = skb_peek_tail(&sk->sk_receive_queue);
		if (!tail || !mptcp_ooo_try_coalesce(msk, tail, skb)) {
			int delta = msk->ack_seq - MPTCP_SKB_CB(skb)->map_seq;

			/* skip overlapping data, if any */
			pr_debug("uncoalesced seq=%llx ack seq=%llx delta=%d",
				 MPTCP_SKB_CB(skb)->map_seq, msk->ack_seq,
				 delta);
			MPTCP_SKB_CB(skb)->offset += delta;
			MPTCP_SKB_CB(skb)->map_seq += delta;
			__skb_queue_tail(&sk->sk_receive_queue, skb);
		}
		msk->bytes_received += end_seq - msk->ack_seq;
		msk->ack_seq = end_seq;
		moved = true;
	}
	return moved;
}

static bool __mptcp_subflow_error_report(struct sock *sk, struct sock *ssk)
{
	int err = sock_error(ssk);
	int ssk_state;

	if (!err)
		return false;

	/* only propagate errors on fallen-back sockets or
	 * on MPC connect
	 */
	if (sk->sk_state != TCP_SYN_SENT && !__mptcp_check_fallback(mptcp_sk(sk)))
		return false;

	/* We need to propagate only transition to CLOSE state.
	 * Orphaned socket will see such state change via
	 * subflow_sched_work_if_closed() and that path will properly
	 * destroy the msk as needed.
	 */
	ssk_state = inet_sk_state_load(ssk);
	if (ssk_state == TCP_CLOSE && !sock_flag(sk, SOCK_DEAD))
		inet_sk_state_store(sk, ssk_state);
	WRITE_ONCE(sk->sk_err, -err);

	/* This barrier is coupled with smp_rmb() in mptcp_poll() */
	smp_wmb();
	sk_error_report(sk);
	return true;
}

void __mptcp_error_report(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);

	mptcp_for_each_subflow(msk, subflow)
		if (__mptcp_subflow_error_report(sk, mptcp_subflow_tcp_sock(subflow)))
			break;
}

/* In most cases we will be able to lock the mptcp socket.  If its already
 * owned, we need to defer to the work queue to avoid ABBA deadlock.
 */
static bool move_skbs_to_msk(struct mptcp_sock *msk, struct sock *ssk)
{
	struct sock *sk = (struct sock *)msk;
	unsigned int moved = 0;

	__mptcp_move_skbs_from_subflow(msk, ssk, &moved);
	__mptcp_ofo_queue(msk);
	if (unlikely(ssk->sk_err)) {
		if (!sock_owned_by_user(sk))
			__mptcp_error_report(sk);
		else
			__set_bit(MPTCP_ERROR_REPORT,  &msk->cb_flags);
	}

	/* If the moves have caught up with the DATA_FIN sequence number
	 * it's time to ack the DATA_FIN and change socket state, but
	 * this is not a good place to change state. Let the workqueue
	 * do it.
	 */
	if (mptcp_pending_data_fin(sk, NULL))
		mptcp_schedule_work(sk);
	return moved > 0;
}

void mptcp_data_ready(struct sock *sk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_sock *msk = mptcp_sk(sk);
	int sk_rbuf, ssk_rbuf;

	/* The peer can send data while we are shutting down this
	 * subflow at msk destruction time, but we must avoid enqueuing
	 * more data to the msk receive queue
	 */
	if (unlikely(subflow->disposable))
		return;

	ssk_rbuf = READ_ONCE(ssk->sk_rcvbuf);
	sk_rbuf = READ_ONCE(sk->sk_rcvbuf);
	if (unlikely(ssk_rbuf > sk_rbuf))
		sk_rbuf = ssk_rbuf;

	/* over limit? can't append more skbs to msk, Also, no need to wake-up*/
	if (__mptcp_rmem(sk) > sk_rbuf) {
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_RCVPRUNED);
		return;
	}

	/* Wake-up the reader only for in-sequence data */
	mptcp_data_lock(sk);
	if (move_skbs_to_msk(msk, ssk) && mptcp_epollin_ready(sk))
		sk->sk_data_ready(sk);
	mptcp_data_unlock(sk);
}

static void mptcp_subflow_joined(struct mptcp_sock *msk, struct sock *ssk)
{
	mptcp_subflow_ctx(ssk)->map_seq = READ_ONCE(msk->ack_seq);
	WRITE_ONCE(msk->allow_infinite_fallback, false);
	mptcp_event(MPTCP_EVENT_SUB_ESTABLISHED, msk, ssk, GFP_ATOMIC);
}

static bool __mptcp_finish_join(struct mptcp_sock *msk, struct sock *ssk)
{
	struct sock *sk = (struct sock *)msk;

	if (sk->sk_state != TCP_ESTABLISHED)
		return false;

	/* attach to msk socket only after we are sure we will deal with it
	 * at close time
	 */
	if (sk->sk_socket && !ssk->sk_socket)
		mptcp_sock_graft(ssk, sk->sk_socket);

	mptcp_subflow_ctx(ssk)->subflow_id = msk->subflow_id++;
	mptcp_sockopt_sync_locked(msk, ssk);
	mptcp_subflow_joined(msk, ssk);
	mptcp_stop_tout_timer(sk);
	__mptcp_propagate_sndbuf(sk, ssk);
	return true;
}

static void __mptcp_flush_join_list(struct sock *sk, struct list_head *join_list)
{
	struct mptcp_subflow_context *tmp, *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);

	list_for_each_entry_safe(subflow, tmp, join_list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast(ssk);

		list_move_tail(&subflow->node, &msk->conn_list);
		if (!__mptcp_finish_join(msk, ssk))
			mptcp_subflow_reset(ssk);
		unlock_sock_fast(ssk, slow);
	}
}

static bool mptcp_rtx_timer_pending(struct sock *sk)
{
	return timer_pending(&inet_csk(sk)->icsk_retransmit_timer);
}

static void mptcp_reset_rtx_timer(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	unsigned long tout;

	/* prevent rescheduling on close */
	if (unlikely(inet_sk_state_load(sk) == TCP_CLOSE))
		return;

	tout = mptcp_sk(sk)->timer_ival;
	sk_reset_timer(sk, &icsk->icsk_retransmit_timer, jiffies + tout);
}

bool mptcp_schedule_work(struct sock *sk)
{
	if (inet_sk_state_load(sk) != TCP_CLOSE &&
	    schedule_work(&mptcp_sk(sk)->work)) {
		/* each subflow already holds a reference to the sk, and the
		 * workqueue is invoked by a subflow, so sk can't go away here.
		 */
		sock_hold(sk);
		return true;
	}
	return false;
}

static struct sock *mptcp_subflow_recv_lookup(const struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	msk_owned_by_me(msk);

	mptcp_for_each_subflow(msk, subflow) {
		if (READ_ONCE(subflow->data_avail))
			return mptcp_subflow_tcp_sock(subflow);
	}

	return NULL;
}

static bool mptcp_skb_can_collapse_to(u64 write_seq,
				      const struct sk_buff *skb,
				      const struct mptcp_ext *mpext)
{
	if (!tcp_skb_can_collapse_to(skb))
		return false;

	/* can collapse only if MPTCP level sequence is in order and this
	 * mapping has not been xmitted yet
	 */
	return mpext && mpext->data_seq + mpext->data_len == write_seq &&
	       !mpext->frozen;
}

/* we can append data to the given data frag if:
 * - there is space available in the backing page_frag
 * - the data frag tail matches the current page_frag free offset
 * - the data frag end sequence number matches the current write seq
 */
static bool mptcp_frag_can_collapse_to(const struct mptcp_sock *msk,
				       const struct page_frag *pfrag,
				       const struct mptcp_data_frag *df)
{
	return df && pfrag->page == df->page &&
		pfrag->size - pfrag->offset > 0 &&
		pfrag->offset == (df->offset + df->data_len) &&
		df->data_seq + df->data_len == msk->write_seq;
}

static void dfrag_uncharge(struct sock *sk, int len)
{
	sk_mem_uncharge(sk, len);
	sk_wmem_queued_add(sk, -len);
}

static void dfrag_clear(struct sock *sk, struct mptcp_data_frag *dfrag)
{
	int len = dfrag->data_len + dfrag->overhead;

	list_del(&dfrag->list);
	dfrag_uncharge(sk, len);
	put_page(dfrag->page);
}

static void __mptcp_clean_una(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_data_frag *dtmp, *dfrag;
	u64 snd_una;

	snd_una = msk->snd_una;
	list_for_each_entry_safe(dfrag, dtmp, &msk->rtx_queue, list) {
		if (after64(dfrag->data_seq + dfrag->data_len, snd_una))
			break;

		if (unlikely(dfrag == msk->first_pending)) {
			/* in recovery mode can see ack after the current snd head */
			if (WARN_ON_ONCE(!msk->recovery))
				break;

			WRITE_ONCE(msk->first_pending, mptcp_send_next(sk));
		}

		dfrag_clear(sk, dfrag);
	}

	dfrag = mptcp_rtx_head(sk);
	if (dfrag && after64(snd_una, dfrag->data_seq)) {
		u64 delta = snd_una - dfrag->data_seq;

		/* prevent wrap around in recovery mode */
		if (unlikely(delta > dfrag->already_sent)) {
			if (WARN_ON_ONCE(!msk->recovery))
				goto out;
			if (WARN_ON_ONCE(delta > dfrag->data_len))
				goto out;
			dfrag->already_sent += delta - dfrag->already_sent;
		}

		dfrag->data_seq += delta;
		dfrag->offset += delta;
		dfrag->data_len -= delta;
		dfrag->already_sent -= delta;

		dfrag_uncharge(sk, delta);
	}

	/* all retransmitted data acked, recovery completed */
	if (unlikely(msk->recovery) && after64(msk->snd_una, msk->recovery_snd_nxt))
		msk->recovery = false;

out:
	if (snd_una == READ_ONCE(msk->snd_nxt) &&
	    snd_una == READ_ONCE(msk->write_seq)) {
		if (mptcp_rtx_timer_pending(sk) && !mptcp_data_fin_enabled(msk))
			mptcp_stop_rtx_timer(sk);
	} else {
		mptcp_reset_rtx_timer(sk);
	}
}

static void __mptcp_clean_una_wakeup(struct sock *sk)
{
	lockdep_assert_held_once(&sk->sk_lock.slock);

	__mptcp_clean_una(sk);
	mptcp_write_space(sk);
}

static void mptcp_clean_una_wakeup(struct sock *sk)
{
	mptcp_data_lock(sk);
	__mptcp_clean_una_wakeup(sk);
	mptcp_data_unlock(sk);
}

static void mptcp_enter_memory_pressure(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool first = true;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (first)
			tcp_enter_memory_pressure(ssk);
		sk_stream_moderate_sndbuf(ssk);

		first = false;
	}
	__mptcp_sync_sndbuf(sk);
}

/* ensure we get enough memory for the frag hdr, beyond some minimal amount of
 * data
 */
static bool mptcp_page_frag_refill(struct sock *sk, struct page_frag *pfrag)
{
	if (likely(skb_page_frag_refill(32U + sizeof(struct mptcp_data_frag),
					pfrag, sk->sk_allocation)))
		return true;

	mptcp_enter_memory_pressure(sk);
	return false;
}

static struct mptcp_data_frag *
mptcp_carve_data_frag(const struct mptcp_sock *msk, struct page_frag *pfrag,
		      int orig_offset)
{
	int offset = ALIGN(orig_offset, sizeof(long));
	struct mptcp_data_frag *dfrag;

	dfrag = (struct mptcp_data_frag *)(page_to_virt(pfrag->page) + offset);
	dfrag->data_len = 0;
	dfrag->data_seq = msk->write_seq;
	dfrag->overhead = offset - orig_offset + sizeof(struct mptcp_data_frag);
	dfrag->offset = offset + sizeof(struct mptcp_data_frag);
	dfrag->already_sent = 0;
	dfrag->page = pfrag->page;

	return dfrag;
}

struct mptcp_sendmsg_info {
	int mss_now;
	int size_goal;
	u16 limit;
	u16 sent;
	unsigned int flags;
	bool data_lock_held;
};

static int mptcp_check_allowed_size(const struct mptcp_sock *msk, struct sock *ssk,
				    u64 data_seq, int avail_size)
{
	u64 window_end = mptcp_wnd_end(msk);
	u64 mptcp_snd_wnd;

	if (__mptcp_check_fallback(msk))
		return avail_size;

	mptcp_snd_wnd = window_end - data_seq;
	avail_size = min_t(unsigned int, mptcp_snd_wnd, avail_size);

	if (unlikely(tcp_sk(ssk)->snd_wnd < mptcp_snd_wnd)) {
		tcp_sk(ssk)->snd_wnd = min_t(u64, U32_MAX, mptcp_snd_wnd);
		MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_SNDWNDSHARED);
	}

	return avail_size;
}

static bool __mptcp_add_ext(struct sk_buff *skb, gfp_t gfp)
{
	struct skb_ext *mpext = __skb_ext_alloc(gfp);

	if (!mpext)
		return false;
	__skb_ext_set(skb, SKB_EXT_MPTCP, mpext);
	return true;
}

static struct sk_buff *__mptcp_do_alloc_tx_skb(struct sock *sk, gfp_t gfp)
{
	struct sk_buff *skb;

	skb = alloc_skb_fclone(MAX_TCP_HEADER, gfp);
	if (likely(skb)) {
		if (likely(__mptcp_add_ext(skb, gfp))) {
			skb_reserve(skb, MAX_TCP_HEADER);
			skb->ip_summed = CHECKSUM_PARTIAL;
			INIT_LIST_HEAD(&skb->tcp_tsorted_anchor);
			return skb;
		}
		__kfree_skb(skb);
	} else {
		mptcp_enter_memory_pressure(sk);
	}
	return NULL;
}

static struct sk_buff *__mptcp_alloc_tx_skb(struct sock *sk, struct sock *ssk, gfp_t gfp)
{
	struct sk_buff *skb;

	skb = __mptcp_do_alloc_tx_skb(sk, gfp);
	if (!skb)
		return NULL;

	if (likely(sk_wmem_schedule(ssk, skb->truesize))) {
		tcp_skb_entail(ssk, skb);
		return skb;
	}
	tcp_skb_tsorted_anchor_cleanup(skb);
	kfree_skb(skb);
	return NULL;
}

static struct sk_buff *mptcp_alloc_tx_skb(struct sock *sk, struct sock *ssk, bool data_lock_held)
{
	gfp_t gfp = data_lock_held ? GFP_ATOMIC : sk->sk_allocation;

	return __mptcp_alloc_tx_skb(sk, ssk, gfp);
}

/* note: this always recompute the csum on the whole skb, even
 * if we just appended a single frag. More status info needed
 */
static void mptcp_update_data_checksum(struct sk_buff *skb, int added)
{
	struct mptcp_ext *mpext = mptcp_get_ext(skb);
	__wsum csum = ~csum_unfold(mpext->csum);
	int offset = skb->len - added;

	mpext->csum = csum_fold(csum_block_add(csum, skb_checksum(skb, offset, added, 0), offset));
}

static void mptcp_update_infinite_map(struct mptcp_sock *msk,
				      struct sock *ssk,
				      struct mptcp_ext *mpext)
{
	if (!mpext)
		return;

	mpext->infinite_map = 1;
	mpext->data_len = 0;

	MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_INFINITEMAPTX);
	mptcp_subflow_ctx(ssk)->send_infinite_map = 0;
	pr_fallback(msk);
	mptcp_do_fallback(ssk);
}

static int mptcp_sendmsg_frag(struct sock *sk, struct sock *ssk,
			      struct mptcp_data_frag *dfrag,
			      struct mptcp_sendmsg_info *info)
{
	u64 data_seq = dfrag->data_seq + info->sent;
	int offset = dfrag->offset + info->sent;
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool zero_window_probe = false;
	struct mptcp_ext *mpext = NULL;
	bool can_coalesce = false;
	bool reuse_skb = true;
	struct sk_buff *skb;
	size_t copy;
	int i;

	pr_debug("msk=%p ssk=%p sending dfrag at seq=%llu len=%u already sent=%u",
		 msk, ssk, dfrag->data_seq, dfrag->data_len, info->sent);

	if (WARN_ON_ONCE(info->sent > info->limit ||
			 info->limit > dfrag->data_len))
		return 0;

	if (unlikely(!__tcp_can_send(ssk)))
		return -EAGAIN;

	/* compute send limit */
	info->mss_now = tcp_send_mss(ssk, &info->size_goal, info->flags);
	copy = info->size_goal;

	skb = tcp_write_queue_tail(ssk);
	if (skb && copy > skb->len) {
		/* Limit the write to the size available in the
		 * current skb, if any, so that we create at most a new skb.
		 * Explicitly tells TCP internals to avoid collapsing on later
		 * queue management operation, to avoid breaking the ext <->
		 * SSN association set here
		 */
		mpext = mptcp_get_ext(skb);
		if (!mptcp_skb_can_collapse_to(data_seq, skb, mpext)) {
			TCP_SKB_CB(skb)->eor = 1;
			goto alloc_skb;
		}

		i = skb_shinfo(skb)->nr_frags;
		can_coalesce = skb_can_coalesce(skb, i, dfrag->page, offset);
		if (!can_coalesce && i >= READ_ONCE(sysctl_max_skb_frags)) {
			tcp_mark_push(tcp_sk(ssk), skb);
			goto alloc_skb;
		}

		copy -= skb->len;
	} else {
alloc_skb:
		skb = mptcp_alloc_tx_skb(sk, ssk, info->data_lock_held);
		if (!skb)
			return -ENOMEM;

		i = skb_shinfo(skb)->nr_frags;
		reuse_skb = false;
		mpext = mptcp_get_ext(skb);
	}

	/* Zero window and all data acked? Probe. */
	copy = mptcp_check_allowed_size(msk, ssk, data_seq, copy);
	if (copy == 0) {
		u64 snd_una = READ_ONCE(msk->snd_una);

		if (snd_una != msk->snd_nxt || tcp_write_queue_tail(ssk)) {
			tcp_remove_empty_skb(ssk);
			return 0;
		}

		zero_window_probe = true;
		data_seq = snd_una - 1;
		copy = 1;
	}

	copy = min_t(size_t, copy, info->limit - info->sent);
	if (!sk_wmem_schedule(ssk, copy)) {
		tcp_remove_empty_skb(ssk);
		return -ENOMEM;
	}

	if (can_coalesce) {
		skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], copy);
	} else {
		get_page(dfrag->page);
		skb_fill_page_desc(skb, i, dfrag->page, offset, copy);
	}

	skb->len += copy;
	skb->data_len += copy;
	skb->truesize += copy;
	sk_wmem_queued_add(ssk, copy);
	sk_mem_charge(ssk, copy);
	WRITE_ONCE(tcp_sk(ssk)->write_seq, tcp_sk(ssk)->write_seq + copy);
	TCP_SKB_CB(skb)->end_seq += copy;
	tcp_skb_pcount_set(skb, 0);

	/* on skb reuse we just need to update the DSS len */
	if (reuse_skb) {
		TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_PSH;
		mpext->data_len += copy;
		goto out;
	}

	memset(mpext, 0, sizeof(*mpext));
	mpext->data_seq = data_seq;
	mpext->subflow_seq = mptcp_subflow_ctx(ssk)->rel_write_seq;
	mpext->data_len = copy;
	mpext->use_map = 1;
	mpext->dsn64 = 1;

	pr_debug("data_seq=%llu subflow_seq=%u data_len=%u dsn64=%d",
		 mpext->data_seq, mpext->subflow_seq, mpext->data_len,
		 mpext->dsn64);

	if (zero_window_probe) {
		mptcp_subflow_ctx(ssk)->rel_write_seq += copy;
		mpext->frozen = 1;
		if (READ_ONCE(msk->csum_enabled))
			mptcp_update_data_checksum(skb, copy);
		tcp_push_pending_frames(ssk);
		return 0;
	}
out:
	if (READ_ONCE(msk->csum_enabled))
		mptcp_update_data_checksum(skb, copy);
	if (mptcp_subflow_ctx(ssk)->send_infinite_map)
		mptcp_update_infinite_map(msk, ssk, mpext);
	trace_mptcp_sendmsg_frag(mpext);
	mptcp_subflow_ctx(ssk)->rel_write_seq += copy;
	return copy;
}

#define MPTCP_SEND_BURST_SIZE		((1 << 16) - \
					 sizeof(struct tcphdr) - \
					 MAX_TCP_OPTION_SPACE - \
					 sizeof(struct ipv6hdr) - \
					 sizeof(struct frag_hdr))

struct subflow_send_info {
	struct sock *ssk;
	u64 linger_time;
};

void mptcp_subflow_set_active(struct mptcp_subflow_context *subflow)
{
	if (!subflow->stale)
		return;

	subflow->stale = 0;
	MPTCP_INC_STATS(sock_net(mptcp_subflow_tcp_sock(subflow)), MPTCP_MIB_SUBFLOWRECOVER);
}

bool mptcp_subflow_active(struct mptcp_subflow_context *subflow)
{
	if (unlikely(subflow->stale)) {
		u32 rcv_tstamp = READ_ONCE(tcp_sk(mptcp_subflow_tcp_sock(subflow))->rcv_tstamp);

		if (subflow->stale_rcv_tstamp == rcv_tstamp)
			return false;

		mptcp_subflow_set_active(subflow);
	}
	return __mptcp_subflow_active(subflow);
}

#define SSK_MODE_ACTIVE	0
#define SSK_MODE_BACKUP	1
#define SSK_MODE_MAX	2

/* implement the mptcp packet scheduler;
 * returns the subflow that will transmit the next DSS
 * additionally updates the rtx timeout
 */
struct sock *mptcp_subflow_get_send(struct mptcp_sock *msk)
{
	struct subflow_send_info send_info[SSK_MODE_MAX];
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	u32 pace, burst, wmem;
	int i, nr_active = 0;
	struct sock *ssk;
	u64 linger_time;
	long tout = 0;

	/* pick the subflow with the lower wmem/wspace ratio */
	for (i = 0; i < SSK_MODE_MAX; ++i) {
		send_info[i].ssk = NULL;
		send_info[i].linger_time = -1;
	}

	mptcp_for_each_subflow(msk, subflow) {
		trace_mptcp_subflow_get_send(subflow);
		ssk =  mptcp_subflow_tcp_sock(subflow);
		if (!mptcp_subflow_active(subflow))
			continue;

		tout = max(tout, mptcp_timeout_from_subflow(subflow));
		nr_active += !subflow->backup;
		pace = subflow->avg_pacing_rate;
		if (unlikely(!pace)) {
			/* init pacing rate from socket */
			subflow->avg_pacing_rate = READ_ONCE(ssk->sk_pacing_rate);
			pace = subflow->avg_pacing_rate;
			if (!pace)
				continue;
		}

		linger_time = div_u64((u64)READ_ONCE(ssk->sk_wmem_queued) << 32, pace);
		if (linger_time < send_info[subflow->backup].linger_time) {
			send_info[subflow->backup].ssk = ssk;
			send_info[subflow->backup].linger_time = linger_time;
		}
	}
	__mptcp_set_timeout(sk, tout);

	/* pick the best backup if no other subflow is active */
	if (!nr_active)
		send_info[SSK_MODE_ACTIVE].ssk = send_info[SSK_MODE_BACKUP].ssk;

	/* According to the blest algorithm, to avoid HoL blocking for the
	 * faster flow, we need to:
	 * - estimate the faster flow linger time
	 * - use the above to estimate the amount of byte transferred
	 *   by the faster flow
	 * - check that the amount of queued data is greter than the above,
	 *   otherwise do not use the picked, slower, subflow
	 * We select the subflow with the shorter estimated time to flush
	 * the queued mem, which basically ensure the above. We just need
	 * to check that subflow has a non empty cwin.
	 */
	ssk = send_info[SSK_MODE_ACTIVE].ssk;
	if (!ssk || !sk_stream_memory_free(ssk))
		return NULL;

	burst = min_t(int, MPTCP_SEND_BURST_SIZE, mptcp_wnd_end(msk) - msk->snd_nxt);
	wmem = READ_ONCE(ssk->sk_wmem_queued);
	if (!burst)
		return ssk;

	subflow = mptcp_subflow_ctx(ssk);
	subflow->avg_pacing_rate = div_u64((u64)subflow->avg_pacing_rate * wmem +
					   READ_ONCE(ssk->sk_pacing_rate) * burst,
					   burst + wmem);
	msk->snd_burst = burst;
	return ssk;
}

static void mptcp_push_release(struct sock *ssk, struct mptcp_sendmsg_info *info)
{
	tcp_push(ssk, 0, info->mss_now, tcp_sk(ssk)->nonagle, info->size_goal);
	release_sock(ssk);
}

static void mptcp_update_post_push(struct mptcp_sock *msk,
				   struct mptcp_data_frag *dfrag,
				   u32 sent)
{
	u64 snd_nxt_new = dfrag->data_seq;

	dfrag->already_sent += sent;

	msk->snd_burst -= sent;

	snd_nxt_new += dfrag->already_sent;

	/* snd_nxt_new can be smaller than snd_nxt in case mptcp
	 * is recovering after a failover. In that event, this re-sends
	 * old segments.
	 *
	 * Thus compute snd_nxt_new candidate based on
	 * the dfrag->data_seq that was sent and the data
	 * that has been handed to the subflow for transmission
	 * and skip update in case it was old dfrag.
	 */
	if (likely(after64(snd_nxt_new, msk->snd_nxt))) {
		msk->bytes_sent += snd_nxt_new - msk->snd_nxt;
		msk->snd_nxt = snd_nxt_new;
	}
}

void mptcp_check_and_set_pending(struct sock *sk)
{
	if (mptcp_send_head(sk))
		mptcp_sk(sk)->push_pending |= BIT(MPTCP_PUSH_PENDING);
}

static int __subflow_push_pending(struct sock *sk, struct sock *ssk,
				  struct mptcp_sendmsg_info *info)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_data_frag *dfrag;
	int len, copied = 0, err = 0;

	while ((dfrag = mptcp_send_head(sk))) {
		info->sent = dfrag->already_sent;
		info->limit = dfrag->data_len;
		len = dfrag->data_len - dfrag->already_sent;
		while (len > 0) {
			int ret = 0;

			ret = mptcp_sendmsg_frag(sk, ssk, dfrag, info);
			if (ret <= 0) {
				err = copied ? : ret;
				goto out;
			}

			info->sent += ret;
			copied += ret;
			len -= ret;

			mptcp_update_post_push(msk, dfrag, ret);
		}
		WRITE_ONCE(msk->first_pending, mptcp_send_next(sk));

		if (msk->snd_burst <= 0 ||
		    !sk_stream_memory_free(ssk) ||
		    !mptcp_subflow_active(mptcp_subflow_ctx(ssk))) {
			err = copied;
			goto out;
		}
		mptcp_set_timeout(sk);
	}
	err = copied;

out:
	return err;
}

void __mptcp_push_pending(struct sock *sk, unsigned int flags)
{
	struct sock *prev_ssk = NULL, *ssk = NULL;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_sendmsg_info info = {
				.flags = flags,
	};
	bool do_check_data_fin = false;
	int push_count = 1;

	while (mptcp_send_head(sk) && (push_count > 0)) {
		struct mptcp_subflow_context *subflow;
		int ret = 0;

		if (mptcp_sched_get_send(msk))
			break;

		push_count = 0;

		mptcp_for_each_subflow(msk, subflow) {
			if (READ_ONCE(subflow->scheduled)) {
				mptcp_subflow_set_scheduled(subflow, false);

				prev_ssk = ssk;
				ssk = mptcp_subflow_tcp_sock(subflow);
				if (ssk != prev_ssk) {
					/* First check. If the ssk has changed since
					 * the last round, release prev_ssk
					 */
					if (prev_ssk)
						mptcp_push_release(prev_ssk, &info);

					/* Need to lock the new subflow only if different
					 * from the previous one, otherwise we are still
					 * helding the relevant lock
					 */
					lock_sock(ssk);
				}

				push_count++;

				ret = __subflow_push_pending(sk, ssk, &info);
				if (ret <= 0) {
					if (ret != -EAGAIN ||
					    (1 << ssk->sk_state) &
					     (TCPF_FIN_WAIT1 | TCPF_FIN_WAIT2 | TCPF_CLOSE))
						push_count--;
					continue;
				}
				do_check_data_fin = true;
			}
		}
	}

	/* at this point we held the socket lock for the last subflow we used */
	if (ssk)
		mptcp_push_release(ssk, &info);

	/* ensure the rtx timer is running */
	if (!mptcp_rtx_timer_pending(sk))
		mptcp_reset_rtx_timer(sk);
	if (do_check_data_fin)
		mptcp_check_send_data_fin(sk);
}

static void __mptcp_subflow_push_pending(struct sock *sk, struct sock *ssk, bool first)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_sendmsg_info info = {
		.data_lock_held = true,
	};
	bool keep_pushing = true;
	struct sock *xmit_ssk;
	int copied = 0;

	info.flags = 0;
	while (mptcp_send_head(sk) && keep_pushing) {
		struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
		int ret = 0;

		/* check for a different subflow usage only after
		 * spooling the first chunk of data
		 */
		if (first) {
			mptcp_subflow_set_scheduled(subflow, false);
			ret = __subflow_push_pending(sk, ssk, &info);
			first = false;
			if (ret <= 0)
				break;
			copied += ret;
			continue;
		}

		if (mptcp_sched_get_send(msk))
			goto out;

		if (READ_ONCE(subflow->scheduled)) {
			mptcp_subflow_set_scheduled(subflow, false);
			ret = __subflow_push_pending(sk, ssk, &info);
			if (ret <= 0)
				keep_pushing = false;
			copied += ret;
		}

		mptcp_for_each_subflow(msk, subflow) {
			if (READ_ONCE(subflow->scheduled)) {
				xmit_ssk = mptcp_subflow_tcp_sock(subflow);
				if (xmit_ssk != ssk) {
					mptcp_subflow_delegate(subflow,
							       MPTCP_DELEGATE_SEND);
					keep_pushing = false;
				}
			}
		}
	}

out:
	/* __mptcp_alloc_tx_skb could have released some wmem and we are
	 * not going to flush it via release_sock()
	 */
	if (copied) {
		tcp_push(ssk, 0, info.mss_now, tcp_sk(ssk)->nonagle,
			 info.size_goal);
		if (!mptcp_rtx_timer_pending(sk))
			mptcp_reset_rtx_timer(sk);

		if (msk->snd_data_fin_enable &&
		    msk->snd_nxt + 1 == msk->write_seq)
			mptcp_schedule_work(sk);
	}
}

static void mptcp_set_nospace(struct sock *sk)
{
	/* enable autotune */
	set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);

	/* will be cleared on avail space */
	set_bit(MPTCP_NOSPACE, &mptcp_sk(sk)->flags);
}

static int mptcp_disconnect(struct sock *sk, int flags);

static int mptcp_sendmsg_fastopen(struct sock *sk, struct msghdr *msg,
				  size_t len, int *copied_syn)
{
	unsigned int saved_flags = msg->msg_flags;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;
	int ret;

	/* on flags based fastopen the mptcp is supposed to create the
	 * first subflow right now. Otherwise we are in the defer_connect
	 * path, and the first subflow must be already present.
	 * Since the defer_connect flag is cleared after the first succsful
	 * fastopen attempt, no need to check for additional subflow status.
	 */
	if (msg->msg_flags & MSG_FASTOPEN) {
		ssk = __mptcp_nmpc_sk(msk);
		if (IS_ERR(ssk))
			return PTR_ERR(ssk);
	}
	if (!msk->first)
		return -EINVAL;

	ssk = msk->first;

	lock_sock(ssk);
	msg->msg_flags |= MSG_DONTWAIT;
	msk->fastopening = 1;
	ret = tcp_sendmsg_fastopen(ssk, msg, copied_syn, len, NULL);
	msk->fastopening = 0;
	msg->msg_flags = saved_flags;
	release_sock(ssk);

	/* do the blocking bits of inet_stream_connect outside the ssk socket lock */
	if (ret == -EINPROGRESS && !(msg->msg_flags & MSG_DONTWAIT)) {
		ret = __inet_stream_connect(sk->sk_socket, msg->msg_name,
					    msg->msg_namelen, msg->msg_flags, 1);

		/* Keep the same behaviour of plain TCP: zero the copied bytes in
		 * case of any error, except timeout or signal
		 */
		if (ret && ret != -EINPROGRESS && ret != -ERESTARTSYS && ret != -EINTR)
			*copied_syn = 0;
	} else if (ret && ret != -EINPROGRESS) {
		/* The disconnect() op called by tcp_sendmsg_fastopen()/
		 * __inet_stream_connect() can fail, due to looking check,
		 * see mptcp_disconnect().
		 * Attempt it again outside the problematic scope.
		 */
		if (!mptcp_disconnect(sk, 0))
			sk->sk_socket->state = SS_UNCONNECTED;
	}
	inet_clear_bit(DEFER_CONNECT, sk);

	return ret;
}

static int do_copy_data_nocache(struct sock *sk, int copy,
				struct iov_iter *from, char *to)
{
	if (sk->sk_route_caps & NETIF_F_NOCACHE_COPY) {
		if (!copy_from_iter_full_nocache(to, copy, from))
			return -EFAULT;
	} else if (!copy_from_iter_full(to, copy, from)) {
		return -EFAULT;
	}
	return 0;
}

static int mptcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct page_frag *pfrag;
	size_t copied = 0;
	int ret = 0;
	long timeo;

	/* silently ignore everything else */
	msg->msg_flags &= MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL | MSG_FASTOPEN;

	lock_sock(sk);

	if (unlikely(inet_test_bit(DEFER_CONNECT, sk) ||
		     msg->msg_flags & MSG_FASTOPEN)) {
		int copied_syn = 0;

		ret = mptcp_sendmsg_fastopen(sk, msg, len, &copied_syn);
		copied += copied_syn;
		if (ret == -EINPROGRESS && copied_syn > 0)
			goto out;
		else if (ret)
			goto do_error;
	}

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	if ((1 << sk->sk_state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		ret = sk_stream_wait_connect(sk, &timeo);
		if (ret)
			goto do_error;
	}

	ret = -EPIPE;
	if (unlikely(sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN)))
		goto do_error;

	pfrag = sk_page_frag(sk);

	while (msg_data_left(msg)) {
		int total_ts, frag_truesize = 0;
		struct mptcp_data_frag *dfrag;
		bool dfrag_collapsed;
		size_t psize, offset;

		/* reuse tail pfrag, if possible, or carve a new one from the
		 * page allocator
		 */
		dfrag = mptcp_pending_tail(sk);
		dfrag_collapsed = mptcp_frag_can_collapse_to(msk, pfrag, dfrag);
		if (!dfrag_collapsed) {
			if (!sk_stream_memory_free(sk))
				goto wait_for_memory;

			if (!mptcp_page_frag_refill(sk, pfrag))
				goto wait_for_memory;

			dfrag = mptcp_carve_data_frag(msk, pfrag, pfrag->offset);
			frag_truesize = dfrag->overhead;
		}

		/* we do not bound vs wspace, to allow a single packet.
		 * memory accounting will prevent execessive memory usage
		 * anyway
		 */
		offset = dfrag->offset + dfrag->data_len;
		psize = pfrag->size - offset;
		psize = min_t(size_t, psize, msg_data_left(msg));
		total_ts = psize + frag_truesize;

		if (!sk_wmem_schedule(sk, total_ts))
			goto wait_for_memory;

		ret = do_copy_data_nocache(sk, psize, &msg->msg_iter,
					   page_address(dfrag->page) + offset);
		if (ret)
			goto do_error;

		/* data successfully copied into the write queue */
		sk_forward_alloc_add(sk, -total_ts);
		copied += psize;
		dfrag->data_len += psize;
		frag_truesize += psize;
		pfrag->offset += frag_truesize;
		WRITE_ONCE(msk->write_seq, msk->write_seq + psize);

		/* charge data on mptcp pending queue to the msk socket
		 * Note: we charge such data both to sk and ssk
		 */
		sk_wmem_queued_add(sk, frag_truesize);
		if (!dfrag_collapsed) {
			get_page(dfrag->page);
			list_add_tail(&dfrag->list, &msk->rtx_queue);
			if (!msk->first_pending)
				WRITE_ONCE(msk->first_pending, dfrag);
		}
		pr_debug("msk=%p dfrag at seq=%llu len=%u sent=%u new=%d", msk,
			 dfrag->data_seq, dfrag->data_len, dfrag->already_sent,
			 !dfrag_collapsed);

		continue;

wait_for_memory:
		mptcp_set_nospace(sk);
		__mptcp_push_pending(sk, msg->msg_flags);
		ret = sk_stream_wait_memory(sk, &timeo);
		if (ret)
			goto do_error;
	}

	if (copied)
		__mptcp_push_pending(sk, msg->msg_flags);

out:
	release_sock(sk);
	return copied;

do_error:
	if (copied)
		goto out;

	copied = sk_stream_error(sk, msg->msg_flags, ret);
	goto out;
}

static int __mptcp_recvmsg_mskq(struct mptcp_sock *msk,
				struct msghdr *msg,
				size_t len, int flags,
				struct scm_timestamping_internal *tss,
				int *cmsg_flags)
{
	struct sk_buff *skb, *tmp;
	int copied = 0;

	skb_queue_walk_safe(&msk->receive_queue, skb, tmp) {
		u32 offset = MPTCP_SKB_CB(skb)->offset;
		u32 data_len = skb->len - offset;
		u32 count = min_t(size_t, len - copied, data_len);
		int err;

		if (!(flags & MSG_TRUNC)) {
			err = skb_copy_datagram_msg(skb, offset, msg, count);
			if (unlikely(err < 0)) {
				if (!copied)
					return err;
				break;
			}
		}

		if (MPTCP_SKB_CB(skb)->has_rxtstamp) {
			tcp_update_recv_tstamps(skb, tss);
			*cmsg_flags |= MPTCP_CMSG_TS;
		}

		copied += count;

		if (count < data_len) {
			if (!(flags & MSG_PEEK)) {
				MPTCP_SKB_CB(skb)->offset += count;
				MPTCP_SKB_CB(skb)->map_seq += count;
				msk->bytes_consumed += count;
			}
			break;
		}

		if (!(flags & MSG_PEEK)) {
			/* we will bulk release the skb memory later */
			skb->destructor = NULL;
			WRITE_ONCE(msk->rmem_released, msk->rmem_released + skb->truesize);
			__skb_unlink(skb, &msk->receive_queue);
			__kfree_skb(skb);
			msk->bytes_consumed += count;
		}

		if (copied >= len)
			break;
	}

	return copied;
}

/* receive buffer autotuning.  See tcp_rcv_space_adjust for more information.
 *
 * Only difference: Use highest rtt estimate of the subflows in use.
 */
static void mptcp_rcv_space_adjust(struct mptcp_sock *msk, int copied)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	u8 scaling_ratio = U8_MAX;
	u32 time, advmss = 1;
	u64 rtt_us, mstamp;

	msk_owned_by_me(msk);

	if (copied <= 0)
		return;

	msk->rcvq_space.copied += copied;

	mstamp = div_u64(tcp_clock_ns(), NSEC_PER_USEC);
	time = tcp_stamp_us_delta(mstamp, msk->rcvq_space.time);

	rtt_us = msk->rcvq_space.rtt_us;
	if (rtt_us && time < (rtt_us >> 3))
		return;

	rtt_us = 0;
	mptcp_for_each_subflow(msk, subflow) {
		const struct tcp_sock *tp;
		u64 sf_rtt_us;
		u32 sf_advmss;

		tp = tcp_sk(mptcp_subflow_tcp_sock(subflow));

		sf_rtt_us = READ_ONCE(tp->rcv_rtt_est.rtt_us);
		sf_advmss = READ_ONCE(tp->advmss);

		rtt_us = max(sf_rtt_us, rtt_us);
		advmss = max(sf_advmss, advmss);
		scaling_ratio = min(tp->scaling_ratio, scaling_ratio);
	}

	msk->rcvq_space.rtt_us = rtt_us;
	msk->scaling_ratio = scaling_ratio;
	if (time < (rtt_us >> 3) || rtt_us == 0)
		return;

	if (msk->rcvq_space.copied <= msk->rcvq_space.space)
		goto new_measure;

	if (READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_moderate_rcvbuf) &&
	    !(sk->sk_userlocks & SOCK_RCVBUF_LOCK)) {
		u64 rcvwin, grow;
		int rcvbuf;

		rcvwin = ((u64)msk->rcvq_space.copied << 1) + 16 * advmss;

		grow = rcvwin * (msk->rcvq_space.copied - msk->rcvq_space.space);

		do_div(grow, msk->rcvq_space.space);
		rcvwin += (grow << 1);

		rcvbuf = min_t(u64, __tcp_space_from_win(scaling_ratio, rcvwin),
			       READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_rmem[2]));

		if (rcvbuf > sk->sk_rcvbuf) {
			u32 window_clamp;

			window_clamp = __tcp_win_from_space(scaling_ratio, rcvbuf);
			WRITE_ONCE(sk->sk_rcvbuf, rcvbuf);

			/* Make subflows follow along.  If we do not do this, we
			 * get drops at subflow level if skbs can't be moved to
			 * the mptcp rx queue fast enough (announced rcv_win can
			 * exceed ssk->sk_rcvbuf).
			 */
			mptcp_for_each_subflow(msk, subflow) {
				struct sock *ssk;
				bool slow;

				ssk = mptcp_subflow_tcp_sock(subflow);
				slow = lock_sock_fast(ssk);
				WRITE_ONCE(ssk->sk_rcvbuf, rcvbuf);
				tcp_sk(ssk)->window_clamp = window_clamp;
				tcp_cleanup_rbuf(ssk, 1);
				unlock_sock_fast(ssk, slow);
			}
		}
	}

	msk->rcvq_space.space = msk->rcvq_space.copied;
new_measure:
	msk->rcvq_space.copied = 0;
	msk->rcvq_space.time = mstamp;
}

static void __mptcp_update_rmem(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (!msk->rmem_released)
		return;

	atomic_sub(msk->rmem_released, &sk->sk_rmem_alloc);
	mptcp_rmem_uncharge(sk, msk->rmem_released);
	WRITE_ONCE(msk->rmem_released, 0);
}

static void __mptcp_splice_receive_queue(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	skb_queue_splice_tail_init(&sk->sk_receive_queue, &msk->receive_queue);
}

static bool __mptcp_move_skbs(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;
	unsigned int moved = 0;
	bool ret, done;

	do {
		struct sock *ssk = mptcp_subflow_recv_lookup(msk);
		bool slowpath;

		/* we can have data pending in the subflows only if the msk
		 * receive buffer was full at subflow_data_ready() time,
		 * that is an unlikely slow path.
		 */
		if (likely(!ssk))
			break;

		slowpath = lock_sock_fast(ssk);
		mptcp_data_lock(sk);
		__mptcp_update_rmem(sk);
		done = __mptcp_move_skbs_from_subflow(msk, ssk, &moved);
		mptcp_data_unlock(sk);

		if (unlikely(ssk->sk_err))
			__mptcp_error_report(sk);
		unlock_sock_fast(ssk, slowpath);
	} while (!done);

	/* acquire the data lock only if some input data is pending */
	ret = moved > 0;
	if (!RB_EMPTY_ROOT(&msk->out_of_order_queue) ||
	    !skb_queue_empty_lockless(&sk->sk_receive_queue)) {
		mptcp_data_lock(sk);
		__mptcp_update_rmem(sk);
		ret |= __mptcp_ofo_queue(msk);
		__mptcp_splice_receive_queue(sk);
		mptcp_data_unlock(sk);
	}
	if (ret)
		mptcp_check_data_fin((struct sock *)msk);
	return !skb_queue_empty(&msk->receive_queue);
}

static unsigned int mptcp_inq_hint(const struct sock *sk)
{
	const struct mptcp_sock *msk = mptcp_sk(sk);
	const struct sk_buff *skb;

	skb = skb_peek(&msk->receive_queue);
	if (skb) {
		u64 hint_val = msk->ack_seq - MPTCP_SKB_CB(skb)->map_seq;

		if (hint_val >= INT_MAX)
			return INT_MAX;

		return (unsigned int)hint_val;
	}

	if (sk->sk_state == TCP_CLOSE || (sk->sk_shutdown & RCV_SHUTDOWN))
		return 1;

	return 0;
}

static int mptcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			 int flags, int *addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct scm_timestamping_internal tss;
	int copied = 0, cmsg_flags = 0;
	int target;
	long timeo;

	/* MSG_ERRQUEUE is really a no-op till we support IP_RECVERR */
	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);

	lock_sock(sk);
	if (unlikely(sk->sk_state == TCP_LISTEN)) {
		copied = -ENOTCONN;
		goto out_err;
	}

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	len = min_t(size_t, len, INT_MAX);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	if (unlikely(msk->recvmsg_inq))
		cmsg_flags = MPTCP_CMSG_INQ;

	while (copied < len) {
		int bytes_read;

		bytes_read = __mptcp_recvmsg_mskq(msk, msg, len - copied, flags, &tss, &cmsg_flags);
		if (unlikely(bytes_read < 0)) {
			if (!copied)
				copied = bytes_read;
			goto out_err;
		}

		copied += bytes_read;

		/* be sure to advertise window change */
		mptcp_cleanup_rbuf(msk);

		if (skb_queue_empty(&msk->receive_queue) && __mptcp_move_skbs(msk))
			continue;

		/* only the master socket status is relevant here. The exit
		 * conditions mirror closely tcp_recvmsg()
		 */
		if (copied >= target)
			break;

		if (copied) {
			if (sk->sk_err ||
			    sk->sk_state == TCP_CLOSE ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    !timeo ||
			    signal_pending(current))
				break;
		} else {
			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}

			if (sk->sk_shutdown & RCV_SHUTDOWN) {
				/* race breaker: the shutdown could be after the
				 * previous receive queue check
				 */
				if (__mptcp_move_skbs(msk))
					continue;
				break;
			}

			if (sk->sk_state == TCP_CLOSE) {
				copied = -ENOTCONN;
				break;
			}

			if (!timeo) {
				copied = -EAGAIN;
				break;
			}

			if (signal_pending(current)) {
				copied = sock_intr_errno(timeo);
				break;
			}
		}

		pr_debug("block timeout %ld", timeo);
		sk_wait_data(sk, &timeo, NULL);
	}

out_err:
	if (cmsg_flags && copied >= 0) {
		if (cmsg_flags & MPTCP_CMSG_TS)
			tcp_recv_timestamp(msg, sk, &tss);

		if (cmsg_flags & MPTCP_CMSG_INQ) {
			unsigned int inq = mptcp_inq_hint(sk);

			put_cmsg(msg, SOL_TCP, TCP_CM_INQ, sizeof(inq), &inq);
		}
	}

	pr_debug("msk=%p rx queue empty=%d:%d copied=%d",
		 msk, skb_queue_empty_lockless(&sk->sk_receive_queue),
		 skb_queue_empty(&msk->receive_queue), copied);
	if (!(flags & MSG_PEEK))
		mptcp_rcv_space_adjust(msk, copied);

	release_sock(sk);
	return copied;
}

static void mptcp_retransmit_timer(struct timer_list *t)
{
	struct inet_connection_sock *icsk = from_timer(icsk, t,
						       icsk_retransmit_timer);
	struct sock *sk = &icsk->icsk_inet.sk;
	struct mptcp_sock *msk = mptcp_sk(sk);

	bh_lock_sock(sk);
	if (!sock_owned_by_user(sk)) {
		/* we need a process context to retransmit */
		if (!test_and_set_bit(MPTCP_WORK_RTX, &msk->flags))
			mptcp_schedule_work(sk);
	} else {
		/* delegate our work to tcp_release_cb() */
		__set_bit(MPTCP_RETRANSMIT, &msk->cb_flags);
	}
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void mptcp_tout_timer(struct timer_list *t)
{
	struct sock *sk = from_timer(sk, t, sk_timer);

	mptcp_schedule_work(sk);
	sock_put(sk);
}

/* Find an idle subflow.  Return NULL if there is unacked data at tcp
 * level.
 *
 * A backup subflow is returned only if that is the only kind available.
 */
struct sock *mptcp_subflow_get_retrans(struct mptcp_sock *msk)
{
	struct sock *backup = NULL, *pick = NULL;
	struct mptcp_subflow_context *subflow;
	int min_stale_count = INT_MAX;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (!__mptcp_subflow_active(subflow))
			continue;

		/* still data outstanding at TCP level? skip this */
		if (!tcp_rtx_and_write_queues_empty(ssk)) {
			mptcp_pm_subflow_chk_stale(msk, ssk);
			min_stale_count = min_t(int, min_stale_count, subflow->stale_count);
			continue;
		}

		if (subflow->backup) {
			if (!backup)
				backup = ssk;
			continue;
		}

		if (!pick)
			pick = ssk;
	}

	if (pick)
		return pick;

	/* use backup only if there are no progresses anywhere */
	return min_stale_count > 1 ? backup : NULL;
}

bool __mptcp_retransmit_pending_data(struct sock *sk)
{
	struct mptcp_data_frag *cur, *rtx_head;
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (__mptcp_check_fallback(msk))
		return false;

	if (tcp_rtx_and_write_queues_empty(sk))
		return false;

	/* the closing socket has some data untransmitted and/or unacked:
	 * some data in the mptcp rtx queue has not really xmitted yet.
	 * keep it simple and re-inject the whole mptcp level rtx queue
	 */
	mptcp_data_lock(sk);
	__mptcp_clean_una_wakeup(sk);
	rtx_head = mptcp_rtx_head(sk);
	if (!rtx_head) {
		mptcp_data_unlock(sk);
		return false;
	}

	msk->recovery_snd_nxt = msk->snd_nxt;
	msk->recovery = true;
	mptcp_data_unlock(sk);

	msk->first_pending = rtx_head;
	msk->snd_burst = 0;

	/* be sure to clear the "sent status" on all re-injected fragments */
	list_for_each_entry(cur, &msk->rtx_queue, list) {
		if (!cur->already_sent)
			break;
		cur->already_sent = 0;
	}

	return true;
}

/* flags for __mptcp_close_ssk() */
#define MPTCP_CF_PUSH		BIT(1)
#define MPTCP_CF_FASTCLOSE	BIT(2)

/* be sure to send a reset only if the caller asked for it, also
 * clean completely the subflow status when the subflow reaches
 * TCP_CLOSE state
 */
static void __mptcp_subflow_disconnect(struct sock *ssk,
				       struct mptcp_subflow_context *subflow,
				       unsigned int flags)
{
	if (((1 << ssk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN)) ||
	    (flags & MPTCP_CF_FASTCLOSE)) {
		/* The MPTCP code never wait on the subflow sockets, TCP-level
		 * disconnect should never fail
		 */
		WARN_ON_ONCE(tcp_disconnect(ssk, 0));
		mptcp_subflow_ctx_reset(subflow);
	} else {
		tcp_shutdown(ssk, SEND_SHUTDOWN);
	}
}

/* subflow sockets can be either outgoing (connect) or incoming
 * (accept).
 *
 * Outgoing subflows use in-kernel sockets.
 * Incoming subflows do not have their own 'struct socket' allocated,
 * so we need to use tcp_close() after detaching them from the mptcp
 * parent socket.
 */
static void __mptcp_close_ssk(struct sock *sk, struct sock *ssk,
			      struct mptcp_subflow_context *subflow,
			      unsigned int flags)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool dispose_it, need_push = false;

	/* If the first subflow moved to a close state before accept, e.g. due
	 * to an incoming reset or listener shutdown, the subflow socket is
	 * already deleted by inet_child_forget() and the mptcp socket can't
	 * survive too.
	 */
	if (msk->in_accept_queue && msk->first == ssk &&
	    (sock_flag(sk, SOCK_DEAD) || sock_flag(ssk, SOCK_DEAD))) {
		/* ensure later check in mptcp_worker() will dispose the msk */
		sock_set_flag(sk, SOCK_DEAD);
		mptcp_set_close_tout(sk, tcp_jiffies32 - (mptcp_close_timeout(sk) + 1));
		lock_sock_nested(ssk, SINGLE_DEPTH_NESTING);
		mptcp_subflow_drop_ctx(ssk);
		goto out_release;
	}

	dispose_it = msk->free_first || ssk != msk->first;
	if (dispose_it)
		list_del(&subflow->node);

	lock_sock_nested(ssk, SINGLE_DEPTH_NESTING);

	if ((flags & MPTCP_CF_FASTCLOSE) && !__mptcp_check_fallback(msk)) {
		/* be sure to force the tcp_close path
		 * to generate the egress reset
		 */
		ssk->sk_lingertime = 0;
		sock_set_flag(ssk, SOCK_LINGER);
		subflow->send_fastclose = 1;
	}

	need_push = (flags & MPTCP_CF_PUSH) && __mptcp_retransmit_pending_data(sk);
	if (!dispose_it) {
		__mptcp_subflow_disconnect(ssk, subflow, flags);
		release_sock(ssk);

		goto out;
	}

	subflow->disposable = 1;

	/* if ssk hit tcp_done(), tcp_cleanup_ulp() cleared the related ops
	 * the ssk has been already destroyed, we just need to release the
	 * reference owned by msk;
	 */
	if (!inet_csk(ssk)->icsk_ulp_ops) {
		WARN_ON_ONCE(!sock_flag(ssk, SOCK_DEAD));
		kfree_rcu(subflow, rcu);
	} else {
		/* otherwise tcp will dispose of the ssk and subflow ctx */
		__tcp_close(ssk, 0);

		/* close acquired an extra ref */
		__sock_put(ssk);
	}

out_release:
	__mptcp_subflow_error_report(sk, ssk);
	release_sock(ssk);

	sock_put(ssk);

	if (ssk == msk->first)
		WRITE_ONCE(msk->first, NULL);

out:
	__mptcp_sync_sndbuf(sk);
	if (need_push)
		__mptcp_push_pending(sk, 0);

	/* Catch every 'all subflows closed' scenario, including peers silently
	 * closing them, e.g. due to timeout.
	 * For established sockets, allow an additional timeout before closing,
	 * as the protocol can still create more subflows.
	 */
	if (list_is_singular(&msk->conn_list) && msk->first &&
	    inet_sk_state_load(msk->first) == TCP_CLOSE) {
		if (sk->sk_state != TCP_ESTABLISHED ||
		    msk->in_accept_queue || sock_flag(sk, SOCK_DEAD)) {
			inet_sk_state_store(sk, TCP_CLOSE);
			mptcp_close_wake_up(sk);
		} else {
			mptcp_start_tout_timer(sk);
		}
	}
}

void mptcp_close_ssk(struct sock *sk, struct sock *ssk,
		     struct mptcp_subflow_context *subflow)
{
	if (sk->sk_state == TCP_ESTABLISHED)
		mptcp_event(MPTCP_EVENT_SUB_CLOSED, mptcp_sk(sk), ssk, GFP_KERNEL);

	/* subflow aborted before reaching the fully_established status
	 * attempt the creation of the next subflow
	 */
	mptcp_pm_subflow_check_next(mptcp_sk(sk), subflow);

	__mptcp_close_ssk(sk, ssk, subflow, MPTCP_CF_PUSH);
}

static unsigned int mptcp_sync_mss(struct sock *sk, u32 pmtu)
{
	return 0;
}

static void __mptcp_close_subflow(struct sock *sk)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct mptcp_sock *msk = mptcp_sk(sk);

	might_sleep();

	mptcp_for_each_subflow_safe(msk, subflow, tmp) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (inet_sk_state_load(ssk) != TCP_CLOSE)
			continue;

		/* 'subflow_data_ready' will re-sched once rx queue is empty */
		if (!skb_queue_empty_lockless(&ssk->sk_receive_queue))
			continue;

		mptcp_close_ssk(sk, ssk, subflow);
	}

}

static bool mptcp_close_tout_expired(const struct sock *sk)
{
	if (!inet_csk(sk)->icsk_mtup.probe_timestamp ||
	    sk->sk_state == TCP_CLOSE)
		return false;

	return time_after32(tcp_jiffies32,
		  inet_csk(sk)->icsk_mtup.probe_timestamp + mptcp_close_timeout(sk));
}

static void mptcp_check_fastclose(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct sock *sk = (struct sock *)msk;

	if (likely(!READ_ONCE(msk->rcv_fastclose)))
		return;

	mptcp_token_destroy(msk);

	mptcp_for_each_subflow_safe(msk, subflow, tmp) {
		struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);
		bool slow;

		slow = lock_sock_fast(tcp_sk);
		if (tcp_sk->sk_state != TCP_CLOSE) {
			tcp_send_active_reset(tcp_sk, GFP_ATOMIC);
			tcp_set_state(tcp_sk, TCP_CLOSE);
		}
		unlock_sock_fast(tcp_sk, slow);
	}

	/* Mirror the tcp_reset() error propagation */
	switch (sk->sk_state) {
	case TCP_SYN_SENT:
		WRITE_ONCE(sk->sk_err, ECONNREFUSED);
		break;
	case TCP_CLOSE_WAIT:
		WRITE_ONCE(sk->sk_err, EPIPE);
		break;
	case TCP_CLOSE:
		return;
	default:
		WRITE_ONCE(sk->sk_err, ECONNRESET);
	}

	inet_sk_state_store(sk, TCP_CLOSE);
	WRITE_ONCE(sk->sk_shutdown, SHUTDOWN_MASK);
	smp_mb__before_atomic(); /* SHUTDOWN must be visible first */
	set_bit(MPTCP_WORK_CLOSE_SUBFLOW, &msk->flags);

	/* the calling mptcp_worker will properly destroy the socket */
	if (sock_flag(sk, SOCK_DEAD))
		return;

	sk->sk_state_change(sk);
	sk_error_report(sk);
}

static void __mptcp_retrans(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_subflow_context *subflow;
	struct mptcp_sendmsg_info info = {};
	struct mptcp_data_frag *dfrag;
	struct sock *ssk;
	int ret, err;
	u16 len = 0;

	mptcp_clean_una_wakeup(sk);

	/* first check ssk: need to kick "stale" logic */
	err = mptcp_sched_get_retrans(msk);
	dfrag = mptcp_rtx_head(sk);
	if (!dfrag) {
		if (mptcp_data_fin_enabled(msk)) {
			struct inet_connection_sock *icsk = inet_csk(sk);

			icsk->icsk_retransmits++;
			mptcp_set_datafin_timeout(sk);
			mptcp_send_ack(msk);

			goto reset_timer;
		}

		if (!mptcp_send_head(sk))
			return;

		goto reset_timer;
	}

	if (err)
		goto reset_timer;

	mptcp_for_each_subflow(msk, subflow) {
		if (READ_ONCE(subflow->scheduled)) {
			u16 copied = 0;

			mptcp_subflow_set_scheduled(subflow, false);

			ssk = mptcp_subflow_tcp_sock(subflow);

			lock_sock(ssk);

			/* limit retransmission to the bytes already sent on some subflows */
			info.sent = 0;
			info.limit = READ_ONCE(msk->csum_enabled) ? dfrag->data_len :
								    dfrag->already_sent;
			while (info.sent < info.limit) {
				ret = mptcp_sendmsg_frag(sk, ssk, dfrag, &info);
				if (ret <= 0)
					break;

				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_RETRANSSEGS);
				copied += ret;
				info.sent += ret;
			}
			if (copied) {
				len = max(copied, len);
				tcp_push(ssk, 0, info.mss_now, tcp_sk(ssk)->nonagle,
					 info.size_goal);
				WRITE_ONCE(msk->allow_infinite_fallback, false);
			}

			release_sock(ssk);
		}
	}

	msk->bytes_retrans += len;
	dfrag->already_sent = max(dfrag->already_sent, len);

reset_timer:
	mptcp_check_and_set_pending(sk);

	if (!mptcp_rtx_timer_pending(sk))
		mptcp_reset_rtx_timer(sk);
}

/* schedule the timeout timer for the relevant event: either close timeout
 * or mp_fail timeout. The close timeout takes precedence on the mp_fail one
 */
void mptcp_reset_tout_timer(struct mptcp_sock *msk, unsigned long fail_tout)
{
	struct sock *sk = (struct sock *)msk;
	unsigned long timeout, close_timeout;

	if (!fail_tout && !inet_csk(sk)->icsk_mtup.probe_timestamp)
		return;

	close_timeout = inet_csk(sk)->icsk_mtup.probe_timestamp - tcp_jiffies32 + jiffies +
			mptcp_close_timeout(sk);

	/* the close timeout takes precedence on the fail one, and here at least one of
	 * them is active
	 */
	timeout = inet_csk(sk)->icsk_mtup.probe_timestamp ? close_timeout : fail_tout;

	sk_reset_timer(sk, &sk->sk_timer, timeout);
}

static void mptcp_mp_fail_no_response(struct mptcp_sock *msk)
{
	struct sock *ssk = msk->first;
	bool slow;

	if (!ssk)
		return;

	pr_debug("MP_FAIL doesn't respond, reset the subflow");

	slow = lock_sock_fast(ssk);
	mptcp_subflow_reset(ssk);
	WRITE_ONCE(mptcp_subflow_ctx(ssk)->fail_tout, 0);
	unlock_sock_fast(ssk, slow);
}

static void mptcp_do_fastclose(struct sock *sk)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct mptcp_sock *msk = mptcp_sk(sk);

	inet_sk_state_store(sk, TCP_CLOSE);
	mptcp_for_each_subflow_safe(msk, subflow, tmp)
		__mptcp_close_ssk(sk, mptcp_subflow_tcp_sock(subflow),
				  subflow, MPTCP_CF_FASTCLOSE);
}

static void mptcp_worker(struct work_struct *work)
{
	struct mptcp_sock *msk = container_of(work, struct mptcp_sock, work);
	struct sock *sk = (struct sock *)msk;
	unsigned long fail_tout;
	int state;

	lock_sock(sk);
	state = sk->sk_state;
	if (unlikely((1 << state) & (TCPF_CLOSE | TCPF_LISTEN)))
		goto unlock;

	mptcp_check_fastclose(msk);

	mptcp_pm_nl_work(msk);

	mptcp_check_send_data_fin(sk);
	mptcp_check_data_fin_ack(sk);
	mptcp_check_data_fin(sk);

	if (test_and_clear_bit(MPTCP_WORK_CLOSE_SUBFLOW, &msk->flags))
		__mptcp_close_subflow(sk);

	if (mptcp_close_tout_expired(sk)) {
		mptcp_do_fastclose(sk);
		mptcp_close_wake_up(sk);
	}

	if (sock_flag(sk, SOCK_DEAD) && sk->sk_state == TCP_CLOSE) {
		__mptcp_destroy_sock(sk);
		goto unlock;
	}

	if (test_and_clear_bit(MPTCP_WORK_RTX, &msk->flags))
		__mptcp_retrans(sk);

	fail_tout = msk->first ? READ_ONCE(mptcp_subflow_ctx(msk->first)->fail_tout) : 0;
	if (fail_tout && time_after(jiffies, fail_tout))
		mptcp_mp_fail_no_response(msk);

unlock:
	release_sock(sk);
	sock_put(sk);
}

static void __mptcp_init_sock(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	INIT_LIST_HEAD(&msk->conn_list);
	INIT_LIST_HEAD(&msk->join_list);
	INIT_LIST_HEAD(&msk->rtx_queue);
	INIT_WORK(&msk->work, mptcp_worker);
	__skb_queue_head_init(&msk->receive_queue);
	msk->out_of_order_queue = RB_ROOT;
	msk->first_pending = NULL;
	msk->rmem_fwd_alloc = 0;
	WRITE_ONCE(msk->rmem_released, 0);
	msk->timer_ival = TCP_RTO_MIN;
	msk->scaling_ratio = TCP_DEFAULT_SCALING_RATIO;

	WRITE_ONCE(msk->first, NULL);
	inet_csk(sk)->icsk_sync_mss = mptcp_sync_mss;
	WRITE_ONCE(msk->csum_enabled, mptcp_is_checksum_enabled(sock_net(sk)));
	WRITE_ONCE(msk->allow_infinite_fallback, true);
	msk->recovery = false;
	msk->subflow_id = 1;

	mptcp_pm_data_init(msk);

	/* re-use the csk retrans timer for MPTCP-level retrans */
	timer_setup(&msk->sk.icsk_retransmit_timer, mptcp_retransmit_timer, 0);
	timer_setup(&sk->sk_timer, mptcp_tout_timer, 0);
}

static void mptcp_ca_reset(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	tcp_assign_congestion_control(sk);
	strcpy(mptcp_sk(sk)->ca_name, icsk->icsk_ca_ops->name);

	/* no need to keep a reference to the ops, the name will suffice */
	tcp_cleanup_congestion_control(sk);
	icsk->icsk_ca_ops = NULL;
}

static int mptcp_init_sock(struct sock *sk)
{
	struct net *net = sock_net(sk);
	int ret;

	__mptcp_init_sock(sk);

	if (!mptcp_is_enabled(net))
		return -ENOPROTOOPT;

	if (unlikely(!net->mib.mptcp_statistics) && !mptcp_mib_alloc(net))
		return -ENOMEM;

	ret = mptcp_init_sched(mptcp_sk(sk),
			       mptcp_sched_find(mptcp_get_scheduler(net)));
	if (ret)
		return ret;

	set_bit(SOCK_CUSTOM_SOCKOPT, &sk->sk_socket->flags);

	/* fetch the ca name; do it outside __mptcp_init_sock(), so that clone will
	 * propagate the correct value
	 */
	mptcp_ca_reset(sk);

	sk_sockets_allocated_inc(sk);
	sk->sk_rcvbuf = READ_ONCE(net->ipv4.sysctl_tcp_rmem[1]);
	sk->sk_sndbuf = READ_ONCE(net->ipv4.sysctl_tcp_wmem[1]);

	return 0;
}

static void __mptcp_clear_xmit(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_data_frag *dtmp, *dfrag;

	WRITE_ONCE(msk->first_pending, NULL);
	list_for_each_entry_safe(dfrag, dtmp, &msk->rtx_queue, list)
		dfrag_clear(sk, dfrag);
}

void mptcp_cancel_work(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (cancel_work_sync(&msk->work))
		__sock_put(sk);
}

void mptcp_subflow_shutdown(struct sock *sk, struct sock *ssk, int how)
{
	lock_sock(ssk);

	switch (ssk->sk_state) {
	case TCP_LISTEN:
		if (!(how & RCV_SHUTDOWN))
			break;
		fallthrough;
	case TCP_SYN_SENT:
		WARN_ON_ONCE(tcp_disconnect(ssk, O_NONBLOCK));
		break;
	default:
		if (__mptcp_check_fallback(mptcp_sk(sk))) {
			pr_debug("Fallback");
			ssk->sk_shutdown |= how;
			tcp_shutdown(ssk, how);

			/* simulate the data_fin ack reception to let the state
			 * machine move forward
			 */
			WRITE_ONCE(mptcp_sk(sk)->snd_una, mptcp_sk(sk)->snd_nxt);
			mptcp_schedule_work(sk);
		} else {
			pr_debug("Sending DATA_FIN on subflow %p", ssk);
			tcp_send_ack(ssk);
			if (!mptcp_rtx_timer_pending(sk))
				mptcp_reset_rtx_timer(sk);
		}
		break;
	}

	release_sock(ssk);
}

static const unsigned char new_state[16] = {
	/* current state:     new state:      action:	*/
	[0 /* (Invalid) */] = TCP_CLOSE,
	[TCP_ESTABLISHED]   = TCP_FIN_WAIT1 | TCP_ACTION_FIN,
	[TCP_SYN_SENT]      = TCP_CLOSE,
	[TCP_SYN_RECV]      = TCP_FIN_WAIT1 | TCP_ACTION_FIN,
	[TCP_FIN_WAIT1]     = TCP_FIN_WAIT1,
	[TCP_FIN_WAIT2]     = TCP_FIN_WAIT2,
	[TCP_TIME_WAIT]     = TCP_CLOSE,	/* should not happen ! */
	[TCP_CLOSE]         = TCP_CLOSE,
	[TCP_CLOSE_WAIT]    = TCP_LAST_ACK  | TCP_ACTION_FIN,
	[TCP_LAST_ACK]      = TCP_LAST_ACK,
	[TCP_LISTEN]        = TCP_CLOSE,
	[TCP_CLOSING]       = TCP_CLOSING,
	[TCP_NEW_SYN_RECV]  = TCP_CLOSE,	/* should not happen ! */
};

static int mptcp_close_state(struct sock *sk)
{
	int next = (int)new_state[sk->sk_state];
	int ns = next & TCP_STATE_MASK;

	inet_sk_state_store(sk, ns);

	return next & TCP_ACTION_FIN;
}

static void mptcp_check_send_data_fin(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);

	pr_debug("msk=%p snd_data_fin_enable=%d pending=%d snd_nxt=%llu write_seq=%llu",
		 msk, msk->snd_data_fin_enable, !!mptcp_send_head(sk),
		 msk->snd_nxt, msk->write_seq);

	/* we still need to enqueue subflows or not really shutting down,
	 * skip this
	 */
	if (!msk->snd_data_fin_enable || msk->snd_nxt + 1 != msk->write_seq ||
	    mptcp_send_head(sk))
		return;

	WRITE_ONCE(msk->snd_nxt, msk->write_seq);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);

		mptcp_subflow_shutdown(sk, tcp_sk, SEND_SHUTDOWN);
	}
}

static void __mptcp_wr_shutdown(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	pr_debug("msk=%p snd_data_fin_enable=%d shutdown=%x state=%d pending=%d",
		 msk, msk->snd_data_fin_enable, sk->sk_shutdown, sk->sk_state,
		 !!mptcp_send_head(sk));

	/* will be ignored by fallback sockets */
	WRITE_ONCE(msk->write_seq, msk->write_seq + 1);
	WRITE_ONCE(msk->snd_data_fin_enable, 1);

	mptcp_check_send_data_fin(sk);
}

static void __mptcp_destroy_sock(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	pr_debug("msk=%p", msk);

	might_sleep();

	mptcp_stop_rtx_timer(sk);
	sk_stop_timer(sk, &sk->sk_timer);
	msk->pm.status = 0;
	mptcp_release_sched(msk);

	sk->sk_prot->destroy(sk);

	WARN_ON_ONCE(msk->rmem_fwd_alloc);
	WARN_ON_ONCE(msk->rmem_released);
	sk_stream_kill_queues(sk);
	xfrm_sk_free_policy(sk);

	sock_put(sk);
}

void __mptcp_unaccepted_force_close(struct sock *sk)
{
	sock_set_flag(sk, SOCK_DEAD);
	mptcp_do_fastclose(sk);
	__mptcp_destroy_sock(sk);
}

static __poll_t mptcp_check_readable(struct sock *sk)
{
	return mptcp_epollin_ready(sk) ? EPOLLIN | EPOLLRDNORM : 0;
}

static void mptcp_check_listen_stop(struct sock *sk)
{
	struct sock *ssk;

	if (inet_sk_state_load(sk) != TCP_LISTEN)
		return;

	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	ssk = mptcp_sk(sk)->first;
	if (WARN_ON_ONCE(!ssk || inet_sk_state_load(ssk) != TCP_LISTEN))
		return;

	lock_sock_nested(ssk, SINGLE_DEPTH_NESTING);
	tcp_set_state(ssk, TCP_CLOSE);
	mptcp_subflow_queue_clean(sk, ssk);
	inet_csk_listen_stop(ssk);
	mptcp_event_pm_listener(ssk, MPTCP_EVENT_LISTENER_CLOSED);
	release_sock(ssk);
}

bool __mptcp_close(struct sock *sk, long timeout)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool do_cancel_work = false;
	int subflows_alive = 0;

	WRITE_ONCE(sk->sk_shutdown, SHUTDOWN_MASK);

	if ((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE)) {
		mptcp_check_listen_stop(sk);
		inet_sk_state_store(sk, TCP_CLOSE);
		goto cleanup;
	}

	if (mptcp_data_avail(msk) || timeout < 0) {
		/* If the msk has read data, or the caller explicitly ask it,
		 * do the MPTCP equivalent of TCP reset, aka MPTCP fastclose
		 */
		mptcp_do_fastclose(sk);
		timeout = 0;
	} else if (mptcp_close_state(sk)) {
		__mptcp_wr_shutdown(sk);
	}

	sk_stream_wait_close(sk, timeout);

cleanup:
	/* orphan all the subflows */
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast_nested(ssk);

		subflows_alive += ssk->sk_state != TCP_CLOSE;

		/* since the close timeout takes precedence on the fail one,
		 * cancel the latter
		 */
		if (ssk == msk->first)
			subflow->fail_tout = 0;

		/* detach from the parent socket, but allow data_ready to
		 * push incoming data into the mptcp stack, to properly ack it
		 */
		ssk->sk_socket = NULL;
		ssk->sk_wq = NULL;
		unlock_sock_fast(ssk, slow);
	}
	sock_orphan(sk);

	/* all the subflows are closed, only timeout can change the msk
	 * state, let's not keep resources busy for no reasons
	 */
	if (subflows_alive == 0)
		inet_sk_state_store(sk, TCP_CLOSE);

	sock_hold(sk);
	pr_debug("msk=%p state=%d", sk, sk->sk_state);
	if (msk->token)
		mptcp_event(MPTCP_EVENT_CLOSED, msk, NULL, GFP_KERNEL);

	if (sk->sk_state == TCP_CLOSE) {
		__mptcp_destroy_sock(sk);
		do_cancel_work = true;
	} else {
		mptcp_start_tout_timer(sk);
	}

	return do_cancel_work;
}

static void mptcp_close(struct sock *sk, long timeout)
{
	bool do_cancel_work;

	lock_sock(sk);

	do_cancel_work = __mptcp_close(sk, timeout);
	release_sock(sk);
	if (do_cancel_work)
		mptcp_cancel_work(sk);

	sock_put(sk);
}

static void mptcp_copy_inaddrs(struct sock *msk, const struct sock *ssk)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	const struct ipv6_pinfo *ssk6 = inet6_sk(ssk);
	struct ipv6_pinfo *msk6 = inet6_sk(msk);

	msk->sk_v6_daddr = ssk->sk_v6_daddr;
	msk->sk_v6_rcv_saddr = ssk->sk_v6_rcv_saddr;

	if (msk6 && ssk6) {
		msk6->saddr = ssk6->saddr;
		msk6->flow_label = ssk6->flow_label;
	}
#endif

	inet_sk(msk)->inet_num = inet_sk(ssk)->inet_num;
	inet_sk(msk)->inet_dport = inet_sk(ssk)->inet_dport;
	inet_sk(msk)->inet_sport = inet_sk(ssk)->inet_sport;
	inet_sk(msk)->inet_daddr = inet_sk(ssk)->inet_daddr;
	inet_sk(msk)->inet_saddr = inet_sk(ssk)->inet_saddr;
	inet_sk(msk)->inet_rcv_saddr = inet_sk(ssk)->inet_rcv_saddr;
}

static int mptcp_disconnect(struct sock *sk, int flags)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	/* We are on the fastopen error path. We can't call straight into the
	 * subflows cleanup code due to lock nesting (we are already under
	 * msk->firstsocket lock).
	 */
	if (msk->fastopening)
		return -EBUSY;

	mptcp_check_listen_stop(sk);
	inet_sk_state_store(sk, TCP_CLOSE);

	mptcp_stop_rtx_timer(sk);
	mptcp_stop_tout_timer(sk);

	if (msk->token)
		mptcp_event(MPTCP_EVENT_CLOSED, msk, NULL, GFP_KERNEL);

	/* msk->subflow is still intact, the following will not free the first
	 * subflow
	 */
	mptcp_destroy_common(msk, MPTCP_CF_FASTCLOSE);
	WRITE_ONCE(msk->flags, 0);
	msk->cb_flags = 0;
	msk->push_pending = 0;
	msk->recovery = false;
	msk->can_ack = false;
	msk->fully_established = false;
	msk->rcv_data_fin = false;
	msk->snd_data_fin_enable = false;
	msk->rcv_fastclose = false;
	msk->use_64bit_ack = false;
	msk->bytes_consumed = 0;
	WRITE_ONCE(msk->csum_enabled, mptcp_is_checksum_enabled(sock_net(sk)));
	mptcp_pm_data_reset(msk);
	mptcp_ca_reset(sk);
	msk->bytes_acked = 0;
	msk->bytes_received = 0;
	msk->bytes_sent = 0;
	msk->bytes_retrans = 0;

	WRITE_ONCE(sk->sk_shutdown, 0);
	sk_error_report(sk);
	return 0;
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct ipv6_pinfo *mptcp_inet6_sk(const struct sock *sk)
{
	unsigned int offset = sizeof(struct mptcp6_sock) - sizeof(struct ipv6_pinfo);

	return (struct ipv6_pinfo *)(((u8 *)sk) + offset);
}
#endif

struct sock *mptcp_sk_clone_init(const struct sock *sk,
				 const struct mptcp_options_received *mp_opt,
				 struct sock *ssk,
				 struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct sock *nsk = sk_clone_lock(sk, GFP_ATOMIC);
	struct mptcp_sock *msk;

	if (!nsk)
		return NULL;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (nsk->sk_family == AF_INET6)
		inet_sk(nsk)->pinet6 = mptcp_inet6_sk(nsk);
#endif

	__mptcp_init_sock(nsk);

	msk = mptcp_sk(nsk);
	msk->local_key = subflow_req->local_key;
	msk->token = subflow_req->token;
	msk->in_accept_queue = 1;
	WRITE_ONCE(msk->fully_established, false);
	if (mp_opt->suboptions & OPTION_MPTCP_CSUMREQD)
		WRITE_ONCE(msk->csum_enabled, true);

	msk->write_seq = subflow_req->idsn + 1;
	msk->snd_nxt = msk->write_seq;
	msk->snd_una = msk->write_seq;
	msk->wnd_end = msk->snd_nxt + req->rsk_rcv_wnd;
	msk->setsockopt_seq = mptcp_sk(sk)->setsockopt_seq;
	mptcp_init_sched(msk, mptcp_sk(sk)->sched);

	/* passive msk is created after the first/MPC subflow */
	msk->subflow_id = 2;

	sock_reset_flag(nsk, SOCK_RCU_FREE);
	security_inet_csk_clone(nsk, req);

	/* this can't race with mptcp_close(), as the msk is
	 * not yet exposted to user-space
	 */
	inet_sk_state_store(nsk, TCP_ESTABLISHED);

	/* The msk maintain a ref to each subflow in the connections list */
	WRITE_ONCE(msk->first, ssk);
	list_add(&mptcp_subflow_ctx(ssk)->node, &msk->conn_list);
	sock_hold(ssk);

	/* new mpc subflow takes ownership of the newly
	 * created mptcp socket
	 */
	mptcp_token_accept(subflow_req, msk);

	/* set msk addresses early to ensure mptcp_pm_get_local_id()
	 * uses the correct data
	 */
	mptcp_copy_inaddrs(nsk, ssk);
	__mptcp_propagate_sndbuf(nsk, ssk);

	mptcp_rcv_space_init(msk, ssk);
	bh_unlock_sock(nsk);

	/* note: the newly allocated socket refcount is 2 now */
	return nsk;
}

void mptcp_rcv_space_init(struct mptcp_sock *msk, const struct sock *ssk)
{
	const struct tcp_sock *tp = tcp_sk(ssk);

	msk->rcvq_space.copied = 0;
	msk->rcvq_space.rtt_us = 0;

	msk->rcvq_space.time = tp->tcp_mstamp;

	/* initial rcv_space offering made to peer */
	msk->rcvq_space.space = min_t(u32, tp->rcv_wnd,
				      TCP_INIT_CWND * tp->advmss);
	if (msk->rcvq_space.space == 0)
		msk->rcvq_space.space = TCP_INIT_CWND * TCP_MSS_DEFAULT;

	WRITE_ONCE(msk->wnd_end, msk->snd_nxt + tcp_sk(ssk)->snd_wnd);
}

static struct sock *mptcp_accept(struct sock *ssk, int flags, int *err,
				 bool kern)
{
	struct sock *newsk;

	pr_debug("ssk=%p, listener=%p", ssk, mptcp_subflow_ctx(ssk));
	newsk = inet_csk_accept(ssk, flags, err, kern);
	if (!newsk)
		return NULL;

	pr_debug("newsk=%p, subflow is mptcp=%d", newsk, sk_is_mptcp(newsk));
	if (sk_is_mptcp(newsk)) {
		struct mptcp_subflow_context *subflow;
		struct sock *new_mptcp_sock;

		subflow = mptcp_subflow_ctx(newsk);
		new_mptcp_sock = subflow->conn;

		/* is_mptcp should be false if subflow->conn is missing, see
		 * subflow_syn_recv_sock()
		 */
		if (WARN_ON_ONCE(!new_mptcp_sock)) {
			tcp_sk(newsk)->is_mptcp = 0;
			goto out;
		}

		newsk = new_mptcp_sock;
		MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_MPCAPABLEPASSIVEACK);
	} else {
		MPTCP_INC_STATS(sock_net(ssk),
				MPTCP_MIB_MPCAPABLEPASSIVEFALLBACK);
	}

out:
	newsk->sk_kern_sock = kern;
	return newsk;
}

void mptcp_destroy_common(struct mptcp_sock *msk, unsigned int flags)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct sock *sk = (struct sock *)msk;

	__mptcp_clear_xmit(sk);

	/* join list will be eventually flushed (with rst) at sock lock release time */
	mptcp_for_each_subflow_safe(msk, subflow, tmp)
		__mptcp_close_ssk(sk, mptcp_subflow_tcp_sock(subflow), subflow, flags);

	/* move to sk_receive_queue, sk_stream_kill_queues will purge it */
	mptcp_data_lock(sk);
	skb_queue_splice_tail_init(&msk->receive_queue, &sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_receive_queue);
	skb_rbtree_purge(&msk->out_of_order_queue);
	mptcp_data_unlock(sk);

	/* move all the rx fwd alloc into the sk_mem_reclaim_final in
	 * inet_sock_destruct() will dispose it
	 */
	sk_forward_alloc_add(sk, msk->rmem_fwd_alloc);
	WRITE_ONCE(msk->rmem_fwd_alloc, 0);
	mptcp_token_destroy(msk);
	mptcp_pm_free_anno_list(msk);
	mptcp_free_local_addr_list(msk);
}

static void mptcp_destroy(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	/* allow the following to close even the initial subflow */
	msk->free_first = 1;
	mptcp_destroy_common(msk, 0);
	sk_sockets_allocated_dec(sk);
}

void __mptcp_data_acked(struct sock *sk)
{
	if (!sock_owned_by_user(sk))
		__mptcp_clean_una(sk);
	else
		__set_bit(MPTCP_CLEAN_UNA, &mptcp_sk(sk)->cb_flags);

	if (mptcp_pending_data_fin_ack(sk))
		mptcp_schedule_work(sk);
}

void __mptcp_check_push(struct sock *sk, struct sock *ssk)
{
	if (!mptcp_send_head(sk))
		return;

	if (!sock_owned_by_user(sk))
		__mptcp_subflow_push_pending(sk, ssk, false);
	else
		__set_bit(MPTCP_PUSH_PENDING, &mptcp_sk(sk)->cb_flags);
}

#define MPTCP_FLAGS_PROCESS_CTX_NEED (BIT(MPTCP_PUSH_PENDING) | \
				      BIT(MPTCP_RETRANSMIT) | \
				      BIT(MPTCP_FLUSH_JOIN_LIST))

/* processes deferred events and flush wmem */
static void mptcp_release_cb(struct sock *sk)
	__must_hold(&sk->sk_lock.slock)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	for (;;) {
		unsigned long flags = (msk->cb_flags & MPTCP_FLAGS_PROCESS_CTX_NEED) |
				      msk->push_pending;
		struct list_head join_list;

		if (!flags)
			break;

		INIT_LIST_HEAD(&join_list);
		list_splice_init(&msk->join_list, &join_list);

		/* the following actions acquire the subflow socket lock
		 *
		 * 1) can't be invoked in atomic scope
		 * 2) must avoid ABBA deadlock with msk socket spinlock: the RX
		 *    datapath acquires the msk socket spinlock while helding
		 *    the subflow socket lock
		 */
		msk->push_pending = 0;
		msk->cb_flags &= ~flags;
		spin_unlock_bh(&sk->sk_lock.slock);

		if (flags & BIT(MPTCP_FLUSH_JOIN_LIST))
			__mptcp_flush_join_list(sk, &join_list);
		if (flags & BIT(MPTCP_PUSH_PENDING))
			__mptcp_push_pending(sk, 0);
		if (flags & BIT(MPTCP_RETRANSMIT))
			__mptcp_retrans(sk);

		cond_resched();
		spin_lock_bh(&sk->sk_lock.slock);
	}

	if (__test_and_clear_bit(MPTCP_CLEAN_UNA, &msk->cb_flags))
		__mptcp_clean_una_wakeup(sk);
	if (unlikely(msk->cb_flags)) {
		/* be sure to set the current sk state before tacking actions
		 * depending on sk_state, that is processing MPTCP_ERROR_REPORT
		 */
		if (__test_and_clear_bit(MPTCP_CONNECTED, &msk->cb_flags))
			__mptcp_set_connected(sk);
		if (__test_and_clear_bit(MPTCP_ERROR_REPORT, &msk->cb_flags))
			__mptcp_error_report(sk);
		if (__test_and_clear_bit(MPTCP_SYNC_SNDBUF, &msk->cb_flags))
			__mptcp_sync_sndbuf(sk);
	}

	__mptcp_update_rmem(sk);
}

/* MP_JOIN client subflow must wait for 4th ack before sending any data:
 * TCP can't schedule delack timer before the subflow is fully established.
 * MPTCP uses the delack timer to do 3rd ack retransmissions
 */
static void schedule_3rdack_retransmission(struct sock *ssk)
{
	struct inet_connection_sock *icsk = inet_csk(ssk);
	struct tcp_sock *tp = tcp_sk(ssk);
	unsigned long timeout;

	if (mptcp_subflow_ctx(ssk)->fully_established)
		return;

	/* reschedule with a timeout above RTT, as we must look only for drop */
	if (tp->srtt_us)
		timeout = usecs_to_jiffies(tp->srtt_us >> (3 - 1));
	else
		timeout = TCP_TIMEOUT_INIT;
	timeout += jiffies;

	WARN_ON_ONCE(icsk->icsk_ack.pending & ICSK_ACK_TIMER);
	icsk->icsk_ack.pending |= ICSK_ACK_SCHED | ICSK_ACK_TIMER;
	icsk->icsk_ack.timeout = timeout;
	sk_reset_timer(ssk, &icsk->icsk_delack_timer, timeout);
}

void mptcp_subflow_process_delegated(struct sock *ssk, long status)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = subflow->conn;

	if (status & BIT(MPTCP_DELEGATE_SEND)) {
		mptcp_data_lock(sk);
		if (!sock_owned_by_user(sk))
			__mptcp_subflow_push_pending(sk, ssk, true);
		else
			__set_bit(MPTCP_PUSH_PENDING, &mptcp_sk(sk)->cb_flags);
		mptcp_data_unlock(sk);
	}
	if (status & BIT(MPTCP_DELEGATE_SNDBUF)) {
		mptcp_data_lock(sk);
		if (!sock_owned_by_user(sk))
			__mptcp_sync_sndbuf(sk);
		else
			__set_bit(MPTCP_SYNC_SNDBUF, &mptcp_sk(sk)->cb_flags);
		mptcp_data_unlock(sk);
	}
	if (status & BIT(MPTCP_DELEGATE_ACK))
		schedule_3rdack_retransmission(ssk);
}

static int mptcp_hash(struct sock *sk)
{
	/* should never be called,
	 * we hash the TCP subflows not the master socket
	 */
	WARN_ON_ONCE(1);
	return 0;
}

static void mptcp_unhash(struct sock *sk)
{
	/* called from sk_common_release(), but nothing to do here */
}

static int mptcp_get_port(struct sock *sk, unsigned short snum)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	pr_debug("msk=%p, ssk=%p", msk, msk->first);
	if (WARN_ON_ONCE(!msk->first))
		return -EINVAL;

	return inet_csk_get_port(msk->first, snum);
}

void mptcp_finish_connect(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk;
	struct sock *sk;

	subflow = mptcp_subflow_ctx(ssk);
	sk = subflow->conn;
	msk = mptcp_sk(sk);

	pr_debug("msk=%p, token=%u", sk, subflow->token);

	subflow->map_seq = subflow->iasn;
	subflow->map_subflow_seq = 1;

	/* the socket is not connected yet, no msk/subflow ops can access/race
	 * accessing the field below
	 */
	WRITE_ONCE(msk->local_key, subflow->local_key);
	WRITE_ONCE(msk->write_seq, subflow->idsn + 1);
	WRITE_ONCE(msk->snd_nxt, msk->write_seq);
	WRITE_ONCE(msk->snd_una, msk->write_seq);

	mptcp_pm_new_connection(msk, ssk, 0);

	mptcp_rcv_space_init(msk, ssk);
}

void mptcp_sock_graft(struct sock *sk, struct socket *parent)
{
	write_lock_bh(&sk->sk_callback_lock);
	rcu_assign_pointer(sk->sk_wq, &parent->wq);
	sk_set_socket(sk, parent);
	sk->sk_uid = SOCK_INODE(parent)->i_uid;
	write_unlock_bh(&sk->sk_callback_lock);
}

bool mptcp_finish_join(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct sock *parent = (void *)msk;
	bool ret = true;

	pr_debug("msk=%p, subflow=%p", msk, subflow);

	/* mptcp socket already closing? */
	if (!mptcp_is_fully_established(parent)) {
		subflow->reset_reason = MPTCP_RST_EMPTCP;
		return false;
	}

	/* active subflow, already present inside the conn_list */
	if (!list_empty(&subflow->node)) {
		mptcp_subflow_joined(msk, ssk);
		mptcp_propagate_sndbuf(parent, ssk);
		return true;
	}

	if (!mptcp_pm_allow_new_subflow(msk))
		goto err_prohibited;

	/* If we can't acquire msk socket lock here, let the release callback
	 * handle it
	 */
	mptcp_data_lock(parent);
	if (!sock_owned_by_user(parent)) {
		ret = __mptcp_finish_join(msk, ssk);
		if (ret) {
			sock_hold(ssk);
			list_add_tail(&subflow->node, &msk->conn_list);
		}
	} else {
		sock_hold(ssk);
		list_add_tail(&subflow->node, &msk->join_list);
		__set_bit(MPTCP_FLUSH_JOIN_LIST, &msk->cb_flags);
	}
	mptcp_data_unlock(parent);

	if (!ret) {
err_prohibited:
		subflow->reset_reason = MPTCP_RST_EPROHIBIT;
		return false;
	}

	return true;
}

static void mptcp_shutdown(struct sock *sk, int how)
{
	pr_debug("sk=%p, how=%d", sk, how);

	if ((how & SEND_SHUTDOWN) && mptcp_close_state(sk))
		__mptcp_wr_shutdown(sk);
}

static int mptcp_forward_alloc_get(const struct sock *sk)
{
	return READ_ONCE(sk->sk_forward_alloc) +
	       READ_ONCE(mptcp_sk(sk)->rmem_fwd_alloc);
}

static int mptcp_ioctl_outq(const struct mptcp_sock *msk, u64 v)
{
	const struct sock *sk = (void *)msk;
	u64 delta;

	if (sk->sk_state == TCP_LISTEN)
		return -EINVAL;

	if ((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV))
		return 0;

	delta = msk->write_seq - v;
	if (__mptcp_check_fallback(msk) && msk->first) {
		struct tcp_sock *tp = tcp_sk(msk->first);

		/* the first subflow is disconnected after close - see
		 * __mptcp_close_ssk(). tcp_disconnect() moves the write_seq
		 * so ignore that status, too.
		 */
		if (!((1 << msk->first->sk_state) &
		      (TCPF_SYN_SENT | TCPF_SYN_RECV | TCPF_CLOSE)))
			delta += READ_ONCE(tp->write_seq) - tp->snd_una;
	}
	if (delta > INT_MAX)
		delta = INT_MAX;

	return (int)delta;
}

static int mptcp_ioctl(struct sock *sk, int cmd, int *karg)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool slow;

	switch (cmd) {
	case SIOCINQ:
		if (sk->sk_state == TCP_LISTEN)
			return -EINVAL;

		lock_sock(sk);
		__mptcp_move_skbs(msk);
		*karg = mptcp_inq_hint(sk);
		release_sock(sk);
		break;
	case SIOCOUTQ:
		slow = lock_sock_fast(sk);
		*karg = mptcp_ioctl_outq(msk, READ_ONCE(msk->snd_una));
		unlock_sock_fast(sk, slow);
		break;
	case SIOCOUTQNSD:
		slow = lock_sock_fast(sk);
		*karg = mptcp_ioctl_outq(msk, msk->snd_nxt);
		unlock_sock_fast(sk, slow);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static void mptcp_subflow_early_fallback(struct mptcp_sock *msk,
					 struct mptcp_subflow_context *subflow)
{
	subflow->request_mptcp = 0;
	__mptcp_do_fallback(msk);
}

static int mptcp_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);
	int err = -EINVAL;
	struct sock *ssk;

	ssk = __mptcp_nmpc_sk(msk);
	if (IS_ERR(ssk))
		return PTR_ERR(ssk);

	inet_sk_state_store(sk, TCP_SYN_SENT);
	subflow = mptcp_subflow_ctx(ssk);
#ifdef CONFIG_TCP_MD5SIG
	/* no MPTCP if MD5SIG is enabled on this socket or we may run out of
	 * TCP option space.
	 */
	if (rcu_access_pointer(tcp_sk(ssk)->md5sig_info))
		mptcp_subflow_early_fallback(msk, subflow);
#endif
	if (subflow->request_mptcp && mptcp_token_new_connect(ssk)) {
		MPTCP_INC_STATS(sock_net(ssk), MPTCP_MIB_TOKENFALLBACKINIT);
		mptcp_subflow_early_fallback(msk, subflow);
	}
	if (likely(!__mptcp_check_fallback(msk)))
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPCAPABLEACTIVE);

	/* if reaching here via the fastopen/sendmsg path, the caller already
	 * acquired the subflow socket lock, too.
	 */
	if (!msk->fastopening)
		lock_sock(ssk);

	/* the following mirrors closely a very small chunk of code from
	 * __inet_stream_connect()
	 */
	if (ssk->sk_state != TCP_CLOSE)
		goto out;

	if (BPF_CGROUP_PRE_CONNECT_ENABLED(ssk)) {
		err = ssk->sk_prot->pre_connect(ssk, uaddr, addr_len);
		if (err)
			goto out;
	}

	err = ssk->sk_prot->connect(ssk, uaddr, addr_len);
	if (err < 0)
		goto out;

	inet_assign_bit(DEFER_CONNECT, sk, inet_test_bit(DEFER_CONNECT, ssk));

out:
	if (!msk->fastopening)
		release_sock(ssk);

	/* on successful connect, the msk state will be moved to established by
	 * subflow_finish_connect()
	 */
	if (unlikely(err)) {
		/* avoid leaving a dangling token in an unconnected socket */
		mptcp_token_destroy(msk);
		inet_sk_state_store(sk, TCP_CLOSE);
		return err;
	}

	mptcp_copy_inaddrs(sk, ssk);
	return 0;
}

static struct proto mptcp_prot = {
	.name		= "MPTCP",
	.owner		= THIS_MODULE,
	.init		= mptcp_init_sock,
	.connect	= mptcp_connect,
	.disconnect	= mptcp_disconnect,
	.close		= mptcp_close,
	.accept		= mptcp_accept,
	.setsockopt	= mptcp_setsockopt,
	.getsockopt	= mptcp_getsockopt,
	.shutdown	= mptcp_shutdown,
	.destroy	= mptcp_destroy,
	.sendmsg	= mptcp_sendmsg,
	.ioctl		= mptcp_ioctl,
	.recvmsg	= mptcp_recvmsg,
	.release_cb	= mptcp_release_cb,
	.hash		= mptcp_hash,
	.unhash		= mptcp_unhash,
	.get_port	= mptcp_get_port,
	.forward_alloc_get	= mptcp_forward_alloc_get,
	.sockets_allocated	= &mptcp_sockets_allocated,

	.memory_allocated	= &tcp_memory_allocated,
	.per_cpu_fw_alloc	= &tcp_memory_per_cpu_fw_alloc,

	.memory_pressure	= &tcp_memory_pressure,
	.sysctl_wmem_offset	= offsetof(struct net, ipv4.sysctl_tcp_wmem),
	.sysctl_rmem_offset	= offsetof(struct net, ipv4.sysctl_tcp_rmem),
	.sysctl_mem	= sysctl_tcp_mem,
	.obj_size	= sizeof(struct mptcp_sock),
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
	.no_autobind	= true,
};

static int mptcp_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct sock *ssk, *sk = sock->sk;
	int err = -EINVAL;

	lock_sock(sk);
	ssk = __mptcp_nmpc_sk(msk);
	if (IS_ERR(ssk)) {
		err = PTR_ERR(ssk);
		goto unlock;
	}

	if (sk->sk_family == AF_INET)
		err = inet_bind_sk(ssk, uaddr, addr_len);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (sk->sk_family == AF_INET6)
		err = inet6_bind_sk(ssk, uaddr, addr_len);
#endif
	if (!err)
		mptcp_copy_inaddrs(sk, ssk);

unlock:
	release_sock(sk);
	return err;
}

static int mptcp_listen(struct socket *sock, int backlog)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct sock *sk = sock->sk;
	struct sock *ssk;
	int err;

	pr_debug("msk=%p", msk);

	lock_sock(sk);

	err = -EINVAL;
	if (sock->state != SS_UNCONNECTED || sock->type != SOCK_STREAM)
		goto unlock;

	ssk = __mptcp_nmpc_sk(msk);
	if (IS_ERR(ssk)) {
		err = PTR_ERR(ssk);
		goto unlock;
	}

	inet_sk_state_store(sk, TCP_LISTEN);
	sock_set_flag(sk, SOCK_RCU_FREE);

	lock_sock(ssk);
	err = __inet_listen_sk(ssk, backlog);
	release_sock(ssk);
	inet_sk_state_store(sk, inet_sk_state_load(ssk));

	if (!err) {
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
		mptcp_copy_inaddrs(sk, ssk);
		mptcp_event_pm_listener(ssk, MPTCP_EVENT_LISTENER_CREATED);
	}

unlock:
	release_sock(sk);
	return err;
}

static int mptcp_stream_accept(struct socket *sock, struct socket *newsock,
			       int flags, bool kern)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct sock *ssk, *newsk;
	int err;

	pr_debug("msk=%p", msk);

	/* Buggy applications can call accept on socket states other then LISTEN
	 * but no need to allocate the first subflow just to error out.
	 */
	ssk = READ_ONCE(msk->first);
	if (!ssk)
		return -EINVAL;

	newsk = mptcp_accept(ssk, flags, &err, kern);
	if (!newsk)
		return err;

	lock_sock(newsk);

	__inet_accept(sock, newsock, newsk);
	if (!mptcp_is_tcpsk(newsock->sk)) {
		struct mptcp_sock *msk = mptcp_sk(newsk);
		struct mptcp_subflow_context *subflow;

		set_bit(SOCK_CUSTOM_SOCKOPT, &newsock->flags);
		msk->in_accept_queue = 0;

		/* set ssk->sk_socket of accept()ed flows to mptcp socket.
		 * This is needed so NOSPACE flag can be set from tcp stack.
		 */
		mptcp_for_each_subflow(msk, subflow) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

			if (!ssk->sk_socket)
				mptcp_sock_graft(ssk, newsock);
		}

		/* Do late cleanup for the first subflow as necessary. Also
		 * deal with bad peers not doing a complete shutdown.
		 */
		if (unlikely(inet_sk_state_load(msk->first) == TCP_CLOSE)) {
			__mptcp_close_ssk(newsk, msk->first,
					  mptcp_subflow_ctx(msk->first), 0);
			if (unlikely(list_is_singular(&msk->conn_list)))
				inet_sk_state_store(newsk, TCP_CLOSE);
		}
	}
	release_sock(newsk);

	return 0;
}

static __poll_t mptcp_check_writeable(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;

	if (sk_stream_is_writeable(sk))
		return EPOLLOUT | EPOLLWRNORM;

	mptcp_set_nospace(sk);
	smp_mb__after_atomic(); /* msk->flags is changed by write_space cb */
	if (sk_stream_is_writeable(sk))
		return EPOLLOUT | EPOLLWRNORM;

	return 0;
}

static __poll_t mptcp_poll(struct file *file, struct socket *sock,
			   struct poll_table_struct *wait)
{
	struct sock *sk = sock->sk;
	struct mptcp_sock *msk;
	__poll_t mask = 0;
	u8 shutdown;
	int state;

	msk = mptcp_sk(sk);
	sock_poll_wait(file, sock, wait);

	state = inet_sk_state_load(sk);
	pr_debug("msk=%p state=%d flags=%lx", msk, state, msk->flags);
	if (state == TCP_LISTEN) {
		struct sock *ssk = READ_ONCE(msk->first);

		if (WARN_ON_ONCE(!ssk))
			return 0;

		return inet_csk_listen_poll(ssk);
	}

	shutdown = READ_ONCE(sk->sk_shutdown);
	if (shutdown == SHUTDOWN_MASK || state == TCP_CLOSE)
		mask |= EPOLLHUP;
	if (shutdown & RCV_SHUTDOWN)
		mask |= EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;

	if (state != TCP_SYN_SENT && state != TCP_SYN_RECV) {
		mask |= mptcp_check_readable(sk);
		if (shutdown & SEND_SHUTDOWN)
			mask |= EPOLLOUT | EPOLLWRNORM;
		else
			mask |= mptcp_check_writeable(msk);
	} else if (state == TCP_SYN_SENT &&
		   inet_test_bit(DEFER_CONNECT, sk)) {
		/* cf tcp_poll() note about TFO */
		mask |= EPOLLOUT | EPOLLWRNORM;
	}

	/* This barrier is coupled with smp_wmb() in __mptcp_error_report() */
	smp_rmb();
	if (READ_ONCE(sk->sk_err))
		mask |= EPOLLERR;

	return mask;
}

static const struct proto_ops mptcp_stream_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = mptcp_bind,
	.connect	   = inet_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = mptcp_stream_accept,
	.getname	   = inet_getname,
	.poll		   = mptcp_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = mptcp_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.set_rcvlowat	   = mptcp_set_rcvlowat,
};

static struct inet_protosw mptcp_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_MPTCP,
	.prot		= &mptcp_prot,
	.ops		= &mptcp_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

static int mptcp_napi_poll(struct napi_struct *napi, int budget)
{
	struct mptcp_delegated_action *delegated;
	struct mptcp_subflow_context *subflow;
	int work_done = 0;

	delegated = container_of(napi, struct mptcp_delegated_action, napi);
	while ((subflow = mptcp_subflow_delegated_next(delegated)) != NULL) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		bh_lock_sock_nested(ssk);
		if (!sock_owned_by_user(ssk)) {
			mptcp_subflow_process_delegated(ssk, xchg(&subflow->delegated_status, 0));
		} else {
			/* tcp_release_cb_override already processed
			 * the action or will do at next release_sock().
			 * In both case must dequeue the subflow here - on the same
			 * CPU that scheduled it.
			 */
			smp_wmb();
			clear_bit(MPTCP_DELEGATE_SCHEDULED, &subflow->delegated_status);
		}
		bh_unlock_sock(ssk);
		sock_put(ssk);

		if (++work_done == budget)
			return budget;
	}

	/* always provide a 0 'work_done' argument, so that napi_complete_done
	 * will not try accessing the NULL napi->dev ptr
	 */
	napi_complete_done(napi, 0);
	return work_done;
}

void __init mptcp_proto_init(void)
{
	struct mptcp_delegated_action *delegated;
	int cpu;

	mptcp_prot.h.hashinfo = tcp_prot.h.hashinfo;

	if (percpu_counter_init(&mptcp_sockets_allocated, 0, GFP_KERNEL))
		panic("Failed to allocate MPTCP pcpu counter\n");

	init_dummy_netdev(&mptcp_napi_dev);
	for_each_possible_cpu(cpu) {
		delegated = per_cpu_ptr(&mptcp_delegated_actions, cpu);
		INIT_LIST_HEAD(&delegated->head);
		netif_napi_add_tx(&mptcp_napi_dev, &delegated->napi,
				  mptcp_napi_poll);
		napi_enable(&delegated->napi);
	}

	mptcp_subflow_init();
	mptcp_pm_init();
	mptcp_sched_init();
	mptcp_token_init();

	if (proto_register(&mptcp_prot, 1) != 0)
		panic("Failed to register MPTCP proto.\n");

	inet_register_protosw(&mptcp_protosw);

	BUILD_BUG_ON(sizeof(struct mptcp_skb_cb) > sizeof_field(struct sk_buff, cb));
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static const struct proto_ops mptcp_v6_stream_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = mptcp_bind,
	.connect	   = inet_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = mptcp_stream_accept,
	.getname	   = inet6_getname,
	.poll		   = mptcp_poll,
	.ioctl		   = inet6_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = mptcp_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet6_sendmsg,
	.recvmsg	   = inet6_recvmsg,
	.mmap		   = sock_no_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet6_compat_ioctl,
#endif
	.set_rcvlowat	   = mptcp_set_rcvlowat,
};

static struct proto mptcp_v6_prot;

static struct inet_protosw mptcp_v6_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_MPTCP,
	.prot		= &mptcp_v6_prot,
	.ops		= &mptcp_v6_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

int __init mptcp_proto_v6_init(void)
{
	int err;

	mptcp_v6_prot = mptcp_prot;
	strcpy(mptcp_v6_prot.name, "MPTCPv6");
	mptcp_v6_prot.slab = NULL;
	mptcp_v6_prot.obj_size = sizeof(struct mptcp6_sock);
	mptcp_v6_prot.ipv6_pinfo_offset = offsetof(struct mptcp6_sock, np);

	err = proto_register(&mptcp_v6_prot, 1);
	if (err)
		return err;

	err = inet6_register_protosw(&mptcp_v6_protosw);
	if (err)
		proto_unregister(&mptcp_v6_prot);

	return err;
}
#endif
