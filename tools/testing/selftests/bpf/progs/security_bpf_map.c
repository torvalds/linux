// SPDX-License-Identifier: GPL-2.0-only

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

#define EPERM 1 /* Operation not permitted */

/* From include/linux/mm.h. */
#define FMODE_WRITE	0x2

struct map;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 1);
} prot_status_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 3);
} prot_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 3);
} not_prot_map SEC(".maps");

SEC("fmod_ret/security_bpf_map")
int BPF_PROG(fmod_bpf_map, struct bpf_map *map, int fmode)
{
	__u32 key = 0;
	__u32 *status_ptr = bpf_map_lookup_elem(&prot_status_map, &key);

	if (!status_ptr || !*status_ptr)
		return 0;

	if (map == &prot_map) {
		/* Allow read-only access */
		if (fmode & FMODE_WRITE)
			return -EPERM;
	}

	return 0;
}

/*
 * This program keeps references to maps. This is needed to prevent
 * optimizing them out.
 */
SEC("fentry/bpf_fentry_test1")
int BPF_PROG(fentry_dummy1, int a)
{
	__u32 key = 0;
	__u32 val1 = a;
	__u32 val2 = a + 1;

	bpf_map_update_elem(&prot_map, &key, &val1, BPF_ANY);
	bpf_map_update_elem(&not_prot_map, &key, &val2, BPF_ANY);
	return 0;
}
