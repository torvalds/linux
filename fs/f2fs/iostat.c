// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs iostat support
 *
 * Copyright 2021 Google LLC
 * Author: Daeho Jeong <daehojeong@google.com>
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/seq_file.h>

#include "f2fs.h"
#include "iostat.h"
#include <trace/events/f2fs.h>

int __maybe_unused iostat_info_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	time64_t now = ktime_get_real_seconds();

	if (!sbi->iostat_enable)
		return 0;

	seq_printf(seq, "time:		%-16llu\n", now);

	/* print app write IOs */
	seq_puts(seq, "[WRITE]\n");
	seq_printf(seq, "app buffered:	%-16llu\n",
				sbi->rw_iostat[APP_BUFFERED_IO]);
	seq_printf(seq, "app direct:	%-16llu\n",
				sbi->rw_iostat[APP_DIRECT_IO]);
	seq_printf(seq, "app mapped:	%-16llu\n",
				sbi->rw_iostat[APP_MAPPED_IO]);

	/* print fs write IOs */
	seq_printf(seq, "fs data:	%-16llu\n",
				sbi->rw_iostat[FS_DATA_IO]);
	seq_printf(seq, "fs node:	%-16llu\n",
				sbi->rw_iostat[FS_NODE_IO]);
	seq_printf(seq, "fs meta:	%-16llu\n",
				sbi->rw_iostat[FS_META_IO]);
	seq_printf(seq, "fs gc data:	%-16llu\n",
				sbi->rw_iostat[FS_GC_DATA_IO]);
	seq_printf(seq, "fs gc node:	%-16llu\n",
				sbi->rw_iostat[FS_GC_NODE_IO]);
	seq_printf(seq, "fs cp data:	%-16llu\n",
				sbi->rw_iostat[FS_CP_DATA_IO]);
	seq_printf(seq, "fs cp node:	%-16llu\n",
				sbi->rw_iostat[FS_CP_NODE_IO]);
	seq_printf(seq, "fs cp meta:	%-16llu\n",
				sbi->rw_iostat[FS_CP_META_IO]);

	/* print app read IOs */
	seq_puts(seq, "[READ]\n");
	seq_printf(seq, "app buffered:	%-16llu\n",
				sbi->rw_iostat[APP_BUFFERED_READ_IO]);
	seq_printf(seq, "app direct:	%-16llu\n",
				sbi->rw_iostat[APP_DIRECT_READ_IO]);
	seq_printf(seq, "app mapped:	%-16llu\n",
				sbi->rw_iostat[APP_MAPPED_READ_IO]);

	/* print fs read IOs */
	seq_printf(seq, "fs data:	%-16llu\n",
				sbi->rw_iostat[FS_DATA_READ_IO]);
	seq_printf(seq, "fs gc data:	%-16llu\n",
				sbi->rw_iostat[FS_GDATA_READ_IO]);
	seq_printf(seq, "fs compr_data:	%-16llu\n",
				sbi->rw_iostat[FS_CDATA_READ_IO]);
	seq_printf(seq, "fs node:	%-16llu\n",
				sbi->rw_iostat[FS_NODE_READ_IO]);
	seq_printf(seq, "fs meta:	%-16llu\n",
				sbi->rw_iostat[FS_META_READ_IO]);

	/* print other IOs */
	seq_puts(seq, "[OTHER]\n");
	seq_printf(seq, "fs discard:	%-16llu\n",
				sbi->rw_iostat[FS_DISCARD]);

	return 0;
}

static inline void f2fs_record_iostat(struct f2fs_sb_info *sbi)
{
	unsigned long long iostat_diff[NR_IO_TYPE];
	int i;

	if (time_is_after_jiffies(sbi->iostat_next_period))
		return;

	/* Need double check under the lock */
	spin_lock(&sbi->iostat_lock);
	if (time_is_after_jiffies(sbi->iostat_next_period)) {
		spin_unlock(&sbi->iostat_lock);
		return;
	}
	sbi->iostat_next_period = jiffies +
				msecs_to_jiffies(sbi->iostat_period_ms);

	for (i = 0; i < NR_IO_TYPE; i++) {
		iostat_diff[i] = sbi->rw_iostat[i] -
				sbi->prev_rw_iostat[i];
		sbi->prev_rw_iostat[i] = sbi->rw_iostat[i];
	}
	spin_unlock(&sbi->iostat_lock);

	trace_f2fs_iostat(sbi, iostat_diff);
}

void f2fs_reset_iostat(struct f2fs_sb_info *sbi)
{
	int i;

	spin_lock(&sbi->iostat_lock);
	for (i = 0; i < NR_IO_TYPE; i++) {
		sbi->rw_iostat[i] = 0;
		sbi->prev_rw_iostat[i] = 0;
	}
	spin_unlock(&sbi->iostat_lock);
}

void f2fs_update_iostat(struct f2fs_sb_info *sbi,
			enum iostat_type type, unsigned long long io_bytes)
{
	if (!sbi->iostat_enable)
		return;

	spin_lock(&sbi->iostat_lock);
	sbi->rw_iostat[type] += io_bytes;

	if (type == APP_WRITE_IO || type == APP_DIRECT_IO)
		sbi->rw_iostat[APP_BUFFERED_IO] =
			sbi->rw_iostat[APP_WRITE_IO] -
			sbi->rw_iostat[APP_DIRECT_IO];

	if (type == APP_READ_IO || type == APP_DIRECT_READ_IO)
		sbi->rw_iostat[APP_BUFFERED_READ_IO] =
			sbi->rw_iostat[APP_READ_IO] -
			sbi->rw_iostat[APP_DIRECT_READ_IO];
	spin_unlock(&sbi->iostat_lock);

	f2fs_record_iostat(sbi);
}

int f2fs_init_iostat(struct f2fs_sb_info *sbi)
{
	/* init iostat info */
	spin_lock_init(&sbi->iostat_lock);
	sbi->iostat_enable = false;
	sbi->iostat_period_ms = DEFAULT_IOSTAT_PERIOD_MS;

	return 0;
}
