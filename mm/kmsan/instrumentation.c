// SPDX-License-Identifier: GPL-2.0
/*
 * KMSAN compiler API.
 *
 * This file implements __msan_XXX hooks that Clang inserts into the code
 * compiled with -fsanitize=kernel-memory.
 * See Documentation/dev-tools/kmsan.rst for more information on how KMSAN
 * instrumentation works.
 *
 * Copyright (C) 2017-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */

#include "kmsan.h"
#include <linux/gfp.h>
#include <linux/kmsan_string.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

static inline bool is_bad_asm_addr(void *addr, uintptr_t size, bool is_store)
{
	if ((u64)addr < TASK_SIZE)
		return true;
	if (!kmsan_get_metadata(addr, KMSAN_META_SHADOW))
		return true;
	return false;
}

static inline struct shadow_origin_ptr
get_shadow_origin_ptr(void *addr, u64 size, bool store)
{
	unsigned long ua_flags = user_access_save();
	struct shadow_origin_ptr ret;

	ret = kmsan_get_shadow_origin_ptr(addr, size, store);
	user_access_restore(ua_flags);
	return ret;
}

/* Get shadow and origin pointers for a memory load with non-standard size. */
struct shadow_origin_ptr __msan_metadata_ptr_for_load_n(void *addr,
							uintptr_t size)
{
	return get_shadow_origin_ptr(addr, size, /*store*/ false);
}
EXPORT_SYMBOL(__msan_metadata_ptr_for_load_n);

/* Get shadow and origin pointers for a memory store with non-standard size. */
struct shadow_origin_ptr __msan_metadata_ptr_for_store_n(void *addr,
							 uintptr_t size)
{
	return get_shadow_origin_ptr(addr, size, /*store*/ true);
}
EXPORT_SYMBOL(__msan_metadata_ptr_for_store_n);

/*
 * Declare functions that obtain shadow/origin pointers for loads and stores
 * with fixed size.
 */
