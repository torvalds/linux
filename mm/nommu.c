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
 *  Copyright (c) 2007-2009 Paul Mundt <lethal@linux-sh.org>
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/tracehook.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include "internal.h"

static inline __attribute__((format(printf, 1, 2)))
void no_printk(const char *fmt, ...)
{
}

#if 0
#define kenter(FMT, ...) \
	printk(KERN_DEBUG "==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	printk(KERN_DEBUG "<== %s()"FMT"\n", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) \
	printk(KERN_DEBUG "xxx" FMT"yyy\n", ##__VA_ARGS__)
#else
#define kenter(FMT, ...) \
	no_printk(KERN_DEBUG "==> %s("FMT")\n", __func__, ##__VA_ARGS__)
#define kleave(FMT, ...) \
	no_printk(KERN_DEBUG "<== %s()"FMT"\n", __func__, ##__VA_ARGS__)
#define kdebug(FMT, ...) \
	no_printk(KERN_DEBUG FMT"\n", ##__VA_ARGS__)
#endif

void *high_memory;
struct page *mem_map;
unsigned long max_mapnr;
unsigned long num_physpages;
unsigned long highest_memmap_pfn;
struct percpu_counter vm_committed_as;
int sysctl_overcommit_memory = OVERCOMMIT_GUESS; /* heuristic overcommit */
int sysctl_overcommit_ratio = 50; /* default is 50% */
int sysctl_max_map_count = DEFAULT_MAX_MAP_COUNT;
int sysctl_nr_trim_pages = CONFIG_NOMMU_INITIAL_TRIM_EXCESS;
int heap_stack_gap = 0;

atomic_long_t mmap_pages_allocated;

EXPORT_SYMBOL(mem_map);
EXPORT_SYMBOL(num_physpages);

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

int __get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
		     unsigned long start, int nr_pages, unsigned int foll_flags,
		     struct page **pages, struct vm_area_struct **vmas)
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
				page_cache_get(pages[i]);
		}
		if (vmas)
			vmas[i] = vma;
		start += PAGE_SIZE;
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
int get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
	unsigned long start, int nr_pages, int write, int force,
	struct page **pages, struct vm_area_struct **vmas)
{
	int flags = 0;

	if (write)
		flags |= FOLL_WRITE;
	if (force)
		flags |= FOLL_FORCE;

	return __get_user_pages(tsk, mm, start, nr_pages, flags, pages, vmas);
}
EXPORT_SYMBOL(get_user_pages);

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

DEFINE_RWLOCK(vmlist_lock);
struct vm_struct *vmlist;

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

void *vmalloc_user(unsigned long size)
{
	void *ret;

	ret = __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO,
			PAGE_KERNEL);
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
	memcpy(buf, addr, count);
	return count;
}

long vwrite(char *buf, char *addr, unsigned long count)
{
	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	memcpy(addr, buf, count);
	return(count);
}

/*
 *	vmalloc  -  allocate virtually continguos memory
 *
 *	@size:		allocation size
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator and map them into continguos kernel virtual space.
 *
 *	For tight control over page level allocator and protection flags
 *	use __vmalloc() instead.
 */
void *vmalloc(unsigned long size)
{
       return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
}
EXPORT_SYMBOL(vmalloc);

void *vmalloc_node(unsigned long size, int node)
{
	return vmalloc(size);
}
EXPORT_SYMBOL(vmalloc_node);

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
 *	page level allocator and map them into continguos kernel virtual space.
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
void  __attribute__((weak)) vmalloc_sync_all(void)
{
}

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
	return mm->brk = brk;
}

/*
 * initialise the VMA and region record slabs
 */
