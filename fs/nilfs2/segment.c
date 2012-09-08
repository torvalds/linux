/*
 * segment.c - NILFS segment constructor.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */

#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/bio.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/crc32.h>
#include <linux/pagevec.h>
#include <linux/slab.h>
#include "nilfs.h"
#include "btnode.h"
#include "page.h"
#include "segment.h"
#include "sufile.h"
#include "cpfile.h"
#include "ifile.h"
#include "segbuf.h"


/*
 * Segment constructor
 */
#define SC_N_INODEVEC	16   /* Size of locally allocated inode vector */

#define SC_MAX_SEGDELTA 64   /* Upper limit of the number of segments
				appended in collection retry loop */

/* Construction mode */
enum {
	SC_LSEG_SR = 1,	/* Make a logical segment having a super root */
	SC_LSEG_DSYNC,	/* Flush data blocks of a given file and make
			   a logical segment without a super root */
	SC_FLUSH_FILE,	/* Flush data files, leads to segment writes without
			   creating a checkpoint */
	SC_FLUSH_DAT,	/* Flush DAT file. This also creates segments without
			   a checkpoint */
};

/* Stage numbers of dirty block collection */
enum {
	NILFS_ST_INIT = 0,
	NILFS_ST_GC,		/* Collecting dirty blocks for GC */
	NILFS_ST_FILE,
	NILFS_ST_IFILE,
	NILFS_ST_CPFILE,
	NILFS_ST_SUFILE,
	NILFS_ST_DAT,
	NILFS_ST_SR,		/* Super root */
	NILFS_ST_DSYNC,		/* Data sync blocks */
	NILFS_ST_DONE,
};

/* State flags of collection */
#define NILFS_CF_NODE		0x0001	/* Collecting node blocks */
#define NILFS_CF_IFILE_STARTED	0x0002	/* IFILE stage has started */
#define NILFS_CF_SUFREED	0x0004	/* segment usages has been freed */
#define NILFS_CF_HISTORY_MASK	(NILFS_CF_IFILE_STARTED | NILFS_CF_SUFREED)

/* Operations depending on the construction mode and file type */
struct nilfs_sc_operations {
	int (*collect_data)(struct nilfs_sc_info *, struct buffer_head *,
			    struct inode *);
	int (*collect_node)(struct nilfs_sc_info *, struct buffer_head *,
			    struct inode *);
	int (*collect_bmap)(struct nilfs_sc_info *, struct buffer_head *,
			    struct inode *);
	void (*write_data_binfo)(struct nilfs_sc_info *,
				 struct nilfs_segsum_pointer *,
				 union nilfs_binfo *);
	void (*write_node_binfo)(struct nilfs_sc_info *,
				 struct nilfs_segsum_pointer *,
				 union nilfs_binfo *);
};

/*
 * Other definitions
 */
static void nilfs_segctor_start_timer(struct nilfs_sc_info *);
static void nilfs_segctor_do_flush(struct nilfs_sc_info *, int);
static void nilfs_segctor_do_immediate_flush(struct nilfs_sc_info *);
static void nilfs_dispose_list(struct the_nilfs *, struct list_head *, int);

#define nilfs_cnt32_gt(a, b)   \
	(typecheck(__u32, a) && typecheck(__u32, b) && \
	 ((__s32)(b) - (__s32)(a) < 0))
#define nilfs_cnt32_ge(a, b)   \
	(typecheck(__u32, a) && typecheck(__u32, b) && \
	 ((__s32)(a) - (__s32)(b) >= 0))
#define nilfs_cnt32_lt(a, b)  nilfs_cnt32_gt(b, a)
#define nilfs_cnt32_le(a, b)  nilfs_cnt32_ge(b, a)

static int nilfs_prepare_segment_lock(struct nilfs_transaction_info *ti)
{
	struct nilfs_transaction_info *cur_ti = current->journal_info;
	void *save = NULL;

	if (cur_ti) {
		if (cur_ti->ti_magic == NILFS_TI_MAGIC)
			return ++cur_ti->ti_count;
		else {
			/*
			 * If journal_info field is occupied by other FS,
			 * it is saved and will be restored on
			 * nilfs_transaction_commit().
			 */
			printk(KERN_WARNING
			       "NILFS warning: journal info from a different "
			       "FS\n");
			save = current->journal_info;
		}
	}
	if (!ti) {
		ti = kmem_cache_alloc(nilfs_transaction_cachep, GFP_NOFS);
		if (!ti)
			return -ENOMEM;
		ti->ti_flags = NILFS_TI_DYNAMIC_ALLOC;
	} else {
		ti->ti_flags = 0;
	}
	ti->ti_count = 0;
	ti->ti_save = save;
	ti->ti_magic = NILFS_TI_MAGIC;
	current->journal_info = ti;
	return 0;
}

/**
 * nilfs_transaction_begin - start indivisible file operations.
 * @sb: super block
 * @ti: nilfs_transaction_info
 * @vacancy_check: flags for vacancy rate checks
 *
 * nilfs_transaction_begin() acquires a reader/writer semaphore, called
 * the segment semaphore, to make a segment construction and write tasks
 * exclusive.  The function is used with nilfs_transaction_commit() in pairs.
 * The region enclosed by these two functions can be nested.  To avoid a
 * deadlock, the semaphore is only acquired or released in the outermost call.
 *
 * This function allocates a nilfs_transaction_info struct to keep context
 * information on it.  It is initialized and hooked onto the current task in
 * the outermost call.  If a pre-allocated struct is given to @ti, it is used
 * instead; otherwise a new struct is assigned from a slab.
 *
 * When @vacancy_check flag is set, this function will check the amount of
 * free space, and will wait for the GC to reclaim disk space if low capacity.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-ENOSPC - No space left on device
 */
int nilfs_transaction_begin(struct super_block *sb,
			    struct nilfs_transaction_info *ti,
			    int vacancy_check)
{
	struct the_nilfs *nilfs;
	int ret = nilfs_prepare_segment_lock(ti);

	if (unlikely(ret < 0))
		return ret;
	if (ret > 0)
		return 0;

	vfs_check_frozen(sb, SB_FREEZE_WRITE);

	nilfs = sb->s_fs_info;
	down_read(&nilfs->ns_segctor_sem);
	if (vacancy_check && nilfs_near_disk_full(nilfs)) {
		up_read(&nilfs->ns_segctor_sem);
		ret = -ENOSPC;
		goto failed;
	}
	return 0;

 failed:
	ti = current->journal_info;
	current->journal_info = ti->ti_save;
	if (ti->ti_flags & NILFS_TI_DYNAMIC_ALLOC)
		kmem_cache_free(nilfs_transaction_cachep, ti);
	return ret;
}

/**
 * nilfs_transaction_commit - commit indivisible file operations.
 * @sb: super block
 *
 * nilfs_transaction_commit() releases the read semaphore which is
 * acquired by nilfs_transaction_begin(). This is only performed
 * in outermost call of this function.  If a commit flag is set,
 * nilfs_transaction_commit() sets a timer to start the segment
 * constructor.  If a sync flag is set, it starts construction
 * directly.
 */
int nilfs_transaction_commit(struct super_block *sb)
{
	struct nilfs_transaction_info *ti = current->journal_info;
	struct the_nilfs *nilfs = sb->s_fs_info;
	int err = 0;

	BUG_ON(ti == NULL || ti->ti_magic != NILFS_TI_MAGIC);
	ti->ti_flags |= NILFS_TI_COMMIT;
	if (ti->ti_count > 0) {
		ti->ti_count--;
		return 0;
	}
	if (nilfs->ns_writer) {
		struct nilfs_sc_info *sci = nilfs->ns_writer;

		if (ti->ti_flags & NILFS_TI_COMMIT)
			nilfs_segctor_start_timer(sci);
		if (atomic_read(&nilfs->ns_ndirtyblks) > sci->sc_watermark)
			nilfs_segctor_do_flush(sci, 0);
	}
	up_read(&nilfs->ns_segctor_sem);
	current->journal_info = ti->ti_save;

	if (ti->ti_flags & NILFS_TI_SYNC)
		err = nilfs_construct_segment(sb);
	if (ti->ti_flags & NILFS_TI_DYNAMIC_ALLOC)
		kmem_cache_free(nilfs_transaction_cachep, ti);
	return err;
}

void nilfs_transaction_abort(struct super_block *sb)
{
	struct nilfs_transaction_info *ti = current->journal_info;
	struct the_nilfs *nilfs = sb->s_fs_info;

	BUG_ON(ti == NULL || ti->ti_magic != NILFS_TI_MAGIC);
	if (ti->ti_count > 0) {
		ti->ti_count--;
		return;
	}
	up_read(&nilfs->ns_segctor_sem);

	current->journal_info = ti->ti_save;
	if (ti->ti_flags & NILFS_TI_DYNAMIC_ALLOC)
		kmem_cache_free(nilfs_transaction_cachep, ti);
}

void nilfs_relax_pressure_in_lock(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;

	if (!sci || !sci->sc_flush_request)
		return;

	set_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags);
	up_read(&nilfs->ns_segctor_sem);

	down_write(&nilfs->ns_segctor_sem);
	if (sci->sc_flush_request &&
	    test_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags)) {
		struct nilfs_transaction_info *ti = current->journal_info;

		ti->ti_flags |= NILFS_TI_WRITER;
		nilfs_segctor_do_immediate_flush(sci);
		ti->ti_flags &= ~NILFS_TI_WRITER;
	}
	downgrade_write(&nilfs->ns_segctor_sem);
}

static void nilfs_transaction_lock(struct super_block *sb,
				   struct nilfs_transaction_info *ti,
				   int gcflag)
{
	struct nilfs_transaction_info *cur_ti = current->journal_info;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;

	WARN_ON(cur_ti);
	ti->ti_flags = NILFS_TI_WRITER;
	ti->ti_count = 0;
	ti->ti_save = cur_ti;
	ti->ti_magic = NILFS_TI_MAGIC;
	INIT_LIST_HEAD(&ti->ti_garbage);
	current->journal_info = ti;

	for (;;) {
		down_write(&nilfs->ns_segctor_sem);
		if (!test_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags))
			break;

		nilfs_segctor_do_immediate_flush(sci);

		up_write(&nilfs->ns_segctor_sem);
		yield();
	}
	if (gcflag)
		ti->ti_flags |= NILFS_TI_GC;
}

static void nilfs_transaction_unlock(struct super_block *sb)
{
	struct nilfs_transaction_info *ti = current->journal_info;
	struct the_nilfs *nilfs = sb->s_fs_info;

	BUG_ON(ti == NULL || ti->ti_magic != NILFS_TI_MAGIC);
	BUG_ON(ti->ti_count > 0);

	up_write(&nilfs->ns_segctor_sem);
	current->journal_info = ti->ti_save;
	if (!list_empty(&ti->ti_garbage))
		nilfs_dispose_list(nilfs, &ti->ti_garbage, 0);
}

static void *nilfs_segctor_map_segsum_entry(struct nilfs_sc_info *sci,
					    struct nilfs_segsum_pointer *ssp,
					    unsigned bytes)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	unsigned blocksize = sci->sc_super->s_blocksize;
	void *p;

	if (unlikely(ssp->offset + bytes > blocksize)) {
		ssp->offset = 0;
		BUG_ON(NILFS_SEGBUF_BH_IS_LAST(ssp->bh,
					       &segbuf->sb_segsum_buffers));
		ssp->bh = NILFS_SEGBUF_NEXT_BH(ssp->bh);
	}
	p = ssp->bh->b_data + ssp->offset;
	ssp->offset += bytes;
	return p;
}

/**
 * nilfs_segctor_reset_segment_buffer - reset the current segment buffer
 * @sci: nilfs_sc_info
 */
