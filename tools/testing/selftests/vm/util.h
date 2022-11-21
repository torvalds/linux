/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __KSELFTEST_VM_UTIL_H
#define __KSELFTEST_VM_UTIL_H

#include <stdint.h>
#include <sys/mman.h>
#include <err.h>
#include <string.h> /* ffsl() */
#include <unistd.h> /* _SC_PAGESIZE */

static unsigned int __page_size;
static unsigned int __page_shift;

static inline unsigned int page_size(void)
{
	if (!__page_size)
		__page_size = sysconf(_SC_PAGESIZE);
	return __page_size;
}

static inline unsigned int page_shift(void)
{
	if (!__page_shift)
		__page_shift = (ffsl(page_size()) - 1);
	return __page_shift;
}

#define PAGE_SHIFT	(page_shift())
#define PAGE_SIZE	(page_size())
/*
 * On ppc64 this will only work with radix 2M hugepage size
 */
#define HPAGE_SHIFT 21
#define HPAGE_SIZE (1 << HPAGE_SHIFT)

#define PAGEMAP_PRESENT(ent)	(((ent) & (1ull << 63)) != 0)
#define PAGEMAP_PFN(ent)	((ent) & ((1ull << 55) - 1))


static inline int64_t allocate_transhuge(void *ptr, int pagemap_fd)
{
	uint64_t ent[2];

	/* drop pmd */
	if (mmap(ptr, HPAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_FIXED | MAP_ANONYMOUS |
		 MAP_NORESERVE | MAP_PRIVATE, -1, 0) != ptr)
		errx(2, "mmap transhuge");

	if (madvise(ptr, HPAGE_SIZE, MADV_HUGEPAGE))
		err(2, "MADV_HUGEPAGE");

	/* allocate transparent huge page */
	*(volatile void **)ptr = ptr;

	if (pread(pagemap_fd, ent, sizeof(ent),
		  (uintptr_t)ptr >> (PAGE_SHIFT - 3)) != sizeof(ent))
		err(2, "read pagemap");

	if (PAGEMAP_PRESENT(ent[0]) && PAGEMAP_PRESENT(ent[1]) &&
	    PAGEMAP_PFN(ent[0]) + 1 == PAGEMAP_PFN(ent[1]) &&
	    !(PAGEMAP_PFN(ent[0]) & ((1 << (HPAGE_SHIFT - PAGE_SHIFT)) - 1)))
		return PAGEMAP_PFN(ent[0]);

	return -1;
}

#endif
