/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Facebook */
#ifndef _BPF_SK_STORAGE_H
#define _BPF_SK_STORAGE_H

#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <net/sock.h>
#include <uapi/linux/sock_diag.h>
#include <uapi/linux/btf.h>

struct sock;

void bpf_sk_storage_free(struct sock *sk);

extern const struct bpf_func_proto bpf_sk_storage_get_proto;
extern const struct bpf_func_proto bpf_sk_storage_delete_proto;

struct bpf_local_storage_elem;
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

/* Helper functions for bpf_local_storage */
int bpf_local_storage_map_alloc_check(union bpf_attr *attr);

struct bpf_local_storage_map *bpf_local_storage_map_alloc(union bpf_attr *attr);

struct bpf_local_storage_data *
bpf_local_storage_lookup(struct bpf_local_storage *local_storage,
			 struct bpf_local_storage_map *smap,
			 bool cacheit_lockit);

void bpf_local_storage_map_free(struct bpf_local_storage_map *smap);

int bpf_local_storage_map_check_btf(const struct bpf_map *map,
				    const struct btf *btf,
				    const struct btf_type *key_type,
				    const struct btf_type *value_type);

void bpf_selem_link_storage_nolock(struct bpf_local_storage *local_storage,
				   struct bpf_local_storage_elem *selem);

bool bpf_selem_unlink_storage_nolock(struct bpf_local_storage *local_storage,
				     struct bpf_local_storage_elem *selem,
				     bool uncharge_omem);

void bpf_selem_unlink(struct bpf_local_storage_elem *selem);

void bpf_selem_link_map(struct bpf_local_storage_map *smap,
			struct bpf_local_storage_elem *selem);

void bpf_selem_unlink_map(struct bpf_local_storage_elem *selem);

struct bpf_local_storage_elem *
bpf_selem_alloc(struct bpf_local_storage_map *smap, void *owner, void *value,
		bool charge_mem);

int
bpf_local_storage_alloc(void *owner,
			struct bpf_local_storage_map *smap,
			struct bpf_local_storage_elem *first_selem);

struct bpf_local_storage_data *
bpf_local_storage_update(void *owner, struct bpf_local_storage_map *smap,
			 void *value, u64 map_flags);

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
