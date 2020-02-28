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
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
#include <net/transp_v6.h>
#endif
#include <net/mptcp.h>
#include "protocol.h"

#define MPTCP_SAME_STATE TCP_MAX_STATES

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
struct mptcp6_sock {
	struct mptcp_sock msk;
	struct ipv6_pinfo np;
};
#endif

struct mptcp_skb_cb {
	u32 offset;
};

#define MPTCP_SKB_CB(__skb)	((struct mptcp_skb_cb *)&((__skb)->cb[0]))

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

static bool __mptcp_needs_tcp_fallback(const struct mptcp_sock *msk)
{
	return msk->first && !sk_is_mptcp(msk->first);
}

static struct socket *__mptcp_tcp_fallback(struct mptcp_sock *msk)
{
	sock_owned_by_me((const struct sock *)msk);

	if (likely(!__mptcp_needs_tcp_fallback(msk)))
		return NULL;

	if (msk->subflow) {
		release_sock((struct sock *)msk);
		return msk->subflow;
	}

	return NULL;
}

static bool __mptcp_can_create_subflow(const struct mptcp_sock *msk)
{
	return !msk->first;
}

static struct socket *__mptcp_socket_create(struct mptcp_sock *msk, int state)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	struct socket *ssock;
	int err;

	ssock = __mptcp_nmpc_socket(msk);
	if (ssock)
		goto set_state;

	if (!__mptcp_can_create_subflow(msk))
		return ERR_PTR(-EINVAL);

	err = mptcp_subflow_create_socket(sk, &ssock);
	if (err)
		return ERR_PTR(err);

	msk->first = ssock->sk;
	msk->subflow = ssock;
	subflow = mptcp_subflow_ctx(ssock->sk);
	list_add(&subflow->node, &msk->conn_list);
	subflow->request_mptcp = 1;

set_state:
	if (state != MPTCP_SAME_STATE)
		inet_sk_state_store(sk, state);
	return ssock;
}

static struct sock *mptcp_subflow_get(const struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	sock_owned_by_me((const struct sock *)msk);

	mptcp_for_each_subflow(msk, subflow) {
		return mptcp_subflow_tcp_sock(subflow);
	}

	return NULL;
}

static void __mptcp_move_skb(struct mptcp_sock *msk, struct sock *ssk,
			     struct sk_buff *skb,
			     unsigned int offset, size_t copy_len)
{
	struct sock *sk = (struct sock *)msk;

	__skb_unlink(skb, &ssk->sk_receive_queue);
	skb_set_owner_r(skb, sk);
	__skb_queue_tail(&sk->sk_receive_queue, skb);

	msk->ack_seq += copy_len;
	MPTCP_SKB_CB(skb)->offset = offset;
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
	int rcvbuf;

	rcvbuf = max(ssk->sk_rcvbuf, sk->sk_rcvbuf);
	if (rcvbuf > sk->sk_rcvbuf)
		sk->sk_rcvbuf = rcvbuf;

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

			__mptcp_move_skb(msk, ssk, skb, offset, len);
			seq += len;
			moved += len;

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

	*bytes = moved;

	return done;
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
	if (!READ_ONCE(sk->sk_lock.owned))
		__mptcp_move_skbs_from_subflow(msk, ssk, &moved);

	spin_unlock_bh(&sk->sk_lock.slock);

	return moved > 0;
}

void mptcp_data_ready(struct sock *sk, struct sock *ssk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

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
	sk->sk_data_ready(sk);
}

