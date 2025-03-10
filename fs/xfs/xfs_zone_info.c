// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023-2025 Christoph Hellwig.
 * Copyright (c) 2024-2025, Western Digital Corporation or its affiliates.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_rtgroup.h"
#include "xfs_zone_alloc.h"
#include "xfs_zone_priv.h"

static const char xfs_write_hint_shorthand[6][16] = {
	"NOT_SET", "NONE", "SHORT", "MEDIUM", "LONG", "EXTREME"};

static inline const char *
xfs_write_hint_to_str(
	uint8_t			write_hint)
{
	if (write_hint > WRITE_LIFE_EXTREME)
		return "UNKNOWN";
	return xfs_write_hint_shorthand[write_hint];
}

static void
xfs_show_open_zone(
	struct seq_file		*m,
	struct xfs_open_zone	*oz)
{
	seq_printf(m, "\t  zone %d, wp %u, written %u, used %u, hint %s\n",
		rtg_rgno(oz->oz_rtg),
		oz->oz_write_pointer, oz->oz_written,
		rtg_rmap(oz->oz_rtg)->i_used_blocks,
		xfs_write_hint_to_str(oz->oz_write_hint));
}

static void
xfs_show_full_zone_used_distribution(
	struct seq_file         *m,
	struct xfs_mount        *mp)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	unsigned int		reclaimable = 0, full, i;

	spin_lock(&zi->zi_used_buckets_lock);
	for (i = 0; i < XFS_ZONE_USED_BUCKETS; i++) {
		unsigned int entries = zi->zi_used_bucket_entries[i];

		seq_printf(m, "\t  %2u..%2u%%: %u\n",
				i * (100 / XFS_ZONE_USED_BUCKETS),
				(i + 1) * (100 / XFS_ZONE_USED_BUCKETS) - 1,
				entries);
		reclaimable += entries;
	}
	spin_unlock(&zi->zi_used_buckets_lock);

	full = mp->m_sb.sb_rgcount;
	if (zi->zi_open_gc_zone)
		full--;
	full -= zi->zi_nr_open_zones;
	full -= atomic_read(&zi->zi_nr_free_zones);
	full -= reclaimable;

	seq_printf(m, "\t     100%%: %u\n", full);
}

void
xfs_zoned_show_stats(
	struct seq_file		*m,
	struct xfs_mount	*mp)
{
	struct xfs_zone_info	*zi = mp->m_zone_info;
	struct xfs_open_zone	*oz;

	seq_puts(m, "\n");

	seq_printf(m, "\tuser free RT blocks: %lld\n",
		xfs_sum_freecounter(mp, XC_FREE_RTEXTENTS));
	seq_printf(m, "\treserved free RT blocks: %lld\n",
		mp->m_free[XC_FREE_RTEXTENTS].res_avail);
	seq_printf(m, "\tuser available RT blocks: %lld\n",
		xfs_sum_freecounter(mp, XC_FREE_RTAVAILABLE));
	seq_printf(m, "\treserved available RT blocks: %lld\n",
		mp->m_free[XC_FREE_RTAVAILABLE].res_avail);
	seq_printf(m, "\tRT reservations required: %d\n",
		!list_empty_careful(&zi->zi_reclaim_reservations));
	seq_printf(m, "\tRT GC required: %d\n",
		xfs_zoned_need_gc(mp));

	seq_printf(m, "\tfree zones: %d\n", atomic_read(&zi->zi_nr_free_zones));
	seq_puts(m, "\topen zones:\n");
	spin_lock(&zi->zi_open_zones_lock);
	list_for_each_entry(oz, &zi->zi_open_zones, oz_entry)
		xfs_show_open_zone(m, oz);
	if (zi->zi_open_gc_zone) {
		seq_puts(m, "\topen gc zone:\n");
		xfs_show_open_zone(m, zi->zi_open_gc_zone);
	}
	spin_unlock(&zi->zi_open_zones_lock);
	seq_puts(m, "\tused blocks distribution (fully written zones):\n");
	xfs_show_full_zone_used_distribution(m, mp);
}
