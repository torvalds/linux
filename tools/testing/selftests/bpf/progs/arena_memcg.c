// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_arena_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	/*
	 * Number of pages. Must cover ARENA_FAULT on the smallest page size
	 * (64M/4K = 16384) yet stay under the 4G arena limit on 64K pages
	 * (50000*64K = 3.2G).
	 */
	__uint(max_entries, 50000);
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, 0x1ull << 32); /* start of mmap() region */
#else
	__ulong(map_extra, 0x1ull << 44); /* start of mmap() region */
#endif
} arena SEC(".maps");

char _license[] SEC("license") = "GPL";
