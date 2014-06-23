/*
 * fs/f2fs/segment.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/prefetch.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>

#include "f2fs.h"
#include "segment.h"
#include "node.h"
#include <trace/events/f2fs.h>

#define __reverse_ffz(x) __reverse_ffs(~(x))

static struct kmem_cache *discard_entry_slab;

/*
 * __reverse_ffs is copied from include/asm-generic/bitops/__ffs.h since
 * MSB and LSB are reversed in a byte by f2fs_set_bit.
 */
static inline unsigned long __reverse_ffs(unsigned long word)
{
	int num = 0;

#if BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf0) == 0)
		num += 4;
	else
		word >>= 4;
	if ((word & 0xc) == 0)
		num += 2;
	else
		word >>= 2;
	if ((word & 0x2) == 0)
		num += 1;
	return num;
}

/*
 * __find_rev_next(_zero)_bit is copied from lib/find_next_bit.c becasue
 * f2fs_set_bit makes MSB and LSB reversed in a byte.
 * Example:
 *                             LSB <--> MSB
 *   f2fs_set_bit(0, bitmap) => 0000 0001
 *   f2fs_set_bit(7, bitmap) => 1000 0000
 */
static unsigned long __find_rev_next_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;
	unsigned long mask, submask;
	unsigned long quot, rest;

	if (offset >= size)
		return size;

	size -= result;
	offset %= BITS_PER_LONG;
	if (!offset)
		goto aligned;

	tmp = *(p++);
	quot = (offset >> 3) << 3;
	rest = offset & 0x7;
	mask = ~0UL << quot;
	submask = (unsigned char)(0xff << rest) >> rest;
	submask <<= quot;
	mask &= submask;
	tmp &= mask;
	if (size < BITS_PER_LONG)
		goto found_first;
	if (tmp)
		goto found_middle;

	size -= BITS_PER_LONG;
	result += BITS_PER_LONG;
aligned:
	while (size & ~(BITS_PER_LONG-1)) {
		tmp = *(p++);
		if (tmp)
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;
found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;   /* Nope. */
found_middle:
	return result + __reverse_ffs(tmp);
}

static unsigned long __find_rev_next_zero_bit(const unsigned long *addr,
			unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;
	unsigned long mask, submask;
	unsigned long quot, rest;

	if (offset >= size)
		return size;

	size -= result;
	offset %= BITS_PER_LONG;
	if (!offset)
		goto aligned;

	tmp = *(p++);
	quot = (offset >> 3) << 3;
	rest = offset & 0x7;
	mask = ~(~0UL << quot);
	submask = (unsigned char)~((unsigned char)(0xff << rest) >> rest);
	submask <<= quot;
	mask += submask;
	tmp |= mask;
	if (size < BITS_PER_LONG)
		goto found_first;
	if (~tmp)
		goto found_middle;

	size -= BITS_PER_LONG;
	result += BITS_PER_LONG;
aligned:
	while (size & ~(BITS_PER_LONG - 1)) {
		tmp = *(p++);
		if (~tmp)
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)        /* Are any bits zero? */
		return result + size;   /* Nope. */
found_middle:
	return result + __reverse_ffz(tmp);
}

/*
 * This function balances dirty node and dentry pages.
 * In addition, it controls garbage collection.
 */
void f2fs_balance_fs(struct f2fs_sb_info *sbi)
{
	/*
	 * We should do GC or end up with checkpoint, if there are so many dirty
	 * dir/node pages without enough free segments.
	 */
	if (has_not_enough_free_secs(sbi, 0)) {
		mutex_lock(&sbi->gc_mutex);
		f2fs_gc(sbi);
	}
}

void f2fs_balance_fs_bg(struct f2fs_sb_info *sbi)
{
	/* check the # of cached NAT entries and prefree segments */
	if (try_to_free_nats(sbi, NAT_ENTRY_PER_BLOCK) ||
				excess_prefree_segs(sbi))
		f2fs_sync_fs(sbi->sb, true);
}

static int issue_flush_thread(void *data)
{
	struct f2fs_sb_info *sbi = data;
	struct flush_cmd_control *fcc = SM_I(sbi)->cmd_control_info;
	wait_queue_head_t *q = &fcc->flush_wait_queue;
repeat:
	if (kthread_should_stop())
		return 0;

	spin_lock(&fcc->issue_lock);
	if (fcc->issue_list) {
		fcc->dispatch_list = fcc->issue_list;
		fcc->issue_list = fcc->issue_tail = NULL;
	}
	spin_unlock(&fcc->issue_lock);

	if (fcc->dispatch_list) {
		struct bio *bio = bio_alloc(GFP_NOIO, 0);
		struct flush_cmd *cmd, *next;
		int ret;

		bio->bi_bdev = sbi->sb->s_bdev;
		ret = submit_bio_wait(WRITE_FLUSH, bio);

		for (cmd = fcc->dispatch_list; cmd; cmd = next) {
			cmd->ret = ret;
			next = cmd->next;
			complete(&cmd->wait);
		}
		bio_put(bio);
		fcc->dispatch_list = NULL;
	}

	wait_event_interruptible(*q,
			kthread_should_stop() || fcc->issue_list);
	goto repeat;
}

int f2fs_issue_flush(struct f2fs_sb_info *sbi)
{
	struct flush_cmd_control *fcc = SM_I(sbi)->cmd_control_info;
	struct flush_cmd cmd;

	if (!test_opt(sbi, FLUSH_MERGE))
		return blkdev_issue_flush(sbi->sb->s_bdev, GFP_KERNEL, NULL);

	init_completion(&cmd.wait);
	cmd.next = NULL;

	spin_lock(&fcc->issue_lock);
	if (fcc->issue_list)
		fcc->issue_tail->next = &cmd;
	else
		fcc->issue_list = &cmd;
	fcc->issue_tail = &cmd;
	spin_unlock(&fcc->issue_lock);

	if (!fcc->dispatch_list)
		wake_up(&fcc->flush_wait_queue);

	wait_for_completion(&cmd.wait);

	return cmd.ret;
}

int create_flush_cmd_control(struct f2fs_sb_info *sbi)
{
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	struct flush_cmd_control *fcc;
	int err = 0;

	fcc = kzalloc(sizeof(struct flush_cmd_control), GFP_KERNEL);
	if (!fcc)
		return -ENOMEM;
	spin_lock_init(&fcc->issue_lock);
	init_waitqueue_head(&fcc->flush_wait_queue);
	sbi->sm_info->cmd_control_info = fcc;
	fcc->f2fs_issue_flush = kthread_run(issue_flush_thread, sbi,
				"f2fs_flush-%u:%u", MAJOR(dev), MINOR(dev));
	if (IS_ERR(fcc->f2fs_issue_flush)) {
		err = PTR_ERR(fcc->f2fs_issue_flush);
		kfree(fcc);
		sbi->sm_info->cmd_control_info = NULL;
		return err;
	}

	return err;
}

void destroy_flush_cmd_control(struct f2fs_sb_info *sbi)
{
	struct flush_cmd_control *fcc =
				sbi->sm_info->cmd_control_info;

	if (fcc && fcc->f2fs_issue_flush)
		kthread_stop(fcc->f2fs_issue_flush);
	kfree(fcc);
	sbi->sm_info->cmd_control_info = NULL;
}

static void __locate_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno,
		enum dirty_type dirty_type)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	/* need not be added */
	if (IS_CURSEG(sbi, segno))
		return;

	if (!test_and_set_bit(segno, dirty_i->dirty_segmap[dirty_type]))
		dirty_i->nr_dirty[dirty_type]++;

	if (dirty_type == DIRTY) {
		struct seg_entry *sentry = get_seg_entry(sbi, segno);
		enum dirty_type t = sentry->type;

		if (!test_and_set_bit(segno, dirty_i->dirty_segmap[t]))
			dirty_i->nr_dirty[t]++;
	}
}

static void __remove_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno,
		enum dirty_type dirty_type)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	if (test_and_clear_bit(segno, dirty_i->dirty_segmap[dirty_type]))
		dirty_i->nr_dirty[dirty_type]--;

	if (dirty_type == DIRTY) {
		struct seg_entry *sentry = get_seg_entry(sbi, segno);
		enum dirty_type t = sentry->type;

		if (test_and_clear_bit(segno, dirty_i->dirty_segmap[t]))
			dirty_i->nr_dirty[t]--;

		if (get_valid_blocks(sbi, segno, sbi->segs_per_sec) == 0)
			clear_bit(GET_SECNO(sbi, segno),
						dirty_i->victim_secmap);
	}
}

