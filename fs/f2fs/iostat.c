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

#define NUM_PREALLOC_IOSTAT_CTXS	128
static struct kmem_cache *bio_iostat_ctx_cache;
static mempool_t *bio_iostat_ctx_pool;

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
	seq_printf(seq, "app buffered data:	%-16llu\n",
				sbi->rw_iostat[APP_BUFFERED_IO]);
	seq_printf(seq, "app direct data:	%-16llu\n",
				sbi->rw_iostat[APP_DIRECT_IO]);
	seq_printf(seq, "app mapped data:	%-16llu\n",
				sbi->rw_iostat[APP_MAPPED_IO]);
	seq_printf(seq, "app buffered cdata:	%-16llu\n",
				sbi->rw_iostat[APP_BUFFERED_CDATA_IO]);
	seq_printf(seq, "app mapped cdata:	%-16llu\n",
				sbi->rw_iostat[APP_MAPPED_CDATA_IO]);

	/* print fs write IOs */
	seq_printf(seq, "fs data:		%-16llu\n",
				sbi->rw_iostat[FS_DATA_IO]);
	seq_printf(seq, "fs cdata:		%-16llu\n",
				sbi->rw_iostat[FS_CDATA_IO]);
	seq_printf(seq, "fs node:		%-16llu\n",
				sbi->rw_iostat[FS_NODE_IO]);
	seq_printf(seq, "fs meta:		%-16llu\n",
				sbi->rw_iostat[FS_META_IO]);
	seq_printf(seq, "fs gc data:		%-16llu\n",
				sbi->rw_iostat[FS_GC_DATA_IO]);
	seq_printf(seq, "fs gc node:		%-16llu\n",
				sbi->rw_iostat[FS_GC_NODE_IO]);
	seq_printf(seq, "fs cp data:		%-16llu\n",
				sbi->rw_iostat[FS_CP_DATA_IO]);
	seq_printf(seq, "fs cp node:		%-16llu\n",
				sbi->rw_iostat[FS_CP_NODE_IO]);
	seq_printf(seq, "fs cp meta:		%-16llu\n",
				sbi->rw_iostat[FS_CP_META_IO]);

	/* print app read IOs */
	seq_puts(seq, "[READ]\n");
	seq_printf(seq, "app buffered data:	%-16llu\n",
				sbi->rw_iostat[APP_BUFFERED_READ_IO]);
	seq_printf(seq, "app direct data:	%-16llu\n",
				sbi->rw_iostat[APP_DIRECT_READ_IO]);
	seq_printf(seq, "app mapped data:	%-16llu\n",
				sbi->rw_iostat[APP_MAPPED_READ_IO]);
	seq_printf(seq, "app buffered cdata:	%-16llu\n",
				sbi->rw_iostat[APP_BUFFERED_CDATA_READ_IO]);
	seq_printf(seq, "app mapped cdata:	%-16llu\n",
				sbi->rw_iostat[APP_MAPPED_CDATA_READ_IO]);

	/* print fs read IOs */
	seq_printf(seq, "fs data:		%-16llu\n",
				sbi->rw_iostat[FS_DATA_READ_IO]);
	seq_printf(seq, "fs gc data:		%-16llu\n",
				sbi->rw_iostat[FS_GDATA_READ_IO]);
	seq_printf(seq, "fs cdata:		%-16llu\n",
				sbi->rw_iostat[FS_CDATA_READ_IO]);
	seq_printf(seq, "fs node:		%-16llu\n",
				sbi->rw_iostat[FS_NODE_READ_IO]);
	seq_printf(seq, "fs meta:		%-16llu\n",
				sbi->rw_iostat[FS_META_READ_IO]);

	/* print other IOs */
	seq_puts(seq, "[OTHER]\n");
	seq_printf(seq, "fs discard:		%-16llu\n",
				sbi->rw_iostat[FS_DISCARD]);

	return 0;
}

