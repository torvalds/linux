// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"
#include "crypto_common.h"

const volatile unsigned int len = 16;
/*
 * cipher[] and key[] are 8-byte aligned and 'params' is kept off the stack to
 * work around an LLVM code generation bug. clang lowers the memcpy() of these
 * byte-aligned globals into a per-byte load/store sequence staged on the stack,
 * and additionally materializes the on-stack 'struct bpf_crypto_params' twice.
 * Both blow the 512-byte BPF stack limit. Aligning the sources lets clang copy
 * word-wise, and a global 'params' removes the large object from the stack.
 */
char cipher[128] __attribute__((aligned(8))) = {};
u32 key_len, authsize;
char dst[256] = {};
u8 key[256] __attribute__((aligned(8))) = {};
static struct bpf_crypto_params params;
long hits = 0;
int status;

SEC("syscall")
int crypto_setup(void *args)
{
	struct bpf_crypto_ctx *cctx;
	int err = 0;

	status = 0;

	if (!cipher[0] || !key_len || key_len > 256) {
		status = -EINVAL;
		return 0;
	}

	__builtin_memcpy(&params.type, "skcipher", sizeof("skcipher"));
	params.key_len = key_len;
	params.authsize = authsize;
	__builtin_memcpy(&params.algo, cipher, sizeof(cipher));
	__builtin_memcpy(&params.key, key, sizeof(key));
	cctx = bpf_crypto_ctx_create(&params, sizeof(params), &err);

	if (!cctx) {
		status = err;
		return 0;
	}

	err = crypto_ctx_insert(cctx);
	if (err && err != -EEXIST)
		status = err;

	return 0;
}

SEC("tc")
int crypto_encrypt(struct __sk_buff *skb)
{
	struct __crypto_ctx_value *v;
	struct bpf_crypto_ctx *ctx;
	struct bpf_dynptr psrc, pdst;

	v = crypto_ctx_value_lookup();
	if (!v) {
		status = -ENOENT;
		return 0;
	}

	ctx = v->ctx;
	if (!ctx) {
		status = -ENOENT;
		return 0;
	}

	bpf_dynptr_from_skb(skb, 0, &psrc);
	bpf_dynptr_from_mem(dst, len, 0, &pdst);

	status = bpf_crypto_encrypt(ctx, &psrc, &pdst, NULL);
	__sync_add_and_fetch(&hits, 1);

	return 0;
}

SEC("tc")
int crypto_decrypt(struct __sk_buff *skb)
{
	struct bpf_dynptr psrc, pdst;
	struct __crypto_ctx_value *v;
	struct bpf_crypto_ctx *ctx;

	v = crypto_ctx_value_lookup();
	if (!v)
		return -ENOENT;

	ctx = v->ctx;
	if (!ctx)
		return -ENOENT;

	bpf_dynptr_from_skb(skb, 0, &psrc);
	bpf_dynptr_from_mem(dst, len, 0, &pdst);

	status = bpf_crypto_decrypt(ctx, &psrc, &pdst, NULL);
	__sync_add_and_fetch(&hits, 1);

	return 0;
}

char __license[] SEC("license") = "GPL";
