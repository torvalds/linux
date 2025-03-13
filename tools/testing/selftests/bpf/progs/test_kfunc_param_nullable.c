// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

SEC("tc")
int kfunc_dynptr_nullable_test1(struct __sk_buff *skb)
{
	struct bpf_dynptr data;

	bpf_dynptr_from_skb(skb, 0, &data);
	bpf_kfunc_dynptr_test(&data, NULL);

	return 0;
}

SEC("tc")
int kfunc_dynptr_nullable_test2(struct __sk_buff *skb)
{
	struct bpf_dynptr data;

	bpf_dynptr_from_skb(skb, 0, &data);
	bpf_kfunc_dynptr_test(&data, &data);

	return 0;
}

SEC("tc")
__failure __msg("expected pointer to stack or const struct bpf_dynptr")
int kfunc_dynptr_nullable_test3(struct __sk_buff *skb)
{
	struct bpf_dynptr data;

	bpf_dynptr_from_skb(skb, 0, &data);
	bpf_kfunc_dynptr_test(NULL, &data);

	return 0;
}

char _license[] SEC("license") = "GPL";
