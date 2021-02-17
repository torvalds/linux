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

static void __mptcp_destroy_sock(struct sock *sk);
static void __mptcp_check_send_data_fin(struct sock *sk);

DEFINE_PER_CPU(struct mptcp_delegated_action, mptcp_delegated_actions);
static struct net_device mptcp_napi_dev;

/* If msk has an initial subflow socket, and the MP_CAPABLE handshake has not
 * completed yet or has failed, return the subflow socket.
 * Otherwise return NULL.
 */
struct socket *__mptcp_nmpc_socket(const struct mptcp_sock *msk)
{
	if (!msk->subflow || READ_ONCE(msk->can_ack))
		return NULL;

	return msk->subflow;
}

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
	sock_hold(ssock->sk);
	subflow->request_mptcp = 1;
	mptcp_sock_graft(msk->first, sk->sk_socket);

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

	seq = MPTCP_SKB_CB(skb)->map_seq;
	end_seq = MPTCP_SKB_CB(skb)->end_seq;
	max_seq = READ_ONCE(msk->rcv_wnd_sent);

	pr_debug("msk=%p seq=%llx limit=%llx empty=%d", msk, seq, max_seq,
		 RB_EMPTY_ROOT(&msk->out_of_order_queue));
	if (after64(end_seq, max_seq)) {
		/* out of window */
		mptcp_drop(sk, skb);
		pr_debug("oow by %lld, rcv_wnd_sent %llu\n",
			 (unsigned long long)end_seq - (unsigned long)max_seq,
			 (unsigned long long)msk->rcv_wnd_sent);
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

	/* try to fetch required memory from subflow */
	if (!sk_rmem_schedule(sk, skb, skb->truesize)) {
		if (ssk->sk_forward_alloc < skb->truesize)
			goto drop;
		__sk_mem_reclaim(ssk, skb->truesize);
		if (!sk_rmem_schedule(sk, skb, skb->truesize))
			goto drop;
	}

	/* the skb map_seq accounts for the skb offset:
	 * mptcp_subflow_get_mapped_dsn() is based on the current tp->copied_seq
	 * value
	 */
	MPTCP_SKB_CB(skb)->map_seq = mptcp_subflow_get_mapped_dsn(subflow);
	MPTCP_SKB_CB(skb)->end_seq = MPTCP_SKB_CB(skb)->map_seq + copy_len;
	MPTCP_SKB_CB(skb)->offset = offset;

	if (MPTCP_SKB_CB(skb)->map_seq == msk->ack_seq) {
		/* in sequence */
		WRITE_ONCE(msk->ack_seq, msk->ack_seq + copy_len);
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
drop:
	mptcp_drop(sk, skb);
	return false;
}

static void mptcp_stop_timer(struct sock *sk)
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

	return !__mptcp_check_fallback(msk) &&
	       ((1 << sk->sk_state) &
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

static void mptcp_set_timeout(const struct sock *sk, const struct sock *ssk)
{
	long tout = ssk && inet_csk(ssk)->icsk_pending ?
				      inet_csk(ssk)->icsk_timeout - jiffies : 0;

	if (tout <= 0)
		tout = mptcp_sk(sk)->timer_ival;
	mptcp_sk(sk)->timer_ival = tout > 0 ? tout : TCP_RTO_MIN;
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

static bool tcp_can_send_ack(const struct sock *ssk)
{
	return !((1 << inet_sk_state_load(ssk)) &
	       (TCPF_SYN_SENT | TCPF_SYN_RECV | TCPF_TIME_WAIT | TCPF_CLOSE | TCPF_LISTEN));
}

static void mptcp_send_ack(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		lock_sock(ssk);
		if (tcp_can_send_ack(ssk))
			tcp_send_ack(ssk);
		release_sock(ssk);
	}
}

static bool mptcp_subflow_cleanup_rbuf(struct sock *ssk)
{
	int ret;

	lock_sock(ssk);
	ret = tcp_can_send_ack(ssk);
	if (ret)
		tcp_cleanup_rbuf(ssk, 1);
	release_sock(ssk);
	return ret;
}

static void mptcp_cleanup_rbuf(struct mptcp_sock *msk)
{
	struct sock *ack_hint = READ_ONCE(msk->ack_hint);
	int old_space = READ_ONCE(msk->old_wspace);
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	bool cleanup;

	/* this is a simple superset of what tcp_cleanup_rbuf() implements
	 * so that we don't have to acquire the ssk socket lock most of the time
	 * to do actually nothing
	 */
	cleanup = __mptcp_space(sk) - old_space >= max(0, old_space);
	if (!cleanup)
		return;

	/* if the hinted ssk is still active, try to use it */
	if (likely(ack_hint)) {
		mptcp_for_each_subflow(msk, subflow) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

			if (ack_hint == ssk && mptcp_subflow_cleanup_rbuf(ssk))
				return;
		}
	}

	/* otherwise pick the first active subflow */
	mptcp_for_each_subflow(msk, subflow)
		if (mptcp_subflow_cleanup_rbuf(mptcp_subflow_tcp_sock(subflow)))
			return;
}

static bool mptcp_check_data_fin(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	u64 rcv_data_fin_seq;
	bool ret = false;

	if (__mptcp_check_fallback(msk) || !msk->first)
		return ret;

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
			break;
		default:
			/* Other states not expected */
			WARN_ON_ONCE(1);
			break;
		}

		ret = true;
		mptcp_set_timeout(sk, NULL);
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
			/* if no data is found, a racing workqueue/recvmsg
			 * already processed the new data, stop here or we
			 * can enter an infinite loop
			 */
			if (!moved)
				done = true;
			break;
		}

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

		if (atomic_read(&sk->sk_rmem_alloc) > sk_rbuf) {
			done = true;
			break;
		}
	} while (more_data_avail);
	WRITE_ONCE(msk->ack_hint, ssk);

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
static void move_skbs_to_msk(struct mptcp_sock *msk, struct sock *ssk)
{
	struct sock *sk = (struct sock *)msk;
	unsigned int moved = 0;

	if (inet_sk_state_load(sk) == TCP_CLOSE)
		return;

	mptcp_data_lock(sk);

	__mptcp_move_skbs_from_subflow(msk, ssk, &moved);
	__mptcp_ofo_queue(msk);

	/* If the moves have caught up with the DATA_FIN sequence number
	 * it's time to ack the DATA_FIN and change socket state, but
	 * this is not a good place to change state. Let the workqueue
	 * do it.
	 */
	if (mptcp_pending_data_fin(sk, NULL))
		mptcp_schedule_work(sk);
	mptcp_data_unlock(sk);
}

