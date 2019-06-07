// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017 - 2018 Covalent IO, Inc. http://covalent.io */

#include <linux/skmsg.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/init.h>
#include <linux/wait.h>

#include <net/inet_common.h>
#include <net/tls.h>

static bool tcp_bpf_stream_read(const struct sock *sk)
{
	struct sk_psock *psock;
	bool empty = true;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (likely(psock))
		empty = list_empty(&psock->ingress_msg);
	rcu_read_unlock();
	return !empty;
}

static int tcp_bpf_wait_data(struct sock *sk, struct sk_psock *psock,
			     int flags, long timeo, int *err)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int ret = 0;

	if (!timeo)
		return ret;

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	ret = sk_wait_event(sk, &timeo,
			    !list_empty(&psock->ingress_msg) ||
			    !skb_queue_empty(&sk->sk_receive_queue), &wait);
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);
	return ret;
}

int __tcp_bpf_recvmsg(struct sock *sk, struct sk_psock *psock,
		      struct msghdr *msg, int len, int flags)
{
	struct iov_iter *iter = &msg->msg_iter;
	int peek = flags & MSG_PEEK;
	int i, ret, copied = 0;
	struct sk_msg *msg_rx;

	msg_rx = list_first_entry_or_null(&psock->ingress_msg,
					  struct sk_msg, list);

	while (copied != len) {
		struct scatterlist *sge;

		if (unlikely(!msg_rx))
			break;

		i = msg_rx->sg.start;
		do {
			struct page *page;
			int copy;

			sge = sk_msg_elem(msg_rx, i);
			copy = sge->length;
			page = sg_page(sge);
			if (copied + copy > len)
				copy = len - copied;
			ret = copy_page_to_iter(page, sge->offset, copy, iter);
			if (ret != copy) {
				msg_rx->sg.start = i;
				return -EFAULT;
			}

			copied += copy;
			if (likely(!peek)) {
				sge->offset += copy;
				sge->length -= copy;
				sk_mem_uncharge(sk, copy);
				msg_rx->sg.size -= copy;

				if (!sge->length) {
					sk_msg_iter_var_next(i);
					if (!msg_rx->skb)
						put_page(page);
				}
			} else {
				sk_msg_iter_var_next(i);
			}

			if (copied == len)
				break;
		} while (i != msg_rx->sg.end);

		if (unlikely(peek)) {
			msg_rx = list_next_entry(msg_rx, list);
			continue;
		}

		msg_rx->sg.start = i;
		if (!sge->length && msg_rx->sg.start == msg_rx->sg.end) {
			list_del(&msg_rx->list);
			if (msg_rx->skb)
				consume_skb(msg_rx->skb);
			kfree(msg_rx);
		}
		msg_rx = list_first_entry_or_null(&psock->ingress_msg,
						  struct sk_msg, list);
	}

	return copied;
}
EXPORT_SYMBOL_GPL(__tcp_bpf_recvmsg);

int tcp_bpf_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		    int nonblock, int flags, int *addr_len)
{
	struct sk_psock *psock;
	int copied, ret;

	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);
	if (!skb_queue_empty(&sk->sk_receive_queue))
		return tcp_recvmsg(sk, msg, len, nonblock, flags, addr_len);

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return tcp_recvmsg(sk, msg, len, nonblock, flags, addr_len);
	lock_sock(sk);
msg_bytes_ready:
	copied = __tcp_bpf_recvmsg(sk, psock, msg, len, flags);
	if (!copied) {
		int data, err = 0;
		long timeo;

		timeo = sock_rcvtimeo(sk, nonblock);
		data = tcp_bpf_wait_data(sk, psock, flags, timeo, &err);
		if (data) {
			if (skb_queue_empty(&sk->sk_receive_queue))
				goto msg_bytes_ready;
			release_sock(sk);
			sk_psock_put(sk, psock);
			return tcp_recvmsg(sk, msg, len, nonblock, flags, addr_len);
		}
		if (err) {
			ret = err;
			goto out;
		}
		copied = -EAGAIN;
	}
	ret = copied;
out:
	release_sock(sk);
	sk_psock_put(sk, psock);
	return ret;
}