static int nilfs_segctor_reset_segment_buffer(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	struct buffer_head *sumbh;
	unsigned sumbytes;
	unsigned flags = 0;
	int err;

	if (nilfs_doing_gc())
		flags = NILFS_SS_GC;
	err = nilfs_segbuf_reset(segbuf, flags, sci->sc_seg_ctime, sci->sc_cno);
	if (unlikely(err))
		return err;

	sumbh = NILFS_SEGBUF_FIRST_BH(&segbuf->sb_segsum_buffers);
	sumbytes = segbuf->sb_sum.sumbytes;
	sci->sc_finfo_ptr.bh = sumbh;  sci->sc_finfo_ptr.offset = sumbytes;
	sci->sc_binfo_ptr.bh = sumbh;  sci->sc_binfo_ptr.offset = sumbytes;
	sci->sc_blk_cnt = sci->sc_datablk_cnt = 0;
	return 0;
}

static int nilfs_segctor_feed_segment(struct nilfs_sc_info *sci)
{
	sci->sc_nblk_this_inc += sci->sc_curseg->sb_sum.nblocks;
	if (NILFS_SEGBUF_IS_LAST(sci->sc_curseg, &sci->sc_segbufs))
		return -E2BIG; /* The current segment is filled up
				  (internal code) */
	sci->sc_curseg = NILFS_NEXT_SEGBUF(sci->sc_curseg);
	return nilfs_segctor_reset_segment_buffer(sci);
}

static int nilfs_segctor_add_super_root(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf = sci->sc_curseg;
	int err;

	if (segbuf->sb_sum.nblocks >= segbuf->sb_rest_blocks) {
		err = nilfs_segctor_feed_segment(sci);
		if (err)
			return err;
		segbuf = sci->sc_curseg;
	}
	err = nilfs_segbuf_extend_payload(segbuf, &segbuf->sb_super_root);
	if (likely(!err))
		segbuf->sb_sum.flags |= NILFS_SS_SR;
	return err;
}

/*
 * Functions for making segment summary and payloads
 */
static int nilfs_segctor_segsum_block_required(
	struct nilfs_sc_info *sci, const struct nilfs_segsum_pointer *ssp,
	unsigned binfo_size)
{
	unsigned blocksize = sci->sc_super->s_blocksize;
	/* Size of finfo and binfo is enough small against blocksize */

	return ssp->offset + binfo_size +
		(!sci->sc_blk_cnt ? sizeof(struct nilfs_finfo) : 0) >
		blocksize;
}

static void nilfs_segctor_begin_finfo(struct nilfs_sc_info *sci,
				      struct inode *inode)
{
	sci->sc_curseg->sb_sum.nfinfo++;
	sci->sc_binfo_ptr = sci->sc_finfo_ptr;
	nilfs_segctor_map_segsum_entry(
		sci, &sci->sc_binfo_ptr, sizeof(struct nilfs_finfo));

	if (NILFS_I(inode)->i_root &&
	    !test_bit(NILFS_SC_HAVE_DELTA, &sci->sc_flags))
		set_bit(NILFS_SC_HAVE_DELTA, &sci->sc_flags);
	/* skip finfo */
}

static void nilfs_segctor_end_finfo(struct nilfs_sc_info *sci,
				    struct inode *inode)
{
	struct nilfs_finfo *finfo;
	struct nilfs_inode_info *ii;
	struct nilfs_segment_buffer *segbuf;
	__u64 cno;

	if (sci->sc_blk_cnt == 0)
		return;

	ii = NILFS_I(inode);

	if (test_bit(NILFS_I_GCINODE, &ii->i_state))
		cno = ii->i_cno;
	else if (NILFS_ROOT_METADATA_FILE(inode->i_ino))
		cno = 0;
	else
		cno = sci->sc_cno;

	finfo = nilfs_segctor_map_segsum_entry(sci, &sci->sc_finfo_ptr,
						 sizeof(*finfo));
	finfo->fi_ino = cpu_to_le64(inode->i_ino);
	finfo->fi_nblocks = cpu_to_le32(sci->sc_blk_cnt);
	finfo->fi_ndatablk = cpu_to_le32(sci->sc_datablk_cnt);
	finfo->fi_cno = cpu_to_le64(cno);

	segbuf = sci->sc_curseg;
	segbuf->sb_sum.sumbytes = sci->sc_binfo_ptr.offset +
		sci->sc_super->s_blocksize * (segbuf->sb_sum.nsumblk - 1);
	sci->sc_finfo_ptr = sci->sc_binfo_ptr;
	sci->sc_blk_cnt = sci->sc_datablk_cnt = 0;
}

static int nilfs_segctor_add_file_block(struct nilfs_sc_info *sci,
					struct buffer_head *bh,
					struct inode *inode,
					unsigned binfo_size)
{
	struct nilfs_segment_buffer *segbuf;
	int required, err = 0;

 retry:
	segbuf = sci->sc_curseg;
	required = nilfs_segctor_segsum_block_required(
		sci, &sci->sc_binfo_ptr, binfo_size);
	if (segbuf->sb_sum.nblocks + required + 1 > segbuf->sb_rest_blocks) {
		nilfs_segctor_end_finfo(sci, inode);
		err = nilfs_segctor_feed_segment(sci);
		if (err)
			return err;
		goto retry;
	}
	if (unlikely(required)) {
		err = nilfs_segbuf_extend_segsum(segbuf);
		if (unlikely(err))
			goto failed;
	}
	if (sci->sc_blk_cnt == 0)
		nilfs_segctor_begin_finfo(sci, inode);

	nilfs_segctor_map_segsum_entry(sci, &sci->sc_binfo_ptr, binfo_size);
	/* Substitution to vblocknr is delayed until update_blocknr() */
	nilfs_segbuf_add_file_buffer(segbuf, bh);
	sci->sc_blk_cnt++;
 failed:
	return err;
}

/*
 * Callback functions that enumerate, mark, and collect dirty blocks
 */
static int nilfs_collect_file_data(struct nilfs_sc_info *sci,
				   struct buffer_head *bh, struct inode *inode)
{
	int err;

	err = nilfs_bmap_propagate(NILFS_I(inode)->i_bmap, bh);
	if (err < 0)
		return err;

	err = nilfs_segctor_add_file_block(sci, bh, inode,
					   sizeof(struct nilfs_binfo_v));
	if (!err)
		sci->sc_datablk_cnt++;
	return err;
}

static int nilfs_collect_file_node(struct nilfs_sc_info *sci,
				   struct buffer_head *bh,
				   struct inode *inode)
{
	return nilfs_bmap_propagate(NILFS_I(inode)->i_bmap, bh);
}

static int nilfs_collect_file_bmap(struct nilfs_sc_info *sci,
				   struct buffer_head *bh,
				   struct inode *inode)
{
	WARN_ON(!buffer_dirty(bh));
	return nilfs_segctor_add_file_block(sci, bh, inode, sizeof(__le64));
}

static void nilfs_write_file_data_binfo(struct nilfs_sc_info *sci,
					struct nilfs_segsum_pointer *ssp,
					union nilfs_binfo *binfo)
{
	struct nilfs_binfo_v *binfo_v = nilfs_segctor_map_segsum_entry(
		sci, ssp, sizeof(*binfo_v));
	*binfo_v = binfo->bi_v;
}

static void nilfs_write_file_node_binfo(struct nilfs_sc_info *sci,
					struct nilfs_segsum_pointer *ssp,
					union nilfs_binfo *binfo)
{
	__le64 *vblocknr = nilfs_segctor_map_segsum_entry(
		sci, ssp, sizeof(*vblocknr));
	*vblocknr = binfo->bi_v.bi_vblocknr;
}

static struct nilfs_sc_operations nilfs_sc_file_ops = {
	.collect_data = nilfs_collect_file_data,
	.collect_node = nilfs_collect_file_node,
	.collect_bmap = nilfs_collect_file_bmap,
	.write_data_binfo = nilfs_write_file_data_binfo,
	.write_node_binfo = nilfs_write_file_node_binfo,
};

static int nilfs_collect_dat_data(struct nilfs_sc_info *sci,
				  struct buffer_head *bh, struct inode *inode)
{
	int err;

	err = nilfs_bmap_propagate(NILFS_I(inode)->i_bmap, bh);
	if (err < 0)
		return err;

	err = nilfs_segctor_add_file_block(sci, bh, inode, sizeof(__le64));
	if (!err)
		sci->sc_datablk_cnt++;
	return err;
}

static int nilfs_collect_dat_bmap(struct nilfs_sc_info *sci,
				  struct buffer_head *bh, struct inode *inode)
{
	WARN_ON(!buffer_dirty(bh));
	return nilfs_segctor_add_file_block(sci, bh, inode,
					    sizeof(struct nilfs_binfo_dat));
}

static void nilfs_write_dat_data_binfo(struct nilfs_sc_info *sci,
				       struct nilfs_segsum_pointer *ssp,
				       union nilfs_binfo *binfo)
{
	__le64 *blkoff = nilfs_segctor_map_segsum_entry(sci, ssp,
							  sizeof(*blkoff));
	*blkoff = binfo->bi_dat.bi_blkoff;
}

static void nilfs_write_dat_node_binfo(struct nilfs_sc_info *sci,
				       struct nilfs_segsum_pointer *ssp,
				       union nilfs_binfo *binfo)
{
	struct nilfs_binfo_dat *binfo_dat =
		nilfs_segctor_map_segsum_entry(sci, ssp, sizeof(*binfo_dat));
	*binfo_dat = binfo->bi_dat;
}

static struct nilfs_sc_operations nilfs_sc_dat_ops = {
	.collect_data = nilfs_collect_dat_data,
	.collect_node = nilfs_collect_file_node,
	.collect_bmap = nilfs_collect_dat_bmap,
	.write_data_binfo = nilfs_write_dat_data_binfo,
	.write_node_binfo = nilfs_write_dat_node_binfo,
};

static struct nilfs_sc_operations nilfs_sc_dsync_ops = {
	.collect_data = nilfs_collect_file_data,
	.collect_node = NULL,
	.collect_bmap = NULL,
	.write_data_binfo = nilfs_write_file_data_binfo,
	.write_node_binfo = NULL,
};

static size_t nilfs_lookup_dirty_data_buffers(struct inode *inode,
					      struct list_head *listp,
					      size_t nlimit,
					      loff_t start, loff_t end)
{
	struct address_space *mapping = inode->i_mapping;
	struct pagevec pvec;
	pgoff_t index = 0, last = ULONG_MAX;
	size_t ndirties = 0;
	int i;

	if (unlikely(start != 0 || end != LLONG_MAX)) {
		/*
		 * A valid range is given for sync-ing data pages. The
		 * range is rounded to per-page; extra dirty buffers
		 * may be included if blocksize < pagesize.
		 */
		index = start >> PAGE_SHIFT;
		last = end >> PAGE_SHIFT;
	}
	pagevec_init(&pvec, 0);
 repeat:
	if (unlikely(index > last) ||
	    !pagevec_lookup_tag(&pvec, mapping, &index, PAGECACHE_TAG_DIRTY,
				min_t(pgoff_t, last - index,
				      PAGEVEC_SIZE - 1) + 1))
		return ndirties;

	for (i = 0; i < pagevec_count(&pvec); i++) {
		struct buffer_head *bh, *head;
		struct page *page = pvec.pages[i];

		if (unlikely(page->index > last))
			break;

		lock_page(page);
		if (!page_has_buffers(page))
			create_empty_buffers(page, 1 << inode->i_blkbits, 0);
		unlock_page(page);

		bh = head = page_buffers(page);
		do {
			if (!buffer_dirty(bh))
				continue;
			get_bh(bh);
			list_add_tail(&bh->b_assoc_buffers, listp);
			ndirties++;
			if (unlikely(ndirties >= nlimit)) {
				pagevec_release(&pvec);
				cond_resched();
				return ndirties;
			}
		} while (bh = bh->b_this_page, bh != head);
	}
	pagevec_release(&pvec);
	cond_resched();
	goto repeat;
}