void mptcp_data_ready(struct sock *sk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_sock *msk = mptcp_sk(sk);
	int sk_rbuf, ssk_rbuf;
	bool wake;

	/* The peer can send data while we are shutting down this
	 * subflow at msk destruction time, but we must avoid enqueuing
	 * more data to the msk receive queue
	 */
	if (unlikely(subflow->disposable))
		return;

	/* move_skbs_to_msk below can legitly clear the data_avail flag,
	 * but we will need later to properly woke the reader, cache its
	 * value
	 */
	wake = subflow->data_avail == MPTCP_SUBFLOW_DATA_AVAIL;
	if (wake)
		set_bit(MPTCP_DATA_READY, &msk->flags);

	ssk_rbuf = READ_ONCE(ssk->sk_rcvbuf);
	sk_rbuf = READ_ONCE(sk->sk_rcvbuf);
	if (unlikely(ssk_rbuf > sk_rbuf))
		sk_rbuf = ssk_rbuf;

	/* over limit? can't append more skbs to msk */
	if (atomic_read(&sk->sk_rmem_alloc) > sk_rbuf)
		goto wake;

	move_skbs_to_msk(msk, ssk);

wake:
	if (wake)
		sk->sk_data_ready(sk);
}

void __mptcp_flush_join_list(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	if (likely(list_empty(&msk->join_list)))
		return;

	spin_lock_bh(&msk->join_list_lock);
	list_for_each_entry(subflow, &msk->join_list, node)
		mptcp_propagate_sndbuf((struct sock *)msk, mptcp_subflow_tcp_sock(subflow));
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

	/* prevent rescheduling on close */
	if (unlikely(inet_sk_state_load(sk) == TCP_CLOSE))
		return;

	/* should never be called with mptcp level timer cleared */
	tout = READ_ONCE(mptcp_sk(sk)->timer_ival);
	if (WARN_ON_ONCE(!tout))
		tout = TCP_RTO_MIN;
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

void mptcp_subflow_eof(struct sock *sk)
{
	if (!test_and_set_bit(MPTCP_WORK_EOF, &mptcp_sk(sk)->flags))
		mptcp_schedule_work(sk);
}

static void mptcp_check_for_eof(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	int receivers = 0;

	mptcp_for_each_subflow(msk, subflow)
		receivers += !subflow->rx_eof;
	if (receivers)
		return;

	if (!(sk->sk_shutdown & RCV_SHUTDOWN)) {
		/* hopefully temporary hack: propagate shutdown status
		 * to msk, when all subflows agree on it
		 */
		sk->sk_shutdown |= RCV_SHUTDOWN;

		smp_mb__before_atomic(); /* SHUTDOWN must be visible first */
		set_bit(MPTCP_DATA_READY, &msk->flags);
		sk->sk_data_ready(sk);
	}

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
		return;
	}
	mptcp_close_wake_up(sk);
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

	/* can collapse only if MPTCP level sequence is in order and this
	 * mapping has not been xmitted yet
	 */
	return mpext && mpext->data_seq + mpext->data_len == write_seq &&
	       !mpext->frozen;
}

static bool mptcp_frag_can_collapse_to(const struct mptcp_sock *msk,
				       const struct page_frag *pfrag,
				       const struct mptcp_data_frag *df)
{
	return df && pfrag->page == df->page &&
		pfrag->size - pfrag->offset > 0 &&
		df->data_seq + df->data_len == msk->write_seq;
}

static int mptcp_wmem_with_overhead(struct sock *sk, int size)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	int ret, skbs;

	ret = size + ((sizeof(struct mptcp_data_frag) * size) >> PAGE_SHIFT);
	skbs = (msk->tx_pending_data + size) / msk->size_goal_cache;
	if (skbs < msk->skb_tx_cache.qlen)
		return ret;

	return ret + (skbs - msk->skb_tx_cache.qlen) * SKB_TRUESIZE(MAX_TCP_HEADER);
}

static void __mptcp_wmem_reserve(struct sock *sk, int size)
{
	int amount = mptcp_wmem_with_overhead(sk, size);
	struct mptcp_sock *msk = mptcp_sk(sk);

	WARN_ON_ONCE(msk->wmem_reserved);
	if (WARN_ON_ONCE(amount < 0))
		amount = 0;

	if (amount <= sk->sk_forward_alloc)
		goto reserve;

	/* under memory pressure try to reserve at most a single page
	 * otherwise try to reserve the full estimate and fallback
	 * to a single page before entering the error path
	 */
	if ((tcp_under_memory_pressure(sk) && amount > PAGE_SIZE) ||
	    !sk_wmem_schedule(sk, amount)) {
		if (amount <= PAGE_SIZE)
			goto nomem;

		amount = PAGE_SIZE;
		if (!sk_wmem_schedule(sk, amount))
			goto nomem;
	}

reserve:
	msk->wmem_reserved = amount;
	sk->sk_forward_alloc -= amount;
	return;

nomem:
	/* we will wait for memory on next allocation */
	msk->wmem_reserved = -1;
}

static void __mptcp_update_wmem(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (!msk->wmem_reserved)
		return;

	if (msk->wmem_reserved < 0)
		msk->wmem_reserved = 0;
	if (msk->wmem_reserved > 0) {
		sk->sk_forward_alloc += msk->wmem_reserved;
		msk->wmem_reserved = 0;
	}
}

static bool mptcp_wmem_alloc(struct sock *sk, int size)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	/* check for pre-existing error condition */
	if (msk->wmem_reserved < 0)
		return false;

	if (msk->wmem_reserved >= size)
		goto account;

	mptcp_data_lock(sk);
	if (!sk_wmem_schedule(sk, size)) {
		mptcp_data_unlock(sk);
		return false;
	}

	sk->sk_forward_alloc -= size;
	msk->wmem_reserved += size;
	mptcp_data_unlock(sk);

account:
	msk->wmem_reserved -= size;
	return true;
}

static void mptcp_wmem_uncharge(struct sock *sk, int size)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (msk->wmem_reserved < 0)
		msk->wmem_reserved = 0;
	msk->wmem_reserved += size;
}