static int bpf_tcp_ingress(struct sock *sk, struct sk_psock *psock,
			   struct sk_msg *msg, u32 apply_bytes, int flags)
{
	bool apply = apply_bytes;
	struct scatterlist *sge;
	u32 size, copied = 0;
	struct sk_msg *tmp;
	int i, ret = 0;

	tmp = kzalloc(sizeof(*tmp), __GFP_NOWARN | GFP_KERNEL);
	if (unlikely(!tmp))
		return -ENOMEM;

	lock_sock(sk);
	tmp->sg.start = msg->sg.start;
	i = msg->sg.start;
	do {
		sge = sk_msg_elem(msg, i);
		size = (apply && apply_bytes < sge->length) ?
			apply_bytes : sge->length;
		if (!sk_wmem_schedule(sk, size)) {
			if (!copied)
				ret = -ENOMEM;
			break;
		}

		sk_mem_charge(sk, size);
		sk_msg_xfer(tmp, msg, i, size);
		copied += size;
		if (sge->length)
			get_page(sk_msg_page(tmp, i));
		sk_msg_iter_var_next(i);
		tmp->sg.end = i;
		if (apply) {
			apply_bytes -= size;
			if (!apply_bytes)
				break;
		}
	} while (i != msg->sg.end);

	if (!ret) {
		msg->sg.start = i;
		msg->sg.size -= apply_bytes;
		sk_psock_queue_msg(psock, tmp);
		sk_psock_data_ready(sk, psock);
	} else {
		sk_msg_free(sk, tmp);
		kfree(tmp);
	}

	release_sock(sk);
	return ret;
}

static int tcp_bpf_push(struct sock *sk, struct sk_msg *msg, u32 apply_bytes,
			int flags, bool uncharge)
{
	bool apply = apply_bytes;
	struct scatterlist *sge;
	struct page *page;
	int size, ret = 0;
	u32 off;

	while (1) {
		bool has_tx_ulp;

		sge = sk_msg_elem(msg, msg->sg.start);
		size = (apply && apply_bytes < sge->length) ?
			apply_bytes : sge->length;
		off  = sge->offset;
		page = sg_page(sge);

		tcp_rate_check_app_limited(sk);
retry:
		has_tx_ulp = tls_sw_has_ctx_tx(sk);
		if (has_tx_ulp) {
			flags |= MSG_SENDPAGE_NOPOLICY;
			ret = kernel_sendpage_locked(sk,
						     page, off, size, flags);
		} else {
			ret = do_tcp_sendpages(sk, page, off, size, flags);
		}

		if (ret <= 0)
			return ret;
		if (apply)
			apply_bytes -= ret;
		msg->sg.size -= ret;
		sge->offset += ret;
		sge->length -= ret;
		if (uncharge)
			sk_mem_uncharge(sk, ret);
		if (ret != size) {
			size -= ret;
			off  += ret;
			goto retry;
		}
		if (!sge->length) {
			put_page(page);
			sk_msg_iter_next(msg, start);
			sg_init_table(sge, 1);
			if (msg->sg.start == msg->sg.end)
				break;
		}
		if (apply && !apply_bytes)
			break;
	}

	return 0;
}

static int tcp_bpf_push_locked(struct sock *sk, struct sk_msg *msg,
			       u32 apply_bytes, int flags, bool uncharge)
{
	int ret;

	lock_sock(sk);
	ret = tcp_bpf_push(sk, msg, apply_bytes, flags, uncharge);
	release_sock(sk);
	return ret;
}

int tcp_bpf_sendmsg_redir(struct sock *sk, struct sk_msg *msg,
			  u32 bytes, int flags)
{
	bool ingress = sk_msg_to_ingress(msg);
	struct sk_psock *psock = sk_psock_get(sk);
	int ret;

	if (unlikely(!psock)) {
		sk_msg_free(sk, msg);
		return 0;
	}
	ret = ingress ? bpf_tcp_ingress(sk, psock, msg, bytes, flags) :
			tcp_bpf_push_locked(sk, msg, bytes, flags, false);
	sk_psock_put(sk, psock);
	return ret;
}
EXPORT_SYMBOL_GPL(tcp_bpf_sendmsg_redir);

