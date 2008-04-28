/*
 *  linux/mm/vmalloc.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  SMP-safe vmalloc/vfree/ioremap, Tigran Aivazian <tigran@veritas.com>, May 2000
 *  Major rework to support vmap/vunmap, Christoph Hellwig, SGI, August 2002
 *  Numa awareness, Christoph Lameter, SGI, June 2005
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/tlbflush.h>


DEFINE_RWLOCK(vmlist_lock);
struct vm_struct *vmlist;

static void *__vmalloc_node(unsigned long size, gfp_t gfp_mask, pgprot_t prot,
			    int node);

static void vunmap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		pte_t ptent = ptep_get_and_clear(&init_mm, addr, pte);
		WARN_ON(!pte_none(ptent) && !pte_present(ptent));
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

static inline void vunmap_pmd_range(pud_t *pud, unsigned long addr,
						unsigned long end)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		vunmap_pte_range(pmd, addr, next);
	} while (pmd++, addr = next, addr != end);
}

static inline void vunmap_pud_range(pgd_t *pgd, unsigned long addr,
						unsigned long end)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		vunmap_pmd_range(pud, addr, next);
	} while (pud++, addr = next, addr != end);
}

void unmap_kernel_range(unsigned long addr, unsigned long size)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long start = addr;
	unsigned long end = addr + size;

	BUG_ON(addr >= end);
	pgd = pgd_offset_k(addr);
	flush_cache_vunmap(addr, end);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		vunmap_pud_range(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
	flush_tlb_kernel_range(start, end);
}

static void unmap_vm_area(struct vm_struct *area)
{
	unmap_kernel_range((unsigned long)area->addr, area->size);
}

static int vmap_pte_range(pmd_t *pmd, unsigned long addr,
			unsigned long end, pgprot_t prot, struct page ***pages)
{
	pte_t *pte;

	pte = pte_alloc_kernel(pmd, addr);
	if (!pte)
		return -ENOMEM;
	do {
		struct page *page = **pages;
		WARN_ON(!pte_none(*pte));
		if (!page)
			return -ENOMEM;
		set_pte_at(&init_mm, addr, pte, mk_pte(page, prot));
		(*pages)++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	return 0;
}

static inline int vmap_pmd_range(pud_t *pud, unsigned long addr,
			unsigned long end, pgprot_t prot, struct page ***pages)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc(&init_mm, pud, addr);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		if (vmap_pte_range(pmd, addr, next, prot, pages))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int vmap_pud_range(pgd_t *pgd, unsigned long addr,
			unsigned long end, pgprot_t prot, struct page ***pages)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_alloc(&init_mm, pgd, addr);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		if (vmap_pmd_range(pud, addr, next, prot, pages))
			return -ENOMEM;
	} while (pud++, addr = next, addr != end);
	return 0;
}

int map_vm_area(struct vm_struct *area, pgprot_t prot, struct page ***pages)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long addr = (unsigned long) area->addr;
	unsigned long end = addr + area->size - PAGE_SIZE;
	int err;

	BUG_ON(addr >= end);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		err = vmap_pud_range(pgd, addr, next, prot, pages);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);
	flush_cache_vmap((unsigned long) area->addr, end);
	return err;
}
EXPORT_SYMBOL_GPL(map_vm_area);

/*
 * Map a vmalloc()-space virtual address to the physical page.
 */