static void nilfs_lookup_dirty_node_buffers(struct inode *inode,
					    struct list_head *listp)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct address_space *mapping = &ii->i_btnode_cache;
	struct pagevec pvec;
	struct buffer_head *bh, *head;
	unsigned int i;
	pgoff_t index = 0;

	pagevec_init(&pvec, 0);

	while (pagevec_lookup_tag(&pvec, mapping, &index, PAGECACHE_TAG_DIRTY,
				  PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			bh = head = page_buffers(pvec.pages[i]);
			do {
				if (buffer_dirty(bh)) {
					get_bh(bh);
					list_add_tail(&bh->b_assoc_buffers,
						      listp);
				}
				bh = bh->b_this_page;
			} while (bh != head);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}

static void nilfs_dispose_list(struct the_nilfs *nilfs,
			       struct list_head *head, int force)
{
	struct nilfs_inode_info *ii, *n;
	struct nilfs_inode_info *ivec[SC_N_INODEVEC], **pii;
	unsigned nv = 0;

	while (!list_empty(head)) {
		spin_lock(&nilfs->ns_inode_lock);
		list_for_each_entry_safe(ii, n, head, i_dirty) {
			list_del_init(&ii->i_dirty);
			if (force) {
				if (unlikely(ii->i_bh)) {
					brelse(ii->i_bh);
					ii->i_bh = NULL;
				}
			} else if (test_bit(NILFS_I_DIRTY, &ii->i_state)) {
				set_bit(NILFS_I_QUEUED, &ii->i_state);
				list_add_tail(&ii->i_dirty,
					      &nilfs->ns_dirty_files);
				continue;
			}
			ivec[nv++] = ii;
			if (nv == SC_N_INODEVEC)
				break;
		}
		spin_unlock(&nilfs->ns_inode_lock);

		for (pii = ivec; nv > 0; pii++, nv--)
			iput(&(*pii)->vfs_inode);
	}
}

static int nilfs_test_metadata_dirty(struct the_nilfs *nilfs,
				     struct nilfs_root *root)
{
	int ret = 0;

	if (nilfs_mdt_fetch_dirty(root->ifile))
		ret++;
	if (nilfs_mdt_fetch_dirty(nilfs->ns_cpfile))
		ret++;
	if (nilfs_mdt_fetch_dirty(nilfs->ns_sufile))
		ret++;
	if ((ret || nilfs_doing_gc()) && nilfs_mdt_fetch_dirty(nilfs->ns_dat))
		ret++;
	return ret;
}

static int nilfs_segctor_clean(struct nilfs_sc_info *sci)
{
	return list_empty(&sci->sc_dirty_files) &&
		!test_bit(NILFS_SC_DIRTY, &sci->sc_flags) &&
		sci->sc_nfreesegs == 0 &&
		(!nilfs_doing_gc() || list_empty(&sci->sc_gc_inodes));
}

static int nilfs_segctor_confirm(struct nilfs_sc_info *sci)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	int ret = 0;

	if (nilfs_test_metadata_dirty(nilfs, sci->sc_root))
		set_bit(NILFS_SC_DIRTY, &sci->sc_flags);

	spin_lock(&nilfs->ns_inode_lock);
	if (list_empty(&nilfs->ns_dirty_files) && nilfs_segctor_clean(sci))
		ret++;

	spin_unlock(&nilfs->ns_inode_lock);
	return ret;
}

static void nilfs_segctor_clear_metadata_dirty(struct nilfs_sc_info *sci)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;

	nilfs_mdt_clear_dirty(sci->sc_root->ifile);
	nilfs_mdt_clear_dirty(nilfs->ns_cpfile);
	nilfs_mdt_clear_dirty(nilfs->ns_sufile);
	nilfs_mdt_clear_dirty(nilfs->ns_dat);
}

static int nilfs_segctor_create_checkpoint(struct nilfs_sc_info *sci)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	struct buffer_head *bh_cp;
	struct nilfs_checkpoint *raw_cp;
	int err;

	/* XXX: this interface will be changed */
	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, nilfs->ns_cno, 1,
					  &raw_cp, &bh_cp);
	if (likely(!err)) {
		/* The following code is duplicated with cpfile.  But, it is
		   needed to collect the checkpoint even if it was not newly
		   created */
		mark_buffer_dirty(bh_cp);
		nilfs_mdt_mark_dirty(nilfs->ns_cpfile);
		nilfs_cpfile_put_checkpoint(
			nilfs->ns_cpfile, nilfs->ns_cno, bh_cp);
	} else
		WARN_ON(err == -EINVAL || err == -ENOENT);

	return err;
}

static int nilfs_segctor_fill_in_checkpoint(struct nilfs_sc_info *sci)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	struct buffer_head *bh_cp;
	struct nilfs_checkpoint *raw_cp;
	int err;

	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, nilfs->ns_cno, 0,
					  &raw_cp, &bh_cp);
	if (unlikely(err)) {
		WARN_ON(err == -EINVAL || err == -ENOENT);
		goto failed_ibh;
	}
	raw_cp->cp_snapshot_list.ssl_next = 0;
	raw_cp->cp_snapshot_list.ssl_prev = 0;
	raw_cp->cp_inodes_count =
		cpu_to_le64(atomic_read(&sci->sc_root->inodes_count));
	raw_cp->cp_blocks_count =
		cpu_to_le64(atomic_read(&sci->sc_root->blocks_count));
	raw_cp->cp_nblk_inc =
		cpu_to_le64(sci->sc_nblk_inc + sci->sc_nblk_this_inc);
	raw_cp->cp_create = cpu_to_le64(sci->sc_seg_ctime);
	raw_cp->cp_cno = cpu_to_le64(nilfs->ns_cno);

	if (test_bit(NILFS_SC_HAVE_DELTA, &sci->sc_flags))
		nilfs_checkpoint_clear_minor(raw_cp);
	else
		nilfs_checkpoint_set_minor(raw_cp);

	nilfs_write_inode_common(sci->sc_root->ifile,
				 &raw_cp->cp_ifile_inode, 1);
	nilfs_cpfile_put_checkpoint(nilfs->ns_cpfile, nilfs->ns_cno, bh_cp);
	return 0;

 failed_ibh:
	return err;
}

static void nilfs_fill_in_file_bmap(struct inode *ifile,
				    struct nilfs_inode_info *ii)

{
	struct buffer_head *ibh;
	struct nilfs_inode *raw_inode;

	if (test_bit(NILFS_I_BMAP, &ii->i_state)) {
		ibh = ii->i_bh;
		BUG_ON(!ibh);
		raw_inode = nilfs_ifile_map_inode(ifile, ii->vfs_inode.i_ino,
						  ibh);
		nilfs_bmap_write(ii->i_bmap, raw_inode);
		nilfs_ifile_unmap_inode(ifile, ii->vfs_inode.i_ino, ibh);
	}
}

static void nilfs_segctor_fill_in_file_bmap(struct nilfs_sc_info *sci)
{
	struct nilfs_inode_info *ii;

	list_for_each_entry(ii, &sci->sc_dirty_files, i_dirty) {
		nilfs_fill_in_file_bmap(sci->sc_root->ifile, ii);
		set_bit(NILFS_I_COLLECTED, &ii->i_state);
	}
}

static void nilfs_segctor_fill_in_super_root(struct nilfs_sc_info *sci,
					     struct the_nilfs *nilfs)
{
	struct buffer_head *bh_sr;
	struct nilfs_super_root *raw_sr;
	unsigned isz, srsz;

	bh_sr = NILFS_LAST_SEGBUF(&sci->sc_segbufs)->sb_super_root;
	raw_sr = (struct nilfs_super_root *)bh_sr->b_data;
	isz = nilfs->ns_inode_size;
	srsz = NILFS_SR_BYTES(isz);

	raw_sr->sr_bytes = cpu_to_le16(srsz);
	raw_sr->sr_nongc_ctime
		= cpu_to_le64(nilfs_doing_gc() ?
			      nilfs->ns_nongc_ctime : sci->sc_seg_ctime);
	raw_sr->sr_flags = 0;

	nilfs_write_inode_common(nilfs->ns_dat, (void *)raw_sr +
				 NILFS_SR_DAT_OFFSET(isz), 1);
	nilfs_write_inode_common(nilfs->ns_cpfile, (void *)raw_sr +
				 NILFS_SR_CPFILE_OFFSET(isz), 1);
	nilfs_write_inode_common(nilfs->ns_sufile, (void *)raw_sr +
				 NILFS_SR_SUFILE_OFFSET(isz), 1);
	memset((void *)raw_sr + srsz, 0, nilfs->ns_blocksize - srsz);
}

static void nilfs_redirty_inodes(struct list_head *head)
{
	struct nilfs_inode_info *ii;

	list_for_each_entry(ii, head, i_dirty) {
		if (test_bit(NILFS_I_COLLECTED, &ii->i_state))
			clear_bit(NILFS_I_COLLECTED, &ii->i_state);
	}
}

static void nilfs_drop_collected_inodes(struct list_head *head)
{
	struct nilfs_inode_info *ii;

	list_for_each_entry(ii, head, i_dirty) {
		if (!test_and_clear_bit(NILFS_I_COLLECTED, &ii->i_state))
			continue;

		clear_bit(NILFS_I_INODE_DIRTY, &ii->i_state);
		set_bit(NILFS_I_UPDATED, &ii->i_state);
	}
}

static int nilfs_segctor_apply_buffers(struct nilfs_sc_info *sci,
				       struct inode *inode,
				       struct list_head *listp,
				       int (*collect)(struct nilfs_sc_info *,
						      struct buffer_head *,
						      struct inode *))
{
	struct buffer_head *bh, *n;
	int err = 0;

	if (collect) {
		list_for_each_entry_safe(bh, n, listp, b_assoc_buffers) {
			list_del_init(&bh->b_assoc_buffers);
			err = collect(sci, bh, inode);
			brelse(bh);
			if (unlikely(err))
				goto dispose_buffers;
		}
		return 0;
	}

 dispose_buffers:
	while (!list_empty(listp)) {
		bh = list_first_entry(listp, struct buffer_head,
				      b_assoc_buffers);
		list_del_init(&bh->b_assoc_buffers);
		brelse(bh);
	}
	return err;
}

static size_t nilfs_segctor_buffer_rest(struct nilfs_sc_info *sci)
{
	/* Remaining number of blocks within segment buffer */
	return sci->sc_segbuf_nblocks -
		(sci->sc_nblk_this_inc + sci->sc_curseg->sb_sum.nblocks);
}

static int nilfs_segctor_scan_file(struct nilfs_sc_info *sci,
				   struct inode *inode,
				   struct nilfs_sc_operations *sc_ops)
{
	LIST_HEAD(data_buffers);
	LIST_HEAD(node_buffers);
	int err;

	if (!(sci->sc_stage.flags & NILFS_CF_NODE)) {
		size_t n, rest = nilfs_segctor_buffer_rest(sci);

		n = nilfs_lookup_dirty_data_buffers(
			inode, &data_buffers, rest + 1, 0, LLONG_MAX);
		if (n > rest) {
			err = nilfs_segctor_apply_buffers(
				sci, inode, &data_buffers,
				sc_ops->collect_data);
			BUG_ON(!err); /* always receive -E2BIG or true error */
			goto break_or_fail;
		}
	}
	nilfs_lookup_dirty_node_buffers(inode, &node_buffers);