void __init mmap_init(void)
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0);
	VM_BUG_ON(ret);
	vm_region_jar = KMEM_CACHE(vm_region, SLAB_PANIC);
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
	BUG_ON(unlikely(last->vm_end <= last->vm_start));
	BUG_ON(unlikely(last->vm_top < last->vm_end));

	while ((p = rb_next(lastp))) {
		region = rb_entry(p, struct vm_region, vm_rb);
		last = rb_entry(lastp, struct vm_region, vm_rb);

		BUG_ON(unlikely(region->vm_end <= region->vm_start));
		BUG_ON(unlikely(region->vm_top < region->vm_end));
		BUG_ON(unlikely(region->vm_start < last->vm_top));

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

		kdebug("- free %lx", from);
		atomic_long_dec(&mmap_pages_allocated);
		if (page_count(page) != 1)
			kdebug("free page %p: refcount not one: %d",
			       page, page_count(page));
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
	kenter("%p{%d}", region, atomic_read(&region->vm_usage));

	BUG_ON(!nommu_region_tree.rb_node);

	if (atomic_dec_and_test(&region->vm_usage)) {
		if (region->vm_top > region->vm_start)
			delete_nommu_region(region);
		up_write(&nommu_region_sem);

		if (region->vm_file)
			fput(region->vm_file);

		/* IO memory and memory shared directly out of the pagecache
		 * from ramfs/tmpfs mustn't be released here */
		if (region->vm_flags & VM_MAPPED_COPY) {
			kdebug("free series");
			free_page_series(region->vm_start, region->vm_top);
		}
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
 * update protection on a vma
 */
static void protect_vma(struct vm_area_struct *vma, unsigned long flags)
{
#ifdef CONFIG_MPU
	struct mm_struct *mm = vma->vm_mm;
	long start = vma->vm_start & PAGE_MASK;
	while (start < vma->vm_end) {
		protect_page(mm, start, flags);
		start += PAGE_SIZE;
	}
	update_protections(mm);
#endif
}

/*
 * add a VMA into a process's mm_struct in the appropriate place in the list
 * and tree and add to the address space's page tree also if not an anonymous
 * page
 * - should be called with mm->mmap_sem held writelocked
 */
static void add_vma_to_mm(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *pvma, **pp, *next;
	struct address_space *mapping;
	struct rb_node **p, *parent;

	kenter(",%p", vma);

	BUG_ON(!vma->vm_region);

	mm->map_count++;
	vma->vm_mm = mm;

	protect_vma(vma, vma->vm_flags);

	/* add the VMA to the mapping */
	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;

		flush_dcache_mmap_lock(mapping);
		vma_prio_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}

	/* add the VMA to the tree */
	parent = NULL;
	p = &mm->mm_rb.rb_node;
	while (*p) {
		parent = *p;
		pvma = rb_entry(parent, struct vm_area_struct, vm_rb);

		/* sort by: start addr, end addr, VMA struct addr in that order
		 * (the latter is necessary as we may get identical VMAs) */
		if (vma->vm_start < pvma->vm_start)
			p = &(*p)->rb_left;
		else if (vma->vm_start > pvma->vm_start)
			p = &(*p)->rb_right;
		else if (vma->vm_end < pvma->vm_end)
			p = &(*p)->rb_left;
		else if (vma->vm_end > pvma->vm_end)
			p = &(*p)->rb_right;
		else if (vma < pvma)
			p = &(*p)->rb_left;
		else if (vma > pvma)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&vma->vm_rb, parent, p);
	rb_insert_color(&vma->vm_rb, &mm->mm_rb);

	/* add VMA to the VMA list also */
	for (pp = &mm->mmap; (pvma = *pp); pp = &(*pp)->vm_next) {
		if (pvma->vm_start > vma->vm_start)
			break;
		if (pvma->vm_start < vma->vm_start)
			continue;
		if (pvma->vm_end < vma->vm_end)
			break;
	}

	next = *pp;
	*pp = vma;
	vma->vm_next = next;
	if (next)
		next->vm_prev = vma;
}

/*
 * delete a VMA from its owning mm_struct and address space
 */
static void delete_vma_from_mm(struct vm_area_struct *vma)
{
	struct vm_area_struct **pp;
	struct address_space *mapping;
	struct mm_struct *mm = vma->vm_mm;

	kenter("%p", vma);

	protect_vma(vma, 0);

	mm->map_count--;
	if (mm->mmap_cache == vma)
		mm->mmap_cache = NULL;

	/* remove the VMA from the mapping */
	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;

		flush_dcache_mmap_lock(mapping);
		vma_prio_tree_remove(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}

	/* remove from the MM's tree and list */
	rb_erase(&vma->vm_rb, &mm->mm_rb);
	for (pp = &mm->mmap; *pp; pp = &(*pp)->vm_next) {
		if (*pp == vma) {
			*pp = vma->vm_next;
			break;
		}
	}

	vma->vm_mm = NULL;
}

/*
 * destroy a VMA record
 */
static void delete_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	kenter("%p", vma);
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file) {
		fput(vma->vm_file);
		if (vma->vm_flags & VM_EXECUTABLE)
			removed_exe_file_vma(mm);
	}
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
	struct rb_node *n = mm->mm_rb.rb_node;

	/* check the cache first */
	vma = mm->mmap_cache;
	if (vma && vma->vm_start <= addr && vma->vm_end > addr)
		return vma;

	/* trawl the tree (there may be multiple mappings in which addr
	 * resides) */
	for (n = rb_first(&mm->mm_rb); n; n = rb_next(n)) {
		vma = rb_entry(n, struct vm_area_struct, vm_rb);
		if (vma->vm_start > addr)
			return NULL;
		if (vma->vm_end > addr) {
			mm->mmap_cache = vma;
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
	struct rb_node *n = mm->mm_rb.rb_node;
	unsigned long end = addr + len;

	/* check the cache first */
	vma = mm->mmap_cache;
	if (vma && vma->vm_start == addr && vma->vm_end == end)
		return vma;

	/* trawl the tree (there may be multiple mappings in which addr
	 * resides) */
	for (n = rb_first(&mm->mm_rb); n; n = rb_next(n)) {
		vma = rb_entry(n, struct vm_area_struct, vm_rb);
		if (vma->vm_start < addr)
			continue;
		if (vma->vm_start > addr)
			return NULL;
		if (vma->vm_end == end) {
			mm->mmap_cache = vma;
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
	unsigned long reqprot = prot;
	int ret;

	/* do the simple checks first */
	if (flags & MAP_FIXED) {
		printk(KERN_DEBUG
		       "%d: Can't do fixed-address/overlay mmap of RAM\n",
		       current->pid);
		return -EINVAL;
	}

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
		/* validate file mapping requests */
		struct address_space *mapping;

		/* files must support mmap */
		if (!file->f_op || !file->f_op->mmap)
			return -ENODEV;

		/* work out if what we've got could possibly be shared
		 * - we support chardevs that provide their own "memory"
		 * - we support files/blockdevs that are memory backed
		 */
		mapping = file->f_mapping;
		if (!mapping)
			mapping = file->f_path.dentry->d_inode->i_mapping;

		capabilities = 0;
		if (mapping && mapping->backing_dev_info)
			capabilities = mapping->backing_dev_info->capabilities;

		if (!capabilities) {
			/* no explicit capabilities set, so assume some
			 * defaults */
			switch (file->f_path.dentry->d_inode->i_mode & S_IFMT) {
			case S_IFREG:
			case S_IFBLK:
				capabilities = BDI_CAP_MAP_COPY;
				break;

			case S_IFCHR:
				capabilities =
					BDI_CAP_MAP_DIRECT |
					BDI_CAP_READ_MAP |
					BDI_CAP_WRITE_MAP;
				break;

			default:
				return -EINVAL;
			}
		}

		/* eliminate any capabilities that we can't support on this
		 * device */
		if (!file->f_op->get_unmapped_area)
			capabilities &= ~BDI_CAP_MAP_DIRECT;
		if (!file->f_op->read)
			capabilities &= ~BDI_CAP_MAP_COPY;

		/* The file shall have been opened with read permission. */
		if (!(file->f_mode & FMODE_READ))
			return -EACCES;

		if (flags & MAP_SHARED) {
			/* do checks for writing, appending and locking */
			if ((prot & PROT_WRITE) &&
			    !(file->f_mode & FMODE_WRITE))
				return -EACCES;

			if (IS_APPEND(file->f_path.dentry->d_inode) &&
			    (file->f_mode & FMODE_WRITE))
				return -EACCES;

			if (locks_verify_locked(file->f_path.dentry->d_inode))
				return -EAGAIN;

			if (!(capabilities & BDI_CAP_MAP_DIRECT))
				return -ENODEV;

			if (((prot & PROT_READ)  && !(capabilities & BDI_CAP_READ_MAP))  ||
			    ((prot & PROT_WRITE) && !(capabilities & BDI_CAP_WRITE_MAP)) ||
			    ((prot & PROT_EXEC)  && !(capabilities & BDI_CAP_EXEC_MAP))
			    ) {
				printk("MAP_SHARED not completely supported on !MMU\n");
				return -EINVAL;
			}

			/* we mustn't privatise shared mappings */
			capabilities &= ~BDI_CAP_MAP_COPY;
		}
		else {
			/* we're going to read the file into private memory we
			 * allocate */
			if (!(capabilities & BDI_CAP_MAP_COPY))
				return -ENODEV;

			/* we don't permit a private writable mapping to be
			 * shared with the backing device */
			if (prot & PROT_WRITE)
				capabilities &= ~BDI_CAP_MAP_DIRECT;
		}

		/* handle executable mappings and implied executable
		 * mappings */
		if (file->f_path.mnt->mnt_flags & MNT_NOEXEC) {
			if (prot & PROT_EXEC)
				return -EPERM;
		}
		else if ((prot & PROT_READ) && !(prot & PROT_EXEC)) {
			/* handle implication of PROT_EXEC by PROT_READ */
			if (current->personality & READ_IMPLIES_EXEC) {
				if (capabilities & BDI_CAP_EXEC_MAP)
					prot |= PROT_EXEC;
			}
		}
		else if ((prot & PROT_READ) &&
			 (prot & PROT_EXEC) &&
			 !(capabilities & BDI_CAP_EXEC_MAP)
			 ) {
			/* backing file is not executable, try to copy */
			capabilities &= ~BDI_CAP_MAP_DIRECT;
		}
	}
	else {
		/* anonymous mappings are always memory backed and can be
		 * privately mapped
		 */
		capabilities = BDI_CAP_MAP_COPY;

		/* handle PROT_EXEC implication by PROT_READ */
		if ((prot & PROT_READ) &&
		    (current->personality & READ_IMPLIES_EXEC))
			prot |= PROT_EXEC;
	}

	/* allow the security API to have its say */
	ret = security_file_mmap(file, reqprot, prot, flags, addr, 0);
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

	vm_flags = calc_vm_prot_bits(prot) | calc_vm_flag_bits(flags);
	vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	/* vm_flags |= mm->def_flags; */

	if (!(capabilities & BDI_CAP_MAP_DIRECT)) {
		/* attempt to share read-only copies of mapped file chunks */
		if (file && !(prot & PROT_WRITE))
			vm_flags |= VM_MAYSHARE;
	}
	else {
		/* overlay a shareable mapping on the backing device or inode
		 * if possible - used for chardevs, ramfs/tmpfs/shmfs and
		 * romfs/cramfs */
		if (flags & MAP_SHARED)
			vm_flags |= VM_MAYSHARE | VM_SHARED;
		else if ((((vm_flags & capabilities) ^ vm_flags) & BDI_CAP_VMFLAGS) == 0)
			vm_flags |= VM_MAYSHARE;
	}

	/* refuse to let anyone share private mappings with this process if
	 * it's being traced - otherwise breakpoints set in it may interfere
	 * with another untraced process
	 */
	if ((flags & MAP_PRIVATE) && tracehook_expect_breakpoints(current))
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

	ret = vma->vm_file->f_op->mmap(vma->vm_file, vma);
	if (ret == 0) {
		vma->vm_region->vm_top = vma->vm_region->vm_end;
		return 0;
	}
	if (ret != -ENOSYS)
		return ret;

	/* getting an ENOSYS error indicates that direct mmap isn't
	 * possible (as opposed to tried but failed) so we'll fall
	 * through to making a private copy of the data and mapping
	 * that if we can */
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
	struct page *pages;
	unsigned long total, point, n, rlen;
	void *base;
	int ret, order;

	/* invoke the file's mapping function so that it can keep track of
	 * shared mappings on devices or memory
	 * - VM_MAYSHARE will be set if it may attempt to share
	 */
	if (capabilities & BDI_CAP_MAP_DIRECT) {
		ret = vma->vm_file->f_op->mmap(vma->vm_file, vma);
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

	rlen = PAGE_ALIGN(len);

	/* allocate some memory to hold the mapping
	 * - note that this may not return a page-aligned address if the object
	 *   we're allocating is smaller than a page
	 */
	order = get_order(rlen);
	kdebug("alloc order %d for %lx", order, len);

	pages = alloc_pages(GFP_KERNEL, order);
	if (!pages)
		goto enomem;

	total = 1 << order;
	atomic_long_add(total, &mmap_pages_allocated);

	point = rlen >> PAGE_SHIFT;

	/* we allocated a power-of-2 sized page set, so we may want to trim off
	 * the excess */
	if (sysctl_nr_trim_pages && total - point >= sysctl_nr_trim_pages) {
		while (total > point) {
			order = ilog2(total - point);
			n = 1 << order;
			kdebug("shave %lu/%lu @%lu", n, total - point, total);
			atomic_long_sub(n, &mmap_pages_allocated);
			total -= n;
			set_page_refcounted(pages + total);
			__free_pages(pages + total, order);
		}
	}

	for (point = 1; point < total; point++)
		set_page_refcounted(&pages[point]);

	base = page_address(pages);
	region->vm_flags = vma->vm_flags |= VM_MAPPED_COPY;
	region->vm_start = (unsigned long) base;
	region->vm_end   = region->vm_start + rlen;
	region->vm_top   = region->vm_start + (total << PAGE_SHIFT);

	vma->vm_start = region->vm_start;
	vma->vm_end   = region->vm_start + len;

	if (vma->vm_file) {
		/* read the contents of a file into the copy */
		mm_segment_t old_fs;
		loff_t fpos;

		fpos = vma->vm_pgoff;
		fpos <<= PAGE_SHIFT;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = vma->vm_file->f_op->read(vma->vm_file, base, rlen, &fpos);
		set_fs(old_fs);

		if (ret < 0)
			goto error_free;

		/* clear the last little bit */
		if (ret < rlen)
			memset(base + ret, 0, rlen - ret);

	} else {
		/* if it's an anonymous mapping, then just clear it */
		memset(base, 0, rlen);
	}

	return 0;

error_free:
	free_page_series(region->vm_start, region->vm_end);
	region->vm_start = vma->vm_start = 0;
	region->vm_end   = vma->vm_end = 0;
	region->vm_top   = 0;
	return ret;

enomem:
	printk("Allocation of length %lu from process %d (%s) failed\n",
	       len, current->pid, current->comm);
	show_free_areas();
	return -ENOMEM;
}

/*
 * handle mapping creation for uClinux
 */
unsigned long do_mmap_pgoff(struct file *file,
			    unsigned long addr,
			    unsigned long len,
			    unsigned long prot,
			    unsigned long flags,
			    unsigned long pgoff)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *rb;
	unsigned long capabilities, vm_flags, result;
	int ret;

	kenter(",%lx,%lx,%lx,%lx,%lx", addr, len, prot, flags, pgoff);

	/* decide whether we should attempt the mapping, and if so what sort of
	 * mapping */
	ret = validate_mmap_request(file, addr, len, prot, flags, pgoff,
				    &capabilities);
	if (ret < 0) {
		kleave(" = %d [val]", ret);
		return ret;
	}

	/* we ignore the address hint */
	addr = 0;

	/* we've determined that we can make the mapping, now translate what we
	 * now know into VMA flags */
	vm_flags = determine_vm_flags(file, prot, flags, capabilities);

	/* we're going to need to record the mapping */
	region = kmem_cache_zalloc(vm_region_jar, GFP_KERNEL);
	if (!region)
		goto error_getting_region;

	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma)
		goto error_getting_vma;

	atomic_set(&region->vm_usage, 1);
	region->vm_flags = vm_flags;
	region->vm_pgoff = pgoff;

	INIT_LIST_HEAD(&vma->anon_vma_node);
	vma->vm_flags = vm_flags;
	vma->vm_pgoff = pgoff;

	if (file) {
		region->vm_file = file;
		get_file(file);
		vma->vm_file = file;
		get_file(file);
		if (vm_flags & VM_EXECUTABLE) {
			added_exe_file_vma(current->mm);
			vma->vm_mm = current->mm;
		}
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
			if (pregion->vm_file->f_path.dentry->d_inode !=
			    file->f_path.dentry->d_inode)
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
				if (!(capabilities & BDI_CAP_MAP_DIRECT))
					goto sharing_violation;
				continue;
			}

			/* we've found a region we can share */
			atomic_inc(&pregion->vm_usage);
			vma->vm_region = pregion;
			start = pregion->vm_start;
			start += (pgoff - pregion->vm_pgoff) << PAGE_SHIFT;
			vma->vm_start = start;
			vma->vm_end = start + len;

			if (pregion->vm_flags & VM_MAPPED_COPY) {
				kdebug("share copy");
				vma->vm_flags |= VM_MAPPED_COPY;
			} else {
				kdebug("share mmap");
				ret = do_mmap_shared_file(vma);
				if (ret < 0) {
					vma->vm_region = NULL;
					vma->vm_start = 0;
					vma->vm_end = 0;
					atomic_dec(&pregion->vm_usage);
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
		if (capabilities & BDI_CAP_MAP_DIRECT) {
			addr = file->f_op->get_unmapped_area(file, addr, len,
							     pgoff, flags);
			if (IS_ERR((void *) addr)) {
				ret = addr;
				if (ret != (unsigned long) -ENOSYS)
					goto error_just_free;

				/* the driver refused to tell us where to site
				 * the mapping so we'll have to attempt to copy
				 * it */
				ret = (unsigned long) -ENODEV;
				if (!(capabilities & BDI_CAP_MAP_COPY))
					goto error_just_free;

				capabilities &= ~BDI_CAP_MAP_DIRECT;
			} else {
				vma->vm_start = region->vm_start = addr;
				vma->vm_end = region->vm_end = addr + len;
			}
		}
	}

	vma->vm_region = region;

	/* set up the mapping
	 * - the region is filled in if BDI_CAP_MAP_DIRECT is still set
	 */
	if (file && vma->vm_flags & VM_SHARED)
		ret = do_mmap_shared_file(vma);
	else
		ret = do_mmap_private(vma, region, len, capabilities);
	if (ret < 0)
		goto error_just_free;
	add_nommu_region(region);

	/* okay... we have a mapping; now we have to register it */
	result = vma->vm_start;

	current->mm->total_vm += len >> PAGE_SHIFT;

share:
	add_vma_to_mm(current->mm, vma);

	up_write(&nommu_region_sem);

	if (prot & PROT_EXEC)
		flush_icache_range(result, result + len);

	kleave(" = %lx", result);
	return result;

error_just_free:
	up_write(&nommu_region_sem);
error:
	if (region->vm_file)
		fput(region->vm_file);
	kmem_cache_free(vm_region_jar, region);
	if (vma->vm_file)
		fput(vma->vm_file);
	if (vma->vm_flags & VM_EXECUTABLE)
		removed_exe_file_vma(vma->vm_mm);
	kmem_cache_free(vm_area_cachep, vma);
	kleave(" = %d", ret);
	return ret;

sharing_violation:
	up_write(&nommu_region_sem);
	printk(KERN_WARNING "Attempt to share mismatched mappings\n");
	ret = -EINVAL;
	goto error;

error_getting_vma:
	kmem_cache_free(vm_region_jar, region);
	printk(KERN_WARNING "Allocation of vma for %lu byte allocation"
	       " from process %d failed\n",
	       len, current->pid);
	show_free_areas();
	return -ENOMEM;

error_getting_region:
	printk(KERN_WARNING "Allocation of vm region for %lu byte allocation"
	       " from process %d failed\n",
	       len, current->pid);
	show_free_areas();
	return -ENOMEM;
}
EXPORT_SYMBOL(do_mmap_pgoff);

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

	kenter("");

	/* we're only permitted to split anonymous regions that have a single
	 * owner */
	if (vma->vm_file ||
	    atomic_read(&vma->vm_region->vm_usage) != 1)
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

	kenter("");

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
	BUG_ON(atomic_read(&region->vm_usage) != 1);

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
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
{
	struct vm_area_struct *vma;
	struct rb_node *rb;
	unsigned long end = start + len;
	int ret;

	kenter(",%lx,%zx", start, len);

	if (len == 0)
		return -EINVAL;

	/* find the first potentially overlapping VMA */
	vma = find_vma(mm, start);
	if (!vma) {
		static int limit = 0;
		if (limit < 5) {
			printk(KERN_WARNING
			       "munmap of memory not mmapped by process %d"
			       " (%s): 0x%lx-0x%lx\n",
			       current->pid, current->comm,
			       start, start + len - 1);
			limit++;
		}
		return -EINVAL;
	}

	/* we're allowed to split an anonymous VMA but not a file-backed one */
	if (vma->vm_file) {
		do {
			if (start > vma->vm_start) {
				kleave(" = -EINVAL [miss]");
				return -EINVAL;
			}
			if (end == vma->vm_end)
				goto erase_whole_vma;
			rb = rb_next(&vma->vm_rb);
			vma = rb_entry(rb, struct vm_area_struct, vm_rb);
		} while (rb);
		kleave(" = -EINVAL [split file]");
		return -EINVAL;
	} else {
		/* the chunk must be a subset of the VMA found */
		if (start == vma->vm_start && end == vma->vm_end)
			goto erase_whole_vma;
		if (start < vma->vm_start || end > vma->vm_end) {
			kleave(" = -EINVAL [superset]");
			return -EINVAL;
		}
		if (start & ~PAGE_MASK) {
			kleave(" = -EINVAL [unaligned start]");
			return -EINVAL;
		}
		if (end != vma->vm_end && end & ~PAGE_MASK) {
			kleave(" = -EINVAL [unaligned split]");
			return -EINVAL;
		}
		if (start != vma->vm_start && end != vma->vm_end) {
			ret = split_vma(mm, vma, start, 1);
			if (ret < 0) {
				kleave(" = %d [split]", ret);
				return ret;
			}
		}
		return shrink_vma(mm, vma, start, end);
	}

erase_whole_vma:
	delete_vma_from_mm(vma);
	delete_vma(mm, vma);
	kleave(" = 0");
	return 0;
}
EXPORT_SYMBOL(do_munmap);

SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	int ret;
	struct mm_struct *mm = current->mm;

	down_write(&mm->mmap_sem);
	ret = do_munmap(mm, addr, len);
	up_write(&mm->mmap_sem);
	return ret;
}

/*
 * release all the mappings made in a process's VM space
 */
void exit_mmap(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	if (!mm)
		return;

	kenter("");

	mm->total_vm = 0;

	while ((vma = mm->mmap)) {
		mm->mmap = vma->vm_next;
		delete_vma_from_mm(vma);
		delete_vma(mm, vma);
		cond_resched();
	}

	kleave("");
}

unsigned long do_brk(unsigned long addr, unsigned long len)
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
unsigned long do_mremap(unsigned long addr,
			unsigned long old_len, unsigned long new_len,
			unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;

	/* insanity checks first */
	if (old_len == 0 || new_len == 0)
		return (unsigned long) -EINVAL;

	if (addr & ~PAGE_MASK)
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
EXPORT_SYMBOL(do_mremap);

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

struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			unsigned int foll_flags)
{
	return NULL;
}

int remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
		unsigned long to, unsigned long size, pgprot_t prot)
{
	vma->vm_start = vma->vm_pgoff << PAGE_SHIFT;
	return 0;
}
EXPORT_SYMBOL(remap_pfn_range);

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