static bool mptcp_ext_cache_refill(struct mptcp_sock *msk)
{
	if (!msk->cached_ext)
		msk->cached_ext = __skb_ext_alloc();

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

static inline bool mptcp_skb_can_collapse_to(const struct mptcp_sock *msk,
					     const struct sk_buff *skb,
					     const struct mptcp_ext *mpext)
{
	if (!tcp_skb_can_collapse_to(skb))
		return false;

	/* can collapse only if MPTCP level sequence is in order */
	return mpext && mpext->data_seq + mpext->data_len == msk->write_seq;
}

static int mptcp_sendmsg_frag(struct sock *sk, struct sock *ssk,
			      struct msghdr *msg, long *timeo, int *pmss_now,
			      int *ps_goal)
{
	int mss_now, avail_size, size_goal, ret;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_ext *mpext = NULL;
	struct sk_buff *skb, *tail;
	bool can_collapse = false;
	struct page_frag *pfrag;
	size_t psize;

	/* use the mptcp page cache so that we can easily move the data
	 * from one substream to another, but do per subflow memory accounting
	 */
	pfrag = sk_page_frag(sk);
	while (!sk_page_frag_refill(ssk, pfrag) ||
	       !mptcp_ext_cache_refill(msk)) {
		ret = sk_stream_wait_memory(ssk, timeo);
		if (ret)
			return ret;
		if (unlikely(__mptcp_needs_tcp_fallback(msk)))
			return 0;
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
			      mptcp_skb_can_collapse_to(msk, skb, mpext);
		if (!can_collapse)
			TCP_SKB_CB(skb)->eor = 1;
		else
			avail_size = size_goal - skb->len;
	}
	psize = min_t(size_t, pfrag->size - pfrag->offset, avail_size);

	/* Copy to page */
	pr_debug("left=%zu", msg_data_left(msg));
	psize = copy_page_from_iter(pfrag->page, pfrag->offset,
				    min_t(size_t, msg_data_left(msg), psize),
				    &msg->msg_iter);
	pr_debug("left=%zu", msg_data_left(msg));
	if (!psize)
		return -EINVAL;

	/* tell the TCP stack to delay the push so that we can safely
	 * access the skb after the sendpages call
	 */
	ret = do_tcp_sendpages(ssk, pfrag->page, pfrag->offset, psize,
			       msg->msg_flags | MSG_SENDPAGE_NOTLAST);
	if (ret <= 0)
		return ret;
	if (unlikely(ret < psize))
		iov_iter_revert(&msg->msg_iter, psize - ret);

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
	mpext->data_seq = msk->write_seq;
	mpext->subflow_seq = mptcp_subflow_ctx(ssk)->rel_write_seq;
	mpext->data_len = ret;
	mpext->use_map = 1;
	mpext->dsn64 = 1;

	pr_debug("data_seq=%llu subflow_seq=%u data_len=%u dsn64=%d",
		 mpext->data_seq, mpext->subflow_seq, mpext->data_len,
		 mpext->dsn64);

out:
	pfrag->offset += ret;
	msk->write_seq += ret;
	mptcp_subflow_ctx(ssk)->rel_write_seq += ret;

	return ret;
}

static void ssk_check_wmem(struct mptcp_sock *msk, struct sock *ssk)
{
	struct socket *sock;

	if (likely(sk_stream_is_writeable(ssk)))
		return;

	sock = READ_ONCE(ssk->sk_socket);

	if (sock) {
		clear_bit(MPTCP_SEND_SPACE, &msk->flags);
		smp_mb__after_atomic();
		/* set NOSPACE only after clearing SEND_SPACE flag */
		set_bit(SOCK_NOSPACE, &sock->flags);
	}
}

static int mptcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	int mss_now = 0, size_goal = 0, ret = 0;
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *ssock;
	size_t copied = 0;
	struct sock *ssk;
	long timeo;

	if (msg->msg_flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
		return -EOPNOTSUPP;

	lock_sock(sk);
	ssock = __mptcp_tcp_fallback(msk);
	if (unlikely(ssock)) {
fallback:
		pr_debug("fallback passthrough");
		ret = sock_sendmsg(ssock, msg);
		return ret >= 0 ? ret + copied : (copied ? copied : ret);
	}

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	ssk = mptcp_subflow_get(msk);
	if (!ssk) {
		release_sock(sk);
		return -ENOTCONN;
	}

	pr_debug("conn_list->subflow=%p", ssk);

	lock_sock(ssk);
	while (msg_data_left(msg)) {
		ret = mptcp_sendmsg_frag(sk, ssk, msg, &timeo, &mss_now,
					 &size_goal);
		if (ret < 0)
			break;
		if (ret == 0 && unlikely(__mptcp_needs_tcp_fallback(msk))) {
			release_sock(ssk);
			ssock = __mptcp_tcp_fallback(msk);
			goto fallback;
		}

		copied += ret;
	}

	if (copied) {
		ret = copied;
		tcp_push(ssk, msg->msg_flags, mss_now, tcp_sk(ssk)->nonagle,
			 size_goal);
	}

	ssk_check_wmem(msk, ssk);
	release_sock(ssk);
	release_sock(sk);
	return ret;
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

static bool __mptcp_move_skbs(struct mptcp_sock *msk)
{
	unsigned int moved = 0;
	bool done;

	do {
		struct sock *ssk = mptcp_subflow_recv_lookup(msk);

		if (!ssk)
			break;

		lock_sock(ssk);
		done = __mptcp_move_skbs_from_subflow(msk, ssk, &moved);
		release_sock(ssk);
	} while (!done);

	return moved > 0;
}

static int mptcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			 int nonblock, int flags, int *addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *ssock;
	int copied = 0;
	int target;
	long timeo;

	if (msg->msg_flags & ~(MSG_WAITALL | MSG_DONTWAIT))
		return -EOPNOTSUPP;

	lock_sock(sk);
	ssock = __mptcp_tcp_fallback(msk);
	if (unlikely(ssock)) {
fallback:
		pr_debug("fallback-read subflow=%p",
			 mptcp_subflow_ctx(ssock->sk));
		copied = sock_recvmsg(ssock, msg, flags);
		return copied;
	}

	timeo = sock_rcvtimeo(sk, nonblock);

	len = min_t(size_t, len, INT_MAX);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

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
		if (unlikely(__mptcp_tcp_fallback(msk)))
			goto fallback;
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
	release_sock(sk);
	return copied;
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

static void mptcp_worker(struct work_struct *work)
{
	struct mptcp_sock *msk = container_of(work, struct mptcp_sock, work);
	struct sock *sk = &msk->sk.icsk_inet.sk;

	lock_sock(sk);
	__mptcp_move_skbs(msk);
	release_sock(sk);
	sock_put(sk);
}

static int __mptcp_init_sock(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	INIT_LIST_HEAD(&msk->conn_list);
	__set_bit(MPTCP_SEND_SPACE, &msk->flags);
	INIT_WORK(&msk->work, mptcp_worker);

	msk->first = NULL;
	inet_csk(sk)->icsk_sync_mss = mptcp_sync_mss;

	return 0;
}

static int mptcp_init_sock(struct sock *sk)
{
	if (!mptcp_is_enabled(sock_net(sk)))
		return -ENOPROTOOPT;

	return __mptcp_init_sock(sk);
}

static void mptcp_cancel_work(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (cancel_work_sync(&msk->work))
		sock_put(sk);
}

static void mptcp_subflow_shutdown(struct sock *ssk, int how)
{
	lock_sock(ssk);

	switch (ssk->sk_state) {
	case TCP_LISTEN:
		if (!(how & RCV_SHUTDOWN))
			break;
		/* fall through */
	case TCP_SYN_SENT:
		tcp_disconnect(ssk, O_NONBLOCK);
		break;
	default:
		ssk->sk_shutdown |= how;
		tcp_shutdown(ssk, how);
		break;
	}

	/* Wake up anyone sleeping in poll. */
	ssk->sk_state_change(ssk);
	release_sock(ssk);
}

/* Called with msk lock held, releases such lock before returning */
static void mptcp_close(struct sock *sk, long timeout)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct mptcp_sock *msk = mptcp_sk(sk);
	LIST_HEAD(conn_list);

	lock_sock(sk);

	mptcp_token_destroy(msk->token);
	inet_sk_state_store(sk, TCP_CLOSE);

	list_splice_init(&msk->conn_list, &conn_list);

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

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct ipv6_pinfo *mptcp_inet6_sk(const struct sock *sk)
{
	unsigned int offset = sizeof(struct mptcp6_sock) - sizeof(struct ipv6_pinfo);

	return (struct ipv6_pinfo *)(((u8 *)sk) + offset);
}
#endif

static struct sock *mptcp_sk_clone_lock(const struct sock *sk)
{
	struct sock *nsk = sk_clone_lock(sk, GFP_ATOMIC);

	if (!nsk)
		return NULL;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (nsk->sk_family == AF_INET6)
		inet_sk(nsk)->pinet6 = mptcp_inet6_sk(nsk);
#endif

	return nsk;
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
		u64 ack_seq;

		subflow = mptcp_subflow_ctx(newsk);
		lock_sock(sk);

		local_bh_disable();
		new_mptcp_sock = mptcp_sk_clone_lock(sk);
		if (!new_mptcp_sock) {
			*err = -ENOBUFS;
			local_bh_enable();
			release_sock(sk);
			mptcp_subflow_shutdown(newsk, SHUT_RDWR + 1);
			tcp_close(newsk, 0);
			return NULL;
		}

		__mptcp_init_sock(new_mptcp_sock);

		msk = mptcp_sk(new_mptcp_sock);
		msk->local_key = subflow->local_key;
		msk->token = subflow->token;
		msk->subflow = NULL;
		msk->first = newsk;

		mptcp_token_update_accept(newsk, new_mptcp_sock);

		msk->write_seq = subflow->idsn + 1;
		if (subflow->can_ack) {
			msk->can_ack = true;
			msk->remote_key = subflow->remote_key;
			mptcp_crypto_key_sha(msk->remote_key, NULL, &ack_seq);
			ack_seq++;
			msk->ack_seq = ack_seq;
		}
		newsk = new_mptcp_sock;
		mptcp_copy_inaddrs(newsk, ssk);
		list_add(&subflow->node, &msk->conn_list);

		/* will be fully established at mptcp_stream_accept()
		 * completion.
		 */
		inet_sk_state_store(new_mptcp_sock, TCP_SYN_RECV);
		bh_unlock_sock(new_mptcp_sock);
		local_bh_enable();
		release_sock(sk);

		/* the subflow can already receive packet, avoid racing with
		 * the receive path and process the pending ones
		 */
		lock_sock(ssk);
		subflow->rel_write_seq = 1;
		subflow->tcp_sock = ssk;
		subflow->conn = new_mptcp_sock;
		if (unlikely(!skb_queue_empty(&ssk->sk_receive_queue)))
			mptcp_subflow_data_available(ssk);
		release_sock(ssk);
	}

	return newsk;
}

static void mptcp_destroy(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (msk->cached_ext)
		__skb_ext_put(msk->cached_ext);
}

static int mptcp_setsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, unsigned int optlen)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *ssock;

	pr_debug("msk=%p", msk);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when TCP socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssock = __mptcp_tcp_fallback(msk);
	if (ssock)
		return tcp_setsockopt(ssock->sk, level, optname, optval,
				      optlen);

	release_sock(sk);

	return -EOPNOTSUPP;
}

