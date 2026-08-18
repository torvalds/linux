// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/cacheflush.h>
#include <linux/err.h>
#include <linux/irq_work.h>
#include "linux/filter.h"
#include <linux/llist.h>
#include <linux/btf_ids.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <asm/tlbflush.h>
#include "range_tree.h"

/*
 * bpf_arena is a sparsely populated shared memory region between bpf program and
 * user space process.
 *
 * For example on x86-64 the values could be:
 * user_vm_start 7f7d26200000     // picked by mmap()
 * kern_vm_start ffffc90001e69000 // picked by get_vm_area()
 * For user space all pointers within the arena are normal 8-byte addresses.
 * In this example 7f7d26200000 is the address of the first page (pgoff=0).
 * The bpf program will access it as: kern_vm_start + lower_32bit_of_user_ptr
 * (u32)7f7d26200000 -> 26200000
 * hence
 * ffffc90001e69000 + 26200000 == ffffc90028069000 is "pgoff=0" within 4Gb
 * kernel memory region.
 *
 * BPF JITs generate the following code to access arena:
 *   mov eax, eax  // eax has lower 32-bit of user pointer
 *   mov word ptr [rax + r12 + off], bx
 * where r12 == kern_vm_start and off is s16.
 * Hence allocate 4Gb + GUARD_SZ/2 on each side.
 *
 * Initially kernel vm_area and user vma are not populated.
 * User space can fault-in any address which will insert the page
 * into kernel and user vma.
 * bpf program can allocate a page via bpf_arena_alloc_pages() kfunc
 * which will insert it into kernel vm_area.
 * The later fault-in from user space will populate that page into user vma.
 */

/* number of bytes addressable by LDX/STX insn with 16-bit 'off' field */
#define GUARD_SZ round_up(1ull << sizeof_field(struct bpf_insn, off) * 8, PAGE_SIZE << 1)
#define KERN_VM_SZ (SZ_4G + GUARD_SZ)

static void arena_free_pages(struct bpf_arena *arena, long uaddr, long page_cnt, bool sleepable);

struct bpf_arena {
	struct bpf_map map;
	u64 user_vm_start;
	u64 user_vm_end;
	struct vm_struct *kern_vm;
	struct page *scratch_page;
	struct range_tree rt;
	/* protects rt */
	rqspinlock_t spinlock;
	struct list_head vma_list;
	/* protects vma_list */
	struct mutex lock;
	u64 zap_gen;
	struct mutex zap_mutex;
	struct irq_work     free_irq;
	struct work_struct  free_work;
	struct llist_head   free_spans;
};

static void arena_free_worker(struct work_struct *work);
static void arena_free_irq(struct irq_work *iw);

struct arena_free_span {
	struct llist_node node;
	unsigned long uaddr;
	u32 page_cnt;
};

u64 bpf_arena_get_kern_vm_start(struct bpf_arena *arena)
{
	return arena ? (u64) (long) arena->kern_vm->addr + GUARD_SZ / 2 : 0;
}

u64 bpf_arena_get_user_vm_start(struct bpf_arena *arena)
{
	return arena ? arena->user_vm_start : 0;
}

/**
 * bpf_arena_map_kern_vm_start - kern_vm_start lookup by struct bpf_map *
 * @map: a BPF_MAP_TYPE_ARENA map
 *
 * Return @map's kern_vm_start.
 */
u64 bpf_arena_map_kern_vm_start(struct bpf_map *map)
{
	return bpf_arena_get_kern_vm_start(container_of(map, struct bpf_arena, map));
}

/**
 * bpf_prog_arena - return the bpf_map of the arena referenced by @prog
 * @prog: a loaded BPF program
 *
 * The verifier enforces at most one arena per program and stores it in
 * prog->aux->arena. Return that arena's underlying bpf_map, or NULL if
 * @prog does not reference an arena.
 */
struct bpf_map *bpf_prog_arena(struct bpf_prog *prog)
{
	struct bpf_arena *arena = prog->aux->arena;

	return arena ? &arena->map : NULL;
}

static long arena_map_peek_elem(struct bpf_map *map, void *value)
{
	return -EOPNOTSUPP;
}

static long arena_map_push_elem(struct bpf_map *map, void *value, u64 flags)
{
	return -EOPNOTSUPP;
}

static long arena_map_pop_elem(struct bpf_map *map, void *value)
{
	return -EOPNOTSUPP;
}

static long arena_map_delete_elem(struct bpf_map *map, void *value)
{
	return -EOPNOTSUPP;
}

static int arena_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	return -EOPNOTSUPP;
}

static long compute_pgoff(struct bpf_arena *arena, long uaddr)
{
	return (u32)(uaddr - (u32)arena->user_vm_start) >> PAGE_SHIFT;
}

struct apply_range_data {
	struct page **pages;
	struct page *scratch_page;
	int i;
};

struct clear_range_data {
	struct llist_head *free_pages;
	struct page *scratch_page;
};