void swap_unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
}

unsigned long arch_get_unmapped_area(struct file *file, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return -ENOMEM;
}

void arch_unmap_area(struct mm_struct *mm, unsigned long addr)
{
}

void unmap_mapping_range(struct address_space *mapping,
			 loff_t const holebegin, loff_t const holelen,
			 int even_cows)
{
}
EXPORT_SYMBOL(unmap_mapping_range);

/*
 * ask for an unmapped area at which to create a mapping on a file
 */
unsigned long get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	unsigned long (*get_area)(struct file *, unsigned long, unsigned long,
				  unsigned long, unsigned long);

	get_area = current->mm->get_unmapped_area;
	if (file && file->f_op && file->f_op->get_unmapped_area)
		get_area = file->f_op->get_unmapped_area;

	if (!get_area)
		return -ENOSYS;

	return get_area(file, addr, len, pgoff, flags);
}
EXPORT_SYMBOL(get_unmapped_area);

/*
 * Check that a process has enough memory to allocate a new virtual
 * mapping. 0 means there is enough memory for the allocation to
 * succeed and -ENOMEM implies there is not.
 *
 * We currently support three overcommit policies, which are set via the
 * vm.overcommit_memory sysctl.  See Documentation/vm/overcommit-accounting
 *
 * Strict overcommit modes added 2002 Feb 26 by Alan Cox.
 * Additional code 2002 Jul 20 by Robert Love.
 *
 * cap_sys_admin is 1 if the process has admin privileges, 0 otherwise.
 *
 * Note this is a helper function intended to be used by LSMs which
 * wish to use this logic.
 */
