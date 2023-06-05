// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains core generic KASAN code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/kfence.h>
#include <linux/kmemleak.h>
#include <linux/linkage.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/bug.h>

#include "kasan.h"
#include "../slab.h"

/*
 * All functions below always inlined so compiler could
 * perform better optimizations in each of __asan_loadX/__assn_storeX
 * depending on memory access size X.
 */

static __always_inline bool memory_is_poisoned_1(unsigned long addr)
{
	s8 shadow_value = *(s8 *)kasan_mem_to_shadow((void *)addr);

	if (unlikely(shadow_value)) {
		s8 last_accessible_byte = addr & KASAN_GRANULE_MASK;
		return unlikely(last_accessible_byte >= shadow_value);
	}

	return false;
}

static __always_inline bool memory_is_poisoned_2_4_8(unsigned long addr,
						unsigned long size)
{
	u8 *shadow_addr = (u8 *)kasan_mem_to_shadow((void *)addr);

	/*
	 * Access crosses 8(shadow size)-byte boundary. Such access maps
	 * into 2 shadow bytes, so we need to check them both.
	 */
	if (unlikely(((addr + size - 1) & KASAN_GRANULE_MASK) < size - 1))
		return *shadow_addr || memory_is_poisoned_1(addr + size - 1);

	return memory_is_poisoned_1(addr + size - 1);
}

static __always_inline bool memory_is_poisoned_16(unsigned long addr)
{
	u16 *shadow_addr = (u16 *)kasan_mem_to_shadow((void *)addr);

	/* Unaligned 16-bytes access maps into 3 shadow bytes. */
	if (unlikely(!IS_ALIGNED(addr, KASAN_GRANULE_SIZE)))
		return *shadow_addr || memory_is_poisoned_1(addr + 15);

	return *shadow_addr;
}

static __always_inline unsigned long bytes_is_nonzero(const u8 *start,
					size_t size)
{
	while (size) {
		if (unlikely(*start))
			return (unsigned long)start;
		start++;
		size--;
	}

	return 0;
}

static __always_inline unsigned long memory_is_nonzero(const void *start,
						const void *end)
{
	unsigned int words;
	unsigned long ret;
	unsigned int prefix = (unsigned long)start % 8;

	if (end - start <= 16)
		return bytes_is_nonzero(start, end - start);

	if (prefix) {
		prefix = 8 - prefix;
		ret = bytes_is_nonzero(start, prefix);
		if (unlikely(ret))
			return ret;
		start += prefix;
	}

	words = (end - start) / 8;
	while (words) {
		if (unlikely(*(u64 *)start))
			return bytes_is_nonzero(start, 8);
		start += 8;
		words--;
	}

	return bytes_is_nonzero(start, (end - start) % 8);
}

static __always_inline bool memory_is_poisoned_n(unsigned long addr,
						size_t size)
{
	unsigned long ret;

	ret = memory_is_nonzero(kasan_mem_to_shadow((void *)addr),
			kasan_mem_to_shadow((void *)addr + size - 1) + 1);

	if (unlikely(ret)) {
		unsigned long last_byte = addr + size - 1;
		s8 *last_shadow = (s8 *)kasan_mem_to_shadow((void *)last_byte);

		if (unlikely(ret != (unsigned long)last_shadow ||
			((long)(last_byte & KASAN_GRANULE_MASK) >= *last_shadow)))
			return true;
	}
	return false;
}

static __always_inline bool memory_is_poisoned(unsigned long addr, size_t size)
{
	if (__builtin_constant_p(size)) {
		switch (size) {
		case 1:
			return memory_is_poisoned_1(addr);
		case 2:
		case 4:
		case 8:
			return memory_is_poisoned_2_4_8(addr, size);
		case 16:
			return memory_is_poisoned_16(addr);
		default:
			BUILD_BUG();
		}
	}

	return memory_is_poisoned_n(addr, size);
}

static __always_inline bool check_region_inline(unsigned long addr,
						size_t size, bool write,
						unsigned long ret_ip)
{
	if (!kasan_arch_is_ready())
		return true;

	if (unlikely(size == 0))
		return true;

	if (unlikely(addr + size < addr))
		return !kasan_report(addr, size, write, ret_ip);

	if (unlikely(!addr_has_metadata((void *)addr)))
		return !kasan_report(addr, size, write, ret_ip);

	if (likely(!memory_is_poisoned(addr, size)))
		return true;

	return !kasan_report(addr, size, write, ret_ip);
}