static int mptcp_getsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int __user *option)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct socket *ssock;

	pr_debug("msk=%p", msk);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssock = __mptcp_tcp_fallback(msk);
	if (ssock)
		return tcp_getsockopt(ssock->sk, level, optname, optval,
				      option);

	release_sock(sk);

	return -EOPNOTSUPP;
}

#define MPTCP_DEFERRED_ALL TCPF_DELACK_TIMER_DEFERRED

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

	if (flags & TCPF_DELACK_TIMER_DEFERRED) {
		struct mptcp_sock *msk = mptcp_sk(sk);
		struct sock *ssk;

		ssk = mptcp_subflow_recv_lookup(msk);
		if (!ssk || !schedule_work(&msk->work))
			__sock_put(sk);
	}
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

	if (!subflow->mp_capable)
		return;

	sk = subflow->conn;
	msk = mptcp_sk(sk);

	pr_debug("msk=%p, token=%u", sk, subflow->token);

	mptcp_crypto_key_sha(subflow->remote_key, NULL, &ack_seq);
	ack_seq++;
	subflow->map_seq = ack_seq;
	subflow->map_subflow_seq = 1;
	subflow->rel_write_seq = 1;

	/* the socket is not connected yet, no msk/subflow ops can access/race
	 * accessing the field below
	 */
	WRITE_ONCE(msk->remote_key, subflow->remote_key);
	WRITE_ONCE(msk->local_key, subflow->local_key);
	WRITE_ONCE(msk->token, subflow->token);
	WRITE_ONCE(msk->write_seq, subflow->idsn + 1);
	WRITE_ONCE(msk->ack_seq, ack_seq);
	WRITE_ONCE(msk->can_ack, 1);
}

