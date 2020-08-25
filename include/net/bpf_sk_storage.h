/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Facebook */
#ifndef _BPF_SK_STORAGE_H
#define _BPF_SK_STORAGE_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct sock;

void bpf_sk_storage_free(struct sock *sk);

extern const struct bpf_func_proto bpf_sk_storage_get_proto;
extern const struct bpf_func_proto bpf_sk_storage_delete_proto;

struct bpf_sk_storage_diag;
struct sk_buff;
struct nlattr;
struct sock;

#define BPF_LOCAL_STORAGE_CACHE_SIZE	16

struct bpf_local_storage_cache {
	spinlock_t idx_lock;
	u64 idx_usage_counts[BPF_LOCAL_STORAGE_CACHE_SIZE];
};

#define DEFINE_BPF_STORAGE_CACHE(name)				\
static struct bpf_local_storage_cache name = {			\
	.idx_lock = __SPIN_LOCK_UNLOCKED(name.idx_lock),	\
}

u16 bpf_local_storage_cache_idx_get(struct bpf_local_storage_cache *cache);
void bpf_local_storage_cache_idx_free(struct bpf_local_storage_cache *cache,
				      u16 idx);

#ifdef CONFIG_BPF_SYSCALL
int bpf_sk_storage_clone(const struct sock *sk, struct sock *newsk);
struct bpf_sk_storage_diag *
bpf_sk_storage_diag_alloc(const struct nlattr *nla_stgs);
void bpf_sk_storage_diag_free(struct bpf_sk_storage_diag *diag);
int bpf_sk_storage_diag_put(struct bpf_sk_storage_diag *diag,
			    struct sock *sk, struct sk_buff *skb,
			    int stg_array_type,
			    unsigned int *res_diag_size);
#else
static inline int bpf_sk_storage_clone(const struct sock *sk,
				       struct sock *newsk)
{
	return 0;
}
static inline struct bpf_sk_storage_diag *
bpf_sk_storage_diag_alloc(const struct nlattr *nla)
{
	return NULL;
}
static inline void bpf_sk_storage_diag_free(struct bpf_sk_storage_diag *diag)
{
}
static inline int bpf_sk_storage_diag_put(struct bpf_sk_storage_diag *diag,
					  struct sock *sk, struct sk_buff *skb,
					  int stg_array_type,
					  unsigned int *res_diag_size)
{
	return 0;
}
#endif

#endif /* _BPF_SK_STORAGE_H */
