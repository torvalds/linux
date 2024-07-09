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

unsigned char key[256] = {};
u16 udp_test_port = 7777;
u32 authsize, key_len;
char algo[128] = {};
char dst[16] = {};
int status;

static int skb_dynptr_validate(struct __sk_buff *skb, struct bpf_dynptr *psrc)
{
	struct ipv6hdr ip6h;
	struct udphdr udph;
	u32 offset;

	if (skb->protocol != __bpf_constant_htons(ETH_P_IPV6))
		return -1;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &ip6h, sizeof(ip6h)))
		return -1;

	if (ip6h.nexthdr != IPPROTO_UDP)
		return -1;

	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(ip6h), &udph, sizeof(udph)))
		return -1;

	if (udph.dest != __bpf_htons(udp_test_port))
		return -1;

	offset = ETH_HLEN + sizeof(ip6h) + sizeof(udph);
	if (skb->len < offset + 16)
		return -1;

	/* let's make sure that 16 bytes of payload are in the linear part of skb */
	bpf_skb_pull_data(skb, offset + 16);
	bpf_dynptr_from_skb(skb, 0, psrc);
	bpf_dynptr_adjust(psrc, offset, offset + 16);

	return 0;
}

SEC("syscall")
int skb_crypto_setup(void *ctx)
{
	struct bpf_crypto_params params = {
		.type = "skcipher",
		.key_len = key_len,
		.authsize = authsize,
	};
	struct bpf_crypto_ctx *cctx;
	int err = 0;

	status = 0;

	if (key_len > 256) {
		status = -EINVAL;
		return 0;
	}

	__builtin_memcpy(&params.algo, algo, sizeof(algo));
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
int decrypt_sanity(struct __sk_buff *skb)
{
	struct __crypto_ctx_value *v;
	struct bpf_crypto_ctx *ctx;
	struct bpf_dynptr psrc, pdst;
	int err;

	err = skb_dynptr_validate(skb, &psrc);
	if (err < 0) {
		status = err;
		return TC_ACT_SHOT;
	}

	v = crypto_ctx_value_lookup();
	if (!v) {
		status = -ENOENT;
		return TC_ACT_SHOT;
	}

	ctx = v->ctx;
	if (!ctx) {
		status = -ENOENT;
		return TC_ACT_SHOT;
	}

	/* dst is a global variable to make testing part easier to check. In real
	 * production code, a percpu map should be used to store the result.
	 */
	bpf_dynptr_from_mem(dst, sizeof(dst), 0, &pdst);

	status = bpf_crypto_decrypt(ctx, &psrc, &pdst, NULL);

	return TC_ACT_SHOT;
}

SEC("tc")
int encrypt_sanity(struct __sk_buff *skb)
{
	struct __crypto_ctx_value *v;
	struct bpf_crypto_ctx *ctx;
	struct bpf_dynptr psrc, pdst;
	int err;

	status = 0;

	err = skb_dynptr_validate(skb, &psrc);
	if (err < 0) {
		status = err;
		return TC_ACT_SHOT;
	}

	v = crypto_ctx_value_lookup();
	if (!v) {
		status = -ENOENT;
		return TC_ACT_SHOT;
	}

	ctx = v->ctx;
	if (!ctx) {
		status = -ENOENT;
		return TC_ACT_SHOT;
	}

	/* dst is a global variable to make testing part easier to check. In real
	 * production code, a percpu map should be used to store the result.
	 */
	bpf_dynptr_from_mem(dst, sizeof(dst), 0, &pdst);

	status = bpf_crypto_encrypt(ctx, &psrc, &pdst, NULL);

	return TC_ACT_SHOT;
}

char __license[] SEC("license") = "GPL";
