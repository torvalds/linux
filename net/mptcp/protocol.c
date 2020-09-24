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
#include "protocol.h"
#include "mib.h"

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
struct mptcp6_sock {
	struct mptcp_sock msk;
	struct ipv6_pinfo np;
};
#endif

struct mptcp_skb_cb {
	u64 map_seq;
	u64 end_seq;
	u32 offset;
};

#define MPTCP_SKB_CB(__skb)	((struct mptcp_skb_cb *)&((__skb)->cb[0]))

static struct percpu_counter mptcp_sockets_allocated;

/* If msk has an initial subflow socket, and the MP_CAPABLE handshake has not
 * completed yet or has failed, return the subflow socket.
 * Otherwise return NULL.
 */
static struct socket *__mptcp_nmpc_socket(const struct mptcp_sock *msk)
{
	if (!msk->subflow || READ_ONCE(msk->can_ack))
		return NULL;

	return msk->subflow;
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
		sock->ops = &inet_stream_ops;
		return true;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	} else if (unlikely(sk->sk_prot == &tcpv6_prot)) {
		sock->ops = &inet6_stream_ops;
		return true;
#endif
	}

	return false;
}

static struct sock *__mptcp_tcp_fallback(struct mptcp_sock *msk)
{
	sock_owned_by_me((const struct sock *)msk);

	if (likely(!__mptcp_check_fallback(msk)))
		return NULL;

	return msk->first;
}

static int __mptcp_socket_create(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	struct socket *ssock;
	int err;

	err = mptcp_subflow_create_socket(sk, &ssock);
	if (err)
		return err;

	msk->first = ssock->sk;
	msk->subflow = ssock;
	subflow = mptcp_subflow_ctx(ssock->sk);
	list_add(&subflow->node, &msk->conn_list);
	subflow->request_mptcp = 1;

	/* accept() will wait on first subflow sk_wq, and we always wakes up
	 * via msk->sk_socket
	 */
	RCU_INIT_POINTER(msk->first->sk_wq, &sk->sk_socket->wq);

	return 0;
}

static void mptcp_drop(struct sock *sk, struct sk_buff *skb)
{
	sk_drops_add(sk, skb);
	__kfree_skb(skb);
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
	kfree_skb_partial(from, fragstolen);
	atomic_add(delta, &sk->sk_rmem_alloc);
	sk_mem_charge(sk, delta);
	return true;
}

static bool mptcp_ooo_try_coalesce(struct mptcp_sock *msk, struct sk_buff *to,
				   struct sk_buff *from)
{
	if (MPTCP_SKB_CB(from)->map_seq != MPTCP_SKB_CB(to)->end_seq)
		return false;

	return mptcp_try_coalesce((struct sock *)msk, to, from);
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
	int space;

	seq = MPTCP_SKB_CB(skb)->map_seq;
	end_seq = MPTCP_SKB_CB(skb)->end_seq;
	space = tcp_space(sk);
	max_seq = space > 0 ? space + msk->ack_seq : msk->ack_seq;

	pr_debug("msk=%p seq=%llx limit=%llx empty=%d", msk, seq, max_seq,
		 RB_EMPTY_ROOT(&msk->out_of_order_queue));
	if (after64(seq, max_seq)) {
		/* out of window */
		mptcp_drop(sk, skb);
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
	skb_set_owner_r(skb, sk);
}

static bool __mptcp_move_skb(struct mptcp_sock *msk, struct sock *ssk,
			     struct sk_buff *skb, unsigned int offset,
			     size_t copy_len)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = (struct sock *)msk;
	struct sk_buff *tail;

	__skb_unlink(skb, &ssk->sk_receive_queue);

	skb_ext_reset(skb);
	skb_orphan(skb);

	/* the skb map_seq accounts for the skb offset:
	 * mptcp_subflow_get_mapped_dsn() is based on the current tp->copied_seq
	 * value
	 */
	MPTCP_SKB_CB(skb)->map_seq = mptcp_subflow_get_mapped_dsn(subflow);
	MPTCP_SKB_CB(skb)->end_seq = MPTCP_SKB_CB(skb)->map_seq + copy_len;
	MPTCP_SKB_CB(skb)->offset = offset;

