/**
 * f2fs debugging statistics
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2012 Linux Foundation
 * Copyright (c) 2012 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/backing-dev.h>
#include <linux/proc_fs.h>
#include <linux/f2fs_fs.h>
#include <linux/blkdev.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "gc.h"

static LIST_HEAD(f2fs_stat_list);
static struct dentry *debugfs_root;

void update_general_status(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = sbi->stat_info;
	int i;

	/* valid check of the segment numbers */
	si->hit_ext = sbi->read_hit_ext;
	si->total_ext = sbi->total_hit_ext;
	si->ndirty_node = get_pages(sbi, F2FS_DIRTY_NODES);
	si->ndirty_dent = get_pages(sbi, F2FS_DIRTY_DENTS);
	si->ndirty_dirs = sbi->n_dirty_dirs;
	si->ndirty_meta = get_pages(sbi, F2FS_DIRTY_META);
	si->total_count = (int)sbi->user_block_count / sbi->blocks_per_seg;
	si->rsvd_segs = reserved_segments(sbi);
	si->overp_segs = overprovision_segments(sbi);
	si->valid_count = valid_user_blocks(sbi);
	si->valid_node_count = valid_node_count(sbi);
	si->valid_inode_count = valid_inode_count(sbi);
	si->utilization = utilization(sbi);

	si->free_segs = free_segments(sbi);
	si->free_secs = free_sections(sbi);
	si->prefree_count = prefree_segments(sbi);
	si->dirty_count = dirty_segments(sbi);
	si->node_pages = sbi->node_inode->i_mapping->nrpages;
	si->meta_pages = sbi->meta_inode->i_mapping->nrpages;
	si->nats = NM_I(sbi)->nat_cnt;
	si->sits = SIT_I(sbi)->dirty_sentries;
	si->fnids = NM_I(sbi)->fcnt;
	si->bg_gc = sbi->bg_gc;
	si->util_free = (int)(free_user_blocks(sbi) >> sbi->log_blocks_per_seg)
		* 100 / (int)(sbi->user_block_count >> sbi->log_blocks_per_seg)
		/ 2;
	si->util_valid = (int)(written_block_count(sbi) >>
						sbi->log_blocks_per_seg)
		* 100 / (int)(sbi->user_block_count >> sbi->log_blocks_per_seg)
		/ 2;
	si->util_invalid = 50 - si->util_free - si->util_valid;
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_NODE; i++) {
		struct curseg_info *curseg = CURSEG_I(sbi, i);
		si->curseg[i] = curseg->segno;
		si->cursec[i] = curseg->segno / sbi->segs_per_sec;
		si->curzone[i] = si->cursec[i] / sbi->secs_per_zone;
	}

	for (i = 0; i < 2; i++) {
		si->segment_count[i] = sbi->segment_count[i];
		si->block_count[i] = sbi->block_count[i];
	}
}

/**
 * This function calculates BDF of every segments
 */
static void update_sit_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = sbi->stat_info;
	unsigned int blks_per_sec, hblks_per_sec, total_vblocks, bimodal, dist;
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned int segno, vblocks;
	int ndirty = 0;

	bimodal = 0;
	total_vblocks = 0;
	blks_per_sec = sbi->segs_per_sec * (1 << sbi->log_blocks_per_seg);
	hblks_per_sec = blks_per_sec / 2;
	mutex_lock(&sit_i->sentry_lock);
	for (segno = 0; segno < TOTAL_SEGS(sbi); segno += sbi->segs_per_sec) {
		vblocks = get_valid_blocks(sbi, segno, sbi->segs_per_sec);
		dist = abs(vblocks - hblks_per_sec);
		bimodal += dist * dist;

		if (vblocks > 0 && vblocks < blks_per_sec) {
			total_vblocks += vblocks;
			ndirty++;
		}
	}
	mutex_unlock(&sit_i->sentry_lock);
	dist = sbi->total_sections * hblks_per_sec * hblks_per_sec / 100;
	si->bimodal = bimodal / dist;
	if (si->dirty_count)
		si->avg_vblocks = total_vblocks / ndirty;
	else
		si->avg_vblocks = 0;
}

/**
 * This function calculates memory footprint.
 */
