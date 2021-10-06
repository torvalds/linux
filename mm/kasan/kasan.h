/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_KASAN_KASAN_H
#define __MM_KASAN_KASAN_H

#include <linux/kasan.h>
#include <linux/kasan-tags.h>
#include <linux/kfence.h>
#include <linux/stackdepot.h>

#ifdef CONFIG_KASAN_HW_TAGS

#include <linux/static_key.h>
#include "../slab.h"

DECLARE_STATIC_KEY_FALSE(kasan_flag_stacktrace);
extern bool kasan_flag_async __ro_after_init;

static inline bool kasan_stack_collection_enabled(void)
{
	return static_branch_unlikely(&kasan_flag_stacktrace);
}

static inline bool kasan_async_mode_enabled(void)
{
	return kasan_flag_async;
}
#else

static inline bool kasan_stack_collection_enabled(void)
{
	return true;
}

static inline bool kasan_async_mode_enabled(void)
{
	return false;
}

#endif

extern bool kasan_flag_panic __ro_after_init;
extern bool kasan_flag_async __ro_after_init;

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
#define KASAN_GRANULE_SIZE	(1UL << KASAN_SHADOW_SCALE_SHIFT)
#else
#include <asm/mte-kasan.h>
#define KASAN_GRANULE_SIZE	MTE_GRANULE_SIZE
#endif

#define KASAN_GRANULE_MASK	(KASAN_GRANULE_SIZE - 1)

#define KASAN_MEMORY_PER_SHADOW_PAGE	(KASAN_GRANULE_SIZE << PAGE_SHIFT)

#ifdef CONFIG_KASAN_GENERIC
#define KASAN_FREE_PAGE         0xFF  /* page was freed */
#define KASAN_PAGE_REDZONE      0xFE  /* redzone for kmalloc_large allocations */
#define KASAN_KMALLOC_REDZONE   0xFC  /* redzone inside slub object */
#define KASAN_KMALLOC_FREE      0xFB  /* object was freed (kmem_cache_free/kfree) */
#define KASAN_KMALLOC_FREETRACK 0xFA  /* object was freed and has free track set */
#else
#define KASAN_FREE_PAGE         KASAN_TAG_INVALID
#define KASAN_PAGE_REDZONE      KASAN_TAG_INVALID
#define KASAN_KMALLOC_REDZONE   KASAN_TAG_INVALID
#define KASAN_KMALLOC_FREE      KASAN_TAG_INVALID
#define KASAN_KMALLOC_FREETRACK KASAN_TAG_INVALID
#endif

#define KASAN_GLOBAL_REDZONE    0xF9  /* redzone for global variable */
#define KASAN_VMALLOC_INVALID   0xF8  /* unallocated space in vmapped page */

/*
 * Stack redzone shadow values
 * (Those are compiler's ABI, don't change them)
 */
#define KASAN_STACK_LEFT        0xF1
#define KASAN_STACK_MID         0xF2
#define KASAN_STACK_RIGHT       0xF3
#define KASAN_STACK_PARTIAL     0xF4

/*
 * alloca redzone shadow values
 */
#define KASAN_ALLOCA_LEFT	0xCA
#define KASAN_ALLOCA_RIGHT	0xCB

#define KASAN_ALLOCA_REDZONE_SIZE	32

/*
 * Stack frame marker (compiler ABI).
 */
#define KASAN_CURRENT_STACK_FRAME_MAGIC 0x41B58AB3

/* Don't break randconfig/all*config builds */
#ifndef KASAN_ABI_VERSION
#define KASAN_ABI_VERSION 1
#endif

/* Metadata layout customization. */
#define META_BYTES_PER_BLOCK 1
#define META_BLOCKS_PER_ROW 16
#define META_BYTES_PER_ROW (META_BLOCKS_PER_ROW * META_BYTES_PER_BLOCK)
#define META_MEM_BYTES_PER_ROW (META_BYTES_PER_ROW * KASAN_GRANULE_SIZE)
#define META_ROWS_AROUND_ADDR 2

