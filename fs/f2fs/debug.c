// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs debugging statistics
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2012 Linux Foundation
 * Copyright (c) 2012 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 */

#include <linux/fs.h>
#include <linux/backing-dev.h>
#include <linux/f2fs_fs.h>
#include <linux/blkdev.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "gc.h"

static LIST_HEAD(f2fs_stat_list);
static DEFINE_MUTEX(f2fs_stat_mutex);
#ifdef CONFIG_DEBUG_FS
static struct dentry *f2fs_debugfs_root;
#endif

/*
 * This function calculates BDF of every segments
 */
void f2fs_update_sit_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = F2FS_STAT(sbi);
	unsigned long long blks_per_sec, hblks_per_sec, total_vblocks;
	unsigned long long bimodal, dist;
	unsigned int segno, vblocks;
	int ndirty = 0;

	bimodal = 0;
	total_vblocks = 0;
	blks_per_sec = BLKS_PER_SEC(sbi);
	hblks_per_sec = blks_per_sec / 2;
	for (segno = 0; segno < MAIN_SEGS(sbi); segno += sbi->segs_per_sec) {
		vblocks = get_valid_blocks(sbi, segno, true);
		dist = abs(vblocks - hblks_per_sec);
		bimodal += dist * dist;

		if (vblocks > 0 && vblocks < blks_per_sec) {
			total_vblocks += vblocks;
			ndirty++;
		}
	}
	dist = div_u64(MAIN_SECS(sbi) * hblks_per_sec * hblks_per_sec, 100);
	si->bimodal = div64_u64(bimodal, dist);
	if (si->dirty_count)
		si->avg_vblocks = div_u64(total_vblocks, ndirty);
	else
		si->avg_vblocks = 0;
}

