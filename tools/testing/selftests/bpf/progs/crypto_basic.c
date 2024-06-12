// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"
#include "crypto_common.h"

int status;
SEC("syscall")
int crypto_release(void *ctx)
{
	struct bpf_crypto_params params = {
		.type = "skcipher",
		.algo = "ecb(aes)",
		.key_len = 16,
	};

	struct bpf_crypto_ctx *cctx;
	int err = 0;

	status = 0;

	cctx = bpf_crypto_ctx_create(&params, sizeof(params), &err);

	if (!cctx) {
		status = err;
		return 0;
	}

	bpf_crypto_ctx_release(cctx);

	return 0;
}

SEC("syscall")
__failure __msg("Unreleased reference")
int crypto_acquire(void *ctx)
{
	struct bpf_crypto_params params = {
		.type = "skcipher",
		.algo = "ecb(aes)",
		.key_len = 16,
	};
	struct bpf_crypto_ctx *cctx;
	int err = 0;

	status = 0;

	cctx = bpf_crypto_ctx_create(&params, sizeof(params), &err);

	if (!cctx) {
		status = err;
		return 0;
	}

	cctx = bpf_crypto_ctx_acquire(cctx);
	if (!cctx)
		return -EINVAL;

	bpf_crypto_ctx_release(cctx);

	return 0;
}

char __license[] SEC("license") = "GPL";
