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

static struct kmem_cache *bio_iostat_ctx_cache;
static mempool_t *bio_iostat_ctx_pool;

static inline unsigned long long iostat_get_avg_bytes(struct f2fs_sb_info *sbi,
	enum iostat_type type)
{
	return sbi->iostat_count[type] ? div64_u64(sbi->iostat_bytes[type],
		sbi->iostat_count[type]) : 0;
}

#define IOSTAT_INFO_SHOW(name, type)					\
	seq_printf(seq, "%-23s %-16llu %-16llu %-16llu\n",		\
			name":", sbi->iostat_bytes[type],		\
			sbi->iostat_count[type],			\
			iostat_get_avg_bytes(sbi, type))

int __maybe_unused iostat_info_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (!sbi->iostat_enable)
		return 0;

	seq_printf(seq, "time:		%-16llu\n", ktime_get_real_seconds());
	seq_printf(seq, "\t\t\t%-16s %-16s %-16s\n",
				"io_bytes", "count", "avg_bytes");

	/* print app write IOs */
	seq_puts(seq, "[WRITE]\n");
	IOSTAT_INFO_SHOW("app buffered data", APP_BUFFERED_IO);
	IOSTAT_INFO_SHOW("app direct data", APP_DIRECT_IO);
	IOSTAT_INFO_SHOW("app mapped data", APP_MAPPED_IO);
	IOSTAT_INFO_SHOW("app buffered cdata", APP_BUFFERED_CDATA_IO);
	IOSTAT_INFO_SHOW("app mapped cdata", APP_MAPPED_CDATA_IO);

	/* print fs write IOs */
	IOSTAT_INFO_SHOW("fs data", FS_DATA_IO);
	IOSTAT_INFO_SHOW("fs cdata", FS_CDATA_IO);
	IOSTAT_INFO_SHOW("fs node", FS_NODE_IO);
	IOSTAT_INFO_SHOW("fs meta", FS_META_IO);
	IOSTAT_INFO_SHOW("fs gc data", FS_GC_DATA_IO);
	IOSTAT_INFO_SHOW("fs gc node", FS_GC_NODE_IO);
	IOSTAT_INFO_SHOW("fs cp data", FS_CP_DATA_IO);
	IOSTAT_INFO_SHOW("fs cp node", FS_CP_NODE_IO);
	IOSTAT_INFO_SHOW("fs cp meta", FS_CP_META_IO);

	/* print app read IOs */
	seq_puts(seq, "[READ]\n");
	IOSTAT_INFO_SHOW("app buffered data", APP_BUFFERED_READ_IO);
	IOSTAT_INFO_SHOW("app direct data", APP_DIRECT_READ_IO);
	IOSTAT_INFO_SHOW("app mapped data", APP_MAPPED_READ_IO);
	IOSTAT_INFO_SHOW("app buffered cdata", APP_BUFFERED_CDATA_READ_IO);
	IOSTAT_INFO_SHOW("app mapped cdata", APP_MAPPED_CDATA_READ_IO);

	/* print fs read IOs */
	IOSTAT_INFO_SHOW("fs data", FS_DATA_READ_IO);
	IOSTAT_INFO_SHOW("fs gc data", FS_GDATA_READ_IO);
	IOSTAT_INFO_SHOW("fs cdata", FS_CDATA_READ_IO);
	IOSTAT_INFO_SHOW("fs node", FS_NODE_READ_IO);
	IOSTAT_INFO_SHOW("fs meta", FS_META_READ_IO);

	/* print other IOs */
	seq_puts(seq, "[OTHER]\n");
	IOSTAT_INFO_SHOW("fs discard", FS_DISCARD_IO);
	IOSTAT_INFO_SHOW("fs flush", FS_FLUSH_IO);

	return 0;
}