/*
 * Should not occur error such as -ENOMEM.
 * Adding dirty entry into seglist is not critical operation.
 * If a given segment is one of current working segments, it won't be added.
 */
static void locate_dirty_segment(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned short valid_blocks;

	if (segno == NULL_SEGNO || IS_CURSEG(sbi, segno))
		return;

	mutex_lock(&dirty_i->seglist_lock);

	valid_blocks = get_valid_blocks(sbi, segno, 0);

	if (valid_blocks == 0) {
		__locate_dirty_segment(sbi, segno, PRE);
		__remove_dirty_segment(sbi, segno, DIRTY);
	} else if (valid_blocks < sbi->blocks_per_seg) {
		__locate_dirty_segment(sbi, segno, DIRTY);
	} else {
		/* Recovery routine with SSR needs this */
		__remove_dirty_segment(sbi, segno, DIRTY);
	}

	mutex_unlock(&dirty_i->seglist_lock);
}

static int f2fs_issue_discard(struct f2fs_sb_info *sbi,
				block_t blkstart, block_t blklen)
{
	sector_t start = SECTOR_FROM_BLOCK(sbi, blkstart);
	sector_t len = SECTOR_FROM_BLOCK(sbi, blklen);
	trace_f2fs_issue_discard(sbi->sb, blkstart, blklen);
	return blkdev_issue_discard(sbi->sb->s_bdev, start, len, GFP_NOFS, 0);
}

void discard_next_dnode(struct f2fs_sb_info *sbi)
{
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_WARM_NODE);
	block_t blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);

	if (f2fs_issue_discard(sbi, blkaddr, 1)) {
		struct page *page = grab_meta_page(sbi, blkaddr);
		/* zero-filled page */
		set_page_dirty(page);
		f2fs_put_page(page, 1);
	}
}

static void add_discard_addrs(struct f2fs_sb_info *sbi,
			unsigned int segno, struct seg_entry *se)
{
	struct list_head *head = &SM_I(sbi)->discard_list;
	struct discard_entry *new;
	int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	int max_blocks = sbi->blocks_per_seg;
	unsigned long *cur_map = (unsigned long *)se->cur_valid_map;
	unsigned long *ckpt_map = (unsigned long *)se->ckpt_valid_map;
	unsigned long dmap[entries];
	unsigned int start = 0, end = -1;
	int i;

	if (!test_opt(sbi, DISCARD))
		return;

	/* zero block will be discarded through the prefree list */
	if (!se->valid_blocks || se->valid_blocks == max_blocks)
		return;

	/* SIT_VBLOCK_MAP_SIZE should be multiple of sizeof(unsigned long) */
	for (i = 0; i < entries; i++)
		dmap[i] = (cur_map[i] ^ ckpt_map[i]) & ckpt_map[i];

	while (SM_I(sbi)->nr_discards <= SM_I(sbi)->max_discards) {
		start = __find_rev_next_bit(dmap, max_blocks, end + 1);
		if (start >= max_blocks)
			break;

		end = __find_rev_next_zero_bit(dmap, max_blocks, start + 1);

		new = f2fs_kmem_cache_alloc(discard_entry_slab, GFP_NOFS);
		INIT_LIST_HEAD(&new->list);
		new->blkaddr = START_BLOCK(sbi, segno) + start;
		new->len = end - start;

		list_add_tail(&new->list, head);
		SM_I(sbi)->nr_discards += end - start;
	}
}

/*
 * Should call clear_prefree_segments after checkpoint is done.
 */
static void set_prefree_as_free_segments(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int segno = -1;
	unsigned int total_segs = TOTAL_SEGS(sbi);

	mutex_lock(&dirty_i->seglist_lock);
	while (1) {
		segno = find_next_bit(dirty_i->dirty_segmap[PRE], total_segs,
				segno + 1);
		if (segno >= total_segs)
			break;
		__set_test_and_free(sbi, segno);
	}
	mutex_unlock(&dirty_i->seglist_lock);
}

void clear_prefree_segments(struct f2fs_sb_info *sbi)
{
	struct list_head *head = &(SM_I(sbi)->discard_list);
	struct discard_entry *entry, *this;
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned long *prefree_map = dirty_i->dirty_segmap[PRE];
	unsigned int total_segs = TOTAL_SEGS(sbi);
	unsigned int start = 0, end = -1;

	mutex_lock(&dirty_i->seglist_lock);

	while (1) {
		int i;
		start = find_next_bit(prefree_map, total_segs, end + 1);
		if (start >= total_segs)
			break;
		end = find_next_zero_bit(prefree_map, total_segs, start + 1);

		for (i = start; i < end; i++)
			clear_bit(i, prefree_map);

		dirty_i->nr_dirty[PRE] -= end - start;

		if (!test_opt(sbi, DISCARD))
			continue;

		f2fs_issue_discard(sbi, START_BLOCK(sbi, start),
				(end - start) << sbi->log_blocks_per_seg);
	}
	mutex_unlock(&dirty_i->seglist_lock);

	/* send small discards */
	list_for_each_entry_safe(entry, this, head, list) {
		f2fs_issue_discard(sbi, entry->blkaddr, entry->len);
		list_del(&entry->list);
		SM_I(sbi)->nr_discards -= entry->len;
		kmem_cache_free(discard_entry_slab, entry);
	}
}

static void __mark_sit_entry_dirty(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct sit_info *sit_i = SIT_I(sbi);
	if (!__test_and_set_bit(segno, sit_i->dirty_sentries_bitmap))
		sit_i->dirty_sentries++;
}

static void __set_sit_entry_type(struct f2fs_sb_info *sbi, int type,
					unsigned int segno, int modified)
{
	struct seg_entry *se = get_seg_entry(sbi, segno);
	se->type = type;
	if (modified)
		__mark_sit_entry_dirty(sbi, segno);
}

static void update_sit_entry(struct f2fs_sb_info *sbi, block_t blkaddr, int del)
{
	struct seg_entry *se;
	unsigned int segno, offset;
	long int new_vblocks;

	segno = GET_SEGNO(sbi, blkaddr);

	se = get_seg_entry(sbi, segno);
	new_vblocks = se->valid_blocks + del;
	offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);

	f2fs_bug_on((new_vblocks >> (sizeof(unsigned short) << 3) ||
				(new_vblocks > sbi->blocks_per_seg)));

	se->valid_blocks = new_vblocks;
	se->mtime = get_mtime(sbi);
	SIT_I(sbi)->max_mtime = se->mtime;

	/* Update valid block bitmap */
	if (del > 0) {
		if (f2fs_set_bit(offset, se->cur_valid_map))
			BUG();
	} else {
		if (!f2fs_clear_bit(offset, se->cur_valid_map))
			BUG();
	}
	if (!f2fs_test_bit(offset, se->ckpt_valid_map))
		se->ckpt_valid_blocks += del;

	__mark_sit_entry_dirty(sbi, segno);

	/* update total number of valid blocks to be written in ckpt area */
	SIT_I(sbi)->written_valid_blocks += del;

	if (sbi->segs_per_sec > 1)
		get_sec_entry(sbi, segno)->valid_blocks += del;
}

void refresh_sit_entry(struct f2fs_sb_info *sbi, block_t old, block_t new)
{
	update_sit_entry(sbi, new, 1);
	if (GET_SEGNO(sbi, old) != NULL_SEGNO)
		update_sit_entry(sbi, old, -1);

	locate_dirty_segment(sbi, GET_SEGNO(sbi, old));
	locate_dirty_segment(sbi, GET_SEGNO(sbi, new));
}

void invalidate_blocks(struct f2fs_sb_info *sbi, block_t addr)
{
	unsigned int segno = GET_SEGNO(sbi, addr);
	struct sit_info *sit_i = SIT_I(sbi);

	f2fs_bug_on(addr == NULL_ADDR);
	if (addr == NEW_ADDR)
		return;

	/* add it into sit main buffer */
	mutex_lock(&sit_i->sentry_lock);

	update_sit_entry(sbi, addr, -1);

	/* add it into dirty seglist */
	locate_dirty_segment(sbi, segno);

	mutex_unlock(&sit_i->sentry_lock);
}