struct kasan_access_info {
	const void *access_addr;
	const void *first_bad_addr;
	size_t access_size;
	bool is_write;
	unsigned long ip;
};

/* The layout of struct dictated by compiler */
struct kasan_source_location {
	const char *filename;
	int line_no;
	int column_no;
};

/* The layout of struct dictated by compiler */
struct kasan_global {
	const void *beg;		/* Address of the beginning of the global variable. */
	size_t size;			/* Size of the global variable. */
	size_t size_with_redzone;	/* Size of the variable + size of the red zone. 32 bytes aligned */
	const void *name;
	const void *module_name;	/* Name of the module where the global variable is declared. */
	unsigned long has_dynamic_init;	/* This needed for C++ */
#if KASAN_ABI_VERSION >= 4
	struct kasan_source_location *location;
#endif
#if KASAN_ABI_VERSION >= 5
	char *odr_indicator;
#endif
};

/**
 * Structures to keep alloc and free tracks *
 */

#define KASAN_STACK_DEPTH 64

struct kasan_track {
	u32 pid;
	depot_stack_handle_t stack;
};

#ifdef CONFIG_KASAN_SW_TAGS_IDENTIFY
#define KASAN_NR_FREE_STACKS 5
#else
#define KASAN_NR_FREE_STACKS 1
#endif

struct kasan_alloc_meta {
	struct kasan_track alloc_track;
#ifdef CONFIG_KASAN_GENERIC
	/*
	 * The auxiliary stack is stored into struct kasan_alloc_meta.
	 * The free stack is stored into struct kasan_free_meta.
	 */
	depot_stack_handle_t aux_stack[2];
#else
	struct kasan_track free_track[KASAN_NR_FREE_STACKS];
#endif
#ifdef CONFIG_KASAN_SW_TAGS_IDENTIFY
	u8 free_pointer_tag[KASAN_NR_FREE_STACKS];
	u8 free_track_idx;
#endif
};

struct qlist_node {
	struct qlist_node *next;
};

/*
 * Generic mode either stores free meta in the object itself or in the redzone
 * after the object. In the former case free meta offset is 0, in the latter
 * case it has some sane value smaller than INT_MAX. Use INT_MAX as free meta
 * offset when free meta isn't present.
 */
#define KASAN_NO_FREE_META INT_MAX

struct kasan_free_meta {
#ifdef CONFIG_KASAN_GENERIC
	/* This field is used while the object is in the quarantine.
	 * Otherwise it might be used for the allocator freelist.
	 */
	struct qlist_node quarantine_link;
	struct kasan_track free_track;
#endif
};

struct kasan_alloc_meta *kasan_get_alloc_meta(struct kmem_cache *cache,
						const void *object);
#ifdef CONFIG_KASAN_GENERIC
struct kasan_free_meta *kasan_get_free_meta(struct kmem_cache *cache,
						const void *object);
#endif

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

static inline const void *kasan_shadow_to_mem(const void *shadow_addr)
{
	return (void *)(((unsigned long)shadow_addr - KASAN_SHADOW_OFFSET)
		<< KASAN_SHADOW_SCALE_SHIFT);
}

static inline bool addr_has_metadata(const void *addr)
{
	return (addr >= kasan_shadow_to_mem((void *)KASAN_SHADOW_START));
}

/**
 * kasan_check_range - Check memory region, and report if invalid access.
 * @addr: the accessed address
 * @size: the accessed size
 * @write: true if access is a write access
 * @ret_ip: return address
 * @return: true if access was valid, false if invalid
 */
bool kasan_check_range(unsigned long addr, size_t size, bool write,
				unsigned long ret_ip);

#else /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

static inline bool addr_has_metadata(const void *addr)
{
	return (is_vmalloc_addr(addr) || virt_addr_valid(addr));
}

#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)
void kasan_print_tags(u8 addr_tag, const void *addr);
#else
static inline void kasan_print_tags(u8 addr_tag, const void *addr) { }
#endif