static int tcp_bpf_send_verdict(struct sock *sk, struct sk_psock *psock,
				struct sk_msg *msg, int *copied, int flags)
{
	bool cork = false, enospc = msg->sg.start == msg->sg.end;
	struct sock *sk_redir;
	u32 tosend, delta = 0;
	int ret;

more_data:
	if (psock->eval == __SK_NONE) {
		/* Track delta in msg size to add/subtract it on SK_DROP from
		 * returned to user copied size. This ensures user doesn't
		 * get a positive return code with msg_cut_data and SK_DROP
		 * verdict.
		 */
		delta = msg->sg.size;
		psock->eval = sk_psock_msg_verdict(sk, psock, msg);
		if (msg->sg.size < delta)
			delta -= msg->sg.size;
		else
			delta = 0;
	}

	if (msg->cork_bytes &&
	    msg->cork_bytes > msg->sg.size && !enospc) {
		psock->cork_bytes = msg->cork_bytes - msg->sg.size;
		if (!psock->cork) {
			psock->cork = kzalloc(sizeof(*psock->cork),
					      GFP_ATOMIC | __GFP_NOWARN);
			if (!psock->cork)
				return -ENOMEM;
		}
		memcpy(psock->cork, msg, sizeof(*msg));
		return 0;
	}

	tosend = msg->sg.size;
	if (psock->apply_bytes && psock->apply_bytes < tosend)
		tosend = psock->apply_bytes;

	switch (psock->eval) {
	case __SK_PASS:
		ret = tcp_bpf_push(sk, msg, tosend, flags, true);
		if (unlikely(ret)) {
			*copied -= sk_msg_free(sk, msg);
			break;
		}
		sk_msg_apply_bytes(psock, tosend);
		break;
	case __SK_REDIRECT:
		sk_redir = psock->sk_redir;
		sk_msg_apply_bytes(psock, tosend);
		if (psock->cork) {
			cork = true;
			psock->cork = NULL;
		}
		sk_msg_return(sk, msg, tosend);
		release_sock(sk);
		ret = tcp_bpf_sendmsg_redir(sk_redir, msg, tosend, flags);
		lock_sock(sk);
		if (unlikely(ret < 0)) {
			int free = sk_msg_free_nocharge(sk, msg);

			if (!cork)
				*copied -= free;
		}
		if (cork) {
			sk_msg_free(sk, msg);
			kfree(msg);
			msg = NULL;
			ret = 0;
		}
		break;
	case __SK_DROP:
	default:
		sk_msg_free_partial(sk, msg, tosend);
		sk_msg_apply_bytes(psock, tosend);
		*copied -= (tosend + delta);
		return -EACCES;
	}

	if (likely(!ret)) {
		if (!psock->apply_bytes) {
			psock->eval =  __SK_NONE;
			if (psock->sk_redir) {
				sock_put(psock->sk_redir);
				psock->sk_redir = NULL;
			}
		}
		if (msg &&
		    msg->sg.data[msg->sg.start].page_link &&
		    msg->sg.data[msg->sg.start].length)
			goto more_data;
	}
	return ret;
}

static int tcp_bpf_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct sk_msg tmp, *msg_tx = NULL;
	int flags = msg->msg_flags | MSG_NO_SHARED_FRAGS;
	int copied = 0, err = 0;
	struct sk_psock *psock;
	long timeo;

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return tcp_sendmsg(sk, msg, size);

	lock_sock(sk);
	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	while (msg_data_left(msg)) {
		bool enospc = false;
		u32 copy, osize;

		if (sk->sk_err) {
			err = -sk->sk_err;
			goto out_err;
		}

		copy = msg_data_left(msg);
		if (!sk_stream_memory_free(sk))
			goto wait_for_sndbuf;
		if (psock->cork) {
			msg_tx = psock->cork;
		} else {
			msg_tx = &tmp;
			sk_msg_init(msg_tx);
		}

		osize = msg_tx->sg.size;
		err = sk_msg_alloc(sk, msg_tx, msg_tx->sg.size + copy, msg_tx->sg.end - 1);
		if (err) {
			if (err != -ENOSPC)
				goto wait_for_memory;
			enospc = true;
			copy = msg_tx->sg.size - osize;
		}

		err = sk_msg_memcopy_from_iter(sk, &msg->msg_iter, msg_tx,
					       copy);
		if (err < 0) {
			sk_msg_trim(sk, msg_tx, osize);
			goto out_err;
		}

		copied += copy;
		if (psock->cork_bytes) {
			if (size > psock->cork_bytes)
				psock->cork_bytes = 0;
			else
				psock->cork_bytes -= size;
			if (psock->cork_bytes && !enospc)
				goto out_err;
			/* All cork bytes are accounted, rerun the prog. */
			psock->eval = __SK_NONE;
			psock->cork_bytes = 0;
		}

		err = tcp_bpf_send_verdict(sk, psock, msg_tx, &copied, flags);
		if (unlikely(err < 0))
			goto out_err;
		continue;
wait_for_sndbuf:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
		err = sk_stream_wait_memory(sk, &timeo);
		if (err) {
			if (msg_tx && msg_tx != psock->cork)
				sk_msg_free(sk, msg_tx);
			goto out_err;
		}
	}
