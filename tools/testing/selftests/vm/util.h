/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __KSELFTEST_VM_UTIL_H
#define __KSELFTEST_VM_UTIL_H

#include <stdint.h>
#include <sys/mman.h>
#include <err.h>

#define PAGE_SHIFT	12
#define HPAGE_SHIFT	21

#define PAGE_SIZE (1 << PAGE_SHIFT)
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