static void mptcp_sock_graft(struct sock *sk, struct socket *parent)
{
	write_lock_bh(&sk->sk_callback_lock);
	rcu_assign_pointer(sk->sk_wq, &parent->wq);
	sk_set_socket(sk, parent);
	sk->sk_uid = SOCK_INODE(parent)->i_uid;
	write_unlock_bh(&sk->sk_callback_lock);
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
	.close		= mptcp_close,
	.accept		= mptcp_accept,
	.setsockopt	= mptcp_setsockopt,
	.getsockopt	= mptcp_getsockopt,
	.shutdown	= tcp_shutdown,
	.destroy	= mptcp_destroy,
	.sendmsg	= mptcp_sendmsg,
	.recvmsg	= mptcp_recvmsg,
	.release_cb	= mptcp_release_cb,
	.hash		= inet_hash,
	.unhash		= inet_unhash,
	.get_port	= mptcp_get_port,
	.stream_memory_free	= mptcp_memory_free,
	.obj_size	= sizeof(struct mptcp_sock),
	.no_autobind	= true,
};

static int mptcp_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	lock_sock(sock->sk);
	ssock = __mptcp_socket_create(msk, MPTCP_SAME_STATE);
	if (IS_ERR(ssock)) {
		err = PTR_ERR(ssock);
		goto unlock;
	}

	err = ssock->ops->bind(ssock, uaddr, addr_len);
	if (!err)
		mptcp_copy_inaddrs(sock->sk, ssock->sk);

