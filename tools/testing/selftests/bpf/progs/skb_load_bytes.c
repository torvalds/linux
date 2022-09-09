// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

__u32 load_offset = 0;
int test_result = 0;

SEC("tc")
int skb_process(struct __sk_buff *skb)
{
	char buf[16];

	test_result = bpf_skb_load_bytes(skb, load_offset, buf, 10);

	return 0;
}
