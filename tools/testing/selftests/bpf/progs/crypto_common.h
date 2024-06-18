/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#ifndef _CRYPTO_COMMON_H
#define _CRYPTO_COMMON_H

#include "errno.h"
#include <stdbool.h>

struct bpf_crypto_ctx *bpf_crypto_ctx_create(const struct bpf_crypto_params *params,
					     u32 params__sz, int *err) __ksym;
struct bpf_crypto_ctx *bpf_crypto_ctx_acquire(struct bpf_crypto_ctx *ctx) __ksym;
void bpf_crypto_ctx_release(struct bpf_crypto_ctx *ctx) __ksym;
int bpf_crypto_encrypt(struct bpf_crypto_ctx *ctx, const struct bpf_dynptr *src,
		       const struct bpf_dynptr *dst, const struct bpf_dynptr *iv) __ksym;
int bpf_crypto_decrypt(struct bpf_crypto_ctx *ctx, const struct bpf_dynptr *src,
		       const struct bpf_dynptr *dst, const struct bpf_dynptr *iv) __ksym;

struct __crypto_ctx_value {
	struct bpf_crypto_ctx __kptr * ctx;
};

struct array_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct __crypto_ctx_value);
	__uint(max_entries, 1);
} __crypto_ctx_map SEC(".maps");

static inline struct __crypto_ctx_value *crypto_ctx_value_lookup(void)
{
	u32 key = 0;

	return bpf_map_lookup_elem(&__crypto_ctx_map, &key);
}

static inline int crypto_ctx_insert(struct bpf_crypto_ctx *ctx)
{
	struct __crypto_ctx_value local, *v;
	struct bpf_crypto_ctx *old;
	u32 key = 0;
	int err;

	local.ctx = NULL;
	err = bpf_map_update_elem(&__crypto_ctx_map, &key, &local, 0);
	if (err) {
		bpf_crypto_ctx_release(ctx);
		return err;
	}

	v = bpf_map_lookup_elem(&__crypto_ctx_map, &key);
	if (!v) {
		bpf_crypto_ctx_release(ctx);
		return -ENOENT;
	}

	old = bpf_kptr_xchg(&v->ctx, ctx);
	if (old) {
		bpf_crypto_ctx_release(old);
		return -EEXIST;
	}

	return 0;
}

#endif /* _CRYPTO_COMMON_H */
