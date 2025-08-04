/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#pragma once

#ifndef arena_container_of
#define arena_container_of(ptr, type, member)			\
	({							\
		void __arena *__mptr = (void __arena *)(ptr);	\
		((type *)(__mptr - offsetof(type, member)));	\
	})
#endif

/* Provide the definition of PAGE_SIZE. */
#include <sys/user.h>

#define __arena
#define __arg_arena
#define cast_kern(ptr) /* nop for user space */
#define cast_user(ptr) /* nop for user space */
char __attribute__((weak)) arena[1];

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