static int apply_range_set_cb(pte_t *pte, unsigned long addr, void *data)
{
	struct apply_range_data *d = data;
	struct page *page;
	pte_t pteval;

	if (!data)
		return 0;

	page = d->pages[d->i];
	/* paranoia, similar to vmap_pages_pte_range() */
	if (WARN_ON_ONCE(!pfn_valid(page_to_pfn(page))))
		return -EINVAL;

	pteval = mk_pte(page, PAGE_KERNEL);
#ifdef ptep_try_set
	/*
	 * Kernel-fault recovery may have installed the scratch page here, and
	 * some architectures (arm64) prohibit valid->valid PTE transitions.
	 * Install atomically into a none slot. If scratch is present, clear it
	 * and flush_tlb_before_set() (break-before-make) before retrying.
	 */
	while (!ptep_try_set(pte, pteval)) {
		pte_t old = ptep_get(pte);

		if (pte_none(old))
			continue;
		if (WARN_ON_ONCE(pte_page(old) != d->scratch_page))
			return -EBUSY;
		ptep_get_and_clear(&init_mm, addr, pte);
		flush_tlb_before_set(addr);
	}
#else
	/*
	 * Without ptep_try_set() there is no atomic installer, but such arches
	 * also do not wire up bpf_arena_handle_page_fault(), so no scratch page
	 * is ever installed and the slot is always none here.
	 */
	if (unlikely(!pte_none(ptep_get(pte))))
		return -EBUSY;
	set_pte_at(&init_mm, addr, pte, pteval);
#endif
	d->i++;
	return 0;
}

static void flush_vmap_cache(unsigned long start, unsigned long size)
{
	flush_cache_vmap(start, start + size);
}

static int apply_range_clear_cb(pte_t *pte, unsigned long addr, void *data)
{
	struct clear_range_data *d = data;
	pte_t old_pte;
	struct page *page;

	/*
	 * Pairs with ptep_try_set() in the kernel-fault scratch installer.
	 * Both sides must be atomic.
	 */
	old_pte = ptep_get_and_clear(&init_mm, addr, pte);
	if (pte_none(old_pte) || !pte_present(old_pte))
		return 0;

	page = pte_page(old_pte);
	if (WARN_ON_ONCE(!page))
		return -EINVAL;

	/*
	 * Skip the per-arena scratch page. A kernel fault on an unallocated uaddr
	 * scratches its PTE. A later bpf_arena_free_pages() over that range walks
	 * here. Without the skip, scratch_page would be freed.
	 */
	if (page == d->scratch_page)
		return 0;

	__llist_add(&page->pcp_llist, d->free_pages);
	return 0;
}

static int apply_range_set_scratch_cb(pte_t *pte, unsigned long addr, void *data)
{
	struct page *scratch_page = data;

	if (!pte_none(ptep_get(pte)))
		return 0;
	/*
	 * Best-effort install. ptep_try_set() returns false only if another
	 * installer (real allocation or concurrent fault) won the cmpxchg.
	 * Their PTE is already valid, so the access retry succeeds.
	 *
	 * No flush_tlb_kernel_range() needed. Stale "not mapped" entries just
	 * cause one extra re-fault through this same path.
	 */
	ptep_try_set(pte, mk_pte(scratch_page, PAGE_KERNEL));
	return 0;
}

static int populate_pgtable_except_pte(struct bpf_arena *arena)
{
	/* Populate intermediates for the recovery range (4 GiB + upper half-guard). */
	return apply_to_page_range(&init_mm, bpf_arena_get_kern_vm_start(arena),
				   SZ_4G + GUARD_SZ / 2, apply_range_set_cb, NULL);
}

static struct bpf_map *arena_map_alloc(union bpf_attr *attr)
{
	struct vm_struct *kern_vm;
	int numa_node = bpf_map_attr_numa_node(attr);
	struct bpf_arena *arena;
	u64 vm_range;
	int err = -ENOMEM;

	if (!bpf_jit_supports_arena())
		return ERR_PTR(-EOPNOTSUPP);

	if (attr->key_size || attr->value_size || attr->max_entries == 0 ||
	    /* BPF_F_MMAPABLE must be set */
	    !(attr->map_flags & BPF_F_MMAPABLE) ||
	    /* No unsupported flags present */
	    (attr->map_flags & ~(BPF_F_SEGV_ON_FAULT | BPF_F_MMAPABLE | BPF_F_NO_USER_CONV)))
		return ERR_PTR(-EINVAL);

	if (attr->map_extra & ~PAGE_MASK)
		/* If non-zero the map_extra is an expected user VMA start address */
		return ERR_PTR(-EINVAL);

	vm_range = (u64)attr->max_entries * PAGE_SIZE;
	if (vm_range > SZ_4G)
		return ERR_PTR(-E2BIG);

	if ((attr->map_extra >> 32) != ((attr->map_extra + vm_range - 1) >> 32))
		/* user vma must not cross 32-bit boundary */
		return ERR_PTR(-ERANGE);

	kern_vm = get_vm_area(KERN_VM_SZ, VM_SPARSE | VM_USERMAP);
	if (!kern_vm)
		return ERR_PTR(-ENOMEM);

	arena = bpf_map_area_alloc(sizeof(*arena), numa_node);
	if (!arena)
		goto err;

	arena->kern_vm = kern_vm;
	arena->user_vm_start = attr->map_extra;
	if (arena->user_vm_start)
		arena->user_vm_end = arena->user_vm_start + vm_range;

	INIT_LIST_HEAD(&arena->vma_list);
	init_llist_head(&arena->free_spans);
	init_irq_work(&arena->free_irq, arena_free_irq);
	INIT_WORK(&arena->free_work, arena_free_worker);
	bpf_map_init_from_attr(&arena->map, attr);

