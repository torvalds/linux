// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

char _license[] SEC("license") = "GPL";

/* Keep the upper index bits: the inlined expansion must truncate them. */
static long (*bpf_get_func_arg_wide)(void *ctx, __u64 n, __u64 *value) =
	(void *)BPF_FUNC_get_func_arg;

/* Both traced functions are called with arguments 2 and 3. */
static __always_inline int check_wide_index(void *ctx)
{
	__u64 a = 0, z = 0xdeadbeefdeadbeefULL;
	__s64 err;
	int ok = 1;

	/* The index is truncated to u32, so these select arguments 0 and 1. */
	err = bpf_get_func_arg_wide(ctx, 1ULL << 32, &a);
	ok &= err == 0 && ((int)a == 2);
	err = bpf_get_func_arg_wide(ctx, (1ULL << 32) | 1, &a);
	ok &= err == 0 && a == 3;
	err = bpf_get_func_arg_wide(ctx, 1ULL << 59, &a);
	ok &= err == 0 && ((int)a == 2);
	err = bpf_get_func_arg_wide(ctx, (1ULL << 59) | 1, &a);
	ok &= err == 0 && a == 3;

	/* Out of range: -EINVAL and no store into the destination. */
	err = bpf_get_func_arg_wide(ctx, (1ULL << 32) | 2, &z);
	ok &= err == -EINVAL && z == 0xdeadbeefdeadbeefULL;
	err = bpf_get_func_arg_wide(ctx, (1ULL << 32) | 0xffffffffULL, &z);
	ok &= err == -EINVAL && z == 0xdeadbeefdeadbeefULL;
	err = bpf_get_func_arg_wide(ctx, (1ULL << 59) | 2, &z);
	ok &= err == -EINVAL && z == 0xdeadbeefdeadbeefULL;
	err = bpf_get_func_arg_wide(ctx, (1ULL << 59) | 0xffffffffULL, &z);
	ok &= err == -EINVAL && z == 0xdeadbeefdeadbeefULL;

	return ok;
}

__u64 test1_result = 0;
SEC("fentry/bpf_fentry_test1")
int BPF_PROG(test1)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, z = 0, ret = 0;
	__s64 err;

	test1_result = cnt == 1;

	/* valid arguments */
	err = bpf_get_func_arg(ctx, 0, &a);

	/* We need to cast access to traced function argument values with
	 * proper type cast, because trampoline uses type specific instruction
	 * to save it, like for 'int a' with 32-bit mov like:
	 *
	 *   mov %edi,-0x8(%rbp)
	 *
	 * so the upper 4 bytes are not zeroed.
	 */
	test1_result &= err == 0 && ((int) a == 1);

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 1, &z);
	test1_result &= err == -EINVAL;

	/* return value fails in fentry */
	err = bpf_get_func_ret(ctx, &ret);
	test1_result &= err == -EOPNOTSUPP;
	return 0;
}

__u64 test2_result = 0;
SEC("fexit/bpf_fentry_test2")
int BPF_PROG(test2)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, b = 0, z = 0, ret = 0;
	__s64 err;

	test2_result = cnt == 2;

	/* valid arguments */
	err = bpf_get_func_arg(ctx, 0, &a);
	test2_result &= err == 0 && (int) a == 2;

	err = bpf_get_func_arg(ctx, 1, &b);
	test2_result &= err == 0 && b == 3;

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 2, &z);
	test2_result &= err == -EINVAL;

	/* return value */
	err = bpf_get_func_ret(ctx, &ret);
	test2_result &= err == 0 && ret == 5;
	test2_result &= check_wide_index(ctx);
	return 0;
}

__u64 test3_result = 0;
SEC("fmod_ret/bpf_modify_return_test")
int BPF_PROG(fmod_ret_test, int _a, int *_b, int _ret)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, b = 0, z = 0, ret = 0;
	__s64 err;

	test3_result = cnt == 2;

	/* valid arguments */
	err = bpf_get_func_arg(ctx, 0, &a);
	test3_result &= err == 0 && ((int) a == 1);

	err = bpf_get_func_arg(ctx, 1, &b);
	test3_result &= err == 0 && ((int *) b == _b);

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 2, &z);
	test3_result &= err == -EINVAL;

	/* return value */
	err = bpf_get_func_ret(ctx, &ret);
	test3_result &= err == 0 && ret == 0;

	/* change return value, it's checked in fexit_test program */
	return 1234;
}

__u64 test4_result = 0;
SEC("fexit/bpf_modify_return_test")
int BPF_PROG(fexit_test, int _a, int *_b, int _ret)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, b = 0, z = 0, ret = 0;
	__s64 err;

	test4_result = cnt == 2;

	/* valid arguments */
	err = bpf_get_func_arg(ctx, 0, &a);
	test4_result &= err == 0 && ((int) a == 1);

	err = bpf_get_func_arg(ctx, 1, &b);
	test4_result &= err == 0 && ((int *) b == _b);

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 2, &z);
	test4_result &= err == -EINVAL;

	/* return value */
	err = bpf_get_func_ret(ctx, &ret);
	test4_result &= err == 0 && ret == 1234;
	return 0;
}

__u64 test5_result = 0;
SEC("tp_btf/bpf_testmod_fentry_test1_tp")
int BPF_PROG(tp_test1)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, z = 0;
	__s64 err;

	test5_result = cnt == 1;

	err = bpf_get_func_arg(ctx, 0, &a);
	test5_result &= err == 0 && ((int) a == 1);

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 1, &z);
	test5_result &= err == -EINVAL;

	return 0;
}

__u64 test6_result = 0;
SEC("tp_btf/bpf_testmod_fentry_test2_tp")
int BPF_PROG(tp_test2)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, b = 0, z = 0;
	__s64 err;

	test6_result = cnt == 2;

	/* valid arguments */
	err = bpf_get_func_arg(ctx, 0, &a);
	test6_result &= err == 0 && (int) a == 2;

	err = bpf_get_func_arg(ctx, 1, &b);
	test6_result &= err == 0 && b == 3;

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 2, &z);
	test6_result &= err == -EINVAL;
	test6_result &= check_wide_index(ctx);

	return 0;
}