	if (MPTCP_SKB_CB(skb)->map_seq == msk->ack_seq) {
		/* in sequence */
		msk->ack_seq += copy_len;
		tail = skb_peek_tail(&sk->sk_receive_queue);
		if (tail && mptcp_try_coalesce(sk, tail, skb))
			return true;

		skb_set_owner_r(skb, sk);
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
	mptcp_drop(sk, skb);
	return false;
}

static void mptcp_stop_timer(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	sk_stop_timer(sk, &icsk->icsk_retransmit_timer);
	mptcp_sk(sk)->timer_ival = 0;
}

static void mptcp_check_data_fin_ack(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (__mptcp_check_fallback(msk))
		return;

	/* Look for an acknowledged DATA_FIN */
	if (((1 << sk->sk_state) &
	     (TCPF_FIN_WAIT1 | TCPF_CLOSING | TCPF_LAST_ACK)) &&
	    msk->write_seq == atomic64_read(&msk->snd_una)) {
		mptcp_stop_timer(sk);

		WRITE_ONCE(msk->snd_data_fin_enable, 0);

		switch (sk->sk_state) {
		case TCP_FIN_WAIT1:
			inet_sk_state_store(sk, TCP_FIN_WAIT2);
			sk->sk_state_change(sk);
			break;
		case TCP_CLOSING:
		case TCP_LAST_ACK:
			inet_sk_state_store(sk, TCP_CLOSE);
			sk->sk_state_change(sk);
			break;
		}

		if (sk->sk_shutdown == SHUTDOWN_MASK ||
		    sk->sk_state == TCP_CLOSE)
			sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
		else
			sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
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

static void mptcp_set_timeout(const struct sock *sk, const struct sock *ssk)
{
	long tout = ssk && inet_csk(ssk)->icsk_pending ?
				      inet_csk(ssk)->icsk_timeout - jiffies : 0;

	if (tout <= 0)
		tout = mptcp_sk(sk)->timer_ival;
	mptcp_sk(sk)->timer_ival = tout > 0 ? tout : TCP_RTO_MIN;
}

static void mptcp_check_data_fin(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	u64 rcv_data_fin_seq;

	if (__mptcp_check_fallback(msk) || !msk->first)
		return;

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
		struct mptcp_subflow_context *subflow;

		msk->ack_seq++;
		WRITE_ONCE(msk->rcv_data_fin, 0);

		sk->sk_shutdown |= RCV_SHUTDOWN;
		smp_mb__before_atomic(); /* SHUTDOWN must be visible first */
		set_bit(MPTCP_DATA_READY, &msk->flags);

		switch (sk->sk_state) {
		case TCP_ESTABLISHED:
			inet_sk_state_store(sk, TCP_CLOSE_WAIT);
			break;
		case TCP_FIN_WAIT1:
			inet_sk_state_store(sk, TCP_CLOSING);
			break;
		case TCP_FIN_WAIT2:
			inet_sk_state_store(sk, TCP_CLOSE);
			// @@ Close subflows now?
			break;
		default:
			/* Other states not expected */
			WARN_ON_ONCE(1);
			break;
		}

		mptcp_set_timeout(sk, NULL);
		mptcp_for_each_subflow(msk, subflow) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

			lock_sock(ssk);
			tcp_send_ack(ssk);
			release_sock(ssk);
		}

		sk->sk_state_change(sk);

		if (sk->sk_shutdown == SHUTDOWN_MASK ||
		    sk->sk_state == TCP_CLOSE)
			sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
		else
			sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	}
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
		if (!skb)
			break;

		if (__mptcp_check_fallback(msk)) {
			/* if we are running under the workqueue, TCP could have
			 * collapsed skbs between dummy map creation and now
			 * be sure to adjust the size
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

		if (atomic_read(&sk->sk_rmem_alloc) > READ_ONCE(sk->sk_rcvbuf)) {
			done = true;
			break;
		}
	} while (more_data_avail);

	*bytes += moved;
	if (moved)
		tcp_cleanup_rbuf(ssk, moved);

	return done;
}

static bool mptcp_ofo_queue(struct mptcp_sock *msk)
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
			__skb_queue_tail(&sk->sk_receive_queue, skb);
		}
		msk->ack_seq = end_seq;
		moved = true;
	}
	return moved;
}

/* In most cases we will be able to lock the mptcp socket.  If its already
 * owned, we need to defer to the work queue to avoid ABBA deadlock.
 */
static bool move_skbs_to_msk(struct mptcp_sock *msk, struct sock *ssk)
{
	struct sock *sk = (struct sock *)msk;
	unsigned int moved = 0;

	if (READ_ONCE(sk->sk_lock.owned))
		return false;

	if (unlikely(!spin_trylock_bh(&sk->sk_lock.slock)))
		return false;

	/* must re-check after taking the lock */
	if (!READ_ONCE(sk->sk_lock.owned)) {
		__mptcp_move_skbs_from_subflow(msk, ssk, &moved);
		mptcp_ofo_queue(msk);

		/* If the moves have caught up with the DATA_FIN sequence number
		 * it's time to ack the DATA_FIN and change socket state, but
		 * this is not a good place to change state. Let the workqueue
		 * do it.
		 */
		if (mptcp_pending_data_fin(sk, NULL) &&
		    schedule_work(&msk->work))
			sock_hold(sk);
	}

	spin_unlock_bh(&sk->sk_lock.slock);

	return moved > 0;
}

void mptcp_data_ready(struct sock *sk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool wake;

	/* move_skbs_to_msk below can legitly clear the data_avail flag,
	 * but we will need later to properly woke the reader, cache its
	 * value
	 */
	wake = subflow->data_avail == MPTCP_SUBFLOW_DATA_AVAIL;
	if (wake)
		set_bit(MPTCP_DATA_READY, &msk->flags);

	if (atomic_read(&sk->sk_rmem_alloc) < READ_ONCE(sk->sk_rcvbuf) &&
	    move_skbs_to_msk(msk, ssk))
		goto wake;

	/* don't schedule if mptcp sk is (still) over limit */
	if (atomic_read(&sk->sk_rmem_alloc) > READ_ONCE(sk->sk_rcvbuf))
		goto wake;

	/* mptcp socket is owned, release_cb should retry */
	if (!test_and_set_bit(TCP_DELACK_TIMER_DEFERRED,
			      &sk->sk_tsq_flags)) {
		sock_hold(sk);

		/* need to try again, its possible release_cb() has already
		 * been called after the test_and_set_bit() above.
		 */
		move_skbs_to_msk(msk, ssk);
	}
wake:
	if (wake)
		sk->sk_data_ready(sk);
}

static void __mptcp_flush_join_list(struct mptcp_sock *msk)
{
	if (likely(list_empty(&msk->join_list)))
		return;

	spin_lock_bh(&msk->join_list_lock);
	list_splice_tail_init(&msk->join_list, &msk->conn_list);
	spin_unlock_bh(&msk->join_list_lock);
}

static bool mptcp_timer_pending(struct sock *sk)
{
	return timer_pending(&inet_csk(sk)->icsk_retransmit_timer);
}

static void mptcp_reset_timer(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	unsigned long tout;

	/* should never be called with mptcp level timer cleared */
	tout = READ_ONCE(mptcp_sk(sk)->timer_ival);
	if (WARN_ON_ONCE(!tout))
		tout = TCP_RTO_MIN;
	sk_reset_timer(sk, &icsk->icsk_retransmit_timer, jiffies + tout);
}

void mptcp_data_acked(struct sock *sk)
{
	mptcp_reset_timer(sk);

	if ((!test_bit(MPTCP_SEND_SPACE, &mptcp_sk(sk)->flags) ||
	     (inet_sk_state_load(sk) != TCP_ESTABLISHED)) &&
	    schedule_work(&mptcp_sk(sk)->work))
		sock_hold(sk);
}

void mptcp_subflow_eof(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (!test_and_set_bit(MPTCP_WORK_EOF, &msk->flags) &&
	    schedule_work(&msk->work))
		sock_hold(sk);
}

static void mptcp_check_for_eof(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	int receivers = 0;

	mptcp_for_each_subflow(msk, subflow)
		receivers += !subflow->rx_eof;

	if (!receivers && !(sk->sk_shutdown & RCV_SHUTDOWN)) {
		/* hopefully temporary hack: propagate shutdown status
		 * to msk, when all subflows agree on it
		 */
		sk->sk_shutdown |= RCV_SHUTDOWN;

		smp_mb__before_atomic(); /* SHUTDOWN must be visible first */
		set_bit(MPTCP_DATA_READY, &msk->flags);
		sk->sk_data_ready(sk);
	}
}

static bool mptcp_ext_cache_refill(struct mptcp_sock *msk)
{
	const struct sock *sk = (const struct sock *)msk;

	if (!msk->cached_ext)
		msk->cached_ext = __skb_ext_alloc(sk->sk_allocation);

	return !!msk->cached_ext;
}

static struct sock *mptcp_subflow_recv_lookup(const struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;

	sock_owned_by_me(sk);

	mptcp_for_each_subflow(msk, subflow) {
		if (subflow->data_avail)
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

	/* can collapse only if MPTCP level sequence is in order */
	return mpext && mpext->data_seq + mpext->data_len == write_seq;
}

static bool mptcp_frag_can_collapse_to(const struct mptcp_sock *msk,
				       const struct page_frag *pfrag,
				       const struct mptcp_data_frag *df)
{
	return df && pfrag->page == df->page &&
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

static bool mptcp_is_writeable(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	if (!sk_stream_is_writeable((struct sock *)msk))
		return false;

	mptcp_for_each_subflow(msk, subflow) {
		if (sk_stream_is_writeable(subflow->tcp_sock))
			return true;
	}
	return false;
}

static void mptcp_clean_una(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_data_frag *dtmp, *dfrag;
	bool cleaned = false;
	u64 snd_una;

	/* on fallback we just need to ignore snd_una, as this is really
	 * plain TCP
	 */
	if (__mptcp_check_fallback(msk))
		atomic64_set(&msk->snd_una, msk->write_seq);
	snd_una = atomic64_read(&msk->snd_una);

	list_for_each_entry_safe(dfrag, dtmp, &msk->rtx_queue, list) {
		if (after64(dfrag->data_seq + dfrag->data_len, snd_una))
			break;

		dfrag_clear(sk, dfrag);
		cleaned = true;
	}

	dfrag = mptcp_rtx_head(sk);
	if (dfrag && after64(snd_una, dfrag->data_seq)) {
		u64 delta = snd_una - dfrag->data_seq;

		if (WARN_ON_ONCE(delta > dfrag->data_len))
			goto out;

		dfrag->data_seq += delta;
		dfrag->offset += delta;
		dfrag->data_len -= delta;

		dfrag_uncharge(sk, delta);
		cleaned = true;
	}

out:
	if (cleaned) {
		sk_mem_reclaim_partial(sk);

		/* Only wake up writers if a subflow is ready */
		if (mptcp_is_writeable(msk)) {
			set_bit(MPTCP_SEND_SPACE, &mptcp_sk(sk)->flags);
			smp_mb__after_atomic();

			/* set SEND_SPACE before sk_stream_write_space clears
			 * NOSPACE
			 */
			sk_stream_write_space(sk);
		}
	}
}

/* ensure we get enough memory for the frag hdr, beyond some minimal amount of
 * data
 */
static bool mptcp_page_frag_refill(struct sock *sk, struct page_frag *pfrag)
{
	if (likely(skb_page_frag_refill(32U + sizeof(struct mptcp_data_frag),
					pfrag, sk->sk_allocation)))
		return true;

	sk->sk_prot->enter_memory_pressure(sk);
	sk_stream_moderate_sndbuf(sk);
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
	dfrag->page = pfrag->page;

	return dfrag;
}

static int mptcp_sendmsg_frag(struct sock *sk, struct sock *ssk,
			      struct msghdr *msg, struct mptcp_data_frag *dfrag,
			      long *timeo, int *pmss_now,
			      int *ps_goal)
{
	int mss_now, avail_size, size_goal, offset, ret, frag_truesize = 0;
	bool dfrag_collapsed, can_collapse = false;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_ext *mpext = NULL;
	bool retransmission = !!dfrag;
	struct sk_buff *skb, *tail;
	struct page_frag *pfrag;
	struct page *page;
	u64 *write_seq;
	size_t psize;

	/* use the mptcp page cache so that we can easily move the data
	 * from one substream to another, but do per subflow memory accounting
	 * Note: pfrag is used only !retransmission, but the compiler if
	 * fooled into a warning if we don't init here
	 */
	pfrag = sk_page_frag(sk);
	if (!retransmission) {
		write_seq = &msk->write_seq;
		page = pfrag->page;
	} else {
		write_seq = &dfrag->data_seq;
		page = dfrag->page;
	}

	/* compute copy limit */
	mss_now = tcp_send_mss(ssk, &size_goal, msg->msg_flags);
	*pmss_now = mss_now;
	*ps_goal = size_goal;
	avail_size = size_goal;
	skb = tcp_write_queue_tail(ssk);
	if (skb) {
		mpext = skb_ext_find(skb, SKB_EXT_MPTCP);

		/* Limit the write to the size available in the
		 * current skb, if any, so that we create at most a new skb.
		 * Explicitly tells TCP internals to avoid collapsing on later
		 * queue management operation, to avoid breaking the ext <->
		 * SSN association set here
		 */
		can_collapse = (size_goal - skb->len > 0) &&
			      mptcp_skb_can_collapse_to(*write_seq, skb, mpext);
		if (!can_collapse)
			TCP_SKB_CB(skb)->eor = 1;
		else
			avail_size = size_goal - skb->len;
	}

	if (!retransmission) {
		/* reuse tail pfrag, if possible, or carve a new one from the
		 * page allocator
		 */
		dfrag = mptcp_rtx_tail(sk);
		offset = pfrag->offset;
		dfrag_collapsed = mptcp_frag_can_collapse_to(msk, pfrag, dfrag);
		if (!dfrag_collapsed) {
			dfrag = mptcp_carve_data_frag(msk, pfrag, offset);
			offset = dfrag->offset;
			frag_truesize = dfrag->overhead;
		}
		psize = min_t(size_t, pfrag->size - offset, avail_size);

		/* Copy to page */
		pr_debug("left=%zu", msg_data_left(msg));
		psize = copy_page_from_iter(pfrag->page, offset,
					    min_t(size_t, msg_data_left(msg),
						  psize),
					    &msg->msg_iter);
		pr_debug("left=%zu", msg_data_left(msg));
		if (!psize)
			return -EINVAL;

		if (!sk_wmem_schedule(sk, psize + dfrag->overhead)) {
			iov_iter_revert(&msg->msg_iter, psize);
			return -ENOMEM;
		}
	} else {
		offset = dfrag->offset;
		psize = min_t(size_t, dfrag->data_len, avail_size);
	}

	/* tell the TCP stack to delay the push so that we can safely
	 * access the skb after the sendpages call
	 */
	ret = do_tcp_sendpages(ssk, page, offset, psize,
			       msg->msg_flags | MSG_SENDPAGE_NOTLAST | MSG_DONTWAIT);
	if (ret <= 0) {
		if (!retransmission)
			iov_iter_revert(&msg->msg_iter, psize);
		return ret;
	}

	frag_truesize += ret;
	if (!retransmission) {
		if (unlikely(ret < psize))
			iov_iter_revert(&msg->msg_iter, psize - ret);

		/* send successful, keep track of sent data for mptcp-level
		 * retransmission
		 */
		dfrag->data_len += ret;
		if (!dfrag_collapsed) {
			get_page(dfrag->page);
			list_add_tail(&dfrag->list, &msk->rtx_queue);
			sk_wmem_queued_add(sk, frag_truesize);
		} else {
			sk_wmem_queued_add(sk, ret);
		}

		/* charge data on mptcp rtx queue to the master socket
		 * Note: we charge such data both to sk and ssk
		 */
		sk->sk_forward_alloc -= frag_truesize;
	}

	/* if the tail skb extension is still the cached one, collapsing
	 * really happened. Note: we can't check for 'same skb' as the sk_buff
	 * hdr on tail can be transmitted, freed and re-allocated by the
	 * do_tcp_sendpages() call
	 */
	tail = tcp_write_queue_tail(ssk);
	if (mpext && tail && mpext == skb_ext_find(tail, SKB_EXT_MPTCP)) {
		WARN_ON_ONCE(!can_collapse);
		mpext->data_len += ret;
		goto out;
	}

	skb = tcp_write_queue_tail(ssk);
	mpext = __skb_ext_set(skb, SKB_EXT_MPTCP, msk->cached_ext);
	msk->cached_ext = NULL;

	memset(mpext, 0, sizeof(*mpext));
	mpext->data_seq = *write_seq;
	mpext->subflow_seq = mptcp_subflow_ctx(ssk)->rel_write_seq;
	mpext->data_len = ret;
	mpext->use_map = 1;
	mpext->dsn64 = 1;

	pr_debug("data_seq=%llu subflow_seq=%u data_len=%u dsn64=%d",
		 mpext->data_seq, mpext->subflow_seq, mpext->data_len,
		 mpext->dsn64);

out:
	if (!retransmission)
		pfrag->offset += frag_truesize;
	WRITE_ONCE(*write_seq, *write_seq + ret);
	mptcp_subflow_ctx(ssk)->rel_write_seq += ret;

	return ret;
}

static void mptcp_nospace(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	clear_bit(MPTCP_SEND_SPACE, &msk->flags);
	smp_mb__after_atomic(); /* msk->flags is changed by write_space cb */

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		struct socket *sock = READ_ONCE(ssk->sk_socket);

		/* enables ssk->write_space() callbacks */
		if (sock)
			set_bit(SOCK_NOSPACE, &sock->flags);
	}
}

static bool mptcp_subflow_active(struct mptcp_subflow_context *subflow)
{
	struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

	/* can't send if JOIN hasn't completed yet (i.e. is usable for mptcp) */
	if (subflow->request_join && !subflow->fully_established)
		return false;

	/* only send if our side has not closed yet */
	return ((1 << ssk->sk_state) & (TCPF_ESTABLISHED | TCPF_CLOSE_WAIT));
}

#define MPTCP_SEND_BURST_SIZE		((1 << 16) - \
					 sizeof(struct tcphdr) - \
					 MAX_TCP_OPTION_SPACE - \
					 sizeof(struct ipv6hdr) - \
					 sizeof(struct frag_hdr))

struct subflow_send_info {
	struct sock *ssk;
	u64 ratio;
};

static struct sock *mptcp_subflow_get_send(struct mptcp_sock *msk,
					   u32 *sndbuf)
{
	struct subflow_send_info send_info[2];
	struct mptcp_subflow_context *subflow;
	int i, nr_active = 0;
	struct sock *ssk;
	u64 ratio;
	u32 pace;

	sock_owned_by_me((struct sock *)msk);

	*sndbuf = 0;
	if (!mptcp_ext_cache_refill(msk))
		return NULL;

	if (__mptcp_check_fallback(msk)) {
		if (!msk->first)
			return NULL;
		*sndbuf = msk->first->sk_sndbuf;
		return sk_stream_memory_free(msk->first) ? msk->first : NULL;
	}

	/* re-use last subflow, if the burst allow that */
	if (msk->last_snd && msk->snd_burst > 0 &&
	    sk_stream_memory_free(msk->last_snd) &&
	    mptcp_subflow_active(mptcp_subflow_ctx(msk->last_snd))) {
		mptcp_for_each_subflow(msk, subflow) {
			ssk =  mptcp_subflow_tcp_sock(subflow);
			*sndbuf = max(tcp_sk(ssk)->snd_wnd, *sndbuf);
		}
		return msk->last_snd;
	}

	/* pick the subflow with the lower wmem/wspace ratio */
	for (i = 0; i < 2; ++i) {
		send_info[i].ssk = NULL;
		send_info[i].ratio = -1;
	}
	mptcp_for_each_subflow(msk, subflow) {
		ssk =  mptcp_subflow_tcp_sock(subflow);
		if (!mptcp_subflow_active(subflow))
			continue;

		nr_active += !subflow->backup;
		*sndbuf = max(tcp_sk(ssk)->snd_wnd, *sndbuf);
		if (!sk_stream_memory_free(subflow->tcp_sock))
			continue;

		pace = READ_ONCE(ssk->sk_pacing_rate);
		if (!pace)
			continue;

		ratio = div_u64((u64)READ_ONCE(ssk->sk_wmem_queued) << 32,
				pace);
		if (ratio < send_info[subflow->backup].ratio) {
			send_info[subflow->backup].ssk = ssk;
			send_info[subflow->backup].ratio = ratio;
		}
	}

	pr_debug("msk=%p nr_active=%d ssk=%p:%lld backup=%p:%lld",
		 msk, nr_active, send_info[0].ssk, send_info[0].ratio,
		 send_info[1].ssk, send_info[1].ratio);

	/* pick the best backup if no other subflow is active */
	if (!nr_active)
		send_info[0].ssk = send_info[1].ssk;

	if (send_info[0].ssk) {
		msk->last_snd = send_info[0].ssk;
		msk->snd_burst = min_t(int, MPTCP_SEND_BURST_SIZE,
				       sk_stream_wspace(msk->last_snd));
		return msk->last_snd;
	}
	return NULL;
}

static void ssk_check_wmem(struct mptcp_sock *msk)
{
	if (unlikely(!mptcp_is_writeable(msk)))
		mptcp_nospace(msk);
}

static int mptcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	int mss_now = 0, size_goal = 0, ret = 0;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct page_frag *pfrag;
	size_t copied = 0;
	struct sock *ssk;
	u32 sndbuf;
	bool tx_ok;
	long timeo;

	if (msg->msg_flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
		return -EOPNOTSUPP;

	lock_sock(sk);

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	if ((1 << sk->sk_state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		ret = sk_stream_wait_connect(sk, &timeo);
		if (ret)
			goto out;
	}

	pfrag = sk_page_frag(sk);
restart:
	mptcp_clean_una(sk);

	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN)) {
		ret = -EPIPE;
		goto out;
	}

	__mptcp_flush_join_list(msk);
	ssk = mptcp_subflow_get_send(msk, &sndbuf);
	while (!sk_stream_memory_free(sk) ||
	       !ssk ||
	       !mptcp_page_frag_refill(ssk, pfrag)) {
		if (ssk) {
			/* make sure retransmit timer is
			 * running before we wait for memory.
			 *
			 * The retransmit timer might be needed
			 * to make the peer send an up-to-date
			 * MPTCP Ack.
			 */
			mptcp_set_timeout(sk, ssk);
			if (!mptcp_timer_pending(sk))
				mptcp_reset_timer(sk);
		}

		mptcp_nospace(msk);
		ret = sk_stream_wait_memory(sk, &timeo);
		if (ret)
			goto out;

		mptcp_clean_una(sk);

		ssk = mptcp_subflow_get_send(msk, &sndbuf);
		if (list_empty(&msk->conn_list)) {
			ret = -ENOTCONN;
			goto out;
		}
	}

	/* do auto tuning */
	if (!(sk->sk_userlocks & SOCK_SNDBUF_LOCK) &&
	    sndbuf > READ_ONCE(sk->sk_sndbuf))
		WRITE_ONCE(sk->sk_sndbuf, sndbuf);

	pr_debug("conn_list->subflow=%p", ssk);

	lock_sock(ssk);
	tx_ok = msg_data_left(msg);
	while (tx_ok) {
		ret = mptcp_sendmsg_frag(sk, ssk, msg, NULL, &timeo, &mss_now,
					 &size_goal);
		if (ret < 0) {
			if (ret == -EAGAIN && timeo > 0) {
				mptcp_set_timeout(sk, ssk);
				release_sock(ssk);
				goto restart;
			}
			break;
		}

		/* burst can be negative, we will try move to the next subflow
		 * at selection time, if possible.
		 */
		msk->snd_burst -= ret;
		copied += ret;

		tx_ok = msg_data_left(msg);
		if (!tx_ok)
			break;

		if (!sk_stream_memory_free(ssk) ||
		    !mptcp_page_frag_refill(ssk, pfrag) ||
		    !mptcp_ext_cache_refill(msk)) {
			tcp_push(ssk, msg->msg_flags, mss_now,
				 tcp_sk(ssk)->nonagle, size_goal);
			mptcp_set_timeout(sk, ssk);
			release_sock(ssk);
			goto restart;
		}

		/* memory is charged to mptcp level socket as well, i.e.
		 * if msg is very large, mptcp socket may run out of buffer
		 * space.  mptcp_clean_una() will release data that has
		 * been acked at mptcp level in the mean time, so there is
		 * a good chance we can continue sending data right away.
		 *
		 * Normally, when the tcp subflow can accept more data, then
		 * so can the MPTCP socket.  However, we need to cope with
		 * peers that might lag behind in their MPTCP-level
		 * acknowledgements, i.e.  data might have been acked at
		 * tcp level only.  So, we must also check the MPTCP socket
		 * limits before we send more data.
		 */
		if (unlikely(!sk_stream_memory_free(sk))) {
			tcp_push(ssk, msg->msg_flags, mss_now,
				 tcp_sk(ssk)->nonagle, size_goal);
			mptcp_clean_una(sk);
			if (!sk_stream_memory_free(sk)) {
				/* can't send more for now, need to wait for
				 * MPTCP-level ACKs from peer.
				 *
				 * Wakeup will happen via mptcp_clean_una().
				 */
				mptcp_set_timeout(sk, ssk);
				release_sock(ssk);
				goto restart;
			}
		}
	}

	mptcp_set_timeout(sk, ssk);
	if (copied) {
		tcp_push(ssk, msg->msg_flags, mss_now, tcp_sk(ssk)->nonagle,
			 size_goal);

		/* start the timer, if it's not pending */
		if (!mptcp_timer_pending(sk))
			mptcp_reset_timer(sk);
	}

	release_sock(ssk);
out:
	ssk_check_wmem(msk);
	release_sock(sk);
	return copied ? : ret;
}

static void mptcp_wait_data(struct sock *sk, long *timeo)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct mptcp_sock *msk = mptcp_sk(sk);

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);

