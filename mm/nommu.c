/*
 *  linux/mm/nommu.c
 *
 *  Replacement code for mm functions to support CPU's that don't
 *  have any form of memory management unit (thus no virtual memory).
 *
 *  See Documentation/nommu-mmap.txt
 *
 *  Copyright (c) 2004-2008 David Howells <dhowells@redhat.com>
 *  Copyright (c) 2000-2003 David McCullough <davidm@snapgear.com>
 *  Copyright (c) 2000-2001 D Jeff Dionne <jeff@uClinux.org>
 *  Copyright (c) 2002      Greg Ungerer <gerg@snapgear.com>
 *  Copyright (c) 2007-2010 Paul Mundt <lethal@linux-sh.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/vmacache.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/compiler.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/printk.h>

#include <linux/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include "internal.h"

void *high_memory;
EXPORT_SYMBOL(high_memory);
struct page *mem_map;
unsigned long max_mapnr;
EXPORT_SYMBOL(max_mapnr);
unsigned long highest_memmap_pfn;
int sysctl_nr_trim_pages = CONFIG_NOMMU_INITIAL_TRIM_EXCESS;
int heap_stack_gap = 0;

atomic_long_t mmap_pages_allocated;

EXPORT_SYMBOL(mem_map);

/* list of mapped, potentially shareable regions */
static struct kmem_cache *vm_region_jar;
struct rb_root nommu_region_tree = RB_ROOT;
DECLARE_RWSEM(nommu_region_sem);

const struct vm_operations_struct generic_file_vm_ops = {
};

/*
 * Return the total memory allocated for this pointer, not
 * just what the caller asked for.
 *
 * Doesn't have to be accurate, i.e. may have races.
 */
unsigned int kobjsize(const void *objp)
{
	struct page *page;

	/*
	 * If the object we have should not have ksize performed on it,
	 * return size of 0
	 */
	if (!objp || !virt_addr_valid(objp))
		return 0;

	page = virt_to_head_page(objp);

	/*
	 * If the allocator sets PageSlab, we know the pointer came from
	 * kmalloc().
	 */
	if (PageSlab(page))
		return ksize(objp);

	/*
	 * If it's not a compound page, see if we have a matching VMA
	 * region. This test is intentionally done in reverse order,
	 * so if there's no VMA, we still fall through and hand back
	 * PAGE_SIZE for 0-order pages.
	 */
	if (!PageCompound(page)) {
		struct vm_area_struct *vma;

		vma = find_vma(current->mm, (unsigned long)objp);
		if (vma)
			return vma->vm_end - vma->vm_start;
	}

	/*
	 * The ksize() function is only guaranteed to work for pointers
	 * returned by kmalloc(). So handle arbitrary pointers here.
	 */
	return PAGE_SIZE << compound_order(page);
}

static long __get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
		      unsigned long start, unsigned long nr_pages,
		      unsigned int foll_flags, struct page **pages,
		      struct vm_area_struct **vmas, int *nonblocking)
{
	struct vm_area_struct *vma;
	unsigned long vm_flags;
	int i;

	/* calculate required read or write permissions.
	 * If FOLL_FORCE is set, we only require the "MAY" flags.
	 */
	vm_flags  = (foll_flags & FOLL_WRITE) ?
			(VM_WRITE | VM_MAYWRITE) : (VM_READ | VM_MAYREAD);
	vm_flags &= (foll_flags & FOLL_FORCE) ?
			(VM_MAYREAD | VM_MAYWRITE) : (VM_READ | VM_WRITE);

	for (i = 0; i < nr_pages; i++) {
		vma = find_vma(mm, start);
		if (!vma)
			goto finish_or_fault;

		/* protect what we can, including chardevs */
		if ((vma->vm_flags & (VM_IO | VM_PFNMAP)) ||
		    !(vm_flags & vma->vm_flags))
			goto finish_or_fault;

		if (pages) {
			pages[i] = virt_to_page(start);
			if (pages[i])
				get_page(pages[i]);
		}
		if (vmas)
			vmas[i] = vma;
		start = (start + PAGE_SIZE) & PAGE_MASK;
	}

	return i;

finish_or_fault:
	return i ? : -EFAULT;
}

/*
 * get a list of pages in an address range belonging to the specified process
 * and indicate the VMA that covers each page
 * - this is potentially dodgy as we may end incrementing the page count of a
 *   slab page or a secondary page from a compound page
 * - don't permit access to VMAs that don't support it, such as I/O mappings
 */
long get_user_pages(unsigned long start, unsigned long nr_pages,
		    unsigned int gup_flags, struct page **pages,
		    struct vm_area_struct **vmas)
{
	return __get_user_pages(current, current->mm, start, nr_pages,
				gup_flags, pages, vmas, NULL);
}
EXPORT_SYMBOL(get_user_pages);

long get_user_pages_locked(unsigned long start, unsigned long nr_pages,
			    unsigned int gup_flags, struct page **pages,
			    int *locked)
{
	return get_user_pages(start, nr_pages, gup_flags, pages, NULL);
}
EXPORT_SYMBOL(get_user_pages_locked);

static long __get_user_pages_unlocked(struct task_struct *tsk,
			struct mm_struct *mm, unsigned long start,
			unsigned long nr_pages, struct page **pages,
			unsigned int gup_flags)
{
	long ret;
	down_read(&mm->mmap_sem);
	ret = __get_user_pages(tsk, mm, start, nr_pages, gup_flags, pages,
				NULL, NULL);
	up_read(&mm->mmap_sem);
	return ret;
}

long get_user_pages_unlocked(unsigned long start, unsigned long nr_pages,
			     struct page **pages, unsigned int gup_flags)
{
	return __get_user_pages_unlocked(current, current->mm, start, nr_pages,
					 pages, gup_flags);
}
EXPORT_SYMBOL(get_user_pages_unlocked);

/**
 * follow_pfn - look up PFN at a user virtual address
 * @vma: memory mapping
 * @address: user virtual address
 * @pfn: location to store found PFN
 *
 * Only IO mappings and raw PFN mappings are allowed.
 *
 * Returns zero and the pfn at @pfn on success, -ve otherwise.
 */
int follow_pfn(struct vm_area_struct *vma, unsigned long address,
	unsigned long *pfn)
{
	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP)))
		return -EINVAL;

	*pfn = address >> PAGE_SHIFT;
	return 0;
}
EXPORT_SYMBOL(follow_pfn);

LIST_HEAD(vmap_area_list);

void vfree(const void *addr)
{
	kfree(addr);
}
EXPORT_SYMBOL(vfree);