static void mptcp_mem_reclaim_partial(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	/* if we are experiencing a transint allocation error,
	 * the forward allocation memory has been already
	 * released
	 */
	if (msk->wmem_reserved < 0)
		return;

	mptcp_data_lock(sk);
	sk->sk_forward_alloc += msk->wmem_reserved;
	sk_mem_reclaim_partial(sk);
	msk->wmem_reserved = sk->sk_forward_alloc;
	sk->sk_forward_alloc = 0;
	mptcp_data_unlock(sk);
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
	bool cleaned = false;
	u64 snd_una;

	/* on fallback we just need to ignore snd_una, as this is really
	 * plain TCP
	 */
	if (__mptcp_check_fallback(msk))
		msk->snd_una = READ_ONCE(msk->snd_nxt);

	snd_una = msk->snd_una;
	list_for_each_entry_safe(dfrag, dtmp, &msk->rtx_queue, list) {
		if (after64(dfrag->data_seq + dfrag->data_len, snd_una))
			break;

		if (WARN_ON_ONCE(dfrag == msk->first_pending))
			break;
		dfrag_clear(sk, dfrag);
		cleaned = true;
	}

	dfrag = mptcp_rtx_head(sk);
	if (dfrag && after64(snd_una, dfrag->data_seq)) {
		u64 delta = snd_una - dfrag->data_seq;

		if (WARN_ON_ONCE(delta > dfrag->already_sent))
			goto out;

		dfrag->data_seq += delta;
		dfrag->offset += delta;
		dfrag->data_len -= delta;
		dfrag->already_sent -= delta;

		dfrag_uncharge(sk, delta);
		cleaned = true;
	}

out:
	if (cleaned) {
		if (tcp_under_memory_pressure(sk)) {
			__mptcp_update_wmem(sk);
			sk_mem_reclaim_partial(sk);
		}
	}

	if (snd_una == READ_ONCE(msk->snd_nxt)) {
		if (msk->timer_ival)
			mptcp_stop_timer(sk);
	} else {
		mptcp_reset_timer(sk);
	}
}

static void mptcp_enter_memory_pressure(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool first = true;

	sk_stream_moderate_sndbuf(sk);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (first)
			tcp_enter_memory_pressure(ssk);
		sk_stream_moderate_sndbuf(ssk);
		first = false;
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
};

static int mptcp_check_allowed_size(struct mptcp_sock *msk, u64 data_seq,
				    int avail_size)
{
	u64 window_end = mptcp_wnd_end(msk);

	if (__mptcp_check_fallback(msk))
		return avail_size;

	if (!before64(data_seq + avail_size, window_end)) {
		u64 allowed_size = window_end - data_seq;

		return min_t(unsigned int, allowed_size, avail_size);
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
			skb->reserved_tailroom = skb->end - skb->tail;
			return skb;
		}
		__kfree_skb(skb);
	} else {
		mptcp_enter_memory_pressure(sk);
	}
	return NULL;
}

static bool mptcp_tx_cache_refill(struct sock *sk, int size,
				  struct sk_buff_head *skbs, int *total_ts)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sk_buff *skb;
	int space_needed;

	if (unlikely(tcp_under_memory_pressure(sk))) {
		mptcp_mem_reclaim_partial(sk);

		/* under pressure pre-allocate at most a single skb */
		if (msk->skb_tx_cache.qlen)
			return true;
		space_needed = msk->size_goal_cache;
	} else {
		space_needed = msk->tx_pending_data + size -
			       msk->skb_tx_cache.qlen * msk->size_goal_cache;
	}

	while (space_needed > 0) {
		skb = __mptcp_do_alloc_tx_skb(sk, sk->sk_allocation);
		if (unlikely(!skb)) {
			/* under memory pressure, try to pass the caller a
			 * single skb to allow forward progress
			 */
			while (skbs->qlen > 1) {
				skb = __skb_dequeue_tail(skbs);
				__kfree_skb(skb);
			}
			return skbs->qlen > 0;
		}

		*total_ts += skb->truesize;
		__skb_queue_tail(skbs, skb);
		space_needed -= msk->size_goal_cache;
	}
	return true;
}

static bool __mptcp_alloc_tx_skb(struct sock *sk, struct sock *ssk, gfp_t gfp)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sk_buff *skb;

	if (ssk->sk_tx_skb_cache) {
		skb = ssk->sk_tx_skb_cache;
		if (unlikely(!skb_ext_find(skb, SKB_EXT_MPTCP) &&
			     !__mptcp_add_ext(skb, gfp)))
			return false;
		return true;
	}

	skb = skb_peek(&msk->skb_tx_cache);
	if (skb) {
		if (likely(sk_wmem_schedule(ssk, skb->truesize))) {
			skb = __skb_dequeue(&msk->skb_tx_cache);
			if (WARN_ON_ONCE(!skb))
				return false;

			mptcp_wmem_uncharge(sk, skb->truesize);
			ssk->sk_tx_skb_cache = skb;
			return true;
		}

		/* over memory limit, no point to try to allocate a new skb */
		return false;
	}

	skb = __mptcp_do_alloc_tx_skb(sk, gfp);
	if (!skb)
		return false;

	if (likely(sk_wmem_schedule(ssk, skb->truesize))) {
		ssk->sk_tx_skb_cache = skb;
		return true;
	}
	kfree_skb(skb);
	return false;
}

static bool mptcp_must_reclaim_memory(struct sock *sk, struct sock *ssk)
{
	return !ssk->sk_tx_skb_cache &&
	       !skb_peek(&mptcp_sk(sk)->skb_tx_cache) &&
	       tcp_under_memory_pressure(sk);
}

static bool mptcp_alloc_tx_skb(struct sock *sk, struct sock *ssk)
{
	if (unlikely(mptcp_must_reclaim_memory(sk, ssk)))
		mptcp_mem_reclaim_partial(sk);
	return __mptcp_alloc_tx_skb(sk, ssk, sk->sk_allocation);
}

static int mptcp_sendmsg_frag(struct sock *sk, struct sock *ssk,
			      struct mptcp_data_frag *dfrag,
			      struct mptcp_sendmsg_info *info)
{
	u64 data_seq = dfrag->data_seq + info->sent;
	struct mptcp_sock *msk = mptcp_sk(sk);
	bool zero_window_probe = false;
	struct mptcp_ext *mpext = NULL;
	struct sk_buff *skb, *tail;
	bool can_collapse = false;
	int size_bias = 0;
	int avail_size;
	size_t ret = 0;

	pr_debug("msk=%p ssk=%p sending dfrag at seq=%lld len=%d already sent=%d",
		 msk, ssk, dfrag->data_seq, dfrag->data_len, info->sent);

	/* compute send limit */
	info->mss_now = tcp_send_mss(ssk, &info->size_goal, info->flags);
	avail_size = info->size_goal;
	msk->size_goal_cache = info->size_goal;
	skb = tcp_write_queue_tail(ssk);
	if (skb) {
		/* Limit the write to the size available in the
		 * current skb, if any, so that we create at most a new skb.
		 * Explicitly tells TCP internals to avoid collapsing on later
		 * queue management operation, to avoid breaking the ext <->
		 * SSN association set here
		 */
		mpext = skb_ext_find(skb, SKB_EXT_MPTCP);
		can_collapse = (info->size_goal - skb->len > 0) &&
			 mptcp_skb_can_collapse_to(data_seq, skb, mpext);
		if (!can_collapse) {
			TCP_SKB_CB(skb)->eor = 1;
		} else {
			size_bias = skb->len;
			avail_size = info->size_goal - skb->len;
		}
	}

