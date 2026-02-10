// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

extern int bpf_kfunc_implicit_arg(int a) __weak __ksym;
extern int bpf_kfunc_implicit_arg_impl(int a, struct bpf_prog_aux *aux) __weak __ksym; /* illegal */
extern int bpf_kfunc_implicit_arg_legacy(int a, int b) __weak __ksym;
extern int bpf_kfunc_implicit_arg_legacy_impl(int a, int b, struct bpf_prog_aux *aux) __weak __ksym;

char _license[] SEC("license") = "GPL";

SEC("syscall")
__retval(5)
int test_kfunc_implicit_arg(void *ctx)
{
	return bpf_kfunc_implicit_arg(5);
}

SEC("syscall")
__failure __msg("cannot find address for kernel function bpf_kfunc_implicit_arg_impl")
int test_kfunc_implicit_arg_impl_illegal(void *ctx)
{
	return bpf_kfunc_implicit_arg_impl(5, NULL);
}

SEC("syscall")
__retval(7)
int test_kfunc_implicit_arg_legacy(void *ctx)
{
	return bpf_kfunc_implicit_arg_legacy(3, 4);
}

SEC("syscall")
__retval(11)
int test_kfunc_implicit_arg_legacy_impl(void *ctx)
{
	return bpf_kfunc_implicit_arg_legacy_impl(5, 6, NULL);
}