static void update_mem_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = sbi->stat_info;
	unsigned npages;

	if (si->base_mem)
		goto get_cache;

	si->base_mem = sizeof(struct f2fs_sb_info) + sbi->sb->s_blocksize;
	si->base_mem += 2 * sizeof(struct f2fs_inode_info);
	si->base_mem += sizeof(*sbi->ckpt);

	/* build sm */
	si->base_mem += sizeof(struct f2fs_sm_info);

	/* build sit */
	si->base_mem += sizeof(struct sit_info);
	si->base_mem += TOTAL_SEGS(sbi) * sizeof(struct seg_entry);
	si->base_mem += f2fs_bitmap_size(TOTAL_SEGS(sbi));
	si->base_mem += 2 * SIT_VBLOCK_MAP_SIZE * TOTAL_SEGS(sbi);
	if (sbi->segs_per_sec > 1)
		si->base_mem += sbi->total_sections *
			sizeof(struct sec_entry);
	si->base_mem += __bitmap_size(sbi, SIT_BITMAP);

	/* build free segmap */
	si->base_mem += sizeof(struct free_segmap_info);
	si->base_mem += f2fs_bitmap_size(TOTAL_SEGS(sbi));
	si->base_mem += f2fs_bitmap_size(sbi->total_sections);

	/* build curseg */
	si->base_mem += sizeof(struct curseg_info) * NR_CURSEG_TYPE;
	si->base_mem += PAGE_CACHE_SIZE * NR_CURSEG_TYPE;

	/* build dirty segmap */
	si->base_mem += sizeof(struct dirty_seglist_info);
	si->base_mem += NR_DIRTY_TYPE * f2fs_bitmap_size(TOTAL_SEGS(sbi));
	si->base_mem += 2 * f2fs_bitmap_size(TOTAL_SEGS(sbi));

	/* buld nm */
	si->base_mem += sizeof(struct f2fs_nm_info);
	si->base_mem += __bitmap_size(sbi, NAT_BITMAP);

	/* build gc */
	si->base_mem += sizeof(struct f2fs_gc_kthread);

get_cache:
	/* free nids */
	si->cache_mem = NM_I(sbi)->fcnt;
	si->cache_mem += NM_I(sbi)->nat_cnt;
	npages = sbi->node_inode->i_mapping->nrpages;
	si->cache_mem += npages << PAGE_CACHE_SHIFT;
	npages = sbi->meta_inode->i_mapping->nrpages;
	si->cache_mem += npages << PAGE_CACHE_SHIFT;
	si->cache_mem += sbi->n_orphans * sizeof(struct orphan_inode_entry);
	si->cache_mem += sbi->n_dirty_dirs * sizeof(struct dir_inode_entry);
}

static int stat_show(struct seq_file *s, void *v)
{
	struct f2fs_stat_info *si, *next;
	int i = 0;
	int j;

	list_for_each_entry_safe(si, next, &f2fs_stat_list, stat_list) {

		mutex_lock(&si->stat_lock);
		if (!si->sbi) {
			mutex_unlock(&si->stat_lock);
			continue;
		}
		update_general_status(si->sbi);

		seq_printf(s, "\n=====[ partition info. #%d ]=====\n", i++);
		seq_printf(s, "[SB: 1] [CP: 2] [NAT: %d] [SIT: %d] ",
			   si->nat_area_segs, si->sit_area_segs);
		seq_printf(s, "[SSA: %d] [MAIN: %d",
			   si->ssa_area_segs, si->main_area_segs);
		seq_printf(s, "(OverProv:%d Resv:%d)]\n\n",
			   si->overp_segs, si->rsvd_segs);
		seq_printf(s, "Utilization: %d%% (%d valid blocks)\n",
			   si->utilization, si->valid_count);
		seq_printf(s, "  - Node: %u (Inode: %u, ",
			   si->valid_node_count, si->valid_inode_count);
		seq_printf(s, "Other: %u)\n  - Data: %u\n",
			   si->valid_node_count - si->valid_inode_count,
			   si->valid_count - si->valid_node_count);
		seq_printf(s, "\nMain area: %d segs, %d secs %d zones\n",
			   si->main_area_segs, si->main_area_sections,
			   si->main_area_zones);
		seq_printf(s, "  - COLD  data: %d, %d, %d\n",
			   si->curseg[CURSEG_COLD_DATA],
			   si->cursec[CURSEG_COLD_DATA],
			   si->curzone[CURSEG_COLD_DATA]);
		seq_printf(s, "  - WARM  data: %d, %d, %d\n",
			   si->curseg[CURSEG_WARM_DATA],
			   si->cursec[CURSEG_WARM_DATA],
			   si->curzone[CURSEG_WARM_DATA]);
		seq_printf(s, "  - HOT   data: %d, %d, %d\n",
			   si->curseg[CURSEG_HOT_DATA],
			   si->cursec[CURSEG_HOT_DATA],
			   si->curzone[CURSEG_HOT_DATA]);
		seq_printf(s, "  - Dir   dnode: %d, %d, %d\n",
			   si->curseg[CURSEG_HOT_NODE],
			   si->cursec[CURSEG_HOT_NODE],
			   si->curzone[CURSEG_HOT_NODE]);
		seq_printf(s, "  - File   dnode: %d, %d, %d\n",
			   si->curseg[CURSEG_WARM_NODE],
			   si->cursec[CURSEG_WARM_NODE],
			   si->curzone[CURSEG_WARM_NODE]);
		seq_printf(s, "  - Indir nodes: %d, %d, %d\n",
			   si->curseg[CURSEG_COLD_NODE],
			   si->cursec[CURSEG_COLD_NODE],
			   si->curzone[CURSEG_COLD_NODE]);
		seq_printf(s, "\n  - Valid: %d\n  - Dirty: %d\n",
			   si->main_area_segs - si->dirty_count -
			   si->prefree_count - si->free_segs,
			   si->dirty_count);
		seq_printf(s, "  - Prefree: %d\n  - Free: %d (%d)\n\n",
			   si->prefree_count, si->free_segs, si->free_secs);
		seq_printf(s, "GC calls: %d (BG: %d)\n",
			   si->call_count, si->bg_gc);
		seq_printf(s, "  - data segments : %d\n", si->data_segs);
		seq_printf(s, "  - node segments : %d\n", si->node_segs);
		seq_printf(s, "Try to move %d blocks\n", si->tot_blks);
		seq_printf(s, "  - data blocks : %d\n", si->data_blks);
		seq_printf(s, "  - node blocks : %d\n", si->node_blks);
		seq_printf(s, "\nExtent Hit Ratio: %d / %d\n",
			   si->hit_ext, si->total_ext);
		seq_printf(s, "\nBalancing F2FS Async:\n");
		seq_printf(s, "  - nodes %4d in %4d\n",
			   si->ndirty_node, si->node_pages);
		seq_printf(s, "  - dents %4d in dirs:%4d\n",
			   si->ndirty_dent, si->ndirty_dirs);
		seq_printf(s, "  - meta %4d in %4d\n",
			   si->ndirty_meta, si->meta_pages);
		seq_printf(s, "  - NATs %5d > %lu\n",
			   si->nats, NM_WOUT_THRESHOLD);
		seq_printf(s, "  - SITs: %5d\n  - free_nids: %5d\n",
			   si->sits, si->fnids);
		seq_printf(s, "\nDistribution of User Blocks:");
		seq_printf(s, " [ valid | invalid | free ]\n");
		seq_printf(s, "  [");

		for (j = 0; j < si->util_valid; j++)
			seq_printf(s, "-");
		seq_printf(s, "|");

		for (j = 0; j < si->util_invalid; j++)
			seq_printf(s, "-");
		seq_printf(s, "|");

		for (j = 0; j < si->util_free; j++)
			seq_printf(s, "-");
		seq_printf(s, "]\n\n");
		seq_printf(s, "SSR: %u blocks in %u segments\n",
			   si->block_count[SSR], si->segment_count[SSR]);
		seq_printf(s, "LFS: %u blocks in %u segments\n",
			   si->block_count[LFS], si->segment_count[LFS]);

		/* segment usage info */
		update_sit_info(si->sbi);
		seq_printf(s, "\nBDF: %u, avg. vblocks: %u\n",
			   si->bimodal, si->avg_vblocks);

		/* memory footprint */
		update_mem_info(si->sbi);
		seq_printf(s, "\nMemory: %u KB = static: %u + cached: %u\n",
				(si->base_mem + si->cache_mem) >> 10,
				si->base_mem >> 10, si->cache_mem >> 10);
		mutex_unlock(&si->stat_lock);
	}
	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, stat_show, inode->i_private);
}

