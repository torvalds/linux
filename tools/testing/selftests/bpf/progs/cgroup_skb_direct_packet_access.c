// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

__u32 data_end;

SEC("cgroup_skb/ingress")
int direct_packet_access(struct __sk_buff *skb)
{
	data_end = skb->data_end;
	return 1;
}

char _license[] SEC("license") = "GPL";
