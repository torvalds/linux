// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_arena_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1000); /* number of pages */
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, 0x1ull << 32); /* start of mmap() region */
#else
	__ulong(map_extra, 0x1ull << 44); /* start of mmap() region */
#endif
} arena SEC(".maps");

void __arena *ptr;
int alloc_cnt;		/* in:  pages to allocate */
long free_byte_off;	/* in:  byte offset within ptr to start freeing */
int free_cnt;		/* in:  pages to free */

SEC("syscall")
int alloc(void *ctx)
{
	ptr = bpf_arena_alloc_pages(&arena, NULL, alloc_cnt, NUMA_NO_NODE, 0);
	/* Success/failure is checked from user space via skel->bss->ptr. */
	return 0;
}

SEC("syscall")
int free_pages(void *ctx)
{
	if (!ptr)
		return 1;
	bpf_arena_free_pages(&arena, (char __arena *)ptr + free_byte_off, free_cnt);
	return 0;
}

char _license[] SEC("license") = "GPL";
