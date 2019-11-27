// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_trace_helpers.h"

struct sk_buff {
	unsigned int len;
};

__u64 test_result = 0;
BPF_TRACE_2("fexit/test_pkt_access", test_main,
	    struct sk_buff *, skb, int, ret)
{
	int len;

	__builtin_preserve_access_index(({
		len = skb->len;
	}));
	if (len != 74 || ret != 0)
		return 0;
	test_result = 1;
	return 0;
}

__u64 test_result_subprog1 = 0;
BPF_TRACE_2("fexit/test_pkt_access_subprog1", test_subprog1,
	    struct sk_buff *, skb, int, ret)
{
	int len;

	__builtin_preserve_access_index(({
		len = skb->len;
	}));
	if (len != 74 || ret != 148)
		return 0;
	test_result_subprog1 = 1;
	return 0;
}

/* Though test_pkt_access_subprog2() is defined in C as:
 * static __attribute__ ((noinline))
 * int test_pkt_access_subprog2(int val, volatile struct __sk_buff *skb)
 * {
 *     return skb->len * val;
 * }
 * llvm optimizations remove 'int val' argument and generate BPF assembly:
 *   r0 = *(u32 *)(r1 + 0)
 *   w0 <<= 1
 *   exit
 * In such case the verifier falls back to conservative and
 * tracing program can access arguments and return value as u64
 * instead of accurate types.
 */
struct args_subprog2 {
	__u64 args[5];
	__u64 ret;
};
__u64 test_result_subprog2 = 0;
SEC("fexit/test_pkt_access_subprog2")
int test_subprog2(struct args_subprog2 *ctx)
{
	struct sk_buff *skb = (void *)ctx->args[0];
	__u64 ret;
	int len;

	bpf_probe_read_kernel(&len, sizeof(len),
			      __builtin_preserve_access_index(&skb->len));

	ret = ctx->ret;
	/* bpf_prog_load() loads "test_pkt_access.o" with BPF_F_TEST_RND_HI32
	 * which randomizes upper 32 bits after BPF_ALU32 insns.
	 * Hence after 'w0 <<= 1' upper bits of $rax are random.
	 * That is expected and correct. Trim them.
	 */
	ret = (__u32) ret;
	if (len != 74 || ret != 148)
		return 0;
	test_result_subprog2 = 1;
	return 0;
}
char _license[] SEC("license") = "GPL";