void *kasan_find_first_bad_addr(void *addr, size_t size);
const char *kasan_get_bug_type(struct kasan_access_info *info);
void kasan_metadata_fetch_row(char *buffer, void *row);

#if defined(CONFIG_KASAN_GENERIC) && defined(CONFIG_KASAN_STACK)
void kasan_print_address_stack_frame(const void *addr);
#else
static inline void kasan_print_address_stack_frame(const void *addr) { }
#endif

bool kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip);
void kasan_report_invalid_free(void *object, unsigned long ip);

struct page *kasan_addr_to_page(const void *addr);

depot_stack_handle_t kasan_save_stack(gfp_t flags);
void kasan_set_track(struct kasan_track *track, gfp_t flags);
void kasan_set_free_info(struct kmem_cache *cache, void *object, u8 tag);
struct kasan_track *kasan_get_free_track(struct kmem_cache *cache,
				void *object, u8 tag);

#if defined(CONFIG_KASAN_GENERIC) && \
	(defined(CONFIG_SLAB) || defined(CONFIG_SLUB))
bool kasan_quarantine_put(struct kmem_cache *cache, void *object);
void kasan_quarantine_reduce(void);
void kasan_quarantine_remove_cache(struct kmem_cache *cache);
#else
static inline bool kasan_quarantine_put(struct kmem_cache *cache, void *object) { return false; }
static inline void kasan_quarantine_reduce(void) { }
static inline void kasan_quarantine_remove_cache(struct kmem_cache *cache) { }
#endif

#ifndef arch_kasan_set_tag
static inline const void *arch_kasan_set_tag(const void *addr, u8 tag)
{
	return addr;
}
#endif
#ifndef arch_kasan_get_tag
#define arch_kasan_get_tag(addr)	0
#endif

#define set_tag(addr, tag)	((void *)arch_kasan_set_tag((addr), (tag)))
#define get_tag(addr)		arch_kasan_get_tag(addr)

#ifdef CONFIG_KASAN_HW_TAGS

#ifndef arch_enable_tagging_sync
#define arch_enable_tagging_sync()
#endif
#ifndef arch_enable_tagging_async
#define arch_enable_tagging_async()
#endif
#ifndef arch_set_tagging_report_once
#define arch_set_tagging_report_once(state)
#endif
#ifndef arch_force_async_tag_fault
#define arch_force_async_tag_fault()
#endif
#ifndef arch_get_random_tag
#define arch_get_random_tag()	(0xFF)
#endif
#ifndef arch_get_mem_tag
#define arch_get_mem_tag(addr)	(0xFF)
#endif
#ifndef arch_set_mem_tag_range
#define arch_set_mem_tag_range(addr, size, tag, init) ((void *)(addr))
#endif

#define hw_enable_tagging_sync()		arch_enable_tagging_sync()
#define hw_enable_tagging_async()		arch_enable_tagging_async()
#define hw_set_tagging_report_once(state)	arch_set_tagging_report_once(state)
#define hw_force_async_tag_fault()		arch_force_async_tag_fault()
#define hw_get_random_tag()			arch_get_random_tag()
#define hw_get_mem_tag(addr)			arch_get_mem_tag(addr)
#define hw_set_mem_tag_range(addr, size, tag, init) \
			arch_set_mem_tag_range((addr), (size), (tag), (init))

#else /* CONFIG_KASAN_HW_TAGS */

#define hw_enable_tagging_sync()
#define hw_enable_tagging_async()
#define hw_set_tagging_report_once(state)

#endif /* CONFIG_KASAN_HW_TAGS */

#if defined(CONFIG_KASAN_HW_TAGS) && IS_ENABLED(CONFIG_KASAN_KUNIT_TEST)

void kasan_set_tagging_report_once(bool state);
void kasan_enable_tagging_sync(void);
void kasan_force_async_fault(void);

#else /* CONFIG_KASAN_HW_TAGS || CONFIG_KASAN_KUNIT_TEST */

