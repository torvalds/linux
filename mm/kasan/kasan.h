/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_KASAN_KASAN_H
#define __MM_KASAN_KASAN_H

#include <linux/atomic.h>
#include <linux/kasan.h>
#include <linux/kasan-tags.h>
#include <linux/kfence.h>
#include <linux/stackdepot.h>

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)

#include <linux/static_key.h>

DECLARE_STATIC_KEY_TRUE(kasan_flag_stacktrace);

static inline bool kasan_stack_collection_enabled(void)
{
	return static_branch_unlikely(&kasan_flag_stacktrace);
}

#else /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

static inline bool kasan_stack_collection_enabled(void)
{
	return true;
}

#endif /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_KASAN_HW_TAGS

#include "../slab.h"

DECLARE_STATIC_KEY_TRUE(kasan_flag_vmalloc);

enum kasan_mode {
	KASAN_MODE_SYNC,
	KASAN_MODE_ASYNC,
	KASAN_MODE_ASYMM,
};

extern enum kasan_mode kasan_mode __ro_after_init;

extern unsigned long kasan_page_alloc_sample;
extern unsigned int kasan_page_alloc_sample_order;
DECLARE_PER_CPU(long, kasan_page_alloc_skip);

static inline bool kasan_vmalloc_enabled(void)
{
	return static_branch_likely(&kasan_flag_vmalloc);
}

static inline bool kasan_async_fault_possible(void)
{
	return kasan_mode == KASAN_MODE_ASYNC || kasan_mode == KASAN_MODE_ASYMM;
}

static inline bool kasan_sync_fault_possible(void)
{
	return kasan_mode == KASAN_MODE_SYNC || kasan_mode == KASAN_MODE_ASYMM;
}

static inline bool kasan_sample_page_alloc(unsigned int order)
{
	/* Fast-path for when sampling is disabled. */
	if (kasan_page_alloc_sample == 1)
		return true;

	if (order < kasan_page_alloc_sample_order)
		return true;

	if (this_cpu_dec_return(kasan_page_alloc_skip) < 0) {
		this_cpu_write(kasan_page_alloc_skip,
			       kasan_page_alloc_sample - 1);
		return true;
	}

	return false;
}

#else /* CONFIG_KASAN_HW_TAGS */

static inline bool kasan_async_fault_possible(void)
{
	return false;
}

static inline bool kasan_sync_fault_possible(void)
{
	return true;
}

static inline bool kasan_sample_page_alloc(unsigned int order)
{
	return true;
}

#endif /* CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_KASAN_GENERIC

/* Generic KASAN uses per-object metadata to store stack traces. */
static inline bool kasan_requires_meta(void)
{
	/*
	 * Technically, Generic KASAN always collects stack traces right now.
	 * However, let's use kasan_stack_collection_enabled() in case the
	 * kasan.stacktrace command-line argument is changed to affect
	 * Generic KASAN.
	 */
	return kasan_stack_collection_enabled();
}

#else /* CONFIG_KASAN_GENERIC */

/* Tag-based KASAN modes do not use per-object metadata. */
static inline bool kasan_requires_meta(void)
{
	return false;
}

#endif /* CONFIG_KASAN_GENERIC */

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
#define KASAN_GRANULE_SIZE	(1UL << KASAN_SHADOW_SCALE_SHIFT)
#else
#include <asm/mte-kasan.h>
#define KASAN_GRANULE_SIZE	MTE_GRANULE_SIZE
#endif

#define KASAN_GRANULE_MASK	(KASAN_GRANULE_SIZE - 1)

#define KASAN_MEMORY_PER_SHADOW_PAGE	(KASAN_GRANULE_SIZE << PAGE_SHIFT)

#ifdef CONFIG_KASAN_GENERIC
#define KASAN_PAGE_FREE		0xFF  /* freed page */
#define KASAN_PAGE_REDZONE	0xFE  /* redzone for kmalloc_large allocation */
#define KASAN_SLAB_REDZONE	0xFC  /* redzone for slab object */
#define KASAN_SLAB_FREE		0xFB  /* freed slab object */
#define KASAN_VMALLOC_INVALID	0xF8  /* inaccessible space in vmap area */
#else
#define KASAN_PAGE_FREE		KASAN_TAG_INVALID
#define KASAN_PAGE_REDZONE	KASAN_TAG_INVALID
#define KASAN_SLAB_REDZONE	KASAN_TAG_INVALID
#define KASAN_SLAB_FREE		KASAN_TAG_INVALID
#define KASAN_VMALLOC_INVALID	KASAN_TAG_INVALID /* only used for SW_TAGS */
#endif

#ifdef CONFIG_KASAN_GENERIC