out_err:
	if (err < 0)
		err = sk_stream_error(sk, msg->msg_flags, err);
	release_sock(sk);
	sk_psock_put(sk, psock);
	return copied ? copied : err;
}

static int tcp_bpf_sendpage(struct sock *sk, struct page *page, int offset,
			    size_t size, int flags)
{
	struct sk_msg tmp, *msg = NULL;
	int err = 0, copied = 0;
	struct sk_psock *psock;
	bool enospc = false;

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return tcp_sendpage(sk, page, offset, size, flags);

	lock_sock(sk);
	if (psock->cork) {
		msg = psock->cork;
	} else {
		msg = &tmp;
		sk_msg_init(msg);
	}

	/* Catch case where ring is full and sendpage is stalled. */
	if (unlikely(sk_msg_full(msg)))
		goto out_err;

	sk_msg_page_add(msg, page, size, offset);
	sk_mem_charge(sk, size);
	copied = size;
	if (sk_msg_full(msg))
		enospc = true;
	if (psock->cork_bytes) {
		if (size > psock->cork_bytes)
			psock->cork_bytes = 0;
		else
			psock->cork_bytes -= size;
		if (psock->cork_bytes && !enospc)
			goto out_err;
		/* All cork bytes are accounted, rerun the prog. */
		psock->eval = __SK_NONE;
		psock->cork_bytes = 0;
	}

	err = tcp_bpf_send_verdict(sk, psock, msg, &copied, flags);
out_err:
	release_sock(sk);
	sk_psock_put(sk, psock);
	return copied ? copied : err;
}

static void tcp_bpf_remove(struct sock *sk, struct sk_psock *psock)
{
	struct sk_psock_link *link;

	while ((link = sk_psock_link_pop(psock))) {
		sk_psock_unlink(sk, link);
		sk_psock_free_link(link);
	}
}

static void tcp_bpf_unhash(struct sock *sk)
{
	void (*saved_unhash)(struct sock *sk);
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		if (sk->sk_prot->unhash)
			sk->sk_prot->unhash(sk);
		return;
	}

	saved_unhash = psock->saved_unhash;
	tcp_bpf_remove(sk, psock);
	rcu_read_unlock();
	saved_unhash(sk);
}

static void tcp_bpf_close(struct sock *sk, long timeout)
{
	void (*saved_close)(struct sock *sk, long timeout);
	struct sk_psock *psock;

	lock_sock(sk);
	rcu_read_lock();
	psock = sk_psock(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		release_sock(sk);
		return sk->sk_prot->close(sk, timeout);
	}

	saved_close = psock->saved_close;
	tcp_bpf_remove(sk, psock);
	rcu_read_unlock();
	release_sock(sk);
	saved_close(sk, timeout);
}

enum {
	TCP_BPF_IPV4,
	TCP_BPF_IPV6,
	TCP_BPF_NUM_PROTS,
};

enum {
	TCP_BPF_BASE,
	TCP_BPF_TX,
	TCP_BPF_NUM_CFGS,
};

static struct proto *tcpv6_prot_saved __read_mostly;
static DEFINE_SPINLOCK(tcpv6_prot_lock);
static struct proto tcp_bpf_prots[TCP_BPF_NUM_PROTS][TCP_BPF_NUM_CFGS];