/*
 * This function should be resided under the curseg_mutex lock
 */
static void __add_sum_entry(struct f2fs_sb_info *sbi, int type,
					struct f2fs_summary *sum)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	void *addr = curseg->sum_blk;
	addr += curseg->next_blkoff * sizeof(struct f2fs_summary);
	memcpy(addr, sum, sizeof(struct f2fs_summary));
}

/*
 * Calculate the number of current summary pages for writing
 */
int npages_for_summary_flush(struct f2fs_sb_info *sbi)
{
	int valid_sum_count = 0;
	int i, sum_in_page;

	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		if (sbi->ckpt->alloc_type[i] == SSR)
			valid_sum_count += sbi->blocks_per_seg;
		else
			valid_sum_count += curseg_blkoff(sbi, i);
	}

	sum_in_page = (PAGE_CACHE_SIZE - 2 * SUM_JOURNAL_SIZE -
			SUM_FOOTER_SIZE) / SUMMARY_SIZE;
	if (valid_sum_count <= sum_in_page)
		return 1;
	else if ((valid_sum_count - sum_in_page) <=
		(PAGE_CACHE_SIZE - SUM_FOOTER_SIZE) / SUMMARY_SIZE)
		return 2;
	return 3;
}

/*
 * Caller should put this summary page
 */
struct page *get_sum_page(struct f2fs_sb_info *sbi, unsigned int segno)
{
	return get_meta_page(sbi, GET_SUM_BLOCK(sbi, segno));
}

static void write_sum_page(struct f2fs_sb_info *sbi,
			struct f2fs_summary_block *sum_blk, block_t blk_addr)
{
	struct page *page = grab_meta_page(sbi, blk_addr);
	void *kaddr = page_address(page);
	memcpy(kaddr, sum_blk, PAGE_CACHE_SIZE);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
}

static int is_next_segment_free(struct f2fs_sb_info *sbi, int type)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	unsigned int segno = curseg->segno + 1;
	struct free_segmap_info *free_i = FREE_I(sbi);

	if (segno < TOTAL_SEGS(sbi) && segno % sbi->segs_per_sec)
		return !test_bit(segno, free_i->free_segmap);
	return 0;
}

/*
 * Find a new segment from the free segments bitmap to right order
 * This function should be returned with success, otherwise BUG
 */
static void get_new_segment(struct f2fs_sb_info *sbi,
			unsigned int *newseg, bool new_sec, int dir)
{
	struct free_segmap_info *free_i = FREE_I(sbi);
	unsigned int segno, secno, zoneno;
	unsigned int total_zones = TOTAL_SECS(sbi) / sbi->secs_per_zone;
	unsigned int hint = *newseg / sbi->segs_per_sec;
	unsigned int old_zoneno = GET_ZONENO_FROM_SEGNO(sbi, *newseg);
	unsigned int left_start = hint;
	bool init = true;
	int go_left = 0;
	int i;

	write_lock(&free_i->segmap_lock);

	if (!new_sec && ((*newseg + 1) % sbi->segs_per_sec)) {
		segno = find_next_zero_bit(free_i->free_segmap,
					TOTAL_SEGS(sbi), *newseg + 1);
		if (segno - *newseg < sbi->segs_per_sec -
					(*newseg % sbi->segs_per_sec))
			goto got_it;
	}
find_other_zone:
	secno = find_next_zero_bit(free_i->free_secmap, TOTAL_SECS(sbi), hint);
	if (secno >= TOTAL_SECS(sbi)) {
		if (dir == ALLOC_RIGHT) {
			secno = find_next_zero_bit(free_i->free_secmap,
							TOTAL_SECS(sbi), 0);
			f2fs_bug_on(secno >= TOTAL_SECS(sbi));
		} else {
			go_left = 1;
			left_start = hint - 1;
		}
	}
	if (go_left == 0)
		goto skip_left;

	while (test_bit(left_start, free_i->free_secmap)) {
		if (left_start > 0) {
			left_start--;
			continue;
		}
		left_start = find_next_zero_bit(free_i->free_secmap,
							TOTAL_SECS(sbi), 0);
		f2fs_bug_on(left_start >= TOTAL_SECS(sbi));
		break;
	}
	secno = left_start;
skip_left:
	hint = secno;
	segno = secno * sbi->segs_per_sec;
	zoneno = secno / sbi->secs_per_zone;

	/* give up on finding another zone */
	if (!init)
		goto got_it;
	if (sbi->secs_per_zone == 1)
		goto got_it;
	if (zoneno == old_zoneno)
		goto got_it;
	if (dir == ALLOC_LEFT) {
		if (!go_left && zoneno + 1 >= total_zones)
			goto got_it;
		if (go_left && zoneno == 0)
			goto got_it;
	}
	for (i = 0; i < NR_CURSEG_TYPE; i++)
		if (CURSEG_I(sbi, i)->zone == zoneno)
			break;

	if (i < NR_CURSEG_TYPE) {
		/* zone is in user, try another */
		if (go_left)
			hint = zoneno * sbi->secs_per_zone - 1;
		else if (zoneno + 1 >= total_zones)
			hint = 0;
		else
			hint = (zoneno + 1) * sbi->secs_per_zone;
		init = false;
		goto find_other_zone;
	}
got_it:
	/* set it as dirty segment in free segmap */
	f2fs_bug_on(test_bit(segno, free_i->free_segmap));
	__set_inuse(sbi, segno);
	*newseg = segno;
	write_unlock(&free_i->segmap_lock);
}

static void reset_curseg(struct f2fs_sb_info *sbi, int type, int modified)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	struct summary_footer *sum_footer;

	curseg->segno = curseg->next_segno;
	curseg->zone = GET_ZONENO_FROM_SEGNO(sbi, curseg->segno);
	curseg->next_blkoff = 0;
	curseg->next_segno = NULL_SEGNO;

	sum_footer = &(curseg->sum_blk->footer);
	memset(sum_footer, 0, sizeof(struct summary_footer));
	if (IS_DATASEG(type))
		SET_SUM_TYPE(sum_footer, SUM_TYPE_DATA);
	if (IS_NODESEG(type))
		SET_SUM_TYPE(sum_footer, SUM_TYPE_NODE);
	__set_sit_entry_type(sbi, type, curseg->segno, modified);
}

/*
 * Allocate a current working segment.
 * This function always allocates a free segment in LFS manner.
 */
static void new_curseg(struct f2fs_sb_info *sbi, int type, bool new_sec)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	unsigned int segno = curseg->segno;
	int dir = ALLOC_LEFT;

	write_sum_page(sbi, curseg->sum_blk,
				GET_SUM_BLOCK(sbi, segno));
	if (type == CURSEG_WARM_DATA || type == CURSEG_COLD_DATA)
		dir = ALLOC_RIGHT;

	if (test_opt(sbi, NOHEAP))
		dir = ALLOC_RIGHT;

	get_new_segment(sbi, &segno, new_sec, dir);
	curseg->next_segno = segno;
	reset_curseg(sbi, type, 1);
	curseg->alloc_type = LFS;
}

static void __next_free_blkoff(struct f2fs_sb_info *sbi,
			struct curseg_info *seg, block_t start)
{
	struct seg_entry *se = get_seg_entry(sbi, seg->segno);
	int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	unsigned long target_map[entries];
	unsigned long *ckpt_map = (unsigned long *)se->ckpt_valid_map;
	unsigned long *cur_map = (unsigned long *)se->cur_valid_map;
	int i, pos;

	for (i = 0; i < entries; i++)
		target_map[i] = ckpt_map[i] | cur_map[i];

	pos = __find_rev_next_zero_bit(target_map, sbi->blocks_per_seg, start);

	seg->next_blkoff = pos;
}

/*
 * If a segment is written by LFS manner, next block offset is just obtained
 * by increasing the current block offset. However, if a segment is written by
 * SSR manner, next block offset obtained by calling __next_free_blkoff
 */
static void __refresh_next_blkoff(struct f2fs_sb_info *sbi,
				struct curseg_info *seg)
{
	if (seg->alloc_type == SSR)
		__next_free_blkoff(sbi, seg, seg->next_blkoff + 1);
	else
		seg->next_blkoff++;
}

/*
 * This function always allocates a used segment (from dirty seglist) by SSR
 * manner, so it should recover the existing segment information of valid blocks
 */