void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot)
{
	/*
	 *  You can't specify __GFP_HIGHMEM with kmalloc() since kmalloc()
	 * returns only a logical address.
	 */
	return kmalloc(size, (gfp_mask | __GFP_COMP) & ~__GFP_HIGHMEM);
}
EXPORT_SYMBOL(__vmalloc);

void *__vmalloc_node_flags(unsigned long size, int node, gfp_t flags)
{
	return __vmalloc(size, flags, PAGE_KERNEL);
}

void *vmalloc_user(unsigned long size)
{
	void *ret;

	ret = __vmalloc(size, GFP_KERNEL | __GFP_ZERO, PAGE_KERNEL);
	if (ret) {
		struct vm_area_struct *vma;

		down_write(&current->mm->mmap_sem);
		vma = find_vma(current->mm, (unsigned long)ret);
		if (vma)
			vma->vm_flags |= VM_USERMAP;
		up_write(&current->mm->mmap_sem);
	}

	return ret;
}
EXPORT_SYMBOL(vmalloc_user);

struct page *vmalloc_to_page(const void *addr)
{
	return virt_to_page(addr);
}
EXPORT_SYMBOL(vmalloc_to_page);

unsigned long vmalloc_to_pfn(const void *addr)
{
	return page_to_pfn(virt_to_page(addr));
}
EXPORT_SYMBOL(vmalloc_to_pfn);

long vread(char *buf, char *addr, unsigned long count)
{
	/* Don't allow overflow */
	if ((unsigned long) buf + count < count)
		count = -(unsigned long) buf;

	memcpy(buf, addr, count);
	return count;
}

long vwrite(char *buf, char *addr, unsigned long count)
{
	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	memcpy(addr, buf, count);
	return count;
}

/*
 *	vmalloc  -  allocate virtually contiguous memory
 *
 *	@size:		allocation size
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator and map them into contiguous kernel virtual space.
 *
 *	For tight control over page level allocator and protection flags
 *	use __vmalloc() instead.
 */
void *vmalloc(unsigned long size)
{
       return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
}
EXPORT_SYMBOL(vmalloc);

/*
 *	vzalloc - allocate virtually contiguous memory with zero fill
 *
 *	@size:		allocation size
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator and map them into contiguous kernel virtual space.
 *	The memory allocated is set to zero.
 *
 *	For tight control over page level allocator and protection flags
 *	use __vmalloc() instead.
 */
void *vzalloc(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO,
			PAGE_KERNEL);
}
EXPORT_SYMBOL(vzalloc);

/**
 * vmalloc_node - allocate memory on a specific node
 * @size:	allocation size
 * @node:	numa node
 *
 * Allocate enough pages to cover @size from the page level
 * allocator and map them into contiguous kernel virtual space.
 *
 * For tight control over page level allocator and protection flags
 * use __vmalloc() instead.
 */
void *vmalloc_node(unsigned long size, int node)
{
	return vmalloc(size);
}
EXPORT_SYMBOL(vmalloc_node);

/**
 * vzalloc_node - allocate memory on a specific node with zero fill
 * @size:	allocation size
 * @node:	numa node
 *
 * Allocate enough pages to cover @size from the page level
 * allocator and map them into contiguous kernel virtual space.
 * The memory allocated is set to zero.
 *
 * For tight control over page level allocator and protection flags
 * use __vmalloc() instead.
 */
void *vzalloc_node(unsigned long size, int node)
{
	return vzalloc(size);
}
EXPORT_SYMBOL(vzalloc_node);

#ifndef PAGE_KERNEL_EXEC
# define PAGE_KERNEL_EXEC PAGE_KERNEL
#endif

/**
 *	vmalloc_exec  -  allocate virtually contiguous, executable memory
 *	@size:		allocation size
 *
 *	Kernel-internal function to allocate enough pages to cover @size
 *	the page level allocator and map them into contiguous and
 *	executable kernel virtual space.
 *
 *	For tight control over page level allocator and protection flags
 *	use __vmalloc() instead.
 */

void *vmalloc_exec(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL_EXEC);
}

/**
 * vmalloc_32  -  allocate virtually contiguous memory (32bit addressable)
 *	@size:		allocation size
 *
 *	Allocate enough 32bit PA addressable pages to cover @size from the
 *	page level allocator and map them into contiguous kernel virtual space.
 */
void *vmalloc_32(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL);
}
EXPORT_SYMBOL(vmalloc_32);

/**
 * vmalloc_32_user - allocate zeroed virtually contiguous 32bit memory
 *	@size:		allocation size
 *
 * The resulting memory area is 32bit addressable and zeroed so it can be
 * mapped to userspace without leaking data.
 *
 * VM_USERMAP is set on the corresponding VMA so that subsequent calls to
 * remap_vmalloc_range() are permissible.
 */
void *vmalloc_32_user(unsigned long size)
{
	/*
	 * We'll have to sort out the ZONE_DMA bits for 64-bit,
	 * but for now this can simply use vmalloc_user() directly.
	 */
	return vmalloc_user(size);
}
EXPORT_SYMBOL(vmalloc_32_user);

void *vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{
	BUG();
	return NULL;
}
EXPORT_SYMBOL(vmap);

void vunmap(const void *addr)
{
	BUG();
}
EXPORT_SYMBOL(vunmap);

void *vm_map_ram(struct page **pages, unsigned int count, int node, pgprot_t prot)
{
	BUG();
	return NULL;
}
EXPORT_SYMBOL(vm_map_ram);

void vm_unmap_ram(const void *mem, unsigned int count)
{
	BUG();
}
EXPORT_SYMBOL(vm_unmap_ram);

void vm_unmap_aliases(void)
{
}
EXPORT_SYMBOL_GPL(vm_unmap_aliases);

/*
 * Implement a stub for vmalloc_sync_all() if the architecture chose not to
 * have one.
 */
void __weak vmalloc_sync_all(void)
{
}

struct vm_struct *alloc_vm_area(size_t size, pte_t **ptes)
{
	BUG();
	return NULL;
}
EXPORT_SYMBOL_GPL(alloc_vm_area);

void free_vm_area(struct vm_struct *area)
{
	BUG();
}
EXPORT_SYMBOL_GPL(free_vm_area);

int vm_insert_page(struct vm_area_struct *vma, unsigned long addr,
		   struct page *page)
{
	return -EINVAL;
}
EXPORT_SYMBOL(vm_insert_page);

/*
 *  sys_brk() for the most part doesn't need the global kernel
 *  lock, except when an application is doing something nasty
 *  like trying to un-brk an area that has already been mapped
 *  to a regular file.  in this case, the unmapping will need
 *  to invoke file system routines that need the global lock.
 */