	if (!(sci->sc_stage.flags & NILFS_CF_NODE)) {
		err = nilfs_segctor_apply_buffers(
			sci, inode, &data_buffers, sc_ops->collect_data);
		if (unlikely(err)) {
			/* dispose node list */
			nilfs_segctor_apply_buffers(
				sci, inode, &node_buffers, NULL);
			goto break_or_fail;
		}
		sci->sc_stage.flags |= NILFS_CF_NODE;
	}
	/* Collect node */
	err = nilfs_segctor_apply_buffers(
		sci, inode, &node_buffers, sc_ops->collect_node);
	if (unlikely(err))
		goto break_or_fail;

	nilfs_bmap_lookup_dirty_buffers(NILFS_I(inode)->i_bmap, &node_buffers);
	err = nilfs_segctor_apply_buffers(
		sci, inode, &node_buffers, sc_ops->collect_bmap);
	if (unlikely(err))
		goto break_or_fail;

	nilfs_segctor_end_finfo(sci, inode);
	sci->sc_stage.flags &= ~NILFS_CF_NODE;

 break_or_fail:
	return err;
}

static int nilfs_segctor_scan_file_dsync(struct nilfs_sc_info *sci,
					 struct inode *inode)
{
	LIST_HEAD(data_buffers);
	size_t n, rest = nilfs_segctor_buffer_rest(sci);
	int err;

	n = nilfs_lookup_dirty_data_buffers(inode, &data_buffers, rest + 1,
					    sci->sc_dsync_start,
					    sci->sc_dsync_end);

	err = nilfs_segctor_apply_buffers(sci, inode, &data_buffers,
					  nilfs_collect_file_data);
	if (!err) {
		nilfs_segctor_end_finfo(sci, inode);
		BUG_ON(n > rest);
		/* always receive -E2BIG or true error if n > rest */
	}
	return err;
}

static int nilfs_segctor_collect_blocks(struct nilfs_sc_info *sci, int mode)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	struct list_head *head;
	struct nilfs_inode_info *ii;
	size_t ndone;
	int err = 0;

	switch (sci->sc_stage.scnt) {
	case NILFS_ST_INIT:
		/* Pre-processes */
		sci->sc_stage.flags = 0;

		if (!test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags)) {
			sci->sc_nblk_inc = 0;
			sci->sc_curseg->sb_sum.flags = NILFS_SS_LOGBGN;
			if (mode == SC_LSEG_DSYNC) {
				sci->sc_stage.scnt = NILFS_ST_DSYNC;
				goto dsync_mode;
			}
		}

		sci->sc_stage.dirty_file_ptr = NULL;
		sci->sc_stage.gc_inode_ptr = NULL;
		if (mode == SC_FLUSH_DAT) {
			sci->sc_stage.scnt = NILFS_ST_DAT;
			goto dat_stage;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_GC:
		if (nilfs_doing_gc()) {
			head = &sci->sc_gc_inodes;
			ii = list_prepare_entry(sci->sc_stage.gc_inode_ptr,
						head, i_dirty);
			list_for_each_entry_continue(ii, head, i_dirty) {
				err = nilfs_segctor_scan_file(
					sci, &ii->vfs_inode,
					&nilfs_sc_file_ops);
				if (unlikely(err)) {
					sci->sc_stage.gc_inode_ptr = list_entry(
						ii->i_dirty.prev,
						struct nilfs_inode_info,
						i_dirty);
					goto break_or_fail;
				}
				set_bit(NILFS_I_COLLECTED, &ii->i_state);
			}
			sci->sc_stage.gc_inode_ptr = NULL;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_FILE:
		head = &sci->sc_dirty_files;
		ii = list_prepare_entry(sci->sc_stage.dirty_file_ptr, head,
					i_dirty);
		list_for_each_entry_continue(ii, head, i_dirty) {
			clear_bit(NILFS_I_DIRTY, &ii->i_state);

			err = nilfs_segctor_scan_file(sci, &ii->vfs_inode,
						      &nilfs_sc_file_ops);
			if (unlikely(err)) {
				sci->sc_stage.dirty_file_ptr =
					list_entry(ii->i_dirty.prev,
						   struct nilfs_inode_info,
						   i_dirty);
				goto break_or_fail;
			}
			/* sci->sc_stage.dirty_file_ptr = NILFS_I(inode); */
			/* XXX: required ? */
		}
		sci->sc_stage.dirty_file_ptr = NULL;
		if (mode == SC_FLUSH_FILE) {
			sci->sc_stage.scnt = NILFS_ST_DONE;
			return 0;
		}
		sci->sc_stage.scnt++;
		sci->sc_stage.flags |= NILFS_CF_IFILE_STARTED;
		/* Fall through */
	case NILFS_ST_IFILE:
		err = nilfs_segctor_scan_file(sci, sci->sc_root->ifile,
					      &nilfs_sc_file_ops);
		if (unlikely(err))
			break;
		sci->sc_stage.scnt++;
		/* Creating a checkpoint */
		err = nilfs_segctor_create_checkpoint(sci);
		if (unlikely(err))
			break;
		/* Fall through */
	case NILFS_ST_CPFILE:
		err = nilfs_segctor_scan_file(sci, nilfs->ns_cpfile,
					      &nilfs_sc_file_ops);
		if (unlikely(err))
			break;
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_SUFILE:
		err = nilfs_sufile_freev(nilfs->ns_sufile, sci->sc_freesegs,
					 sci->sc_nfreesegs, &ndone);
		if (unlikely(err)) {
			nilfs_sufile_cancel_freev(nilfs->ns_sufile,
						  sci->sc_freesegs, ndone,
						  NULL);
			break;
		}
		sci->sc_stage.flags |= NILFS_CF_SUFREED;

		err = nilfs_segctor_scan_file(sci, nilfs->ns_sufile,
					      &nilfs_sc_file_ops);
		if (unlikely(err))
			break;
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_DAT:
 dat_stage:
		err = nilfs_segctor_scan_file(sci, nilfs->ns_dat,
					      &nilfs_sc_dat_ops);
		if (unlikely(err))
			break;
		if (mode == SC_FLUSH_DAT) {
			sci->sc_stage.scnt = NILFS_ST_DONE;
			return 0;
		}
		sci->sc_stage.scnt++;  /* Fall through */
	case NILFS_ST_SR:
		if (mode == SC_LSEG_SR) {
			/* Appending a super root */
			err = nilfs_segctor_add_super_root(sci);
			if (unlikely(err))
				break;
		}
		/* End of a logical segment */
		sci->sc_curseg->sb_sum.flags |= NILFS_SS_LOGEND;
		sci->sc_stage.scnt = NILFS_ST_DONE;
		return 0;
	case NILFS_ST_DSYNC:
 dsync_mode:
		sci->sc_curseg->sb_sum.flags |= NILFS_SS_SYNDT;
		ii = sci->sc_dsync_inode;
		if (!test_bit(NILFS_I_BUSY, &ii->i_state))
			break;

		err = nilfs_segctor_scan_file_dsync(sci, &ii->vfs_inode);
		if (unlikely(err))
			break;
		sci->sc_curseg->sb_sum.flags |= NILFS_SS_LOGEND;
		sci->sc_stage.scnt = NILFS_ST_DONE;
		return 0;
	case NILFS_ST_DONE:
		return 0;
	default:
		BUG();
	}

 break_or_fail:
	return err;
}

/**
 * nilfs_segctor_begin_construction - setup segment buffer to make a new log
 * @sci: nilfs_sc_info
 * @nilfs: nilfs object
 */
static int nilfs_segctor_begin_construction(struct nilfs_sc_info *sci,
					    struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf, *prev;
	__u64 nextnum;
	int err, alloc = 0;

	segbuf = nilfs_segbuf_new(sci->sc_super);
	if (unlikely(!segbuf))
		return -ENOMEM;

	if (list_empty(&sci->sc_write_logs)) {
		nilfs_segbuf_map(segbuf, nilfs->ns_segnum,
				 nilfs->ns_pseg_offset, nilfs);
		if (segbuf->sb_rest_blocks < NILFS_PSEG_MIN_BLOCKS) {
			nilfs_shift_to_next_segment(nilfs);
			nilfs_segbuf_map(segbuf, nilfs->ns_segnum, 0, nilfs);
		}

		segbuf->sb_sum.seg_seq = nilfs->ns_seg_seq;
		nextnum = nilfs->ns_nextnum;

		if (nilfs->ns_segnum == nilfs->ns_nextnum)
			/* Start from the head of a new full segment */
			alloc++;
	} else {
		/* Continue logs */
		prev = NILFS_LAST_SEGBUF(&sci->sc_write_logs);
		nilfs_segbuf_map_cont(segbuf, prev);
		segbuf->sb_sum.seg_seq = prev->sb_sum.seg_seq;
		nextnum = prev->sb_nextnum;

		if (segbuf->sb_rest_blocks < NILFS_PSEG_MIN_BLOCKS) {
			nilfs_segbuf_map(segbuf, prev->sb_nextnum, 0, nilfs);
			segbuf->sb_sum.seg_seq++;
			alloc++;
		}
	}

	err = nilfs_sufile_mark_dirty(nilfs->ns_sufile, segbuf->sb_segnum);
	if (err)
		goto failed;

	if (alloc) {
		err = nilfs_sufile_alloc(nilfs->ns_sufile, &nextnum);
		if (err)
			goto failed;
	}
	nilfs_segbuf_set_next_segnum(segbuf, nextnum, nilfs);

	BUG_ON(!list_empty(&sci->sc_segbufs));
	list_add_tail(&segbuf->sb_list, &sci->sc_segbufs);
	sci->sc_segbuf_nblocks = segbuf->sb_rest_blocks;
	return 0;

 failed:
	nilfs_segbuf_free(segbuf);
	return err;
}

static int nilfs_segctor_extend_segments(struct nilfs_sc_info *sci,
					 struct the_nilfs *nilfs, int nadd)
{
	struct nilfs_segment_buffer *segbuf, *prev;
	struct inode *sufile = nilfs->ns_sufile;
	__u64 nextnextnum;
	LIST_HEAD(list);
	int err, ret, i;

	prev = NILFS_LAST_SEGBUF(&sci->sc_segbufs);
	/*
	 * Since the segment specified with nextnum might be allocated during
	 * the previous construction, the buffer including its segusage may
	 * not be dirty.  The following call ensures that the buffer is dirty
	 * and will pin the buffer on memory until the sufile is written.
	 */
	err = nilfs_sufile_mark_dirty(sufile, prev->sb_nextnum);
	if (unlikely(err))
		return err;

	for (i = 0; i < nadd; i++) {
		/* extend segment info */
		err = -ENOMEM;
		segbuf = nilfs_segbuf_new(sci->sc_super);
		if (unlikely(!segbuf))
			goto failed;

		/* map this buffer to region of segment on-disk */
		nilfs_segbuf_map(segbuf, prev->sb_nextnum, 0, nilfs);
		sci->sc_segbuf_nblocks += segbuf->sb_rest_blocks;

		/* allocate the next next full segment */
		err = nilfs_sufile_alloc(sufile, &nextnextnum);
		if (unlikely(err))
			goto failed_segbuf;

		segbuf->sb_sum.seg_seq = prev->sb_sum.seg_seq + 1;
		nilfs_segbuf_set_next_segnum(segbuf, nextnextnum, nilfs);

		list_add_tail(&segbuf->sb_list, &list);
		prev = segbuf;
	}
	list_splice_tail(&list, &sci->sc_segbufs);
	return 0;

