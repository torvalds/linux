// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Richard Henderson
 * Copyright (C) 2001 Rusty Russell, 2002, 2010 Rusty Russell IBM.
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 * Copyright (C) 2024 Mike Rapoport IBM.
 */

#define pr_fmt(fmt) "execmem: " fmt

#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/execmem.h>
#include <linux/maple_tree.h>
#include <linux/set_memory.h>
#include <linux/moduleloader.h>
#include <linux/text-patching.h>

#include <asm/tlbflush.h>

#include "internal.h"

static struct execmem_info *execmem_info __ro_after_init;
static struct execmem_info default_execmem_info __ro_after_init;

#ifdef CONFIG_MMU
static void *execmem_vmalloc(struct execmem_range *range, size_t size,
			     pgprot_t pgprot, unsigned long vm_flags)
{
	bool kasan = range->flags & EXECMEM_KASAN_SHADOW;
	gfp_t gfp_flags = GFP_KERNEL | __GFP_NOWARN;
	unsigned int align = range->alignment;
	unsigned long start = range->start;
	unsigned long end = range->end;
	void *p;

	if (kasan)
		vm_flags |= VM_DEFER_KMEMLEAK;

	if (vm_flags & VM_ALLOW_HUGE_VMAP)
		align = PMD_SIZE;

	p = __vmalloc_node_range(size, align, start, end, gfp_flags,
				 pgprot, vm_flags, NUMA_NO_NODE,
				 __builtin_return_address(0));
	if (!p && range->fallback_start) {
		start = range->fallback_start;
		end = range->fallback_end;
		p = __vmalloc_node_range(size, align, start, end, gfp_flags,
					 pgprot, vm_flags, NUMA_NO_NODE,
					 __builtin_return_address(0));
	}

	if (!p) {
		pr_warn_ratelimited("unable to allocate memory\n");
		return NULL;
	}

	if (kasan && (kasan_alloc_module_shadow(p, size, GFP_KERNEL) < 0)) {
		vfree(p);
		return NULL;
	}

	return p;
}

struct vm_struct *execmem_vmap(size_t size)
{
	struct execmem_range *range = &execmem_info->ranges[EXECMEM_MODULE_DATA];
	struct vm_struct *area;

	area = __get_vm_area_node(size, range->alignment, PAGE_SHIFT, VM_ALLOC,
				  range->start, range->end, NUMA_NO_NODE,
				  GFP_KERNEL, __builtin_return_address(0));
	if (!area && range->fallback_start)
		area = __get_vm_area_node(size, range->alignment, PAGE_SHIFT, VM_ALLOC,
					  range->fallback_start, range->fallback_end,
					  NUMA_NO_NODE, GFP_KERNEL, __builtin_return_address(0));

	return area;
}
#else
static void *execmem_vmalloc(struct execmem_range *range, size_t size,
			     pgprot_t pgprot, unsigned long vm_flags)
{
	return vmalloc(size);
}
#endif /* CONFIG_MMU */

#ifdef CONFIG_ARCH_HAS_EXECMEM_ROX
struct execmem_cache {
	struct mutex mutex;
	struct maple_tree busy_areas;
	struct maple_tree free_areas;
};

static struct execmem_cache execmem_cache = {
	.mutex = __MUTEX_INITIALIZER(execmem_cache.mutex),
	.busy_areas = MTREE_INIT_EXT(busy_areas, MT_FLAGS_LOCK_EXTERN,
				     execmem_cache.mutex),
	.free_areas = MTREE_INIT_EXT(free_areas, MT_FLAGS_LOCK_EXTERN,
				     execmem_cache.mutex),
};

static inline unsigned long mas_range_len(struct ma_state *mas)
{
	return mas->last - mas->index + 1;
}

static int execmem_set_direct_map_valid(struct vm_struct *vm, bool valid)
{
	unsigned int nr = (1 << get_vm_area_page_order(vm));
	unsigned int updated = 0;
	int err = 0;

	for (int i = 0; i < vm->nr_pages; i += nr) {
		err = set_direct_map_valid_noflush(vm->pages[i], nr, valid);
		if (err)
			goto err_restore;
		updated += nr;
	}

	return 0;

err_restore:
	for (int i = 0; i < updated; i += nr)
		set_direct_map_valid_noflush(vm->pages[i], nr, !valid);

	return err;
}

static void execmem_cache_clean(struct work_struct *work)
{
	struct maple_tree *free_areas = &execmem_cache.free_areas;
	struct mutex *mutex = &execmem_cache.mutex;
	MA_STATE(mas, free_areas, 0, ULONG_MAX);
	void *area;

	mutex_lock(mutex);
	mas_for_each(&mas, area, ULONG_MAX) {
		size_t size = mas_range_len(&mas);

		if (IS_ALIGNED(size, PMD_SIZE) &&
		    IS_ALIGNED(mas.index, PMD_SIZE)) {
			struct vm_struct *vm = find_vm_area(area);

			execmem_set_direct_map_valid(vm, true);
			mas_store_gfp(&mas, NULL, GFP_KERNEL);
			vfree(area);
		}
	}
	mutex_unlock(mutex);
}

