/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_SWAP_TABLE_H
#define _MM_SWAP_TABLE_H

#include <linux/rcupdate.h>
#include <linux/atomic.h>
#include "swap.h"

/* A typical flat array in each cluster as swap table */
struct swap_table {
	atomic_long_t entries[SWAPFILE_CLUSTER];
};

#define SWP_TABLE_USE_PAGE (sizeof(struct swap_table) == PAGE_SIZE)

/*
 * A swap table entry represents the status of a swap slot on a swap
 * (physical or virtual) device. The swap table in each cluster is a
 * 1:1 map of the swap slots in this cluster.
 *
 * Swap table entry type and bits layouts:
 *
 * NULL:     |---------------- 0 ---------------| - Free slot
 * Shadow:   | SWAP_COUNT |---- SHADOW_VAL ---|1| - Swapped out slot
 * PFN:      | SWAP_COUNT |------ PFN -------|10| - Cached slot
 * Pointer:  |----------- Pointer ----------|100| - (Unused)
 * Bad:      |------------- 1 -------------|1000| - Bad slot
 *
 * SWAP_COUNT is `SWP_TB_COUNT_BITS` long, each entry is an atomic long.
 *
 * Usages:
 *
 * - NULL: Swap slot is unused, could be allocated.
 *
 * - Shadow: Swap slot is used and not cached (usually swapped out). It reuses
 *   the XA_VALUE format to be compatible with working set shadows. SHADOW_VAL
 *   part might be all 0 if the working shadow info is absent. In such a case,
 *   we still want to keep the shadow format as a placeholder.
 *
 *   Memcg ID is embedded in SHADOW_VAL.
 *
 * - PFN: Swap slot is in use, and cached. Memcg info is recorded on the page
 *   struct.
 *
 * - Pointer: Unused yet. `0b100` is reserved for potential pointer usage
 *   because only the lower three bits can be used as a marker for 8 bytes
 *   aligned pointers.
 *
 * - Bad: Swap slot is reserved, protects swap header or holes on swap devices.
 */

#if defined(MAX_POSSIBLE_PHYSMEM_BITS)
#define SWAP_CACHE_PFN_BITS (MAX_POSSIBLE_PHYSMEM_BITS - PAGE_SHIFT)
#elif defined(MAX_PHYSMEM_BITS)
#define SWAP_CACHE_PFN_BITS (MAX_PHYSMEM_BITS - PAGE_SHIFT)
#else
#define SWAP_CACHE_PFN_BITS (BITS_PER_LONG - PAGE_SHIFT)
#endif

/* NULL Entry, all 0 */
#define SWP_TB_NULL		0UL

/* Swapped out: shadow */
#define SWP_TB_SHADOW_MARK	0b1UL

/* Cached: PFN */
#define SWP_TB_PFN_BITS		(SWAP_CACHE_PFN_BITS + SWP_TB_PFN_MARK_BITS)
#define SWP_TB_PFN_MARK		0b10UL
#define SWP_TB_PFN_MARK_BITS	2
#define SWP_TB_PFN_MARK_MASK	(BIT(SWP_TB_PFN_MARK_BITS) - 1)

/* SWAP_COUNT part for PFN or shadow, the width can be shrunk or extended */
#define SWP_TB_COUNT_BITS      min(4, BITS_PER_LONG - SWP_TB_PFN_BITS)
#define SWP_TB_COUNT_MASK      (~((~0UL) >> SWP_TB_COUNT_BITS))
#define SWP_TB_COUNT_SHIFT     (BITS_PER_LONG - SWP_TB_COUNT_BITS)
#define SWP_TB_COUNT_MAX       ((1 << SWP_TB_COUNT_BITS) - 1)

/* Bad slot: ends with 0b1000 and rests of bits are all 1 */
#define SWP_TB_BAD		((~0UL) << 3)

/* Macro for shadow offset calculation */
#define SWAP_COUNT_SHIFT	SWP_TB_COUNT_BITS

/*
 * Helpers for casting one type of info into a swap table entry.
 */
static inline unsigned long null_to_swp_tb(void)
{
	BUILD_BUG_ON(sizeof(unsigned long) != sizeof(atomic_long_t));
	return 0;
}

static inline unsigned long __count_to_swp_tb(unsigned char count)
{
	/*
	 * At least three values are needed to distinguish free (0),
	 * used (count > 0 && count < SWP_TB_COUNT_MAX), and
	 * overflow (count == SWP_TB_COUNT_MAX).
	 */
	BUILD_BUG_ON(SWP_TB_COUNT_MAX < 2 || SWP_TB_COUNT_BITS < 2);
	VM_WARN_ON(count > SWP_TB_COUNT_MAX);
	return ((unsigned long)count) << SWP_TB_COUNT_SHIFT;
}

