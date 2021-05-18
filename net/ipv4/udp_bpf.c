// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Cloudflare Ltd https://cloudflare.com */

#include <linux/skmsg.h>
#include <net/sock.h>
#include <net/udp.h>
#include <net/inet_common.h>

#include "udp_impl.h"

static struct proto *udpv6_prot_saved __read_mostly;

static int sk_udp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			  int noblock, int flags, int *addr_len)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		return udpv6_prot_saved->recvmsg(sk, msg, len, noblock, flags,
						 addr_len);
#endif
	return udp_prot.recvmsg(sk, msg, len, noblock, flags, addr_len);
}

static int udp_bpf_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			   int nonblock, int flags, int *addr_len)
{
	struct sk_psock *psock;
	int copied, ret;

	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);

	psock = sk_psock_get(sk);
	if (unlikely(!psock))
		return sk_udp_recvmsg(sk, msg, len, nonblock, flags, addr_len);

	lock_sock(sk);
	if (sk_psock_queue_empty(psock)) {
		ret = sk_udp_recvmsg(sk, msg, len, nonblock, flags, addr_len);
		goto out;
	}

msg_bytes_ready:
	copied = sk_msg_recvmsg(sk, psock, msg, len, flags);
	if (!copied) {
		int data, err = 0;
		long timeo;

		timeo = sock_rcvtimeo(sk, nonblock);
		data = sk_msg_wait_data(sk, psock, flags, timeo, &err);
		if (data) {
			if (!sk_psock_queue_empty(psock))
				goto msg_bytes_ready;
			ret = sk_udp_recvmsg(sk, msg, len, nonblock, flags, addr_len);
			goto out;
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
	prot->unhash = sock_map_unhash;
	prot->close  = sock_map_close;
	prot->recvmsg = udp_bpf_recvmsg;
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
core_initcall(udp_bpf_v4_build_proto);

int udp_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore)
{
	int family = sk->sk_family == AF_INET ? UDP_BPF_IPV4 : UDP_BPF_IPV6;

	if (restore) {
		sk->sk_write_space = psock->saved_write_space;
		WRITE_ONCE(sk->sk_prot, psock->sk_proto);
		return 0;
	}

	if (sk->sk_family == AF_INET6)
		udp_bpf_check_v6_needs_rebuild(psock->sk_proto);

	WRITE_ONCE(sk->sk_prot, &udp_bpf_prots[family]);
	return 0;
}
EXPORT_SYMBOL_GPL(udp_bpf_update_proto);
