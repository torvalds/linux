// SPDX-License-Identifier: GPL-2.0
/* Copyright 2025 Google LLC */

#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

extern int tcp_memory_per_cpu_fw_alloc __ksym;
extern int udp_memory_per_cpu_fw_alloc __ksym;

int nr_cpus;
bool tcp_activated, udp_activated;
long tcp_memory_allocated, udp_memory_allocated;

struct sk_prot {
	long *memory_allocated;
	int *memory_per_cpu_fw_alloc;
};

static int drain_memory_per_cpu_fw_alloc(__u32 i, struct sk_prot *sk_prot_ctx)
{
	int *memory_per_cpu_fw_alloc;

	memory_per_cpu_fw_alloc = bpf_per_cpu_ptr(sk_prot_ctx->memory_per_cpu_fw_alloc, i);
	if (memory_per_cpu_fw_alloc)
		*sk_prot_ctx->memory_allocated += *memory_per_cpu_fw_alloc;

	return 0;
}

static long get_memory_allocated(struct sock *_sk, int *memory_per_cpu_fw_alloc)
{
	struct sock *sk = bpf_core_cast(_sk, struct sock);
	struct sk_prot sk_prot_ctx;
	long memory_allocated;

	/* net_aligned_data.{tcp,udp}_memory_allocated was not available. */
	memory_allocated = sk->__sk_common.skc_prot->memory_allocated->counter;

	sk_prot_ctx.memory_allocated = &memory_allocated;
	sk_prot_ctx.memory_per_cpu_fw_alloc = memory_per_cpu_fw_alloc;

	bpf_loop(nr_cpus, drain_memory_per_cpu_fw_alloc, &sk_prot_ctx, 0);

	return memory_allocated;
}

static void fentry_init_sock(struct sock *sk, bool *activated,
			     long *memory_allocated, int *memory_per_cpu_fw_alloc)
{
	if (!*activated)
		return;

	*memory_allocated = get_memory_allocated(sk, memory_per_cpu_fw_alloc);
	*activated = false;
}

SEC("fentry/tcp_init_sock")
int BPF_PROG(fentry_tcp_init_sock, struct sock *sk)
{
	fentry_init_sock(sk, &tcp_activated,
			 &tcp_memory_allocated, &tcp_memory_per_cpu_fw_alloc);
	return 0;
}

SEC("fentry/udp_init_sock")
int BPF_PROG(fentry_udp_init_sock, struct sock *sk)
{
	fentry_init_sock(sk, &udp_activated,
			 &udp_memory_allocated, &udp_memory_per_cpu_fw_alloc);
	return 0;
}

SEC("cgroup/sock_create")
int sock_create(struct bpf_sock *ctx)
{
	int err, val = 1;

	err = bpf_setsockopt(ctx, SOL_SOCKET, SK_BPF_BYPASS_PROT_MEM,
			     &val, sizeof(val));
	if (err)
		goto err;

	val = 0;

	err = bpf_getsockopt(ctx, SOL_SOCKET, SK_BPF_BYPASS_PROT_MEM,
			     &val, sizeof(val));
	if (err)
		goto err;

	if (val != 1) {
		err = -EINVAL;
		goto err;
	}

	return 1;

err:
	bpf_set_retval(err);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
