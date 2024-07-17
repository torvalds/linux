// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf_sockopt_helpers.h>
#include "bpf_misc.h"

SEC("cgroup/recvmsg4")
__success
int recvmsg4_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/recvmsg4")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int recvmsg4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/recvmsg6")
__success
int recvmsg6_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/recvmsg6")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int recvmsg6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/recvmsg_unix")
__success
int recvmsg_unix_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/recvmsg_unix")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int recvmsg_unix_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/sendmsg4")
__success
int sendmsg4_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/sendmsg4")
__success
int sendmsg4_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/sendmsg4")
__failure __msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int sendmsg4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/sendmsg6")
__success
int sendmsg6_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/sendmsg6")
__success
int sendmsg6_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/sendmsg6")
__failure __msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int sendmsg6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/sendmsg_unix")
__success
int sendmsg_unix_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/sendmsg_unix")
__success
int sendmsg_unix_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/sendmsg_unix")
__failure __msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int sendmsg_unix_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/getpeername4")
__success
int getpeername4_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/getpeername4")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int getpeername4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/getpeername6")
__success
int getpeername6_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/getpeername6")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int getpeername6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/getpeername_unix")
__success
int getpeername_unix_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/getpeername_unix")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int getpeername_unix_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/getsockname4")
__success
int getsockname4_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/getsockname4")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int getsockname4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/getsockname6")
__success
int getsockname6_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/getsockname6")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int getsockname6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/getsockname_unix")
__success
int getsockname_unix_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/getsockname_unix")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int getsockname_unix_unix_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/bind4")
__success
int bind4_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/bind4")
__success
int bind4_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/bind4")
__success
int bind4_good_return_code_2(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/bind4")
__success
int bind4_good_return_code_3(struct bpf_sock_addr *ctx)
{
	return 3;
}

SEC("cgroup/bind4")
__failure __msg("At program exit the register R0 has smin=4 smax=4 should have been in [0, 3]")
int bind4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 4;
}

SEC("cgroup/bind6")
__success
int bind6_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/bind6")
__success
int bind6_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/bind6")
__success
int bind6_good_return_code_2(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/bind6")
__success
int bind6_good_return_code_3(struct bpf_sock_addr *ctx)
{
	return 3;
}

SEC("cgroup/bind6")
__failure __msg("At program exit the register R0 has smin=4 smax=4 should have been in [0, 3]")
int bind6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 4;
}

SEC("cgroup/connect4")
__success
int connect4_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/connect4")
__success
int connect4_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/connect4")
__failure __msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int connect4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/connect6")
__success
int connect6_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/connect6")
__success
int connect6_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/connect6")
__failure __msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int connect6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 2;
}

SEC("cgroup/connect_unix")
__success
int connect_unix_good_return_code_0(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/connect_unix")
__success
int connect_unix_good_return_code_1(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/connect_unix")
__failure __msg("At program exit the register R0 has smin=2 smax=2 should have been in [0, 1]")
int connect_unix_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 2;
}

char _license[] SEC("license") = "GPL";