SYSCALL_DEFINE1(brk, unsigned long, brk)
{
	struct mm_struct *mm = current->mm;

	if (brk < mm->start_brk || brk > mm->context.end_brk)
		return mm->brk;

	if (mm->brk == brk)
		return mm->brk;

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= mm->brk) {
		mm->brk = brk;
		return brk;
	}

	/*
	 * Ok, looks good - let it rip.
	 */
	flush_icache_range(mm->brk, brk);
	return mm->brk = brk;
}

/*
 * initialise the percpu counter for VM and region record slabs
 */
void __init mmap_init(void)
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
	VM_BUG_ON(ret);
	vm_region_jar = KMEM_CACHE(vm_region, SLAB_PANIC|SLAB_ACCOUNT);
}

/*
 * validate the region tree
 * - the caller must hold the region lock
 */
#ifdef CONFIG_DEBUG_NOMMU_REGIONS
static noinline void validate_nommu_regions(void)
{
	struct vm_region *region, *last;
	struct rb_node *p, *lastp;

	lastp = rb_first(&nommu_region_tree);
	if (!lastp)
		return;

	last = rb_entry(lastp, struct vm_region, vm_rb);
	BUG_ON(last->vm_end <= last->vm_start);
	BUG_ON(last->vm_top < last->vm_end);

	while ((p = rb_next(lastp))) {
		region = rb_entry(p, struct vm_region, vm_rb);
		last = rb_entry(lastp, struct vm_region, vm_rb);

		BUG_ON(region->vm_end <= region->vm_start);
		BUG_ON(region->vm_top < region->vm_end);
		BUG_ON(region->vm_start < last->vm_top);

		lastp = p;
	}
}
#else
static void validate_nommu_regions(void)
{
}
#endif

/*
 * add a region into the global tree
 */
static void add_nommu_region(struct vm_region *region)
{
	struct vm_region *pregion;
	struct rb_node **p, *parent;

	validate_nommu_regions();

	parent = NULL;
	p = &nommu_region_tree.rb_node;
	while (*p) {
		parent = *p;
		pregion = rb_entry(parent, struct vm_region, vm_rb);
		if (region->vm_start < pregion->vm_start)
			p = &(*p)->rb_left;
		else if (region->vm_start > pregion->vm_start)
			p = &(*p)->rb_right;
		else if (pregion == region)
			return;
		else
			BUG();
	}

	rb_link_node(&region->vm_rb, parent, p);
	rb_insert_color(&region->vm_rb, &nommu_region_tree);

	validate_nommu_regions();
}

/*
 * delete a region from the global tree
 */
static void delete_nommu_region(struct vm_region *region)
{
	BUG_ON(!nommu_region_tree.rb_node);

	validate_nommu_regions();
	rb_erase(&region->vm_rb, &nommu_region_tree);
	validate_nommu_regions();
}

/*
 * free a contiguous series of pages
 */
static void free_page_series(unsigned long from, unsigned long to)
{
	for (; from < to; from += PAGE_SIZE) {
		struct page *page = virt_to_page(from);

		atomic_long_dec(&mmap_pages_allocated);
		put_page(page);
	}
}

/*
 * release a reference to a region
 * - the caller must hold the region semaphore for writing, which this releases
 * - the region may not have been added to the tree yet, in which case vm_top
 *   will equal vm_start
 */
static void __put_nommu_region(struct vm_region *region)
	__releases(nommu_region_sem)
{
	BUG_ON(!nommu_region_tree.rb_node);

	if (--region->vm_usage == 0) {
		if (region->vm_top > region->vm_start)
			delete_nommu_region(region);
		up_write(&nommu_region_sem);

		if (region->vm_file)
			fput(region->vm_file);

		/* IO memory and memory shared directly out of the pagecache
		 * from ramfs/tmpfs mustn't be released here */
		if (region->vm_flags & VM_MAPPED_COPY)
			free_page_series(region->vm_start, region->vm_top);
		kmem_cache_free(vm_region_jar, region);
	} else {
		up_write(&nommu_region_sem);
	}
}

/*
 * release a reference to a region
 */
static void put_nommu_region(struct vm_region *region)
{
	down_write(&nommu_region_sem);
	__put_nommu_region(region);
}

/*
 * add a VMA into a process's mm_struct in the appropriate place in the list
 * and tree and add to the address space's page tree also if not an anonymous
 * page
 * - should be called with mm->mmap_sem held writelocked
 */
static void add_vma_to_mm(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *pvma, *prev;
	struct address_space *mapping;
	struct rb_node **p, *parent, *rb_prev;

	BUG_ON(!vma->vm_region);

	mm->map_count++;
	vma->vm_mm = mm;

	/* add the VMA to the mapping */
	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;

		i_mmap_lock_write(mapping);
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
		i_mmap_unlock_write(mapping);
	}

	/* add the VMA to the tree */
	parent = rb_prev = NULL;
	p = &mm->mm_rb.rb_node;
	while (*p) {
		parent = *p;
		pvma = rb_entry(parent, struct vm_area_struct, vm_rb);

		/* sort by: start addr, end addr, VMA struct addr in that order
		 * (the latter is necessary as we may get identical VMAs) */
		if (vma->vm_start < pvma->vm_start)
			p = &(*p)->rb_left;
		else if (vma->vm_start > pvma->vm_start) {
			rb_prev = parent;
			p = &(*p)->rb_right;
		} else if (vma->vm_end < pvma->vm_end)
			p = &(*p)->rb_left;
		else if (vma->vm_end > pvma->vm_end) {
			rb_prev = parent;
			p = &(*p)->rb_right;
		} else if (vma < pvma)
			p = &(*p)->rb_left;
		else if (vma > pvma) {
			rb_prev = parent;
			p = &(*p)->rb_right;
		} else
			BUG();
	}

	rb_link_node(&vma->vm_rb, parent, p);
	rb_insert_color(&vma->vm_rb, &mm->mm_rb);

	/* add VMA to the VMA list also */
	prev = NULL;
	if (rb_prev)
		prev = rb_entry(rb_prev, struct vm_area_struct, vm_rb);

	__vma_link_list(mm, vma, prev, parent);
}

/*
 * delete a VMA from its owning mm_struct and address space
 */
