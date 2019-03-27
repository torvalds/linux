/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
 * Copyright (c) 2015 François Tigeot
 * Copyright (c) 2015 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_MM_H_
#define	_LINUX_MM_H_

#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/pfn.h>
#include <linux/list.h>

#include <asm/pgtable.h>

#define	PAGE_ALIGN(x)	ALIGN(x, PAGE_SIZE)

/*
 * Make sure our LinuxKPI defined virtual memory flags don't conflict
 * with the ones defined by FreeBSD:
 */
CTASSERT((VM_PROT_ALL & -(1 << 8)) == 0);

#define	VM_READ			VM_PROT_READ
#define	VM_WRITE		VM_PROT_WRITE
#define	VM_EXEC			VM_PROT_EXECUTE

#define	VM_PFNINTERNAL		(1 << 8)	/* FreeBSD private flag to vm_insert_pfn() */
#define	VM_MIXEDMAP		(1 << 9)
#define	VM_NORESERVE		(1 << 10)
#define	VM_PFNMAP		(1 << 11)
#define	VM_IO			(1 << 12)
#define	VM_MAYWRITE		(1 << 13)
#define	VM_DONTCOPY		(1 << 14)
#define	VM_DONTEXPAND		(1 << 15)
#define	VM_DONTDUMP		(1 << 16)

#define	VMA_MAX_PREFAULT_RECORD	1

#define	FOLL_WRITE		(1 << 0)
#define	FOLL_FORCE		(1 << 1)

#define	VM_FAULT_OOM		(1 << 0)
#define	VM_FAULT_SIGBUS		(1 << 1)
#define	VM_FAULT_MAJOR		(1 << 2)
#define	VM_FAULT_WRITE		(1 << 3)
#define	VM_FAULT_HWPOISON	(1 << 4)
#define	VM_FAULT_HWPOISON_LARGE	(1 << 5)
#define	VM_FAULT_SIGSEGV	(1 << 6)
#define	VM_FAULT_NOPAGE		(1 << 7)
#define	VM_FAULT_LOCKED		(1 << 8)
#define	VM_FAULT_RETRY		(1 << 9)
#define	VM_FAULT_FALLBACK	(1 << 10)

#define	FAULT_FLAG_WRITE	(1 << 0)
#define	FAULT_FLAG_MKWRITE	(1 << 1)
#define	FAULT_FLAG_ALLOW_RETRY	(1 << 2)
#define	FAULT_FLAG_RETRY_NOWAIT	(1 << 3)
#define	FAULT_FLAG_KILLABLE	(1 << 4)
#define	FAULT_FLAG_TRIED	(1 << 5)
#define	FAULT_FLAG_USER		(1 << 6)
#define	FAULT_FLAG_REMOTE	(1 << 7)
#define	FAULT_FLAG_INSTRUCTION	(1 << 8)

typedef int (*pte_fn_t)(linux_pte_t *, pgtable_t, unsigned long addr, void *data);

struct vm_area_struct {
	vm_offset_t vm_start;
	vm_offset_t vm_end;
	vm_offset_t vm_pgoff;
	pgprot_t vm_page_prot;
	unsigned long vm_flags;
	struct mm_struct *vm_mm;
	void   *vm_private_data;
	const struct vm_operations_struct *vm_ops;
	struct linux_file *vm_file;

	/* internal operation */
	vm_paddr_t vm_pfn;		/* PFN for memory map */
	vm_size_t vm_len;		/* length for memory map */
	vm_pindex_t vm_pfn_first;
	int	vm_pfn_count;
	int    *vm_pfn_pcount;
	vm_object_t vm_obj;
	vm_map_t vm_cached_map;
	TAILQ_ENTRY(vm_area_struct) vm_entry;
};

struct vm_fault {
	unsigned int flags;
	pgoff_t	pgoff;
	union {
		/* user-space address */
		void *virtual_address;	/* < 4.11 */
		unsigned long address;	/* >= 4.11 */
	};
	struct page *page;
	struct vm_area_struct *vma;
};

struct vm_operations_struct {
	void    (*open) (struct vm_area_struct *);
	void    (*close) (struct vm_area_struct *);
	int     (*fault) (struct vm_area_struct *, struct vm_fault *);
	int	(*access) (struct vm_area_struct *, unsigned long, void *, int, int);
};

struct sysinfo {
	uint64_t totalram;
	uint64_t totalhigh;
	uint32_t mem_unit;
};

/*
 * Compute log2 of the power of two rounded up count of pages
 * needed for size bytes.
 */
static inline int
get_order(unsigned long size)
{
	int order;

	size = (size - 1) >> PAGE_SHIFT;
	order = 0;
	while (size) {
		order++;
		size >>= 1;
	}
	return (order);
}

static inline void *
lowmem_page_address(struct page *page)
{
	return (page_address(page));
}

/*
 * This only works via memory map operations.
 */
static inline int
io_remap_pfn_range(struct vm_area_struct *vma,
    unsigned long addr, unsigned long pfn, unsigned long size,
    vm_memattr_t prot)
{
	vma->vm_page_prot = prot;
	vma->vm_pfn = pfn;
	vma->vm_len = size;

	return (0);
}

static inline int
apply_to_page_range(struct mm_struct *mm, unsigned long address,
    unsigned long size, pte_fn_t fn, void *data)
{
	return (-ENOTSUP);
}

int zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
    unsigned long size);

static inline int
remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	return (-ENOTSUP);
}

static inline unsigned long
vma_pages(struct vm_area_struct *vma)
{
	return ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT);
}

#define	offset_in_page(off)	((off) & (PAGE_SIZE - 1))

static inline void
set_page_dirty(struct vm_page *page)
{
	vm_page_dirty(page);
}

static inline void
set_page_dirty_lock(struct vm_page *page)
{
	vm_page_lock(page);
	vm_page_dirty(page);
	vm_page_unlock(page);
}

static inline void
mark_page_accessed(struct vm_page *page)
{
	vm_page_reference(page);
}

static inline void
get_page(struct vm_page *page)
{
	vm_page_lock(page);
	vm_page_wire(page);
	vm_page_unlock(page);
}

extern long
get_user_pages(unsigned long start, unsigned long nr_pages,
    int gup_flags, struct page **,
    struct vm_area_struct **);

extern int
__get_user_pages_fast(unsigned long start, int nr_pages, int write,
    struct page **);

extern long
get_user_pages_remote(struct task_struct *, struct mm_struct *,
    unsigned long start, unsigned long nr_pages,
    int gup_flags, struct page **,
    struct vm_area_struct **);

static inline void
put_page(struct vm_page *page)
{
	vm_page_lock(page);
	if (vm_page_unwire(page, PQ_ACTIVE) && page->object == NULL)
		vm_page_free(page);
	vm_page_unlock(page);
}

#define	copy_highpage(to, from) pmap_copy_page(from, to)

static inline pgprot_t
vm_get_page_prot(unsigned long vm_flags)
{
	return (vm_flags & VM_PROT_ALL);
}

static inline vm_page_t
vmalloc_to_page(const void *addr)
{
	vm_paddr_t paddr;

	paddr = pmap_kextract((vm_offset_t)addr);
	return (PHYS_TO_VM_PAGE(paddr));
}

extern int is_vmalloc_addr(const void *addr);
void si_meminfo(struct sysinfo *si);

#endif					/* _LINUX_MM_H_ */
