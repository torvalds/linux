/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PGALLOC_H
#define _LINUX_PGALLOC_H

#include <linux/pgtable.h>
#include <asm/pgalloc.h>

/*
 * {pgd,p4d}_populate_kernel() are defined as macros to allow
 * compile-time optimization based on the configured page table levels.
 * Without this, linking may fail because callers (e.g., KASAN) may rely
 * on calls to these functions being optimized away when passing symbols
 * that exist only for certain page table levels.
 */
#define pgd_populate_kernel(addr, pgd, p4d)				\
	do {								\
		pgd_populate(&init_mm, pgd, p4d);			\
		if (ARCH_PAGE_TABLE_SYNC_MASK & PGTBL_PGD_MODIFIED)	\
			arch_sync_kernel_mappings(addr, addr);		\
	} while (0)

#define p4d_populate_kernel(addr, p4d, pud)				\
	do {								\
		p4d_populate(&init_mm, p4d, pud);			\
		if (ARCH_PAGE_TABLE_SYNC_MASK & PGTBL_P4D_MODIFIED)	\
			arch_sync_kernel_mappings(addr, addr);		\
	} while (0)

#endif /* _LINUX_PGALLOC_H */