struct page *vmalloc_to_page(const void *vmalloc_addr)
{
	unsigned long addr = (unsigned long) vmalloc_addr;
	struct page *page = NULL;
	pgd_t *pgd = pgd_offset_k(addr);
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (!pgd_none(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (!pud_none(*pud)) {
			pmd = pmd_offset(pud, addr);
			if (!pmd_none(*pmd)) {
				ptep = pte_offset_map(pmd, addr);
				pte = *ptep;
				if (pte_present(pte))
					page = pte_page(pte);
				pte_unmap(ptep);
			}
		}
	}
	return page;
}
EXPORT_SYMBOL(vmalloc_to_page);

/*
 * Map a vmalloc()-space virtual address to the physical page frame number.
 */
unsigned long vmalloc_to_pfn(const void *vmalloc_addr)
{
	return page_to_pfn(vmalloc_to_page(vmalloc_addr));
}
EXPORT_SYMBOL(vmalloc_to_pfn);

static struct vm_struct *__get_vm_area_node(unsigned long size, unsigned long flags,
					    unsigned long start, unsigned long end,
					    int node, gfp_t gfp_mask)
{
	struct vm_struct **p, *tmp, *area;
	unsigned long align = 1;
	unsigned long addr;

	BUG_ON(in_interrupt());
	if (flags & VM_IOREMAP) {
		int bit = fls(size);

		if (bit > IOREMAP_MAX_ORDER)
			bit = IOREMAP_MAX_ORDER;
		else if (bit < PAGE_SHIFT)
			bit = PAGE_SHIFT;

		align = 1ul << bit;
	}
	addr = ALIGN(start, align);
	size = PAGE_ALIGN(size);
	if (unlikely(!size))
		return NULL;

	area = kmalloc_node(sizeof(*area), gfp_mask & GFP_RECLAIM_MASK, node);

	if (unlikely(!area))
		return NULL;

	/*
	 * We always allocate a guard page.
	 */
	size += PAGE_SIZE;

	write_lock(&vmlist_lock);
	for (p = &vmlist; (tmp = *p) != NULL ;p = &tmp->next) {
		if ((unsigned long)tmp->addr < addr) {
			if((unsigned long)tmp->addr + tmp->size >= addr)
				addr = ALIGN(tmp->size + 
					     (unsigned long)tmp->addr, align);
			continue;
		}
		if ((size + addr) < addr)
			goto out;
		if (size + addr <= (unsigned long)tmp->addr)
			goto found;
		addr = ALIGN(tmp->size + (unsigned long)tmp->addr, align);
		if (addr > end - size)
			goto out;
	}
	if ((size + addr) < addr)
		goto out;
	if (addr > end - size)
		goto out;

found:
	area->next = *p;
	*p = area;

	area->flags = flags;
	area->addr = (void *)addr;
	area->size = size;
	area->pages = NULL;
	area->nr_pages = 0;
	area->phys_addr = 0;
	write_unlock(&vmlist_lock);

	return area;

out:
	write_unlock(&vmlist_lock);
	kfree(area);
	if (printk_ratelimit())
		printk(KERN_WARNING "allocation failed: out of vmalloc space - use vmalloc=<size> to increase size.\n");
	return NULL;
}

struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
				unsigned long start, unsigned long end)
{
	return __get_vm_area_node(size, flags, start, end, -1, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(__get_vm_area);

/**
 *	get_vm_area  -  reserve a contiguous kernel virtual area
 *	@size:		size of the area
 *	@flags:		%VM_IOREMAP for I/O mappings or VM_ALLOC
 *
 *	Search an area of @size in the kernel virtual mapping area,
 *	and reserved it for out purposes.  Returns the area descriptor
 *	on success or %NULL on failure.
 */
struct vm_struct *get_vm_area(unsigned long size, unsigned long flags)
{
	return __get_vm_area(size, flags, VMALLOC_START, VMALLOC_END);
}

struct vm_struct *get_vm_area_node(unsigned long size, unsigned long flags,
				   int node, gfp_t gfp_mask)
{
	return __get_vm_area_node(size, flags, VMALLOC_START, VMALLOC_END, node,
				  gfp_mask);
}

/* Caller must hold vmlist_lock */
static struct vm_struct *__find_vm_area(const void *addr)
{
	struct vm_struct *tmp;

	for (tmp = vmlist; tmp != NULL; tmp = tmp->next) {
		 if (tmp->addr == addr)
			break;
	}

	return tmp;
}

/* Caller must hold vmlist_lock */
static struct vm_struct *__remove_vm_area(const void *addr)
{
	struct vm_struct **p, *tmp;

	for (p = &vmlist ; (tmp = *p) != NULL ;p = &tmp->next) {
		 if (tmp->addr == addr)
			 goto found;
	}
	return NULL;

found:
	unmap_vm_area(tmp);
	*p = tmp->next;

	/*
	 * Remove the guard page.
	 */
	tmp->size -= PAGE_SIZE;
	return tmp;
}

/**
 *	remove_vm_area  -  find and remove a continuous kernel virtual area
 *	@addr:		base address
 *
 *	Search for the kernel VM area starting at @addr, and remove it.
 *	This function returns the found VM area, but using it is NOT safe
 *	on SMP machines, except for its size or flags.
 */
struct vm_struct *remove_vm_area(const void *addr)
{
	struct vm_struct *v;
	write_lock(&vmlist_lock);
	v = __remove_vm_area(addr);
	write_unlock(&vmlist_lock);
	return v;
}

static void __vunmap(const void *addr, int deallocate_pages)
{
	struct vm_struct *area;

	if (!addr)
		return;

	if ((PAGE_SIZE-1) & (unsigned long)addr) {
		printk(KERN_ERR "Trying to vfree() bad address (%p)\n", addr);
		WARN_ON(1);
		return;
	}

	area = remove_vm_area(addr);
	if (unlikely(!area)) {
		printk(KERN_ERR "Trying to vfree() nonexistent vm area (%p)\n",
				addr);
		WARN_ON(1);
		return;
	}

	debug_check_no_locks_freed(addr, area->size);

	if (deallocate_pages) {
		int i;

		for (i = 0; i < area->nr_pages; i++) {
			struct page *page = area->pages[i];

			BUG_ON(!page);
			__free_page(page);
		}

		if (area->flags & VM_VPAGES)
			vfree(area->pages);
		else
			kfree(area->pages);
	}

	kfree(area);
	return;
}

/**
 *	vfree  -  release memory allocated by vmalloc()
 *	@addr:		memory base address
 *
 *	Free the virtually continuous memory area starting at @addr, as
 *	obtained from vmalloc(), vmalloc_32() or __vmalloc(). If @addr is
 *	NULL, no operation is performed.
 *
 *	Must not be called in interrupt context.
 */
void vfree(const void *addr)
{
	BUG_ON(in_interrupt());
	__vunmap(addr, 1);
}
EXPORT_SYMBOL(vfree);

/**
 *	vunmap  -  release virtual mapping obtained by vmap()
 *	@addr:		memory base address
 *
 *	Free the virtually contiguous memory area starting at @addr,
 *	which was created from the page array passed to vmap().
 *
 *	Must not be called in interrupt context.
 */
void vunmap(const void *addr)
{
	BUG_ON(in_interrupt());
	__vunmap(addr, 0);
}
EXPORT_SYMBOL(vunmap);

/**
 *	vmap  -  map an array of pages into virtually contiguous space
 *	@pages:		array of page pointers
 *	@count:		number of pages to map
 *	@flags:		vm_area->flags
 *	@prot:		page protection for the mapping
 *
 *	Maps @count pages from @pages into contiguous kernel virtual
 *	space.
 */
void *vmap(struct page **pages, unsigned int count,
		unsigned long flags, pgprot_t prot)
{
	struct vm_struct *area;

	if (count > num_physpages)
		return NULL;

	area = get_vm_area((count << PAGE_SHIFT), flags);
	if (!area)
		return NULL;
	if (map_vm_area(area, prot, &pages)) {
		vunmap(area->addr);
		return NULL;
	}

	return area->addr;
}
EXPORT_SYMBOL(vmap);

static void *__vmalloc_area_node(struct vm_struct *area, gfp_t gfp_mask,
				 pgprot_t prot, int node)
{
	struct page **pages;
	unsigned int nr_pages, array_size, i;

	nr_pages = (area->size - PAGE_SIZE) >> PAGE_SHIFT;
	array_size = (nr_pages * sizeof(struct page *));

	area->nr_pages = nr_pages;
	/* Please note that the recursion is strictly bounded. */
	if (array_size > PAGE_SIZE) {
		pages = __vmalloc_node(array_size, gfp_mask | __GFP_ZERO,
					PAGE_KERNEL, node);
		area->flags |= VM_VPAGES;
	} else {
		pages = kmalloc_node(array_size,
				(gfp_mask & GFP_RECLAIM_MASK) | __GFP_ZERO,
				node);
	}
	area->pages = pages;
	if (!area->pages) {
		remove_vm_area(area->addr);
		kfree(area);
		return NULL;
	}

	for (i = 0; i < area->nr_pages; i++) {
		struct page *page;

		if (node < 0)
			page = alloc_page(gfp_mask);
		else
			page = alloc_pages_node(node, gfp_mask, 0);

		if (unlikely(!page)) {
			/* Successfully allocated i pages, free them in __vunmap() */
			area->nr_pages = i;
			goto fail;
		}
		area->pages[i] = page;
	}

	if (map_vm_area(area, prot, &pages))
		goto fail;
	return area->addr;

fail:
	vfree(area->addr);
	return NULL;
}

void *__vmalloc_area(struct vm_struct *area, gfp_t gfp_mask, pgprot_t prot)
{
	return __vmalloc_area_node(area, gfp_mask, prot, -1);
}

/**
 *	__vmalloc_node  -  allocate virtually contiguous memory
 *	@size:		allocation size
 *	@gfp_mask:	flags for the page level allocator
 *	@prot:		protection mask for the allocated pages
 *	@node:		node to use for allocation or -1
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator with @gfp_mask flags.  Map them into contiguous
 *	kernel virtual space, using a pagetable protection of @prot.
 */
static void *__vmalloc_node(unsigned long size, gfp_t gfp_mask, pgprot_t prot,
			    int node)
{
	struct vm_struct *area;

	size = PAGE_ALIGN(size);
	if (!size || (size >> PAGE_SHIFT) > num_physpages)
		return NULL;

	area = get_vm_area_node(size, VM_ALLOC, node, gfp_mask);
	if (!area)
		return NULL;

	return __vmalloc_area_node(area, gfp_mask, prot, node);
}

void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot)
{
	return __vmalloc_node(size, gfp_mask, prot, -1);
}
EXPORT_SYMBOL(__vmalloc);

/**
 *	vmalloc  -  allocate virtually contiguous memory
 *	@size:		allocation size
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

/**
 * vmalloc_user - allocate zeroed virtually contiguous memory for userspace
 * @size: allocation size
 *
 * The resulting memory area is zeroed so it can be mapped to userspace
 * without leaking data.
 */
void *vmalloc_user(unsigned long size)
{
	struct vm_struct *area;
	void *ret;

	ret = __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO, PAGE_KERNEL);
	if (ret) {
		write_lock(&vmlist_lock);
		area = __find_vm_area(ret);
		area->flags |= VM_USERMAP;
		write_unlock(&vmlist_lock);
	}
	return ret;
}
EXPORT_SYMBOL(vmalloc_user);