static void delete_vma_from_mm(struct vm_area_struct *vma)
{
	int i;
	struct address_space *mapping;
	struct mm_struct *mm = vma->vm_mm;
	struct task_struct *curr = current;

	mm->map_count--;
	for (i = 0; i < VMACACHE_SIZE; i++) {
		/* if the vma is cached, invalidate the entire cache */
		if (curr->vmacache.vmas[i] == vma) {
			vmacache_invalidate(mm);
			break;
		}
	}

	/* remove the VMA from the mapping */
	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;

		i_mmap_lock_write(mapping);
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_remove(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
		i_mmap_unlock_write(mapping);
	}

	/* remove from the MM's tree and list */
	rb_erase(&vma->vm_rb, &mm->mm_rb);

	if (vma->vm_prev)
		vma->vm_prev->vm_next = vma->vm_next;
	else
		mm->mmap = vma->vm_next;

	if (vma->vm_next)
		vma->vm_next->vm_prev = vma->vm_prev;
}

/*
 * destroy a VMA record
 */
static void delete_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file)
		fput(vma->vm_file);
	put_nommu_region(vma->vm_region);
	kmem_cache_free(vm_area_cachep, vma);
}

/*
 * look up the first VMA in which addr resides, NULL if none
 * - should be called with mm->mmap_sem at least held readlocked
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;

	/* check the cache first */
	vma = vmacache_find(mm, addr);
	if (likely(vma))
		return vma;

	/* trawl the list (there may be multiple mappings in which addr
	 * resides) */
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->vm_start > addr)
			return NULL;
		if (vma->vm_end > addr) {
			vmacache_update(addr, vma);
			return vma;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(find_vma);

/*
 * find a VMA
 * - we don't extend stack VMAs under NOMMU conditions
 */
struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	return find_vma(mm, addr);
}

/*
 * expand a stack to a given address
 * - not supported under NOMMU conditions
 */
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	return -ENOMEM;
}

/*
 * look up the first VMA exactly that exactly matches addr
 * - should be called with mm->mmap_sem at least held readlocked
 */
static struct vm_area_struct *find_vma_exact(struct mm_struct *mm,
					     unsigned long addr,
					     unsigned long len)
{
	struct vm_area_struct *vma;
	unsigned long end = addr + len;

	/* check the cache first */
	vma = vmacache_find_exact(mm, addr, end);
	if (vma)
		return vma;

	/* trawl the list (there may be multiple mappings in which addr
	 * resides) */
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->vm_start < addr)
			continue;
		if (vma->vm_start > addr)
			return NULL;
		if (vma->vm_end == end) {
			vmacache_update(addr, vma);
			return vma;
		}
	}

	return NULL;
}

/*
 * determine whether a mapping should be permitted and, if so, what sort of
 * mapping we're capable of supporting
 */
static int validate_mmap_request(struct file *file,
				 unsigned long addr,
				 unsigned long len,
				 unsigned long prot,
				 unsigned long flags,
				 unsigned long pgoff,
				 unsigned long *_capabilities)
{
	unsigned long capabilities, rlen;
	int ret;

	/* do the simple checks first */
	if (flags & MAP_FIXED)
		return -EINVAL;

	if ((flags & MAP_TYPE) != MAP_PRIVATE &&
	    (flags & MAP_TYPE) != MAP_SHARED)
		return -EINVAL;

	if (!len)
		return -EINVAL;

	/* Careful about overflows.. */
	rlen = PAGE_ALIGN(len);
	if (!rlen || rlen > TASK_SIZE)
		return -ENOMEM;

	/* offset overflow? */
	if ((pgoff + (rlen >> PAGE_SHIFT)) < pgoff)
		return -EOVERFLOW;

	if (file) {
		/* files must support mmap */
		if (!file->f_op->mmap)
			return -ENODEV;

		/* work out if what we've got could possibly be shared
		 * - we support chardevs that provide their own "memory"
		 * - we support files/blockdevs that are memory backed
		 */
		if (file->f_op->mmap_capabilities) {
			capabilities = file->f_op->mmap_capabilities(file);
		} else {
			/* no explicit capabilities set, so assume some
			 * defaults */
			switch (file_inode(file)->i_mode & S_IFMT) {
			case S_IFREG:
			case S_IFBLK:
				capabilities = NOMMU_MAP_COPY;
				break;

			case S_IFCHR:
				capabilities =
					NOMMU_MAP_DIRECT |
					NOMMU_MAP_READ |
					NOMMU_MAP_WRITE;
				break;

			default:
				return -EINVAL;
			}
		}

		/* eliminate any capabilities that we can't support on this
		 * device */
		if (!file->f_op->get_unmapped_area)
			capabilities &= ~NOMMU_MAP_DIRECT;
		if (!(file->f_mode & FMODE_CAN_READ))
			capabilities &= ~NOMMU_MAP_COPY;

		/* The file shall have been opened with read permission. */
		if (!(file->f_mode & FMODE_READ))
			return -EACCES;

		if (flags & MAP_SHARED) {
			/* do checks for writing, appending and locking */
			if ((prot & PROT_WRITE) &&
			    !(file->f_mode & FMODE_WRITE))
				return -EACCES;

			if (IS_APPEND(file_inode(file)) &&
			    (file->f_mode & FMODE_WRITE))
				return -EACCES;

			if (locks_verify_locked(file))
				return -EAGAIN;

			if (!(capabilities & NOMMU_MAP_DIRECT))
				return -ENODEV;

			/* we mustn't privatise shared mappings */
			capabilities &= ~NOMMU_MAP_COPY;
		} else {
			/* we're going to read the file into private memory we
			 * allocate */
			if (!(capabilities & NOMMU_MAP_COPY))
				return -ENODEV;

			/* we don't permit a private writable mapping to be
			 * shared with the backing device */
			if (prot & PROT_WRITE)
				capabilities &= ~NOMMU_MAP_DIRECT;
		}

		if (capabilities & NOMMU_MAP_DIRECT) {
			if (((prot & PROT_READ)  && !(capabilities & NOMMU_MAP_READ))  ||
			    ((prot & PROT_WRITE) && !(capabilities & NOMMU_MAP_WRITE)) ||
			    ((prot & PROT_EXEC)  && !(capabilities & NOMMU_MAP_EXEC))
			    ) {
				capabilities &= ~NOMMU_MAP_DIRECT;
				if (flags & MAP_SHARED) {
					pr_warn("MAP_SHARED not completely supported on !MMU\n");
					return -EINVAL;
				}
			}
		}

		/* handle executable mappings and implied executable
		 * mappings */
		if (path_noexec(&file->f_path)) {
			if (prot & PROT_EXEC)
				return -EPERM;
		} else if ((prot & PROT_READ) && !(prot & PROT_EXEC)) {
			/* handle implication of PROT_EXEC by PROT_READ */
			if (current->personality & READ_IMPLIES_EXEC) {
				if (capabilities & NOMMU_MAP_EXEC)
					prot |= PROT_EXEC;
			}
		} else if ((prot & PROT_READ) &&
			 (prot & PROT_EXEC) &&
			 !(capabilities & NOMMU_MAP_EXEC)
			 ) {
			/* backing file is not executable, try to copy */
			capabilities &= ~NOMMU_MAP_DIRECT;
		}
	} else {
		/* anonymous mappings are always memory backed and can be
		 * privately mapped
		 */
		capabilities = NOMMU_MAP_COPY;

		/* handle PROT_EXEC implication by PROT_READ */
		if ((prot & PROT_READ) &&
		    (current->personality & READ_IMPLIES_EXEC))
			prot |= PROT_EXEC;
	}

	/* allow the security API to have its say */
	ret = security_mmap_addr(addr);
	if (ret < 0)
		return ret;

	/* looks okay */
	*_capabilities = capabilities;
	return 0;
}

