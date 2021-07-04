// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Cong Wang <cong.wang@bytedance.com> */

#include <linux/skmsg.h>
#include <linux/bpf.h>
#include <net/sock.h>
#include <net/af_unix.h>

static struct proto *unix_prot_saved __read_mostly;
static DEFINE_SPINLOCK(unix_prot_lock);
static struct proto unix_bpf_prot;

static void unix_bpf_rebuild_protos(struct proto *prot, const struct proto *base)
{
	*prot        = *base;
	prot->close  = sock_map_close;
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