static DECLARE_WORK(execmem_cache_clean_work, execmem_cache_clean);

static int execmem_cache_add(void *ptr, size_t size)
{
	struct maple_tree *free_areas = &execmem_cache.free_areas;
	struct mutex *mutex = &execmem_cache.mutex;
	unsigned long addr = (unsigned long)ptr;
	MA_STATE(mas, free_areas, addr - 1, addr + 1);
	unsigned long lower, upper;
	void *area = NULL;
	int err;

	lower = addr;
	upper = addr + size - 1;

	mutex_lock(mutex);
	area = mas_walk(&mas);
	if (area && mas.last == addr - 1)
		lower = mas.index;

	area = mas_next(&mas, ULONG_MAX);
	if (area && mas.index == addr + size)
		upper = mas.last;

	mas_set_range(&mas, lower, upper);
	err = mas_store_gfp(&mas, (void *)lower, GFP_KERNEL);
	mutex_unlock(mutex);
	if (err)
		return err;

	return 0;
}

static bool within_range(struct execmem_range *range, struct ma_state *mas,
			 size_t size)
{
	unsigned long addr = mas->index;

	if (addr >= range->start && addr + size < range->end)
		return true;

	if (range->fallback_start &&
	    addr >= range->fallback_start && addr + size < range->fallback_end)
		return true;

	return false;
}

static void *__execmem_cache_alloc(struct execmem_range *range, size_t size)
{
	struct maple_tree *free_areas = &execmem_cache.free_areas;
	struct maple_tree *busy_areas = &execmem_cache.busy_areas;
	MA_STATE(mas_free, free_areas, 0, ULONG_MAX);
	MA_STATE(mas_busy, busy_areas, 0, ULONG_MAX);
	struct mutex *mutex = &execmem_cache.mutex;
	unsigned long addr, last, area_size = 0;
	void *area, *ptr = NULL;
	int err;

	mutex_lock(mutex);
	mas_for_each(&mas_free, area, ULONG_MAX) {
		area_size = mas_range_len(&mas_free);

		if (area_size >= size && within_range(range, &mas_free, size))
			break;
	}

	if (area_size < size)
		goto out_unlock;

	addr = mas_free.index;
	last = mas_free.last;

	/* insert allocated size to busy_areas at range [addr, addr + size) */
	mas_set_range(&mas_busy, addr, addr + size - 1);
	err = mas_store_gfp(&mas_busy, (void *)addr, GFP_KERNEL);
	if (err)
		goto out_unlock;

	mas_store_gfp(&mas_free, NULL, GFP_KERNEL);
	if (area_size > size) {
		void *ptr = (void *)(addr + size);

		/*
		 * re-insert remaining free size to free_areas at range
		 * [addr + size, last]
		 */
		mas_set_range(&mas_free, addr + size, last);
		err = mas_store_gfp(&mas_free, ptr, GFP_KERNEL);
		if (err) {
			mas_store_gfp(&mas_busy, NULL, GFP_KERNEL);
			goto out_unlock;
		}
	}
	ptr = (void *)addr;

out_unlock:
	mutex_unlock(mutex);
	return ptr;
}

static int execmem_cache_populate(struct execmem_range *range, size_t size)
{
	unsigned long vm_flags = VM_ALLOW_HUGE_VMAP;
	struct vm_struct *vm;
	size_t alloc_size;
	int err = -ENOMEM;
	void *p;

	alloc_size = round_up(size, PMD_SIZE);
	p = execmem_vmalloc(range, alloc_size, PAGE_KERNEL, vm_flags);
	if (!p)
		return err;

	vm = find_vm_area(p);
	if (!vm)
		goto err_free_mem;

	/* fill memory with instructions that will trap */
	execmem_fill_trapping_insns(p, alloc_size, /* writable = */ true);

	err = set_memory_rox((unsigned long)p, vm->nr_pages);
	if (err)
		goto err_free_mem;

	err = execmem_cache_add(p, alloc_size);
	if (err)
		goto err_reset_direct_map;

	return 0;

err_reset_direct_map:
	execmem_set_direct_map_valid(vm, true);
err_free_mem:
	vfree(p);
	return err;
}

static void *execmem_cache_alloc(struct execmem_range *range, size_t size)
{
	void *p;
	int err;

	p = __execmem_cache_alloc(range, size);
	if (p)
		return p;

	err = execmem_cache_populate(range, size);
	if (err)
		return NULL;

	return __execmem_cache_alloc(range, size);
}