	err = bpf_map_alloc_pages(&arena->map, NUMA_NO_NODE, 1, &arena->scratch_page);
	if (err)
		goto err_free_arena;

	range_tree_init(&arena->rt);
	err = range_tree_set(&arena->rt, 0, attr->max_entries);
	if (err)
		goto err_free_scratch;
	mutex_init(&arena->lock);
	mutex_init(&arena->zap_mutex);
	raw_res_spin_lock_init(&arena->spinlock);
	err = populate_pgtable_except_pte(arena);
	if (err)
		goto err_destroy_rt;

	return &arena->map;

err_destroy_rt:
	range_tree_destroy(&arena->rt);
err_free_scratch:
	__free_page(arena->scratch_page);
err_free_arena:
	bpf_map_area_free(arena);
err:
	free_vm_area(kern_vm);
	return ERR_PTR(err);
}

static int existing_page_cb(pte_t *ptep, unsigned long addr, void *data)
{
	struct bpf_arena *arena = data;
	struct page *page;
	pte_t pte;

	pte = ptep_get(ptep);
	if (!pte_present(pte)) /* sanity check */
		return 0;
	page = pte_page(pte);
	/*
	 * Skip the scratch page. The walk is page-table-driven, not range-tree-driven,
	 * so it can visit scratch PTEs at uaddrs the BPF program never allocated.
	 */
	if (page == arena->scratch_page)
		return 0;
	/*
	 * We do not update pte here:
	 * 1. Nobody should be accessing bpf_arena's range outside of a kernel bug
	 * 2. TLB flushing is batched or deferred. Even if we clear pte,
	 * the TLB entries can stick around and continue to permit access to
	 * the freed page. So it all relies on 1.
	 */
	__free_page(page);
	return 0;
}

static void arena_map_free(struct bpf_map *map)
{
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	/*
	 * Check that user vma-s are not around when bpf map is freed.
	 * mmap() holds vm_file which holds bpf_map refcnt.
	 * munmap() must have happened on vma followed by arena_vm_close()
	 * which would clear arena->vma_list.
	 */
	if (WARN_ON_ONCE(!list_empty(&arena->vma_list)))
		return;

	/* Ensure no pending deferred frees */
	irq_work_sync(&arena->free_irq);
	flush_work(&arena->free_work);

	/*
	 * free_vm_area() calls remove_vm_area() that calls free_unmap_vmap_area().
	 * It unmaps everything from vmalloc area and clears pgtables.
	 * Call apply_to_existing_page_range() first to find populated ptes and
	 * free those pages.
	 */
	apply_to_existing_page_range(&init_mm, bpf_arena_get_kern_vm_start(arena),
				     SZ_4G + GUARD_SZ / 2, existing_page_cb, arena);
	free_vm_area(arena->kern_vm);
	range_tree_destroy(&arena->rt);
	__free_page(arena->scratch_page);
	bpf_map_area_free(arena);
}

static void *arena_map_lookup_elem(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EINVAL);
}

static long arena_map_update_elem(struct bpf_map *map, void *key,
				  void *value, u64 flags)
{
	return -EOPNOTSUPP;
}

static int arena_map_check_btf(struct bpf_map *map, const struct btf *btf,
			       const struct btf_type *key_type, const struct btf_type *value_type)
{
	return 0;
}

static u64 arena_map_mem_usage(const struct bpf_map *map)
{
	return 0;
}

struct vma_list {
	struct vm_area_struct *vma;
	struct list_head head;
	refcount_t mmap_count;
	u64 zap_gen;
};

static int remember_vma(struct bpf_arena *arena, struct vm_area_struct *vma)
{
	struct vma_list *vml;

	vml = kmalloc_obj(*vml);
	if (!vml)
		return -ENOMEM;
	refcount_set(&vml->mmap_count, 1);
	vma->vm_private_data = vml;
	vml->vma = vma;
	vml->zap_gen = 0;
	list_add(&vml->head, &arena->vma_list);
	return 0;
}

static void arena_vm_open(struct vm_area_struct *vma)
{
	struct vma_list *vml = vma->vm_private_data;

	refcount_inc(&vml->mmap_count);
}

static int arena_vm_may_split(struct vm_area_struct *vma, unsigned long addr)
{
	return -EINVAL;
}

static int arena_vm_mremap(struct vm_area_struct *vma)
{
	return -EINVAL;
}

static void arena_vm_close(struct vm_area_struct *vma)
{
	struct bpf_map *map = vma->vm_file->private_data;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);
	struct vma_list *vml = vma->vm_private_data;

	if (!refcount_dec_and_test(&vml->mmap_count))
		return;
	guard(mutex)(&arena->lock);
	/* update link list under lock */
	list_del(&vml->head);
	vma->vm_private_data = NULL;
	kfree(vml);
}