/**
 *	vmalloc_node  -  allocate memory on a specific node
 *	@size:		allocation size
 *	@node:		numa node
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator and map them into contiguous kernel virtual space.
 *
 *	For tight control over page level allocator and protection flags
 *	use __vmalloc() instead.
 */
void *vmalloc_node(unsigned long size, int node)
{
	return __vmalloc_node(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL, node);
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

#if defined(CONFIG_64BIT) && defined(CONFIG_ZONE_DMA32)
#define GFP_VMALLOC32 GFP_DMA32 | GFP_KERNEL
#elif defined(CONFIG_64BIT) && defined(CONFIG_ZONE_DMA)
#define GFP_VMALLOC32 GFP_DMA | GFP_KERNEL
#else
#define GFP_VMALLOC32 GFP_KERNEL
#endif

/**
 *	vmalloc_32  -  allocate virtually contiguous memory (32bit addressable)
 *	@size:		allocation size
 *
 *	Allocate enough 32bit PA addressable pages to cover @size from the
 *	page level allocator and map them into contiguous kernel virtual space.
 */
void *vmalloc_32(unsigned long size)
{
	return __vmalloc(size, GFP_VMALLOC32, PAGE_KERNEL);
}
EXPORT_SYMBOL(vmalloc_32);

/**
 * vmalloc_32_user - allocate zeroed virtually contiguous 32bit memory
 *	@size:		allocation size
 *
 * The resulting memory area is 32bit addressable and zeroed so it can be
 * mapped to userspace without leaking data.
 */
void *vmalloc_32_user(unsigned long size)
{
	struct vm_struct *area;
	void *ret;

	ret = __vmalloc(size, GFP_VMALLOC32 | __GFP_ZERO, PAGE_KERNEL);
	if (ret) {
		write_lock(&vmlist_lock);
		area = __find_vm_area(ret);
		area->flags |= VM_USERMAP;
		write_unlock(&vmlist_lock);
	}
	return ret;
}
EXPORT_SYMBOL(vmalloc_32_user);

long vread(char *buf, char *addr, unsigned long count)
{
	struct vm_struct *tmp;
	char *vaddr, *buf_start = buf;
	unsigned long n;

	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	read_lock(&vmlist_lock);
	for (tmp = vmlist; tmp; tmp = tmp->next) {
		vaddr = (char *) tmp->addr;
		if (addr >= vaddr + tmp->size - PAGE_SIZE)
			continue;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			*buf = '\0';
			buf++;
			addr++;
			count--;
		}
		n = vaddr + tmp->size - PAGE_SIZE - addr;
		do {
			if (count == 0)
				goto finished;
			*buf = *addr;
			buf++;
			addr++;
			count--;
		} while (--n > 0);
	}
finished:
	read_unlock(&vmlist_lock);
	return buf - buf_start;
}