	sk_wait_event(sk, timeo,
		      test_and_clear_bit(MPTCP_DATA_READY, &msk->flags), &wait);

	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);
}

static int __mptcp_recvmsg_mskq(struct mptcp_sock *msk,
				struct msghdr *msg,
				size_t len)
{
	struct sock *sk = (struct sock *)msk;
	struct sk_buff *skb;
	int copied = 0;

	while ((skb = skb_peek(&sk->sk_receive_queue)) != NULL) {
		u32 offset = MPTCP_SKB_CB(skb)->offset;
		u32 data_len = skb->len - offset;
		u32 count = min_t(size_t, len - copied, data_len);
		int err;

		err = skb_copy_datagram_msg(skb, offset, msg, count);
		if (unlikely(err < 0)) {
			if (!copied)
				return err;
			break;
		}

		copied += count;

		if (count < data_len) {
			MPTCP_SKB_CB(skb)->offset += count;
			break;
		}

		__skb_unlink(skb, &sk->sk_receive_queue);
		__kfree_skb(skb);

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
	u32 time, advmss = 1;
	u64 rtt_us, mstamp;

	sock_owned_by_me(sk);

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
	}

	msk->rcvq_space.rtt_us = rtt_us;
	if (time < (rtt_us >> 3) || rtt_us == 0)
		return;

	if (msk->rcvq_space.copied <= msk->rcvq_space.space)
		goto new_measure;

	if (sock_net(sk)->ipv4.sysctl_tcp_moderate_rcvbuf &&
	    !(sk->sk_userlocks & SOCK_RCVBUF_LOCK)) {
		int rcvmem, rcvbuf;
		u64 rcvwin, grow;

		rcvwin = ((u64)msk->rcvq_space.copied << 1) + 16 * advmss;

		grow = rcvwin * (msk->rcvq_space.copied - msk->rcvq_space.space);

		do_div(grow, msk->rcvq_space.space);
		rcvwin += (grow << 1);

		rcvmem = SKB_TRUESIZE(advmss + MAX_TCP_HEADER);
		while (tcp_win_from_space(sk, rcvmem) < advmss)
			rcvmem += 128;

		do_div(rcvwin, advmss);
		rcvbuf = min_t(u64, rcvwin * rcvmem,
			       sock_net(sk)->ipv4.sysctl_tcp_rmem[2]);

		if (rcvbuf > sk->sk_rcvbuf) {
			u32 window_clamp;

			window_clamp = tcp_win_from_space(sk, rcvbuf);
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

static bool __mptcp_move_skbs(struct mptcp_sock *msk)
{
	unsigned int moved = 0;
	bool done;

	/* avoid looping forever below on racing close */
	if (((struct sock *)msk)->sk_state == TCP_CLOSE)
		return false;

	__mptcp_flush_join_list(msk);
	do {
		struct sock *ssk = mptcp_subflow_recv_lookup(msk);

		if (!ssk)
			break;

		lock_sock(ssk);
		done = __mptcp_move_skbs_from_subflow(msk, ssk, &moved);
		release_sock(ssk);
	} while (!done);

	if (mptcp_ofo_queue(msk) || moved > 0) {
		mptcp_check_data_fin((struct sock *)msk);
		return true;
	}
	return false;
}

static int mptcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			 int nonblock, int flags, int *addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	int copied = 0;
	int target;
	long timeo;

	if (msg->msg_flags & ~(MSG_WAITALL | MSG_DONTWAIT))
		return -EOPNOTSUPP;

	lock_sock(sk);
	timeo = sock_rcvtimeo(sk, nonblock);

	len = min_t(size_t, len, INT_MAX);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);
	__mptcp_flush_join_list(msk);

	while (len > (size_t)copied) {
		int bytes_read;

		bytes_read = __mptcp_recvmsg_mskq(msk, msg, len - copied);
		if (unlikely(bytes_read < 0)) {
			if (!copied)
				copied = bytes_read;
			goto out_err;
		}

		copied += bytes_read;

		if (skb_queue_empty(&sk->sk_receive_queue) &&
		    __mptcp_move_skbs(msk))
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

			if (test_and_clear_bit(MPTCP_WORK_EOF, &msk->flags))
				mptcp_check_for_eof(msk);

			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

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
		mptcp_wait_data(sk, &timeo);
	}

	if (skb_queue_empty(&sk->sk_receive_queue)) {
		/* entire backlog drained, clear DATA_READY. */
		clear_bit(MPTCP_DATA_READY, &msk->flags);

		/* .. race-breaker: ssk might have gotten new data
		 * after last __mptcp_move_skbs() returned false.
		 */
		if (unlikely(__mptcp_move_skbs(msk)))
			set_bit(MPTCP_DATA_READY, &msk->flags);
	} else if (unlikely(!test_bit(MPTCP_DATA_READY, &msk->flags))) {
		/* data to read but mptcp_wait_data() cleared DATA_READY */
		set_bit(MPTCP_DATA_READY, &msk->flags);
	}
out_err:
	pr_debug("msk=%p data_ready=%d rx queue empty=%d copied=%d",
		 msk, test_bit(MPTCP_DATA_READY, &msk->flags),
		 skb_queue_empty(&sk->sk_receive_queue), copied);
	mptcp_rcv_space_adjust(msk, copied);

	release_sock(sk);
	return copied;
}

static void mptcp_retransmit_handler(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (atomic64_read(&msk->snd_una) == READ_ONCE(msk->write_seq)) {
		mptcp_stop_timer(sk);
	} else {
		set_bit(MPTCP_WORK_RTX, &msk->flags);
		if (schedule_work(&msk->work))
			sock_hold(sk);
	}
}

static void mptcp_retransmit_timer(struct timer_list *t)
{
	struct inet_connection_sock *icsk = from_timer(icsk, t,
						       icsk_retransmit_timer);
	struct sock *sk = &icsk->icsk_inet.sk;

	bh_lock_sock(sk);
	if (!sock_owned_by_user(sk)) {
		mptcp_retransmit_handler(sk);
	} else {
		/* delegate our work to tcp_release_cb() */
		if (!test_and_set_bit(TCP_WRITE_TIMER_DEFERRED,
				      &sk->sk_tsq_flags))
			sock_hold(sk);
	}
	bh_unlock_sock(sk);
	sock_put(sk);
}

/* Find an idle subflow.  Return NULL if there is unacked data at tcp
 * level.
 *
 * A backup subflow is returned only if that is the only kind available.
 */
static struct sock *mptcp_subflow_get_retrans(const struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *backup = NULL;

	sock_owned_by_me((const struct sock *)msk);

	if (__mptcp_check_fallback(msk))
		return msk->first;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (!mptcp_subflow_active(subflow))
			continue;

		/* still data outstanding at TCP level?  Don't retransmit. */
		if (!tcp_write_queue_empty(ssk))
			return NULL;

		if (subflow->backup) {
			if (!backup)
				backup = ssk;
			continue;
		}

		return ssk;
	}

	return backup;
}