static inline unsigned long pfn_to_swp_tb(unsigned long pfn, unsigned int count)
{
	unsigned long swp_tb;

	BUILD_BUG_ON(sizeof(unsigned long) != sizeof(void *));
	BUILD_BUG_ON(SWAP_CACHE_PFN_BITS >
		     (BITS_PER_LONG - SWP_TB_PFN_MARK_BITS - SWP_TB_COUNT_BITS));

	swp_tb = (pfn << SWP_TB_PFN_MARK_BITS) | SWP_TB_PFN_MARK;
	VM_WARN_ON_ONCE(swp_tb & SWP_TB_COUNT_MASK);

	return swp_tb | __count_to_swp_tb(count);
}

static inline unsigned long folio_to_swp_tb(struct folio *folio, unsigned int count)
{
	return pfn_to_swp_tb(folio_pfn(folio), count);
}

static inline unsigned long shadow_to_swp_tb(void *shadow, unsigned int count)
{
	BUILD_BUG_ON((BITS_PER_XA_VALUE + 1) !=
		     BITS_PER_BYTE * sizeof(unsigned long));
	BUILD_BUG_ON((unsigned long)xa_mk_value(0) != SWP_TB_SHADOW_MARK);

	VM_WARN_ON_ONCE(shadow && !xa_is_value(shadow));
	VM_WARN_ON_ONCE(shadow && ((unsigned long)shadow & SWP_TB_COUNT_MASK));

	return (unsigned long)shadow | __count_to_swp_tb(count) | SWP_TB_SHADOW_MARK;
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
	return ((swp_tb & SWP_TB_PFN_MARK_MASK) == SWP_TB_PFN_MARK);
}

static inline bool swp_tb_is_shadow(unsigned long swp_tb)
{
	return xa_is_value((void *)swp_tb);
}

static inline bool swp_tb_is_bad(unsigned long swp_tb)
{
	return swp_tb == SWP_TB_BAD;
}

static inline bool swp_tb_is_countable(unsigned long swp_tb)
{
	return (swp_tb_is_shadow(swp_tb) || swp_tb_is_folio(swp_tb) ||
		swp_tb_is_null(swp_tb));
}

/*
 * Helpers for retrieving info from swap table.
 */
static inline struct folio *swp_tb_to_folio(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_folio(swp_tb));
	return pfn_folio((swp_tb & ~SWP_TB_COUNT_MASK) >> SWP_TB_PFN_MARK_BITS);
}

static inline void *swp_tb_to_shadow(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_shadow(swp_tb));
	/* No shift needed, xa_value is stored as it is in the lower bits. */
	return (void *)(swp_tb & ~SWP_TB_COUNT_MASK);
}

static inline unsigned char __swp_tb_get_count(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_countable(swp_tb));
	return ((swp_tb & SWP_TB_COUNT_MASK) >> SWP_TB_COUNT_SHIFT);
}

static inline int swp_tb_get_count(unsigned long swp_tb)
{
	if (swp_tb_is_countable(swp_tb))
		return __swp_tb_get_count(swp_tb);
	return -EINVAL;
}

/*
 * Helpers for accessing or modifying the swap table of a cluster,
 * the swap cluster must be locked.
 */
static inline void __swap_table_set(struct swap_cluster_info *ci,
				    unsigned int off, unsigned long swp_tb)
{
	atomic_long_t *table = rcu_dereference_protected(ci->table, true);

	lockdep_assert_held(&ci->lock);
	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);
	atomic_long_set(&table[off], swp_tb);
}

static inline unsigned long __swap_table_xchg(struct swap_cluster_info *ci,
					      unsigned int off, unsigned long swp_tb)
{
	atomic_long_t *table = rcu_dereference_protected(ci->table, true);

	lockdep_assert_held(&ci->lock);
	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);
	/* Ordering is guaranteed by cluster lock, relax */
	return atomic_long_xchg_relaxed(&table[off], swp_tb);
}

static inline unsigned long __swap_table_get(struct swap_cluster_info *ci,
					     unsigned int off)
{
	atomic_long_t *table;

	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);
	table = rcu_dereference_check(ci->table, lockdep_is_held(&ci->lock));

	return atomic_long_read(&table[off]);
}

static inline unsigned long swap_table_get(struct swap_cluster_info *ci,
					unsigned int off)
{
	atomic_long_t *table;
	unsigned long swp_tb;

	VM_WARN_ON_ONCE(off >= SWAPFILE_CLUSTER);

	rcu_read_lock();
	table = rcu_dereference(ci->table);
	swp_tb = table ? atomic_long_read(&table[off]) : null_to_swp_tb();
	rcu_read_unlock();

	return swp_tb;
}
#endif