long vwrite(char *buf, char *addr, unsigned long count)
{
	struct vm_struct *tmp;
	char *vaddr, *buf_start = buf;
	unsigned long n;

	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	read_lock(&vmlist_lock);
	for (tmp = vmlist; tmp; tmp = tmp->next) {
		vaddr = (char *) tmp->addr;
		if (addr >= vaddr + tmp->size - PAGE_SIZE)
			continue;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			buf++;
			addr++;
			count--;
		}
		n = vaddr + tmp->size - PAGE_SIZE - addr;
		do {
			if (count == 0)
				goto finished;
			*addr = *buf;
			buf++;
			addr++;
			count--;
		} while (--n > 0);
	}
finished:
	read_unlock(&vmlist_lock);
	return buf - buf_start;
}

/**
 *	remap_vmalloc_range  -  map vmalloc pages to userspace
 *	@vma:		vma to cover (map full range of vma)
 *	@addr:		vmalloc memory
 *	@pgoff:		number of pages into addr before first page to map
 *
 *	Returns:	0 for success, -Exxx on failure
 *
 *	This function checks that addr is a valid vmalloc'ed area, and
 *	that it is big enough to cover the vma. Will return failure if
 *	that criteria isn't met.
 *
 *	Similar to remap_pfn_range() (see mm/memory.c)
 */