static inline void __record_iostat_latency(struct f2fs_sb_info *sbi)
{
	int io, idx = 0;
	unsigned int cnt;
	struct f2fs_iostat_latency iostat_lat[MAX_IO_TYPE][NR_PAGE_TYPE];
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	unsigned long flags;

	spin_lock_irqsave(&sbi->iostat_lat_lock, flags);
	for (idx = 0; idx < MAX_IO_TYPE; idx++) {
		for (io = 0; io < NR_PAGE_TYPE; io++) {
			cnt = io_lat->bio_cnt[idx][io];
			iostat_lat[idx][io].peak_lat =
			   jiffies_to_msecs(io_lat->peak_lat[idx][io]);
			iostat_lat[idx][io].cnt = cnt;
			iostat_lat[idx][io].avg_lat = cnt ?
			   jiffies_to_msecs(io_lat->sum_lat[idx][io]) / cnt : 0;
			io_lat->sum_lat[idx][io] = 0;
			io_lat->peak_lat[idx][io] = 0;
			io_lat->bio_cnt[idx][io] = 0;
		}
	}
	spin_unlock_irqrestore(&sbi->iostat_lat_lock, flags);

	trace_f2fs_iostat_latency(sbi, iostat_lat);
}

static inline void f2fs_record_iostat(struct f2fs_sb_info *sbi)
{
	unsigned long long iostat_diff[NR_IO_TYPE];
	int i;
	unsigned long flags;

	if (time_is_after_jiffies(sbi->iostat_next_period))
		return;

	/* Need double check under the lock */
	spin_lock_irqsave(&sbi->iostat_lock, flags);
	if (time_is_after_jiffies(sbi->iostat_next_period)) {
		spin_unlock_irqrestore(&sbi->iostat_lock, flags);
		return;
	}
	sbi->iostat_next_period = jiffies +
				msecs_to_jiffies(sbi->iostat_period_ms);

	for (i = 0; i < NR_IO_TYPE; i++) {
		iostat_diff[i] = sbi->rw_iostat[i] -
				sbi->prev_rw_iostat[i];
		sbi->prev_rw_iostat[i] = sbi->rw_iostat[i];
	}
	spin_unlock_irqrestore(&sbi->iostat_lock, flags);

	trace_f2fs_iostat(sbi, iostat_diff);

	__record_iostat_latency(sbi);
}

void f2fs_reset_iostat(struct f2fs_sb_info *sbi)
{
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	int i;

	spin_lock_irq(&sbi->iostat_lock);
	for (i = 0; i < NR_IO_TYPE; i++) {
		sbi->rw_iostat[i] = 0;
		sbi->prev_rw_iostat[i] = 0;
	}
	spin_unlock_irq(&sbi->iostat_lock);

	spin_lock_irq(&sbi->iostat_lat_lock);
	memset(io_lat, 0, sizeof(struct iostat_lat_info));
	spin_unlock_irq(&sbi->iostat_lat_lock);
}

void f2fs_update_iostat(struct f2fs_sb_info *sbi, struct inode *inode,
			enum iostat_type type, unsigned long long io_bytes)
{
	unsigned long flags;

	if (!sbi->iostat_enable)
		return;

	spin_lock_irqsave(&sbi->iostat_lock, flags);
	sbi->rw_iostat[type] += io_bytes;

	if (type == APP_BUFFERED_IO || type == APP_DIRECT_IO)
		sbi->rw_iostat[APP_WRITE_IO] += io_bytes;

	if (type == APP_BUFFERED_READ_IO || type == APP_DIRECT_READ_IO)
		sbi->rw_iostat[APP_READ_IO] += io_bytes;

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (inode && f2fs_compressed_file(inode)) {
		if (type == APP_BUFFERED_IO)
			sbi->rw_iostat[APP_BUFFERED_CDATA_IO] += io_bytes;

		if (type == APP_BUFFERED_READ_IO)
			sbi->rw_iostat[APP_BUFFERED_CDATA_READ_IO] += io_bytes;

		if (type == APP_MAPPED_READ_IO)
			sbi->rw_iostat[APP_MAPPED_CDATA_READ_IO] += io_bytes;

		if (type == APP_MAPPED_IO)
			sbi->rw_iostat[APP_MAPPED_CDATA_IO] += io_bytes;

		if (type == FS_DATA_READ_IO)
			sbi->rw_iostat[FS_CDATA_READ_IO] += io_bytes;

		if (type == FS_DATA_IO)
			sbi->rw_iostat[FS_CDATA_IO] += io_bytes;
	}
#endif