/* subflow sockets can be either outgoing (connect) or incoming
 * (accept).
 *
 * Outgoing subflows use in-kernel sockets.
 * Incoming subflows do not have their own 'struct socket' allocated,
 * so we need to use tcp_close() after detaching them from the mptcp
 * parent socket.
 */
void __mptcp_close_ssk(struct sock *sk, struct sock *ssk,
		       struct mptcp_subflow_context *subflow,
		       long timeout)
{
	struct socket *sock = READ_ONCE(ssk->sk_socket);

	list_del(&subflow->node);

	if (sock && sock != sk->sk_socket) {
		/* outgoing subflow */
		sock_release(sock);
	} else {
		/* incoming subflow */
		tcp_close(ssk, timeout);
	}
}

static unsigned int mptcp_sync_mss(struct sock *sk, u32 pmtu)
{
	return 0;
}

static void pm_work(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	spin_lock_bh(&msk->pm.lock);

	pr_debug("msk=%p status=%x", msk, pm->status);
	if (pm->status & BIT(MPTCP_PM_ADD_ADDR_RECEIVED)) {
		pm->status &= ~BIT(MPTCP_PM_ADD_ADDR_RECEIVED);
		mptcp_pm_nl_add_addr_received(msk);
	}
	if (pm->status & BIT(MPTCP_PM_RM_ADDR_RECEIVED)) {
		pm->status &= ~BIT(MPTCP_PM_RM_ADDR_RECEIVED);
		mptcp_pm_nl_rm_addr_received(msk);
	}
	if (pm->status & BIT(MPTCP_PM_ESTABLISHED)) {
		pm->status &= ~BIT(MPTCP_PM_ESTABLISHED);
		mptcp_pm_nl_fully_established(msk);
	}
	if (pm->status & BIT(MPTCP_PM_SUBFLOW_ESTABLISHED)) {
		pm->status &= ~BIT(MPTCP_PM_SUBFLOW_ESTABLISHED);
		mptcp_pm_nl_subflow_established(msk);
	}

	spin_unlock_bh(&msk->pm.lock);
}