	/* Zero window and all data acked? Probe. */
	avail_size = mptcp_check_allowed_size(msk, data_seq, avail_size);
	if (avail_size == 0) {
		u64 snd_una = READ_ONCE(msk->snd_una);

		if (skb || snd_una != msk->snd_nxt)
			return 0;
		zero_window_probe = true;
		data_seq = snd_una - 1;
		avail_size = 1;
	}

	if (WARN_ON_ONCE(info->sent > info->limit ||
			 info->limit > dfrag->data_len))
		return 0;

	ret = info->limit - info->sent;
	tail = tcp_build_frag(ssk, avail_size + size_bias, info->flags,
			      dfrag->page, dfrag->offset + info->sent, &ret);
	if (!tail) {
		tcp_remove_empty_skb(sk, tcp_write_queue_tail(ssk));
		return -ENOMEM;
	}

	/* if the tail skb is still the cached one, collapsing really happened.
	 */
	if (skb == tail) {
		TCP_SKB_CB(tail)->tcp_flags &= ~TCPHDR_PSH;
		mpext->data_len += ret;
		WARN_ON_ONCE(!can_collapse);
		WARN_ON_ONCE(zero_window_probe);
		goto out;
	}

	mpext = skb_ext_find(tail, SKB_EXT_MPTCP);
	if (WARN_ON_ONCE(!mpext)) {
		/* should never reach here, stream corrupted */
		return -EINVAL;
	}

	memset(mpext, 0, sizeof(*mpext));
	mpext->data_seq = data_seq;
	mpext->subflow_seq = mptcp_subflow_ctx(ssk)->rel_write_seq;
	mpext->data_len = ret;
	mpext->use_map = 1;
	mpext->dsn64 = 1;

	pr_debug("data_seq=%llu subflow_seq=%u data_len=%u dsn64=%d",
		 mpext->data_seq, mpext->subflow_seq, mpext->data_len,
		 mpext->dsn64);

	if (zero_window_probe) {
		mptcp_subflow_ctx(ssk)->rel_write_seq += ret;
		mpext->frozen = 1;
		ret = 0;
		tcp_push_pending_frames(ssk);
	}
out:
	mptcp_subflow_ctx(ssk)->rel_write_seq += ret;
	return ret;
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

static struct sock *mptcp_subflow_get_send(struct mptcp_sock *msk)
{
	struct subflow_send_info send_info[2];
	struct mptcp_subflow_context *subflow;
	int i, nr_active = 0;
	struct sock *ssk;
	u64 ratio;
	u32 pace;

	sock_owned_by_me((struct sock *)msk);

	if (__mptcp_check_fallback(msk)) {
		if (!msk->first)
			return NULL;
		return sk_stream_memory_free(msk->first) ? msk->first : NULL;
	}

	/* re-use last subflow, if the burst allow that */
	if (msk->last_snd && msk->snd_burst > 0 &&
	    sk_stream_memory_free(msk->last_snd) &&
	    mptcp_subflow_active(mptcp_subflow_ctx(msk->last_snd)))
		return msk->last_snd;

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
		if (!sk_stream_memory_free(subflow->tcp_sock) || !tcp_sk(ssk)->snd_wnd)
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
				       tcp_sk(msk->last_snd)->snd_wnd);
		return msk->last_snd;
	}

	return NULL;
}

static void mptcp_push_release(struct sock *sk, struct sock *ssk,
			       struct mptcp_sendmsg_info *info)
{
	mptcp_set_timeout(sk, ssk);
	tcp_push(ssk, 0, info->mss_now, tcp_sk(ssk)->nonagle, info->size_goal);
	release_sock(ssk);
}

static void mptcp_push_pending(struct sock *sk, unsigned int flags)
{
	struct sock *prev_ssk = NULL, *ssk = NULL;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_sendmsg_info info = {
				.flags = flags,
	};
	struct mptcp_data_frag *dfrag;
	int len, copied = 0;

	while ((dfrag = mptcp_send_head(sk))) {
		info.sent = dfrag->already_sent;
		info.limit = dfrag->data_len;
		len = dfrag->data_len - dfrag->already_sent;
		while (len > 0) {
			int ret = 0;

			prev_ssk = ssk;
			__mptcp_flush_join_list(msk);
			ssk = mptcp_subflow_get_send(msk);

			/* try to keep the subflow socket lock across
			 * consecutive xmit on the same socket
			 */
			if (ssk != prev_ssk && prev_ssk)
				mptcp_push_release(sk, prev_ssk, &info);
			if (!ssk)
				goto out;

			if (ssk != prev_ssk || !prev_ssk)
				lock_sock(ssk);

			/* keep it simple and always provide a new skb for the
			 * subflow, even if we will not use it when collapsing
			 * on the pending one
			 */
			if (!mptcp_alloc_tx_skb(sk, ssk)) {
				mptcp_push_release(sk, ssk, &info);
				goto out;
			}

			ret = mptcp_sendmsg_frag(sk, ssk, dfrag, &info);
			if (ret <= 0) {
				mptcp_push_release(sk, ssk, &info);
				goto out;
			}

			info.sent += ret;
			dfrag->already_sent += ret;
			msk->snd_nxt += ret;
			msk->snd_burst -= ret;
			msk->tx_pending_data -= ret;
			copied += ret;
			len -= ret;
		}
		WRITE_ONCE(msk->first_pending, mptcp_send_next(sk));
	}

	/* at this point we held the socket lock for the last subflow we used */
	if (ssk)
		mptcp_push_release(sk, ssk, &info);

out:
	if (copied) {
		/* start the timer, if it's not pending */
		if (!mptcp_timer_pending(sk))
			mptcp_reset_timer(sk);
		__mptcp_check_send_data_fin(sk);
	}
}