static void change_curseg(struct f2fs_sb_info *sbi, int type, bool reuse)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	unsigned int new_segno = curseg->next_segno;
	struct f2fs_summary_block *sum_node;
	struct page *sum_page;

	write_sum_page(sbi, curseg->sum_blk,
				GET_SUM_BLOCK(sbi, curseg->segno));
	__set_test_and_inuse(sbi, new_segno);

	mutex_lock(&dirty_i->seglist_lock);
	__remove_dirty_segment(sbi, new_segno, PRE);
	__remove_dirty_segment(sbi, new_segno, DIRTY);
	mutex_unlock(&dirty_i->seglist_lock);

	reset_curseg(sbi, type, 1);
	curseg->alloc_type = SSR;
	__next_free_blkoff(sbi, curseg, 0);

	if (reuse) {
		sum_page = get_sum_page(sbi, new_segno);
		sum_node = (struct f2fs_summary_block *)page_address(sum_page);
		memcpy(curseg->sum_blk, sum_node, SUM_ENTRY_SIZE);
		f2fs_put_page(sum_page, 1);
	}
}

static int get_ssr_segment(struct f2fs_sb_info *sbi, int type)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	const struct victim_selection *v_ops = DIRTY_I(sbi)->v_ops;

	if (IS_NODESEG(type) || !has_not_enough_free_secs(sbi, 0))
		return v_ops->get_victim(sbi,
				&(curseg)->next_segno, BG_GC, type, SSR);

	/* For data segments, let's do SSR more intensively */
	for (; type >= CURSEG_HOT_DATA; type--)
		if (v_ops->get_victim(sbi, &(curseg)->next_segno,
						BG_GC, type, SSR))
			return 1;
	return 0;
}

/*
 * flush out current segment and replace it with new segment
 * This function should be returned with success, otherwise BUG
 */
static void allocate_segment_by_default(struct f2fs_sb_info *sbi,
						int type, bool force)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);

	if (force)
		new_curseg(sbi, type, true);
	else if (type == CURSEG_WARM_NODE)
		new_curseg(sbi, type, false);
	else if (curseg->alloc_type == LFS && is_next_segment_free(sbi, type))
		new_curseg(sbi, type, false);
	else if (need_SSR(sbi) && get_ssr_segment(sbi, type))
		change_curseg(sbi, type, true);
	else
		new_curseg(sbi, type, false);

	stat_inc_seg_type(sbi, curseg);
}

void allocate_new_segments(struct f2fs_sb_info *sbi)
{
	struct curseg_info *curseg;
	unsigned int old_curseg;
	int i;

	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		curseg = CURSEG_I(sbi, i);
		old_curseg = curseg->segno;
		SIT_I(sbi)->s_ops->allocate_segment(sbi, i, true);
		locate_dirty_segment(sbi, old_curseg);
	}
}

static const struct segment_allocation default_salloc_ops = {
	.allocate_segment = allocate_segment_by_default,
};

static bool __has_curseg_space(struct f2fs_sb_info *sbi, int type)
{
	struct curseg_info *curseg = CURSEG_I(sbi, type);
	if (curseg->next_blkoff < sbi->blocks_per_seg)
		return true;
	return false;
}

static int __get_segment_type_2(struct page *page, enum page_type p_type)
{
	if (p_type == DATA)
		return CURSEG_HOT_DATA;
	else
		return CURSEG_HOT_NODE;
}

static int __get_segment_type_4(struct page *page, enum page_type p_type)
{
	if (p_type == DATA) {
		struct inode *inode = page->mapping->host;

		if (S_ISDIR(inode->i_mode))
			return CURSEG_HOT_DATA;
		else
			return CURSEG_COLD_DATA;
	} else {
		if (IS_DNODE(page) && !is_cold_node(page))
			return CURSEG_HOT_NODE;
		else
			return CURSEG_COLD_NODE;
	}
}

static int __get_segment_type_6(struct page *page, enum page_type p_type)
{
	if (p_type == DATA) {
		struct inode *inode = page->mapping->host;

		if (S_ISDIR(inode->i_mode))
			return CURSEG_HOT_DATA;
		else if (is_cold_data(page) || file_is_cold(inode))
			return CURSEG_COLD_DATA;
		else
			return CURSEG_WARM_DATA;
	} else {
		if (IS_DNODE(page))
			return is_cold_node(page) ? CURSEG_WARM_NODE :
						CURSEG_HOT_NODE;
		else
			return CURSEG_COLD_NODE;
	}
}

static int __get_segment_type(struct page *page, enum page_type p_type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(page->mapping->host->i_sb);
	switch (sbi->active_logs) {
	case 2:
		return __get_segment_type_2(page, p_type);
	case 4:
		return __get_segment_type_4(page, p_type);
	}
	/* NR_CURSEG_TYPE(6) logs by default */
	f2fs_bug_on(sbi->active_logs != NR_CURSEG_TYPE);
	return __get_segment_type_6(page, p_type);
}

void allocate_data_block(struct f2fs_sb_info *sbi, struct page *page,
		block_t old_blkaddr, block_t *new_blkaddr,
		struct f2fs_summary *sum, int type)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct curseg_info *curseg;
	unsigned int old_cursegno;

	curseg = CURSEG_I(sbi, type);

	mutex_lock(&curseg->curseg_mutex);

	*new_blkaddr = NEXT_FREE_BLKADDR(sbi, curseg);
	old_cursegno = curseg->segno;

	/*
	 * __add_sum_entry should be resided under the curseg_mutex
	 * because, this function updates a summary entry in the
	 * current summary block.
	 */
	__add_sum_entry(sbi, type, sum);

	mutex_lock(&sit_i->sentry_lock);
	__refresh_next_blkoff(sbi, curseg);

	stat_inc_block_count(sbi, curseg);

	if (!__has_curseg_space(sbi, type))
		sit_i->s_ops->allocate_segment(sbi, type, false);
	/*
	 * SIT information should be updated before segment allocation,
	 * since SSR needs latest valid block information.
	 */
	refresh_sit_entry(sbi, old_blkaddr, *new_blkaddr);
	locate_dirty_segment(sbi, old_cursegno);

	mutex_unlock(&sit_i->sentry_lock);

	if (page && IS_NODESEG(type))
		fill_node_footer_blkaddr(page, NEXT_FREE_BLKADDR(sbi, curseg));

	mutex_unlock(&curseg->curseg_mutex);
}

static void do_write_page(struct f2fs_sb_info *sbi, struct page *page,
			block_t old_blkaddr, block_t *new_blkaddr,
			struct f2fs_summary *sum, struct f2fs_io_info *fio)
{
	int type = __get_segment_type(page, fio->type);

	allocate_data_block(sbi, page, old_blkaddr, new_blkaddr, sum, type);

	/* writeout dirty page into bdev */
	f2fs_submit_page_mbio(sbi, page, *new_blkaddr, fio);
}

void write_meta_page(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_io_info fio = {
		.type = META,
		.rw = WRITE_SYNC | REQ_META | REQ_PRIO
	};

	set_page_writeback(page);
	f2fs_submit_page_mbio(sbi, page, page->index, &fio);
}

void write_node_page(struct f2fs_sb_info *sbi, struct page *page,
		struct f2fs_io_info *fio,
		unsigned int nid, block_t old_blkaddr, block_t *new_blkaddr)
{
	struct f2fs_summary sum;
	set_summary(&sum, nid, 0, 0);
	do_write_page(sbi, page, old_blkaddr, new_blkaddr, &sum, fio);
}

void write_data_page(struct page *page, struct dnode_of_data *dn,
		block_t *new_blkaddr, struct f2fs_io_info *fio)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct f2fs_summary sum;
	struct node_info ni;

	f2fs_bug_on(dn->data_blkaddr == NULL_ADDR);
	get_node_info(sbi, dn->nid, &ni);
	set_summary(&sum, dn->nid, dn->ofs_in_node, ni.version);

	do_write_page(sbi, page, dn->data_blkaddr, new_blkaddr, &sum, fio);
}

void rewrite_data_page(struct page *page, block_t old_blkaddr,
					struct f2fs_io_info *fio)
{
	struct inode *inode = page->mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	f2fs_submit_page_mbio(sbi, page, old_blkaddr, fio);
}

