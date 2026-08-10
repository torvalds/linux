// SPDX-License-Identifier: GPL-2.0

#include "bpf_tracing_net.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("struct_ops")
void BPF_PROG(untrusted_btf_write_init, struct sock *sk)
{
	struct tcp_sock *tp;
	int v = 1;
	void *p;

	p = bpf_rdonly_cast(&v, 0);
	tp = bpf_rdonly_cast(p, bpf_core_type_id_kernel(struct tcp_sock));
	tp->snd_cwnd = 1;
}

SEC(".struct_ops")
struct tcp_congestion_ops untrusted_btf_write = {
	.init = (void *)untrusted_btf_write_init,
	.name = "bpf_ro_btf",
};