/*
 * we've determined that we can make the mapping, now translate what we
 * now know into VMA flags
 */
static unsigned long determine_vm_flags(struct file *file,
					unsigned long prot,
					unsigned long flags,
					unsigned long capabilities)
{
	unsigned long vm_flags;

	vm_flags = calc_vm_prot_bits(prot, 0) | calc_vm_flag_bits(flags);
	/* vm_flags |= mm->def_flags; */

	if (!(capabilities & NOMMU_MAP_DIRECT)) {
		/* attempt to share read-only copies of mapped file chunks */
		vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
		if (file && !(prot & PROT_WRITE))
			vm_flags |= VM_MAYSHARE;
	} else {
		/* overlay a shareable mapping on the backing device or inode
		 * if possible - used for chardevs, ramfs/tmpfs/shmfs and
		 * romfs/cramfs */
		vm_flags |= VM_MAYSHARE | (capabilities & NOMMU_VMFLAGS);
		if (flags & MAP_SHARED)
			vm_flags |= VM_SHARED;
	}

	/* refuse to let anyone share private mappings with this process if
	 * it's being traced - otherwise breakpoints set in it may interfere
	 * with another untraced process
	 */
	if ((flags & MAP_PRIVATE) && current->ptrace)
		vm_flags &= ~VM_MAYSHARE;

	return vm_flags;
}

/*
 * set up a shared mapping on a file (the driver or filesystem provides and
 * pins the storage)
 */
static int do_mmap_shared_file(struct vm_area_struct *vma)
{
	int ret;

	ret = call_mmap(vma->vm_file, vma);
	if (ret == 0) {
		vma->vm_region->vm_top = vma->vm_region->vm_end;
		return 0;
	}
	if (ret != -ENOSYS)
		return ret;

	/* getting -ENOSYS indicates that direct mmap isn't possible (as
	 * opposed to tried but failed) so we can only give a suitable error as
	 * it's not possible to make a private copy if MAP_SHARED was given */
	return -ENODEV;
}

/*
 * set up a private mapping or an anonymous shared mapping
 */
static int do_mmap_private(struct vm_area_struct *vma,
			   struct vm_region *region,
			   unsigned long len,
			   unsigned long capabilities)
{
	unsigned long total, point;
	void *base;
	int ret, order;

	/* invoke the file's mapping function so that it can keep track of
	 * shared mappings on devices or memory
	 * - VM_MAYSHARE will be set if it may attempt to share
	 */
	if (capabilities & NOMMU_MAP_DIRECT) {
		ret = call_mmap(vma->vm_file, vma);
		if (ret == 0) {
			/* shouldn't return success if we're not sharing */
			BUG_ON(!(vma->vm_flags & VM_MAYSHARE));
			vma->vm_region->vm_top = vma->vm_region->vm_end;
			return 0;
		}
		if (ret != -ENOSYS)
			return ret;

		/* getting an ENOSYS error indicates that direct mmap isn't
		 * possible (as opposed to tried but failed) so we'll try to
		 * make a private copy of the data and map that instead */
	}


	/* allocate some memory to hold the mapping
	 * - note that this may not return a page-aligned address if the object
	 *   we're allocating is smaller than a page
	 */
	order = get_order(len);
	total = 1 << order;
	point = len >> PAGE_SHIFT;

	/* we don't want to allocate a power-of-2 sized page set */
	if (sysctl_nr_trim_pages && total - point >= sysctl_nr_trim_pages)
		total = point;

	base = alloc_pages_exact(total << PAGE_SHIFT, GFP_KERNEL);
	if (!base)
		goto enomem;

	atomic_long_add(total, &mmap_pages_allocated);

	region->vm_flags = vma->vm_flags |= VM_MAPPED_COPY;
	region->vm_start = (unsigned long) base;
	region->vm_end   = region->vm_start + len;
	region->vm_top   = region->vm_start + (total << PAGE_SHIFT);

	vma->vm_start = region->vm_start;
	vma->vm_end   = region->vm_start + len;

	if (vma->vm_file) {
		/* read the contents of a file into the copy */
		loff_t fpos;

		fpos = vma->vm_pgoff;
		fpos <<= PAGE_SHIFT;

		ret = kernel_read(vma->vm_file, base, len, &fpos);
		if (ret < 0)
			goto error_free;

		/* clear the last little bit */
		if (ret < len)
			memset(base + ret, 0, len - ret);

	}

	return 0;

error_free:
	free_page_series(region->vm_start, region->vm_top);
	region->vm_start = vma->vm_start = 0;
	region->vm_end   = vma->vm_end = 0;
	region->vm_top   = 0;
	return ret;

enomem:
	pr_err("Allocation of length %lu from process %d (%s) failed\n",
	       len, current->pid, current->comm);
	show_free_areas(0, NULL);
	return -ENOMEM;
}

/*
 * handle mapping creation for uClinux
 */