 failed_segbuf:
	nilfs_segbuf_free(segbuf);
 failed:
	list_for_each_entry(segbuf, &list, sb_list) {
		ret = nilfs_sufile_free(sufile, segbuf->sb_nextnum);
		WARN_ON(ret); /* never fails */
	}
	nilfs_destroy_logs(&list);
	return err;
}

static void nilfs_free_incomplete_logs(struct list_head *logs,
				       struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf, *prev;
	struct inode *sufile = nilfs->ns_sufile;
	int ret;

	segbuf = NILFS_FIRST_SEGBUF(logs);
	if (nilfs->ns_nextnum != segbuf->sb_nextnum) {
		ret = nilfs_sufile_free(sufile, segbuf->sb_nextnum);
		WARN_ON(ret); /* never fails */
	}
	if (atomic_read(&segbuf->sb_err)) {
		/* Case 1: The first segment failed */
		if (segbuf->sb_pseg_start != segbuf->sb_fseg_start)
			/* Case 1a:  Partial segment appended into an existing
			   segment */
			nilfs_terminate_segment(nilfs, segbuf->sb_fseg_start,
						segbuf->sb_fseg_end);
		else /* Case 1b:  New full segment */
			set_nilfs_discontinued(nilfs);
	}

	prev = segbuf;
	list_for_each_entry_continue(segbuf, logs, sb_list) {
		if (prev->sb_nextnum != segbuf->sb_nextnum) {
			ret = nilfs_sufile_free(sufile, segbuf->sb_nextnum);
			WARN_ON(ret); /* never fails */
		}
		if (atomic_read(&segbuf->sb_err) &&
		    segbuf->sb_segnum != nilfs->ns_nextnum)
			/* Case 2: extended segment (!= next) failed */
			nilfs_sufile_set_error(sufile, segbuf->sb_segnum);
		prev = segbuf;
	}
}

static void nilfs_segctor_update_segusage(struct nilfs_sc_info *sci,
					  struct inode *sufile)
{
	struct nilfs_segment_buffer *segbuf;
	unsigned long live_blocks;
	int ret;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		live_blocks = segbuf->sb_sum.nblocks +
			(segbuf->sb_pseg_start - segbuf->sb_fseg_start);
		ret = nilfs_sufile_set_segment_usage(sufile, segbuf->sb_segnum,
						     live_blocks,
						     sci->sc_seg_ctime);
		WARN_ON(ret); /* always succeed because the segusage is dirty */
	}
}

static void nilfs_cancel_segusage(struct list_head *logs, struct inode *sufile)
{
	struct nilfs_segment_buffer *segbuf;
	int ret;

	segbuf = NILFS_FIRST_SEGBUF(logs);
	ret = nilfs_sufile_set_segment_usage(sufile, segbuf->sb_segnum,
					     segbuf->sb_pseg_start -
					     segbuf->sb_fseg_start, 0);
	WARN_ON(ret); /* always succeed because the segusage is dirty */

	list_for_each_entry_continue(segbuf, logs, sb_list) {
		ret = nilfs_sufile_set_segment_usage(sufile, segbuf->sb_segnum,
						     0, 0);
		WARN_ON(ret); /* always succeed */
	}
}

static void nilfs_segctor_truncate_segments(struct nilfs_sc_info *sci,
					    struct nilfs_segment_buffer *last,
					    struct inode *sufile)
{
	struct nilfs_segment_buffer *segbuf = last;
	int ret;

	list_for_each_entry_continue(segbuf, &sci->sc_segbufs, sb_list) {
		sci->sc_segbuf_nblocks -= segbuf->sb_rest_blocks;
		ret = nilfs_sufile_free(sufile, segbuf->sb_nextnum);
		WARN_ON(ret);
	}
	nilfs_truncate_logs(&sci->sc_segbufs, last);
}


static int nilfs_segctor_collect(struct nilfs_sc_info *sci,
				 struct the_nilfs *nilfs, int mode)
{
	struct nilfs_cstage prev_stage = sci->sc_stage;
	int err, nadd = 1;

	/* Collection retry loop */
	for (;;) {
		sci->sc_nblk_this_inc = 0;
		sci->sc_curseg = NILFS_FIRST_SEGBUF(&sci->sc_segbufs);

		err = nilfs_segctor_reset_segment_buffer(sci);
		if (unlikely(err))
			goto failed;

		err = nilfs_segctor_collect_blocks(sci, mode);
		sci->sc_nblk_this_inc += sci->sc_curseg->sb_sum.nblocks;
		if (!err)
			break;

		if (unlikely(err != -E2BIG))
			goto failed;

		/* The current segment is filled up */
		if (mode != SC_LSEG_SR || sci->sc_stage.scnt < NILFS_ST_CPFILE)
			break;

		nilfs_clear_logs(&sci->sc_segbufs);

		err = nilfs_segctor_extend_segments(sci, nilfs, nadd);
		if (unlikely(err))
			return err;

		if (sci->sc_stage.flags & NILFS_CF_SUFREED) {
			err = nilfs_sufile_cancel_freev(nilfs->ns_sufile,
							sci->sc_freesegs,
							sci->sc_nfreesegs,
							NULL);
			WARN_ON(err); /* do not happen */
		}
		nadd = min_t(int, nadd << 1, SC_MAX_SEGDELTA);
		sci->sc_stage = prev_stage;
	}
	nilfs_segctor_truncate_segments(sci, sci->sc_curseg, nilfs->ns_sufile);
	return 0;

 failed:
	return err;
}

static void nilfs_list_replace_buffer(struct buffer_head *old_bh,
				      struct buffer_head *new_bh)
{
	BUG_ON(!list_empty(&new_bh->b_assoc_buffers));

	list_replace_init(&old_bh->b_assoc_buffers, &new_bh->b_assoc_buffers);
	/* The caller must release old_bh */
}

static int
nilfs_segctor_update_payload_blocknr(struct nilfs_sc_info *sci,
				     struct nilfs_segment_buffer *segbuf,
				     int mode)
{
	struct inode *inode = NULL;
	sector_t blocknr;
	unsigned long nfinfo = segbuf->sb_sum.nfinfo;
	unsigned long nblocks = 0, ndatablk = 0;
	struct nilfs_sc_operations *sc_op = NULL;
	struct nilfs_segsum_pointer ssp;
	struct nilfs_finfo *finfo = NULL;
	union nilfs_binfo binfo;
	struct buffer_head *bh, *bh_org;
	ino_t ino = 0;
	int err = 0;

	if (!nfinfo)
		goto out;

	blocknr = segbuf->sb_pseg_start + segbuf->sb_sum.nsumblk;
	ssp.bh = NILFS_SEGBUF_FIRST_BH(&segbuf->sb_segsum_buffers);
	ssp.offset = sizeof(struct nilfs_segment_summary);

	list_for_each_entry(bh, &segbuf->sb_payload_buffers, b_assoc_buffers) {
		if (bh == segbuf->sb_super_root)
			break;
		if (!finfo) {
			finfo =	nilfs_segctor_map_segsum_entry(
				sci, &ssp, sizeof(*finfo));
			ino = le64_to_cpu(finfo->fi_ino);
			nblocks = le32_to_cpu(finfo->fi_nblocks);
			ndatablk = le32_to_cpu(finfo->fi_ndatablk);

			inode = bh->b_page->mapping->host;

			if (mode == SC_LSEG_DSYNC)
				sc_op = &nilfs_sc_dsync_ops;
			else if (ino == NILFS_DAT_INO)
				sc_op = &nilfs_sc_dat_ops;
			else /* file blocks */
				sc_op = &nilfs_sc_file_ops;
		}
		bh_org = bh;
		get_bh(bh_org);
		err = nilfs_bmap_assign(NILFS_I(inode)->i_bmap, &bh, blocknr,
					&binfo);
		if (bh != bh_org)
			nilfs_list_replace_buffer(bh_org, bh);
		brelse(bh_org);
		if (unlikely(err))
			goto failed_bmap;

		if (ndatablk > 0)
			sc_op->write_data_binfo(sci, &ssp, &binfo);
		else
			sc_op->write_node_binfo(sci, &ssp, &binfo);

		blocknr++;
		if (--nblocks == 0) {
			finfo = NULL;
			if (--nfinfo == 0)
				break;
		} else if (ndatablk > 0)
			ndatablk--;
	}
 out:
	return 0;

 failed_bmap:
	return err;
}

static int nilfs_segctor_assign(struct nilfs_sc_info *sci, int mode)
{
	struct nilfs_segment_buffer *segbuf;
	int err;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		err = nilfs_segctor_update_payload_blocknr(sci, segbuf, mode);
		if (unlikely(err))
			return err;
		nilfs_segbuf_fill_in_segsum(segbuf);
	}
	return 0;
}

static void nilfs_begin_page_io(struct page *page)
{
	if (!page || PageWriteback(page))
		/* For split b-tree node pages, this function may be called
		   twice.  We ignore the 2nd or later calls by this check. */
		return;

	lock_page(page);
	clear_page_dirty_for_io(page);
	set_page_writeback(page);
	unlock_page(page);
}

static void nilfs_segctor_prepare_write(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf;
	struct page *bd_page = NULL, *fs_page = NULL;

	list_for_each_entry(segbuf, &sci->sc_segbufs, sb_list) {
		struct buffer_head *bh;

		list_for_each_entry(bh, &segbuf->sb_segsum_buffers,
				    b_assoc_buffers) {
			if (bh->b_page != bd_page) {
				if (bd_page) {
					lock_page(bd_page);
					clear_page_dirty_for_io(bd_page);
					set_page_writeback(bd_page);
					unlock_page(bd_page);
				}
				bd_page = bh->b_page;
			}
		}

		list_for_each_entry(bh, &segbuf->sb_payload_buffers,
				    b_assoc_buffers) {
			if (bh == segbuf->sb_super_root) {
				if (bh->b_page != bd_page) {
					lock_page(bd_page);
					clear_page_dirty_for_io(bd_page);
					set_page_writeback(bd_page);
					unlock_page(bd_page);
					bd_page = bh->b_page;
				}
				break;
			}
			if (bh->b_page != fs_page) {
				nilfs_begin_page_io(fs_page);
				fs_page = bh->b_page;
			}
		}
	}
	if (bd_page) {
		lock_page(bd_page);
		clear_page_dirty_for_io(bd_page);
		set_page_writeback(bd_page);
		unlock_page(bd_page);
	}
	nilfs_begin_page_io(fs_page);
}

static int nilfs_segctor_write(struct nilfs_sc_info *sci,
			       struct the_nilfs *nilfs)
{
	int ret;

	ret = nilfs_write_logs(&sci->sc_segbufs, nilfs);
	list_splice_tail_init(&sci->sc_segbufs, &sci->sc_write_logs);
	return ret;
}

static void nilfs_end_page_io(struct page *page, int err)
{
	if (!page)
		return;

	if (buffer_nilfs_node(page_buffers(page)) && !PageWriteback(page)) {
		/*
		 * For b-tree node pages, this function may be called twice
		 * or more because they might be split in a segment.
		 */
		if (PageDirty(page)) {
			/*
			 * For pages holding split b-tree node buffers, dirty
			 * flag on the buffers may be cleared discretely.
			 * In that case, the page is once redirtied for
			 * remaining buffers, and it must be cancelled if
			 * all the buffers get cleaned later.
			 */
			lock_page(page);
			if (nilfs_page_buffers_clean(page))
				__nilfs_clear_page_dirty(page);
			unlock_page(page);
		}
		return;
	}

	if (!err) {
		if (!nilfs_page_buffers_clean(page))
			__set_page_dirty_nobuffers(page);
		ClearPageError(page);
	} else {
		__set_page_dirty_nobuffers(page);
		SetPageError(page);
	}

	end_page_writeback(page);
}

