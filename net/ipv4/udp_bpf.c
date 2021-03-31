// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Cloudflare Ltd https://cloudflare.com */

#include <linux/skmsg.h>
#include <net/sock.h>
#include <net/udp.h>

enum {
	UDP_BPF_IPV4,
	UDP_BPF_IPV6,
	UDP_BPF_NUM_PROTS,
};

static struct proto *udpv6_prot_saved __read_mostly;
static DEFINE_SPINLOCK(udpv6_prot_lock);
static struct proto udp_bpf_prots[UDP_BPF_NUM_PROTS];

static void udp_bpf_rebuild_protos(struct proto *prot, const struct proto *base)
{
	*prot        = *base;
	prot->unhash = sock_map_unhash;
	prot->close  = sock_map_close;
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

int udp_bpf_update_proto(struct sock *sk, bool restore)
{
	int family = sk->sk_family == AF_INET ? UDP_BPF_IPV4 : UDP_BPF_IPV6;
	struct sk_psock *psock = sk_psock(sk);

	if (restore) {
		sk->sk_write_space = psock->saved_write_space;
		/* Pairs with lockless read in sk_clone_lock() */
		WRITE_ONCE(sk->sk_prot, psock->sk_proto);
		return 0;
	}

	if (sk->sk_family == AF_INET6)
		udp_bpf_check_v6_needs_rebuild(psock->sk_proto);

	/* Pairs with lockless read in sk_clone_lock() */
	WRITE_ONCE(sk->sk_prot, &udp_bpf_prots[family]);
	return 0;
}
EXPORT_SYMBOL_GPL(udp_bpf_update_proto);