static void mptcp_worker(struct work_struct *work)
{
	struct mptcp_sock *msk = container_of(work, struct mptcp_sock, work);
	struct sock *ssk, *sk = &msk->sk.icsk_inet.sk;
	int orig_len, orig_offset, mss_now = 0, size_goal = 0;
	struct mptcp_data_frag *dfrag;
	u64 orig_write_seq;
	size_t copied = 0;
	struct msghdr msg = {
		.msg_flags = MSG_DONTWAIT,
	};
	long timeo = 0;

	lock_sock(sk);
	mptcp_clean_una(sk);
	mptcp_check_data_fin_ack(sk);
	__mptcp_flush_join_list(msk);
	__mptcp_move_skbs(msk);

	if (msk->pm.status)
		pm_work(msk);

	if (test_and_clear_bit(MPTCP_WORK_EOF, &msk->flags))
		mptcp_check_for_eof(msk);

	mptcp_check_data_fin(sk);

	if (!test_and_clear_bit(MPTCP_WORK_RTX, &msk->flags))
		goto unlock;

	dfrag = mptcp_rtx_head(sk);
	if (!dfrag)
		goto unlock;

	if (!mptcp_ext_cache_refill(msk))
		goto reset_unlock;

	ssk = mptcp_subflow_get_retrans(msk);
	if (!ssk)
		goto reset_unlock;

	lock_sock(ssk);

	orig_len = dfrag->data_len;
	orig_offset = dfrag->offset;
	orig_write_seq = dfrag->data_seq;
	while (dfrag->data_len > 0) {
		int ret = mptcp_sendmsg_frag(sk, ssk, &msg, dfrag, &timeo,
					     &mss_now, &size_goal);
		if (ret < 0)
			break;

		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_RETRANSSEGS);
		copied += ret;
		dfrag->data_len -= ret;
		dfrag->offset += ret;

		if (!mptcp_ext_cache_refill(msk))
			break;
	}
	if (copied)
		tcp_push(ssk, msg.msg_flags, mss_now, tcp_sk(ssk)->nonagle,
			 size_goal);

	dfrag->data_seq = orig_write_seq;
	dfrag->offset = orig_offset;
	dfrag->data_len = orig_len;

	mptcp_set_timeout(sk, ssk);
	release_sock(ssk);

reset_unlock:
	if (!mptcp_timer_pending(sk))
		mptcp_reset_timer(sk);

unlock:
	release_sock(sk);
	sock_put(sk);
}

