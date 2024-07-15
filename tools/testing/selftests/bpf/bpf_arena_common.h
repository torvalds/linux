/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#pragma once

#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ((*(volatile typeof(x) *) &(x)) = (val))
#endif

#ifndef NUMA_NO_NODE
#define	NUMA_NO_NODE	(-1)
#endif

#ifndef arena_container_of
#define arena_container_of(ptr, type, member)			\
	({							\
		void __arena *__mptr = (void __arena *)(ptr);	\
		((type *)(__mptr - offsetof(type, member)));	\
	})
#endif

#ifdef __BPF__ /* when compiled as bpf program */

#ifndef PAGE_SIZE
#define PAGE_SIZE __PAGE_SIZE
/*
 * for older kernels try sizeof(struct genradix_node)
 * or flexible:
 * static inline long __bpf_page_size(void) {
 *   return bpf_core_enum_value(enum page_size_enum___l, __PAGE_SIZE___l) ?: sizeof(struct genradix_node);
 * }
 * but generated code is not great.
 */
#endif

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) && !defined(BPF_ARENA_FORCE_ASM)
#define __arena __attribute__((address_space(1)))
#define cast_kern(ptr) /* nop for bpf prog. emitted by LLVM */
#define cast_user(ptr) /* nop for bpf prog. emitted by LLVM */
#else
#define __arena
#define cast_kern(ptr) bpf_addr_space_cast(ptr, 0, 1)
#define cast_user(ptr) bpf_addr_space_cast(ptr, 1, 0)
#endif

void __arena* bpf_arena_alloc_pages(void *map, void __arena *addr, __u32 page_cnt,
				    int node_id, __u64 flags) __ksym __weak;
void bpf_arena_free_pages(void *map, void __arena *ptr, __u32 page_cnt) __ksym __weak;

#else /* when compiled as user space code */

#define __arena
#define __arg_arena
#define cast_kern(ptr) /* nop for user space */
#define cast_user(ptr) /* nop for user space */
__weak char arena[1];

#ifndef offsetof
#define offsetof(type, member)  ((unsigned long)&((type *)0)->member)
#endif

static inline void __arena* bpf_arena_alloc_pages(void *map, void *addr, __u32 page_cnt,
						  int node_id, __u64 flags)
{
	return NULL;
}
static inline void bpf_arena_free_pages(void *map, void __arena *ptr, __u32 page_cnt)
{
}

#endif