unsigned long do_mmap(struct file *file,
			unsigned long addr,
			unsigned long len,
			unsigned long prot,
			unsigned long flags,
			vm_flags_t vm_flags,
			unsigned long pgoff,
			unsigned long *populate,
			struct list_head *uf)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *rb;
	unsigned long capabilities, result;
	int ret;

	*populate = 0;

	/* decide whether we should attempt the mapping, and if so what sort of
	 * mapping */
	ret = validate_mmap_request(file, addr, len, prot, flags, pgoff,
				    &capabilities);
	if (ret < 0)
		return ret;

	/* we ignore the address hint */
	addr = 0;
	len = PAGE_ALIGN(len);

	/* we've determined that we can make the mapping, now translate what we
	 * now know into VMA flags */
	vm_flags |= determine_vm_flags(file, prot, flags, capabilities);

	/* we're going to need to record the mapping */
	region = kmem_cache_zalloc(vm_region_jar, GFP_KERNEL);
	if (!region)
		goto error_getting_region;

	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma)
		goto error_getting_vma;

	region->vm_usage = 1;
	region->vm_flags = vm_flags;
	region->vm_pgoff = pgoff;

	INIT_LIST_HEAD(&vma->anon_vma_chain);
	vma->vm_flags = vm_flags;
	vma->vm_pgoff = pgoff;

	if (file) {
		region->vm_file = get_file(file);
		vma->vm_file = get_file(file);
	}

	down_write(&nommu_region_sem);

	/* if we want to share, we need to check for regions created by other
	 * mmap() calls that overlap with our proposed mapping
	 * - we can only share with a superset match on most regular files
	 * - shared mappings on character devices and memory backed files are
	 *   permitted to overlap inexactly as far as we are concerned for in
	 *   these cases, sharing is handled in the driver or filesystem rather
	 *   than here
	 */
	if (vm_flags & VM_MAYSHARE) {
		struct vm_region *pregion;
		unsigned long pglen, rpglen, pgend, rpgend, start;

		pglen = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		pgend = pgoff + pglen;

		for (rb = rb_first(&nommu_region_tree); rb; rb = rb_next(rb)) {
			pregion = rb_entry(rb, struct vm_region, vm_rb);

			if (!(pregion->vm_flags & VM_MAYSHARE))
				continue;

			/* search for overlapping mappings on the same file */
			if (file_inode(pregion->vm_file) !=
			    file_inode(file))
				continue;

			if (pregion->vm_pgoff >= pgend)
				continue;

			rpglen = pregion->vm_end - pregion->vm_start;
			rpglen = (rpglen + PAGE_SIZE - 1) >> PAGE_SHIFT;
			rpgend = pregion->vm_pgoff + rpglen;
			if (pgoff >= rpgend)
				continue;

			/* handle inexactly overlapping matches between
			 * mappings */
			if ((pregion->vm_pgoff != pgoff || rpglen != pglen) &&
			    !(pgoff >= pregion->vm_pgoff && pgend <= rpgend)) {
				/* new mapping is not a subset of the region */
				if (!(capabilities & NOMMU_MAP_DIRECT))
					goto sharing_violation;
				continue;
			}

			/* we've found a region we can share */
			pregion->vm_usage++;
			vma->vm_region = pregion;
			start = pregion->vm_start;
			start += (pgoff - pregion->vm_pgoff) << PAGE_SHIFT;
			vma->vm_start = start;
			vma->vm_end = start + len;

			if (pregion->vm_flags & VM_MAPPED_COPY)
				vma->vm_flags |= VM_MAPPED_COPY;
			else {
				ret = do_mmap_shared_file(vma);
				if (ret < 0) {
					vma->vm_region = NULL;
					vma->vm_start = 0;
					vma->vm_end = 0;
					pregion->vm_usage--;
					pregion = NULL;
					goto error_just_free;
				}
			}
			fput(region->vm_file);
			kmem_cache_free(vm_region_jar, region);
			region = pregion;
			result = start;
			goto share;
		}

		/* obtain the address at which to make a shared mapping
		 * - this is the hook for quasi-memory character devices to
		 *   tell us the location of a shared mapping
		 */
		if (capabilities & NOMMU_MAP_DIRECT) {
			addr = file->f_op->get_unmapped_area(file, addr, len,
							     pgoff, flags);
			if (IS_ERR_VALUE(addr)) {
				ret = addr;
				if (ret != -ENOSYS)
					goto error_just_free;

				/* the driver refused to tell us where to site
				 * the mapping so we'll have to attempt to copy
				 * it */
				ret = -ENODEV;
				if (!(capabilities & NOMMU_MAP_COPY))
					goto error_just_free;

				capabilities &= ~NOMMU_MAP_DIRECT;
			} else {
				vma->vm_start = region->vm_start = addr;
				vma->vm_end = region->vm_end = addr + len;
			}
		}
	}

	vma->vm_region = region;

	/* set up the mapping
	 * - the region is filled in if NOMMU_MAP_DIRECT is still set
	 */
	if (file && vma->vm_flags & VM_SHARED)
		ret = do_mmap_shared_file(vma);
	else
		ret = do_mmap_private(vma, region, len, capabilities);
	if (ret < 0)
		goto error_just_free;
	add_nommu_region(region);

	/* clear anonymous mappings that don't ask for uninitialized data */
	if (!vma->vm_file && !(flags & MAP_UNINITIALIZED))
		memset((void *)region->vm_start, 0,
		       region->vm_end - region->vm_start);

	/* okay... we have a mapping; now we have to register it */
	result = vma->vm_start;

	current->mm->total_vm += len >> PAGE_SHIFT;

share:
	add_vma_to_mm(current->mm, vma);

	/* we flush the region from the icache only when the first executable
	 * mapping of it is made  */
	if (vma->vm_flags & VM_EXEC && !region->vm_icache_flushed) {
		flush_icache_range(region->vm_start, region->vm_end);
		region->vm_icache_flushed = true;
	}

	up_write(&nommu_region_sem);

	return result;

error_just_free:
	up_write(&nommu_region_sem);
error:
	if (region->vm_file)
		fput(region->vm_file);
	kmem_cache_free(vm_region_jar, region);
	if (vma->vm_file)
		fput(vma->vm_file);
	kmem_cache_free(vm_area_cachep, vma);
	return ret;

sharing_violation:
	up_write(&nommu_region_sem);
	pr_warn("Attempt to share mismatched mappings\n");
	ret = -EINVAL;
	goto error;

error_getting_vma:
	kmem_cache_free(vm_region_jar, region);
	pr_warn("Allocation of vma for %lu byte allocation from process %d failed\n",
			len, current->pid);
	show_free_areas(0, NULL);
	return -ENOMEM;

error_getting_region:
	pr_warn("Allocation of vm region for %lu byte allocation from process %d failed\n",
			len, current->pid);
	show_free_areas(0, NULL);
	return -ENOMEM;
}

unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff)
{
	struct file *file = NULL;
	unsigned long retval = -EBADF;

	audit_mmap_fd(fd, flags);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);

	if (file)
		fput(file);
out:
	return retval;
}

SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}

#ifdef __ARCH_WANT_SYS_OLD_MMAP
struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(old_mmap, struct mmap_arg_struct __user *, arg)
{
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (offset_in_page(a.offset))
		return -EINVAL;

	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset >> PAGE_SHIFT);
}
#endif /* __ARCH_WANT_SYS_OLD_MMAP */

/*
 * split a vma into two pieces at address 'addr', a new vma is allocated either
 * for the first part or the tail.
 */