bool kasan_check_range(unsigned long addr, size_t size, bool write,
					unsigned long ret_ip)
{
	return check_region_inline(addr, size, write, ret_ip);
}

bool kasan_byte_accessible(const void *addr)
{
	s8 shadow_byte;

	if (!kasan_arch_is_ready())
		return true;

	shadow_byte = READ_ONCE(*(s8 *)kasan_mem_to_shadow(addr));

	return shadow_byte >= 0 && shadow_byte < KASAN_GRANULE_SIZE;
}

void kasan_cache_shrink(struct kmem_cache *cache)
{
	kasan_quarantine_remove_cache(cache);
}

void kasan_cache_shutdown(struct kmem_cache *cache)
{
	if (!__kmem_cache_empty(cache))
		kasan_quarantine_remove_cache(cache);
}

static void register_global(struct kasan_global *global)
{
	size_t aligned_size = round_up(global->size, KASAN_GRANULE_SIZE);

	kasan_unpoison(global->beg, global->size, false);

	kasan_poison(global->beg + aligned_size,
		     global->size_with_redzone - aligned_size,
		     KASAN_GLOBAL_REDZONE, false);
}

void __asan_register_globals(struct kasan_global *globals, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		register_global(&globals[i]);
}
EXPORT_SYMBOL(__asan_register_globals);

void __asan_unregister_globals(struct kasan_global *globals, size_t size)
{
}
EXPORT_SYMBOL(__asan_unregister_globals);