unlock:
	release_sock(sock->sk);
	return err;
}

static int mptcp_stream_connect(struct socket *sock, struct sockaddr *uaddr,
				int addr_len, int flags)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	lock_sock(sock->sk);
	ssock = __mptcp_socket_create(msk, TCP_SYN_SENT);
	if (IS_ERR(ssock)) {
		err = PTR_ERR(ssock);
		goto unlock;
	}

#ifdef CONFIG_TCP_MD5SIG
	/* no MPTCP if MD5SIG is enabled on this socket or we may run out of
	 * TCP option space.
	 */
	if (rcu_access_pointer(tcp_sk(ssock->sk)->md5sig_info))
		mptcp_subflow_ctx(ssock->sk)->request_mptcp = 0;
#endif

	err = ssock->ops->connect(ssock, uaddr, addr_len, flags);
	inet_sk_state_store(sock->sk, inet_sk_state_load(ssock->sk));
	mptcp_copy_inaddrs(sock->sk, ssock->sk);

unlock:
	release_sock(sock->sk);
	return err;
}

static int mptcp_v4_getname(struct socket *sock, struct sockaddr *uaddr,
			    int peer)
{
	if (sock->sk->sk_prot == &tcp_prot) {
		/* we are being invoked from __sys_accept4, after
		 * mptcp_accept() has just accepted a non-mp-capable
		 * flow: sk is a tcp_sk, not an mptcp one.
		 *
		 * Hand the socket over to tcp so all further socket ops
		 * bypass mptcp.
		 */
		sock->ops = &inet_stream_ops;
	}

	return inet_getname(sock, uaddr, peer);
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static int mptcp_v6_getname(struct socket *sock, struct sockaddr *uaddr,
			    int peer)
{
	if (sock->sk->sk_prot == &tcpv6_prot) {
		/* we are being invoked from __sys_accept4 after
		 * mptcp_accept() has accepted a non-mp-capable
		 * subflow: sk is a tcp_sk, not mptcp.
		 *
		 * Hand the socket over to tcp so all further
		 * socket ops bypass mptcp.
		 */
		sock->ops = &inet6_stream_ops;
	}

	return inet6_getname(sock, uaddr, peer);
}
#endif

static int mptcp_listen(struct socket *sock, int backlog)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct socket *ssock;
	int err;

	pr_debug("msk=%p", msk);

	lock_sock(sock->sk);
	ssock = __mptcp_socket_create(msk, TCP_LISTEN);
	if (IS_ERR(ssock)) {
		err = PTR_ERR(ssock);
		goto unlock;
	}

	err = ssock->ops->listen(ssock, backlog);
	inet_sk_state_store(sock->sk, inet_sk_state_load(ssock->sk));
	if (!err)
		mptcp_copy_inaddrs(sock->sk, ssock->sk);

unlock:
	release_sock(sock->sk);
	return err;
}

