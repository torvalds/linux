/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Facebook */
#ifndef _BPF_SK_STORAGE_H
#define _BPF_SK_STORAGE_H

struct sock;

void bpf_sk_storage_free(struct sock *sk);

extern const struct bpf_func_proto bpf_sk_storage_get_proto;
extern const struct bpf_func_proto bpf_sk_storage_delete_proto;

#ifdef CONFIG_BPF_SYSCALL
int bpf_sk_storage_clone(const struct sock *sk, struct sock *newsk);
#else
static inline int bpf_sk_storage_clone(const struct sock *sk,
				       struct sock *newsk)
{
	return 0;
}
#endif

#endif /* _BPF_SK_STORAGE_H */