#define KASAN_SLAB_FREETRACK	0xFA  /* freed slab object with free track */
#define KASAN_GLOBAL_REDZONE	0xF9  /* redzone for global variable */

/* Stack redzone shadow values. Compiler ABI, do not change. */
#define KASAN_STACK_LEFT	0xF1
#define KASAN_STACK_MID		0xF2
#define KASAN_STACK_RIGHT	0xF3
#define KASAN_STACK_PARTIAL	0xF4

/* alloca redzone shadow values. */
#define KASAN_ALLOCA_LEFT	0xCA
#define KASAN_ALLOCA_RIGHT	0xCB

/* alloca redzone size. Compiler ABI, do not change. */
#define KASAN_ALLOCA_REDZONE_SIZE	32

/* Stack frame marker. Compiler ABI, do not change. */
#define KASAN_CURRENT_STACK_FRAME_MAGIC 0x41B58AB3

/* Dummy value to avoid breaking randconfig/all*config builds. */
#ifndef KASAN_ABI_VERSION
#define KASAN_ABI_VERSION 1
#endif

#endif /* CONFIG_KASAN_GENERIC */

/* Metadata layout customization. */
#define META_BYTES_PER_BLOCK 1
#define META_BLOCKS_PER_ROW 16
#define META_BYTES_PER_ROW (META_BLOCKS_PER_ROW * META_BYTES_PER_BLOCK)
#define META_MEM_BYTES_PER_ROW (META_BYTES_PER_ROW * KASAN_GRANULE_SIZE)
#define META_ROWS_AROUND_ADDR 2

#define KASAN_STACK_DEPTH 64

struct kasan_track {
	u32 pid;
	depot_stack_handle_t stack;
};

enum kasan_report_type {
	KASAN_REPORT_ACCESS,
	KASAN_REPORT_INVALID_FREE,
	KASAN_REPORT_DOUBLE_FREE,
};

struct kasan_report_info {
	/* Filled in by kasan_report_*(). */
	enum kasan_report_type type;
	void *access_addr;
	size_t access_size;
	bool is_write;
	unsigned long ip;

	/* Filled in by the common reporting code. */
	void *first_bad_addr;
	struct kmem_cache *cache;
	void *object;

	/* Filled in by the mode-specific reporting code. */
	const char *bug_type;
	struct kasan_track alloc_track;
	struct kasan_track free_track;
};

/* Do not change the struct layout: compiler ABI. */
struct kasan_source_location {
	const char *filename;
	int line_no;
	int column_no;
};

/* Do not change the struct layout: compiler ABI. */
struct kasan_global {
	const void *beg;		/* Address of the beginning of the global variable. */
	size_t size;			/* Size of the global variable. */
	size_t size_with_redzone;	/* Size of the variable + size of the redzone. 32 bytes aligned. */
	const void *name;
	const void *module_name;	/* Name of the module where the global variable is declared. */
	unsigned long has_dynamic_init;	/* This is needed for C++. */
#if KASAN_ABI_VERSION >= 4
	struct kasan_source_location *location;
#endif
#if KASAN_ABI_VERSION >= 5
	char *odr_indicator;
#endif
};

/* Structures for keeping alloc and free meta. */

#ifdef CONFIG_KASAN_GENERIC

struct kasan_alloc_meta {
	struct kasan_track alloc_track;
	/* Free track is stored in kasan_free_meta. */
	depot_stack_handle_t aux_stack[2];
};

struct qlist_node {
	struct qlist_node *next;
};

/*
 * Free meta is stored either in the object itself or in the redzone after the
 * object. In the former case, free meta offset is 0. In the latter case, the
 * offset is between 0 and INT_MAX. INT_MAX marks that free meta is not present.
 */
#define KASAN_NO_FREE_META INT_MAX

/*
 * Free meta is only used by Generic mode while the object is in quarantine.
 * After that, slab allocator stores the freelist pointer in the object.
 */
struct kasan_free_meta {
	struct qlist_node quarantine_link;
	struct kasan_track free_track;
};

#endif /* CONFIG_KASAN_GENERIC */

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)

struct kasan_stack_ring_entry {
	void *ptr;
	size_t size;
	u32 pid;
	depot_stack_handle_t stack;
	bool is_free;
};

struct kasan_stack_ring {
	rwlock_t lock;
	size_t size;
	atomic64_t pos;
	struct kasan_stack_ring_entry *entries;
};

#endif /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

#if IS_ENABLED(CONFIG_KASAN_KUNIT_TEST)
/* Used in KUnit-compatible KASAN tests. */
struct kunit_kasan_status {
	bool report_found;
	bool sync_fault;
};
#endif

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

