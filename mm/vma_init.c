// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Functions for initialisaing, allocating, freeing and duplicating VMAs. Shared
 * between CONFIG_MMU and non-CONFIG_MMU kernel configurations.
 */

#include "vma_internal.h"
#include "vma.h"

/* SLAB cache for vm_area_struct structures */
static struct kmem_cache *vm_area_cachep;

void __init vma_state_init(void)
{
	struct kmem_cache_args args = {
		.use_freeptr_offset = true,
		.freeptr_offset = offsetof(struct vm_area_struct, vm_freeptr),
	};

	vm_area_cachep = kmem_cache_create("vm_area_struct",
			sizeof(struct vm_area_struct), &args,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_TYPESAFE_BY_RCU|
			SLAB_ACCOUNT);
}

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!vma)
		return NULL;

	vma_init(vma, mm);

	return vma;
}

static void vm_area_init_from(const struct vm_area_struct *src,
			      struct vm_area_struct *dest)
{
	dest->vm_mm = src->vm_mm;
	dest->vm_ops = src->vm_ops;
	dest->vm_start = src->vm_start;
	dest->vm_end = src->vm_end;
	dest->anon_vma = src->anon_vma;
	dest->vm_pgoff = src->vm_pgoff;
	dest->vm_file = src->vm_file;
	dest->vm_private_data = src->vm_private_data;
	vm_flags_init(dest, src->vm_flags);
	memcpy(&dest->vm_page_prot, &src->vm_page_prot,
	       sizeof(dest->vm_page_prot));
	/*
	 * src->shared.rb may be modified concurrently when called from
	 * dup_mmap(), but the clone will reinitialize it.
	 */
	data_race(memcpy(&dest->shared, &src->shared, sizeof(dest->shared)));
	memcpy(&dest->vm_userfaultfd_ctx, &src->vm_userfaultfd_ctx,
	       sizeof(dest->vm_userfaultfd_ctx));
#ifdef CONFIG_ANON_VMA_NAME
	dest->anon_name = src->anon_name;
#endif
#ifdef CONFIG_SWAP
	memcpy(&dest->swap_readahead_info, &src->swap_readahead_info,
	       sizeof(dest->swap_readahead_info));
#endif
#ifndef CONFIG_MMU
	dest->vm_region = src->vm_region;
#endif
#ifdef CONFIG_NUMA
	dest->vm_policy = src->vm_policy;
#endif
}

struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig)
{
	struct vm_area_struct *new = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);

	if (!new)
		return NULL;

	ASSERT_EXCLUSIVE_WRITER(orig->vm_flags);
	ASSERT_EXCLUSIVE_WRITER(orig->vm_file);
	vm_area_init_from(orig, new);
	vma_lock_init(new, true);
	INIT_LIST_HEAD(&new->anon_vma_chain);
	vma_numab_state_init(new);
	dup_anon_vma_name(orig, new);

	return new;
}

void vm_area_free(struct vm_area_struct *vma)
{
	/* The vma should be detached while being destroyed. */
	vma_assert_detached(vma);
	vma_numab_state_free(vma);
	free_anon_vma_name(vma);
	kmem_cache_free(vm_area_cachep, vma);
}
