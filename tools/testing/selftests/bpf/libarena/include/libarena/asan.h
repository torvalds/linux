// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#pragma once

struct asan_init_args {
	u64 arena_all_pages;
	u64 arena_globals_pages;
};

int asan_init(struct asan_init_args *args);

extern volatile u64 __asan_shadow_memory_dynamic_address;
extern volatile u32 asan_reported;
extern volatile bool asan_inited;
extern volatile bool asan_report_once;

#ifdef __BPF__

#define ASAN_SHADOW_SHIFT 3
#define ASAN_SHADOW_SCALE (1ULL << ASAN_SHADOW_SHIFT)
#define ASAN_GRANULE_MASK ((1ULL << ASAN_SHADOW_SHIFT) - 1)
#define ASAN_GRANULE(addr) ((s8)((u32)(u64)((addr)) & ASAN_GRANULE_MASK))

#define __noasan __attribute__((no_sanitize("address")))

#ifdef BPF_ARENA_ASAN

static inline
s8 __arena *mem_to_shadow(void __arena *addr)
{
	return (s8 __arena *)(((u32)(u64)addr >> ASAN_SHADOW_SHIFT) +
			__asan_shadow_memory_dynamic_address);
}

__weak __noasan
bool asan_ready(void)
{
	return __asan_shadow_memory_dynamic_address;
}

int asan_poison(void __arena *addr, s8 val, size_t size);
int asan_unpoison(void __arena *addr, size_t size);
bool asan_shadow_set(void __arena *addr);

/*
 * Dummy calls to ensure the ASAN runtime's BTF information is present
 * in every object file when compiling the runtime and local BPF code
 * separately. The runtime calls are injected into the LLVM IR file
 */
#define DECLARE_ASAN_LOAD_STORE_SIZE(size)				\
	void __asan_store##size(intptr_t addr);				\
	void __asan_store##size##_noabort(intptr_t addr);	\
	void __asan_load##size(intptr_t addr);				\
	void __asan_load##size##_noabort(intptr_t addr);	\
	void __asan_report_store##size(intptr_t addr);			\
	void __asan_report_store##size##_noabort(intptr_t addr);		\
	void __asan_report_load##size(intptr_t addr);			\
	void __asan_report_load##size##_noabort(intptr_t addr);

DECLARE_ASAN_LOAD_STORE_SIZE(1);
DECLARE_ASAN_LOAD_STORE_SIZE(2);
DECLARE_ASAN_LOAD_STORE_SIZE(4);
DECLARE_ASAN_LOAD_STORE_SIZE(8);

void __asan_storeN(intptr_t addr, ssize_t size);
void __asan_storeN_noabort(intptr_t addr, ssize_t size);
void __asan_loadN(intptr_t addr, ssize_t size);
void __asan_loadN_noabort(intptr_t addr, ssize_t size);

/*
 * Force LLVM to emit BTF information for the stubs,
 * because the ASAN pass in LLVM by itself doesn't.
 */
#define ASAN_LOAD_STORE_SIZE(size)		\
	__asan_store##size,			\
	__asan_store##size##_noabort,		\
	__asan_load##size,			\
	__asan_load##size##_noabort,		\
	__asan_report_store##size,		\
	__asan_report_store##size##_noabort,	\
	__asan_report_load##size,		\
	__asan_report_load##size##_noabort

__attribute__((used))
static void (*__asan_btf_anchors[])(intptr_t) = {
	ASAN_LOAD_STORE_SIZE(1),
	ASAN_LOAD_STORE_SIZE(2),
	ASAN_LOAD_STORE_SIZE(4),
	ASAN_LOAD_STORE_SIZE(8),
};

#else /* BPF_ARENA_ASAN */

static inline int asan_poison(void __arena *addr, s8 val, size_t size) { return 0; }
static inline int asan_unpoison(void __arena *addr, size_t size) { return 0; }
static inline bool asan_shadow_set(void __arena *addr) { return 0; }
__weak bool asan_ready(void) { return true; }

#endif /* BPF_ARENA_ASAN */

#endif /* __BPF__ */