static int __mptcp_init_sock(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	spin_lock_init(&msk->join_list_lock);

	INIT_LIST_HEAD(&msk->conn_list);
	INIT_LIST_HEAD(&msk->join_list);
	INIT_LIST_HEAD(&msk->rtx_queue);
	__set_bit(MPTCP_SEND_SPACE, &msk->flags);
	INIT_WORK(&msk->work, mptcp_worker);
	msk->out_of_order_queue = RB_ROOT;

	msk->first = NULL;
	inet_csk(sk)->icsk_sync_mss = mptcp_sync_mss;

	mptcp_pm_data_init(msk);

	/* re-use the csk retrans timer for MPTCP-level retrans */
	timer_setup(&msk->sk.icsk_retransmit_timer, mptcp_retransmit_timer, 0);

	return 0;
}

static int mptcp_init_sock(struct sock *sk)
{
	struct net *net = sock_net(sk);
	int ret;

	ret = __mptcp_init_sock(sk);
	if (ret)
		return ret;

	if (!mptcp_is_enabled(net))
		return -ENOPROTOOPT;

	if (unlikely(!net->mib.mptcp_statistics) && !mptcp_mib_alloc(net))
		return -ENOMEM;

	ret = __mptcp_socket_create(mptcp_sk(sk));
	if (ret)
		return ret;

	sk_sockets_allocated_inc(sk);
	sk->sk_rcvbuf = sock_net(sk)->ipv4.sysctl_tcp_rmem[1];
	sk->sk_sndbuf = sock_net(sk)->ipv4.sysctl_tcp_wmem[1];

	return 0;
}

static void __mptcp_clear_xmit(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_data_frag *dtmp, *dfrag;

	sk_stop_timer(sk, &msk->sk.icsk_retransmit_timer);

	list_for_each_entry_safe(dfrag, dtmp, &msk->rtx_queue, list)
		dfrag_clear(sk, dfrag);
}

static void mptcp_cancel_work(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (cancel_work_sync(&msk->work))
		sock_put(sk);
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
		tcp_disconnect(ssk, O_NONBLOCK);
		break;
	default:
		if (__mptcp_check_fallback(mptcp_sk(sk))) {
			pr_debug("Fallback");
			ssk->sk_shutdown |= how;
			tcp_shutdown(ssk, how);
		} else {
			pr_debug("Sending DATA_FIN on subflow %p", ssk);
			mptcp_set_timeout(sk, ssk);
			tcp_send_ack(ssk);
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

static void mptcp_close(struct sock *sk, long timeout)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct mptcp_sock *msk = mptcp_sk(sk);
	LIST_HEAD(conn_list);

	lock_sock(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;

	if (sk->sk_state == TCP_LISTEN) {
		inet_sk_state_store(sk, TCP_CLOSE);
		goto cleanup;
	} else if (sk->sk_state == TCP_CLOSE) {
		goto cleanup;
	}

	if (__mptcp_check_fallback(msk)) {
		goto update_state;
	} else if (mptcp_close_state(sk)) {
		pr_debug("Sending DATA_FIN sk=%p", sk);
		WRITE_ONCE(msk->write_seq, msk->write_seq + 1);
		WRITE_ONCE(msk->snd_data_fin_enable, 1);

		mptcp_for_each_subflow(msk, subflow) {
			struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);

			mptcp_subflow_shutdown(sk, tcp_sk, SHUTDOWN_MASK);
		}
	}

	sk_stream_wait_close(sk, timeout);

update_state:
	inet_sk_state_store(sk, TCP_CLOSE);

cleanup:
	/* be sure to always acquire the join list lock, to sync vs
	 * mptcp_finish_join().
	 */
	spin_lock_bh(&msk->join_list_lock);
	list_splice_tail_init(&msk->join_list, &msk->conn_list);
	spin_unlock_bh(&msk->join_list_lock);
	list_splice_init(&msk->conn_list, &conn_list);

	__mptcp_clear_xmit(sk);

	release_sock(sk);

	list_for_each_entry_safe(subflow, tmp, &conn_list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		__mptcp_close_ssk(sk, ssk, subflow, timeout);
	}

	mptcp_cancel_work(sk);

	__skb_queue_purge(&sk->sk_receive_queue);

	sk_common_release(sk);
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
	/* Should never be called.
	 * inet_stream_connect() calls ->disconnect, but that
	 * refers to the subflow socket, not the mptcp one.
	 */
	WARN_ON_ONCE(1);
	return 0;
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct ipv6_pinfo *mptcp_inet6_sk(const struct sock *sk)
{
	unsigned int offset = sizeof(struct mptcp6_sock) - sizeof(struct ipv6_pinfo);

	return (struct ipv6_pinfo *)(((u8 *)sk) + offset);
}
#endif

struct sock *mptcp_sk_clone(const struct sock *sk,
			    const struct mptcp_options_received *mp_opt,
			    struct request_sock *req)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);
	struct sock *nsk = sk_clone_lock(sk, GFP_ATOMIC);
	struct mptcp_sock *msk;
	u64 ack_seq;

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
	msk->subflow = NULL;
	WRITE_ONCE(msk->fully_established, false);

	msk->write_seq = subflow_req->idsn + 1;
	atomic64_set(&msk->snd_una, msk->write_seq);
	if (mp_opt->mp_capable) {
		msk->can_ack = true;
		msk->remote_key = mp_opt->sndr_key;
		mptcp_crypto_key_sha(msk->remote_key, NULL, &ack_seq);
		ack_seq++;
		msk->ack_seq = ack_seq;
	}

	sock_reset_flag(nsk, SOCK_RCU_FREE);
	/* will be fully established after successful MPC subflow creation */
	inet_sk_state_store(nsk, TCP_SYN_RECV);
	bh_unlock_sock(nsk);

	/* keep a single reference */
	__sock_put(nsk);
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
}

static struct sock *mptcp_accept(struct sock *sk, int flags, int *err,
				 bool kern)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *listener;
	struct sock *newsk;

	listener = __mptcp_nmpc_socket(msk);
	if (WARN_ON_ONCE(!listener)) {
		*err = -EINVAL;
		return NULL;
	}

	pr_debug("msk=%p, listener=%p", msk, mptcp_subflow_ctx(listener->sk));
	newsk = inet_csk_accept(listener->sk, flags, err, kern);
	if (!newsk)
		return NULL;

	pr_debug("msk=%p, subflow is mptcp=%d", msk, sk_is_mptcp(newsk));
	if (sk_is_mptcp(newsk)) {
		struct mptcp_subflow_context *subflow;
		struct sock *new_mptcp_sock;
		struct sock *ssk = newsk;

		subflow = mptcp_subflow_ctx(newsk);
		new_mptcp_sock = subflow->conn;

		/* is_mptcp should be false if subflow->conn is missing, see
		 * subflow_syn_recv_sock()
		 */
		if (WARN_ON_ONCE(!new_mptcp_sock)) {
			tcp_sk(newsk)->is_mptcp = 0;
			return newsk;
		}

		/* acquire the 2nd reference for the owning socket */
		sock_hold(new_mptcp_sock);

		local_bh_disable();
		bh_lock_sock(new_mptcp_sock);
		msk = mptcp_sk(new_mptcp_sock);
		msk->first = newsk;

		newsk = new_mptcp_sock;
		mptcp_copy_inaddrs(newsk, ssk);
		list_add(&subflow->node, &msk->conn_list);

		mptcp_rcv_space_init(msk, ssk);
		bh_unlock_sock(new_mptcp_sock);

		__MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPCAPABLEPASSIVEACK);
		local_bh_enable();
	} else {
		MPTCP_INC_STATS(sock_net(sk),
				MPTCP_MIB_MPCAPABLEPASSIVEFALLBACK);
	}

	return newsk;
}

void mptcp_destroy_common(struct mptcp_sock *msk)
{
	skb_rbtree_purge(&msk->out_of_order_queue);
	mptcp_token_destroy(msk);
	mptcp_pm_free_anno_list(msk);
}

static void mptcp_destroy(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (msk->cached_ext)
		__skb_ext_put(msk->cached_ext);

	mptcp_destroy_common(msk);
	sk_sockets_allocated_dec(sk);
}

