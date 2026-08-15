// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#pragma once

#ifdef __BPF__

#include <vmlinux.h>

#include <bpf_arena_common.h>
#include <bpf_arena_spin_lock.h>

#include <asm-generic/errno.h>

#ifndef __BPF_FEATURE_ADDR_SPACE_CAST
#error "Arena allocators require bpf_addr_space_cast feature"
#endif

#define arena_stdout(fmt, ...) bpf_stream_printk(1, (fmt), ##__VA_ARGS__)
#define arena_stderr(fmt, ...) bpf_stream_printk(2, (fmt), ##__VA_ARGS__)

#ifndef __maybe_unused
#define __maybe_unused __attribute__((__unused__))
#endif

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))

#define ARENA_PAGES (1UL << (32 - __builtin_ffs(__PAGE_SIZE) + 1))

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, ARENA_PAGES); /* number of pages */
#if defined(__TARGET_ARCH_arm64) || defined(__aarch64__)
	__ulong(map_extra, (1ull << 32)); /* start of mmap() region */
#else
	__ulong(map_extra, (1ull << 44)); /* start of mmap() region */
#endif
} arena __weak SEC(".maps");

/*
 * This is a variable used to aid verification. The may_goto directive
 * permits open-coded for loops, but requires that the index variable is
 * imprecise. To force the variable to be imprecise, initialize it with
 * the opaque volatile variable 0 instead of the constant 0.
 */
extern const volatile u32 zero;
extern volatile u64 asan_violated;

int arena_fls(__u64 word);

void __arena *arena_malloc(size_t size);
void arena_free(void __arena *ptr);

/*
 * The verifier associates arenas with programs by checking LD.IMM
 * instruction operands for an arena and populating the program state
 * with the first instance it finds. This requires accessing our global
 * arena variable, but subprogs do not necessarily do so while still
 * using pointers from that arena. Insert an LD.IMM instruction  to
 * access the arena and help the verifier.
 */
#define arena_subprog_init() do { asm volatile ("" :: "r"(&arena)); } while (0)

#else /* ! __BPF__ */

#include <stdint.h>

#define __arena

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* Dummy "definition" for userspace. */
#define arena_spinlock_t int

#endif /* __BPF__ */

struct arena_get_info_args {
	void __arena *arena_base;
};

struct arena_alloc_reserve_args {
	u64 nr_pages;
};

/* Reasonable default number of pages reserved by arena_alloc_reserve. */
#define ARENA_RESERVE_PAGES_DFL (8)