#define DEFINE_ASAN_LOAD_STORE(size)					\
	void __asan_load##size(unsigned long addr)			\
	{								\
		check_region_inline(addr, size, false, _RET_IP_);	\
	}								\
	EXPORT_SYMBOL(__asan_load##size);				\
	__alias(__asan_load##size)					\
	void __asan_load##size##_noabort(unsigned long);		\
	EXPORT_SYMBOL(__asan_load##size##_noabort);			\
	void __asan_store##size(unsigned long addr)			\
	{								\
		check_region_inline(addr, size, true, _RET_IP_);	\
	}								\
	EXPORT_SYMBOL(__asan_store##size);				\
	__alias(__asan_store##size)					\
	void __asan_store##size##_noabort(unsigned long);		\
	EXPORT_SYMBOL(__asan_store##size##_noabort)

DEFINE_ASAN_LOAD_STORE(1);
DEFINE_ASAN_LOAD_STORE(2);
DEFINE_ASAN_LOAD_STORE(4);
DEFINE_ASAN_LOAD_STORE(8);
DEFINE_ASAN_LOAD_STORE(16);

void __asan_loadN(unsigned long addr, size_t size)
{
	kasan_check_range(addr, size, false, _RET_IP_);
}
EXPORT_SYMBOL(__asan_loadN);

__alias(__asan_loadN)
void __asan_loadN_noabort(unsigned long, size_t);
EXPORT_SYMBOL(__asan_loadN_noabort);

void __asan_storeN(unsigned long addr, size_t size)
{
	kasan_check_range(addr, size, true, _RET_IP_);
}
EXPORT_SYMBOL(__asan_storeN);

__alias(__asan_storeN)
void __asan_storeN_noabort(unsigned long, size_t);
EXPORT_SYMBOL(__asan_storeN_noabort);

/* to shut up compiler complaints */
void __asan_handle_no_return(void) {}
EXPORT_SYMBOL(__asan_handle_no_return);

/* Emitted by compiler to poison alloca()ed objects. */
void __asan_alloca_poison(unsigned long addr, size_t size)
{
	size_t rounded_up_size = round_up(size, KASAN_GRANULE_SIZE);
	size_t padding_size = round_up(size, KASAN_ALLOCA_REDZONE_SIZE) -
			rounded_up_size;
	size_t rounded_down_size = round_down(size, KASAN_GRANULE_SIZE);

	const void *left_redzone = (const void *)(addr -
			KASAN_ALLOCA_REDZONE_SIZE);
	const void *right_redzone = (const void *)(addr + rounded_up_size);

	WARN_ON(!IS_ALIGNED(addr, KASAN_ALLOCA_REDZONE_SIZE));

	kasan_unpoison((const void *)(addr + rounded_down_size),
			size - rounded_down_size, false);
	kasan_poison(left_redzone, KASAN_ALLOCA_REDZONE_SIZE,
		     KASAN_ALLOCA_LEFT, false);
	kasan_poison(right_redzone, padding_size + KASAN_ALLOCA_REDZONE_SIZE,
		     KASAN_ALLOCA_RIGHT, false);
}
EXPORT_SYMBOL(__asan_alloca_poison);

/* Emitted by compiler to unpoison alloca()ed areas when the stack unwinds. */
void __asan_allocas_unpoison(const void *stack_top, const void *stack_bottom)
{
	if (unlikely(!stack_top || stack_top > stack_bottom))
		return;

	kasan_unpoison(stack_top, stack_bottom - stack_top, false);
}
EXPORT_SYMBOL(__asan_allocas_unpoison);

/* Emitted by the compiler to [un]poison local variables. */
#define DEFINE_ASAN_SET_SHADOW(byte) \
	void __asan_set_shadow_##byte(const void *addr, size_t size)	\
	{								\
		__memset((void *)addr, 0x##byte, size);			\
	}								\
	EXPORT_SYMBOL(__asan_set_shadow_##byte)

DEFINE_ASAN_SET_SHADOW(00);
DEFINE_ASAN_SET_SHADOW(f1);
DEFINE_ASAN_SET_SHADOW(f2);
DEFINE_ASAN_SET_SHADOW(f3);
DEFINE_ASAN_SET_SHADOW(f5);
DEFINE_ASAN_SET_SHADOW(f8);

/* Only allow cache merging when no per-object metadata is present. */
slab_flags_t kasan_never_merge(void)
{
	if (!kasan_requires_meta())
		return 0;
	return SLAB_KASAN;
}

/*
 * Adaptive redzone policy taken from the userspace AddressSanitizer runtime.
 * For larger allocations larger redzones are used.
 */
static inline unsigned int optimal_redzone(unsigned int object_size)
{
	return
		object_size <= 64        - 16   ? 16 :
		object_size <= 128       - 32   ? 32 :
		object_size <= 512       - 64   ? 64 :
		object_size <= 4096      - 128  ? 128 :
		object_size <= (1 << 14) - 256  ? 256 :
		object_size <= (1 << 15) - 512  ? 512 :
		object_size <= (1 << 16) - 1024 ? 1024 : 2048;
}

void kasan_cache_create(struct kmem_cache *cache, unsigned int *size,
			  slab_flags_t *flags)
{
	unsigned int ok_size;
	unsigned int optimal_size;

	if (!kasan_requires_meta())
		return;

	/*
	 * SLAB_KASAN is used to mark caches that are sanitized by KASAN
	 * and that thus have per-object metadata.
	 * Currently this flag is used in two places:
	 * 1. In slab_ksize() to account for per-object metadata when
	 *    calculating the size of the accessible memory within the object.
	 * 2. In slab_common.c via kasan_never_merge() to prevent merging of
	 *    caches with per-object metadata.
	 */
	*flags |= SLAB_KASAN;

	ok_size = *size;

	/* Add alloc meta into redzone. */
	cache->kasan_info.alloc_meta_offset = *size;
	*size += sizeof(struct kasan_alloc_meta);

	/*
	 * If alloc meta doesn't fit, don't add it.
	 * This can only happen with SLAB, as it has KMALLOC_MAX_SIZE equal
	 * to KMALLOC_MAX_CACHE_SIZE and doesn't fall back to page_alloc for
	 * larger sizes.
	 */
	if (*size > KMALLOC_MAX_SIZE) {
		cache->kasan_info.alloc_meta_offset = 0;
		*size = ok_size;
		/* Continue, since free meta might still fit. */
	}

	/*
	 * Add free meta into redzone when it's not possible to store
	 * it in the object. This is the case when:
	 * 1. Object is SLAB_TYPESAFE_BY_RCU, which means that it can
	 *    be touched after it was freed, or
	 * 2. Object has a constructor, which means it's expected to
	 *    retain its content until the next allocation, or
	 * 3. Object is too small.
	 * Otherwise cache->kasan_info.free_meta_offset = 0 is implied.
	 */
	if ((cache->flags & SLAB_TYPESAFE_BY_RCU) || cache->ctor ||
	    cache->object_size < sizeof(struct kasan_free_meta)) {
		ok_size = *size;

		cache->kasan_info.free_meta_offset = *size;
		*size += sizeof(struct kasan_free_meta);

		/* If free meta doesn't fit, don't add it. */
		if (*size > KMALLOC_MAX_SIZE) {
			cache->kasan_info.free_meta_offset = KASAN_NO_FREE_META;
			*size = ok_size;
		}
	}

	/* Calculate size with optimal redzone. */
	optimal_size = cache->object_size + optimal_redzone(cache->object_size);
	/* Limit it with KMALLOC_MAX_SIZE (relevant for SLAB only). */
	if (optimal_size > KMALLOC_MAX_SIZE)
		optimal_size = KMALLOC_MAX_SIZE;
	/* Use optimal size if the size with added metas is not large enough. */
	if (*size < optimal_size)
		*size = optimal_size;
}

struct kasan_alloc_meta *kasan_get_alloc_meta(struct kmem_cache *cache,
					      const void *object)
{
	if (!cache->kasan_info.alloc_meta_offset)
		return NULL;
	return (void *)object + cache->kasan_info.alloc_meta_offset;
}

struct kasan_free_meta *kasan_get_free_meta(struct kmem_cache *cache,
					    const void *object)
{
	BUILD_BUG_ON(sizeof(struct kasan_free_meta) > 32);
	if (cache->kasan_info.free_meta_offset == KASAN_NO_FREE_META)
		return NULL;
	return (void *)object + cache->kasan_info.free_meta_offset;
}

void kasan_init_object_meta(struct kmem_cache *cache, const void *object)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (alloc_meta)
		__memset(alloc_meta, 0, sizeof(*alloc_meta));
}

size_t kasan_metadata_size(struct kmem_cache *cache, bool in_object)
{
	struct kasan_cache *info = &cache->kasan_info;

	if (!kasan_requires_meta())
		return 0;

	if (in_object)
		return (info->free_meta_offset ?
			0 : sizeof(struct kasan_free_meta));
	else
		return (info->alloc_meta_offset ?
			sizeof(struct kasan_alloc_meta) : 0) +
			((info->free_meta_offset &&
			info->free_meta_offset != KASAN_NO_FREE_META) ?
			sizeof(struct kasan_free_meta) : 0);
}

static void __kasan_record_aux_stack(void *addr, bool can_alloc)
{
	struct slab *slab = kasan_addr_to_slab(addr);
	struct kmem_cache *cache;
	struct kasan_alloc_meta *alloc_meta;
	void *object;

	if (is_kfence_address(addr) || !slab)
		return;

	cache = slab->slab_cache;
	object = nearest_obj(cache, slab, addr);
	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (!alloc_meta)
		return;

	alloc_meta->aux_stack[1] = alloc_meta->aux_stack[0];
	alloc_meta->aux_stack[0] = kasan_save_stack(GFP_NOWAIT, can_alloc);
}

void kasan_record_aux_stack(void *addr)
{
	return __kasan_record_aux_stack(addr, true);
}

void kasan_record_aux_stack_noalloc(void *addr)
{
	return __kasan_record_aux_stack(addr, false);
}

void kasan_save_alloc_info(struct kmem_cache *cache, void *object, gfp_t flags)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (alloc_meta)
		kasan_set_track(&alloc_meta->alloc_track, flags);
}

void kasan_save_free_info(struct kmem_cache *cache, void *object)
{
	struct kasan_free_meta *free_meta;

	free_meta = kasan_get_free_meta(cache, object);
	if (!free_meta)
		return;

	kasan_set_track(&free_meta->free_track, GFP_NOWAIT);
	/* The object was freed and has free track set. */
	*(u8 *)kasan_mem_to_shadow(object) = KASAN_SLAB_FREETRACK;
}
