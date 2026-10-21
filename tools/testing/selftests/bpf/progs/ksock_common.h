/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Isovalent */

#ifndef _KSOCK_COMMON_H
#define _KSOCK_COMMON_H

#include "errno.h"

#define SOCK_DGRAM	2
#define IPPROTO_UDP	17

struct bpf_ksock *bpf_ksock_create(const struct bpf_ksock_create_opts *opts,
				   u32 opts__sz, int *err__uninit) __ksym;
int bpf_ksock_connect(struct bpf_ksock *ks, const union bpf_ksock_addr *addr,
		      u32 addr__sz) __ksym;
struct bpf_ksock *bpf_ksock_acquire(struct bpf_ksock *ks) __ksym;
void bpf_ksock_release(struct bpf_ksock *ks) __ksym;
int bpf_ksock_send(struct bpf_ksock *ks, const void *data, u32 data__sz) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

struct __ksock_ctx_value {
	struct bpf_ksock __kptr * ctx;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct __ksock_ctx_value);
	__uint(max_entries, 1);
} __ksock_ctx_map SEC(".maps");

static inline struct __ksock_ctx_value *ksock_ctx_value_lookup(void)
{
	u32 key = 0;

	return bpf_map_lookup_elem(&__ksock_ctx_map, &key);
}

static inline struct bpf_ksock *ksock_ctx_get(void)
{
	struct __ksock_ctx_value *v;
	struct bpf_ksock *ks = NULL, *tmp;

	v = ksock_ctx_value_lookup();
	if (!v)
		return NULL;

	bpf_rcu_read_lock();
	tmp = v->ctx;
	if (tmp)
		ks = bpf_ksock_acquire(tmp);
	bpf_rcu_read_unlock();

	return ks;
}

static inline int ksock_ctx_insert(struct bpf_ksock *ctx)
{
	struct __ksock_ctx_value *v;
	struct bpf_ksock *old;

	v = ksock_ctx_value_lookup();
	if (!v) {
		bpf_ksock_release(ctx);
		return -ENOENT;
	}

	old = bpf_kptr_xchg(&v->ctx, ctx);
	if (old) {
		bpf_ksock_release(old);
		return -EEXIST;
	}

	return 0;
}

#endif /* _KSOCK_COMMON_H */
