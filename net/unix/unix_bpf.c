// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Cong Wang <cong.wang@bytedance.com> */

#include <linux/skmsg.h>
#include <linux/bpf.h>
#include <net/sock.h>
#include <net/af_unix.h>

#define unix_sk_has_data(__sk, __psock)					\
		({	!skb_queue_empty(&__sk->sk_receive_queue) ||	\
			!skb_queue_empty(&__psock->ingress_skb) ||	\
			!list_empty(&__psock->ingress_msg);		\
		})

static int unix_msg_wait_data(struct sock *sk, struct sk_psock *psock,
			      long timeo)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct unix_sock *u = unix_sk(sk);
	int ret = 0;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		return 1;

	if (!timeo)
		return ret;

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	if (!unix_sk_has_data(sk, psock)) {
		mutex_unlock(&u->iolock);
		wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);
		mutex_lock(&u->iolock);
		ret = unix_sk_has_data(sk, psock);
	}
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);
	return ret;
}

static int unix_dgram_bpf_recvmsg(struct sock *sk, struct msghdr *msg,
				  size_t len, int nonblock, int flags,
				  int *addr_len)
{
	struct unix_sock *u = unix_sk(sk);
	struct sk_psock *psock;
	int copied, ret;

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return __unix_dgram_recvmsg(sk, msg, len, flags);

	mutex_lock(&u->iolock);
	if (!skb_queue_empty(&sk->sk_receive_queue) &&
	    sk_psock_queue_empty(psock)) {
		ret = __unix_dgram_recvmsg(sk, msg, len, flags);
		goto out;
	}

msg_bytes_ready:
	copied = sk_msg_recvmsg(sk, psock, msg, len, flags);
	if (!copied) {
		long timeo;
		int data;

		timeo = sock_rcvtimeo(sk, nonblock);
		data = unix_msg_wait_data(sk, psock, timeo);
		if (data) {
			if (!sk_psock_queue_empty(psock))
				goto msg_bytes_ready;
			ret = __unix_dgram_recvmsg(sk, msg, len, flags);
			goto out;
		}
		copied = -EAGAIN;
	}
	ret = copied;
out:
	mutex_unlock(&u->iolock);
	sk_psock_put(sk, psock);
	return ret;
}

static struct proto *unix_prot_saved __read_mostly;
static DEFINE_SPINLOCK(unix_prot_lock);
static struct proto unix_bpf_prot;

static void unix_bpf_rebuild_protos(struct proto *prot, const struct proto *base)
{
	*prot        = *base;
	prot->close  = sock_map_close;
	prot->recvmsg = unix_dgram_bpf_recvmsg;
}

static void unix_bpf_check_needs_rebuild(struct proto *ops)
{
	if (unlikely(ops != smp_load_acquire(&unix_prot_saved))) {
		spin_lock_bh(&unix_prot_lock);
		if (likely(ops != unix_prot_saved)) {
			unix_bpf_rebuild_protos(&unix_bpf_prot, ops);
			smp_store_release(&unix_prot_saved, ops);
		}
		spin_unlock_bh(&unix_prot_lock);
	}
}

int unix_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore)
{
	if (restore) {
		sk->sk_write_space = psock->saved_write_space;
		WRITE_ONCE(sk->sk_prot, psock->sk_proto);
		return 0;
	}

	unix_bpf_check_needs_rebuild(psock->sk_proto);
	WRITE_ONCE(sk->sk_prot, &unix_bpf_prot);
	return 0;
}

void __init unix_bpf_build_proto(void)
{
	unix_bpf_rebuild_protos(&unix_bpf_prot, &unix_proto);
}