	spin_unlock_irqrestore(&sbi->iostat_lock, flags);

	f2fs_record_iostat(sbi);
}

static inline void __update_iostat_latency(struct bio_iostat_ctx *iostat_ctx,
				int rw, bool is_sync)
{
	unsigned long ts_diff;
	unsigned int iotype = iostat_ctx->type;
	struct f2fs_sb_info *sbi = iostat_ctx->sbi;
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	int idx;
	unsigned long flags;

	if (!sbi->iostat_enable)
		return;

	ts_diff = jiffies - iostat_ctx->submit_ts;
	if (iotype >= META_FLUSH)
		iotype = META;

	if (rw == 0) {
		idx = READ_IO;
	} else {
		if (is_sync)
			idx = WRITE_SYNC_IO;
		else
			idx = WRITE_ASYNC_IO;
	}

	spin_lock_irqsave(&sbi->iostat_lat_lock, flags);
	io_lat->sum_lat[idx][iotype] += ts_diff;
	io_lat->bio_cnt[idx][iotype]++;
	if (ts_diff > io_lat->peak_lat[idx][iotype])
		io_lat->peak_lat[idx][iotype] = ts_diff;
	spin_unlock_irqrestore(&sbi->iostat_lat_lock, flags);
}

void iostat_update_and_unbind_ctx(struct bio *bio, int rw)
{
	struct bio_iostat_ctx *iostat_ctx = bio->bi_private;
	bool is_sync = bio->bi_opf & REQ_SYNC;

	if (rw == 0)
		bio->bi_private = iostat_ctx->post_read_ctx;
	else
		bio->bi_private = iostat_ctx->sbi;
	__update_iostat_latency(iostat_ctx, rw, is_sync);
	mempool_free(iostat_ctx, bio_iostat_ctx_pool);
}

void iostat_alloc_and_bind_ctx(struct f2fs_sb_info *sbi,
		struct bio *bio, struct bio_post_read_ctx *ctx)
{
	struct bio_iostat_ctx *iostat_ctx;
	/* Due to the mempool, this never fails. */
	iostat_ctx = mempool_alloc(bio_iostat_ctx_pool, GFP_NOFS);
	iostat_ctx->sbi = sbi;
	iostat_ctx->submit_ts = 0;
	iostat_ctx->type = 0;
	iostat_ctx->post_read_ctx = ctx;
	bio->bi_private = iostat_ctx;
}

int __init f2fs_init_iostat_processing(void)
{
	bio_iostat_ctx_cache =
		kmem_cache_create("f2fs_bio_iostat_ctx",
				  sizeof(struct bio_iostat_ctx), 0, 0, NULL);
	if (!bio_iostat_ctx_cache)
		goto fail;
	bio_iostat_ctx_pool =
		mempool_create_slab_pool(NUM_PREALLOC_IOSTAT_CTXS,
					 bio_iostat_ctx_cache);
	if (!bio_iostat_ctx_pool)
		goto fail_free_cache;
	return 0;

fail_free_cache:
	kmem_cache_destroy(bio_iostat_ctx_cache);
fail:
	return -ENOMEM;
}

void f2fs_destroy_iostat_processing(void)
{
	mempool_destroy(bio_iostat_ctx_pool);
	kmem_cache_destroy(bio_iostat_ctx_cache);
}

int f2fs_init_iostat(struct f2fs_sb_info *sbi)
{
	/* init iostat info */
	spin_lock_init(&sbi->iostat_lock);
	spin_lock_init(&sbi->iostat_lat_lock);
	sbi->iostat_enable = false;
	sbi->iostat_period_ms = DEFAULT_IOSTAT_PERIOD_MS;
	sbi->iostat_io_lat = f2fs_kzalloc(sbi, sizeof(struct iostat_lat_info),
					GFP_KERNEL);
	if (!sbi->iostat_io_lat)
		return -ENOMEM;

	return 0;
}

void f2fs_destroy_iostat(struct f2fs_sb_info *sbi)
{
	kfree(sbi->iostat_io_lat);
}
