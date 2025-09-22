/* Public domain. */

#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <sys/types.h>
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/stdint.h>
#include <sys/atomic.h>
#include <machine/cpu.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_glue.h>
#include <lib/libkern/libkern.h> /* for flsl */
#include <linux/shrinker.h>
#include <linux/overflow.h>
#include <linux/pgtable.h>

#define PageHighMem(x)	0

#define page_to_phys(page)	(VM_PAGE_TO_PHYS(page))
#define page_to_pfn(pp)		(VM_PAGE_TO_PHYS(pp) / PAGE_SIZE)
#define pfn_to_page(pfn)	(PHYS_TO_VM_PAGE(ptoa(pfn)))
#define nth_page(page, n)	(&(page)[(n)])
#define offset_in_page(off)	((vaddr_t)(off) & PAGE_MASK)
#define set_page_dirty(page)	atomic_clearbits_int(&page->pg_flags, PG_CLEAN)

#define PAGE_ALIGN(addr)	(((addr) + PAGE_MASK) & ~PAGE_MASK)

#define PFN_UP(x)		(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)		((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)		((x) << PAGE_SHIFT)

bool is_vmalloc_addr(const void *);

static inline void *
kvmalloc(size_t size, gfp_t flags)
{
	return malloc(size, M_DRM, flags);
}

static inline void *
kvmalloc_array(size_t n, size_t size, int flags)
{
	if (n != 0 && SIZE_MAX / n < size)
		return NULL;
	return malloc(n * size, M_DRM, flags);
}

static inline struct vm_page *
vmalloc_to_page(const void *va)
{
	return uvm_atopg((vaddr_t)va);
}

static inline struct vm_page *
virt_to_page(const void *va)
{
	return uvm_atopg((vaddr_t)va);
}

static inline void *
kvcalloc(size_t n, size_t size, int flags)
{
	return kvmalloc_array(n, size, flags | M_ZERO);
}

static inline void *
kvzalloc(size_t size, int flags)
{
	return malloc(size, M_DRM, flags | M_ZERO);
}

static inline void
kvfree(const void *objp)
{
	free((void *)objp, M_DRM, 0);
}

static inline long
si_mem_available(void)
{
	return uvmexp.free;
}

static inline unsigned int
get_order(size_t size)
{
	return flsl((size - 1) >> PAGE_SHIFT);
}

static inline long
totalram_pages(void)
{
	return uvmexp.npages;
}

static inline bool
want_init_on_free(void)
{
	return false;
}

#endif
