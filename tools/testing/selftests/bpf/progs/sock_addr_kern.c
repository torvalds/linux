// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

SEC("syscall")
int init_sock(struct init_sock_args *args)
{
	bpf_kfunc_init_sock(args);

	return 0;
}

SEC("syscall")
int close_sock(void *ctx)
{
	bpf_kfunc_close_sock();

	return 0;
}

SEC("syscall")
int kernel_connect(struct addr_args *args)
{
	return bpf_kfunc_call_kernel_connect(args);
}

SEC("syscall")
int kernel_bind(struct addr_args *args)
{
	return bpf_kfunc_call_kernel_bind(args);
}

SEC("syscall")
int kernel_listen(struct addr_args *args)
{
	return bpf_kfunc_call_kernel_listen();
}

SEC("syscall")
int kernel_sendmsg(struct sendmsg_args *args)
{
	return bpf_kfunc_call_kernel_sendmsg(args);
}

SEC("syscall")
int sock_sendmsg(struct sendmsg_args *args)
{
	return bpf_kfunc_call_sock_sendmsg(args);
}

SEC("syscall")
int kernel_getsockname(struct addr_args *args)
{
	return bpf_kfunc_call_kernel_getsockname(args);
}

SEC("syscall")
int kernel_getpeername(struct addr_args *args)
{
	return bpf_kfunc_call_kernel_getpeername(args);
}

char _license[] SEC("license") = "GPL";