static const struct file_operations stat_fops = {
	.open = stat_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int init_stats(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	struct f2fs_stat_info *si;

	sbi->stat_info = kzalloc(sizeof(struct f2fs_stat_info), GFP_KERNEL);
	if (!sbi->stat_info)
		return -ENOMEM;

	si = sbi->stat_info;
	mutex_init(&si->stat_lock);
	list_add_tail(&si->stat_list, &f2fs_stat_list);

	si->all_area_segs = le32_to_cpu(raw_super->segment_count);
	si->sit_area_segs = le32_to_cpu(raw_super->segment_count_sit);
	si->nat_area_segs = le32_to_cpu(raw_super->segment_count_nat);
	si->ssa_area_segs = le32_to_cpu(raw_super->segment_count_ssa);
	si->main_area_segs = le32_to_cpu(raw_super->segment_count_main);
	si->main_area_sections = le32_to_cpu(raw_super->section_count);
	si->main_area_zones = si->main_area_sections /
				le32_to_cpu(raw_super->secs_per_zone);
	si->sbi = sbi;
	return 0;
}

int f2fs_build_stats(struct f2fs_sb_info *sbi)
{
	int retval;

	retval = init_stats(sbi);
	if (retval)
		return retval;

	if (!debugfs_root)
		debugfs_root = debugfs_create_dir("f2fs", NULL);

	debugfs_create_file("status", S_IRUGO, debugfs_root, NULL, &stat_fops);
	return 0;
}

void f2fs_destroy_stats(struct f2fs_sb_info *sbi)
{
	struct f2fs_stat_info *si = sbi->stat_info;

	list_del(&si->stat_list);
	mutex_lock(&si->stat_lock);
	si->sbi = NULL;
	mutex_unlock(&si->stat_lock);
	kfree(sbi->stat_info);
}

void destroy_root_stats(void)
{
	debugfs_remove_recursive(debugfs_root);
	debugfs_root = NULL;
}