int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
						unsigned long pgoff)
{
	struct vm_struct *area;
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	int ret;

	if ((PAGE_SIZE-1) & (unsigned long)addr)
		return -EINVAL;

	read_lock(&vmlist_lock);
	area = __find_vm_area(addr);
	if (!area)
		goto out_einval_locked;

	if (!(area->flags & VM_USERMAP))
		goto out_einval_locked;

	if (usize + (pgoff << PAGE_SHIFT) > area->size - PAGE_SIZE)
		goto out_einval_locked;
	read_unlock(&vmlist_lock);

	addr += pgoff << PAGE_SHIFT;
	do {
		struct page *page = vmalloc_to_page(addr);
		ret = vm_insert_page(vma, uaddr, page);
		if (ret)
			return ret;

		uaddr += PAGE_SIZE;
		addr += PAGE_SIZE;
		usize -= PAGE_SIZE;
	} while (usize > 0);

	/* Prevent "things" like memory migration? VM_flags need a cleanup... */
	vma->vm_flags |= VM_RESERVED;

	return ret;

out_einval_locked:
	read_unlock(&vmlist_lock);
	return -EINVAL;
}
EXPORT_SYMBOL(remap_vmalloc_range);

/*
 * Implement a stub for vmalloc_sync_all() if the architecture chose not to
 * have one.
 */