void recover_data_page(struct f2fs_sb_info *sbi,
			struct page *page, struct f2fs_summary *sum,
			block_t old_blkaddr, block_t new_blkaddr)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct curseg_info *curseg;
	unsigned int segno, old_cursegno;
	struct seg_entry *se;
	int type;

	segno = GET_SEGNO(sbi, new_blkaddr);
	se = get_seg_entry(sbi, segno);
	type = se->type;

	if (se->valid_blocks == 0 && !IS_CURSEG(sbi, segno)) {
		if (old_blkaddr == NULL_ADDR)
			type = CURSEG_COLD_DATA;
		else
			type = CURSEG_WARM_DATA;
	}
	curseg = CURSEG_I(sbi, type);

	mutex_lock(&curseg->curseg_mutex);
	mutex_lock(&sit_i->sentry_lock);

	old_cursegno = curseg->segno;

	/* change the current segment */
	if (segno != curseg->segno) {
		curseg->next_segno = segno;
		change_curseg(sbi, type, true);
	}

	curseg->next_blkoff = GET_BLKOFF_FROM_SEG0(sbi, new_blkaddr);
	__add_sum_entry(sbi, type, sum);

	refresh_sit_entry(sbi, old_blkaddr, new_blkaddr);
	locate_dirty_segment(sbi, old_cursegno);

	mutex_unlock(&sit_i->sentry_lock);
	mutex_unlock(&curseg->curseg_mutex);
}

void rewrite_node_page(struct f2fs_sb_info *sbi,
			struct page *page, struct f2fs_summary *sum,
			block_t old_blkaddr, block_t new_blkaddr)
{
	struct sit_info *sit_i = SIT_I(sbi);
	int type = CURSEG_WARM_NODE;
	struct curseg_info *curseg;
	unsigned int segno, old_cursegno;
	block_t next_blkaddr = next_blkaddr_of_node(page);
	unsigned int next_segno = GET_SEGNO(sbi, next_blkaddr);
	struct f2fs_io_info fio = {
		.type = NODE,
		.rw = WRITE_SYNC,
	};

	curseg = CURSEG_I(sbi, type);

	mutex_lock(&curseg->curseg_mutex);
	mutex_lock(&sit_i->sentry_lock);

	segno = GET_SEGNO(sbi, new_blkaddr);
	old_cursegno = curseg->segno;

	/* change the current segment */
	if (segno != curseg->segno) {
		curseg->next_segno = segno;
		change_curseg(sbi, type, true);
	}
	curseg->next_blkoff = GET_BLKOFF_FROM_SEG0(sbi, new_blkaddr);
	__add_sum_entry(sbi, type, sum);

	/* change the current log to the next block addr in advance */
	if (next_segno != segno) {
		curseg->next_segno = next_segno;
		change_curseg(sbi, type, true);
	}
	curseg->next_blkoff = GET_BLKOFF_FROM_SEG0(sbi, next_blkaddr);

	/* rewrite node page */
	set_page_writeback(page);
	f2fs_submit_page_mbio(sbi, page, new_blkaddr, &fio);
	f2fs_submit_merged_bio(sbi, NODE, WRITE);
	refresh_sit_entry(sbi, old_blkaddr, new_blkaddr);
	locate_dirty_segment(sbi, old_cursegno);

	mutex_unlock(&sit_i->sentry_lock);
	mutex_unlock(&curseg->curseg_mutex);
}

static inline bool is_merged_page(struct f2fs_sb_info *sbi,
					struct page *page, enum page_type type)
{
	enum page_type btype = PAGE_TYPE_OF_BIO(type);
	struct f2fs_bio_info *io = &sbi->write_io[btype];
	struct bio_vec *bvec;
	int i;

	down_read(&io->io_rwsem);
	if (!io->bio)
		goto out;

	bio_for_each_segment_all(bvec, io->bio, i) {
		if (page == bvec->bv_page) {
			up_read(&io->io_rwsem);
			return true;
		}
	}

out:
	up_read(&io->io_rwsem);
	return false;
}

void f2fs_wait_on_page_writeback(struct page *page,
				enum page_type type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(page->mapping->host->i_sb);
	if (PageWriteback(page)) {
		if (is_merged_page(sbi, page, type))
			f2fs_submit_merged_bio(sbi, type, WRITE);
		wait_on_page_writeback(page);
	}
}

static int read_compacted_summaries(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct curseg_info *seg_i;
	unsigned char *kaddr;
	struct page *page;
	block_t start;
	int i, j, offset;

	start = start_sum_block(sbi);

	page = get_meta_page(sbi, start++);
	kaddr = (unsigned char *)page_address(page);

	/* Step 1: restore nat cache */
	seg_i = CURSEG_I(sbi, CURSEG_HOT_DATA);
	memcpy(&seg_i->sum_blk->n_nats, kaddr, SUM_JOURNAL_SIZE);

	/* Step 2: restore sit cache */
	seg_i = CURSEG_I(sbi, CURSEG_COLD_DATA);
	memcpy(&seg_i->sum_blk->n_sits, kaddr + SUM_JOURNAL_SIZE,
						SUM_JOURNAL_SIZE);
	offset = 2 * SUM_JOURNAL_SIZE;

	/* Step 3: restore summary entries */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		unsigned short blk_off;
		unsigned int segno;

		seg_i = CURSEG_I(sbi, i);
		segno = le32_to_cpu(ckpt->cur_data_segno[i]);
		blk_off = le16_to_cpu(ckpt->cur_data_blkoff[i]);
		seg_i->next_segno = segno;
		reset_curseg(sbi, i, 0);
		seg_i->alloc_type = ckpt->alloc_type[i];
		seg_i->next_blkoff = blk_off;

		if (seg_i->alloc_type == SSR)
			blk_off = sbi->blocks_per_seg;

		for (j = 0; j < blk_off; j++) {
			struct f2fs_summary *s;
			s = (struct f2fs_summary *)(kaddr + offset);
			seg_i->sum_blk->entries[j] = *s;
			offset += SUMMARY_SIZE;
			if (offset + SUMMARY_SIZE <= PAGE_CACHE_SIZE -
						SUM_FOOTER_SIZE)
				continue;

			f2fs_put_page(page, 1);
			page = NULL;

			page = get_meta_page(sbi, start++);
			kaddr = (unsigned char *)page_address(page);
			offset = 0;
		}
	}
	f2fs_put_page(page, 1);
	return 0;
}

static int read_normal_summaries(struct f2fs_sb_info *sbi, int type)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct f2fs_summary_block *sum;
	struct curseg_info *curseg;
	struct page *new;
	unsigned short blk_off;
	unsigned int segno = 0;
	block_t blk_addr = 0;

	/* get segment number and block addr */
	if (IS_DATASEG(type)) {
		segno = le32_to_cpu(ckpt->cur_data_segno[type]);
		blk_off = le16_to_cpu(ckpt->cur_data_blkoff[type -
							CURSEG_HOT_DATA]);
		if (is_set_ckpt_flags(ckpt, CP_UMOUNT_FLAG))
			blk_addr = sum_blk_addr(sbi, NR_CURSEG_TYPE, type);
		else
			blk_addr = sum_blk_addr(sbi, NR_CURSEG_DATA_TYPE, type);
	} else {
		segno = le32_to_cpu(ckpt->cur_node_segno[type -
							CURSEG_HOT_NODE]);
		blk_off = le16_to_cpu(ckpt->cur_node_blkoff[type -
							CURSEG_HOT_NODE]);
		if (is_set_ckpt_flags(ckpt, CP_UMOUNT_FLAG))
			blk_addr = sum_blk_addr(sbi, NR_CURSEG_NODE_TYPE,
							type - CURSEG_HOT_NODE);
		else
			blk_addr = GET_SUM_BLOCK(sbi, segno);
	}

	new = get_meta_page(sbi, blk_addr);
	sum = (struct f2fs_summary_block *)page_address(new);

	if (IS_NODESEG(type)) {
		if (is_set_ckpt_flags(ckpt, CP_UMOUNT_FLAG)) {
			struct f2fs_summary *ns = &sum->entries[0];
			int i;
			for (i = 0; i < sbi->blocks_per_seg; i++, ns++) {
				ns->version = 0;
				ns->ofs_in_node = 0;
			}
		} else {
			int err;

			err = restore_node_summary(sbi, segno, sum);
			if (err) {
				f2fs_put_page(new, 1);
				return err;
			}
		}
	}

	/* set uncompleted segment to curseg */
	curseg = CURSEG_I(sbi, type);
	mutex_lock(&curseg->curseg_mutex);
	memcpy(curseg->sum_blk, sum, PAGE_CACHE_SIZE);
	curseg->next_segno = segno;
	reset_curseg(sbi, type, 0);
	curseg->alloc_type = ckpt->alloc_type[type];
	curseg->next_blkoff = blk_off;
	mutex_unlock(&curseg->curseg_mutex);
	f2fs_put_page(new, 1);
	return 0;
}