static inline void kasan_set_tagging_report_once(bool state) { }
static inline void kasan_enable_tagging_sync(void) { }
static inline void kasan_force_async_fault(void) { }

#endif /* CONFIG_KASAN_HW_TAGS || CONFIG_KASAN_KUNIT_TEST */

#ifdef CONFIG_KASAN_SW_TAGS
u8 kasan_random_tag(void);
#elif defined(CONFIG_KASAN_HW_TAGS)
static inline u8 kasan_random_tag(void) { return hw_get_random_tag(); }
#else
static inline u8 kasan_random_tag(void) { return 0; }
#endif

#ifdef CONFIG_KASAN_HW_TAGS

static inline void kasan_poison(const void *addr, size_t size, u8 value, bool init)
{
	addr = kasan_reset_tag(addr);

	/* Skip KFENCE memory if called explicitly outside of sl*b. */
	if (is_kfence_address(addr))
		return;

	if (WARN_ON((unsigned long)addr & KASAN_GRANULE_MASK))
		return;
	if (WARN_ON(size & KASAN_GRANULE_MASK))
		return;

	hw_set_mem_tag_range((void *)addr, size, value, init);
}

static inline void kasan_unpoison(const void *addr, size_t size, bool init)
{
	u8 tag = get_tag(addr);

	addr = kasan_reset_tag(addr);

	/* Skip KFENCE memory if called explicitly outside of sl*b. */
	if (is_kfence_address(addr))
		return;

	if (WARN_ON((unsigned long)addr & KASAN_GRANULE_MASK))
		return;
	/*
	 * Explicitly initialize the memory with the precise object size to
	 * avoid overwriting the SLAB redzone. This disables initialization in
	 * the arch code and may thus lead to performance penalty. The penalty
	 * is accepted since SLAB redzones aren't enabled in production builds.
	 */
	if (__slub_debug_enabled() &&
	    init && ((unsigned long)size & KASAN_GRANULE_MASK)) {
		init = false;
		memzero_explicit((void *)addr, size);
	}
	size = round_up(size, KASAN_GRANULE_SIZE);

	hw_set_mem_tag_range((void *)addr, size, tag, init);
}

static inline bool kasan_byte_accessible(const void *addr)
{
	u8 ptr_tag = get_tag(addr);
	u8 mem_tag = hw_get_mem_tag((void *)addr);

	return ptr_tag == KASAN_TAG_KERNEL || ptr_tag == mem_tag;
}

#else /* CONFIG_KASAN_HW_TAGS */

/**
 * kasan_poison - mark the memory range as unaccessible
 * @addr - range start address, must be aligned to KASAN_GRANULE_SIZE
 * @size - range size, must be aligned to KASAN_GRANULE_SIZE
 * @value - value that's written to metadata for the range
 * @init - whether to initialize the memory range (only for hardware tag-based)
 *
 * The size gets aligned to KASAN_GRANULE_SIZE before marking the range.
 */
void kasan_poison(const void *addr, size_t size, u8 value, bool init);

/**
 * kasan_unpoison - mark the memory range as accessible
 * @addr - range start address, must be aligned to KASAN_GRANULE_SIZE
 * @size - range size, can be unaligned
 * @init - whether to initialize the memory range (only for hardware tag-based)
 *
 * For the tag-based modes, the @size gets aligned to KASAN_GRANULE_SIZE before
 * marking the range.
 * For the generic mode, the last granule of the memory range gets partially
 * unpoisoned based on the @size.
 */
void kasan_unpoison(const void *addr, size_t size, bool init);

bool kasan_byte_accessible(const void *addr);

#endif /* CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_KASAN_GENERIC

/**
 * kasan_poison_last_granule - mark the last granule of the memory range as
 * unaccessible
 * @addr - range start address, must be aligned to KASAN_GRANULE_SIZE
 * @size - range size
 *
 * This function is only available for the generic mode, as it's the only mode
 * that has partially poisoned memory granules.
 */
