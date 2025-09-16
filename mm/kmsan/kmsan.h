/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Functions used by the KMSAN runtime.
 *
 * Copyright (C) 2017-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */

#ifndef __MM_KMSAN_KMSAN_H
#define __MM_KMSAN_KMSAN_H

#include <linux/irqflags.h>
#include <linux/kmsan.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/pgtable.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>

#define KMSAN_ALLOCA_MAGIC_ORIGIN 0xabcd0100
#define KMSAN_CHAIN_MAGIC_ORIGIN 0xabcd0200

#define KMSAN_POISON_NOCHECK 0x0
#define KMSAN_POISON_CHECK 0x1
#define KMSAN_POISON_FREE 0x2

#define KMSAN_ORIGIN_SIZE 4
#define KMSAN_MAX_ORIGIN_DEPTH 7

#define KMSAN_STACK_DEPTH 64

#define KMSAN_META_SHADOW (false)
#define KMSAN_META_ORIGIN (true)

/*
 * A pair of metadata pointers to be returned by the instrumentation functions.
 */
struct shadow_origin_ptr {
	void *shadow, *origin;
};

struct shadow_origin_ptr kmsan_get_shadow_origin_ptr(void *addr, u64 size,
						     bool store);
void __init kmsan_init_alloc_meta_for_range(void *start, void *end);

enum kmsan_bug_reason {
	REASON_ANY,
	REASON_COPY_TO_USER,
	REASON_SUBMIT_URB,
};

void kmsan_print_origin(depot_stack_handle_t origin);

/**
 * kmsan_report() - Report a use of uninitialized value.
 * @origin:    Stack ID of the uninitialized value.
 * @address:   Address at which the memory access happens.
 * @size:      Memory access size.
 * @off_first: Offset (from @address) of the first byte to be reported.
 * @off_last:  Offset (from @address) of the last byte to be reported.
 * @user_addr: When non-NULL, denotes the userspace address to which the kernel
 *             is leaking data.
 * @reason:    Error type from enum kmsan_bug_reason.
 *
 * kmsan_report() prints an error message for a consequent group of bytes
 * sharing the same origin. If an uninitialized value is used in a comparison,
 * this function is called once without specifying the addresses. When checking
 * a memory range, KMSAN may call kmsan_report() multiple times with the same
 * @address, @size, @user_addr and @reason, but different @off_first and
 * @off_last corresponding to different @origin values.
 */
void kmsan_report(depot_stack_handle_t origin, void *address, int size,
		  int off_first, int off_last, const void __user *user_addr,
		  enum kmsan_bug_reason reason);

DECLARE_PER_CPU(struct kmsan_ctx, kmsan_percpu_ctx);

static __always_inline struct kmsan_ctx *kmsan_get_context(void)
{
	return in_task() ? &current->kmsan_ctx : raw_cpu_ptr(&kmsan_percpu_ctx);
}

/*
 * When a compiler hook or KMSAN runtime function is invoked, it may make a
 * call to instrumented code and eventually call itself recursively. To avoid
 * that, we guard the runtime entry regions with
 * kmsan_enter_runtime()/kmsan_leave_runtime() and exit the hook if
 * kmsan_in_runtime() is true.
 *
 * Non-runtime code may occasionally get executed in nested IRQs from the
 * runtime code (e.g. when called via smp_call_function_single()). Because some
 * KMSAN routines may take locks (e.g. for memory allocation), we conservatively
 * bail out instead of calling them. To minimize the effect of this (potentially
 * missing initialization events) kmsan_in_runtime() is not checked in
 * non-blocking runtime functions.
 */
static __always_inline bool kmsan_in_runtime(void)
{
	if ((hardirq_count() >> HARDIRQ_SHIFT) > 1)
		return true;
	if (in_nmi())
		return true;
	return kmsan_get_context()->kmsan_in_runtime;
}

static __always_inline void kmsan_enter_runtime(void)
{
	struct kmsan_ctx *ctx;

	ctx = kmsan_get_context();
	KMSAN_WARN_ON(ctx->kmsan_in_runtime++);
}

static __always_inline void kmsan_leave_runtime(void)
{
	struct kmsan_ctx *ctx = kmsan_get_context();

	KMSAN_WARN_ON(--ctx->kmsan_in_runtime);
}

depot_stack_handle_t kmsan_save_stack_with_flags(gfp_t flags,
						 unsigned int extra_bits);

/*
 * Pack and unpack the origin chain depth and UAF flag to/from the extra bits
 * provided by the stack depot.
 * The UAF flag is stored in the lowest bit, followed by the depth in the upper
 * bits.
 * set_dsh_extra_bits() is responsible for clamping the value.
 */
static __always_inline unsigned int kmsan_extra_bits(unsigned int depth,
						     bool uaf)
{
	return (depth << 1) | uaf;
}

static __always_inline bool kmsan_uaf_from_eb(unsigned int extra_bits)
{
	return extra_bits & 1;
}

static __always_inline unsigned int kmsan_depth_from_eb(unsigned int extra_bits)
{
	return extra_bits >> 1;
}

/*
 * kmsan_internal_ functions are supposed to be very simple and not require the
 * kmsan_in_runtime() checks.
 */
void kmsan_internal_memmove_metadata(void *dst, void *src, size_t n);
void kmsan_internal_poison_memory(void *address, size_t size, gfp_t flags,
				  unsigned int poison_flags);
void kmsan_internal_unpoison_memory(void *address, size_t size, bool checked);
void kmsan_internal_set_shadow_origin(void *address, size_t size, int b,
				      u32 origin, bool checked);
depot_stack_handle_t kmsan_internal_chain_origin(depot_stack_handle_t id);

void kmsan_internal_task_create(struct task_struct *task);

bool kmsan_metadata_is_contiguous(void *addr, size_t size);
void kmsan_internal_check_memory(void *addr, size_t size,
				 const void __user *user_addr, int reason);

struct page *kmsan_vmalloc_to_page_or_null(void *vaddr);
void kmsan_setup_meta(struct page *page, struct page *shadow,
		      struct page *origin, int order);

/*
 * kmsan_internal_is_module_addr() and kmsan_internal_is_vmalloc_addr() are
 * non-instrumented versions of is_module_address() and is_vmalloc_addr() that
 * are safe to call from KMSAN runtime without recursion.
 */
static inline bool kmsan_internal_is_module_addr(void *vaddr)
{
	return ((u64)vaddr >= MODULES_VADDR) && ((u64)vaddr < MODULES_END);
}

static inline bool kmsan_internal_is_vmalloc_addr(void *addr)
{
	return ((u64)addr >= VMALLOC_START) && ((u64)addr < VMALLOC_END);
}

#endif /* __MM_KMSAN_KMSAN_H */
