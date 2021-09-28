// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* Dummy prog to test TC-BPF API */

SEC("tc")
int cls(struct __sk_buff *skb)
{
	return 0;
}