static void tcp_bpf_rebuild_protos(struct proto prot[TCP_BPF_NUM_CFGS],
				   struct proto *base)
{
	prot[TCP_BPF_BASE]			= *base;
	prot[TCP_BPF_BASE].unhash		= tcp_bpf_unhash;
	prot[TCP_BPF_BASE].close		= tcp_bpf_close;
	prot[TCP_BPF_BASE].recvmsg		= tcp_bpf_recvmsg;
	prot[TCP_BPF_BASE].stream_memory_read	= tcp_bpf_stream_read;

	prot[TCP_BPF_TX]			= prot[TCP_BPF_BASE];
	prot[TCP_BPF_TX].sendmsg		= tcp_bpf_sendmsg;
	prot[TCP_BPF_TX].sendpage		= tcp_bpf_sendpage;
}

static void tcp_bpf_check_v6_needs_rebuild(struct sock *sk, struct proto *ops)
{
	if (sk->sk_family == AF_INET6 &&
	    unlikely(ops != smp_load_acquire(&tcpv6_prot_saved))) {
		spin_lock_bh(&tcpv6_prot_lock);
		if (likely(ops != tcpv6_prot_saved)) {
			tcp_bpf_rebuild_protos(tcp_bpf_prots[TCP_BPF_IPV6], ops);
			smp_store_release(&tcpv6_prot_saved, ops);
		}
		spin_unlock_bh(&tcpv6_prot_lock);
	}
}

static int __init tcp_bpf_v4_build_proto(void)
{
	tcp_bpf_rebuild_protos(tcp_bpf_prots[TCP_BPF_IPV4], &tcp_prot);
	return 0;
}
core_initcall(tcp_bpf_v4_build_proto);

static void tcp_bpf_update_sk_prot(struct sock *sk, struct sk_psock *psock)
{
	int family = sk->sk_family == AF_INET6 ? TCP_BPF_IPV6 : TCP_BPF_IPV4;
	int config = psock->progs.msg_parser   ? TCP_BPF_TX   : TCP_BPF_BASE;

	sk_psock_update_proto(sk, psock, &tcp_bpf_prots[family][config]);
}

static void tcp_bpf_reinit_sk_prot(struct sock *sk, struct sk_psock *psock)
{
	int family = sk->sk_family == AF_INET6 ? TCP_BPF_IPV6 : TCP_BPF_IPV4;
	int config = psock->progs.msg_parser   ? TCP_BPF_TX   : TCP_BPF_BASE;

	/* Reinit occurs when program types change e.g. TCP_BPF_TX is removed
	 * or added requiring sk_prot hook updates. We keep original saved
	 * hooks in this case.
	 */
	sk->sk_prot = &tcp_bpf_prots[family][config];
}

static int tcp_bpf_assert_proto_ops(struct proto *ops)
{
	/* In order to avoid retpoline, we make assumptions when we call
	 * into ops if e.g. a psock is not present. Make sure they are
	 * indeed valid assumptions.
	 */
	return ops->recvmsg  == tcp_recvmsg &&
	       ops->sendmsg  == tcp_sendmsg &&
	       ops->sendpage == tcp_sendpage ? 0 : -ENOTSUPP;
}

void tcp_bpf_reinit(struct sock *sk)
{
	struct sk_psock *psock;

	sock_owned_by_me(sk);

	rcu_read_lock();
	psock = sk_psock(sk);
	tcp_bpf_reinit_sk_prot(sk, psock);
	rcu_read_unlock();
}

int tcp_bpf_init(struct sock *sk)
{
	struct proto *ops = READ_ONCE(sk->sk_prot);
	struct sk_psock *psock;

	sock_owned_by_me(sk);

	rcu_read_lock();
	psock = sk_psock(sk);
	if (unlikely(!psock || psock->sk_proto ||
		     tcp_bpf_assert_proto_ops(ops))) {
		rcu_read_unlock();
		return -EINVAL;
	}
	tcp_bpf_check_v6_needs_rebuild(sk, ops);
	tcp_bpf_update_sk_prot(sk, psock);
	rcu_read_unlock();
	return 0;
}