static void __mptcp_subflow_push_pending(struct sock *sk, struct sock *ssk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_sendmsg_info info;
	struct mptcp_data_frag *dfrag;
	struct sock *xmit_ssk;
	int len, copied = 0;
	bool first = true;

	info.flags = 0;
	while ((dfrag = mptcp_send_head(sk))) {
		info.sent = dfrag->already_sent;
		info.limit = dfrag->data_len;
		len = dfrag->data_len - dfrag->already_sent;
		while (len > 0) {
			int ret = 0;

			/* the caller already invoked the packet scheduler,
			 * check for a different subflow usage only after
			 * spooling the first chunk of data
			 */
			xmit_ssk = first ? ssk : mptcp_subflow_get_send(mptcp_sk(sk));
			if (!xmit_ssk)
				goto out;
			if (xmit_ssk != ssk) {
				mptcp_subflow_delegate(mptcp_subflow_ctx(xmit_ssk));
				goto out;
			}

			if (unlikely(mptcp_must_reclaim_memory(sk, ssk))) {
				__mptcp_update_wmem(sk);
				sk_mem_reclaim_partial(sk);
			}
			if (!__mptcp_alloc_tx_skb(sk, ssk, GFP_ATOMIC))
				goto out;

			ret = mptcp_sendmsg_frag(sk, ssk, dfrag, &info);
			if (ret <= 0)
				goto out;

			info.sent += ret;
			dfrag->already_sent += ret;
			msk->snd_nxt += ret;
			msk->snd_burst -= ret;
			msk->tx_pending_data -= ret;
			copied += ret;
			len -= ret;
			first = false;
		}
		WRITE_ONCE(msk->first_pending, mptcp_send_next(sk));
	}

out:
	/* __mptcp_alloc_tx_skb could have released some wmem and we are
	 * not going to flush it via release_sock()
	 */
	__mptcp_update_wmem(sk);
	if (copied) {
		mptcp_set_timeout(sk, ssk);
		tcp_push(ssk, 0, info.mss_now, tcp_sk(ssk)->nonagle,
			 info.size_goal);
		if (!mptcp_timer_pending(sk))
			mptcp_reset_timer(sk);

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

static int mptcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct page_frag *pfrag;
	size_t copied = 0;
	int ret = 0;
	long timeo;

	if (msg->msg_flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
		return -EOPNOTSUPP;

	mptcp_lock_sock(sk, __mptcp_wmem_reserve(sk, min_t(size_t, 1 << 20, len)));

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	if ((1 << sk->sk_state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		ret = sk_stream_wait_connect(sk, &timeo);
		if (ret)
			goto out;
	}

	pfrag = sk_page_frag(sk);

	while (msg_data_left(msg)) {
		int total_ts, frag_truesize = 0;
		struct mptcp_data_frag *dfrag;
		struct sk_buff_head skbs;
		bool dfrag_collapsed;
		size_t psize, offset;

		if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN)) {
			ret = -EPIPE;
			goto out;
		}

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
		__skb_queue_head_init(&skbs);
		if (!mptcp_tx_cache_refill(sk, psize, &skbs, &total_ts))
			goto wait_for_memory;

		if (!mptcp_wmem_alloc(sk, total_ts)) {
			__skb_queue_purge(&skbs);
			goto wait_for_memory;
		}

		skb_queue_splice_tail(&skbs, &msk->skb_tx_cache);
		if (copy_page_from_iter(dfrag->page, offset, psize,
					&msg->msg_iter) != psize) {
			mptcp_wmem_uncharge(sk, psize + frag_truesize);
			ret = -EFAULT;
			goto out;
		}

		/* data successfully copied into the write queue */
		copied += psize;
		dfrag->data_len += psize;
		frag_truesize += psize;
		pfrag->offset += frag_truesize;
		WRITE_ONCE(msk->write_seq, msk->write_seq + psize);
		msk->tx_pending_data += psize;

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
		pr_debug("msk=%p dfrag at seq=%lld len=%d sent=%d new=%d", msk,
			 dfrag->data_seq, dfrag->data_len, dfrag->already_sent,
			 !dfrag_collapsed);

		continue;

wait_for_memory:
		mptcp_set_nospace(sk);
		mptcp_push_pending(sk, msg->msg_flags);
		ret = sk_stream_wait_memory(sk, &timeo);
		if (ret)
			goto out;
	}

	if (copied)
		mptcp_push_pending(sk, msg->msg_flags);

out:
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
	struct sk_buff *skb;
	int copied = 0;

	while ((skb = skb_peek(&msk->receive_queue)) != NULL) {
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

		/* we will bulk release the skb memory later */
		skb->destructor = NULL;
		msk->rmem_released += skb->truesize;
		__skb_unlink(skb, &msk->receive_queue);
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

static void __mptcp_update_rmem(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (!msk->rmem_released)
		return;

	atomic_sub(msk->rmem_released, &sk->sk_rmem_alloc);
	sk_mem_uncharge(sk, msk->rmem_released);
	msk->rmem_released = 0;
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

	__mptcp_flush_join_list(msk);
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
		tcp_cleanup_rbuf(ssk, moved);
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
		mptcp_cleanup_rbuf(msk);
	}
	if (ret)
		mptcp_check_data_fin((struct sock *)msk);
	return !skb_queue_empty(&msk->receive_queue);
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

	mptcp_lock_sock(sk, __mptcp_splice_receive_queue(sk));
	if (unlikely(sk->sk_state == TCP_LISTEN)) {
		copied = -ENOTCONN;
		goto out_err;
	}

	timeo = sock_rcvtimeo(sk, nonblock);

	len = min_t(size_t, len, INT_MAX);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	while (copied < len) {
		int bytes_read;

		bytes_read = __mptcp_recvmsg_mskq(msk, msg, len - copied);
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

			if (test_and_clear_bit(MPTCP_WORK_EOF, &msk->flags))
				mptcp_check_for_eof(msk);

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
		mptcp_wait_data(sk, &timeo);
	}

	if (skb_queue_empty_lockless(&sk->sk_receive_queue) &&
	    skb_queue_empty(&msk->receive_queue)) {
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
		 skb_queue_empty_lockless(&sk->sk_receive_queue), copied);
	mptcp_rcv_space_adjust(msk, copied);

	release_sock(sk);
	return copied;
}

static void mptcp_retransmit_handler(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	set_bit(MPTCP_WORK_RTX, &msk->flags);
	mptcp_schedule_work(sk);
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

static void mptcp_timeout_timer(struct timer_list *t)
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
static struct sock *mptcp_subflow_get_retrans(const struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct sock *backup = NULL;

	sock_owned_by_me((const struct sock *)msk);

	if (__mptcp_check_fallback(msk))
		return NULL;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (!mptcp_subflow_active(subflow))
			continue;

		/* still data outstanding at TCP level?  Don't retransmit. */
		if (!tcp_write_queue_empty(ssk)) {
			if (inet_csk(ssk)->icsk_ca_state >= TCP_CA_Loss)
				continue;
			return NULL;
		}

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
static void __mptcp_close_ssk(struct sock *sk, struct sock *ssk,
			      struct mptcp_subflow_context *subflow)
{
	list_del(&subflow->node);

	lock_sock_nested(ssk, SINGLE_DEPTH_NESTING);

	/* if we are invoked by the msk cleanup code, the subflow is
	 * already orphaned
	 */
	if (ssk->sk_socket)
		sock_orphan(ssk);

	subflow->disposable = 1;

	/* if ssk hit tcp_done(), tcp_cleanup_ulp() cleared the related ops
	 * the ssk has been already destroyed, we just need to release the
	 * reference owned by msk;
	 */
	if (!inet_csk(ssk)->icsk_ulp_ops) {
		kfree_rcu(subflow, rcu);
	} else {
		/* otherwise tcp will dispose of the ssk and subflow ctx */
		__tcp_close(ssk, 0);

		/* close acquired an extra ref */
		__sock_put(ssk);
	}
	release_sock(ssk);

	sock_put(ssk);
}

void mptcp_close_ssk(struct sock *sk, struct sock *ssk,
		     struct mptcp_subflow_context *subflow)
{
	if (sk->sk_state == TCP_ESTABLISHED)
		mptcp_event(MPTCP_EVENT_SUB_CLOSED, mptcp_sk(sk), ssk, GFP_KERNEL);
	__mptcp_close_ssk(sk, ssk, subflow);
}

static unsigned int mptcp_sync_mss(struct sock *sk, u32 pmtu)
{
	return 0;
}

static void __mptcp_close_subflow(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow, *tmp;

	might_sleep();

	list_for_each_entry_safe(subflow, tmp, &msk->conn_list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (inet_sk_state_load(ssk) != TCP_CLOSE)
			continue;

		/* 'subflow_data_ready' will re-sched once rx queue is empty */
		if (!skb_queue_empty_lockless(&ssk->sk_receive_queue))
			continue;

		mptcp_close_ssk((struct sock *)msk, ssk, subflow);
	}
}

static bool mptcp_check_close_timeout(const struct sock *sk)
{
	s32 delta = tcp_jiffies32 - inet_csk(sk)->icsk_mtup.probe_timestamp;
	struct mptcp_subflow_context *subflow;

	if (delta >= TCP_TIMEWAIT_LEN)
		return true;

	/* if all subflows are in closed status don't bother with additional
	 * timeout
	 */
	mptcp_for_each_subflow(mptcp_sk(sk), subflow) {
		if (inet_sk_state_load(mptcp_subflow_tcp_sock(subflow)) !=
		    TCP_CLOSE)
			return false;
	}
	return true;
}

static void mptcp_check_fastclose(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct sock *sk = &msk->sk.icsk_inet.sk;

	if (likely(!READ_ONCE(msk->rcv_fastclose)))
		return;

	mptcp_token_destroy(msk);

	list_for_each_entry_safe(subflow, tmp, &msk->conn_list, node) {
		struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);

		lock_sock(tcp_sk);
		if (tcp_sk->sk_state != TCP_CLOSE) {
			tcp_send_active_reset(tcp_sk, GFP_ATOMIC);
			tcp_set_state(tcp_sk, TCP_CLOSE);
		}
		release_sock(tcp_sk);
	}

	inet_sk_state_store(sk, TCP_CLOSE);
	sk->sk_shutdown = SHUTDOWN_MASK;
	smp_mb__before_atomic(); /* SHUTDOWN must be visible first */
	set_bit(MPTCP_DATA_READY, &msk->flags);
	set_bit(MPTCP_WORK_CLOSE_SUBFLOW, &msk->flags);

	mptcp_close_wake_up(sk);
}

static void mptcp_worker(struct work_struct *work)
{
	struct mptcp_sock *msk = container_of(work, struct mptcp_sock, work);
	struct sock *ssk, *sk = &msk->sk.icsk_inet.sk;
	struct mptcp_sendmsg_info info = {};
	struct mptcp_data_frag *dfrag;
	size_t copied = 0;
	int state, ret;

	lock_sock(sk);
	state = sk->sk_state;
	if (unlikely(state == TCP_CLOSE))
		goto unlock;

	mptcp_check_data_fin_ack(sk);
	__mptcp_flush_join_list(msk);

	mptcp_check_fastclose(msk);

	if (msk->pm.status)
		mptcp_pm_nl_work(msk);

	if (test_and_clear_bit(MPTCP_WORK_EOF, &msk->flags))
		mptcp_check_for_eof(msk);

	__mptcp_check_send_data_fin(sk);
	mptcp_check_data_fin(sk);

	/* if the msk data is completely acked, or the socket timedout,
	 * there is no point in keeping around an orphaned sk
	 */
	if (sock_flag(sk, SOCK_DEAD) &&
	    (mptcp_check_close_timeout(sk) ||
	    (state != sk->sk_state &&
	    ((1 << inet_sk_state_load(sk)) & (TCPF_CLOSE | TCPF_FIN_WAIT2))))) {
		inet_sk_state_store(sk, TCP_CLOSE);
		__mptcp_destroy_sock(sk);
		goto unlock;
	}

	if (test_and_clear_bit(MPTCP_WORK_CLOSE_SUBFLOW, &msk->flags))
		__mptcp_close_subflow(msk);

	if (!test_and_clear_bit(MPTCP_WORK_RTX, &msk->flags))
		goto unlock;

	__mptcp_clean_una(sk);
	dfrag = mptcp_rtx_head(sk);
	if (!dfrag)
		goto unlock;

	ssk = mptcp_subflow_get_retrans(msk);
	if (!ssk)
		goto reset_unlock;

	lock_sock(ssk);

	/* limit retransmission to the bytes already sent on some subflows */
	info.sent = 0;
	info.limit = dfrag->already_sent;
	while (info.sent < dfrag->already_sent) {
		if (!mptcp_alloc_tx_skb(sk, ssk))
			break;

		ret = mptcp_sendmsg_frag(sk, ssk, dfrag, &info);
		if (ret <= 0)
			break;

		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_RETRANSSEGS);
		copied += ret;
		info.sent += ret;
	}
	if (copied)
		tcp_push(ssk, 0, info.mss_now, tcp_sk(ssk)->nonagle,
			 info.size_goal);

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
	INIT_WORK(&msk->work, mptcp_worker);
	__skb_queue_head_init(&msk->receive_queue);
	__skb_queue_head_init(&msk->skb_tx_cache);
	msk->out_of_order_queue = RB_ROOT;
	msk->first_pending = NULL;
	msk->wmem_reserved = 0;
	msk->rmem_released = 0;
	msk->tx_pending_data = 0;
	msk->size_goal_cache = TCP_BASE_MSS;

	msk->ack_hint = NULL;
	msk->first = NULL;
	inet_csk(sk)->icsk_sync_mss = mptcp_sync_mss;

	mptcp_pm_data_init(msk);

	/* re-use the csk retrans timer for MPTCP-level retrans */
	timer_setup(&msk->sk.icsk_retransmit_timer, mptcp_retransmit_timer, 0);
	timer_setup(&sk->sk_timer, mptcp_timeout_timer, 0);
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
	struct sk_buff *skb;

	WRITE_ONCE(msk->first_pending, NULL);
	list_for_each_entry_safe(dfrag, dtmp, &msk->rtx_queue, list)
		dfrag_clear(sk, dfrag);
	while ((skb = __skb_dequeue(&msk->skb_tx_cache)) != NULL) {
		sk->sk_forward_alloc += skb->truesize;
		kfree_skb(skb);
	}
}

static void mptcp_cancel_work(struct sock *sk)
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

static void __mptcp_check_send_data_fin(struct sock *sk)
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

	/* fallback socket will not get data_fin/ack, can move to the next
	 * state now
	 */
	if (__mptcp_check_fallback(msk)) {
		if ((1 << sk->sk_state) & (TCPF_CLOSING | TCPF_LAST_ACK)) {
			inet_sk_state_store(sk, TCP_CLOSE);
			mptcp_close_wake_up(sk);
		} else if (sk->sk_state == TCP_FIN_WAIT1) {
			inet_sk_state_store(sk, TCP_FIN_WAIT2);
		}
	}

	__mptcp_flush_join_list(msk);
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

	__mptcp_check_send_data_fin(sk);
}

static void __mptcp_destroy_sock(struct sock *sk)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct mptcp_sock *msk = mptcp_sk(sk);
	LIST_HEAD(conn_list);

	pr_debug("msk=%p", msk);

	might_sleep();

	/* dispose the ancillatory tcp socket, if any */
	if (msk->subflow) {
		iput(SOCK_INODE(msk->subflow));
		msk->subflow = NULL;
	}

	/* be sure to always acquire the join list lock, to sync vs
	 * mptcp_finish_join().
	 */
	spin_lock_bh(&msk->join_list_lock);
	list_splice_tail_init(&msk->join_list, &msk->conn_list);
	spin_unlock_bh(&msk->join_list_lock);
	list_splice_init(&msk->conn_list, &conn_list);

	sk_stop_timer(sk, &msk->sk.icsk_retransmit_timer);
	sk_stop_timer(sk, &sk->sk_timer);
	msk->pm.status = 0;

	list_for_each_entry_safe(subflow, tmp, &conn_list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		__mptcp_close_ssk(sk, ssk, subflow);
	}

	sk->sk_prot->destroy(sk);

	WARN_ON_ONCE(msk->wmem_reserved);
	WARN_ON_ONCE(msk->rmem_released);
	sk_stream_kill_queues(sk);
	xfrm_sk_free_policy(sk);
	sk_refcnt_debug_release(sk);
	sock_put(sk);
}