void kasan_poison_last_granule(const void *address, size_t size);

#else /* CONFIG_KASAN_GENERIC */

static inline void kasan_poison_last_granule(const void *address, size_t size) { }

#endif /* CONFIG_KASAN_GENERIC */

/*
 * Exported functions for interfaces called from assembly or from generated
 * code. Declarations here to avoid warning about missing declarations.
 */
asmlinkage void kasan_unpoison_task_stack_below(const void *watermark);
void __asan_register_globals(struct kasan_global *globals, size_t size);
void __asan_unregister_globals(struct kasan_global *globals, size_t size);
void __asan_handle_no_return(void);
void __asan_alloca_poison(unsigned long addr, size_t size);
void __asan_allocas_unpoison(const void *stack_top, const void *stack_bottom);

void __asan_load1(unsigned long addr);
void __asan_store1(unsigned long addr);
void __asan_load2(unsigned long addr);
void __asan_store2(unsigned long addr);
void __asan_load4(unsigned long addr);
void __asan_store4(unsigned long addr);
void __asan_load8(unsigned long addr);
void __asan_store8(unsigned long addr);
void __asan_load16(unsigned long addr);
void __asan_store16(unsigned long addr);
void __asan_loadN(unsigned long addr, size_t size);
void __asan_storeN(unsigned long addr, size_t size);

void __asan_load1_noabort(unsigned long addr);
void __asan_store1_noabort(unsigned long addr);
void __asan_load2_noabort(unsigned long addr);
void __asan_store2_noabort(unsigned long addr);
void __asan_load4_noabort(unsigned long addr);
void __asan_store4_noabort(unsigned long addr);
void __asan_load8_noabort(unsigned long addr);
void __asan_store8_noabort(unsigned long addr);
void __asan_load16_noabort(unsigned long addr);
void __asan_store16_noabort(unsigned long addr);
void __asan_loadN_noabort(unsigned long addr, size_t size);
void __asan_storeN_noabort(unsigned long addr, size_t size);

void __asan_report_load1_noabort(unsigned long addr);
void __asan_report_store1_noabort(unsigned long addr);
void __asan_report_load2_noabort(unsigned long addr);
void __asan_report_store2_noabort(unsigned long addr);
void __asan_report_load4_noabort(unsigned long addr);
void __asan_report_store4_noabort(unsigned long addr);
void __asan_report_load8_noabort(unsigned long addr);
void __asan_report_store8_noabort(unsigned long addr);
void __asan_report_load16_noabort(unsigned long addr);
void __asan_report_store16_noabort(unsigned long addr);
void __asan_report_load_n_noabort(unsigned long addr, size_t size);
void __asan_report_store_n_noabort(unsigned long addr, size_t size);

void __asan_set_shadow_00(const void *addr, size_t size);
void __asan_set_shadow_f1(const void *addr, size_t size);
void __asan_set_shadow_f2(const void *addr, size_t size);
void __asan_set_shadow_f3(const void *addr, size_t size);
void __asan_set_shadow_f5(const void *addr, size_t size);
void __asan_set_shadow_f8(const void *addr, size_t size);

void __hwasan_load1_noabort(unsigned long addr);
void __hwasan_store1_noabort(unsigned long addr);
void __hwasan_load2_noabort(unsigned long addr);
void __hwasan_store2_noabort(unsigned long addr);
void __hwasan_load4_noabort(unsigned long addr);
void __hwasan_store4_noabort(unsigned long addr);
void __hwasan_load8_noabort(unsigned long addr);
void __hwasan_store8_noabort(unsigned long addr);
void __hwasan_load16_noabort(unsigned long addr);
void __hwasan_store16_noabort(unsigned long addr);
void __hwasan_loadN_noabort(unsigned long addr, size_t size);
void __hwasan_storeN_noabort(unsigned long addr, size_t size);

void __hwasan_tag_memory(unsigned long addr, u8 tag, unsigned long size);

#endif