static int mptcp_setsockopt_sol_socket(struct mptcp_sock *msk, int optname,
				       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	struct socket *ssock;
	int ret;

	switch (optname) {
	case SO_REUSEPORT:
	case SO_REUSEADDR:
		lock_sock(sk);
		ssock = __mptcp_nmpc_socket(msk);
		if (!ssock) {
			release_sock(sk);
			return -EINVAL;
		}

		ret = sock_setsockopt(ssock, SOL_SOCKET, optname, optval, optlen);
		if (ret == 0) {
			if (optname == SO_REUSEPORT)
				sk->sk_reuseport = ssock->sk->sk_reuseport;
			else if (optname == SO_REUSEADDR)
				sk->sk_reuse = ssock->sk->sk_reuse;
		}
		release_sock(sk);
		return ret;
	}

	return sock_setsockopt(sk->sk_socket, SOL_SOCKET, optname, optval, optlen);
}

static int mptcp_setsockopt_v6(struct mptcp_sock *msk, int optname,
			       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	int ret = -EOPNOTSUPP;
	struct socket *ssock;

	switch (optname) {
	case IPV6_V6ONLY:
		lock_sock(sk);
		ssock = __mptcp_nmpc_socket(msk);
		if (!ssock) {
			release_sock(sk);
			return -EINVAL;
		}

		ret = tcp_setsockopt(ssock->sk, SOL_IPV6, optname, optval, optlen);
		if (ret == 0)
			sk->sk_ipv6only = ssock->sk->sk_ipv6only;

		release_sock(sk);
		break;
	}

	return ret;
}

static int mptcp_setsockopt(struct sock *sk, int level, int optname,
			    sockptr_t optval, unsigned int optlen)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p", msk);

	if (level == SOL_SOCKET)
		return mptcp_setsockopt_sol_socket(msk, optname, optval, optlen);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when TCP socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssk = __mptcp_tcp_fallback(msk);
	release_sock(sk);
	if (ssk)
		return tcp_setsockopt(ssk, level, optname, optval, optlen);

	if (level == SOL_IPV6)
		return mptcp_setsockopt_v6(msk, optname, optval, optlen);

	return -EOPNOTSUPP;
}

static int mptcp_getsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int __user *option)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p", msk);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssk = __mptcp_tcp_fallback(msk);
	release_sock(sk);
	if (ssk)
		return tcp_getsockopt(ssk, level, optname, optval, option);

	return -EOPNOTSUPP;
}

#define MPTCP_DEFERRED_ALL (TCPF_DELACK_TIMER_DEFERRED | \
			    TCPF_WRITE_TIMER_DEFERRED)

/* this is very alike tcp_release_cb() but we must handle differently a
 * different set of events
 */
static void mptcp_release_cb(struct sock *sk)
{
	unsigned long flags, nflags;

	do {
		flags = sk->sk_tsq_flags;
		if (!(flags & MPTCP_DEFERRED_ALL))
			return;
		nflags = flags & ~MPTCP_DEFERRED_ALL;
	} while (cmpxchg(&sk->sk_tsq_flags, flags, nflags) != flags);

	sock_release_ownership(sk);

	if (flags & TCPF_DELACK_TIMER_DEFERRED) {
		struct mptcp_sock *msk = mptcp_sk(sk);
		struct sock *ssk;

		ssk = mptcp_subflow_recv_lookup(msk);
		if (!ssk || !schedule_work(&msk->work))
			__sock_put(sk);
	}

	if (flags & TCPF_WRITE_TIMER_DEFERRED) {
		mptcp_retransmit_handler(sk);
		__sock_put(sk);
	}
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
	struct socket *ssock;

	ssock = __mptcp_nmpc_socket(msk);
	pr_debug("msk=%p, subflow=%p", msk, ssock);
	if (WARN_ON_ONCE(!ssock))
		return -EINVAL;

	return inet_csk_get_port(ssock->sk, snum);
}

void mptcp_finish_connect(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk;
	struct sock *sk;
	u64 ack_seq;

	subflow = mptcp_subflow_ctx(ssk);
	sk = subflow->conn;
	msk = mptcp_sk(sk);

	pr_debug("msk=%p, token=%u", sk, subflow->token);

	mptcp_crypto_key_sha(subflow->remote_key, NULL, &ack_seq);
	ack_seq++;
	subflow->map_seq = ack_seq;
	subflow->map_subflow_seq = 1;

	/* the socket is not connected yet, no msk/subflow ops can access/race
	 * accessing the field below
	 */
	WRITE_ONCE(msk->remote_key, subflow->remote_key);
	WRITE_ONCE(msk->local_key, subflow->local_key);
	WRITE_ONCE(msk->write_seq, subflow->idsn + 1);
	WRITE_ONCE(msk->ack_seq, ack_seq);
	WRITE_ONCE(msk->can_ack, 1);
	atomic64_set(&msk->snd_una, msk->write_seq);

	mptcp_pm_new_connection(msk, 0);

	mptcp_rcv_space_init(msk, ssk);
}

static void mptcp_sock_graft(struct sock *sk, struct socket *parent)
{
	write_lock_bh(&sk->sk_callback_lock);
	rcu_assign_pointer(sk->sk_wq, &parent->wq);
	sk_set_socket(sk, parent);
	sk->sk_uid = SOCK_INODE(parent)->i_uid;
	write_unlock_bh(&sk->sk_callback_lock);
}

bool mptcp_finish_join(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct sock *parent = (void *)msk;
	struct socket *parent_sock;
	bool ret;

	pr_debug("msk=%p, subflow=%p", msk, subflow);

	/* mptcp socket already closing? */
	if (!mptcp_is_fully_established(parent))
		return false;

	if (!msk->pm.server_side)
		return true;

	if (!mptcp_pm_allow_new_subflow(msk))
		return false;

	/* active connections are already on conn_list, and we can't acquire
	 * msk lock here.
	 * use the join list lock as synchronization point and double-check
	 * msk status to avoid racing with mptcp_close()
	 */
	spin_lock_bh(&msk->join_list_lock);
	ret = inet_sk_state_load(parent) == TCP_ESTABLISHED;
	if (ret && !WARN_ON_ONCE(!list_empty(&subflow->node)))
		list_add_tail(&subflow->node, &msk->join_list);
	spin_unlock_bh(&msk->join_list_lock);
	if (!ret)
		return false;

	/* attach to msk socket only after we are sure he will deal with us
	 * at close time
	 */
	parent_sock = READ_ONCE(parent->sk_socket);
	if (parent_sock && !sk->sk_socket)
		mptcp_sock_graft(sk, parent_sock);
	subflow->map_seq = msk->ack_seq;
	return true;
}

static bool mptcp_memory_free(const struct sock *sk, int wake)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	return wake ? test_bit(MPTCP_SEND_SPACE, &msk->flags) : true;
}

static struct proto mptcp_prot = {
	.name		= "MPTCP",
	.owner		= THIS_MODULE,
	.init		= mptcp_init_sock,
	.disconnect	= mptcp_disconnect,
	.close		= mptcp_close,
	.accept		= mptcp_accept,
	.setsockopt	= mptcp_setsockopt,
	.getsockopt	= mptcp_getsockopt,
	.shutdown	= tcp_shutdown,
	.destroy	= mptcp_destroy,
	.sendmsg	= mptcp_sendmsg,
	.recvmsg	= mptcp_recvmsg,
	.release_cb	= mptcp_release_cb,
	.hash		= mptcp_hash,
	.unhash		= mptcp_unhash,
	.get_port	= mptcp_get_port,
	.sockets_allocated	= &mptcp_sockets_allocated,
	.memory_allocated	= &tcp_memory_allocated,
	.memory_pressure	= &tcp_memory_pressure,
	.stream_memory_free	= mptcp_memory_free,
	.sysctl_wmem_offset	= offsetof(struct net, ipv4.sysctl_tcp_wmem),
	.sysctl_mem	= sysctl_tcp_mem,
	.obj_size	= sizeof(struct mptcp_sock),
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
	.no_autobind	= true,
};

static int mptcp_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	lock_sock(sock->sk);
	ssock = __mptcp_nmpc_socket(msk);
	if (!ssock) {
		err = -EINVAL;
		goto unlock;
	}

	err = ssock->ops->bind(ssock, uaddr, addr_len);
	if (!err)
		mptcp_copy_inaddrs(sock->sk, ssock->sk);

unlock:
	release_sock(sock->sk);
	return err;
}

static void mptcp_subflow_early_fallback(struct mptcp_sock *msk,
					 struct mptcp_subflow_context *subflow)
{
	subflow->request_mptcp = 0;
	__mptcp_do_fallback(msk);
}

