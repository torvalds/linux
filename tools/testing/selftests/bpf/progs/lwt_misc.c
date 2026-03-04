// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("lwt_xmit")
__success __retval(0)
int test_missing_dst(struct __sk_buff *skb)
{
	struct iphdr iph;

	__builtin_memset(&iph, 0, sizeof(struct iphdr));
	iph.ihl = 5;
	iph.version = 4;

	bpf_lwt_push_encap(skb, BPF_LWT_ENCAP_IP, &iph, sizeof(struct iphdr));

	return 0;
}

char _license[] SEC("license") = "GPL";
