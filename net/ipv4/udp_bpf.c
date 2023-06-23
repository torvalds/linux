// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Cloudflare Ltd https://cloudflare.com */

#include <linux/skmsg.h>
#include <net/sock.h>
#include <net/udp.h>
#include <net/inet_common.h>

#include "udp_impl.h"

static struct proto *udpv6_prot_saved __read_mostly;

static int sk_udp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			  int flags, int *addr_len)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		return udpv6_prot_saved->recvmsg(sk, msg, len, flags, addr_len);
#endif
	return udp_prot.recvmsg(sk, msg, len, flags, addr_len);
}

static bool udp_sk_has_data(struct sock *sk)
{
	return !skb_queue_empty(&udp_sk(sk)->reader_queue) ||
	       !skb_queue_empty(&sk->sk_receive_queue);
}

static bool psock_has_data(struct sk_psock *psock)
{
	return !skb_queue_empty(&psock->ingress_skb) ||
	       !sk_psock_queue_empty(psock);
}

#define udp_msg_has_data(__sk, __psock)	\
		({ udp_sk_has_data(__sk) || psock_has_data(__psock); })

static int udp_msg_wait_data(struct sock *sk, struct sk_psock *psock,
			     long timeo)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int ret = 0;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		return 1;

	if (!timeo)
		return ret;

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	ret = udp_msg_has_data(sk, psock);
	if (!ret) {
		wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);
		ret = udp_msg_has_data(sk, psock);
	}
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);
	return ret;
}

static int udp_bpf_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			   int flags, int *addr_len)
{
	struct sk_psock *psock;
	int copied, ret;

	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return sk_udp_recvmsg(sk, msg, len, flags, addr_len);

	if (!psock_has_data(psock)) {
		ret = sk_udp_recvmsg(sk, msg, len, flags, addr_len);
		goto out;
	}

msg_bytes_ready:
	copied = sk_msg_recvmsg(sk, psock, msg, len, flags);
	if (!copied) {
		long timeo;
		int data;

		timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
		data = udp_msg_wait_data(sk, psock, timeo);
		if (data) {
			if (psock_has_data(psock))
				goto msg_bytes_ready;
			ret = sk_udp_recvmsg(sk, msg, len, flags, addr_len);
			goto out;
		}
		copied = -EAGAIN;
	}
	ret = copied;
out:
	sk_psock_put(sk, psock);
	return ret;
}

enum {
	UDP_BPF_IPV4,
	UDP_BPF_IPV6,
	UDP_BPF_NUM_PROTS,
};

static DEFINE_SPINLOCK(udpv6_prot_lock);
static struct proto udp_bpf_prots[UDP_BPF_NUM_PROTS];

static void udp_bpf_rebuild_protos(struct proto *prot, const struct proto *base)
{
	*prot        = *base;
	prot->close  = sock_map_close;
	prot->recvmsg = udp_bpf_recvmsg;
	prot->sock_is_readable = sk_msg_is_readable;
}

static void udp_bpf_check_v6_needs_rebuild(struct proto *ops)
{
	if (unlikely(ops != smp_load_acquire(&udpv6_prot_saved))) {
		spin_lock_bh(&udpv6_prot_lock);
		if (likely(ops != udpv6_prot_saved)) {
			udp_bpf_rebuild_protos(&udp_bpf_prots[UDP_BPF_IPV6], ops);
			smp_store_release(&udpv6_prot_saved, ops);
		}
		spin_unlock_bh(&udpv6_prot_lock);
	}
}

static int __init udp_bpf_v4_build_proto(void)
{
	udp_bpf_rebuild_protos(&udp_bpf_prots[UDP_BPF_IPV4], &udp_prot);
	return 0;
}
late_initcall(udp_bpf_v4_build_proto);

int udp_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore)
{
	int family = sk->sk_family == AF_INET ? UDP_BPF_IPV4 : UDP_BPF_IPV6;

	if (restore) {
		sk->sk_write_space = psock->saved_write_space;
		sock_replace_proto(sk, psock->sk_proto);
		return 0;
	}

	if (sk->sk_family == AF_INET6)
		udp_bpf_check_v6_needs_rebuild(psock->sk_proto);

	sock_replace_proto(sk, &udp_bpf_prots[family]);
	return 0;
}
EXPORT_SYMBOL_GPL(udp_bpf_update_proto);