static inline const void *kasan_shadow_to_mem(const void *shadow_addr)
{
	return (void *)(((unsigned long)shadow_addr - KASAN_SHADOW_OFFSET)
		<< KASAN_SHADOW_SCALE_SHIFT);
}

static inline bool addr_has_metadata(const void *addr)
{
	return (kasan_reset_tag(addr) >=
		kasan_shadow_to_mem((void *)KASAN_SHADOW_START));
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

void *kasan_find_first_bad_addr(void *addr, size_t size);
void kasan_complete_mode_report_info(struct kasan_report_info *info);
void kasan_metadata_fetch_row(char *buffer, void *row);

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)
void kasan_print_tags(u8 addr_tag, const void *addr);
#else
static inline void kasan_print_tags(u8 addr_tag, const void *addr) { }
#endif

#if defined(CONFIG_KASAN_STACK)
void kasan_print_address_stack_frame(const void *addr);
#else
static inline void kasan_print_address_stack_frame(const void *addr) { }
#endif

#ifdef CONFIG_KASAN_GENERIC
void kasan_print_aux_stacks(struct kmem_cache *cache, const void *object);
#else
static inline void kasan_print_aux_stacks(struct kmem_cache *cache, const void *object) { }
#endif

bool kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip);
void kasan_report_invalid_free(void *object, unsigned long ip, enum kasan_report_type type);

struct slab *kasan_addr_to_slab(const void *addr);

#ifdef CONFIG_KASAN_GENERIC
void kasan_init_cache_meta(struct kmem_cache *cache, unsigned int *size);
void kasan_init_object_meta(struct kmem_cache *cache, const void *object);
struct kasan_alloc_meta *kasan_get_alloc_meta(struct kmem_cache *cache,
						const void *object);
struct kasan_free_meta *kasan_get_free_meta(struct kmem_cache *cache,
						const void *object);
#else
static inline void kasan_init_cache_meta(struct kmem_cache *cache, unsigned int *size) { }
static inline void kasan_init_object_meta(struct kmem_cache *cache, const void *object) { }
#endif

depot_stack_handle_t kasan_save_stack(gfp_t flags, bool can_alloc);
void kasan_set_track(struct kasan_track *track, gfp_t flags);
void kasan_save_alloc_info(struct kmem_cache *cache, void *object, gfp_t flags);
void kasan_save_free_info(struct kmem_cache *cache, void *object);

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

#define hw_enable_tagging_sync()		arch_enable_tagging_sync()
#define hw_enable_tagging_async()		arch_enable_tagging_async()
#define hw_enable_tagging_asymm()		arch_enable_tagging_asymm()
#define hw_force_async_tag_fault()		arch_force_async_tag_fault()
#define hw_get_random_tag()			arch_get_random_tag()
#define hw_get_mem_tag(addr)			arch_get_mem_tag(addr)
#define hw_set_mem_tag_range(addr, size, tag, init) \
			arch_set_mem_tag_range((addr), (size), (tag), (init))

void kasan_enable_tagging(void);

#else /* CONFIG_KASAN_HW_TAGS */

static inline void kasan_enable_tagging(void) { }

#endif /* CONFIG_KASAN_HW_TAGS */

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)
void __init kasan_init_tags(void);
#endif /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

#if defined(CONFIG_KASAN_HW_TAGS) && IS_ENABLED(CONFIG_KASAN_KUNIT_TEST)

void kasan_force_async_fault(void);

#else /* CONFIG_KASAN_HW_TAGS && CONFIG_KASAN_KUNIT_TEST */

static inline void kasan_force_async_fault(void) { }

#endif /* CONFIG_KASAN_HW_TAGS && CONFIG_KASAN_KUNIT_TEST */

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
	 * avoid overwriting the slab redzone. This disables initialization in
	 * the arch code and may thus lead to performance penalty. This penalty
	 * does not affect production builds, as slab redzones are not enabled
	 * there.
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
 * kasan_poison - mark the memory range as inaccessible
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
 * inaccessible
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

#ifndef kasan_arch_is_ready
static inline bool kasan_arch_is_ready(void)	{ return true; }
#elif !defined(CONFIG_KASAN_GENERIC) || !defined(CONFIG_KASAN_OUTLINE)
#error kasan_arch_is_ready only works in KASAN generic outline mode!
#endif

#if IS_ENABLED(CONFIG_KASAN_KUNIT_TEST) || IS_ENABLED(CONFIG_KASAN_MODULE_TEST)

bool kasan_save_enable_multi_shot(void);
void kasan_restore_multi_shot(bool enabled);

#endif

/*
 * Exported functions for interfaces called from assembly or from generated
 * code. Declared here to avoid warnings about missing declarations.
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

#endif /* __MM_KASAN_KASAN_H */