int __vm_enough_memory(struct mm_struct *mm, long pages, int cap_sys_admin)
{
	unsigned long free, allowed;

	vm_acct_memory(pages);

	/*
	 * Sometimes we want to use more memory than we have
	 */
	if (sysctl_overcommit_memory == OVERCOMMIT_ALWAYS)
		return 0;

	if (sysctl_overcommit_memory == OVERCOMMIT_GUESS) {
		unsigned long n;

		free = global_page_state(NR_FILE_PAGES);
		free += nr_swap_pages;

		/*
		 * Any slabs which are created with the
		 * SLAB_RECLAIM_ACCOUNT flag claim to have contents
		 * which are reclaimable, under pressure.  The dentry
		 * cache and most inode caches should fall into this
		 */
		free += global_page_state(NR_SLAB_RECLAIMABLE);

		/*
		 * Leave the last 3% for root
		 */
		if (!cap_sys_admin)
			free -= free / 32;

		if (free > pages)
			return 0;

		/*
		 * nr_free_pages() is very expensive on large systems,
		 * only call if we're about to fail.
		 */
		n = nr_free_pages();

		/*
		 * Leave reserved pages. The pages are not for anonymous pages.
		 */
		if (n <= totalreserve_pages)
			goto error;
		else
			n -= totalreserve_pages;

		/*
		 * Leave the last 3% for root
		 */
		if (!cap_sys_admin)
			n -= n / 32;
		free += n;

		if (free > pages)
			return 0;

		goto error;
	}

	allowed = totalram_pages * sysctl_overcommit_ratio / 100;
	/*
	 * Leave the last 3% for root
	 */
	if (!cap_sys_admin)
		allowed -= allowed / 32;
	allowed += total_swap_pages;

	/* Don't let a single process grow too big:
	   leave 3% of the size of this process for other processes */
	if (mm)
		allowed -= mm->total_vm / 32;

	if (percpu_counter_read_positive(&vm_committed_as) < allowed)
		return 0;

error:
	vm_unacct_memory(pages);

	return -ENOMEM;
}

int in_gate_area_no_task(unsigned long addr)
{
	return 0;
}

int filemap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	BUG();
	return 0;
}
EXPORT_SYMBOL(filemap_fault);

/*
 * Access another process' address space.
 * - source/target buffer must be kernel space
 */
int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm;

	if (addr + len < addr)
		return 0;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	down_read(&mm->mmap_sem);

	/* the access must start within one of the target process's mappings */
	vma = find_vma(mm, addr);
	if (vma) {
		/* don't overrun this mapping */
		if (addr + len >= vma->vm_end)
			len = vma->vm_end - addr;

		/* only read or write mappings where it is permitted */
		if (write && vma->vm_flags & VM_MAYWRITE)
			len -= copy_to_user((void *) addr, buf, len);
		else if (!write && vma->vm_flags & VM_MAYREAD)
			len -= copy_from_user(buf, (void *) addr, len);
		else
			len = 0;
	} else {
		len = 0;
	}

	up_read(&mm->mmap_sem);
	mmput(mm);
	return len;
}