int split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	struct vm_area_struct *new;
	struct vm_region *region;
	unsigned long npages;

	/* we're only permitted to split anonymous regions (these should have
	 * only a single usage on the region) */
	if (vma->vm_file)
		return -ENOMEM;

	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	region = kmem_cache_alloc(vm_region_jar, GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	new = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!new) {
		kmem_cache_free(vm_region_jar, region);
		return -ENOMEM;
	}

	/* most fields are the same, copy all, and then fixup */
	*new = *vma;
	*region = *vma->vm_region;
	new->vm_region = region;

	npages = (addr - vma->vm_start) >> PAGE_SHIFT;

	if (new_below) {
		region->vm_top = region->vm_end = new->vm_end = addr;
	} else {
		region->vm_start = new->vm_start = addr;
		region->vm_pgoff = new->vm_pgoff += npages;
	}

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	delete_vma_from_mm(vma);
	down_write(&nommu_region_sem);
	delete_nommu_region(vma->vm_region);
	if (new_below) {
		vma->vm_region->vm_start = vma->vm_start = addr;
		vma->vm_region->vm_pgoff = vma->vm_pgoff += npages;
	} else {
		vma->vm_region->vm_end = vma->vm_end = addr;
		vma->vm_region->vm_top = addr;
	}
	add_nommu_region(vma->vm_region);
	add_nommu_region(new->vm_region);
	up_write(&nommu_region_sem);
	add_vma_to_mm(mm, vma);
	add_vma_to_mm(mm, new);
	return 0;
}

/*
 * shrink a VMA by removing the specified chunk from either the beginning or
 * the end
 */
static int shrink_vma(struct mm_struct *mm,
		      struct vm_area_struct *vma,
		      unsigned long from, unsigned long to)
{
	struct vm_region *region;

	/* adjust the VMA's pointers, which may reposition it in the MM's tree
	 * and list */
	delete_vma_from_mm(vma);
	if (from > vma->vm_start)
		vma->vm_end = from;
	else
		vma->vm_start = to;
	add_vma_to_mm(mm, vma);

	/* cut the backing region down to size */
	region = vma->vm_region;
	BUG_ON(region->vm_usage != 1);

	down_write(&nommu_region_sem);
	delete_nommu_region(region);
	if (from > region->vm_start) {
		to = region->vm_top;
		region->vm_top = region->vm_end = from;
	} else {
		region->vm_start = to;
	}
	add_nommu_region(region);
	up_write(&nommu_region_sem);

	free_page_series(from, to);
	return 0;
}

/*
 * release a mapping
 * - under NOMMU conditions the chunk to be unmapped must be backed by a single
 *   VMA, though it need not cover the whole VMA
 */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len, struct list_head *uf)
{
	struct vm_area_struct *vma;
	unsigned long end;
	int ret;

	len = PAGE_ALIGN(len);
	if (len == 0)
		return -EINVAL;

	end = start + len;

	/* find the first potentially overlapping VMA */
	vma = find_vma(mm, start);
	if (!vma) {
		static int limit;
		if (limit < 5) {
			pr_warn("munmap of memory not mmapped by process %d (%s): 0x%lx-0x%lx\n",
					current->pid, current->comm,
					start, start + len - 1);
			limit++;
		}
		return -EINVAL;
	}

	/* we're allowed to split an anonymous VMA but not a file-backed one */
	if (vma->vm_file) {
		do {
			if (start > vma->vm_start)
				return -EINVAL;
			if (end == vma->vm_end)
				goto erase_whole_vma;
			vma = vma->vm_next;
		} while (vma);
		return -EINVAL;
	} else {
		/* the chunk must be a subset of the VMA found */
		if (start == vma->vm_start && end == vma->vm_end)
			goto erase_whole_vma;
		if (start < vma->vm_start || end > vma->vm_end)
			return -EINVAL;
		if (offset_in_page(start))
			return -EINVAL;
		if (end != vma->vm_end && offset_in_page(end))
			return -EINVAL;
		if (start != vma->vm_start && end != vma->vm_end) {
			ret = split_vma(mm, vma, start, 1);
			if (ret < 0)
				return ret;
		}
		return shrink_vma(mm, vma, start, end);
	}

erase_whole_vma:
	delete_vma_from_mm(vma);
	delete_vma(mm, vma);
	return 0;
}
EXPORT_SYMBOL(do_munmap);

int vm_munmap(unsigned long addr, size_t len)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_write(&mm->mmap_sem);
	ret = do_munmap(mm, addr, len, NULL);
	up_write(&mm->mmap_sem);
	return ret;
}
EXPORT_SYMBOL(vm_munmap);

SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	return vm_munmap(addr, len);
}

/*
 * release all the mappings made in a process's VM space
 */
void exit_mmap(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	if (!mm)
		return;

	mm->total_vm = 0;

	while ((vma = mm->mmap)) {
		mm->mmap = vma->vm_next;
		delete_vma_from_mm(vma);
		delete_vma(mm, vma);
		cond_resched();
	}
}

int vm_brk(unsigned long addr, unsigned long len)
{
	return -ENOMEM;
}

/*
 * expand (or shrink) an existing mapping, potentially moving it at the same
 * time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 *
 * under NOMMU conditions, we only permit changing a mapping's size, and only
 * as long as it stays within the region allocated by do_mmap_private() and the
 * block is not shareable
 *
 * MREMAP_FIXED is not supported under NOMMU conditions
 */
static unsigned long do_mremap(unsigned long addr,
			unsigned long old_len, unsigned long new_len,
			unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;

	/* insanity checks first */
	old_len = PAGE_ALIGN(old_len);
	new_len = PAGE_ALIGN(new_len);
	if (old_len == 0 || new_len == 0)
		return (unsigned long) -EINVAL;

	if (offset_in_page(addr))
		return -EINVAL;

	if (flags & MREMAP_FIXED && new_addr != addr)
		return (unsigned long) -EINVAL;

	vma = find_vma_exact(current->mm, addr, old_len);
	if (!vma)
		return (unsigned long) -EINVAL;

	if (vma->vm_end != vma->vm_start + old_len)
		return (unsigned long) -EFAULT;

	if (vma->vm_flags & VM_MAYSHARE)
		return (unsigned long) -EPERM;

	if (new_len > vma->vm_region->vm_end - vma->vm_region->vm_start)
		return (unsigned long) -ENOMEM;

	/* all checks complete - do it */
	vma->vm_end = vma->vm_start + new_len;
	return vma->vm_start;
}

SYSCALL_DEFINE5(mremap, unsigned long, addr, unsigned long, old_len,
		unsigned long, new_len, unsigned long, flags,
		unsigned long, new_addr)
{
	unsigned long ret;

	down_write(&current->mm->mmap_sem);
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
	up_write(&current->mm->mmap_sem);
	return ret;
}