static void nilfs_abort_logs(struct list_head *logs, int err)
{
	struct nilfs_segment_buffer *segbuf;
	struct page *bd_page = NULL, *fs_page = NULL;
	struct buffer_head *bh;

	if (list_empty(logs))
		return;

	list_for_each_entry(segbuf, logs, sb_list) {
		list_for_each_entry(bh, &segbuf->sb_segsum_buffers,
				    b_assoc_buffers) {
			if (bh->b_page != bd_page) {
				if (bd_page)
					end_page_writeback(bd_page);
				bd_page = bh->b_page;
			}
		}

		list_for_each_entry(bh, &segbuf->sb_payload_buffers,
				    b_assoc_buffers) {
			if (bh == segbuf->sb_super_root) {
				if (bh->b_page != bd_page) {
					end_page_writeback(bd_page);
					bd_page = bh->b_page;
				}
				break;
			}
			if (bh->b_page != fs_page) {
				nilfs_end_page_io(fs_page, err);
				fs_page = bh->b_page;
			}
		}
	}
	if (bd_page)
		end_page_writeback(bd_page);

	nilfs_end_page_io(fs_page, err);
}

static void nilfs_segctor_abort_construction(struct nilfs_sc_info *sci,
					     struct the_nilfs *nilfs, int err)
{
	LIST_HEAD(logs);
	int ret;

	list_splice_tail_init(&sci->sc_write_logs, &logs);
	ret = nilfs_wait_on_logs(&logs);
	nilfs_abort_logs(&logs, ret ? : err);

	list_splice_tail_init(&sci->sc_segbufs, &logs);
	nilfs_cancel_segusage(&logs, nilfs->ns_sufile);
	nilfs_free_incomplete_logs(&logs, nilfs);

	if (sci->sc_stage.flags & NILFS_CF_SUFREED) {
		ret = nilfs_sufile_cancel_freev(nilfs->ns_sufile,
						sci->sc_freesegs,
						sci->sc_nfreesegs,
						NULL);
		WARN_ON(ret); /* do not happen */
	}

	nilfs_destroy_logs(&logs);
}

static void nilfs_set_next_segment(struct the_nilfs *nilfs,
				   struct nilfs_segment_buffer *segbuf)
{
	nilfs->ns_segnum = segbuf->sb_segnum;
	nilfs->ns_nextnum = segbuf->sb_nextnum;
	nilfs->ns_pseg_offset = segbuf->sb_pseg_start - segbuf->sb_fseg_start
		+ segbuf->sb_sum.nblocks;
	nilfs->ns_seg_seq = segbuf->sb_sum.seg_seq;
	nilfs->ns_ctime = segbuf->sb_sum.ctime;
}

static void nilfs_segctor_complete_write(struct nilfs_sc_info *sci)
{
	struct nilfs_segment_buffer *segbuf;
	struct page *bd_page = NULL, *fs_page = NULL;
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	int update_sr = false;

	list_for_each_entry(segbuf, &sci->sc_write_logs, sb_list) {
		struct buffer_head *bh;

		list_for_each_entry(bh, &segbuf->sb_segsum_buffers,
				    b_assoc_buffers) {
			set_buffer_uptodate(bh);
			clear_buffer_dirty(bh);
			if (bh->b_page != bd_page) {
				if (bd_page)
					end_page_writeback(bd_page);
				bd_page = bh->b_page;
			}
		}
		/*
		 * We assume that the buffers which belong to the same page
		 * continue over the buffer list.
		 * Under this assumption, the last BHs of pages is
		 * identifiable by the discontinuity of bh->b_page
		 * (page != fs_page).
		 *
		 * For B-tree node blocks, however, this assumption is not
		 * guaranteed.  The cleanup code of B-tree node pages needs
		 * special care.
		 */
		list_for_each_entry(bh, &segbuf->sb_payload_buffers,
				    b_assoc_buffers) {
			set_buffer_uptodate(bh);
			clear_buffer_dirty(bh);
			clear_buffer_delay(bh);
			clear_buffer_nilfs_volatile(bh);
			clear_buffer_nilfs_redirected(bh);
			if (bh == segbuf->sb_super_root) {
				if (bh->b_page != bd_page) {
					end_page_writeback(bd_page);
					bd_page = bh->b_page;
				}
				update_sr = true;
				break;
			}
			if (bh->b_page != fs_page) {
				nilfs_end_page_io(fs_page, 0);
				fs_page = bh->b_page;
			}
		}

		if (!nilfs_segbuf_simplex(segbuf)) {
			if (segbuf->sb_sum.flags & NILFS_SS_LOGBGN) {
				set_bit(NILFS_SC_UNCLOSED, &sci->sc_flags);
				sci->sc_lseg_stime = jiffies;
			}
			if (segbuf->sb_sum.flags & NILFS_SS_LOGEND)
				clear_bit(NILFS_SC_UNCLOSED, &sci->sc_flags);
		}
	}
	/*
	 * Since pages may continue over multiple segment buffers,
	 * end of the last page must be checked outside of the loop.
	 */
	if (bd_page)
		end_page_writeback(bd_page);

	nilfs_end_page_io(fs_page, 0);

	nilfs_drop_collected_inodes(&sci->sc_dirty_files);

	if (nilfs_doing_gc())
		nilfs_drop_collected_inodes(&sci->sc_gc_inodes);
	else
		nilfs->ns_nongc_ctime = sci->sc_seg_ctime;

	sci->sc_nblk_inc += sci->sc_nblk_this_inc;

	segbuf = NILFS_LAST_SEGBUF(&sci->sc_write_logs);
	nilfs_set_next_segment(nilfs, segbuf);

	if (update_sr) {
		nilfs_set_last_segment(nilfs, segbuf->sb_pseg_start,
				       segbuf->sb_sum.seg_seq, nilfs->ns_cno++);

		clear_bit(NILFS_SC_HAVE_DELTA, &sci->sc_flags);
		clear_bit(NILFS_SC_DIRTY, &sci->sc_flags);
		set_bit(NILFS_SC_SUPER_ROOT, &sci->sc_flags);
		nilfs_segctor_clear_metadata_dirty(sci);
	} else
		clear_bit(NILFS_SC_SUPER_ROOT, &sci->sc_flags);
}

static int nilfs_segctor_wait(struct nilfs_sc_info *sci)
{
	int ret;

	ret = nilfs_wait_on_logs(&sci->sc_write_logs);
	if (!ret) {
		nilfs_segctor_complete_write(sci);
		nilfs_destroy_logs(&sci->sc_write_logs);
	}
	return ret;
}

static int nilfs_segctor_collect_dirty_files(struct nilfs_sc_info *sci,
					     struct the_nilfs *nilfs)
{
	struct nilfs_inode_info *ii, *n;
	struct inode *ifile = sci->sc_root->ifile;

	spin_lock(&nilfs->ns_inode_lock);
 retry:
	list_for_each_entry_safe(ii, n, &nilfs->ns_dirty_files, i_dirty) {
		if (!ii->i_bh) {
			struct buffer_head *ibh;
			int err;

			spin_unlock(&nilfs->ns_inode_lock);
			err = nilfs_ifile_get_inode_block(
				ifile, ii->vfs_inode.i_ino, &ibh);
			if (unlikely(err)) {
				nilfs_warning(sci->sc_super, __func__,
					      "failed to get inode block.\n");
				return err;
			}
			mark_buffer_dirty(ibh);
			nilfs_mdt_mark_dirty(ifile);
			spin_lock(&nilfs->ns_inode_lock);
			if (likely(!ii->i_bh))
				ii->i_bh = ibh;
			else
				brelse(ibh);
			goto retry;
		}

		clear_bit(NILFS_I_QUEUED, &ii->i_state);
		set_bit(NILFS_I_BUSY, &ii->i_state);
		list_move_tail(&ii->i_dirty, &sci->sc_dirty_files);
	}
	spin_unlock(&nilfs->ns_inode_lock);

	return 0;
}

static void nilfs_segctor_drop_written_files(struct nilfs_sc_info *sci,
					     struct the_nilfs *nilfs)
{
	struct nilfs_transaction_info *ti = current->journal_info;
	struct nilfs_inode_info *ii, *n;

	spin_lock(&nilfs->ns_inode_lock);
	list_for_each_entry_safe(ii, n, &sci->sc_dirty_files, i_dirty) {
		if (!test_and_clear_bit(NILFS_I_UPDATED, &ii->i_state) ||
		    test_bit(NILFS_I_DIRTY, &ii->i_state))
			continue;

		clear_bit(NILFS_I_BUSY, &ii->i_state);
		brelse(ii->i_bh);
		ii->i_bh = NULL;
		list_move_tail(&ii->i_dirty, &ti->ti_garbage);
	}
	spin_unlock(&nilfs->ns_inode_lock);
}

/*
 * Main procedure of segment constructor
 */
static int nilfs_segctor_do_construct(struct nilfs_sc_info *sci, int mode)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	int err;

	sci->sc_stage.scnt = NILFS_ST_INIT;
	sci->sc_cno = nilfs->ns_cno;

	err = nilfs_segctor_collect_dirty_files(sci, nilfs);
	if (unlikely(err))
		goto out;

	if (nilfs_test_metadata_dirty(nilfs, sci->sc_root))
		set_bit(NILFS_SC_DIRTY, &sci->sc_flags);

	if (nilfs_segctor_clean(sci))
		goto out;

	do {
		sci->sc_stage.flags &= ~NILFS_CF_HISTORY_MASK;

		err = nilfs_segctor_begin_construction(sci, nilfs);
		if (unlikely(err))
			goto out;

		/* Update time stamp */
		sci->sc_seg_ctime = get_seconds();

		err = nilfs_segctor_collect(sci, nilfs, mode);
		if (unlikely(err))
			goto failed;

		/* Avoid empty segment */
		if (sci->sc_stage.scnt == NILFS_ST_DONE &&
		    nilfs_segbuf_empty(sci->sc_curseg)) {
			nilfs_segctor_abort_construction(sci, nilfs, 1);
			goto out;
		}

		err = nilfs_segctor_assign(sci, mode);
		if (unlikely(err))
			goto failed;

		if (sci->sc_stage.flags & NILFS_CF_IFILE_STARTED)
			nilfs_segctor_fill_in_file_bmap(sci);

		if (mode == SC_LSEG_SR &&
		    sci->sc_stage.scnt >= NILFS_ST_CPFILE) {
			err = nilfs_segctor_fill_in_checkpoint(sci);
			if (unlikely(err))
				goto failed_to_write;

			nilfs_segctor_fill_in_super_root(sci, nilfs);
		}
		nilfs_segctor_update_segusage(sci, nilfs->ns_sufile);

		/* Write partial segments */
		nilfs_segctor_prepare_write(sci);

		nilfs_add_checksums_on_logs(&sci->sc_segbufs,
					    nilfs->ns_crc_seed);

		err = nilfs_segctor_write(sci, nilfs);
		if (unlikely(err))
			goto failed_to_write;

		if (sci->sc_stage.scnt == NILFS_ST_DONE ||
		    nilfs->ns_blocksize_bits != PAGE_CACHE_SHIFT) {
			/*
			 * At this point, we avoid double buffering
			 * for blocksize < pagesize because page dirty
			 * flag is turned off during write and dirty
			 * buffers are not properly collected for
			 * pages crossing over segments.
			 */
			err = nilfs_segctor_wait(sci);
			if (err)
				goto failed_to_write;
		}
	} while (sci->sc_stage.scnt != NILFS_ST_DONE);

 out:
	nilfs_segctor_drop_written_files(sci, nilfs);
	return err;

 failed_to_write:
	if (sci->sc_stage.flags & NILFS_CF_IFILE_STARTED)
		nilfs_redirty_inodes(&sci->sc_dirty_files);

 failed:
	if (nilfs_doing_gc())
		nilfs_redirty_inodes(&sci->sc_gc_inodes);
	nilfs_segctor_abort_construction(sci, nilfs, err);
	goto out;
}