#ifdef CONFIG_DEBUG_FS
static void update_general_status(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = F2FS_STAT(sbi);
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	int i;

	/* these will be changed if online resize is done */
	si->main_area_segs = le32_to_cpu(raw_super->segment_count_main);
	si->main_area_sections = le32_to_cpu(raw_super->section_count);
	si->main_area_zones = si->main_area_sections /
				le32_to_cpu(raw_super->secs_per_zone);

	/* general extent cache stats */
	for (i = 0; i < NR_EXTENT_CACHES; i++) {
		struct extent_tree_info *eti = &sbi->extent_tree[i];

		si->hit_cached[i] = atomic64_read(&sbi->read_hit_cached[i]);
		si->hit_rbtree[i] = atomic64_read(&sbi->read_hit_rbtree[i]);
		si->total_ext[i] = atomic64_read(&sbi->total_hit_ext[i]);
		si->hit_total[i] = si->hit_cached[i] + si->hit_rbtree[i];
		si->ext_tree[i] = atomic_read(&eti->total_ext_tree);
		si->zombie_tree[i] = atomic_read(&eti->total_zombie_tree);
		si->ext_node[i] = atomic_read(&eti->total_ext_node);
	}
	/* read extent_cache only */
	si->hit_largest = atomic64_read(&sbi->read_hit_largest);
	si->hit_total[EX_READ] += si->hit_largest;

	/* block age extent_cache only */
	si->allocated_data_blocks = atomic64_read(&sbi->allocated_data_blocks);

	/* validation check of the segment numbers */
	si->ndirty_node = get_pages(sbi, F2FS_DIRTY_NODES);
	si->ndirty_dent = get_pages(sbi, F2FS_DIRTY_DENTS);
	si->ndirty_meta = get_pages(sbi, F2FS_DIRTY_META);
	si->ndirty_data = get_pages(sbi, F2FS_DIRTY_DATA);
	si->ndirty_qdata = get_pages(sbi, F2FS_DIRTY_QDATA);
	si->ndirty_imeta = get_pages(sbi, F2FS_DIRTY_IMETA);
	si->ndirty_dirs = sbi->ndirty_inode[DIR_INODE];
	si->ndirty_files = sbi->ndirty_inode[FILE_INODE];
	si->nquota_files = sbi->nquota_files;
	si->ndirty_all = sbi->ndirty_inode[DIRTY_META];
	si->inmem_pages = get_pages(sbi, F2FS_INMEM_PAGES);
	si->aw_cnt = sbi->atomic_files;
	si->vw_cnt = atomic_read(&sbi->vw_cnt);
	si->max_aw_cnt = atomic_read(&sbi->max_aw_cnt);
	si->max_vw_cnt = atomic_read(&sbi->max_vw_cnt);
	si->nr_dio_read = get_pages(sbi, F2FS_DIO_READ);
	si->nr_dio_write = get_pages(sbi, F2FS_DIO_WRITE);
	si->nr_wb_cp_data = get_pages(sbi, F2FS_WB_CP_DATA);
	si->nr_wb_data = get_pages(sbi, F2FS_WB_DATA);
	si->nr_rd_data = get_pages(sbi, F2FS_RD_DATA);
	si->nr_rd_node = get_pages(sbi, F2FS_RD_NODE);
	si->nr_rd_meta = get_pages(sbi, F2FS_RD_META);
	if (SM_I(sbi)->fcc_info) {
		si->nr_flushed =
			atomic_read(&SM_I(sbi)->fcc_info->issued_flush);
		si->nr_flushing =
			atomic_read(&SM_I(sbi)->fcc_info->queued_flush);
		si->flush_list_empty =
			llist_empty(&SM_I(sbi)->fcc_info->issue_list);
	}
	if (SM_I(sbi)->dcc_info) {
		si->nr_discarded =
			atomic_read(&SM_I(sbi)->dcc_info->issued_discard);
		si->nr_discarding =
			atomic_read(&SM_I(sbi)->dcc_info->queued_discard);
		si->nr_discard_cmd =
			atomic_read(&SM_I(sbi)->dcc_info->discard_cmd_cnt);
		si->undiscard_blks = SM_I(sbi)->dcc_info->undiscard_blks;
	}
	si->nr_issued_ckpt = atomic_read(&sbi->cprc_info.issued_ckpt);
	si->nr_total_ckpt = atomic_read(&sbi->cprc_info.total_ckpt);
	si->nr_queued_ckpt = atomic_read(&sbi->cprc_info.queued_ckpt);
	spin_lock(&sbi->cprc_info.stat_lock);
	si->cur_ckpt_time = sbi->cprc_info.cur_time;
	si->peak_ckpt_time = sbi->cprc_info.peak_time;
	spin_unlock(&sbi->cprc_info.stat_lock);
	si->total_count = (int)sbi->user_block_count / sbi->blocks_per_seg;
	si->rsvd_segs = reserved_segments(sbi);
	si->overp_segs = overprovision_segments(sbi);
	si->valid_count = valid_user_blocks(sbi);
	si->discard_blks = discard_blocks(sbi);
	si->valid_node_count = valid_node_count(sbi);
	si->valid_inode_count = valid_inode_count(sbi);
	si->inline_xattr = atomic_read(&sbi->inline_xattr);
	si->inline_inode = atomic_read(&sbi->inline_inode);
	si->inline_dir = atomic_read(&sbi->inline_dir);
	si->compr_inode = atomic_read(&sbi->compr_inode);
	si->compr_blocks = atomic64_read(&sbi->compr_blocks);
	si->append = sbi->im[APPEND_INO].ino_num;
	si->update = sbi->im[UPDATE_INO].ino_num;
	si->orphans = sbi->im[ORPHAN_INO].ino_num;
	si->utilization = utilization(sbi);

	si->free_segs = free_segments(sbi);
	si->free_secs = free_sections(sbi);
	si->prefree_count = prefree_segments(sbi);
	si->dirty_count = dirty_segments(sbi);
	if (sbi->node_inode)
		si->node_pages = NODE_MAPPING(sbi)->nrpages;
	if (sbi->meta_inode)
		si->meta_pages = META_MAPPING(sbi)->nrpages;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (sbi->compress_inode) {
		si->compress_pages = COMPRESS_MAPPING(sbi)->nrpages;
		si->compress_page_hit = atomic_read(&sbi->compress_page_hit);
	}
#endif
	si->nats = NM_I(sbi)->nat_cnt[TOTAL_NAT];
	si->dirty_nats = NM_I(sbi)->nat_cnt[DIRTY_NAT];
	si->sits = MAIN_SEGS(sbi);
	si->dirty_sits = SIT_I(sbi)->dirty_sentries;
	si->free_nids = NM_I(sbi)->nid_cnt[FREE_NID];
	si->avail_nids = NM_I(sbi)->available_nids;
	si->alloc_nids = NM_I(sbi)->nid_cnt[PREALLOC_NID];
	si->io_skip_bggc = sbi->io_skip_bggc;
	si->other_skip_bggc = sbi->other_skip_bggc;
	si->skipped_atomic_files[BG_GC] = sbi->skipped_atomic_files[BG_GC];
	si->skipped_atomic_files[FG_GC] = sbi->skipped_atomic_files[FG_GC];
	si->util_free = (int)(free_user_blocks(sbi) >> sbi->log_blocks_per_seg)
		* 100 / (int)(sbi->user_block_count >> sbi->log_blocks_per_seg)
		/ 2;
	si->util_valid = (int)(written_block_count(sbi) >>
						sbi->log_blocks_per_seg)
		* 100 / (int)(sbi->user_block_count >> sbi->log_blocks_per_seg)
		/ 2;
	si->util_invalid = 50 - si->util_free - si->util_valid;
	for (i = CURSEG_HOT_DATA; i < NO_CHECK_TYPE; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);

		si->curseg[i] = curseg->segno;
		si->cursec[i] = GET_SEC_FROM_SEG(sbi, curseg->segno);
		si->curzone[i] = GET_ZONE_FROM_SEC(sbi, si->cursec[i]);
	}

	for (i = META_CP; i < META_MAX; i++)
		si->meta_count[i] = atomic_read(&sbi->meta_count[i]);

	for (i = 0; i < NO_CHECK_TYPE; i++) {
		si->dirty_seg[i] = 0;
		si->full_seg[i] = 0;
		si->valid_blks[i] = 0;
	}

	for (i = 0; i < MAIN_SEGS(sbi); i++) {
		int blks = get_seg_entry(sbi, i)->valid_blocks;
		int type = get_seg_entry(sbi, i)->type;

		if (!blks)
			continue;

		if (blks == sbi->blocks_per_seg)
			si->full_seg[type]++;
		else
			si->dirty_seg[type]++;
		si->valid_blks[type] += blks;
	}

	for (i = 0; i < 2; i++) {
		si->segment_count[i] = sbi->segment_count[i];
		si->block_count[i] = sbi->block_count[i];
	}

	si->inplace_count = atomic_read(&sbi->inplace_count);
}

