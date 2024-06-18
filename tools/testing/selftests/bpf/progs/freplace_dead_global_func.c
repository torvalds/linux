// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("freplace")
int freplace_prog(void)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