static bool is_tcp_proto(const struct proto *p)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	return p == &tcp_prot || p == &tcpv6_prot;
#else
	return p == &tcp_prot;
#endif
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

	sock_hold(ssock->sk);
	release_sock(sock->sk);

	err = ssock->ops->accept(sock, newsock, flags, kern);
	if (err == 0 && !is_tcp_proto(newsock->sk->sk_prot)) {
		struct mptcp_sock *msk = mptcp_sk(newsock->sk);
		struct mptcp_subflow_context *subflow;

		/* set ssk->sk_socket of accept()ed flows to mptcp socket.
		 * This is needed so NOSPACE flag can be set from tcp stack.
		 */
		list_for_each_entry(subflow, &msk->conn_list, node) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

			if (!ssk->sk_socket)
				mptcp_sock_graft(ssk, newsock);
		}

		inet_sk_state_store(newsock->sk, TCP_ESTABLISHED);
	}

	sock_put(ssock->sk);
	return err;

unlock_fail:
	release_sock(sock->sk);
	return -EINVAL;
}

static __poll_t mptcp_poll(struct file *file, struct socket *sock,
			   struct poll_table_struct *wait)
{
	struct sock *sk = sock->sk;
	struct mptcp_sock *msk;
	struct socket *ssock;
	__poll_t mask = 0;

	msk = mptcp_sk(sk);
	lock_sock(sk);
	ssock = __mptcp_nmpc_socket(msk);
	if (ssock) {
		mask = ssock->ops->poll(file, ssock, wait);
		release_sock(sk);
		return mask;
	}

	release_sock(sk);
	sock_poll_wait(file, sock, wait);
	lock_sock(sk);
	ssock = __mptcp_tcp_fallback(msk);
	if (unlikely(ssock))
		return ssock->ops->poll(file, ssock, NULL);

	if (test_bit(MPTCP_DATA_READY, &msk->flags))
		mask = EPOLLIN | EPOLLRDNORM;
	if (sk_stream_is_writeable(sk) &&
	    test_bit(MPTCP_SEND_SPACE, &msk->flags))
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;

	release_sock(sk);

	return mask;
}

static int mptcp_shutdown(struct socket *sock, int how)
{
	struct mptcp_sock *msk = mptcp_sk(sock->sk);
	struct mptcp_subflow_context *subflow;
	int ret = 0;

	pr_debug("sk=%p, how=%d", msk, how);

	lock_sock(sock->sk);

	if (how == SHUT_WR || how == SHUT_RDWR)
		inet_sk_state_store(sock->sk, TCP_FIN_WAIT1);

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

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *tcp_sk = mptcp_subflow_tcp_sock(subflow);

		mptcp_subflow_shutdown(tcp_sk, how);
	}

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
	.getname	   = mptcp_v4_getname,
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
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

static struct inet_protosw mptcp_protosw = {
	.type		= SOCK_STREAM,
	.protocol	= IPPROTO_MPTCP,
	.prot		= &mptcp_prot,
	.ops		= &mptcp_stream_ops,
	.flags		= INET_PROTOSW_ICSK,
};

void mptcp_proto_init(void)
{
	mptcp_prot.h.hashinfo = tcp_prot.h.hashinfo;

	mptcp_subflow_init();

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
	.getname	   = mptcp_v6_getname,
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
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
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

int mptcp_proto_v6_init(void)
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