static inline void __record_iostat_latency(struct f2fs_sb_info *sbi)
{
	int io, idx;
	struct f2fs_iostat_latency iostat_lat[MAX_IO_TYPE][NR_PAGE_TYPE];
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	unsigned long flags;

	spin_lock_irqsave(&sbi->iostat_lat_lock, flags);
	for (idx = 0; idx < MAX_IO_TYPE; idx++) {
		for (io = 0; io < NR_PAGE_TYPE; io++) {
			iostat_lat[idx][io].peak_lat =
			   jiffies_to_msecs(io_lat->peak_lat[idx][io]);
			iostat_lat[idx][io].cnt = io_lat->bio_cnt[idx][io];
			iostat_lat[idx][io].avg_lat = iostat_lat[idx][io].cnt ?
			   jiffies_to_msecs(io_lat->sum_lat[idx][io]) / iostat_lat[idx][io].cnt : 0;
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
		iostat_diff[i] = sbi->iostat_bytes[i] -
				sbi->prev_iostat_bytes[i];
		sbi->prev_iostat_bytes[i] = sbi->iostat_bytes[i];
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
		sbi->iostat_count[i] = 0;
		sbi->iostat_bytes[i] = 0;
		sbi->prev_iostat_bytes[i] = 0;
	}
	spin_unlock_irq(&sbi->iostat_lock);

	spin_lock_irq(&sbi->iostat_lat_lock);
	memset(io_lat, 0, sizeof(struct iostat_lat_info));
	spin_unlock_irq(&sbi->iostat_lat_lock);
}

static inline void __f2fs_update_iostat(struct f2fs_sb_info *sbi,
			enum iostat_type type, unsigned long long io_bytes)
{
	sbi->iostat_bytes[type] += io_bytes;
	sbi->iostat_count[type]++;
}

void f2fs_update_iostat(struct f2fs_sb_info *sbi, struct inode *inode,
			enum iostat_type type, unsigned long long io_bytes)
{
	unsigned long flags;

	if (!sbi->iostat_enable)
		return;

	spin_lock_irqsave(&sbi->iostat_lock, flags);
	__f2fs_update_iostat(sbi, type, io_bytes);

	if (type == APP_BUFFERED_IO || type == APP_DIRECT_IO)
		__f2fs_update_iostat(sbi, APP_WRITE_IO, io_bytes);

	if (type == APP_BUFFERED_READ_IO || type == APP_DIRECT_READ_IO)
		__f2fs_update_iostat(sbi, APP_READ_IO, io_bytes);

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (inode && f2fs_compressed_file(inode)) {
		if (type == APP_BUFFERED_IO)
			__f2fs_update_iostat(sbi, APP_BUFFERED_CDATA_IO, io_bytes);

		if (type == APP_BUFFERED_READ_IO)
			__f2fs_update_iostat(sbi, APP_BUFFERED_CDATA_READ_IO, io_bytes);

		if (type == APP_MAPPED_READ_IO)
			__f2fs_update_iostat(sbi, APP_MAPPED_CDATA_READ_IO, io_bytes);

		if (type == APP_MAPPED_IO)
			__f2fs_update_iostat(sbi, APP_MAPPED_CDATA_IO, io_bytes);

		if (type == FS_DATA_READ_IO)
			__f2fs_update_iostat(sbi, FS_CDATA_READ_IO, io_bytes);

		if (type == FS_DATA_IO)
			__f2fs_update_iostat(sbi, FS_CDATA_IO, io_bytes);
	}
#endif

	spin_unlock_irqrestore(&sbi->iostat_lock, flags);

	f2fs_record_iostat(sbi);
}

static inline void __update_iostat_latency(struct bio_iostat_ctx *iostat_ctx,
				enum iostat_lat_type lat_type)
{
	unsigned long ts_diff;
	unsigned int page_type = iostat_ctx->type;
	struct f2fs_sb_info *sbi = iostat_ctx->sbi;
	struct iostat_lat_info *io_lat = sbi->iostat_io_lat;
	unsigned long flags;

	if (!sbi->iostat_enable)
		return;

	ts_diff = jiffies - iostat_ctx->submit_ts;
	if (page_type == META_FLUSH) {
		page_type = META;
	} else if (page_type >= NR_PAGE_TYPE) {
		f2fs_warn(sbi, "%s: %d over NR_PAGE_TYPE", __func__, page_type);
		return;
	}

	spin_lock_irqsave(&sbi->iostat_lat_lock, flags);
	io_lat->sum_lat[lat_type][page_type] += ts_diff;
	io_lat->bio_cnt[lat_type][page_type]++;
	if (ts_diff > io_lat->peak_lat[lat_type][page_type])
		io_lat->peak_lat[lat_type][page_type] = ts_diff;
	spin_unlock_irqrestore(&sbi->iostat_lat_lock, flags);
}

void iostat_update_and_unbind_ctx(struct bio *bio)
{
	struct bio_iostat_ctx *iostat_ctx = bio->bi_private;
	enum iostat_lat_type lat_type;

	if (op_is_write(bio_op(bio))) {
		lat_type = bio->bi_opf & REQ_SYNC ?
				WRITE_SYNC_IO : WRITE_ASYNC_IO;
		bio->bi_private = iostat_ctx->sbi;
	} else {
		lat_type = READ_IO;
		bio->bi_private = iostat_ctx->post_read_ctx;
	}

	__update_iostat_latency(iostat_ctx, lat_type);
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
