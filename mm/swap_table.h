/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_TABLE_H
#define _MM_SWAP_TABLE_H

#include "swap.h"

/*
 * A swap table entry represents the status of a swap slot on a swap
 * (physical or virtual) device. The swap table in each cluster is a
 * 1:1 map of the swap slots in this cluster.
 *
 * Each swap table entry could be a pointer (folio), a XA_VALUE
 * (shadow), or NULL.
 */

/*
 * Helpers for casting one type of info into a swap table entry.
 */
static inline unsigned long null_to_swp_tb(void)
{
	BUILD_BUG_ON(sizeof(unsigned long) != sizeof(atomic_long_t));
	return 0;
}

static inline unsigned long folio_to_swp_tb(struct folio *folio)
{
	BUILD_BUG_ON(sizeof(unsigned long) != sizeof(void *));
	return (unsigned long)folio;
}

static inline unsigned long shadow_swp_to_tb(void *shadow)
{
	BUILD_BUG_ON((BITS_PER_XA_VALUE + 1) !=
		     BITS_PER_BYTE * sizeof(unsigned long));
	VM_WARN_ON_ONCE(shadow && !xa_is_value(shadow));
	return (unsigned long)shadow;
}

/*
 * Helpers for swap table entry type checking.
 */
static inline bool swp_tb_is_null(unsigned long swp_tb)
{
	return !swp_tb;
}

static inline bool swp_tb_is_folio(unsigned long swp_tb)
{
	return !xa_is_value((void *)swp_tb) && !swp_tb_is_null(swp_tb);
}

static inline bool swp_tb_is_shadow(unsigned long swp_tb)
{
	return xa_is_value((void *)swp_tb);
}

/*
 * Helpers for retrieving info from swap table.
 */
static inline struct folio *swp_tb_to_folio(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_folio(swp_tb));
	return (void *)swp_tb;
}

static inline void *swp_tb_to_shadow(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_shadow(swp_tb));
	return (void *)swp_tb;
}

/*
 * Helpers for accessing or modifying the swap table of a cluster,
 * the swap cluster must be locked.
 */
static inline void __swap_table_set(struct swap_cluster_info *ci,
				    unsigned int off, unsigned long swp_tb)
{
	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);
	atomic_long_set(&ci->table[off], swp_tb);
}

static inline unsigned long __swap_table_xchg(struct swap_cluster_info *ci,
					      unsigned int off, unsigned long swp_tb)
{
	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);
	/* Ordering is guaranteed by cluster lock, relax */
	return atomic_long_xchg_relaxed(&ci->table[off], swp_tb);
}

static inline unsigned long __swap_table_get(struct swap_cluster_info *ci,
					     unsigned int off)
{
	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);
	return atomic_long_read(&ci->table[off]);
}
#endif
