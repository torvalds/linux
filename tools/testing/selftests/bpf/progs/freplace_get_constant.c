// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

volatile __u64 test_get_constant = 0;
SEC("freplace/get_constant")
int security_new_get_constant(long val)
{
	if (val != 123)
		return 0;
	test_get_constant = 1;
	return test_get_constant; /* original get_constant() returns val - 122 */
}
char _license[] SEC("license") = "GPL";
