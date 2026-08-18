// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

extern int bpf_kfunc_implicit_arg(int a) __weak __ksym;

char _license[] SEC("license") = "GPL";

/* Shared arg checks; reports arg count and aux, returns 1 on success. */
static __always_inline __u64
check_implicit_args(void *ctx, __u64 *arg_cnt, __u64 *aux_arg)
{
	__u64 a = 0, aux = 0, z = 0;
	__u64 result;
	__s64 err;

	*arg_cnt = bpf_get_func_arg_cnt(ctx);
	result = *arg_cnt == 2;

	err = bpf_get_func_arg(ctx, 0, &a);
	result &= err == 0 && (int)a == 5;

	err = bpf_get_func_arg(ctx, 1, &aux);
	*aux_arg = aux;
	result &= err == 0 && aux != 0;

	err = bpf_get_func_arg(ctx, 2, &z);
	result &= err == -EINVAL;

	return result;
}

__u64 fentry_result;
__u64 fentry_arg_cnt;
__u64 fentry_aux_arg;

SEC("fentry/bpf_kfunc_implicit_arg")
int BPF_PROG(trace_implicit_arg_fentry)
{
	__u64 ret = 0;
	__s64 err;

	fentry_result = check_implicit_args(ctx, &fentry_arg_cnt, &fentry_aux_arg);

	err = bpf_get_func_ret(ctx, &ret);
	fentry_result &= err == -EOPNOTSUPP;

	return 0;
}

__u64 fexit_result;
__u64 fexit_arg_cnt;
__u64 fexit_aux_arg;

SEC("fexit/bpf_kfunc_implicit_arg")
int BPF_PROG(trace_implicit_arg_fexit)
{
	__u64 ret = 0;
	__s64 err;

	fexit_result = check_implicit_args(ctx, &fexit_arg_cnt, &fexit_aux_arg);

	err = bpf_get_func_ret(ctx, &ret);
	fexit_result &= err == 0 && ret == 5;

	return 0;
}

SEC("syscall")
int trigger_implicit_arg(void *ctx)
{
	return bpf_kfunc_implicit_arg(5);
}
