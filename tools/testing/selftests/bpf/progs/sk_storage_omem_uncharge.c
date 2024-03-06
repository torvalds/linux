// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Facebook */
#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

void *local_storage_ptr = NULL;
void *sk_ptr = NULL;
int cookie_found = 0;
__u64 cookie = 0;
__u32 omem = 0;

void *bpf_rdonly_cast(void *, __u32) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sk_storage SEC(".maps");

SEC("fexit/bpf_local_storage_destroy")
int BPF_PROG(bpf_local_storage_destroy, struct bpf_local_storage *local_storage)
{
	struct sock *sk;

	if (local_storage_ptr != local_storage)
		return 0;

	sk = bpf_rdonly_cast(sk_ptr, bpf_core_type_id_kernel(struct sock));
	if (sk->sk_cookie.counter != cookie)
		return 0;

	cookie_found++;
	omem = sk->sk_omem_alloc.counter;
	local_storage_ptr = NULL;

	return 0;
}

SEC("fentry/inet6_sock_destruct")
int BPF_PROG(inet6_sock_destruct, struct sock *sk)
{
	int *value;

	if (!cookie || sk->sk_cookie.counter != cookie)
		return 0;

	value = bpf_sk_storage_get(&sk_storage, sk, 0, 0);
	if (value && *value == 0xdeadbeef) {
		cookie_found++;
		sk_ptr = sk;
		local_storage_ptr = sk->sk_bpf_storage;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