/**
 * nilfs_segctor_start_timer - set timer of background write
 * @sci: nilfs_sc_info
 *
 * If the timer has already been set, it ignores the new request.
 * This function MUST be called within a section locking the segment
 * semaphore.
 */
static void nilfs_segctor_start_timer(struct nilfs_sc_info *sci)
{
	spin_lock(&sci->sc_state_lock);
	if (!(sci->sc_state & NILFS_SEGCTOR_COMMIT)) {
		sci->sc_timer.expires = jiffies + sci->sc_interval;
		add_timer(&sci->sc_timer);
		sci->sc_state |= NILFS_SEGCTOR_COMMIT;
	}
	spin_unlock(&sci->sc_state_lock);
}

static void nilfs_segctor_do_flush(struct nilfs_sc_info *sci, int bn)
{
	spin_lock(&sci->sc_state_lock);
	if (!(sci->sc_flush_request & (1 << bn))) {
		unsigned long prev_req = sci->sc_flush_request;

		sci->sc_flush_request |= (1 << bn);
		if (!prev_req)
			wake_up(&sci->sc_wait_daemon);
	}
	spin_unlock(&sci->sc_state_lock);
}

/**
 * nilfs_flush_segment - trigger a segment construction for resource control
 * @sb: super block
 * @ino: inode number of the file to be flushed out.
 */
void nilfs_flush_segment(struct super_block *sb, ino_t ino)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;

	if (!sci || nilfs_doing_construction())
		return;
	nilfs_segctor_do_flush(sci, NILFS_MDT_INODE(sb, ino) ? ino : 0);
					/* assign bit 0 to data files */
}

struct nilfs_segctor_wait_request {
	wait_queue_t	wq;
	__u32		seq;
	int		err;
	atomic_t	done;
};

static int nilfs_segctor_sync(struct nilfs_sc_info *sci)
{
	struct nilfs_segctor_wait_request wait_req;
	int err = 0;

	spin_lock(&sci->sc_state_lock);
	init_wait(&wait_req.wq);
	wait_req.err = 0;
	atomic_set(&wait_req.done, 0);
	wait_req.seq = ++sci->sc_seq_request;
	spin_unlock(&sci->sc_state_lock);

	init_waitqueue_entry(&wait_req.wq, current);
	add_wait_queue(&sci->sc_wait_request, &wait_req.wq);
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up(&sci->sc_wait_daemon);

	for (;;) {
		if (atomic_read(&wait_req.done)) {
			err = wait_req.err;
			break;
		}
		if (!signal_pending(current)) {
			schedule();
			continue;
		}
		err = -ERESTARTSYS;
		break;
	}
	finish_wait(&sci->sc_wait_request, &wait_req.wq);
	return err;
}

static void nilfs_segctor_wakeup(struct nilfs_sc_info *sci, int err)
{
	struct nilfs_segctor_wait_request *wrq, *n;
	unsigned long flags;

	spin_lock_irqsave(&sci->sc_wait_request.lock, flags);
	list_for_each_entry_safe(wrq, n, &sci->sc_wait_request.task_list,
				 wq.task_list) {
		if (!atomic_read(&wrq->done) &&
		    nilfs_cnt32_ge(sci->sc_seq_done, wrq->seq)) {
			wrq->err = err;
			atomic_set(&wrq->done, 1);
		}
		if (atomic_read(&wrq->done)) {
			wrq->wq.func(&wrq->wq,
				     TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE,
				     0, NULL);
		}
	}
	spin_unlock_irqrestore(&sci->sc_wait_request.lock, flags);
}

/**
 * nilfs_construct_segment - construct a logical segment
 * @sb: super block
 *
 * Return Value: On success, 0 is retured. On errors, one of the following
 * negative error code is returned.
 *
 * %-EROFS - Read only filesystem.
 *
 * %-EIO - I/O error
 *
 * %-ENOSPC - No space left on device (only in a panic state).
 *
 * %-ERESTARTSYS - Interrupted.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_construct_segment(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;
	struct nilfs_transaction_info *ti;
	int err;

	if (!sci)
		return -EROFS;

	/* A call inside transactions causes a deadlock. */
	BUG_ON((ti = current->journal_info) && ti->ti_magic == NILFS_TI_MAGIC);

	err = nilfs_segctor_sync(sci);
	return err;
}

/**
 * nilfs_construct_dsync_segment - construct a data-only logical segment
 * @sb: super block
 * @inode: inode whose data blocks should be written out
 * @start: start byte offset
 * @end: end byte offset (inclusive)
 *
 * Return Value: On success, 0 is retured. On errors, one of the following
 * negative error code is returned.
 *
 * %-EROFS - Read only filesystem.
 *
 * %-EIO - I/O error
 *
 * %-ENOSPC - No space left on device (only in a panic state).
 *
 * %-ERESTARTSYS - Interrupted.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_construct_dsync_segment(struct super_block *sb, struct inode *inode,
				  loff_t start, loff_t end)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;
	struct nilfs_inode_info *ii;
	struct nilfs_transaction_info ti;
	int err = 0;

	if (!sci)
		return -EROFS;

	nilfs_transaction_lock(sb, &ti, 0);

	ii = NILFS_I(inode);
	if (test_bit(NILFS_I_INODE_DIRTY, &ii->i_state) ||
	    nilfs_test_opt(nilfs, STRICT_ORDER) ||
	    test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags) ||
	    nilfs_discontinued(nilfs)) {
		nilfs_transaction_unlock(sb);
		err = nilfs_segctor_sync(sci);
		return err;
	}

	spin_lock(&nilfs->ns_inode_lock);
	if (!test_bit(NILFS_I_QUEUED, &ii->i_state) &&
	    !test_bit(NILFS_I_BUSY, &ii->i_state)) {
		spin_unlock(&nilfs->ns_inode_lock);
		nilfs_transaction_unlock(sb);
		return 0;
	}
	spin_unlock(&nilfs->ns_inode_lock);
	sci->sc_dsync_inode = ii;
	sci->sc_dsync_start = start;
	sci->sc_dsync_end = end;

	err = nilfs_segctor_do_construct(sci, SC_LSEG_DSYNC);

	nilfs_transaction_unlock(sb);
	return err;
}

#define FLUSH_FILE_BIT	(0x1) /* data file only */
#define FLUSH_DAT_BIT	(1 << NILFS_DAT_INO) /* DAT only */

/**
 * nilfs_segctor_accept - record accepted sequence count of log-write requests
 * @sci: segment constructor object
 */
static void nilfs_segctor_accept(struct nilfs_sc_info *sci)
{
	spin_lock(&sci->sc_state_lock);
	sci->sc_seq_accepted = sci->sc_seq_request;
	spin_unlock(&sci->sc_state_lock);
	del_timer_sync(&sci->sc_timer);
}

/**
 * nilfs_segctor_notify - notify the result of request to caller threads
 * @sci: segment constructor object
 * @mode: mode of log forming
 * @err: error code to be notified
 */
static void nilfs_segctor_notify(struct nilfs_sc_info *sci, int mode, int err)
{
	/* Clear requests (even when the construction failed) */
	spin_lock(&sci->sc_state_lock);

	if (mode == SC_LSEG_SR) {
		sci->sc_state &= ~NILFS_SEGCTOR_COMMIT;
		sci->sc_seq_done = sci->sc_seq_accepted;
		nilfs_segctor_wakeup(sci, err);
		sci->sc_flush_request = 0;
	} else {
		if (mode == SC_FLUSH_FILE)
			sci->sc_flush_request &= ~FLUSH_FILE_BIT;
		else if (mode == SC_FLUSH_DAT)
			sci->sc_flush_request &= ~FLUSH_DAT_BIT;

		/* re-enable timer if checkpoint creation was not done */
		if ((sci->sc_state & NILFS_SEGCTOR_COMMIT) &&
		    time_before(jiffies, sci->sc_timer.expires))
			add_timer(&sci->sc_timer);
	}
	spin_unlock(&sci->sc_state_lock);
}

/**
 * nilfs_segctor_construct - form logs and write them to disk
 * @sci: segment constructor object
 * @mode: mode of log forming
 */
static int nilfs_segctor_construct(struct nilfs_sc_info *sci, int mode)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	struct nilfs_super_block **sbp;
	int err = 0;

	nilfs_segctor_accept(sci);

	if (nilfs_discontinued(nilfs))
		mode = SC_LSEG_SR;
	if (!nilfs_segctor_confirm(sci))
		err = nilfs_segctor_do_construct(sci, mode);

	if (likely(!err)) {
		if (mode != SC_FLUSH_DAT)
			atomic_set(&nilfs->ns_ndirtyblks, 0);
		if (test_bit(NILFS_SC_SUPER_ROOT, &sci->sc_flags) &&
		    nilfs_discontinued(nilfs)) {
			down_write(&nilfs->ns_sem);
			err = -EIO;
			sbp = nilfs_prepare_super(sci->sc_super,
						  nilfs_sb_will_flip(nilfs));
			if (likely(sbp)) {
				nilfs_set_log_cursor(sbp[0], nilfs);
				err = nilfs_commit_super(sci->sc_super,
							 NILFS_SB_COMMIT);
			}
			up_write(&nilfs->ns_sem);
		}
	}

	nilfs_segctor_notify(sci, mode, err);
	return err;
}

static void nilfs_construction_timeout(unsigned long data)
{
	struct task_struct *p = (struct task_struct *)data;
	wake_up_process(p);
}

static void
nilfs_remove_written_gcinodes(struct the_nilfs *nilfs, struct list_head *head)
{
	struct nilfs_inode_info *ii, *n;

	list_for_each_entry_safe(ii, n, head, i_dirty) {
		if (!test_bit(NILFS_I_UPDATED, &ii->i_state))
			continue;
		list_del_init(&ii->i_dirty);
		truncate_inode_pages(&ii->vfs_inode.i_data, 0);
		nilfs_btnode_cache_clear(&ii->i_btnode_cache);
		iput(&ii->vfs_inode);
	}
}

int nilfs_clean_segments(struct super_block *sb, struct nilfs_argv *argv,
			 void **kbufs)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;
	struct nilfs_transaction_info ti;
	int err;

	if (unlikely(!sci))
		return -EROFS;

	nilfs_transaction_lock(sb, &ti, 1);

	err = nilfs_mdt_save_to_shadow_map(nilfs->ns_dat);
	if (unlikely(err))
		goto out_unlock;

	err = nilfs_ioctl_prepare_clean_segments(nilfs, argv, kbufs);
	if (unlikely(err)) {
		nilfs_mdt_restore_from_shadow_map(nilfs->ns_dat);
		goto out_unlock;
	}

	sci->sc_freesegs = kbufs[4];
	sci->sc_nfreesegs = argv[4].v_nmembs;
	list_splice_tail_init(&nilfs->ns_gc_inodes, &sci->sc_gc_inodes);

	for (;;) {
		err = nilfs_segctor_construct(sci, SC_LSEG_SR);
		nilfs_remove_written_gcinodes(nilfs, &sci->sc_gc_inodes);

		if (likely(!err))
			break;

		nilfs_warning(sb, __func__,
			      "segment construction failed. (err=%d)", err);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(sci->sc_interval);
	}
	if (nilfs_test_opt(nilfs, DISCARD)) {
		int ret = nilfs_discard_segments(nilfs, sci->sc_freesegs,
						 sci->sc_nfreesegs);
		if (ret) {
			printk(KERN_WARNING
			       "NILFS warning: error %d on discard request, "
			       "turning discards off for the device\n", ret);
			nilfs_clear_opt(nilfs, DISCARD);
		}
	}

 out_unlock:
	sci->sc_freesegs = NULL;
	sci->sc_nfreesegs = 0;
	nilfs_mdt_clear_shadow_map(nilfs->ns_dat);
	nilfs_transaction_unlock(sb);
	return err;
}

