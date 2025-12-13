// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
} pinmap1 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u32);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
} pinmap2 SEC(".maps");
