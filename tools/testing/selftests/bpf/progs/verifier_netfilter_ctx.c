// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"

#include "bpf_misc.h"

#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

SEC("netfilter")
__description("netfilter invalid context access, size too short")
__failure __msg("invalid bpf_context access")
__naked void with_invalid_ctx_access_test1(void)
{
	asm volatile ("					\
	r2 = *(u8*)(r1 + %[__bpf_nf_ctx_state]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__bpf_nf_ctx_state, offsetof(struct bpf_nf_ctx, state))
	: __clobber_all);
}

SEC("netfilter")
__description("netfilter invalid context access, size too short")
__failure __msg("invalid bpf_context access")
__naked void with_invalid_ctx_access_test2(void)
{
	asm volatile ("					\
	r2 = *(u16*)(r1 + %[__bpf_nf_ctx_skb]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__bpf_nf_ctx_skb, offsetof(struct bpf_nf_ctx, skb))
	: __clobber_all);
}

SEC("netfilter")
__description("netfilter invalid context access, past end of ctx")
__failure __msg("invalid bpf_context access")
__naked void with_invalid_ctx_access_test3(void)
{
	asm volatile ("					\
	r2 = *(u64*)(r1 + %[__bpf_nf_ctx_size]);	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__bpf_nf_ctx_size, sizeof(struct bpf_nf_ctx))
	: __clobber_all);
}

SEC("netfilter")
__description("netfilter invalid context, write")
__failure __msg("invalid bpf_context access")
__naked void with_invalid_ctx_access_test4(void)
{
	asm volatile ("					\
	r2 = r1;					\
	*(u64*)(r2 + 0) = r1;				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_const(__bpf_nf_ctx_skb, offsetof(struct bpf_nf_ctx, skb))
	: __clobber_all);
}

#define NF_DROP 0
#define NF_ACCEPT 1

SEC("netfilter")
__description("netfilter valid context read and invalid write")
__failure __msg("only read is supported")
int with_invalid_ctx_access_test5(struct bpf_nf_ctx *ctx)
{
	struct nf_hook_state *state = (void *)ctx->state;

	state->sk = NULL;
	return NF_ACCEPT;
}

extern int bpf_dynptr_from_skb(struct sk_buff *skb, __u64 flags,
                               struct bpf_dynptr *ptr__uninit) __ksym;
extern void *bpf_dynptr_slice(const struct bpf_dynptr *ptr, uint32_t offset,
                                   void *buffer, uint32_t buffer__sz) __ksym;

SEC("netfilter")
__description("netfilter test prog with skb and state read access")
__success __failure_unpriv
__retval(0)
int with_valid_ctx_access_test6(struct bpf_nf_ctx *ctx)
{
	const struct nf_hook_state *state = ctx->state;
	struct sk_buff *skb = ctx->skb;
	const struct iphdr *iph;
	const struct tcphdr *th;
	u8 buffer_iph[20] = {};
	u8 buffer_th[40] = {};
	struct bpf_dynptr ptr;
	uint8_t ihl;

	if (skb->len <= 20 || bpf_dynptr_from_skb(skb, 0, &ptr))
		return NF_ACCEPT;

	iph = bpf_dynptr_slice(&ptr, 0, buffer_iph, sizeof(buffer_iph));
	if (!iph)
		return NF_ACCEPT;

	if (state->pf != 2)
		return NF_ACCEPT;

	ihl = iph->ihl << 2;

	th = bpf_dynptr_slice(&ptr, ihl, buffer_th, sizeof(buffer_th));
	if (!th)
		return NF_ACCEPT;

	return th->dest == bpf_htons(22) ? NF_ACCEPT : NF_DROP;
}

char _license[] SEC("license") = "GPL";