#define DECLARE_METADATA_PTR_GETTER(size)                                  \
	struct shadow_origin_ptr __msan_metadata_ptr_for_load_##size(      \
		void *addr)                                                \
	{                                                                  \
		return get_shadow_origin_ptr(addr, size, /*store*/ false); \
	}                                                                  \
	EXPORT_SYMBOL(__msan_metadata_ptr_for_load_##size);                \
	struct shadow_origin_ptr __msan_metadata_ptr_for_store_##size(     \
		void *addr)                                                \
	{                                                                  \
		return get_shadow_origin_ptr(addr, size, /*store*/ true);  \
	}                                                                  \
	EXPORT_SYMBOL(__msan_metadata_ptr_for_store_##size)

DECLARE_METADATA_PTR_GETTER(1);
DECLARE_METADATA_PTR_GETTER(2);
DECLARE_METADATA_PTR_GETTER(4);
DECLARE_METADATA_PTR_GETTER(8);

/*
 * Handle a memory store performed by inline assembly. KMSAN conservatively
 * attempts to unpoison the outputs of asm() directives to prevent false
 * positives caused by missed stores.
 */
void __msan_instrument_asm_store(void *addr, uintptr_t size)
{
	unsigned long ua_flags;

	if (!kmsan_enabled || kmsan_in_runtime())
		return;

	ua_flags = user_access_save();
	/*
	 * Most of the accesses are below 32 bytes. The two exceptions so far
	 * are clwb() (64 bytes) and FPU state (512 bytes).
	 * It's unlikely that the assembly will touch more than 512 bytes.
	 */
	if (size > 512) {
		WARN_ONCE(1, "assembly store size too big: %ld\n", size);
		size = 8;
	}
	if (is_bad_asm_addr(addr, size, /*is_store*/ true)) {
		user_access_restore(ua_flags);
		return;
	}
	kmsan_enter_runtime();
	/* Unpoisoning the memory on best effort. */
	kmsan_internal_unpoison_memory(addr, size, /*checked*/ false);
	kmsan_leave_runtime();
	user_access_restore(ua_flags);
}
EXPORT_SYMBOL(__msan_instrument_asm_store);

/*
 * KMSAN instrumentation pass replaces LLVM memcpy, memmove and memset
 * intrinsics with calls to respective __msan_ functions. We use
 * get_param0_metadata() and set_retval_metadata() to store the shadow/origin
 * values for the destination argument of these functions and use them for the
 * functions' return values.
 */
static inline void get_param0_metadata(u64 *shadow,
				       depot_stack_handle_t *origin)
{
	struct kmsan_ctx *ctx = kmsan_get_context();

	*shadow = *(u64 *)(ctx->cstate.param_tls);
	*origin = ctx->cstate.param_origin_tls[0];
}

static inline void set_retval_metadata(u64 shadow, depot_stack_handle_t origin)
{
	struct kmsan_ctx *ctx = kmsan_get_context();

	*(u64 *)(ctx->cstate.retval_tls) = shadow;
	ctx->cstate.retval_origin_tls = origin;
}

/* Handle llvm.memmove intrinsic. */
void *__msan_memmove(void *dst, const void *src, uintptr_t n)
{
	depot_stack_handle_t origin;
	void *result;
	u64 shadow;

	get_param0_metadata(&shadow, &origin);
	result = __memmove(dst, src, n);
	if (!n)
		/* Some people call memmove() with zero length. */
		return result;
	if (!kmsan_enabled || kmsan_in_runtime())
		return result;

	kmsan_enter_runtime();
	kmsan_internal_memmove_metadata(dst, (void *)src, n);
	kmsan_leave_runtime();

	set_retval_metadata(shadow, origin);
	return result;
}
EXPORT_SYMBOL(__msan_memmove);

/* Handle llvm.memcpy intrinsic. */
void *__msan_memcpy(void *dst, const void *src, uintptr_t n)
{
	depot_stack_handle_t origin;
	void *result;
	u64 shadow;

	get_param0_metadata(&shadow, &origin);
	result = __memcpy(dst, src, n);
	if (!n)
		/* Some people call memcpy() with zero length. */
		return result;

	if (!kmsan_enabled || kmsan_in_runtime())
		return result;

	kmsan_enter_runtime();
	/* Using memmove instead of memcpy doesn't affect correctness. */
	kmsan_internal_memmove_metadata(dst, (void *)src, n);
	kmsan_leave_runtime();

	set_retval_metadata(shadow, origin);
	return result;
}
EXPORT_SYMBOL(__msan_memcpy);

/* Handle llvm.memset intrinsic. */
void *__msan_memset(void *dst, int c, uintptr_t n)
{
	depot_stack_handle_t origin;
	void *result;
	u64 shadow;

	get_param0_metadata(&shadow, &origin);
	result = __memset(dst, c, n);
	if (!kmsan_enabled || kmsan_in_runtime())
		return result;

	kmsan_enter_runtime();
	/*
	 * Clang doesn't pass parameter metadata here, so it is impossible to
	 * use shadow of @c to set up the shadow for @dst.
	 */
	kmsan_internal_unpoison_memory(dst, n, /*checked*/ false);
	kmsan_leave_runtime();

	set_retval_metadata(shadow, origin);
	return result;
}
EXPORT_SYMBOL(__msan_memset);

/*
 * Create a new origin from an old one. This is done when storing an
 * uninitialized value to memory. When reporting an error, KMSAN unrolls and
 * prints the whole chain of stores that preceded the use of this value.
 */
depot_stack_handle_t __msan_chain_origin(depot_stack_handle_t origin)
{
	depot_stack_handle_t ret = 0;
	unsigned long ua_flags;

	if (!kmsan_enabled || kmsan_in_runtime())
		return ret;

	ua_flags = user_access_save();

	/* Creating new origins may allocate memory. */
	kmsan_enter_runtime();
	ret = kmsan_internal_chain_origin(origin);
	kmsan_leave_runtime();
	user_access_restore(ua_flags);
	return ret;
}
EXPORT_SYMBOL(__msan_chain_origin);

/* Poison a local variable when entering a function. */
void __msan_poison_alloca(void *address, uintptr_t size, char *descr)
{
	depot_stack_handle_t handle;
	unsigned long entries[4];
	unsigned long ua_flags;

	if (!kmsan_enabled || kmsan_in_runtime())
		return;

	ua_flags = user_access_save();
	entries[0] = KMSAN_ALLOCA_MAGIC_ORIGIN;
	entries[1] = (u64)descr;
	entries[2] = (u64)__builtin_return_address(0);
	/*
	 * With frame pointers enabled, it is possible to quickly fetch the
	 * second frame of the caller stack without calling the unwinder.
	 * Without them, simply do not bother.
	 */
	if (IS_ENABLED(CONFIG_UNWINDER_FRAME_POINTER))
		entries[3] = (u64)__builtin_return_address(1);
	else
		entries[3] = 0;

	/* stack_depot_save() may allocate memory. */
	kmsan_enter_runtime();
	handle = stack_depot_save(entries, ARRAY_SIZE(entries), GFP_ATOMIC);
	kmsan_leave_runtime();

	kmsan_internal_set_shadow_origin(address, size, -1, handle,
					 /*checked*/ true);
	user_access_restore(ua_flags);
}
EXPORT_SYMBOL(__msan_poison_alloca);

/* Unpoison a local variable. */
void __msan_unpoison_alloca(void *address, uintptr_t size)
{
	if (!kmsan_enabled || kmsan_in_runtime())
		return;

	kmsan_enter_runtime();
	kmsan_internal_unpoison_memory(address, size, /*checked*/ true);
	kmsan_leave_runtime();
}
EXPORT_SYMBOL(__msan_unpoison_alloca);

/*
 * Report that an uninitialized value with the given origin was used in a way
 * that constituted undefined behavior.
 */
void __msan_warning(u32 origin)
{
	if (!kmsan_enabled || kmsan_in_runtime())
		return;
	kmsan_enter_runtime();
	kmsan_report(origin, /*address*/ 0, /*size*/ 0,
		     /*off_first*/ 0, /*off_last*/ 0, /*user_addr*/ 0,
		     REASON_ANY);
	kmsan_leave_runtime();
}
EXPORT_SYMBOL(__msan_warning);

/*
 * At the beginning of an instrumented function, obtain the pointer to
 * `struct kmsan_context_state` holding the metadata for function parameters.
 */
struct kmsan_context_state *__msan_get_context_state(void)
{
	return &kmsan_get_context()->cstate;
}
EXPORT_SYMBOL(__msan_get_context_state);