static vm_fault_t arena_vm_fault(struct vm_fault *vmf)
{
	struct bpf_map *map = vmf->vma->vm_file->private_data;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);
	struct mem_cgroup *new_memcg, *old_memcg;
	struct page *page;
	long kbase, kaddr;
	unsigned long flags;
	int ret;

	kbase = bpf_arena_get_kern_vm_start(arena);
	kaddr = kbase + (u32)(vmf->address);

	if (raw_res_spin_lock_irqsave(&arena->spinlock, flags))
		/* Make a reasonable effort to address impossible case */
		return VM_FAULT_RETRY;

	page = vmalloc_to_page((void *)kaddr);
	if (page) {
		if (page == arena->scratch_page)
			/* BPF triggered scratch here; don't lazy-alloc over it */
			goto out_sigsegv;
		/* already have a page vmap-ed */
		goto out;
	}

	bpf_map_memcg_enter(&arena->map, &old_memcg, &new_memcg);

	if (arena->map.map_flags & BPF_F_SEGV_ON_FAULT)
		/* User space requested to segfault when page is not allocated by bpf prog */
		goto out_sigsegv_memcg;

	ret = range_tree_clear(&arena->rt, vmf->pgoff, 1);
	if (ret)
		goto out_sigsegv_memcg;

	struct apply_range_data data = { .pages = &page, .i = 0,
					 .scratch_page = arena->scratch_page };
	/* Account into memcg of the process that created bpf_arena */
	ret = bpf_map_alloc_pages(map, NUMA_NO_NODE, 1, &page);
	if (ret) {
		range_tree_set(&arena->rt, vmf->pgoff, 1);
		goto out_sigsegv_memcg;
	}

	ret = apply_to_page_range(&init_mm, kaddr, PAGE_SIZE, apply_range_set_cb, &data);
	if (ret) {
		range_tree_set(&arena->rt, vmf->pgoff, 1);
		free_pages_nolock(page, 0);
		goto out_sigsegv_memcg;
	}
	flush_vmap_cache(kaddr, PAGE_SIZE);
	bpf_map_memcg_exit(old_memcg, new_memcg);
out:
	page_ref_add(page, 1);
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);
	vmf->page = page;
	return 0;
out_sigsegv_memcg:
	bpf_map_memcg_exit(old_memcg, new_memcg);
out_sigsegv:
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);
	return VM_FAULT_SIGSEGV;
}

static const struct vm_operations_struct arena_vm_ops = {
	.open		= arena_vm_open,
	.may_split	= arena_vm_may_split,
	.mremap		= arena_vm_mremap,
	.close		= arena_vm_close,
	.fault          = arena_vm_fault,
};

static unsigned long arena_get_unmapped_area(struct file *filp, unsigned long addr,
					     unsigned long len, unsigned long pgoff,
					     unsigned long flags)
{
	struct bpf_map *map = filp->private_data;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);
	long ret;

	if (pgoff)
		return -EINVAL;
	if (len > SZ_4G)
		return -E2BIG;

	/* if user_vm_start was specified at arena creation time */
	if (arena->user_vm_start) {
		if (len > arena->user_vm_end - arena->user_vm_start)
			return -E2BIG;
		if (len != arena->user_vm_end - arena->user_vm_start)
			return -EINVAL;
		if (addr != arena->user_vm_start)
			return -EINVAL;
	}

	ret = mm_get_unmapped_area(filp, addr, len * 2, 0, flags);
	if (IS_ERR_VALUE(ret))
		return ret;
	if ((ret >> 32) == ((ret + len - 1) >> 32))
		return ret;
	if (WARN_ON_ONCE(arena->user_vm_start))
		/* checks at map creation time should prevent this */
		return -EFAULT;
	return round_up(ret, SZ_4G);
}

static int arena_map_mmap(struct bpf_map *map, struct vm_area_struct *vma)
{
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	guard(mutex)(&arena->lock);
	if (arena->user_vm_start && arena->user_vm_start != vma->vm_start)
		/*
		 * If map_extra was not specified at arena creation time then
		 * 1st user process can do mmap(NULL, ...) to pick user_vm_start
		 * 2nd user process must pass the same addr to mmap(addr, MAP_FIXED..);
		 *   or
		 * specify addr in map_extra and
		 * use the same addr later with mmap(addr, MAP_FIXED..);
		 */
		return -EBUSY;

	if (arena->user_vm_end && arena->user_vm_end != vma->vm_end)
		/* all user processes must have the same size of mmap-ed region */
		return -EBUSY;

	/* Earlier checks should prevent this */
	if (WARN_ON_ONCE(vma->vm_end - vma->vm_start > SZ_4G || vma->vm_pgoff))
		return -EFAULT;

	if (remember_vma(arena, vma))
		return -ENOMEM;

	arena->user_vm_start = vma->vm_start;
	arena->user_vm_end = vma->vm_end;
	/*
	 * bpf_map_mmap() checks that it's being mmaped as VM_SHARED and
	 * clears VM_MAYEXEC. Set VM_DONTEXPAND to avoid potential change
	 * of user_vm_start. Set VM_DONTCOPY to prevent arena VMA from
	 * being copied into the child process on fork.
	 */
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTCOPY);
	vma->vm_ops = &arena_vm_ops;
	return 0;
}

static int arena_map_direct_value_addr(const struct bpf_map *map, u64 *imm, u32 off)
{
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if ((u64)off >= arena->user_vm_end - arena->user_vm_start)
		return -ERANGE;
	*imm = (unsigned long)arena->user_vm_start;
	return 0;
}