/*
 * This function calculates memory footprint.
 */
static void update_mem_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = F2FS_STAT(sbi);
	int i;

	if (si->base_mem)
		goto get_cache;

	/* build stat */
	si->base_mem = sizeof(struct f2fs_stat_info);

	/* build superblock */
	si->base_mem += sizeof(struct f2fs_sb_info) + sbi->sb->s_blocksize;
	si->base_mem += 2 * sizeof(struct f2fs_inode_info);
	si->base_mem += sizeof(*sbi->ckpt);

	/* build sm */
	si->base_mem += sizeof(struct f2fs_sm_info);

	/* build sit */
	si->base_mem += sizeof(struct sit_info);
	si->base_mem += MAIN_SEGS(sbi) * sizeof(struct seg_entry);
	si->base_mem += f2fs_bitmap_size(MAIN_SEGS(sbi));
	si->base_mem += 2 * SIT_VBLOCK_MAP_SIZE * MAIN_SEGS(sbi);
	si->base_mem += SIT_VBLOCK_MAP_SIZE * MAIN_SEGS(sbi);
	si->base_mem += SIT_VBLOCK_MAP_SIZE;
	if (__is_large_section(sbi))
		si->base_mem += MAIN_SECS(sbi) * sizeof(struct sec_entry);
	si->base_mem += __bitmap_size(sbi, SIT_BITMAP);

	/* build free segmap */
	si->base_mem += sizeof(struct free_segmap_info);
	si->base_mem += f2fs_bitmap_size(MAIN_SEGS(sbi));
	si->base_mem += f2fs_bitmap_size(MAIN_SECS(sbi));

	/* build curseg */
	si->base_mem += sizeof(struct curseg_info) * NR_CURSEG_TYPE;
	si->base_mem += PAGE_SIZE * NR_CURSEG_TYPE;

	/* build dirty segmap */
	si->base_mem += sizeof(struct dirty_seglist_info);
	si->base_mem += NR_DIRTY_TYPE * f2fs_bitmap_size(MAIN_SEGS(sbi));
	si->base_mem += f2fs_bitmap_size(MAIN_SECS(sbi));

	/* build nm */
	si->base_mem += sizeof(struct f2fs_nm_info);
	si->base_mem += __bitmap_size(sbi, NAT_BITMAP);
	si->base_mem += (NM_I(sbi)->nat_bits_blocks << F2FS_BLKSIZE_BITS);
	si->base_mem += NM_I(sbi)->nat_blocks *
				f2fs_bitmap_size(NAT_ENTRY_PER_BLOCK);
	si->base_mem += NM_I(sbi)->nat_blocks / 8;
	si->base_mem += NM_I(sbi)->nat_blocks * sizeof(unsigned short);

