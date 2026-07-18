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

/* For storing memcg private id */
struct swap_memcg_table {
	unsigned short id[SWAPFILE_CLUSTER];
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
 * Shadow:   |SWAP_COUNT|Z|---- SHADOW_VAL ---|1| - Swapped out slot
 * PFN:      |SWAP_COUNT|Z|------ PFN -------|10| - Cached slot
 * Pointer:  |----------- Pointer ----------|100| - (Unused)
 * Bad:      |------------- 1 -------------|1000| - Bad slot
 *
 * COUNT is `SWP_TB_COUNT_BITS` long, Z is the `SWP_TB_ZERO_FLAG` bit,
 * and together they form the `SWP_TB_FLAGS_BITS` wide flags field.
 * Each entry is an atomic long.
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

/* NULL Entry, all 0 */
#define SWP_TB_NULL		0UL

/* Swapped out: shadow */
#define SWP_TB_SHADOW_MARK	0b1UL

/* Cached: PFN */
#define SWP_TB_PFN_BITS		(SWAP_CACHE_PFN_BITS + SWAP_CACHE_PFN_MARK_BITS)
#define SWP_TB_PFN_MARK		0b10UL
#define SWP_TB_PFN_MARK_MASK	(BIT(SWAP_CACHE_PFN_MARK_BITS) - 1)

/* Flags: For PFN or shadow, contains SWAP_COUNT, width changes */
#define SWP_TB_FLAGS_BITS	min(5, BITS_PER_LONG - SWP_TB_PFN_BITS)
#define SWP_TB_COUNT_BITS	(SWP_TB_FLAGS_BITS - SWAP_TABLE_HAS_ZEROFLAG)
#define SWP_TB_FLAGS_MASK	(~((~0UL) >> SWP_TB_FLAGS_BITS))
#define SWP_TB_COUNT_MASK      (~((~0UL) >> SWP_TB_COUNT_BITS))
#define SWP_TB_FLAGS_SHIFT     (BITS_PER_LONG - SWP_TB_FLAGS_BITS)
#define SWP_TB_COUNT_SHIFT     (BITS_PER_LONG - SWP_TB_COUNT_BITS)
#define SWP_TB_COUNT_MAX       ((1 << SWP_TB_COUNT_BITS) - 1)
/* The first flag is zero bit (SWAP_TABLE_HAS_ZEROFLAG) */
#define SWP_TB_ZERO_FLAG	BIT(BITS_PER_LONG - SWP_TB_FLAGS_BITS)

/* Bad slot: ends with 0b1000 and rests of bits are all 1 */
#define SWP_TB_BAD		((~0UL) << 3)

/* Macro for shadow offset calculation */
#define SWAP_COUNT_SHIFT	SWP_TB_FLAGS_BITS

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
	BUILD_BUG_ON(SWP_TB_COUNT_BITS < SWAP_COUNT_MIN_BITS);
	VM_WARN_ON(count > SWP_TB_COUNT_MAX);
	return ((unsigned long)count) << SWP_TB_COUNT_SHIFT;
}

static inline unsigned long __flags_to_swp_tb(unsigned char flags)
{
	BUILD_BUG_ON(SWP_TB_FLAGS_BITS > BITS_PER_BYTE);
	VM_WARN_ON(flags >> SWP_TB_FLAGS_BITS);
	return ((unsigned long)flags) << SWP_TB_FLAGS_SHIFT;
}

static inline unsigned long pfn_to_swp_tb(unsigned long pfn, unsigned char flags)
{
	unsigned long swp_tb;

	BUILD_BUG_ON(sizeof(unsigned long) != sizeof(void *));
	BUILD_BUG_ON(SWAP_CACHE_PFN_BITS >
		     (BITS_PER_LONG - SWAP_CACHE_PFN_MARK_BITS - SWP_TB_FLAGS_BITS));

	swp_tb = (pfn << SWAP_CACHE_PFN_MARK_BITS) | SWP_TB_PFN_MARK;
	VM_WARN_ON_ONCE(swp_tb & SWP_TB_FLAGS_MASK);

	return swp_tb | __flags_to_swp_tb(flags);
}

static inline unsigned long folio_to_swp_tb(struct folio *folio, unsigned char flags)
{
	return pfn_to_swp_tb(folio_pfn(folio), flags);
}

static inline unsigned long shadow_to_swp_tb(void *shadow, unsigned char flags)
{
	BUILD_BUG_ON((BITS_PER_XA_VALUE + 1) !=
		     BITS_PER_BYTE * sizeof(unsigned long));
	BUILD_BUG_ON((unsigned long)xa_mk_value(0) != SWP_TB_SHADOW_MARK);

	VM_WARN_ON_ONCE(shadow && !xa_is_value(shadow));
	VM_WARN_ON_ONCE(shadow && ((unsigned long)shadow & SWP_TB_FLAGS_MASK));

	return (unsigned long)shadow | SWP_TB_SHADOW_MARK | __flags_to_swp_tb(flags);
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
	return pfn_folio((swp_tb & ~SWP_TB_FLAGS_MASK) >> SWAP_CACHE_PFN_MARK_BITS);
}