BTF_ID_LIST_SINGLE(bpf_arena_map_btf_ids, struct, bpf_arena)
const struct bpf_map_ops arena_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = arena_map_alloc,
	.map_free = arena_map_free,
	.map_direct_value_addr = arena_map_direct_value_addr,
	.map_mmap = arena_map_mmap,
	.map_get_unmapped_area = arena_get_unmapped_area,
	.map_get_next_key = arena_map_get_next_key,
	.map_push_elem = arena_map_push_elem,
	.map_peek_elem = arena_map_peek_elem,
	.map_pop_elem = arena_map_pop_elem,
	.map_lookup_elem = arena_map_lookup_elem,
	.map_update_elem = arena_map_update_elem,
	.map_delete_elem = arena_map_delete_elem,
	.map_check_btf = arena_map_check_btf,
	.map_mem_usage = arena_map_mem_usage,
	.map_btf_id = &bpf_arena_map_btf_ids[0],
};

static u64 clear_lo32(u64 val)
{
	return val & ~(u64)~0U;
}

/*
 * Allocate pages and vmap them into kernel vmalloc area.
 * Later the pages will be mmaped into user space vma.
 */
static long arena_alloc_pages(struct bpf_arena *arena, long uaddr, long page_cnt, int node_id,
			      bool sleepable)
{
	/* user_vm_end/start are fixed before bpf prog runs */
	long page_cnt_max = (arena->user_vm_end - arena->user_vm_start) >> PAGE_SHIFT;
	u64 kern_vm_start = bpf_arena_get_kern_vm_start(arena);
	struct mem_cgroup *new_memcg, *old_memcg;
	struct apply_range_data data;
	struct page **pages = NULL;
	long remaining, mapped = 0;
	long alloc_pages;
	unsigned long flags;
	long pgoff = 0;
	u32 uaddr32;
	int ret, i;

	if (node_id != NUMA_NO_NODE &&
	    ((unsigned int)node_id >= nr_node_ids || !node_online(node_id)))
		return 0;

	if (page_cnt > page_cnt_max)
		return 0;

	if (uaddr) {
		if (uaddr & ~PAGE_MASK)
			return 0;
		pgoff = compute_pgoff(arena, uaddr);
		if (pgoff > page_cnt_max - page_cnt)
			/* requested address will be outside of user VMA */
			return 0;
	}

	bpf_map_memcg_enter(&arena->map, &old_memcg, &new_memcg);
	/* Cap allocation size to KMALLOC_MAX_CACHE_SIZE so kmalloc_nolock() can succeed. */
	alloc_pages = min(page_cnt, KMALLOC_MAX_CACHE_SIZE / sizeof(struct page *));
	pages = kmalloc_nolock(alloc_pages * sizeof(struct page *), __GFP_ACCOUNT, NUMA_NO_NODE);
	if (!pages) {
		bpf_map_memcg_exit(old_memcg, new_memcg);
		return 0;
	}
	data.pages = pages;
	data.scratch_page = arena->scratch_page;

	if (raw_res_spin_lock_irqsave(&arena->spinlock, flags))
		goto out_free_pages;

	if (uaddr) {
		ret = is_range_tree_set(&arena->rt, pgoff, page_cnt);
		if (ret)
			goto out_unlock_free_pages;
		ret = range_tree_clear(&arena->rt, pgoff, page_cnt);
	} else {
		ret = pgoff = range_tree_find(&arena->rt, page_cnt);
		if (pgoff >= 0)
			ret = range_tree_clear(&arena->rt, pgoff, page_cnt);
	}
	if (ret)
		goto out_unlock_free_pages;

	remaining = page_cnt;
	uaddr32 = (u32)(arena->user_vm_start + pgoff * PAGE_SIZE);

	while (remaining) {
		long this_batch = min(remaining, alloc_pages);

		/* zeroing is needed, since alloc_pages_bulk() only fills in non-zero entries */
		memset(pages, 0, this_batch * sizeof(struct page *));

		ret = bpf_map_alloc_pages(&arena->map, node_id, this_batch, pages);
		if (ret)
			goto out;

		/*
		 * Earlier checks made sure that uaddr32 + page_cnt * PAGE_SIZE - 1
		 * will not overflow 32-bit. Lower 32-bit need to represent
		 * contiguous user address range.
		 * Map these pages at kern_vm_start base.
		 * kern_vm_start + uaddr32 + page_cnt * PAGE_SIZE - 1 can overflow
		 * lower 32-bit and it's ok.
		 */
		data.i = 0;
		ret = apply_to_page_range(&init_mm,
					  kern_vm_start + uaddr32 + (mapped << PAGE_SHIFT),
					  this_batch << PAGE_SHIFT, apply_range_set_cb, &data);
		if (ret) {
			/* data.i pages were mapped, account them and free the remaining */
			mapped += data.i;
			for (i = data.i; i < this_batch; i++)
				free_pages_nolock(pages[i], 0);
			goto out;
		}

		mapped += this_batch;
		remaining -= this_batch;
	}
	flush_vmap_cache(kern_vm_start + uaddr32, mapped << PAGE_SHIFT);
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);
	kfree_nolock(pages);
	bpf_map_memcg_exit(old_memcg, new_memcg);
	return clear_lo32(arena->user_vm_start) + uaddr32;