static int restore_curseg_summaries(struct f2fs_sb_info *sbi)
{
	int type = CURSEG_HOT_DATA;
	int err;

	if (is_set_ckpt_flags(F2FS_CKPT(sbi), CP_COMPACT_SUM_FLAG)) {
		/* restore for compacted data summary */
		if (read_compacted_summaries(sbi))
			return -EINVAL;
		type = CURSEG_HOT_NODE;
	}

	for (; type <= CURSEG_COLD_NODE; type++) {
		err = read_normal_summaries(sbi, type);
		if (err)
			return err;
	}

	return 0;
}

static void write_compacted_summaries(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	struct page *page;
	unsigned char *kaddr;
	struct f2fs_summary *summary;
	struct curseg_info *seg_i;
	int written_size = 0;
	int i, j;

	page = grab_meta_page(sbi, blkaddr++);
	kaddr = (unsigned char *)page_address(page);

	/* Step 1: write nat cache */
	seg_i = CURSEG_I(sbi, CURSEG_HOT_DATA);
	memcpy(kaddr, &seg_i->sum_blk->n_nats, SUM_JOURNAL_SIZE);
	written_size += SUM_JOURNAL_SIZE;

	/* Step 2: write sit cache */
	seg_i = CURSEG_I(sbi, CURSEG_COLD_DATA);
	memcpy(kaddr + written_size, &seg_i->sum_blk->n_sits,
						SUM_JOURNAL_SIZE);
	written_size += SUM_JOURNAL_SIZE;

	/* Step 3: write summary entries */
	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++) {
		unsigned short blkoff;
		seg_i = CURSEG_I(sbi, i);
		if (sbi->ckpt->alloc_type[i] == SSR)
			blkoff = sbi->blocks_per_seg;
		else
			blkoff = curseg_blkoff(sbi, i);

		for (j = 0; j < blkoff; j++) {
			if (!page) {
				page = grab_meta_page(sbi, blkaddr++);
				kaddr = (unsigned char *)page_address(page);
				written_size = 0;
			}
			summary = (struct f2fs_summary *)(kaddr + written_size);
			*summary = seg_i->sum_blk->entries[j];
			written_size += SUMMARY_SIZE;

			if (written_size + SUMMARY_SIZE <= PAGE_CACHE_SIZE -
							SUM_FOOTER_SIZE)
				continue;

			set_page_dirty(page);
			f2fs_put_page(page, 1);
			page = NULL;
		}
	}
	if (page) {
		set_page_dirty(page);
		f2fs_put_page(page, 1);
	}
}

static void write_normal_summaries(struct f2fs_sb_info *sbi,
					block_t blkaddr, int type)
{
	int i, end;
	if (IS_DATASEG(type))
		end = type + NR_CURSEG_DATA_TYPE;
	else
		end = type + NR_CURSEG_NODE_TYPE;

	for (i = type; i < end; i++) {
		struct curseg_info *sum = CURSEG_I(sbi, i);
		mutex_lock(&sum->curseg_mutex);
		write_sum_page(sbi, sum->sum_blk, blkaddr + (i - type));
		mutex_unlock(&sum->curseg_mutex);
	}
}

void write_data_summaries(struct f2fs_sb_info *sbi, block_t start_blk)
{
	if (is_set_ckpt_flags(F2FS_CKPT(sbi), CP_COMPACT_SUM_FLAG))
		write_compacted_summaries(sbi, start_blk);
	else
		write_normal_summaries(sbi, start_blk, CURSEG_HOT_DATA);
}

void write_node_summaries(struct f2fs_sb_info *sbi, block_t start_blk)
{
	if (is_set_ckpt_flags(F2FS_CKPT(sbi), CP_UMOUNT_FLAG))
		write_normal_summaries(sbi, start_blk, CURSEG_HOT_NODE);
}

int lookup_journal_in_cursum(struct f2fs_summary_block *sum, int type,
					unsigned int val, int alloc)
{
	int i;

	if (type == NAT_JOURNAL) {
		for (i = 0; i < nats_in_cursum(sum); i++) {
			if (le32_to_cpu(nid_in_journal(sum, i)) == val)
				return i;
		}
		if (alloc && nats_in_cursum(sum) < NAT_JOURNAL_ENTRIES)
			return update_nats_in_cursum(sum, 1);
	} else if (type == SIT_JOURNAL) {
		for (i = 0; i < sits_in_cursum(sum); i++)
			if (le32_to_cpu(segno_in_journal(sum, i)) == val)
				return i;
		if (alloc && sits_in_cursum(sum) < SIT_JOURNAL_ENTRIES)
			return update_sits_in_cursum(sum, 1);
	}
	return -1;
}

static struct page *get_current_sit_page(struct f2fs_sb_info *sbi,
					unsigned int segno)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned int offset = SIT_BLOCK_OFFSET(sit_i, segno);
	block_t blk_addr = sit_i->sit_base_addr + offset;

	check_seg_range(sbi, segno);

	/* calculate sit block address */
	if (f2fs_test_bit(offset, sit_i->sit_bitmap))
		blk_addr += sit_i->sit_blocks;

	return get_meta_page(sbi, blk_addr);
}

static struct page *get_next_sit_page(struct f2fs_sb_info *sbi,
					unsigned int start)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct page *src_page, *dst_page;
	pgoff_t src_off, dst_off;
	void *src_addr, *dst_addr;

	src_off = current_sit_addr(sbi, start);
	dst_off = next_sit_addr(sbi, src_off);

	/* get current sit block page without lock */
	src_page = get_meta_page(sbi, src_off);
	dst_page = grab_meta_page(sbi, dst_off);
	f2fs_bug_on(PageDirty(src_page));

	src_addr = page_address(src_page);
	dst_addr = page_address(dst_page);
	memcpy(dst_addr, src_addr, PAGE_CACHE_SIZE);

	set_page_dirty(dst_page);
	f2fs_put_page(src_page, 1);

	set_to_next_sit(sit_i, start);

	return dst_page;
}

static bool flush_sits_in_journal(struct f2fs_sb_info *sbi)
{
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_COLD_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	int i;

	/*
	 * If the journal area in the current summary is full of sit entries,
	 * all the sit entries will be flushed. Otherwise the sit entries
	 * are not able to replace with newly hot sit entries.
	 */
	if (sits_in_cursum(sum) >= SIT_JOURNAL_ENTRIES) {
		for (i = sits_in_cursum(sum) - 1; i >= 0; i--) {
			unsigned int segno;
			segno = le32_to_cpu(segno_in_journal(sum, i));
			__mark_sit_entry_dirty(sbi, segno);
		}
		update_sits_in_cursum(sum, -sits_in_cursum(sum));
		return true;
	}
	return false;
}

/*
 * CP calls this function, which flushes SIT entries including sit_journal,
 * and moves prefree segs to free segs.
 */
