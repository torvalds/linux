// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Bobby Eshleman <bobby.eshleman@bytedance.com>
 *
 * Based off of net/unix/unix_bpf.c
 */

#include <linux/bpf.h>
#include <linux/module.h>
#include <linux/skmsg.h>
#include <linux/socket.h>
#include <linux/wait.h>
#include <net/af_vsock.h>
#include <net/sock.h>

#define vsock_sk_has_data(__sk, __psock)				\
		({	!skb_queue_empty(&(__sk)->sk_receive_queue) ||	\
			!skb_queue_empty(&(__psock)->ingress_skb) ||	\
			!list_empty(&(__psock)->ingress_msg);		\
		})

static struct proto *vsock_prot_saved __read_mostly;
static DEFINE_SPINLOCK(vsock_prot_lock);
static struct proto vsock_bpf_prot;

static bool vsock_has_data(struct sock *sk, struct sk_psock *psock)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	s64 ret;

	ret = vsock_connectible_has_data(vsk);
	if (ret > 0)
		return true;

	return vsock_sk_has_data(sk, psock);
}

static bool vsock_msg_wait_data(struct sock *sk, struct sk_psock *psock, long timeo)
{
	bool ret;

	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		return true;

	if (!timeo)
		return false;

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	ret = vsock_has_data(sk, psock);
	if (!ret) {
		wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);
		ret = vsock_has_data(sk, psock);
	}
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);
	return ret;
}

static int __vsock_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int flags)
{
	struct socket *sock = sk->sk_socket;
	int err;

	if (sk->sk_type == SOCK_STREAM || sk->sk_type == SOCK_SEQPACKET)
		err = vsock_connectible_recvmsg(sock, msg, len, flags);
	else if (sk->sk_type == SOCK_DGRAM)
		err = vsock_dgram_recvmsg(sock, msg, len, flags);
	else
		err = -EPROTOTYPE;

	return err;
}

static int vsock_bpf_recvmsg(struct sock *sk, struct msghdr *msg,
			     size_t len, int flags, int *addr_len)
{
	struct sk_psock *psock;
	int copied;

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return __vsock_recvmsg(sk, msg, len, flags);

	lock_sock(sk);
	if (vsock_has_data(sk, psock) && sk_psock_queue_empty(psock)) {
		release_sock(sk);
		sk_psock_put(sk, psock);
		return __vsock_recvmsg(sk, msg, len, flags);
	}

	copied = sk_msg_recvmsg(sk, psock, msg, len, flags);
	while (copied == 0) {
		long timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

		if (!vsock_msg_wait_data(sk, psock, timeo)) {
			copied = -EAGAIN;
			break;
		}

		if (sk_psock_queue_empty(psock)) {
			release_sock(sk);
			sk_psock_put(sk, psock);
			return __vsock_recvmsg(sk, msg, len, flags);
		}

		copied = sk_msg_recvmsg(sk, psock, msg, len, flags);
	}

	release_sock(sk);
	sk_psock_put(sk, psock);

	return copied;
}

/* Copy of original proto with updated sock_map methods */
static struct proto vsock_bpf_prot = {
	.close = sock_map_close,
	.recvmsg = vsock_bpf_recvmsg,
	.sock_is_readable = sk_msg_is_readable,
	.unhash = sock_map_unhash,
};

static void vsock_bpf_rebuild_protos(struct proto *prot, const struct proto *base)
{
	*prot        = *base;
	prot->close  = sock_map_close;
	prot->recvmsg = vsock_bpf_recvmsg;
	prot->sock_is_readable = sk_msg_is_readable;
}

static void vsock_bpf_check_needs_rebuild(struct proto *ops)
{
	/* Paired with the smp_store_release() below. */
	if (unlikely(ops != smp_load_acquire(&vsock_prot_saved))) {
		spin_lock_bh(&vsock_prot_lock);
		if (likely(ops != vsock_prot_saved)) {
			vsock_bpf_rebuild_protos(&vsock_bpf_prot, ops);
			/* Make sure proto function pointers are updated before publishing the
			 * pointer to the struct.
			 */
			smp_store_release(&vsock_prot_saved, ops);
		}
		spin_unlock_bh(&vsock_prot_lock);
	}
}

int vsock_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore)
{
	struct vsock_sock *vsk;

	if (restore) {
		sk->sk_write_space = psock->saved_write_space;
		sock_replace_proto(sk, psock->sk_proto);
		return 0;
	}

	vsk = vsock_sk(sk);
	if (!vsk->transport)
		return -ENODEV;

	if (!vsk->transport->read_skb)
		return -EOPNOTSUPP;

	vsock_bpf_check_needs_rebuild(psock->sk_proto);
	sock_replace_proto(sk, &vsock_bpf_prot);
	return 0;
}

void __init vsock_bpf_build_proto(void)
{
	vsock_bpf_rebuild_protos(&vsock_bpf_prot, &vsock_proto);
}
