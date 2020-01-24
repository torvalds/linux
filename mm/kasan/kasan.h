/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_KASAN_KASAN_H
#define __MM_KASAN_KASAN_H

#include <linux/kasan.h>
#include <linux/stackdepot.h>

#define KASAN_SHADOW_SCALE_SIZE (1UL << KASAN_SHADOW_SCALE_SHIFT)
#define KASAN_SHADOW_MASK       (KASAN_SHADOW_SCALE_SIZE - 1)

#define KASAN_TAG_KERNEL	0xFF /* native kernel pointers tag */
#define KASAN_TAG_INVALID	0xFE /* inaccessible memory tag */
#define KASAN_TAG_MAX		0xFD /* maximum value for random tags */

#ifdef CONFIG_KASAN_GENERIC
#define KASAN_FREE_PAGE         0xFF  /* page was freed */
#define KASAN_PAGE_REDZONE      0xFE  /* redzone for kmalloc_large allocations */
#define KASAN_KMALLOC_REDZONE   0xFC  /* redzone inside slub object */
#define KASAN_KMALLOC_FREE      0xFB  /* object was freed (kmem_cache_free/kfree) */
#else
#define KASAN_FREE_PAGE         KASAN_TAG_INVALID
#define KASAN_PAGE_REDZONE      KASAN_TAG_INVALID
#define KASAN_KMALLOC_REDZONE   KASAN_TAG_INVALID
#define KASAN_KMALLOC_FREE      KASAN_TAG_INVALID
#endif

#define KASAN_GLOBAL_REDZONE    0xFA  /* redzone for global variable */
#define KASAN_VMALLOC_INVALID   0xF9  /* unallocated space in vmapped page */

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
	struct kasan_track free_track[KASAN_NR_FREE_STACKS];
#ifdef CONFIG_KASAN_SW_TAGS_IDENTIFY
	u8 free_pointer_tag[KASAN_NR_FREE_STACKS];
	u8 free_track_idx;
#endif
};

struct qlist_node {
	struct qlist_node *next;
};
struct kasan_free_meta {
	/* This field is used while the object is in the quarantine.
	 * Otherwise it might be used for the allocator freelist.
	 */
	struct qlist_node quarantine_link;
};

struct kasan_alloc_meta *get_alloc_info(struct kmem_cache *cache,
					const void *object);
struct kasan_free_meta *get_free_info(struct kmem_cache *cache,
					const void *object);

static inline const void *kasan_shadow_to_mem(const void *shadow_addr)
{
	return (void *)(((unsigned long)shadow_addr - KASAN_SHADOW_OFFSET)
		<< KASAN_SHADOW_SCALE_SHIFT);
}

static inline bool addr_has_shadow(const void *addr)
{
	return (addr >= kasan_shadow_to_mem((void *)KASAN_SHADOW_START));
}

void kasan_poison_shadow(const void *address, size_t size, u8 value);

/**
 * check_memory_region - Check memory region, and report if invalid access.
 * @addr: the accessed address
 * @size: the accessed size
 * @write: true if access is a write access
 * @ret_ip: return address
 * @return: true if access was valid, false if invalid
 */
bool check_memory_region(unsigned long addr, size_t size, bool write,
				unsigned long ret_ip);

void *find_first_bad_addr(void *addr, size_t size);
const char *get_bug_type(struct kasan_access_info *info);

void kasan_report(unsigned long addr, size_t size,
		bool is_write, unsigned long ip);
void kasan_report_invalid_free(void *object, unsigned long ip);

struct page *kasan_addr_to_page(const void *addr);

#if defined(CONFIG_KASAN_GENERIC) && \
	(defined(CONFIG_SLAB) || defined(CONFIG_SLUB))
void quarantine_put(struct kasan_free_meta *info, struct kmem_cache *cache);
void quarantine_reduce(void);
void quarantine_remove_cache(struct kmem_cache *cache);
#else
static inline void quarantine_put(struct kasan_free_meta *info,
				struct kmem_cache *cache) { }
static inline void quarantine_reduce(void) { }
static inline void quarantine_remove_cache(struct kmem_cache *cache) { }
#endif

#ifdef CONFIG_KASAN_SW_TAGS

void print_tags(u8 addr_tag, const void *addr);

u8 random_tag(void);

#else

static inline void print_tags(u8 addr_tag, const void *addr) { }

static inline u8 random_tag(void)
{
	return 0;
}

#endif

#ifndef arch_kasan_set_tag
static inline const void *arch_kasan_set_tag(const void *addr, u8 tag)
{
	return addr;
}
#endif
#ifndef arch_kasan_reset_tag
#define arch_kasan_reset_tag(addr)	((void *)(addr))
#endif
#ifndef arch_kasan_get_tag
#define arch_kasan_get_tag(addr)	0
#endif

#define set_tag(addr, tag)	((void *)arch_kasan_set_tag((addr), (tag)))
#define reset_tag(addr)		((void *)arch_kasan_reset_tag(addr))
#define get_tag(addr)		arch_kasan_get_tag(addr)

/*
 * Exported functions for interfaces called from assembly or from generated
 * code. Declarations here to avoid warning about missing declarations.
 */
asmlinkage void kasan_unpoison_task_stack_below(const void *watermark);
void __asan_register_globals(struct kasan_global *globals, size_t size);
void __asan_unregister_globals(struct kasan_global *globals, size_t size);
void __asan_loadN(unsigned long addr, size_t size);
void __asan_storeN(unsigned long addr, size_t size);
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

void __asan_set_shadow_00(const void *addr, size_t size);
void __asan_set_shadow_f1(const void *addr, size_t size);
void __asan_set_shadow_f2(const void *addr, size_t size);
void __asan_set_shadow_f3(const void *addr, size_t size);
void __asan_set_shadow_f5(const void *addr, size_t size);
void __asan_set_shadow_f8(const void *addr, size_t size);

#endif