out:
	range_tree_set(&arena->rt, pgoff + mapped, page_cnt - mapped);
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);
	if (mapped) {
		flush_vmap_cache(kern_vm_start + uaddr32, mapped << PAGE_SHIFT);
		arena_free_pages(arena, uaddr32, mapped, sleepable);
	}
	goto out_free_pages;
out_unlock_free_pages:
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);
out_free_pages:
	kfree_nolock(pages);
	bpf_map_memcg_exit(old_memcg, new_memcg);
	return 0;
}

/*
 * If page is present in vmalloc area, unmap it from vmalloc area,
 * unmap it from all user space vma-s,
 * and free it.
 */
static void zap_pages(struct bpf_arena *arena, long uaddr, long page_cnt)
{
	unsigned long size = (unsigned long)page_cnt << PAGE_SHIFT;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct vma_list *vml;
	unsigned long vm_start;
	u64 my_gen;

	/*
	 * Taking mmap_read_lock() under arena->lock would deadlock against
	 * arena_vm_close(), which runs with mmap_write_lock held and then
	 * acquires arena->lock. Drop arena->lock for mmap_read_lock().
	 *
	 * Use per-call my_gen, recorded in vml->zap_gen, to remember which
	 * vmls this invocation has already processed across the lock drop.
	 * Hold zap_mutex around the whole walk so concurrent zap_pages()
	 * callers cannot overwrite each other's marks on shared vmls --
	 * otherwise call B's mark would make call A skip a vml that A has
	 * not yet zapped for A's uaddr range.
	 */
	mutex_lock(&arena->zap_mutex);
	mutex_lock(&arena->lock);
	my_gen = ++arena->zap_gen;
	for (;;) {
		mm = NULL;
		list_for_each_entry(vml, &arena->vma_list, head) {
			if (vml->zap_gen >= my_gen)
				continue;
			vml->zap_gen = my_gen;
			if (!mmget_not_zero(vml->vma->vm_mm))
				continue;
			mm = vml->vma->vm_mm;
			vm_start = vml->vma->vm_start;
			break;
		}
		if (!mm)
			break;
		mutex_unlock(&arena->lock);

		mmap_read_lock(mm);
		/*
		 * Re-resolve: while we waited the VMA could have been unmapped
		 * and a different mapping installed at the same address.
		 */
		vma = find_vma(mm, vm_start);
		if (vma && vma->vm_start == vm_start &&
		    vma->vm_file && vma->vm_file->private_data == &arena->map)
			zap_vma_range(vma, uaddr, size);
		mmap_read_unlock(mm);
		mmput(mm);

		mutex_lock(&arena->lock);
	}
	mutex_unlock(&arena->lock);
	mutex_unlock(&arena->zap_mutex);
}

static void arena_free_pages(struct bpf_arena *arena, long uaddr, long page_cnt, bool sleepable)
{
	struct mem_cgroup *new_memcg, *old_memcg;
	u64 full_uaddr, uaddr_end;
	long kaddr, pgoff;
	struct page *page;
	struct llist_head free_pages;
	struct llist_node *pos, *t;
	struct arena_free_span *s;
	struct clear_range_data cdata;
	unsigned long flags;
	int ret = 0;

	/* only aligned lower 32-bit are relevant */
	uaddr = (u32)uaddr;
	uaddr &= PAGE_MASK;
	kaddr = bpf_arena_get_kern_vm_start(arena) + uaddr;
	full_uaddr = clear_lo32(arena->user_vm_start) + uaddr;
	uaddr_end = min(arena->user_vm_end, full_uaddr + (page_cnt << PAGE_SHIFT));
	if (full_uaddr >= uaddr_end)
		return;

	page_cnt = (uaddr_end - full_uaddr) >> PAGE_SHIFT;
	pgoff = compute_pgoff(arena, uaddr);
	bpf_map_memcg_enter(&arena->map, &old_memcg, &new_memcg);

	if (!sleepable)
		goto defer;

	ret = raw_res_spin_lock_irqsave(&arena->spinlock, flags);

	/* Can't proceed without holding the spinlock so defer the free */
	if (ret)
		goto defer;

	range_tree_set(&arena->rt, pgoff, page_cnt);

	init_llist_head(&free_pages);
	cdata.free_pages = &free_pages;
	cdata.scratch_page = arena->scratch_page;
	/* clear ptes and collect struct pages */
	apply_to_existing_page_range(&init_mm, kaddr, page_cnt << PAGE_SHIFT,
				     apply_range_clear_cb, &cdata);

	/* drop the lock to do the tlb flush and zap pages */
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);

	/* ensure no stale TLB entries */
	flush_tlb_kernel_range(kaddr, kaddr + (page_cnt * PAGE_SIZE));

	if (page_cnt > 1)
		/* bulk zap if multiple pages being freed */
		zap_pages(arena, full_uaddr, page_cnt);

	llist_for_each_safe(pos, t, __llist_del_all(&free_pages)) {
		page = llist_entry(pos, struct page, pcp_llist);
		if (page_cnt == 1 && page_ref_count(page) > 1) /* maybe mapped by user space */
			/* Optimization for the common case of page_cnt==1:
			 * If page wasn't mapped into some user vma there
			 * is no need to call zap_pages which is slow. When
			 * page_cnt is big it's faster to do the batched zap.
			 */
			zap_pages(arena, full_uaddr, 1);
		__free_page(page);
	}
	bpf_map_memcg_exit(old_memcg, new_memcg);

	return;