static void mptcp_close(struct sock *sk, long timeout)
{
	struct mptcp_subflow_context *subflow;
	bool do_cancel_work = false;

	lock_sock(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;

	if ((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE)) {
		inet_sk_state_store(sk, TCP_CLOSE);
		goto cleanup;
	}

	if (mptcp_close_state(sk))
		__mptcp_wr_shutdown(sk);

	sk_stream_wait_close(sk, timeout);

cleanup:
	/* orphan all the subflows */
	inet_csk(sk)->icsk_mtup.probe_timestamp = tcp_jiffies32;
	list_for_each_entry(subflow, &mptcp_sk(sk)->conn_list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast(ssk);

		sock_orphan(ssk);
		unlock_sock_fast(ssk, slow);
	}
	sock_orphan(sk);

	sock_hold(sk);
	pr_debug("msk=%p state=%d", sk, sk->sk_state);
	if (sk->sk_state == TCP_CLOSE) {
		__mptcp_destroy_sock(sk);
		do_cancel_work = true;
	} else {
		sk_reset_timer(sk, &sk->sk_timer, jiffies + TCP_TIMEWAIT_LEN);
	}
	release_sock(sk);
	if (do_cancel_work)
		mptcp_cancel_work(sk);

	if (mptcp_sk(sk)->token)
		mptcp_event(MPTCP_EVENT_CLOSED, mptcp_sk(sk), NULL, GFP_KERNEL);

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
	struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk = mptcp_sk(sk);

	__mptcp_flush_join_list(msk);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		lock_sock(ssk);
		tcp_disconnect(ssk, flags);
		release_sock(ssk);
	}
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
	msk->snd_nxt = msk->write_seq;
	msk->snd_una = msk->write_seq;
	msk->wnd_end = msk->snd_nxt + req->rsk_rcv_wnd;

	if (mp_opt->mp_capable) {
		msk->can_ack = true;
		msk->remote_key = mp_opt->sndr_key;
		mptcp_crypto_key_sha(msk->remote_key, NULL, &ack_seq);
		ack_seq++;
		WRITE_ONCE(msk->ack_seq, ack_seq);
		WRITE_ONCE(msk->rcv_wnd_sent, ack_seq);
	}

	sock_reset_flag(nsk, SOCK_RCU_FREE);
	/* will be fully established after successful MPC subflow creation */
	inet_sk_state_store(nsk, TCP_SYN_RECV);

	security_inet_csk_clone(nsk, req);
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

	WRITE_ONCE(msk->wnd_end, msk->snd_nxt + tcp_sk(ssk)->snd_wnd);
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
		newsk = new_mptcp_sock;
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPCAPABLEPASSIVEACK);
	} else {
		MPTCP_INC_STATS(sock_net(sk),
				MPTCP_MIB_MPCAPABLEPASSIVEFALLBACK);
	}

	return newsk;
}