static inline void *swp_tb_to_shadow(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_shadow(swp_tb));
	/* No shift needed, xa_value is stored as it is in the lower bits. */
	return (void *)(swp_tb & ~SWP_TB_FLAGS_MASK);
}

static inline unsigned char __swp_tb_get_count(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_countable(swp_tb));
	return ((swp_tb & SWP_TB_COUNT_MASK) >> SWP_TB_COUNT_SHIFT);
}

static inline unsigned char __swp_tb_get_flags(unsigned long swp_tb)
{
	VM_WARN_ON(!swp_tb_is_countable(swp_tb));
	return ((swp_tb & SWP_TB_FLAGS_MASK) >> SWP_TB_FLAGS_SHIFT);
}

static inline int swp_tb_get_count(unsigned long swp_tb)
{
	if (swp_tb_is_countable(swp_tb))
		return __swp_tb_get_count(swp_tb);
	return -EINVAL;
}

static inline unsigned long __swp_tb_mk_count(unsigned long swp_tb, int count)
{
	return ((swp_tb & ~SWP_TB_COUNT_MASK) | __count_to_swp_tb(count));
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

static inline void __swap_table_set_zero(struct swap_cluster_info *ci,
					 unsigned int ci_off)
{
#if SWAP_TABLE_HAS_ZEROFLAG
	unsigned long swp_tb = __swap_table_get(ci, ci_off);

	BUILD_BUG_ON(SWP_TB_ZERO_FLAG & ~SWP_TB_FLAGS_MASK);
	VM_WARN_ON(!swp_tb_is_countable(swp_tb));
	swp_tb |= SWP_TB_ZERO_FLAG;
	__swap_table_set(ci, ci_off, swp_tb);
#else
	lockdep_assert_held(&ci->lock);
	__set_bit(ci_off, ci->zero_bitmap);
#endif
}

static inline bool __swap_table_test_zero(struct swap_cluster_info *ci,
					  unsigned int ci_off)
{
#if SWAP_TABLE_HAS_ZEROFLAG
	unsigned long swp_tb = __swap_table_get(ci, ci_off);

	VM_WARN_ON(!swp_tb_is_countable(swp_tb));
	return !!(swp_tb & SWP_TB_ZERO_FLAG);
#else
	return test_bit(ci_off, ci->zero_bitmap);
#endif
}

static inline void __swap_table_clear_zero(struct swap_cluster_info *ci,
					   unsigned int ci_off)
{
#if SWAP_TABLE_HAS_ZEROFLAG
	unsigned long swp_tb = __swap_table_get(ci, ci_off);

	VM_WARN_ON(!swp_tb_is_countable(swp_tb));
	swp_tb &= ~SWP_TB_ZERO_FLAG;
	__swap_table_set(ci, ci_off, swp_tb);
#else
	lockdep_assert_held(&ci->lock);
	__clear_bit(ci_off, ci->zero_bitmap);
#endif
}

#ifdef CONFIG_MEMCG
static inline void __swap_cgroup_set(struct swap_cluster_info *ci,
		unsigned int ci_off, unsigned long nr, unsigned short id)
{
	lockdep_assert_held(&ci->lock);
	VM_WARN_ON_ONCE(ci_off >= SWAPFILE_CLUSTER);
	if (WARN_ON_ONCE(!ci->memcg_table))
		return;
	do {
		ci->memcg_table->id[ci_off++] = id;
	} while (--nr);
}

static inline unsigned short __swap_cgroup_get(struct swap_cluster_info *ci,
					       unsigned int ci_off)
{
	lockdep_assert_held(&ci->lock);
	VM_WARN_ON_ONCE(ci_off >= SWAPFILE_CLUSTER);
	if (unlikely(!ci->memcg_table))
		return 0;
	return ci->memcg_table->id[ci_off];
}

static inline unsigned short __swap_cgroup_clear(struct swap_cluster_info *ci,
						 unsigned int ci_off,
						 unsigned long nr)
{
	unsigned short old = __swap_cgroup_get(ci, ci_off);

	if (!old)
		return 0;
	do {
		VM_WARN_ON_ONCE(ci->memcg_table->id[ci_off] != old);
		ci->memcg_table->id[ci_off++] = 0;
	} while (--nr);

	return old;
}
#else
static inline void __swap_cgroup_set(struct swap_cluster_info *ci,
		unsigned int ci_off, unsigned long nr, unsigned short id)
{
}

static inline unsigned short __swap_cgroup_get(struct swap_cluster_info *ci,
					       unsigned int ci_off)
{
	return 0;
}

static inline unsigned short __swap_cgroup_clear(struct swap_cluster_info *ci,
						 unsigned int ci_off,
						 unsigned long nr)
{
	return 0;
}
#endif

#endif