defer:
	s = kmalloc_nolock(sizeof(struct arena_free_span), __GFP_ACCOUNT, -1);
	bpf_map_memcg_exit(old_memcg, new_memcg);
	if (!s)
		/*
		 * If allocation fails in non-sleepable context, pages are intentionally left
		 * inaccessible (leaked) until the arena is destroyed. Cleanup or retries are not
		 * possible here, so we intentionally omit them for safety.
		 */
		return;

	s->page_cnt = page_cnt;
	s->uaddr = uaddr;
	llist_add(&s->node, &arena->free_spans);
	irq_work_queue(&arena->free_irq);
}

/*
 * Reserve an arena virtual address range without populating it. This call stops
 * bpf_arena_alloc_pages from adding pages to this range.
 */
static int arena_reserve_pages(struct bpf_arena *arena, long uaddr, u32 page_cnt)
{
	long page_cnt_max = (arena->user_vm_end - arena->user_vm_start) >> PAGE_SHIFT;
	struct mem_cgroup *new_memcg, *old_memcg;
	unsigned long flags;
	long pgoff;
	int ret;

	if (uaddr & ~PAGE_MASK)
		return 0;

	pgoff = compute_pgoff(arena, uaddr);
	if (pgoff + page_cnt > page_cnt_max)
		return -EINVAL;

	if (raw_res_spin_lock_irqsave(&arena->spinlock, flags))
		return -EBUSY;

	/* Cannot guard already allocated pages. */
	ret = is_range_tree_set(&arena->rt, pgoff, page_cnt);
	if (ret) {
		ret = -EBUSY;
		goto out;
	}

	/* "Allocate" the region to prevent it from being allocated. */
	bpf_map_memcg_enter(&arena->map, &old_memcg, &new_memcg);
	ret = range_tree_clear(&arena->rt, pgoff, page_cnt);
	bpf_map_memcg_exit(old_memcg, new_memcg);
out:
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);
	return ret;
}

static void arena_free_worker(struct work_struct *work)
{
	struct bpf_arena *arena = container_of(work, struct bpf_arena, free_work);
	struct mem_cgroup *new_memcg, *old_memcg;
	struct llist_node *list, *pos, *t;
	struct arena_free_span *s;
	u64 arena_vm_start, user_vm_start;
	struct llist_head free_pages;
	struct clear_range_data cdata;
	struct page *page;
	unsigned long full_uaddr;
	long kaddr, page_cnt, pgoff;
	unsigned long flags;

	if (raw_res_spin_lock_irqsave(&arena->spinlock, flags)) {
		schedule_work(work);
		return;
	}

	bpf_map_memcg_enter(&arena->map, &old_memcg, &new_memcg);

	init_llist_head(&free_pages);
	cdata.free_pages = &free_pages;
	cdata.scratch_page = arena->scratch_page;
	arena_vm_start = bpf_arena_get_kern_vm_start(arena);
	user_vm_start = bpf_arena_get_user_vm_start(arena);

	list = llist_del_all(&arena->free_spans);
	llist_for_each(pos, list) {
		s = llist_entry(pos, struct arena_free_span, node);
		page_cnt = s->page_cnt;
		kaddr = arena_vm_start + s->uaddr;
		pgoff = compute_pgoff(arena, s->uaddr);

		/* clear ptes and collect pages in free_pages llist */
		apply_to_existing_page_range(&init_mm, kaddr, page_cnt << PAGE_SHIFT,
					     apply_range_clear_cb, &cdata);

		range_tree_set(&arena->rt, pgoff, page_cnt);
	}
	raw_res_spin_unlock_irqrestore(&arena->spinlock, flags);

	/* Iterate the list again without holding spinlock to do the tlb flush and zap_pages */
	llist_for_each_safe(pos, t, list) {
		s = llist_entry(pos, struct arena_free_span, node);
		page_cnt = s->page_cnt;
		full_uaddr = clear_lo32(user_vm_start) + s->uaddr;
		kaddr = arena_vm_start + s->uaddr;

		/* ensure no stale TLB entries */
		flush_tlb_kernel_range(kaddr, kaddr + (page_cnt * PAGE_SIZE));

		/* remove pages from user vmas */
		zap_pages(arena, full_uaddr, page_cnt);

		kfree_nolock(s);
	}

	/* free all pages collected by apply_to_existing_page_range() in the first loop */
	llist_for_each_safe(pos, t, __llist_del_all(&free_pages)) {
		page = llist_entry(pos, struct page, pcp_llist);
		__free_page(page);
	}

	bpf_map_memcg_exit(old_memcg, new_memcg);
}

static void arena_free_irq(struct irq_work *iw)
{
	struct bpf_arena *arena = container_of(iw, struct bpf_arena, free_irq);

	schedule_work(&arena->free_work);
}

__bpf_kfunc_start_defs();

__bpf_kfunc void *bpf_arena_alloc_pages(void *p__map, void *addr__ign, u32 page_cnt,
					int node_id, u64 flags)
{
	struct bpf_map *map = p__map;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if (map->map_type != BPF_MAP_TYPE_ARENA || flags || !page_cnt)
		return NULL;

	return (void *)arena_alloc_pages(arena, (long)addr__ign, page_cnt, node_id, true);
}

