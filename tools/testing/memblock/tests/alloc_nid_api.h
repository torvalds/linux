/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MEMBLOCK_ALLOC_NID_H
#define _MEMBLOCK_ALLOC_NID_H

#include "common.h"

int memblock_alloc_nid_checks(void);
int memblock_alloc_exact_nid_range_checks(void);
int __memblock_alloc_nid_numa_checks(void);

#ifdef CONFIG_NUMA
static inline int memblock_alloc_nid_numa_checks(void)
{
	__memblock_alloc_nid_numa_checks();
	return 0;
}

#else
static inline int memblock_alloc_nid_numa_checks(void)
{
	return 0;
}

#endif /* CONFIG_NUMA */

#endif
