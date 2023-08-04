// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/nommu.c
 *
 *  Replacement code for mm functions to support CPU's that don't
 *  have any form of memory management unit (thus no virtual memory).
 *
 *  See Documentation/admin-guide/mm/nommu-mmap.rst
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
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/backing-dev.h>
#include <linux/compiler.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/printk.h>

#include <linux/uaccess.h>
#include <linux/uio.h>
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
	return page_size(page);
}

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

void *__vmalloc(unsigned long size, gfp_t gfp_mask)
{
	/*
	 *  You can't specify __GFP_HIGHMEM with kmalloc() since kmalloc()
	 * returns only a logical address.
	 */
	return kmalloc(size, (gfp_mask | __GFP_COMP) & ~__GFP_HIGHMEM);
}
EXPORT_SYMBOL(__vmalloc);

void *__vmalloc_node_range(unsigned long size, unsigned long align,
		unsigned long start, unsigned long end, gfp_t gfp_mask,
		pgprot_t prot, unsigned long vm_flags, int node,
		const void *caller)
{
	return __vmalloc(size, gfp_mask);
}

void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		int node, const void *caller)
{
	return __vmalloc(size, gfp_mask);
}

static void *__vmalloc_user_flags(unsigned long size, gfp_t flags)
{
	void *ret;

	ret = __vmalloc(size, flags);
	if (ret) {
		struct vm_area_struct *vma;

		mmap_write_lock(current->mm);
		vma = find_vma(current->mm, (unsigned long)ret);
		if (vma)
			vm_flags_set(vma, VM_USERMAP);
		mmap_write_unlock(current->mm);
	}

	return ret;
}