void flush_sit_entries(struct f2fs_sb_info *sbi)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned long *bitmap = sit_i->dirty_sentries_bitmap;
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_COLD_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	unsigned long nsegs = TOTAL_SEGS(sbi);
	struct page *page = NULL;
	struct f2fs_sit_block *raw_sit = NULL;
	unsigned int start = 0, end = 0;
	unsigned int segno = -1;
	bool flushed;

	mutex_lock(&curseg->curseg_mutex);
	mutex_lock(&sit_i->sentry_lock);

	/*
	 * "flushed" indicates whether sit entries in journal are flushed
	 * to the SIT area or not.
	 */
	flushed = flush_sits_in_journal(sbi);

	while ((segno = find_next_bit(bitmap, nsegs, segno + 1)) < nsegs) {
		struct seg_entry *se = get_seg_entry(sbi, segno);
		int sit_offset, offset;

		sit_offset = SIT_ENTRY_OFFSET(sit_i, segno);

		/* add discard candidates */
		if (SM_I(sbi)->nr_discards < SM_I(sbi)->max_discards)
			add_discard_addrs(sbi, segno, se);

		if (flushed)
			goto to_sit_page;

		offset = lookup_journal_in_cursum(sum, SIT_JOURNAL, segno, 1);
		if (offset >= 0) {
			segno_in_journal(sum, offset) = cpu_to_le32(segno);
			seg_info_to_raw_sit(se, &sit_in_journal(sum, offset));
			goto flush_done;
		}
to_sit_page:
		if (!page || (start > segno) || (segno > end)) {
			if (page) {
				f2fs_put_page(page, 1);
				page = NULL;
			}

			start = START_SEGNO(sit_i, segno);
			end = start + SIT_ENTRY_PER_BLOCK - 1;

			/* read sit block that will be updated */
			page = get_next_sit_page(sbi, start);
			raw_sit = page_address(page);
		}

		/* udpate entry in SIT block */
		seg_info_to_raw_sit(se, &raw_sit->entries[sit_offset]);
flush_done:
		__clear_bit(segno, bitmap);
		sit_i->dirty_sentries--;
	}
	mutex_unlock(&sit_i->sentry_lock);
	mutex_unlock(&curseg->curseg_mutex);

	/* writeout last modified SIT block */
	f2fs_put_page(page, 1);

	set_prefree_as_free_segments(sbi);
}

static int build_sit_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct sit_info *sit_i;
	unsigned int sit_segs, start;
	char *src_bitmap, *dst_bitmap;
	unsigned int bitmap_size;

	/* allocate memory for SIT information */
	sit_i = kzalloc(sizeof(struct sit_info), GFP_KERNEL);
	if (!sit_i)
		return -ENOMEM;

	SM_I(sbi)->sit_info = sit_i;

	sit_i->sentries = vzalloc(TOTAL_SEGS(sbi) * sizeof(struct seg_entry));
	if (!sit_i->sentries)
		return -ENOMEM;

	bitmap_size = f2fs_bitmap_size(TOTAL_SEGS(sbi));
	sit_i->dirty_sentries_bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!sit_i->dirty_sentries_bitmap)
		return -ENOMEM;

	for (start = 0; start < TOTAL_SEGS(sbi); start++) {
		sit_i->sentries[start].cur_valid_map
			= kzalloc(SIT_VBLOCK_MAP_SIZE, GFP_KERNEL);
		sit_i->sentries[start].ckpt_valid_map
			= kzalloc(SIT_VBLOCK_MAP_SIZE, GFP_KERNEL);
		if (!sit_i->sentries[start].cur_valid_map
				|| !sit_i->sentries[start].ckpt_valid_map)
			return -ENOMEM;
	}

	if (sbi->segs_per_sec > 1) {
		sit_i->sec_entries = vzalloc(TOTAL_SECS(sbi) *
					sizeof(struct sec_entry));
		if (!sit_i->sec_entries)
			return -ENOMEM;
	}

	/* get information related with SIT */
	sit_segs = le32_to_cpu(raw_super->segment_count_sit) >> 1;

	/* setup SIT bitmap from ckeckpoint pack */
	bitmap_size = __bitmap_size(sbi, SIT_BITMAP);
	src_bitmap = __bitmap_ptr(sbi, SIT_BITMAP);

	dst_bitmap = kmemdup(src_bitmap, bitmap_size, GFP_KERNEL);
	if (!dst_bitmap)
		return -ENOMEM;

	/* init SIT information */
	sit_i->s_ops = &default_salloc_ops;

	sit_i->sit_base_addr = le32_to_cpu(raw_super->sit_blkaddr);
	sit_i->sit_blocks = sit_segs << sbi->log_blocks_per_seg;
	sit_i->written_valid_blocks = le64_to_cpu(ckpt->valid_block_count);
	sit_i->sit_bitmap = dst_bitmap;
	sit_i->bitmap_size = bitmap_size;
	sit_i->dirty_sentries = 0;
	sit_i->sents_per_block = SIT_ENTRY_PER_BLOCK;
	sit_i->elapsed_time = le64_to_cpu(sbi->ckpt->elapsed_time);
	sit_i->mounted_time = CURRENT_TIME_SEC.tv_sec;
	mutex_init(&sit_i->sentry_lock);
	return 0;
}

static int build_free_segmap(struct f2fs_sb_info *sbi)
{
	struct f2fs_sm_info *sm_info = SM_I(sbi);
	struct free_segmap_info *free_i;
	unsigned int bitmap_size, sec_bitmap_size;

	/* allocate memory for free segmap information */
	free_i = kzalloc(sizeof(struct free_segmap_info), GFP_KERNEL);
	if (!free_i)
		return -ENOMEM;

	SM_I(sbi)->free_info = free_i;

	bitmap_size = f2fs_bitmap_size(TOTAL_SEGS(sbi));
	free_i->free_segmap = kmalloc(bitmap_size, GFP_KERNEL);
	if (!free_i->free_segmap)
		return -ENOMEM;

	sec_bitmap_size = f2fs_bitmap_size(TOTAL_SECS(sbi));
	free_i->free_secmap = kmalloc(sec_bitmap_size, GFP_KERNEL);
	if (!free_i->free_secmap)
		return -ENOMEM;

	/* set all segments as dirty temporarily */
	memset(free_i->free_segmap, 0xff, bitmap_size);
	memset(free_i->free_secmap, 0xff, sec_bitmap_size);

	/* init free segmap information */
	free_i->start_segno =
		(unsigned int) GET_SEGNO_FROM_SEG0(sbi, sm_info->main_blkaddr);
	free_i->free_segments = 0;
	free_i->free_sections = 0;
	rwlock_init(&free_i->segmap_lock);
	return 0;
}

static int build_curseg(struct f2fs_sb_info *sbi)
{
	struct curseg_info *array;
	int i;

	array = kcalloc(NR_CURSEG_TYPE, sizeof(*array), GFP_KERNEL);
	if (!array)
		return -ENOMEM;

	SM_I(sbi)->curseg_array = array;

	for (i = 0; i < NR_CURSEG_TYPE; i++) {
		mutex_init(&array[i].curseg_mutex);
		array[i].sum_blk = kzalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
		if (!array[i].sum_blk)
			return -ENOMEM;
		array[i].segno = NULL_SEGNO;
		array[i].next_blkoff = 0;
	}
	return restore_curseg_summaries(sbi);
}

static void build_sit_entries(struct f2fs_sb_info *sbi)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_COLD_DATA);
	struct f2fs_summary_block *sum = curseg->sum_blk;
	int sit_blk_cnt = SIT_BLK_CNT(sbi);
	unsigned int i, start, end;
	unsigned int readed, start_blk = 0;
	int nrpages = MAX_BIO_BLOCKS(max_hw_blocks(sbi));

	do {
		readed = ra_meta_pages(sbi, start_blk, nrpages, META_SIT);

		start = start_blk * sit_i->sents_per_block;
		end = (start_blk + readed) * sit_i->sents_per_block;

		for (; start < end && start < TOTAL_SEGS(sbi); start++) {
			struct seg_entry *se = &sit_i->sentries[start];
			struct f2fs_sit_block *sit_blk;
			struct f2fs_sit_entry sit;
			struct page *page;

			mutex_lock(&curseg->curseg_mutex);
			for (i = 0; i < sits_in_cursum(sum); i++) {
				if (le32_to_cpu(segno_in_journal(sum, i))
								== start) {
					sit = sit_in_journal(sum, i);
					mutex_unlock(&curseg->curseg_mutex);
					goto got_it;
				}
			}
			mutex_unlock(&curseg->curseg_mutex);

			page = get_current_sit_page(sbi, start);
			sit_blk = (struct f2fs_sit_block *)page_address(page);
			sit = sit_blk->entries[SIT_ENTRY_OFFSET(sit_i, start)];
			f2fs_put_page(page, 1);
got_it:
			check_block_count(sbi, start, &sit);
			seg_info_from_raw_sit(se, &sit);
			if (sbi->segs_per_sec > 1) {
				struct sec_entry *e = get_sec_entry(sbi, start);
				e->valid_blocks += se->valid_blocks;
			}
		}
		start_blk += readed;
	} while (start_blk < sit_blk_cnt);
}