void mptcp_destroy_common(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;

	__mptcp_clear_xmit(sk);

	/* move to sk_receive_queue, sk_stream_kill_queues will purge it */
	skb_queue_splice_tail_init(&msk->receive_queue, &sk->sk_receive_queue);

	skb_rbtree_purge(&msk->out_of_order_queue);
	mptcp_token_destroy(msk);
	mptcp_pm_free_anno_list(msk);
}

static void mptcp_destroy(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

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

void __mptcp_data_acked(struct sock *sk)
{
	if (!sock_owned_by_user(sk))
		__mptcp_clean_una(sk);
	else
		set_bit(MPTCP_CLEAN_UNA, &mptcp_sk(sk)->flags);

	if (mptcp_pending_data_fin_ack(sk))
		mptcp_schedule_work(sk);
}

void __mptcp_check_push(struct sock *sk, struct sock *ssk)
{
	if (!mptcp_send_head(sk))
		return;

	if (!sock_owned_by_user(sk)) {
		struct sock *xmit_ssk = mptcp_subflow_get_send(mptcp_sk(sk));

		if (xmit_ssk == ssk)
			__mptcp_subflow_push_pending(sk, ssk);
		else if (xmit_ssk)
			mptcp_subflow_delegate(mptcp_subflow_ctx(xmit_ssk));
	} else {
		set_bit(MPTCP_PUSH_PENDING, &mptcp_sk(sk)->flags);
	}
}

#define MPTCP_DEFERRED_ALL (TCPF_WRITE_TIMER_DEFERRED)

/* processes deferred events and flush wmem */
static void mptcp_release_cb(struct sock *sk)
{
	unsigned long flags, nflags;

	/* push_pending may touch wmem_reserved, do it before the later
	 * cleanup
	 */
	if (test_and_clear_bit(MPTCP_CLEAN_UNA, &mptcp_sk(sk)->flags))
		__mptcp_clean_una(sk);
	if (test_and_clear_bit(MPTCP_PUSH_PENDING, &mptcp_sk(sk)->flags)) {
		/* mptcp_push_pending() acquires the subflow socket lock
		 *
		 * 1) can't be invoked in atomic scope
		 * 2) must avoid ABBA deadlock with msk socket spinlock: the RX
		 *    datapath acquires the msk socket spinlock while helding
		 *    the subflow socket lock
		 */

		spin_unlock_bh(&sk->sk_lock.slock);
		mptcp_push_pending(sk, 0);
		spin_lock_bh(&sk->sk_lock.slock);
	}
	if (test_and_clear_bit(MPTCP_ERROR_REPORT, &mptcp_sk(sk)->flags))
		__mptcp_error_report(sk);

	/* clear any wmem reservation and errors */
	__mptcp_update_wmem(sk);
	__mptcp_update_rmem(sk);

	do {
		flags = sk->sk_tsq_flags;
		if (!(flags & MPTCP_DEFERRED_ALL))
			return;
		nflags = flags & ~MPTCP_DEFERRED_ALL;
	} while (cmpxchg(&sk->sk_tsq_flags, flags, nflags) != flags);

	sock_release_ownership(sk);

	if (flags & TCPF_WRITE_TIMER_DEFERRED) {
		mptcp_retransmit_handler(sk);
		__sock_put(sk);
	}
}

void mptcp_subflow_process_delegated(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = subflow->conn;

	mptcp_data_lock(sk);
	if (!sock_owned_by_user(sk))
		__mptcp_subflow_push_pending(sk, ssk);
	else
		set_bit(MPTCP_PUSH_PENDING, &mptcp_sk(sk)->flags);
	mptcp_data_unlock(sk);
	mptcp_subflow_delegated_done(subflow);
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
	WRITE_ONCE(msk->snd_nxt, msk->write_seq);
	WRITE_ONCE(msk->ack_seq, ack_seq);
	WRITE_ONCE(msk->rcv_wnd_sent, ack_seq);
	WRITE_ONCE(msk->can_ack, 1);
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
	struct socket *parent_sock;
	bool ret;

	pr_debug("msk=%p, subflow=%p", msk, subflow);

	/* mptcp socket already closing? */
	if (!mptcp_is_fully_established(parent))
		return false;

	if (!msk->pm.server_side)
		goto out;

	if (!mptcp_pm_allow_new_subflow(msk))
		return false;

	/* active connections are already on conn_list, and we can't acquire
	 * msk lock here.
	 * use the join list lock as synchronization point and double-check
	 * msk status to avoid racing with __mptcp_destroy_sock()
	 */
	spin_lock_bh(&msk->join_list_lock);
	ret = inet_sk_state_load(parent) == TCP_ESTABLISHED;
	if (ret && !WARN_ON_ONCE(!list_empty(&subflow->node))) {
		list_add_tail(&subflow->node, &msk->join_list);
		sock_hold(ssk);
	}
	spin_unlock_bh(&msk->join_list_lock);
	if (!ret)
		return false;

	/* attach to msk socket only after we are sure he will deal with us
	 * at close time
	 */
	parent_sock = READ_ONCE(parent->sk_socket);
	if (parent_sock && !ssk->sk_socket)
		mptcp_sock_graft(ssk, parent_sock);
	subflow->map_seq = READ_ONCE(msk->ack_seq);
out:
	mptcp_event(MPTCP_EVENT_SUB_ESTABLISHED, msk, ssk, GFP_ATOMIC);
	return true;
}

static void mptcp_shutdown(struct sock *sk, int how)
{
	pr_debug("sk=%p, how=%d", sk, how);

	if ((how & SEND_SHUTDOWN) && mptcp_close_state(sk))
		__mptcp_wr_shutdown(sk);
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
	.shutdown	= mptcp_shutdown,
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
		struct sock *newsk = newsock->sk;

		lock_sock(newsk);

		/* PM/worker can now acquire the first subflow socket
		 * lock without racing with listener queue cleanup,
		 * we can notify it, if needed.
		 */
		subflow = mptcp_subflow_ctx(msk->first);
		list_add(&subflow->node, &msk->conn_list);
		sock_hold(msk->first);
		if (mptcp_is_fully_established(newsk))
			mptcp_pm_fully_established(msk, msk->first, GFP_KERNEL);

		mptcp_copy_inaddrs(newsk, msk->first);
		mptcp_rcv_space_init(msk, msk->first);
		mptcp_propagate_sndbuf(newsk, msk->first);

		/* set ssk->sk_socket of accept()ed flows to mptcp socket.
		 * This is needed so NOSPACE flag can be set from tcp stack.
		 */
		__mptcp_flush_join_list(msk);
		mptcp_for_each_subflow(msk, subflow) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

			if (!ssk->sk_socket)
				mptcp_sock_graft(ssk, newsock);
		}
		release_sock(newsk);
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

static __poll_t mptcp_check_writeable(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;

	if (unlikely(sk->sk_shutdown & SEND_SHUTDOWN))
		return EPOLLOUT | EPOLLWRNORM;

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
	int state;

	msk = mptcp_sk(sk);
	sock_poll_wait(file, sock, wait);

	state = inet_sk_state_load(sk);
	pr_debug("msk=%p state=%d flags=%lx", msk, state, msk->flags);
	if (state == TCP_LISTEN)
		return mptcp_check_readable(msk);

	if (state != TCP_SYN_SENT && state != TCP_SYN_RECV) {
		mask |= mptcp_check_readable(msk);
		mask |= mptcp_check_writeable(msk);
	}
	if (sk->sk_shutdown == SHUTDOWN_MASK || state == TCP_CLOSE)
		mask |= EPOLLHUP;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;

	/* This barrier is coupled with smp_wmb() in tcp_reset() */
	smp_rmb();
	if (sk->sk_err)
		mask |= EPOLLERR;

	return mask;
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
	.shutdown	   = inet_shutdown,
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

static int mptcp_napi_poll(struct napi_struct *napi, int budget)
{
	struct mptcp_delegated_action *delegated;
	struct mptcp_subflow_context *subflow;
	int work_done = 0;

	delegated = container_of(napi, struct mptcp_delegated_action, napi);
	while ((subflow = mptcp_subflow_delegated_next(delegated)) != NULL) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		bh_lock_sock_nested(ssk);
		if (!sock_owned_by_user(ssk) &&
		    mptcp_subflow_has_delegated_action(subflow))
			mptcp_subflow_process_delegated(ssk);
		/* ... elsewhere tcp_release_cb_override already processed
		 * the action or will do at next release_sock().
		 * In both case must dequeue the subflow here - on the same
		 * CPU that scheduled it.
		 */
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
		netif_tx_napi_add(&mptcp_napi_dev, &delegated->napi, mptcp_napi_poll,
				  NAPI_POLL_WEIGHT);
		napi_enable(&delegated->napi);
	}

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
	.shutdown	   = inet_shutdown,
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