static int mptcp_stream_connect(struct socket *sock, struct sockaddr *uaddr,
				int addr_len, int flags)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct mptcp_subflow_context *subflow;
	struct socket *ssock;
	int err;

	lock_sock(sock->sk);
	if (sock->state != SS_UNCONNECTED && msk->subflow) {
		/* pending connection or invalid state, let existing subflow
		 * cope with that
		 */
		ssock = msk->subflow;
		goto do_connect;
	}

	ssock = __mptcp_nmpc_socket(msk);
	if (!ssock) {
		err = -EINVAL;
		goto unlock;
	}

	mptcp_token_destroy(msk);
	inet_sk_state_store(sock->sk, TCP_SYN_SENT);
	subflow = mptcp_subflow_ctx(ssock->sk);
#ifdef CONFIG_TCP_MD5SIG
	/* no MPTCP if MD5SIG is enabled on this socket or we may run out of
	 * TCP option space.
	 */
	if (rcu_access_pointer(tcp_sk(ssock->sk)->md5sig_info))
		mptcp_subflow_early_fallback(msk, subflow);
#endif
	if (subflow->request_mptcp && mptcp_token_new_connect(ssock->sk))
		mptcp_subflow_early_fallback(msk, subflow);

do_connect:
	err = ssock->ops->connect(ssock, uaddr, addr_len, flags);
	sock->state = ssock->state;

	/* on successful connect, the msk state will be moved to established by
	 * subflow_finish_connect()
	 */
	if (!err || err == -EINPROGRESS)
		mptcp_copy_inaddrs(sock->sk, ssock->sk);
	else
		inet_sk_state_store(sock->sk, inet_sk_state_load(ssock->sk));

unlock:
	release_sock(sock->sk);
	return err;
}

static int mptcp_listen(struct socket *sock, int backlog)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	pr_debug("msk=%p", msk);

	lock_sock(sock->sk);
	ssock = __mptcp_nmpc_socket(msk);
	if (!ssock) {
		err = -EINVAL;
		goto unlock;
	}

	mptcp_token_destroy(msk);
	inet_sk_state_store(sock->sk, TCP_LISTEN);
	sock_set_flag(sock->sk, SOCK_RCU_FREE);

	err = ssock->ops->listen(ssock, backlog);
	inet_sk_state_store(sock->sk, inet_sk_state_load(ssock->sk));
	if (!err)
		mptcp_copy_inaddrs(sock->sk, ssock->sk);

unlock:
	release_sock(sock->sk);
	return err;
}

static int mptcp_stream_accept(struct socket *sock, struct socket *newsock,
			       int flags, bool kern)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	pr_debug("msk=%p", msk);

	lock_sock(sock->sk);
	if (sock->sk->sk_state != TCP_LISTEN)
		goto unlock_fail;

	ssock = __mptcp_nmpc_socket(msk);
	if (!ssock)
		goto unlock_fail;

	clear_bit(MPTCP_DATA_READY, &msk->flags);
	sock_hold(ssock->sk);
	release_sock(sock->sk);

	err = ssock->ops->accept(sock, newsock, flags, kern);
	if (err == 0 && !mptcp_is_tcpsk(newsock->sk)) {
		struct mptcp_sock *msk = mptcp_sk(newsock->sk);
		struct mptcp_subflow_context *subflow;

		/* set ssk->sk_socket of accept()ed flows to mptcp socket.
		 * This is needed so NOSPACE flag can be set from tcp stack.
		 */
		__mptcp_flush_join_list(msk);
		mptcp_for_each_subflow(msk, subflow) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

			if (!ssk->sk_socket)
				mptcp_sock_graft(ssk, newsock);
		}
	}

	if (inet_csk_listen_poll(ssock->sk))
		set_bit(MPTCP_DATA_READY, &msk->flags);
	sock_put(ssock->sk);
	return err;

unlock_fail:
	release_sock(sock->sk);
	return -EINVAL;
}

static __poll_t mptcp_check_readable(struct mptcp_sock *msk)
{
	return test_bit(MPTCP_DATA_READY, &msk->flags) ? EPOLLIN | EPOLLRDNORM :
	       0;
}

static __poll_t mptcp_poll(struct file *file, struct socket *sock,
			   struct poll_table_struct *wait)
{
	struct sock *sk = sock->sk;
	struct mptcp_sock *msk;
	__poll_t mask = 0;
	int state;

	msk = mptcp_sk(sk);
	sock_poll_wait(file, sock, wait);

	state = inet_sk_state_load(sk);
	pr_debug("msk=%p state=%d flags=%lx", msk, state, msk->flags);
	if (state == TCP_LISTEN)
		return mptcp_check_readable(msk);

	if (state != TCP_SYN_SENT && state != TCP_SYN_RECV) {
		mask |= mptcp_check_readable(msk);
		if (test_bit(MPTCP_SEND_SPACE, &msk->flags))
			mask |= EPOLLOUT | EPOLLWRNORM;
	}
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;

	return mask;
}

static int mptcp_shutdown(struct socket *sock, int how)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct mptcp_subflow_context *subflow;
	int ret = 0;

	pr_debug("sk=%p, how=%d", msk, how);

	lock_sock(sock->sk);

	how++;
	if ((how & ~SHUTDOWN_MASK) || !how) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (sock->state == SS_CONNECTING) {
		if ((1 << sock->sk->sk_state) &
		    (TCPF_SYN_SENT | TCPF_SYN_RECV | TCPF_CLOSE))
			sock->state = SS_DISCONNECTING;
		else
			sock->state = SS_CONNECTED;
	}

	/* If we've already sent a FIN, or it's a closed state, skip this. */
	if (__mptcp_check_fallback(msk)) {
		if (how == SHUT_WR || how == SHUT_RDWR)
			inet_sk_state_store(sock->sk, TCP_FIN_WAIT1);

		mptcp_for_each_subflow(msk, subflow) {
			struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);

			mptcp_subflow_shutdown(sock->sk, tcp_sk, how);
		}
	} else if ((how & SEND_SHUTDOWN) &&
		   ((1 << sock->sk->sk_state) &
		    (TCPF_ESTABLISHED | TCPF_SYN_SENT |
		     TCPF_SYN_RECV | TCPF_CLOSE_WAIT)) &&
		   mptcp_close_state(sock->sk)) {
		__mptcp_flush_join_list(msk);

		WRITE_ONCE(msk->write_seq, msk->write_seq + 1);
		WRITE_ONCE(msk->snd_data_fin_enable, 1);

		mptcp_for_each_subflow(msk, subflow) {
			struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);

			mptcp_subflow_shutdown(sock->sk, tcp_sk, how);
		}
	}

	/* Wake up anyone sleeping in poll. */
	sock->sk->sk_state_change(sock->sk);

out_unlock:
	release_sock(sock->sk);

	return ret;
}

static const struct proto_ops mptcp_stream_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = mptcp_bind,
	.connect	   = mptcp_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = mptcp_stream_accept,
	.getname	   = inet_getname,
	.poll		   = mptcp_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = mptcp_listen,
	.shutdown	   = mptcp_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = inet_sendpage,
};

static struct inet_protosw mptcp_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_MPTCP,
	.prot		= &mptcp_prot,
	.ops		= &mptcp_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

void __init mptcp_proto_init(void)
{
	mptcp_prot.h.hashinfo = tcp_prot.h.hashinfo;

	if (percpu_counter_init(&mptcp_sockets_allocated, 0, GFP_KERNEL))
		panic("Failed to allocate MPTCP pcpu counter\n");

	mptcp_subflow_init();
	mptcp_pm_init();
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
	.connect	   = mptcp_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = mptcp_stream_accept,
	.getname	   = inet6_getname,
	.poll		   = mptcp_poll,
	.ioctl		   = inet6_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = mptcp_listen,
	.shutdown	   = mptcp_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet6_sendmsg,
	.recvmsg	   = inet6_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = inet_sendpage,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet6_compat_ioctl,
#endif
};

static struct proto mptcp_v6_prot;

static void mptcp_v6_destroy(struct sock *sk)
{
	mptcp_destroy(sk);
	inet6_destroy_sock(sk);
}

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
	mptcp_v6_prot.destroy = mptcp_v6_destroy;
	mptcp_v6_prot.obj_size = sizeof(struct mptcp6_sock);

	err = proto_register(&mptcp_v6_prot, 1);
	if (err)
		return err;

	err = inet6_register_protosw(&mptcp_v6_protosw);
	if (err)
		proto_unregister(&mptcp_v6_prot);

	return err;
}
#endif