static void nilfs_segctor_thread_construct(struct nilfs_sc_info *sci, int mode)
{
	struct nilfs_transaction_info ti;

	nilfs_transaction_lock(sci->sc_super, &ti, 0);
	nilfs_segctor_construct(sci, mode);

	/*
	 * Unclosed segment should be retried.  We do this using sc_timer.
	 * Timeout of sc_timer will invoke complete construction which leads
	 * to close the current logical segment.
	 */
	if (test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags))
		nilfs_segctor_start_timer(sci);

	nilfs_transaction_unlock(sci->sc_super);
}

static void nilfs_segctor_do_immediate_flush(struct nilfs_sc_info *sci)
{
	int mode = 0;
	int err;

	spin_lock(&sci->sc_state_lock);
	mode = (sci->sc_flush_request & FLUSH_DAT_BIT) ?
		SC_FLUSH_DAT : SC_FLUSH_FILE;
	spin_unlock(&sci->sc_state_lock);

	if (mode) {
		err = nilfs_segctor_do_construct(sci, mode);

		spin_lock(&sci->sc_state_lock);
		sci->sc_flush_request &= (mode == SC_FLUSH_FILE) ?
			~FLUSH_FILE_BIT : ~FLUSH_DAT_BIT;
		spin_unlock(&sci->sc_state_lock);
	}
	clear_bit(NILFS_SC_PRIOR_FLUSH, &sci->sc_flags);
}

static int nilfs_segctor_flush_mode(struct nilfs_sc_info *sci)
{
	if (!test_bit(NILFS_SC_UNCLOSED, &sci->sc_flags) ||
	    time_before(jiffies, sci->sc_lseg_stime + sci->sc_mjcp_freq)) {
		if (!(sci->sc_flush_request & ~FLUSH_FILE_BIT))
			return SC_FLUSH_FILE;
		else if (!(sci->sc_flush_request & ~FLUSH_DAT_BIT))
			return SC_FLUSH_DAT;
	}
	return SC_LSEG_SR;
}

/**
 * nilfs_segctor_thread - main loop of the segment constructor thread.
 * @arg: pointer to a struct nilfs_sc_info.
 *
 * nilfs_segctor_thread() initializes a timer and serves as a daemon
 * to execute segment constructions.
 */
static int nilfs_segctor_thread(void *arg)
{
	struct nilfs_sc_info *sci = (struct nilfs_sc_info *)arg;
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	int timeout = 0;

	sci->sc_timer.data = (unsigned long)current;
	sci->sc_timer.function = nilfs_construction_timeout;

	/* start sync. */
	sci->sc_task = current;
	wake_up(&sci->sc_wait_task); /* for nilfs_segctor_start_thread() */
	printk(KERN_INFO
	       "segctord starting. Construction interval = %lu seconds, "
	       "CP frequency < %lu seconds\n",
	       sci->sc_interval / HZ, sci->sc_mjcp_freq / HZ);

	spin_lock(&sci->sc_state_lock);
 loop:
	for (;;) {
		int mode;

		if (sci->sc_state & NILFS_SEGCTOR_QUIT)
			goto end_thread;

		if (timeout || sci->sc_seq_request != sci->sc_seq_done)
			mode = SC_LSEG_SR;
		else if (!sci->sc_flush_request)
			break;
		else
			mode = nilfs_segctor_flush_mode(sci);

		spin_unlock(&sci->sc_state_lock);
		nilfs_segctor_thread_construct(sci, mode);
		spin_lock(&sci->sc_state_lock);
		timeout = 0;
	}


	if (freezing(current)) {
		spin_unlock(&sci->sc_state_lock);
		refrigerator();
		spin_lock(&sci->sc_state_lock);
	} else {
		DEFINE_WAIT(wait);
		int should_sleep = 1;

		prepare_to_wait(&sci->sc_wait_daemon, &wait,
				TASK_INTERRUPTIBLE);

		if (sci->sc_seq_request != sci->sc_seq_done)
			should_sleep = 0;
		else if (sci->sc_flush_request)
			should_sleep = 0;
		else if (sci->sc_state & NILFS_SEGCTOR_COMMIT)
			should_sleep = time_before(jiffies,
					sci->sc_timer.expires);

		if (should_sleep) {
			spin_unlock(&sci->sc_state_lock);
			schedule();
			spin_lock(&sci->sc_state_lock);
		}
		finish_wait(&sci->sc_wait_daemon, &wait);
		timeout = ((sci->sc_state & NILFS_SEGCTOR_COMMIT) &&
			   time_after_eq(jiffies, sci->sc_timer.expires));

		if (nilfs_sb_dirty(nilfs) && nilfs_sb_need_update(nilfs))
			set_nilfs_discontinued(nilfs);
	}
	goto loop;

 end_thread:
	spin_unlock(&sci->sc_state_lock);

	/* end sync. */
	sci->sc_task = NULL;
	wake_up(&sci->sc_wait_task); /* for nilfs_segctor_kill_thread() */
	return 0;
}

static int nilfs_segctor_start_thread(struct nilfs_sc_info *sci)
{
	struct task_struct *t;

	t = kthread_run(nilfs_segctor_thread, sci, "segctord");
	if (IS_ERR(t)) {
		int err = PTR_ERR(t);

		printk(KERN_ERR "NILFS: error %d creating segctord thread\n",
		       err);
		return err;
	}
	wait_event(sci->sc_wait_task, sci->sc_task != NULL);
	return 0;
}

static void nilfs_segctor_kill_thread(struct nilfs_sc_info *sci)
	__acquires(&sci->sc_state_lock)
	__releases(&sci->sc_state_lock)
{
	sci->sc_state |= NILFS_SEGCTOR_QUIT;

	while (sci->sc_task) {
		wake_up(&sci->sc_wait_daemon);
		spin_unlock(&sci->sc_state_lock);
		wait_event(sci->sc_wait_task, sci->sc_task == NULL);
		spin_lock(&sci->sc_state_lock);
	}
}

/*
 * Setup & clean-up functions
 */
static struct nilfs_sc_info *nilfs_segctor_new(struct super_block *sb,
					       struct nilfs_root *root)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci;

	sci = kzalloc(sizeof(*sci), GFP_KERNEL);
	if (!sci)
		return NULL;

	sci->sc_super = sb;

	nilfs_get_root(root);
	sci->sc_root = root;

	init_waitqueue_head(&sci->sc_wait_request);
	init_waitqueue_head(&sci->sc_wait_daemon);
	init_waitqueue_head(&sci->sc_wait_task);
	spin_lock_init(&sci->sc_state_lock);
	INIT_LIST_HEAD(&sci->sc_dirty_files);
	INIT_LIST_HEAD(&sci->sc_segbufs);
	INIT_LIST_HEAD(&sci->sc_write_logs);
	INIT_LIST_HEAD(&sci->sc_gc_inodes);
	init_timer(&sci->sc_timer);

	sci->sc_interval = HZ * NILFS_SC_DEFAULT_TIMEOUT;
	sci->sc_mjcp_freq = HZ * NILFS_SC_DEFAULT_SR_FREQ;
	sci->sc_watermark = NILFS_SC_DEFAULT_WATERMARK;

	if (nilfs->ns_interval)
		sci->sc_interval = HZ * nilfs->ns_interval;
	if (nilfs->ns_watermark)
		sci->sc_watermark = nilfs->ns_watermark;
	return sci;
}

static void nilfs_segctor_write_out(struct nilfs_sc_info *sci)
{
	int ret, retrycount = NILFS_SC_CLEANUP_RETRY;

	/* The segctord thread was stopped and its timer was removed.
	   But some tasks remain. */
	do {
		struct nilfs_transaction_info ti;

		nilfs_transaction_lock(sci->sc_super, &ti, 0);
		ret = nilfs_segctor_construct(sci, SC_LSEG_SR);
		nilfs_transaction_unlock(sci->sc_super);

	} while (ret && retrycount-- > 0);
}

/**
 * nilfs_segctor_destroy - destroy the segment constructor.
 * @sci: nilfs_sc_info
 *
 * nilfs_segctor_destroy() kills the segctord thread and frees
 * the nilfs_sc_info struct.
 * Caller must hold the segment semaphore.
 */
static void nilfs_segctor_destroy(struct nilfs_sc_info *sci)
{
	struct the_nilfs *nilfs = sci->sc_super->s_fs_info;
	int flag;

	up_write(&nilfs->ns_segctor_sem);

	spin_lock(&sci->sc_state_lock);
	nilfs_segctor_kill_thread(sci);
	flag = ((sci->sc_state & NILFS_SEGCTOR_COMMIT) || sci->sc_flush_request
		|| sci->sc_seq_request != sci->sc_seq_done);
	spin_unlock(&sci->sc_state_lock);

	if (flag || !nilfs_segctor_confirm(sci))
		nilfs_segctor_write_out(sci);

	if (!list_empty(&sci->sc_dirty_files)) {
		nilfs_warning(sci->sc_super, __func__,
			      "dirty file(s) after the final construction\n");
		nilfs_dispose_list(nilfs, &sci->sc_dirty_files, 1);
	}

	WARN_ON(!list_empty(&sci->sc_segbufs));
	WARN_ON(!list_empty(&sci->sc_write_logs));

	nilfs_put_root(sci->sc_root);

	down_write(&nilfs->ns_segctor_sem);

	del_timer_sync(&sci->sc_timer);
	kfree(sci);
}

/**
 * nilfs_attach_log_writer - attach log writer
 * @sb: super block instance
 * @root: root object of the current filesystem tree
 *
 * This allocates a log writer object, initializes it, and starts the
 * log writer.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_attach_log_writer(struct super_block *sb, struct nilfs_root *root)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	int err;

	if (nilfs->ns_writer) {
		/*
		 * This happens if the filesystem was remounted
		 * read/write after nilfs_error degenerated it into a
		 * read-only mount.
		 */
		nilfs_detach_log_writer(sb);
	}

	nilfs->ns_writer = nilfs_segctor_new(sb, root);
	if (!nilfs->ns_writer)
		return -ENOMEM;

	err = nilfs_segctor_start_thread(nilfs->ns_writer);
	if (err) {
		kfree(nilfs->ns_writer);
		nilfs->ns_writer = NULL;
	}
	return err;
}

/**
 * nilfs_detach_log_writer - destroy log writer
 * @sb: super block instance
 *
 * This kills log writer daemon, frees the log writer object, and
 * destroys list of dirty files.
 */
void nilfs_detach_log_writer(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	LIST_HEAD(garbage_list);

	down_write(&nilfs->ns_segctor_sem);
	if (nilfs->ns_writer) {
		nilfs_segctor_destroy(nilfs->ns_writer);
		nilfs->ns_writer = NULL;
	}

	/* Force to free the list of dirty files */
	spin_lock(&nilfs->ns_inode_lock);
	if (!list_empty(&nilfs->ns_dirty_files)) {
		list_splice_init(&nilfs->ns_dirty_files, &garbage_list);
		nilfs_warning(sb, __func__,
			      "Hit dirty file after stopped log writer\n");
	}
	spin_unlock(&nilfs->ns_inode_lock);
	up_write(&nilfs->ns_segctor_sem);

	nilfs_dispose_list(nilfs, &garbage_list, 1);
}