static void init_free_segmap(struct f2fs_sb_info *sbi)
{
	unsigned int start;
	int type;

	for (start = 0; start < TOTAL_SEGS(sbi); start++) {
		struct seg_entry *sentry = get_seg_entry(sbi, start);
		if (!sentry->valid_blocks)
			__set_free(sbi, start);
	}

	/* set use the current segments */
	for (type = CURSEG_HOT_DATA; type <= CURSEG_COLD_NODE; type++) {
		struct curseg_info *curseg_t = CURSEG_I(sbi, type);
		__set_test_and_inuse(sbi, curseg_t->segno);
	}
}

static void init_dirty_segmap(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct free_segmap_info *free_i = FREE_I(sbi);
	unsigned int segno = 0, offset = 0, total_segs = TOTAL_SEGS(sbi);
	unsigned short valid_blocks;

	while (1) {
		/* find dirty segment based on free segmap */
		segno = find_next_inuse(free_i, total_segs, offset);
		if (segno >= total_segs)
			break;
		offset = segno + 1;
		valid_blocks = get_valid_blocks(sbi, segno, 0);
		if (valid_blocks >= sbi->blocks_per_seg || !valid_blocks)
			continue;
		mutex_lock(&dirty_i->seglist_lock);
		__locate_dirty_segment(sbi, segno, DIRTY);
		mutex_unlock(&dirty_i->seglist_lock);
	}
}

static int init_victim_secmap(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int bitmap_size = f2fs_bitmap_size(TOTAL_SECS(sbi));

	dirty_i->victim_secmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dirty_i->victim_secmap)
		return -ENOMEM;
	return 0;
}

static int build_dirty_segmap(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i;
	unsigned int bitmap_size, i;

	/* allocate memory for dirty segments list information */
	dirty_i = kzalloc(sizeof(struct dirty_seglist_info), GFP_KERNEL);
	if (!dirty_i)
		return -ENOMEM;

	SM_I(sbi)->dirty_info = dirty_i;
	mutex_init(&dirty_i->seglist_lock);

	bitmap_size = f2fs_bitmap_size(TOTAL_SEGS(sbi));

	for (i = 0; i < NR_DIRTY_TYPE; i++) {
		dirty_i->dirty_segmap[i] = kzalloc(bitmap_size, GFP_KERNEL);
		if (!dirty_i->dirty_segmap[i])
			return -ENOMEM;
	}

	init_dirty_segmap(sbi);
	return init_victim_secmap(sbi);
}

/*
 * Update min, max modified time for cost-benefit GC algorithm
 */
static void init_min_max_mtime(struct f2fs_sb_info *sbi)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned int segno;

	mutex_lock(&sit_i->sentry_lock);

	sit_i->min_mtime = LLONG_MAX;

	for (segno = 0; segno < TOTAL_SEGS(sbi); segno += sbi->segs_per_sec) {
		unsigned int i;
		unsigned long long mtime = 0;

		for (i = 0; i < sbi->segs_per_sec; i++)
			mtime += get_seg_entry(sbi, segno + i)->mtime;

		mtime = div_u64(mtime, sbi->segs_per_sec);

		if (sit_i->min_mtime > mtime)
			sit_i->min_mtime = mtime;
	}
	sit_i->max_mtime = get_mtime(sbi);
	mutex_unlock(&sit_i->sentry_lock);
}

int build_segment_manager(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct f2fs_sm_info *sm_info;
	int err;

	sm_info = kzalloc(sizeof(struct f2fs_sm_info), GFP_KERNEL);
	if (!sm_info)
		return -ENOMEM;

	/* init sm info */
	sbi->sm_info = sm_info;
	sm_info->seg0_blkaddr = le32_to_cpu(raw_super->segment0_blkaddr);
	sm_info->main_blkaddr = le32_to_cpu(raw_super->main_blkaddr);
	sm_info->segment_count = le32_to_cpu(raw_super->segment_count);
	sm_info->reserved_segments = le32_to_cpu(ckpt->rsvd_segment_count);
	sm_info->ovp_segments = le32_to_cpu(ckpt->overprov_segment_count);
	sm_info->main_segments = le32_to_cpu(raw_super->segment_count_main);
	sm_info->ssa_blkaddr = le32_to_cpu(raw_super->ssa_blkaddr);
	sm_info->rec_prefree_segments = sm_info->main_segments *
					DEF_RECLAIM_PREFREE_SEGMENTS / 100;
	sm_info->ipu_policy = F2FS_IPU_DISABLE;
	sm_info->min_ipu_util = DEF_MIN_IPU_UTIL;

	INIT_LIST_HEAD(&sm_info->discard_list);
	sm_info->nr_discards = 0;
	sm_info->max_discards = 0;

	if (test_opt(sbi, FLUSH_MERGE) && !f2fs_readonly(sbi->sb)) {
		err = create_flush_cmd_control(sbi);
		if (err)
			return err;
	}

	err = build_sit_info(sbi);
	if (err)
		return err;
	err = build_free_segmap(sbi);
	if (err)
		return err;
	err = build_curseg(sbi);
	if (err)
		return err;

	/* reinit free segmap based on SIT */
	build_sit_entries(sbi);

	init_free_segmap(sbi);
	err = build_dirty_segmap(sbi);
	if (err)
		return err;

	init_min_max_mtime(sbi);
	return 0;
}

static void discard_dirty_segmap(struct f2fs_sb_info *sbi,
		enum dirty_type dirty_type)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	mutex_lock(&dirty_i->seglist_lock);
	kfree(dirty_i->dirty_segmap[dirty_type]);
	dirty_i->nr_dirty[dirty_type] = 0;
	mutex_unlock(&dirty_i->seglist_lock);
}

static void destroy_victim_secmap(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	kfree(dirty_i->victim_secmap);
}

static void destroy_dirty_segmap(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	int i;

	if (!dirty_i)
		return;

	/* discard pre-free/dirty segments list */
	for (i = 0; i < NR_DIRTY_TYPE; i++)
		discard_dirty_segmap(sbi, i);

	destroy_victim_secmap(sbi);
	SM_I(sbi)->dirty_info = NULL;
	kfree(dirty_i);
}

static void destroy_curseg(struct f2fs_sb_info *sbi)
{
	struct curseg_info *array = SM_I(sbi)->curseg_array;
	int i;

	if (!array)
		return;
	SM_I(sbi)->curseg_array = NULL;
	for (i = 0; i < NR_CURSEG_TYPE; i++)
		kfree(array[i].sum_blk);
	kfree(array);
}

static void destroy_free_segmap(struct f2fs_sb_info *sbi)
{
	struct free_segmap_info *free_i = SM_I(sbi)->free_info;
	if (!free_i)
		return;
	SM_I(sbi)->free_info = NULL;
	kfree(free_i->free_segmap);
	kfree(free_i->free_secmap);
	kfree(free_i);
}

static void destroy_sit_info(struct f2fs_sb_info *sbi)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned int start;

	if (!sit_i)
		return;

	if (sit_i->sentries) {
		for (start = 0; start < TOTAL_SEGS(sbi); start++) {
			kfree(sit_i->sentries[start].cur_valid_map);
			kfree(sit_i->sentries[start].ckpt_valid_map);
		}
	}
	vfree(sit_i->sentries);
	vfree(sit_i->sec_entries);
	kfree(sit_i->dirty_sentries_bitmap);

	SM_I(sbi)->sit_info = NULL;
	kfree(sit_i->sit_bitmap);
	kfree(sit_i);
}

void destroy_segment_manager(struct f2fs_sb_info *sbi)
{
	struct f2fs_sm_info *sm_info = SM_I(sbi);

	if (!sm_info)
		return;
	destroy_flush_cmd_control(sbi);
	destroy_dirty_segmap(sbi);
	destroy_curseg(sbi);
	destroy_free_segmap(sbi);
	destroy_sit_info(sbi);
	sbi->sm_info = NULL;
	kfree(sm_info);
}

int __init create_segment_manager_caches(void)
{
	discard_entry_slab = f2fs_kmem_cache_create("discard_entry",
			sizeof(struct discard_entry));
	if (!discard_entry_slab)
		return -ENOMEM;
	return 0;
}

void destroy_segment_manager_caches(void)
{
	kmem_cache_destroy(discard_entry_slab);
}