static bool execmem_cache_free(void *ptr)
{
	struct maple_tree *busy_areas = &execmem_cache.busy_areas;
	struct mutex *mutex = &execmem_cache.mutex;
	unsigned long addr = (unsigned long)ptr;
	MA_STATE(mas, busy_areas, addr, addr);
	size_t size;
	void *area;

	mutex_lock(mutex);
	area = mas_walk(&mas);
	if (!area) {
		mutex_unlock(mutex);
		return false;
	}
	size = mas_range_len(&mas);

	mas_store_gfp(&mas, NULL, GFP_KERNEL);
	mutex_unlock(mutex);

	execmem_fill_trapping_insns(ptr, size, /* writable = */ false);

	execmem_cache_add(ptr, size);

	schedule_work(&execmem_cache_clean_work);

	return true;
}

int execmem_make_temp_rw(void *ptr, size_t size)
{
	unsigned int nr = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long addr = (unsigned long)ptr;
	int ret;

	ret = set_memory_nx(addr, nr);
	if (ret)
		return ret;

	return set_memory_rw(addr, nr);
}

int execmem_restore_rox(void *ptr, size_t size)
{
	unsigned int nr = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long addr = (unsigned long)ptr;

	return set_memory_rox(addr, nr);
}

#else /* CONFIG_ARCH_HAS_EXECMEM_ROX */
static void *execmem_cache_alloc(struct execmem_range *range, size_t size)
{
	return NULL;
}

static bool execmem_cache_free(void *ptr)
{
	return false;
}
#endif /* CONFIG_ARCH_HAS_EXECMEM_ROX */

void *execmem_alloc(enum execmem_type type, size_t size)
{
	struct execmem_range *range = &execmem_info->ranges[type];
	bool use_cache = range->flags & EXECMEM_ROX_CACHE;
	unsigned long vm_flags = VM_FLUSH_RESET_PERMS;
	pgprot_t pgprot = range->pgprot;
	void *p;

	if (use_cache)
		p = execmem_cache_alloc(range, size);
	else
		p = execmem_vmalloc(range, size, pgprot, vm_flags);

	return kasan_reset_tag(p);
}

void execmem_free(void *ptr)
{
	/*
	 * This memory may be RO, and freeing RO memory in an interrupt is not
	 * supported by vmalloc.
	 */
	WARN_ON(in_interrupt());

	if (!execmem_cache_free(ptr))
		vfree(ptr);
}

void *execmem_update_copy(void *dst, const void *src, size_t size)
{
	return text_poke_copy(dst, src, size);
}

bool execmem_is_rox(enum execmem_type type)
{
	return !!(execmem_info->ranges[type].flags & EXECMEM_ROX_CACHE);
}

static bool execmem_validate(struct execmem_info *info)
{
	struct execmem_range *r = &info->ranges[EXECMEM_DEFAULT];

	if (!r->alignment || !r->start || !r->end || !pgprot_val(r->pgprot)) {
		pr_crit("Invalid parameters for execmem allocator, module loading will fail");
		return false;
	}

	if (!IS_ENABLED(CONFIG_ARCH_HAS_EXECMEM_ROX)) {
		for (int i = EXECMEM_DEFAULT; i < EXECMEM_TYPE_MAX; i++) {
			r = &info->ranges[i];

			if (r->flags & EXECMEM_ROX_CACHE) {
				pr_warn_once("ROX cache is not supported\n");
				r->flags &= ~EXECMEM_ROX_CACHE;
			}
		}
	}

	return true;
}

static void execmem_init_missing(struct execmem_info *info)
{
	struct execmem_range *default_range = &info->ranges[EXECMEM_DEFAULT];

	for (int i = EXECMEM_DEFAULT + 1; i < EXECMEM_TYPE_MAX; i++) {
		struct execmem_range *r = &info->ranges[i];

		if (!r->start) {
			if (i == EXECMEM_MODULE_DATA)
				r->pgprot = PAGE_KERNEL;
			else
				r->pgprot = default_range->pgprot;
			r->alignment = default_range->alignment;
			r->start = default_range->start;
			r->end = default_range->end;
			r->flags = default_range->flags;
			r->fallback_start = default_range->fallback_start;
			r->fallback_end = default_range->fallback_end;
		}
	}
}

struct execmem_info * __weak execmem_arch_setup(void)
{
	return NULL;
}

static void __init __execmem_init(void)
{
	struct execmem_info *info = execmem_arch_setup();

	if (!info) {
		info = execmem_info = &default_execmem_info;
		info->ranges[EXECMEM_DEFAULT].start = VMALLOC_START;
		info->ranges[EXECMEM_DEFAULT].end = VMALLOC_END;
		info->ranges[EXECMEM_DEFAULT].pgprot = PAGE_KERNEL_EXEC;
		info->ranges[EXECMEM_DEFAULT].alignment = 1;
	}

	if (!execmem_validate(info))
		return;

	execmem_init_missing(info);

	execmem_info = info;
}

#ifdef CONFIG_ARCH_WANTS_EXECMEM_LATE
static int __init execmem_late_init(void)
{
	__execmem_init();
	return 0;
}
core_initcall(execmem_late_init);
#else
void __init execmem_init(void)
{
	__execmem_init();
}
#endif
