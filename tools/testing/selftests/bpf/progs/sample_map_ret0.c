/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct bpf_map_def SEC("maps") htab = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(__u32),
	.value_size = sizeof(long),
	.max_entries = 2,
};

struct bpf_map_def SEC("maps") array = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(long),
	.max_entries = 2,
};

/* Sample program which should always load for testing control paths. */
SEC(".text") int func()
{
	__u64 key64 = 0;
	__u32 key = 0;
	long *value;

	value = bpf_map_lookup_elem(&htab, &key);
	if (!value)
		return 1;
	value = bpf_map_lookup_elem(&array, &key64);
	if (!value)
		return 1;

	return 0;
}