get_cache:
	si->cache_mem = 0;

	/* build gc */
	if (sbi->gc_thread)
		si->cache_mem += sizeof(struct f2fs_gc_kthread);

	/* build merge flush thread */
	if (SM_I(sbi)->fcc_info)
		si->cache_mem += sizeof(struct flush_cmd_control);
	if (SM_I(sbi)->dcc_info) {
		si->cache_mem += sizeof(struct discard_cmd_control);
		si->cache_mem += sizeof(struct discard_cmd) *
			atomic_read(&SM_I(sbi)->dcc_info->discard_cmd_cnt);
	}

	/* free nids */
	si->cache_mem += (NM_I(sbi)->nid_cnt[FREE_NID] +
				NM_I(sbi)->nid_cnt[PREALLOC_NID]) *
				sizeof(struct free_nid);
	si->cache_mem += NM_I(sbi)->nat_cnt[TOTAL_NAT] *
				sizeof(struct nat_entry);
	si->cache_mem += NM_I(sbi)->nat_cnt[DIRTY_NAT] *
				sizeof(struct nat_entry_set);
	si->cache_mem += si->inmem_pages * sizeof(struct inmem_pages);
	for (i = 0; i < MAX_INO_ENTRY; i++)
		si->cache_mem += sbi->im[i].ino_num * sizeof(struct ino_entry);

	for (i = 0; i < NR_EXTENT_CACHES; i++) {
		struct extent_tree_info *eti = &sbi->extent_tree[i];

		si->ext_mem[i] = atomic_read(&eti->total_ext_tree) *
						sizeof(struct extent_tree);
		si->ext_mem[i] += atomic_read(&eti->total_ext_node) *
						sizeof(struct extent_node);
		si->cache_mem += si->ext_mem[i];
	}

	si->page_mem = 0;
	if (sbi->node_inode) {
		unsigned npages = NODE_MAPPING(sbi)->nrpages;

		si->page_mem += (unsigned long long)npages << PAGE_SHIFT;
	}
	if (sbi->meta_inode) {
		unsigned npages = META_MAPPING(sbi)->nrpages;

		si->page_mem += (unsigned long long)npages << PAGE_SHIFT;
	}
#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (sbi->compress_inode) {
		unsigned npages = COMPRESS_MAPPING(sbi)->nrpages;
		si->page_mem += (unsigned long long)npages << PAGE_SHIFT;
	}
#endif
}

