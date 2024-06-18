// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"

SEC("?fentry")
int pfentry(void *ctx)
{
	return 0;
}

SEC("?fentry")
int throwing_fentry(void *ctx)
{
	bpf_throw(0);
	return 0;
}

__noinline int exception_cb(u64 cookie)
{
	return cookie + 64;
}

SEC("?freplace")
int extension(struct __sk_buff *ctx)
{
	return 0;
}

SEC("?freplace")
__exception_cb(exception_cb)
int throwing_exception_cb_extension(u64 cookie)
{
	bpf_throw(32);
	return 0;
}

SEC("?freplace")
__exception_cb(exception_cb)
int throwing_extension(struct __sk_buff *ctx)
{
	bpf_throw(64);
	return 0;
}

SEC("?fexit")
int pfexit(void *ctx)
{
	return 0;
}

SEC("?fexit")
int throwing_fexit(void *ctx)
{
	bpf_throw(0);
	return 0;
}

SEC("?fmod_ret")
int pfmod_ret(void *ctx)
{
	return 0;
}

SEC("?fmod_ret")
int throwing_fmod_ret(void *ctx)
{
	bpf_throw(0);
	return 0;
}

char _license[] SEC("license") = "GPL";