void *bpf_arena_alloc_pages_non_sleepable(void *p__map, void *addr__ign, u32 page_cnt,
					  int node_id, u64 flags)
{
	struct bpf_map *map = p__map;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if (map->map_type != BPF_MAP_TYPE_ARENA || flags || !page_cnt)
		return NULL;

	return (void *)arena_alloc_pages(arena, (long)addr__ign, page_cnt, node_id, false);
}

void *bpf_arena_alloc_pages_sleepable(void *p__map, void *addr__ign, u32 page_cnt,
				      int node_id, u64 flags)
{
	struct bpf_map *map = p__map;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if (map->map_type != BPF_MAP_TYPE_ARENA || flags || !page_cnt)
		return NULL;

	return (void *)arena_alloc_pages(arena, (long)addr__ign, page_cnt, node_id, true);
}

__bpf_kfunc void bpf_arena_free_pages(void *p__map, void *ptr__ign, u32 page_cnt)
{
	struct bpf_map *map = p__map;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if (map->map_type != BPF_MAP_TYPE_ARENA || !page_cnt || !ptr__ign)
		return;
	arena_free_pages(arena, (long)ptr__ign, page_cnt, true);
}

void bpf_arena_free_pages_non_sleepable(void *p__map, void *ptr__ign, u32 page_cnt)
{
	struct bpf_map *map = p__map;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if (map->map_type != BPF_MAP_TYPE_ARENA || !page_cnt || !ptr__ign)
		return;
	arena_free_pages(arena, (long)ptr__ign, page_cnt, false);
}

__bpf_kfunc int bpf_arena_reserve_pages(void *p__map, void *ptr__ign, u32 page_cnt)
{
	struct bpf_map *map = p__map;
	struct bpf_arena *arena = container_of(map, struct bpf_arena, map);

	if (map->map_type != BPF_MAP_TYPE_ARENA)
		return -EINVAL;

	if (!page_cnt)
		return 0;

	return arena_reserve_pages(arena, (long)ptr__ign, page_cnt);
}
__bpf_kfunc_end_defs();

BTF_KFUNCS_START(arena_kfuncs)
BTF_ID_FLAGS(func, bpf_arena_alloc_pages, KF_ARENA_RET | KF_ARENA_ARG2)
BTF_ID_FLAGS(func, bpf_arena_free_pages, KF_ARENA_ARG2)
BTF_ID_FLAGS(func, bpf_arena_reserve_pages, KF_ARENA_ARG2)
BTF_KFUNCS_END(arena_kfuncs)

static const struct btf_kfunc_id_set common_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &arena_kfuncs,
};

static int __init kfunc_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_UNSPEC, &common_kfunc_set);
}
late_initcall(kfunc_init);

static void __bpf_prog_report_arena_violation(struct bpf_prog *prog, bool write,
					      unsigned long addr, unsigned long fault_ip)
{
	struct bpf_stream_stage ss;
	u64 user_vm_start;

	/* Use main prog for stream access */
	prog = prog->aux->main_prog_aux->prog;

	user_vm_start = bpf_arena_get_user_vm_start(prog->aux->arena);
	addr += clear_lo32(user_vm_start);

	bpf_stream_stage(ss, prog, BPF_STDERR, ({
		bpf_stream_printk(ss, "ERROR: Arena %s access at unmapped address 0x%lx\n",
				  write ? "WRITE" : "READ", addr);
		bpf_stream_dump_stack(ss);
	}));
}

bool bpf_arena_handle_page_fault(unsigned long addr, bool is_write, unsigned long fault_ip)
{
	struct bpf_arena *arena;
	struct bpf_prog *prog;
	unsigned long kbase;
	unsigned long page_addr = addr & PAGE_MASK;

	prog = bpf_prog_find_from_stack();
	if (!prog)
		return false;

	arena = prog->aux->arena;
	/* a prog not using arena may be on stack, so arena can be NULL */
	if (!arena)
		return false;

	kbase = bpf_arena_get_kern_vm_start(arena);

	/*
	 * Recovery covers the 4 GiB mappable band plus the upper half-guard.
	 * Lower guard is unreachable from kfuncs; an address there indicates
	 * a different bug class - leave it to the regular kernel oops path.
	 */
	if (page_addr < kbase || page_addr >= kbase + SZ_4G + GUARD_SZ / 2)
		return false;

	apply_to_page_range(&init_mm, page_addr, PAGE_SIZE,
			    apply_range_set_scratch_cb, arena->scratch_page);
	flush_vmap_cache(page_addr, PAGE_SIZE);
	__bpf_prog_report_arena_violation(prog, is_write, page_addr - kbase, fault_ip);
	return true;
}

void bpf_prog_report_arena_violation(bool write, unsigned long addr, unsigned long fault_ip)
{
	struct bpf_prog *prog;

	/*
	 * The RCU read lock is held to safely traverse the latch tree, but we
	 * don't need its protection when accessing the prog, since it will not
	 * disappear while we are handling the fault.
	 */
	rcu_read_lock();
	prog = bpf_prog_ksym_find(fault_ip);
	rcu_read_unlock();
	if (!prog)
		return;
	__bpf_prog_report_arena_violation(prog, write, addr, fault_ip);
}