void *vmalloc_user(unsigned long size)
{
	return __vmalloc_user_flags(size, GFP_KERNEL | __GFP_ZERO);
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

long vread_iter(struct iov_iter *iter, const char *addr, size_t count)
{
	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	return copy_to_iter(addr, count, iter);
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
	return __vmalloc(size, GFP_KERNEL);
}
EXPORT_SYMBOL(vmalloc);

void *vmalloc_huge(unsigned long size, gfp_t gfp_mask) __weak __alias(__vmalloc);

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
	return __vmalloc(size, GFP_KERNEL | __GFP_ZERO);
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

/**
 * vmalloc_32  -  allocate virtually contiguous memory (32bit addressable)
 *	@size:		allocation size
 *
 *	Allocate enough 32bit PA addressable pages to cover @size from the
 *	page level allocator and map them into contiguous kernel virtual space.
 */
void *vmalloc_32(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL);
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

void *vm_map_ram(struct page **pages, unsigned int count, int node)
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

int vm_map_pages(struct vm_area_struct *vma, struct page **pages,
			unsigned long num)
{
	return -EINVAL;
}
EXPORT_SYMBOL(vm_map_pages);

int vm_map_pages_zero(struct vm_area_struct *vma, struct page **pages,
				unsigned long num)
{
	return -EINVAL;
}
EXPORT_SYMBOL(vm_map_pages_zero);

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
	flush_icache_user_range(mm->brk, brk);
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
		struct page *page = virt_to_page((void *)from);

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

static void setup_vma_to_mm(struct vm_area_struct *vma, struct mm_struct *mm)
{
	vma->vm_mm = mm;

	/* add the VMA to the mapping */
	if (vma->vm_file) {
		struct address_space *mapping = vma->vm_file->f_mapping;

		i_mmap_lock_write(mapping);
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
		i_mmap_unlock_write(mapping);
	}
}

static void cleanup_vma_from_mm(struct vm_area_struct *vma)
{
	vma->vm_mm->map_count--;
	/* remove the VMA from the mapping */
	if (vma->vm_file) {
		struct address_space *mapping;
		mapping = vma->vm_file->f_mapping;

		i_mmap_lock_write(mapping);
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_remove(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
		i_mmap_unlock_write(mapping);
	}
}

/*
 * delete a VMA from its owning mm_struct and address space
 */
static int delete_vma_from_mm(struct vm_area_struct *vma)
{
	VMA_ITERATOR(vmi, vma->vm_mm, vma->vm_start);

	vma_iter_config(&vmi, vma->vm_start, vma->vm_end);
	if (vma_iter_prealloc(&vmi, vma)) {
		pr_warn("Allocation of vma tree for process %d failed\n",
		       current->pid);
		return -ENOMEM;
	}
	cleanup_vma_from_mm(vma);

	/* remove from the MM's tree and list */
	vma_iter_clear(&vmi);
	return 0;
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
	vm_area_free(vma);
}

struct vm_area_struct *find_vma_intersection(struct mm_struct *mm,
					     unsigned long start_addr,
					     unsigned long end_addr)
{
	unsigned long index = start_addr;

	mmap_assert_locked(mm);
	return mt_find(&mm->mm_mt, &index, end_addr - 1);
}
EXPORT_SYMBOL(find_vma_intersection);

/*
 * look up the first VMA in which addr resides, NULL if none
 * - should be called with mm->mmap_lock at least held readlocked
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	VMA_ITERATOR(vmi, mm, addr);

	return vma_iter_load(&vmi);
}
EXPORT_SYMBOL(find_vma);

/*
 * At least xtensa ends up having protection faults even with no
 * MMU.. No stack expansion, at least.
 */
struct vm_area_struct *lock_mm_and_find_vma(struct mm_struct *mm,
			unsigned long addr, struct pt_regs *regs)
{
	struct vm_area_struct *vma;

	mmap_read_lock(mm);
	vma = vma_lookup(mm, addr);
	if (!vma)
		mmap_read_unlock(mm);
	return vma;
}

/*
 * expand a stack to a given address
 * - not supported under NOMMU conditions
 */
int expand_stack_locked(struct vm_area_struct *vma, unsigned long addr)
{
	return -ENOMEM;
}

struct vm_area_struct *expand_stack(struct mm_struct *mm, unsigned long addr)
{
	mmap_read_unlock(mm);
	return NULL;
}

/*
 * look up the first VMA exactly that exactly matches addr
 * - should be called with mm->mmap_lock at least held readlocked
 */
static struct vm_area_struct *find_vma_exact(struct mm_struct *mm,
					     unsigned long addr,
					     unsigned long len)
{
	struct vm_area_struct *vma;
	unsigned long end = addr + len;
	VMA_ITERATOR(vmi, mm, addr);

	vma = vma_iter_load(&vmi);
	if (!vma)
		return NULL;
	if (vma->vm_start != addr)
		return NULL;
	if (vma->vm_end != end)
		return NULL;

	return vma;
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

	if (!file) {
		/*
		 * MAP_ANONYMOUS. MAP_SHARED is mapped to MAP_PRIVATE, because
		 * there is no fork().
		 */
		vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	} else if (flags & MAP_PRIVATE) {
		/* MAP_PRIVATE file mapping */
		if (capabilities & NOMMU_MAP_DIRECT)
			vm_flags |= (capabilities & NOMMU_VMFLAGS);
		else
			vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

		if (!(prot & PROT_WRITE) && !current->ptrace)
			/*
			 * R/O private file mapping which cannot be used to
			 * modify memory, especially also not via active ptrace
			 * (e.g., set breakpoints) or later by upgrading
			 * permissions (no mprotect()). We can try overlaying
			 * the file mapping, which will work e.g., on chardevs,
			 * ramfs/tmpfs/shmfs and romfs/cramf.
			 */
			vm_flags |= VM_MAYOVERLAY;
	} else {
		/* MAP_SHARED file mapping: NOMMU_MAP_DIRECT is set. */
		vm_flags |= VM_SHARED | VM_MAYSHARE |
			    (capabilities & NOMMU_VMFLAGS);
	}

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

	/*
	 * Invoke the file's mapping function so that it can keep track of
	 * shared mappings on devices or memory. VM_MAYOVERLAY will be set if
	 * it may attempt to share, which will make is_nommu_shared_mapping()
	 * happy.
	 */
	if (capabilities & NOMMU_MAP_DIRECT) {
		ret = call_mmap(vma->vm_file, vma);
		/* shouldn't return success if we're not sharing */
		if (WARN_ON_ONCE(!is_nommu_shared_mapping(vma->vm_flags)))
			ret = -ENOSYS;
		if (ret == 0) {
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

	vm_flags_set(vma, VM_MAPPED_COPY);
	region->vm_flags = vma->vm_flags;
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

	} else {
		vma_set_anonymous(vma);
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
	show_mem();
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
			unsigned long pgoff,
			unsigned long *populate,
			struct list_head *uf)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *rb;
	vm_flags_t vm_flags;
	unsigned long capabilities, result;
	int ret;
	VMA_ITERATOR(vmi, current->mm, 0);

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
	vm_flags = determine_vm_flags(file, prot, flags, capabilities);


	/* we're going to need to record the mapping */
	region = kmem_cache_zalloc(vm_region_jar, GFP_KERNEL);
	if (!region)
		goto error_getting_region;

	vma = vm_area_alloc(current->mm);
	if (!vma)
		goto error_getting_vma;

	region->vm_usage = 1;
	region->vm_flags = vm_flags;
	region->vm_pgoff = pgoff;

	vm_flags_init(vma, vm_flags);
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
	if (is_nommu_shared_mapping(vm_flags)) {
		struct vm_region *pregion;
		unsigned long pglen, rpglen, pgend, rpgend, start;

		pglen = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		pgend = pgoff + pglen;

		for (rb = rb_first(&nommu_region_tree); rb; rb = rb_next(rb)) {
			pregion = rb_entry(rb, struct vm_region, vm_rb);

			if (!is_nommu_shared_mapping(pregion->vm_flags))
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
				vm_flags_set(vma, VM_MAPPED_COPY);
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
	if (!vma->vm_file &&
	    (!IS_ENABLED(CONFIG_MMAP_ALLOW_UNINITIALIZED) ||
	     !(flags & MAP_UNINITIALIZED)))
		memset((void *)region->vm_start, 0,
		       region->vm_end - region->vm_start);

	/* okay... we have a mapping; now we have to register it */
	result = vma->vm_start;

	current->mm->total_vm += len >> PAGE_SHIFT;

share:
	BUG_ON(!vma->vm_region);
	vma_iter_config(&vmi, vma->vm_start, vma->vm_end);
	if (vma_iter_prealloc(&vmi, vma))
		goto error_just_free;

	setup_vma_to_mm(vma, current->mm);
	current->mm->map_count++;
	/* add the VMA to the tree */
	vma_iter_store(&vmi, vma);

	/* we flush the region from the icache only when the first executable
	 * mapping of it is made  */
	if (vma->vm_flags & VM_EXEC && !region->vm_icache_flushed) {
		flush_icache_user_range(region->vm_start, region->vm_end);
		region->vm_icache_flushed = true;
	}

	up_write(&nommu_region_sem);

	return result;

error_just_free:
	up_write(&nommu_region_sem);
error:
	vma_iter_free(&vmi);
	if (region->vm_file)
		fput(region->vm_file);
	kmem_cache_free(vm_region_jar, region);
	if (vma->vm_file)
		fput(vma->vm_file);
	vm_area_free(vma);
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
	show_mem();
	return -ENOMEM;

error_getting_region:
	pr_warn("Allocation of vm region for %lu byte allocation from process %d failed\n",
			len, current->pid);
	show_mem();
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
int split_vma(struct vma_iterator *vmi, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	struct vm_area_struct *new;
	struct vm_region *region;
	unsigned long npages;
	struct mm_struct *mm;

	/* we're only permitted to split anonymous regions (these should have
	 * only a single usage on the region) */
	if (vma->vm_file)
		return -ENOMEM;

	mm = vma->vm_mm;
	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	region = kmem_cache_alloc(vm_region_jar, GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	new = vm_area_dup(vma);
	if (!new)
		goto err_vma_dup;

	/* most fields are the same, copy all, and then fixup */
	*region = *vma->vm_region;
	new->vm_region = region;

	npages = (addr - vma->vm_start) >> PAGE_SHIFT;

	if (new_below) {
		region->vm_top = region->vm_end = new->vm_end = addr;
	} else {
		region->vm_start = new->vm_start = addr;
		region->vm_pgoff = new->vm_pgoff += npages;
	}

	vma_iter_config(vmi, new->vm_start, new->vm_end);
	if (vma_iter_prealloc(vmi, vma)) {
		pr_warn("Allocation of vma tree for process %d failed\n",
			current->pid);
		goto err_vmi_preallocate;
	}

	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

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

	setup_vma_to_mm(vma, mm);
	setup_vma_to_mm(new, mm);
	vma_iter_store(vmi, new);
	mm->map_count++;
	return 0;

err_vmi_preallocate:
	vm_area_free(new);
err_vma_dup:
	kmem_cache_free(vm_region_jar, region);
	return -ENOMEM;
}

/*
 * shrink a VMA by removing the specified chunk from either the beginning or
 * the end
 */
static int vmi_shrink_vma(struct vma_iterator *vmi,
		      struct vm_area_struct *vma,
		      unsigned long from, unsigned long to)
{
	struct vm_region *region;

	/* adjust the VMA's pointers, which may reposition it in the MM's tree
	 * and list */
	if (from > vma->vm_start) {
		if (vma_iter_clear_gfp(vmi, from, vma->vm_end, GFP_KERNEL))
			return -ENOMEM;
		vma->vm_end = from;
	} else {
		if (vma_iter_clear_gfp(vmi, vma->vm_start, to, GFP_KERNEL))
			return -ENOMEM;
		vma->vm_start = to;
	}

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
	VMA_ITERATOR(vmi, mm, start);
	struct vm_area_struct *vma;
	unsigned long end;
	int ret = 0;

	len = PAGE_ALIGN(len);
	if (len == 0)
		return -EINVAL;

	end = start + len;

	/* find the first potentially overlapping VMA */
	vma = vma_find(&vmi, end);
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
			vma = vma_find(&vmi, end);
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
			ret = split_vma(&vmi, vma, start, 1);
			if (ret < 0)
				return ret;
		}
		return vmi_shrink_vma(&vmi, vma, start, end);
	}

erase_whole_vma:
	if (delete_vma_from_mm(vma))
		ret = -ENOMEM;
	else
		delete_vma(mm, vma);
	return ret;
}

int vm_munmap(unsigned long addr, size_t len)
{
	struct mm_struct *mm = current->mm;
	int ret;

	mmap_write_lock(mm);
	ret = do_munmap(mm, addr, len, NULL);
	mmap_write_unlock(mm);
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
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	if (!mm)
		return;

	mm->total_vm = 0;

	/*
	 * Lock the mm to avoid assert complaining even though this is the only
	 * user of the mm
	 */
	mmap_write_lock(mm);
	for_each_vma(vmi, vma) {
		cleanup_vma_from_mm(vma);
		delete_vma(mm, vma);
		cond_resched();
	}
	__mt_destroy(&mm->mm_mt);
	mmap_write_unlock(mm);
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

	if (is_nommu_shared_mapping(vma->vm_flags))
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

	mmap_write_lock(current->mm);
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
	mmap_write_unlock(current->mm);
	return ret;
}

struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			 unsigned int foll_flags)
{
	return NULL;
}

int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	if (addr != (pfn << PAGE_SHIFT))
		return -EINVAL;

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
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

vm_fault_t filemap_fault(struct vm_fault *vmf)
{
	BUG();
	return 0;
}
EXPORT_SYMBOL(filemap_fault);

vm_fault_t filemap_map_pages(struct vm_fault *vmf,
		pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	BUG();
	return 0;
}
EXPORT_SYMBOL(filemap_map_pages);

int __access_remote_vm(struct mm_struct *mm, unsigned long addr, void *buf,
		       int len, unsigned int gup_flags)
{
	struct vm_area_struct *vma;
	int write = gup_flags & FOLL_WRITE;

	if (mmap_read_lock_killable(mm))
		return 0;

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

	mmap_read_unlock(mm);

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
	return __access_remote_vm(mm, addr, buf, len, gup_flags);
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

	len = __access_remote_vm(mm, addr, buf, len, gup_flags);

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
 * make sure that any outstanding VMAs aren't broken and then shrink the
 * vm_regions that extend beyond so that do_mmap() doesn't
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

	free_kbytes = K(global_zone_page_state(NR_FREE_PAGES));

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

	free_kbytes = K(global_zone_page_state(NR_FREE_PAGES));

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, 1UL << 13);
	return 0;
}
subsys_initcall(init_admin_reserve);
