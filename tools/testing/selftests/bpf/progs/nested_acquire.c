// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../bpf_testmod/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

SEC("tp_btf/tcp_probe")
__success
int BPF_PROG(test_nested_acquire_nonzero, struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *ptr;

	ptr = bpf_kfunc_nested_acquire_nonzero_offset_test(&sk->sk_write_queue);

	bpf_kfunc_nested_release_test(ptr);
	return 0;
}

SEC("tp_btf/tcp_probe")
__success
int BPF_PROG(test_nested_acquire_zero, struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *ptr;

	ptr = bpf_kfunc_nested_acquire_zero_offset_test(&sk->__sk_common);

	bpf_kfunc_nested_release_test(ptr);
	return 0;
}