struct page *follow_page_mask(struct vm_area_struct *vma,
			      unsigned long address, unsigned int flags,
			      unsigned int *page_mask)
{
	*page_mask = 0;
	return NULL;
}

int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	if (addr != (pfn << PAGE_SHIFT))
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
}
EXPORT_SYMBOL(remap_pfn_range);

int vm_iomap_memory(struct vm_area_struct *vma, phys_addr_t start, unsigned long len)
{
	unsigned long pfn = start >> PAGE_SHIFT;
	unsigned long vm_len = vma->vm_end - vma->vm_start;

	pfn += vma->vm_pgoff;
	return io_remap_pfn_range(vma, vma->vm_start, pfn, vm_len, vma->vm_page_prot);
}
EXPORT_SYMBOL(vm_iomap_memory);

int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
			unsigned long pgoff)
{
	unsigned int size = vma->vm_end - vma->vm_start;

	if (!(vma->vm_flags & VM_USERMAP))
		return -EINVAL;

	vma->vm_start = (unsigned long)(addr + (pgoff << PAGE_SHIFT));
	vma->vm_end = vma->vm_start + size;

	return 0;
}
EXPORT_SYMBOL(remap_vmalloc_range);

unsigned long arch_get_unmapped_area(struct file *file, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return -ENOMEM;
}

vm_fault_t filemap_fault(struct vm_fault *vmf)
{
	BUG();
	return 0;
}
EXPORT_SYMBOL(filemap_fault);

void filemap_map_pages(struct vm_fault *vmf,
		pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	BUG();
}
EXPORT_SYMBOL(filemap_map_pages);

int __access_remote_vm(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long addr, void *buf, int len, unsigned int gup_flags)
{
	struct vm_area_struct *vma;
	int write = gup_flags & FOLL_WRITE;

	down_read(&mm->mmap_sem);

	/* the access must start within one of the target process's mappings */
	vma = find_vma(mm, addr);
	if (vma) {
		/* don't overrun this mapping */
		if (addr + len >= vma->vm_end)
			len = vma->vm_end - addr;

		/* only read or write mappings where it is permitted */
		if (write && vma->vm_flags & VM_MAYWRITE)
			copy_to_user_page(vma, NULL, addr,
					 (void *) addr, buf, len);
		else if (!write && vma->vm_flags & VM_MAYREAD)
			copy_from_user_page(vma, NULL, addr,
					    buf, (void *) addr, len);
		else
			len = 0;
	} else {
		len = 0;
	}

	up_read(&mm->mmap_sem);

	return len;
}

/**
 * access_remote_vm - access another process' address space
 * @mm:		the mm_struct of the target address space
 * @addr:	start address to access
 * @buf:	source or destination buffer
 * @len:	number of bytes to transfer
 * @gup_flags:	flags modifying lookup behaviour
 *
 * The caller must hold a reference on @mm.
 */
int access_remote_vm(struct mm_struct *mm, unsigned long addr,
		void *buf, int len, unsigned int gup_flags)
{
	return __access_remote_vm(NULL, mm, addr, buf, len, gup_flags);
}

/*
 * Access another process' address space.
 * - source/target buffer must be kernel space
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len,
		unsigned int gup_flags)
{
	struct mm_struct *mm;

	if (addr + len < addr)
		return 0;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	len = __access_remote_vm(tsk, mm, addr, buf, len, gup_flags);

	mmput(mm);
	return len;
}
EXPORT_SYMBOL_GPL(access_process_vm);

/**
 * nommu_shrink_inode_mappings - Shrink the shared mappings on an inode
 * @inode: The inode to check
 * @size: The current filesize of the inode
 * @newsize: The proposed filesize of the inode
 *
 * Check the shared mappings on an inode on behalf of a shrinking truncate to
 * make sure that that any outstanding VMAs aren't broken and then shrink the
 * vm_regions that extend that beyond so that do_mmap_pgoff() doesn't
 * automatically grant mappings that are too large.
 */
int nommu_shrink_inode_mappings(struct inode *inode, size_t size,
				size_t newsize)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	pgoff_t low, high;
	size_t r_size, r_top;

	low = newsize >> PAGE_SHIFT;
	high = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	down_write(&nommu_region_sem);
	i_mmap_lock_read(inode->i_mapping);

	/* search for VMAs that fall within the dead zone */
	vma_interval_tree_foreach(vma, &inode->i_mapping->i_mmap, low, high) {
		/* found one - only interested if it's shared out of the page
		 * cache */
		if (vma->vm_flags & VM_SHARED) {
			i_mmap_unlock_read(inode->i_mapping);
			up_write(&nommu_region_sem);
			return -ETXTBSY; /* not quite true, but near enough */
		}
	}

	/* reduce any regions that overlap the dead zone - if in existence,
	 * these will be pointed to by VMAs that don't overlap the dead zone
	 *
	 * we don't check for any regions that start beyond the EOF as there
	 * shouldn't be any
	 */
	vma_interval_tree_foreach(vma, &inode->i_mapping->i_mmap, 0, ULONG_MAX) {
		if (!(vma->vm_flags & VM_SHARED))
			continue;

		region = vma->vm_region;
		r_size = region->vm_top - region->vm_start;
		r_top = (region->vm_pgoff << PAGE_SHIFT) + r_size;

		if (r_top > newsize) {
			region->vm_top -= r_top - newsize;
			if (region->vm_end > region->vm_top)
				region->vm_end = region->vm_top;
		}
	}

	i_mmap_unlock_read(inode->i_mapping);
	up_write(&nommu_region_sem);
	return 0;
}

/*
 * Initialise sysctl_user_reserve_kbytes.
 *
 * This is intended to prevent a user from starting a single memory hogging
 * process, such that they cannot recover (kill the hog) in OVERCOMMIT_NEVER
 * mode.
 *
 * The default value is min(3% of free memory, 128MB)
 * 128MB is enough to recover with sshd/login, bash, and top/kill.
 */
static int __meminit init_user_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_user_reserve_kbytes = min(free_kbytes / 32, 1UL << 17);
	return 0;
}
subsys_initcall(init_user_reserve);

/*
 * Initialise sysctl_admin_reserve_kbytes.
 *
 * The purpose of sysctl_admin_reserve_kbytes is to allow the sys admin
 * to log in and kill a memory hogging process.
 *
 * Systems with more than 256MB will reserve 8MB, enough to recover
 * with sshd, bash, and top in OVERCOMMIT_GUESS. Smaller systems will
 * only reserve 3% of free pages by default.
 */
static int __meminit init_admin_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_zone_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, 1UL << 13);
	return 0;
}
subsys_initcall(init_admin_reserve);