static int stat_show(struct seq_file *s, void *v)
{
	struct f2fs_stat_info *si;
	int i = 0;
	int j;

	mutex_lock(&f2fs_stat_mutex);
	list_for_each_entry(si, &f2fs_stat_list, stat_list) {
		update_general_status(si->sbi);

		seq_printf(s, "\n=====[ partition info(%pg). #%d, %s, CP: %s]=====\n",
			si->sbi->sb->s_bdev, i++,
			f2fs_readonly(si->sbi->sb) ? "RO": "RW",
			is_set_ckpt_flags(si->sbi, CP_DISABLED_FLAG) ?
			"Disabled": (f2fs_cp_error(si->sbi) ? "Error": "Good"));
		seq_printf(s, "[SB: 1] [CP: 2] [SIT: %d] [NAT: %d] ",
			   si->sit_area_segs, si->nat_area_segs);
		seq_printf(s, "[SSA: %d] [MAIN: %d",
			   si->ssa_area_segs, si->main_area_segs);
		seq_printf(s, "(OverProv:%d Resv:%d)]\n\n",
			   si->overp_segs, si->rsvd_segs);
		seq_printf(s, "Current Time Sec: %llu / Mounted Time Sec: %llu\n\n",
					ktime_get_boottime_seconds(),
					SIT_I(si->sbi)->mounted_time);
		if (test_opt(si->sbi, DISCARD))
			seq_printf(s, "Utilization: %u%% (%u valid blocks, %u discard blocks)\n",
				si->utilization, si->valid_count, si->discard_blks);
		else
			seq_printf(s, "Utilization: %u%% (%u valid blocks)\n",
				si->utilization, si->valid_count);

		seq_printf(s, "  - Node: %u (Inode: %u, ",
			   si->valid_node_count, si->valid_inode_count);
		seq_printf(s, "Other: %u)\n  - Data: %u\n",
			   si->valid_node_count - si->valid_inode_count,
			   si->valid_count - si->valid_node_count);
		seq_printf(s, "  - Inline_xattr Inode: %u\n",
			   si->inline_xattr);
		seq_printf(s, "  - Inline_data Inode: %u\n",
			   si->inline_inode);
		seq_printf(s, "  - Inline_dentry Inode: %u\n",
			   si->inline_dir);
		seq_printf(s, "  - Compressed Inode: %u, Blocks: %llu\n",
			   si->compr_inode, si->compr_blocks);
		seq_printf(s, "  - Orphan/Append/Update Inode: %u, %u, %u\n",
			   si->orphans, si->append, si->update);
		seq_printf(s, "\nMain area: %d segs, %d secs %d zones\n",
			   si->main_area_segs, si->main_area_sections,
			   si->main_area_zones);
		seq_printf(s, "    TYPE         %8s %8s %8s %10s %10s %10s\n",
			   "segno", "secno", "zoneno", "dirty_seg", "full_seg", "valid_blk");
		seq_printf(s, "  - COLD   data: %8d %8d %8d %10u %10u %10u\n",
			   si->curseg[CURSEG_COLD_DATA],
			   si->cursec[CURSEG_COLD_DATA],
			   si->curzone[CURSEG_COLD_DATA],
			   si->dirty_seg[CURSEG_COLD_DATA],
			   si->full_seg[CURSEG_COLD_DATA],
			   si->valid_blks[CURSEG_COLD_DATA]);
		seq_printf(s, "  - WARM   data: %8d %8d %8d %10u %10u %10u\n",
			   si->curseg[CURSEG_WARM_DATA],
			   si->cursec[CURSEG_WARM_DATA],
			   si->curzone[CURSEG_WARM_DATA],
			   si->dirty_seg[CURSEG_WARM_DATA],
			   si->full_seg[CURSEG_WARM_DATA],
			   si->valid_blks[CURSEG_WARM_DATA]);
		seq_printf(s, "  - HOT    data: %8d %8d %8d %10u %10u %10u\n",
			   si->curseg[CURSEG_HOT_DATA],
			   si->cursec[CURSEG_HOT_DATA],
			   si->curzone[CURSEG_HOT_DATA],
			   si->dirty_seg[CURSEG_HOT_DATA],
			   si->full_seg[CURSEG_HOT_DATA],
			   si->valid_blks[CURSEG_HOT_DATA]);
		seq_printf(s, "  - Dir   dnode: %8d %8d %8d %10u %10u %10u\n",
			   si->curseg[CURSEG_HOT_NODE],
			   si->cursec[CURSEG_HOT_NODE],
			   si->curzone[CURSEG_HOT_NODE],
			   si->dirty_seg[CURSEG_HOT_NODE],
			   si->full_seg[CURSEG_HOT_NODE],
			   si->valid_blks[CURSEG_HOT_NODE]);
		seq_printf(s, "  - File  dnode: %8d %8d %8d %10u %10u %10u\n",
			   si->curseg[CURSEG_WARM_NODE],
			   si->cursec[CURSEG_WARM_NODE],
			   si->curzone[CURSEG_WARM_NODE],
			   si->dirty_seg[CURSEG_WARM_NODE],
			   si->full_seg[CURSEG_WARM_NODE],
			   si->valid_blks[CURSEG_WARM_NODE]);
		seq_printf(s, "  - Indir nodes: %8d %8d %8d %10u %10u %10u\n",
			   si->curseg[CURSEG_COLD_NODE],
			   si->cursec[CURSEG_COLD_NODE],
			   si->curzone[CURSEG_COLD_NODE],
			   si->dirty_seg[CURSEG_COLD_NODE],
			   si->full_seg[CURSEG_COLD_NODE],
			   si->valid_blks[CURSEG_COLD_NODE]);
		seq_printf(s, "  - Pinned file: %8d %8d %8d\n",
			   si->curseg[CURSEG_COLD_DATA_PINNED],
			   si->cursec[CURSEG_COLD_DATA_PINNED],
			   si->curzone[CURSEG_COLD_DATA_PINNED]);
		seq_printf(s, "  - ATGC   data: %8d %8d %8d\n",
			   si->curseg[CURSEG_ALL_DATA_ATGC],
			   si->cursec[CURSEG_ALL_DATA_ATGC],
			   si->curzone[CURSEG_ALL_DATA_ATGC]);
		seq_printf(s, "\n  - Valid: %d\n  - Dirty: %d\n",
			   si->main_area_segs - si->dirty_count -
			   si->prefree_count - si->free_segs,
			   si->dirty_count);
		seq_printf(s, "  - Prefree: %d\n  - Free: %d (%d)\n\n",
			   si->prefree_count, si->free_segs, si->free_secs);
		seq_printf(s, "CP calls: %d (BG: %d)\n",
				si->cp_count, si->bg_cp_count);
		seq_printf(s, "  - cp blocks : %u\n", si->meta_count[META_CP]);
		seq_printf(s, "  - sit blocks : %u\n",
				si->meta_count[META_SIT]);
		seq_printf(s, "  - nat blocks : %u\n",
				si->meta_count[META_NAT]);
		seq_printf(s, "  - ssa blocks : %u\n",
				si->meta_count[META_SSA]);
		seq_printf(s, "CP merge (Queued: %4d, Issued: %4d, Total: %4d, "
				"Cur time: %4d(ms), Peak time: %4d(ms))\n",
				si->nr_queued_ckpt, si->nr_issued_ckpt,
				si->nr_total_ckpt, si->cur_ckpt_time,
				si->peak_ckpt_time);
		seq_printf(s, "GC calls: %d (BG: %d)\n",
			   si->call_count, si->bg_gc);
		seq_printf(s, "  - data segments : %d (%d)\n",
				si->data_segs, si->bg_data_segs);
		seq_printf(s, "  - node segments : %d (%d)\n",
				si->node_segs, si->bg_node_segs);
		seq_printf(s, "  - Reclaimed segs : Normal (%d), Idle CB (%d), "
				"Idle Greedy (%d), Idle AT (%d), "
				"Urgent High (%d), Urgent Mid (%d), "
				"Urgent Low (%d)\n",
				si->sbi->gc_reclaimed_segs[GC_NORMAL],
				si->sbi->gc_reclaimed_segs[GC_IDLE_CB],
				si->sbi->gc_reclaimed_segs[GC_IDLE_GREEDY],
				si->sbi->gc_reclaimed_segs[GC_IDLE_AT],
				si->sbi->gc_reclaimed_segs[GC_URGENT_HIGH],
				si->sbi->gc_reclaimed_segs[GC_URGENT_MID],
				si->sbi->gc_reclaimed_segs[GC_URGENT_LOW]);
		seq_printf(s, "Try to move %d blocks (BG: %d)\n", si->tot_blks,
				si->bg_data_blks + si->bg_node_blks);
		seq_printf(s, "  - data blocks : %d (%d)\n", si->data_blks,
				si->bg_data_blks);
		seq_printf(s, "  - node blocks : %d (%d)\n", si->node_blks,
				si->bg_node_blks);
		seq_printf(s, "Skipped : atomic write %llu (%llu)\n",
				si->skipped_atomic_files[BG_GC] +
				si->skipped_atomic_files[FG_GC],
				si->skipped_atomic_files[BG_GC]);
		seq_printf(s, "BG skip : IO: %u, Other: %u\n",
				si->io_skip_bggc, si->other_skip_bggc);
		seq_puts(s, "\nExtent Cache (Read):\n");
		seq_printf(s, "  - Hit Count: L1-1:%llu L1-2:%llu L2:%llu\n",
				si->hit_largest, si->hit_cached[EX_READ],
				si->hit_rbtree[EX_READ]);
		seq_printf(s, "  - Hit Ratio: %llu%% (%llu / %llu)\n",
				!si->total_ext[EX_READ] ? 0 :
				div64_u64(si->hit_total[EX_READ] * 100,
				si->total_ext[EX_READ]),
				si->hit_total[EX_READ], si->total_ext[EX_READ]);
		seq_printf(s, "  - Inner Struct Count: tree: %d(%d), node: %d\n",
				si->ext_tree[EX_READ], si->zombie_tree[EX_READ],
				si->ext_node[EX_READ]);
		seq_puts(s, "\nExtent Cache (Block Age):\n");
		seq_printf(s, "  - Allocated Data Blocks: %llu\n",
				si->allocated_data_blocks);
		seq_printf(s, "  - Hit Count: L1:%llu L2:%llu\n",
				si->hit_cached[EX_BLOCK_AGE],
				si->hit_rbtree[EX_BLOCK_AGE]);
		seq_printf(s, "  - Hit Ratio: %llu%% (%llu / %llu)\n",
				!si->total_ext[EX_BLOCK_AGE] ? 0 :
				div64_u64(si->hit_total[EX_BLOCK_AGE] * 100,
				si->total_ext[EX_BLOCK_AGE]),
				si->hit_total[EX_BLOCK_AGE],
				si->total_ext[EX_BLOCK_AGE]);
		seq_printf(s, "  - Inner Struct Count: tree: %d(%d), node: %d\n",
				si->ext_tree[EX_BLOCK_AGE],
				si->zombie_tree[EX_BLOCK_AGE],
				si->ext_node[EX_BLOCK_AGE]);
		seq_puts(s, "\nBalancing F2FS Async:\n");
		seq_printf(s, "  - DIO (R: %4d, W: %4d)\n",
			   si->nr_dio_read, si->nr_dio_write);
		seq_printf(s, "  - IO_R (Data: %4d, Node: %4d, Meta: %4d\n",
			   si->nr_rd_data, si->nr_rd_node, si->nr_rd_meta);
		seq_printf(s, "  - IO_W (CP: %4d, Data: %4d, Flush: (%4d %4d %4d), "
			"Discard: (%4d %4d)) cmd: %4d undiscard:%4u\n",
			   si->nr_wb_cp_data, si->nr_wb_data,
			   si->nr_flushing, si->nr_flushed,
			   si->flush_list_empty,
			   si->nr_discarding, si->nr_discarded,
			   si->nr_discard_cmd, si->undiscard_blks);
		seq_printf(s, "  - inmem: %4d, atomic IO: %4d (Max. %4d), "
			"volatile IO: %4d (Max. %4d)\n",
			   si->inmem_pages, si->aw_cnt, si->max_aw_cnt,
			   si->vw_cnt, si->max_vw_cnt);
		seq_printf(s, "  - compress: %4d, hit:%8d\n", si->compress_pages, si->compress_page_hit);
		seq_printf(s, "  - nodes: %4d in %4d\n",
			   si->ndirty_node, si->node_pages);
		seq_printf(s, "  - dents: %4d in dirs:%4d (%4d)\n",
			   si->ndirty_dent, si->ndirty_dirs, si->ndirty_all);
		seq_printf(s, "  - datas: %4d in files:%4d\n",
			   si->ndirty_data, si->ndirty_files);
		seq_printf(s, "  - quota datas: %4d in quota files:%4d\n",
			   si->ndirty_qdata, si->nquota_files);
		seq_printf(s, "  - meta: %4d in %4d\n",
			   si->ndirty_meta, si->meta_pages);
		seq_printf(s, "  - imeta: %4d\n",
			   si->ndirty_imeta);
		seq_printf(s, "  - NATs: %9d/%9d\n  - SITs: %9d/%9d\n",
			   si->dirty_nats, si->nats, si->dirty_sits, si->sits);
		seq_printf(s, "  - free_nids: %9d/%9d\n  - alloc_nids: %9d\n",
			   si->free_nids, si->avail_nids, si->alloc_nids);
		seq_puts(s, "\nDistribution of User Blocks:");
		seq_puts(s, " [ valid | invalid | free ]\n");
		seq_puts(s, "  [");

		for (j = 0; j < si->util_valid; j++)
			seq_putc(s, '-');
		seq_putc(s, '|');

		for (j = 0; j < si->util_invalid; j++)
			seq_putc(s, '-');
		seq_putc(s, '|');

		for (j = 0; j < si->util_free; j++)
			seq_putc(s, '-');
		seq_puts(s, "]\n\n");
		seq_printf(s, "IPU: %u blocks\n", si->inplace_count);
		seq_printf(s, "SSR: %u blocks in %u segments\n",
			   si->block_count[SSR], si->segment_count[SSR]);
		seq_printf(s, "LFS: %u blocks in %u segments\n",
			   si->block_count[LFS], si->segment_count[LFS]);

		/* segment usage info */
		f2fs_update_sit_info(si->sbi);
		seq_printf(s, "\nBDF: %u, avg. vblocks: %u\n",
			   si->bimodal, si->avg_vblocks);

		/* memory footprint */
		update_mem_info(si->sbi);
		seq_printf(s, "\nMemory: %llu KB\n",
			(si->base_mem + si->cache_mem + si->page_mem) >> 10);
		seq_printf(s, "  - static: %llu KB\n",
				si->base_mem >> 10);
		seq_printf(s, "  - cached all: %llu KB\n",
				si->cache_mem >> 10);
		seq_printf(s, "  - read extent cache: %llu KB\n",
				si->ext_mem[EX_READ] >> 10);
		seq_printf(s, "  - block age extent cache: %llu KB\n",
				si->ext_mem[EX_BLOCK_AGE] >> 10);
		seq_printf(s, "  - paged : %llu KB\n",
				si->page_mem >> 10);
	}
	mutex_unlock(&f2fs_stat_mutex);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(stat);
#endif

int f2fs_build_stats(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	struct f2fs_stat_info *si;
	int i;

	si = f2fs_kzalloc(sbi, sizeof(struct f2fs_stat_info), GFP_KERNEL);
	if (!si)
		return -ENOMEM;

	si->all_area_segs = le32_to_cpu(raw_super->segment_count);
	si->sit_area_segs = le32_to_cpu(raw_super->segment_count_sit);
	si->nat_area_segs = le32_to_cpu(raw_super->segment_count_nat);
	si->ssa_area_segs = le32_to_cpu(raw_super->segment_count_ssa);
	si->main_area_segs = le32_to_cpu(raw_super->segment_count_main);
	si->main_area_sections = le32_to_cpu(raw_super->section_count);
	si->main_area_zones = si->main_area_sections /
				le32_to_cpu(raw_super->secs_per_zone);
	si->sbi = sbi;
	sbi->stat_info = si;

	/* general extent cache stats */
	for (i = 0; i < NR_EXTENT_CACHES; i++) {
		atomic64_set(&sbi->total_hit_ext[i], 0);
		atomic64_set(&sbi->read_hit_rbtree[i], 0);
		atomic64_set(&sbi->read_hit_cached[i], 0);
	}

	/* read extent_cache only */
	atomic64_set(&sbi->read_hit_largest, 0);

	atomic_set(&sbi->inline_xattr, 0);
	atomic_set(&sbi->inline_inode, 0);
	atomic_set(&sbi->inline_dir, 0);
	atomic_set(&sbi->compr_inode, 0);
	atomic64_set(&sbi->compr_blocks, 0);
	atomic_set(&sbi->inplace_count, 0);
	for (i = META_CP; i < META_MAX; i++)
		atomic_set(&sbi->meta_count[i], 0);

	atomic_set(&sbi->vw_cnt, 0);
	atomic_set(&sbi->max_aw_cnt, 0);
	atomic_set(&sbi->max_vw_cnt, 0);

	mutex_lock(&f2fs_stat_mutex);
	list_add_tail(&si->stat_list, &f2fs_stat_list);
	mutex_unlock(&f2fs_stat_mutex);

	return 0;
}

void f2fs_destroy_stats(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = F2FS_STAT(sbi);

	mutex_lock(&f2fs_stat_mutex);
	list_del(&si->stat_list);
	mutex_unlock(&f2fs_stat_mutex);

	kfree(si);
}

void __init f2fs_create_root_stats(void)
{
#ifdef CONFIG_DEBUG_FS
	f2fs_debugfs_root = debugfs_create_dir("f2fs", NULL);

	debugfs_create_file("status", S_IRUGO, f2fs_debugfs_root, NULL,
			    &stat_fops);
#endif
}

void f2fs_destroy_root_stats(void)
{
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(f2fs_debugfs_root);
	f2fs_debugfs_root = NULL;
#endif
}
