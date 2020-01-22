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

/* If msk has an initial subflow socket, and the MP_CAPABLE handshake has not
 * completed yet or has failed, return the subflow socket.
 * Otherwise return NULL.
 */
static struct socket *__mptcp_nmpc_socket(const struct mptcp_sock *msk)
{
	if (!msk->subflow || mptcp_subflow_ctx(msk->subflow->sk)->fourth_ack)
		return NULL;

	return msk->subflow;
}

/* if msk has a single subflow, and the mp_capable handshake is failed,
 * return it.
 * Otherwise returns NULL
 */
static struct socket *__mptcp_tcp_fallback(const struct mptcp_sock *msk)
{
	struct socket *ssock = __mptcp_nmpc_socket(msk);

	sock_owned_by_me((const struct sock *)msk);

	if (!ssock || sk_is_mptcp(ssock->sk))
		return NULL;

	return ssock;
}

static bool __mptcp_can_create_subflow(const struct mptcp_sock *msk)
{
	return ((struct sock *)msk)->sk_state == TCP_CLOSE;
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
	if (ssock) {
		pr_debug("fallback passthrough");
		ret = sock_sendmsg(ssock, msg);
		release_sock(sk);
		return ret;
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

int mptcp_read_actor(read_descriptor_t *desc, struct sk_buff *skb,
		     unsigned int offset, size_t len)
{
	struct mptcp_read_arg *arg = desc->arg.data;
	size_t copy_len;

	copy_len = min(desc->count, len);

	if (likely(arg->msg)) {
		int err;

		err = skb_copy_datagram_msg(skb, offset, arg->msg, copy_len);
		if (err) {
			pr_debug("error path");
			desc->error = err;
			return err;
		}
	} else {
		pr_debug("Flushing skb payload");
	}

	desc->count -= copy_len;

	pr_debug("consumed %zu bytes, %zu left", copy_len, desc->count);
	return copy_len;
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

static int mptcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			 int nonblock, int flags, int *addr_len)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_subflow_context *subflow;
	bool more_data_avail = false;
	struct mptcp_read_arg arg;
	read_descriptor_t desc;
	bool wait_data = false;
	struct socket *ssock;
	struct tcp_sock *tp;
	bool done = false;
	struct sock *ssk;
	int copied = 0;
	int target;
	long timeo;

	if (msg->msg_flags & ~(MSG_WAITALL | MSG_DONTWAIT))
		return -EOPNOTSUPP;

	lock_sock(sk);
	ssock = __mptcp_tcp_fallback(msk);
	if (ssock) {
		pr_debug("fallback-read subflow=%p",
			 mptcp_subflow_ctx(ssock->sk));
		copied = sock_recvmsg(ssock, msg, flags);
		release_sock(sk);
		return copied;
	}

	arg.msg = msg;
	desc.arg.data = &arg;
	desc.error = 0;

	timeo = sock_rcvtimeo(sk, nonblock);

	len = min_t(size_t, len, INT_MAX);
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	while (!done) {
		u32 map_remaining;
		int bytes_read;

		ssk = mptcp_subflow_recv_lookup(msk);
		pr_debug("msk=%p ssk=%p", msk, ssk);
		if (!ssk)
			goto wait_for_data;

		subflow = mptcp_subflow_ctx(ssk);
		tp = tcp_sk(ssk);

		lock_sock(ssk);
		do {
			/* try to read as much data as available */
			map_remaining = subflow->map_data_len -
					mptcp_subflow_get_map_offset(subflow);
			desc.count = min_t(size_t, len - copied, map_remaining);
			pr_debug("reading %zu bytes, copied %d", desc.count,
				 copied);
			bytes_read = tcp_read_sock(ssk, &desc,
						   mptcp_read_actor);
			if (bytes_read < 0) {
				if (!copied)
					copied = bytes_read;
				done = true;
				goto next;
			}

			pr_debug("msk ack_seq=%llx -> %llx", msk->ack_seq,
				 msk->ack_seq + bytes_read);
			msk->ack_seq += bytes_read;
			copied += bytes_read;
			if (copied >= len) {
				done = true;
				goto next;
			}
			if (tp->urg_data && tp->urg_seq == tp->copied_seq) {
				pr_err("Urgent data present, cannot proceed");
				done = true;
				goto next;
			}
next:
			more_data_avail = mptcp_subflow_data_available(ssk);
		} while (more_data_avail && !done);
		release_sock(ssk);
		continue;

wait_for_data:
		more_data_avail = false;

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
		wait_data = true;
		mptcp_wait_data(sk, &timeo);
	}

	if (more_data_avail) {
		if (!test_bit(MPTCP_DATA_READY, &msk->flags))
			set_bit(MPTCP_DATA_READY, &msk->flags);
	} else if (!wait_data) {
		clear_bit(MPTCP_DATA_READY, &msk->flags);

		/* .. race-breaker: ssk might get new data after last
		 * data_available() returns false.
		 */
		ssk = mptcp_subflow_recv_lookup(msk);
		if (unlikely(ssk))
			set_bit(MPTCP_DATA_READY, &msk->flags);
	}

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

static int __mptcp_init_sock(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	INIT_LIST_HEAD(&msk->conn_list);
	__set_bit(MPTCP_SEND_SPACE, &msk->flags);

	return 0;
}

static int mptcp_init_sock(struct sock *sk)
{
	if (!mptcp_is_enabled(sock_net(sk)))
		return -ENOPROTOOPT;

	return __mptcp_init_sock(sk);
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

static void mptcp_close(struct sock *sk, long timeout)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct mptcp_sock *msk = mptcp_sk(sk);

	mptcp_token_destroy(msk->token);
	inet_sk_state_store(sk, TCP_CLOSE);

	lock_sock(sk);

	list_for_each_entry_safe(subflow, tmp, &msk->conn_list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		__mptcp_close_ssk(sk, ssk, subflow, timeout);
	}

	if (msk->cached_ext)
		__skb_ext_put(msk->cached_ext);
	release_sock(sk);
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
		new_mptcp_sock = sk_clone_lock(sk, GFP_ATOMIC);
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
		msk->remote_key = subflow->remote_key;
		msk->local_key = subflow->local_key;
		msk->token = subflow->token;
		msk->subflow = NULL;

		mptcp_token_update_accept(newsk, new_mptcp_sock);

		mptcp_crypto_key_sha(msk->remote_key, NULL, &ack_seq);
		msk->write_seq = subflow->idsn + 1;
		ack_seq++;
		msk->ack_seq = ack_seq;
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
		subflow->map_seq = ack_seq;
		subflow->map_subflow_seq = 1;
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
}

static int mptcp_setsockopt(struct sock *sk, int level, int optname,
			    char __user *uoptval, unsigned int optlen)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	char __kernel *optval;
	int ret = -EOPNOTSUPP;
	struct socket *ssock;

	/* will be treated as __user in tcp_setsockopt */
	optval = (char __kernel __force *)uoptval;

	pr_debug("msk=%p", msk);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not defined.
	 */
	lock_sock(sk);
	ssock = __mptcp_socket_create(msk, MPTCP_SAME_STATE);
	if (!IS_ERR(ssock)) {
		pr_debug("subflow=%p", ssock->sk);
		ret = kernel_setsockopt(ssock, level, optname, optval, optlen);
	}
	release_sock(sk);

	return ret;
}

static int mptcp_getsockopt(struct sock *sk, int level, int optname,
			    char __user *uoptval, int __user *uoption)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	char __kernel *optval;
	int ret = -EOPNOTSUPP;
	int __kernel *option;
	struct socket *ssock;

	/* will be treated as __user in tcp_getsockopt */
	optval = (char __kernel __force *)uoptval;
	option = (int __kernel __force *)uoption;

	pr_debug("msk=%p", msk);

	/* @@ the meaning of getsockopt() when the socket is connected and
	 * there are multiple subflows is not defined.
	 */
	lock_sock(sk);
	ssock = __mptcp_socket_create(msk, MPTCP_SAME_STATE);
	if (!IS_ERR(ssock)) {
		pr_debug("subflow=%p", ssock->sk);
		ret = kernel_getsockopt(ssock, level, optname, optval, option);
	}
	release_sock(sk);

	return ret;
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
	const struct mptcp_sock *msk;
	struct sock *sk = sock->sk;
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

static struct proto_ops mptcp_stream_ops;

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
	mptcp_stream_ops = inet_stream_ops;
	mptcp_stream_ops.bind = mptcp_bind;
	mptcp_stream_ops.connect = mptcp_stream_connect;
	mptcp_stream_ops.poll = mptcp_poll;
	mptcp_stream_ops.accept = mptcp_stream_accept;
	mptcp_stream_ops.getname = mptcp_v4_getname;
	mptcp_stream_ops.listen = mptcp_listen;
	mptcp_stream_ops.shutdown = mptcp_shutdown;

	mptcp_subflow_init();

	if (proto_register(&mptcp_prot, 1) != 0)
		panic("Failed to register MPTCP proto.\n");

	inet_register_protosw(&mptcp_protosw);
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static struct proto_ops mptcp_v6_stream_ops;
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
	mptcp_v6_prot.obj_size = sizeof(struct mptcp_sock) +
				 sizeof(struct ipv6_pinfo);

	err = proto_register(&mptcp_v6_prot, 1);
	if (err)
		return err;

	mptcp_v6_stream_ops = inet6_stream_ops;
	mptcp_v6_stream_ops.bind = mptcp_bind;
	mptcp_v6_stream_ops.connect = mptcp_stream_connect;
	mptcp_v6_stream_ops.poll = mptcp_poll;
	mptcp_v6_stream_ops.accept = mptcp_stream_accept;
	mptcp_v6_stream_ops.getname = mptcp_v6_getname;
	mptcp_v6_stream_ops.listen = mptcp_listen;
	mptcp_v6_stream_ops.shutdown = mptcp_shutdown;

	err = inet6_register_protosw(&mptcp_v6_protosw);
	if (err)
		proto_unregister(&mptcp_v6_prot);

	return err;
}
#endif