void  __attribute__((weak)) vmalloc_sync_all(void)
{
}


static int f(pte_t *pte, pgtable_t table, unsigned long addr, void *data)
{
	/* apply_to_page_range() does all the hard work. */
	return 0;
}

/**
 *	alloc_vm_area - allocate a range of kernel address space
 *	@size:		size of the area
 *
 *	Returns:	NULL on failure, vm_struct on success
 *
 *	This function reserves a range of kernel address space, and
 *	allocates pagetables to map that range.  No actual mappings
 *	are created.  If the kernel address space is not shared
 *	between processes, it syncs the pagetable across all
 *	processes.
 */
struct vm_struct *alloc_vm_area(size_t size)
{
	struct vm_struct *area;

	area = get_vm_area(size, VM_IOREMAP);
	if (area == NULL)
		return NULL;

	/*
	 * This ensures that page tables are constructed for this region
	 * of kernel virtual address space and mapped into init_mm.
	 */
	if (apply_to_page_range(&init_mm, (unsigned long)area->addr,
				area->size, f, NULL)) {
		free_vm_area(area);
		return NULL;
	}

	/* Make sure the pagetables are constructed in process kernel
	   mappings */
	vmalloc_sync_all();

	return area;
}
EXPORT_SYMBOL_GPL(alloc_vm_area);

void free_vm_area(struct vm_struct *area)
{
	struct vm_struct *ret;
	ret = remove_vm_area(area->addr);
	BUG_ON(ret != area);
	kfree(area);
}
EXPORT_SYMBOL_GPL(free_vm_area);


#ifdef CONFIG_PROC_FS
static void *s_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;
	struct vm_struct *v;

	read_lock(&vmlist_lock);
	v = vmlist;
	while (n > 0 && v) {
		n--;
		v = v->next;
	}
	if (!n)
		return v;

	return NULL;

}

static void *s_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct vm_struct *v = p;

	++*pos;
	return v->next;
}

static void s_stop(struct seq_file *m, void *p)
{
	read_unlock(&vmlist_lock);
}

static int s_show(struct seq_file *m, void *p)
{
	struct vm_struct *v = p;

	seq_printf(m, "0x%p-0x%p %7ld",
		v->addr, v->addr + v->size, v->size);

	if (v->nr_pages)
		seq_printf(m, " pages=%d", v->nr_pages);

	if (v->phys_addr)
		seq_printf(m, " phys=%lx", v->phys_addr);

	if (v->flags & VM_IOREMAP)
		seq_printf(m, " ioremap");

	if (v->flags & VM_ALLOC)
		seq_printf(m, " vmalloc");

	if (v->flags & VM_MAP)
		seq_printf(m, " vmap");

	if (v->flags & VM_USERMAP)
		seq_printf(m, " user");

	if (v->flags & VM_VPAGES)
		seq_printf(m, " vpages");

	seq_putc(m, '\n');
	return 0;
}

const struct seq_operations vmalloc_op = {
	.start = s_start,
	.next = s_next,
	.stop = s_stop,
	.show = s_show,
};
#endif

