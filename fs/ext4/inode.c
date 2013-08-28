/*
 *  linux/fs/ext4/inode.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 *	(jj@sunsite.ms.mff.cuni.cz)
 *
 *  Assorted race fixes, rewrite of ext4_get_block() by Al Viro, 2000
 */

#include <linux/fs.h>
#include <linux/time.h>
#include <linux/jbd2.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/uio.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/aio.h>
#include <linux/bitops.h>

#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "truncate.h"

#include <trace/events/ext4.h>

#define MPAGE_DA_EXTENT_TAIL 0x01

static __u32 ext4_inode_csum(struct inode *inode, struct ext4_inode *raw,
			      struct ext4_inode_info *ei)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	__u16 csum_lo;
	__u16 csum_hi = 0;
	__u32 csum;

	csum_lo = le16_to_cpu(raw->i_checksum_lo);
	raw->i_checksum_lo = 0;
	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE &&
	    EXT4_FITS_IN_INODE(raw, ei, i_checksum_hi)) {
		csum_hi = le16_to_cpu(raw->i_checksum_hi);
		raw->i_checksum_hi = 0;
	}

	csum = ext4_chksum(sbi, ei->i_csum_seed, (__u8 *)raw,
			   EXT4_INODE_SIZE(inode->i_sb));

	raw->i_checksum_lo = cpu_to_le16(csum_lo);
	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE &&
	    EXT4_FITS_IN_INODE(raw, ei, i_checksum_hi))
		raw->i_checksum_hi = cpu_to_le16(csum_hi);

	return csum;
}

static int ext4_inode_csum_verify(struct inode *inode, struct ext4_inode *raw,
				  struct ext4_inode_info *ei)
{
	__u32 provided, calculated;

	if (EXT4_SB(inode->i_sb)->s_es->s_creator_os !=
	    cpu_to_le32(EXT4_OS_LINUX) ||
	    !EXT4_HAS_RO_COMPAT_FEATURE(inode->i_sb,
		EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		return 1;

	provided = le16_to_cpu(raw->i_checksum_lo);
	calculated = ext4_inode_csum(inode, raw, ei);
	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE &&
	    EXT4_FITS_IN_INODE(raw, ei, i_checksum_hi))
		provided |= ((__u32)le16_to_cpu(raw->i_checksum_hi)) << 16;
	else
		calculated &= 0xFFFF;

	return provided == calculated;
}

static void ext4_inode_csum_set(struct inode *inode, struct ext4_inode *raw,
				struct ext4_inode_info *ei)
{
	__u32 csum;

	if (EXT4_SB(inode->i_sb)->s_es->s_creator_os !=
	    cpu_to_le32(EXT4_OS_LINUX) ||
	    !EXT4_HAS_RO_COMPAT_FEATURE(inode->i_sb,
		EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		return;

	csum = ext4_inode_csum(inode, raw, ei);
	raw->i_checksum_lo = cpu_to_le16(csum & 0xFFFF);
	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE &&
	    EXT4_FITS_IN_INODE(raw, ei, i_checksum_hi))
		raw->i_checksum_hi = cpu_to_le16(csum >> 16);
}

static inline int ext4_begin_ordered_truncate(struct inode *inode,
					      loff_t new_size)
{
	trace_ext4_begin_ordered_truncate(inode, new_size);
	/*
	 * If jinode is zero, then we never opened the file for
	 * writing, so there's no need to call
	 * jbd2_journal_begin_ordered_truncate() since there's no
	 * outstanding writes we need to flush.
	 */
	if (!EXT4_I(inode)->jinode)
		return 0;
	return jbd2_journal_begin_ordered_truncate(EXT4_JOURNAL(inode),
						   EXT4_I(inode)->jinode,
						   new_size);
}

static void ext4_invalidatepage(struct page *page, unsigned long offset);
static int __ext4_journalled_writepage(struct page *page, unsigned int len);
static int ext4_bh_delay_or_unwritten(handle_t *handle, struct buffer_head *bh);
static int ext4_discard_partial_page_buffers_no_lock(handle_t *handle,
		struct inode *inode, struct page *page, loff_t from,
		loff_t length, int flags);

/*
 * Test whether an inode is a fast symlink.
 */
static int ext4_inode_is_fast_symlink(struct inode *inode)
{
	int ea_blocks = EXT4_I(inode)->i_file_acl ?
		(inode->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(inode->i_mode) && inode->i_blocks - ea_blocks == 0);
}

/*
 * Restart the transaction associated with *handle.  This does a commit,
 * so before we call here everything must be consistently dirtied against
 * this transaction.
 */
int ext4_truncate_restart_trans(handle_t *handle, struct inode *inode,
				 int nblocks)
{
	int ret;

	/*
	 * Drop i_data_sem to avoid deadlock with ext4_map_blocks.  At this
	 * moment, get_block can be called only for blocks inside i_size since
	 * page cache has been already dropped and writes are blocked by
	 * i_mutex. So we can safely drop the i_data_sem here.
	 */
	BUG_ON(EXT4_JOURNAL(inode) == NULL);
	jbd_debug(2, "restarting handle %p\n", handle);
	up_write(&EXT4_I(inode)->i_data_sem);
	ret = ext4_journal_restart(handle, nblocks);
	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_discard_preallocations(inode);

	return ret;
}

/*
 * Called at the last iput() if i_nlink is zero.
 */
void ext4_evict_inode(struct inode *inode)
{
	handle_t *handle;
	int err;

	trace_ext4_evict_inode(inode);

	if (inode->i_nlink) {
		/*
		 * When journalling data dirty buffers are tracked only in the
		 * journal. So although mm thinks everything is clean and
		 * ready for reaping the inode might still have some pages to
		 * write in the running transaction or waiting to be
		 * checkpointed. Thus calling jbd2_journal_invalidatepage()
		 * (via truncate_inode_pages()) to discard these buffers can
		 * cause data loss. Also even if we did not discard these
		 * buffers, we would have no way to find them after the inode
		 * is reaped and thus user could see stale data if he tries to
		 * read them before the transaction is checkpointed. So be
		 * careful and force everything to disk here... We use
		 * ei->i_datasync_tid to store the newest transaction
		 * containing inode's data.
		 *
		 * Note that directories do not have this problem because they
		 * don't use page cache.
		 */
		if (ext4_should_journal_data(inode) &&
		    (S_ISLNK(inode->i_mode) || S_ISREG(inode->i_mode)) &&
		    inode->i_ino != EXT4_JOURNAL_INO) {
			journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
			tid_t commit_tid = EXT4_I(inode)->i_datasync_tid;

			jbd2_complete_transaction(journal, commit_tid);
			filemap_write_and_wait(&inode->i_data);
		}
		truncate_inode_pages(&inode->i_data, 0);
		ext4_ioend_shutdown(inode);
		goto no_delete;
	}

	if (!is_bad_inode(inode))
		dquot_initialize(inode);

	if (ext4_should_order_data(inode))
		ext4_begin_ordered_truncate(inode, 0);
	truncate_inode_pages(&inode->i_data, 0);
	ext4_ioend_shutdown(inode);

	if (is_bad_inode(inode))
		goto no_delete;

	/*
	 * Protect us against freezing - iput() caller didn't have to have any
	 * protection against it
	 */
	sb_start_intwrite(inode->i_sb);
	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE,
				    ext4_blocks_for_truncate(inode)+3);
	if (IS_ERR(handle)) {
		ext4_std_error(inode->i_sb, PTR_ERR(handle));
		/*
		 * If we're going to skip the normal cleanup, we still need to
		 * make sure that the in-core orphan linked list is properly
		 * cleaned up.
		 */
		ext4_orphan_del(NULL, inode);
		sb_end_intwrite(inode->i_sb);
		goto no_delete;
	}

	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
	inode->i_size = 0;
	err = ext4_mark_inode_dirty(handle, inode);
	if (err) {
		ext4_warning(inode->i_sb,
			     "couldn't mark inode dirty (err %d)", err);
		goto stop_handle;
	}
	if (inode->i_blocks)
		ext4_truncate(inode);

	/*
	 * ext4_ext_truncate() doesn't reserve any slop when it
	 * restarts journal transactions; therefore there may not be
	 * enough credits left in the handle to remove the inode from
	 * the orphan list and set the dtime field.
	 */
	if (!ext4_handle_has_enough_credits(handle, 3)) {
		err = ext4_journal_extend(handle, 3);
		if (err > 0)
			err = ext4_journal_restart(handle, 3);
		if (err != 0) {
			ext4_warning(inode->i_sb,
				     "couldn't extend journal (err %d)", err);
		stop_handle:
			ext4_journal_stop(handle);
			ext4_orphan_del(NULL, inode);
			sb_end_intwrite(inode->i_sb);
			goto no_delete;
		}
	}

	/*
	 * Kill off the orphan record which ext4_truncate created.
	 * AKPM: I think this can be inside the above `if'.
	 * Note that ext4_orphan_del() has to be able to cope with the
	 * deletion of a non-existent orphan - this is because we don't
	 * know if ext4_truncate() actually created an orphan record.
	 * (Well, we could do this if we need to, but heck - it works)
	 */
	ext4_orphan_del(handle, inode);
	EXT4_I(inode)->i_dtime	= get_seconds();

	/*
	 * One subtle ordering requirement: if anything has gone wrong
	 * (transaction abort, IO errors, whatever), then we can still
	 * do these next steps (the fs will already have been marked as
	 * having errors), but we can't free the inode if the mark_dirty
	 * fails.
	 */
	if (ext4_mark_inode_dirty(handle, inode))
		/* If that failed, just do the required in-core inode clear. */
		ext4_clear_inode(inode);
	else
		ext4_free_inode(handle, inode);
	ext4_journal_stop(handle);
	sb_end_intwrite(inode->i_sb);
	return;
no_delete:
	ext4_clear_inode(inode);	/* We must guarantee clearing of inode... */
}

#ifdef CONFIG_QUOTA
qsize_t *ext4_get_reserved_space(struct inode *inode)
{
	return &EXT4_I(inode)->i_reserved_quota;
}
#endif

/*
 * Calculate the number of metadata blocks need to reserve
 * to allocate a block located at @lblock
 */
static int ext4_calc_metadata_amount(struct inode *inode, ext4_lblk_t lblock)
{
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		return ext4_ext_calc_metadata_amount(inode, lblock);

	return ext4_ind_calc_metadata_amount(inode, lblock);
}

/*
 * Called with i_data_sem down, which is important since we can call
 * ext4_discard_preallocations() from here.
 */
void ext4_da_update_reserve_space(struct inode *inode,
					int used, int quota_claim)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);

	spin_lock(&ei->i_block_reservation_lock);
	trace_ext4_da_update_reserve_space(inode, used, quota_claim);
	if (unlikely(used > ei->i_reserved_data_blocks)) {
		ext4_warning(inode->i_sb, "%s: ino %lu, used %d "
			 "with only %d reserved data blocks",
			 __func__, inode->i_ino, used,
			 ei->i_reserved_data_blocks);
		WARN_ON(1);
		used = ei->i_reserved_data_blocks;
	}

	if (unlikely(ei->i_allocated_meta_blocks > ei->i_reserved_meta_blocks)) {
		ext4_warning(inode->i_sb, "ino %lu, allocated %d "
			"with only %d reserved metadata blocks "
			"(releasing %d blocks with reserved %d data blocks)",
			inode->i_ino, ei->i_allocated_meta_blocks,
			     ei->i_reserved_meta_blocks, used,
			     ei->i_reserved_data_blocks);
		WARN_ON(1);
		ei->i_allocated_meta_blocks = ei->i_reserved_meta_blocks;
	}

	/* Update per-inode reservations */
	ei->i_reserved_data_blocks -= used;
	ei->i_reserved_meta_blocks -= ei->i_allocated_meta_blocks;
	percpu_counter_sub(&sbi->s_dirtyclusters_counter,
			   used + ei->i_allocated_meta_blocks);
	ei->i_allocated_meta_blocks = 0;

	if (ei->i_reserved_data_blocks == 0) {
		/*
		 * We can release all of the reserved metadata blocks
		 * only when we have written all of the delayed
		 * allocation blocks.
		 */
		percpu_counter_sub(&sbi->s_dirtyclusters_counter,
				   ei->i_reserved_meta_blocks);
		ei->i_reserved_meta_blocks = 0;
		ei->i_da_metadata_calc_len = 0;
	}
	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	/* Update quota subsystem for data blocks */
	if (quota_claim)
		dquot_claim_block(inode, EXT4_C2B(sbi, used));
	else {
		/*
		 * We did fallocate with an offset that is already delayed
		 * allocated. So on delayed allocated writeback we should
		 * not re-claim the quota for fallocated blocks.
		 */
		dquot_release_reservation_block(inode, EXT4_C2B(sbi, used));
	}

	/*
	 * If we have done all the pending block allocations and if
	 * there aren't any writers on the inode, we can discard the
	 * inode's preallocations.
	 */
	if ((ei->i_reserved_data_blocks == 0) &&
	    (atomic_read(&inode->i_writecount) == 0))
		ext4_discard_preallocations(inode);
}

static int __check_block_validity(struct inode *inode, const char *func,
				unsigned int line,
				struct ext4_map_blocks *map)
{
	if (!ext4_data_block_valid(EXT4_SB(inode->i_sb), map->m_pblk,
				   map->m_len)) {
		ext4_error_inode(inode, func, line, map->m_pblk,
				 "lblock %lu mapped to illegal pblock "
				 "(length %d)", (unsigned long) map->m_lblk,
				 map->m_len);
		return -EIO;
	}
	return 0;
}

#define check_block_validity(inode, map)	\
	__check_block_validity((inode), __func__, __LINE__, (map))

/*
 * Return the number of contiguous dirty pages in a given inode
 * starting at page frame idx.
 */
static pgoff_t ext4_num_dirty_pages(struct inode *inode, pgoff_t idx,
				    unsigned int max_pages)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t	index;
	struct pagevec pvec;
	pgoff_t num = 0;
	int i, nr_pages, done = 0;

	if (max_pages == 0)
		return 0;
	pagevec_init(&pvec, 0);
	while (!done) {
		index = idx;
		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
					      PAGECACHE_TAG_DIRTY,
					      (pgoff_t)PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			struct buffer_head *bh, *head;

			lock_page(page);
			if (unlikely(page->mapping != mapping) ||
			    !PageDirty(page) ||
			    PageWriteback(page) ||
			    page->index != idx) {
				done = 1;
				unlock_page(page);
				break;
			}
			if (page_has_buffers(page)) {
				bh = head = page_buffers(page);
				do {
					if (!buffer_delay(bh) &&
					    !buffer_unwritten(bh))
						done = 1;
					bh = bh->b_this_page;
				} while (!done && (bh != head));
			}
			unlock_page(page);
			if (done)
				break;
			idx++;
			num++;
			if (num >= max_pages) {
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
	}
	return num;
}

#ifdef ES_AGGRESSIVE_TEST
static void ext4_map_blocks_es_recheck(handle_t *handle,
				       struct inode *inode,
				       struct ext4_map_blocks *es_map,
				       struct ext4_map_blocks *map,
				       int flags)
{
	int retval;

	map->m_flags = 0;
	/*
	 * There is a race window that the result is not the same.
	 * e.g. xfstests #223 when dioread_nolock enables.  The reason
	 * is that we lookup a block mapping in extent status tree with
	 * out taking i_data_sem.  So at the time the unwritten extent
	 * could be converted.
	 */
	if (!(flags & EXT4_GET_BLOCKS_NO_LOCK))
		down_read((&EXT4_I(inode)->i_data_sem));
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		retval = ext4_ext_map_blocks(handle, inode, map, flags &
					     EXT4_GET_BLOCKS_KEEP_SIZE);
	} else {
		retval = ext4_ind_map_blocks(handle, inode, map, flags &
					     EXT4_GET_BLOCKS_KEEP_SIZE);
	}
	if (!(flags & EXT4_GET_BLOCKS_NO_LOCK))
		up_read((&EXT4_I(inode)->i_data_sem));
	/*
	 * Clear EXT4_MAP_FROM_CLUSTER and EXT4_MAP_BOUNDARY flag
	 * because it shouldn't be marked in es_map->m_flags.
	 */
	map->m_flags &= ~(EXT4_MAP_FROM_CLUSTER | EXT4_MAP_BOUNDARY);

	/*
	 * We don't check m_len because extent will be collpased in status
	 * tree.  So the m_len might not equal.
	 */
	if (es_map->m_lblk != map->m_lblk ||
	    es_map->m_flags != map->m_flags ||
	    es_map->m_pblk != map->m_pblk) {
		printk("ES cache assertation failed for inode: %lu "
		       "es_cached ex [%d/%d/%llu/%x] != "
		       "found ex [%d/%d/%llu/%x] retval %d flags %x\n",
		       inode->i_ino, es_map->m_lblk, es_map->m_len,
		       es_map->m_pblk, es_map->m_flags, map->m_lblk,
		       map->m_len, map->m_pblk, map->m_flags,
		       retval, flags);
	}
}
#endif /* ES_AGGRESSIVE_TEST */

/*
 * The ext4_map_blocks() function tries to look up the requested blocks,
 * and returns if the blocks are already mapped.
 *
 * Otherwise it takes the write lock of the i_data_sem and allocate blocks
 * and store the allocated blocks in the result buffer head and mark it
 * mapped.
 *
 * If file type is extents based, it will call ext4_ext_map_blocks(),
 * Otherwise, call with ext4_ind_map_blocks() to handle indirect mapping
 * based files
 *
 * On success, it returns the number of blocks being mapped or allocate.
 * if create==0 and the blocks are pre-allocated and uninitialized block,
 * the result buffer head is unmapped. If the create ==1, it will make sure
 * the buffer head is mapped.
 *
 * It returns 0 if plain look up failed (blocks have not been allocated), in
 * that case, buffer head is unmapped
 *
 * It returns the error in case of allocation failure.
 */
int ext4_map_blocks(handle_t *handle, struct inode *inode,
		    struct ext4_map_blocks *map, int flags)
{
	struct extent_status es;
	int retval;
#ifdef ES_AGGRESSIVE_TEST
	struct ext4_map_blocks orig_map;

	memcpy(&orig_map, map, sizeof(*map));
#endif

	map->m_flags = 0;
	ext_debug("ext4_map_blocks(): inode %lu, flag %d, max_blocks %u,"
		  "logical block %lu\n", inode->i_ino, flags, map->m_len,
		  (unsigned long) map->m_lblk);

	/* Lookup extent status tree firstly */
	if (ext4_es_lookup_extent(inode, map->m_lblk, &es)) {
		if (ext4_es_is_written(&es) || ext4_es_is_unwritten(&es)) {
			map->m_pblk = ext4_es_pblock(&es) +
					map->m_lblk - es.es_lblk;
			map->m_flags |= ext4_es_is_written(&es) ?
					EXT4_MAP_MAPPED : EXT4_MAP_UNWRITTEN;
			retval = es.es_len - (map->m_lblk - es.es_lblk);
			if (retval > map->m_len)
				retval = map->m_len;
			map->m_len = retval;
		} else if (ext4_es_is_delayed(&es) || ext4_es_is_hole(&es)) {
			retval = 0;
		} else {
			BUG_ON(1);
		}
#ifdef ES_AGGRESSIVE_TEST
		ext4_map_blocks_es_recheck(handle, inode, map,
					   &orig_map, flags);
#endif
		goto found;
	}

	/*
	 * Try to see if we can get the block without requesting a new
	 * file system block.
	 */
	if (!(flags & EXT4_GET_BLOCKS_NO_LOCK))
		down_read((&EXT4_I(inode)->i_data_sem));
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		retval = ext4_ext_map_blocks(handle, inode, map, flags &
					     EXT4_GET_BLOCKS_KEEP_SIZE);
	} else {
		retval = ext4_ind_map_blocks(handle, inode, map, flags &
					     EXT4_GET_BLOCKS_KEEP_SIZE);
	}
	if (retval > 0) {
		int ret;
		unsigned long long status;

#ifdef ES_AGGRESSIVE_TEST
		if (retval != map->m_len) {
			printk("ES len assertation failed for inode: %lu "
			       "retval %d != map->m_len %d "
			       "in %s (lookup)\n", inode->i_ino, retval,
			       map->m_len, __func__);
		}
#endif

		status = map->m_flags & EXT4_MAP_UNWRITTEN ?
				EXTENT_STATUS_UNWRITTEN : EXTENT_STATUS_WRITTEN;
		if (!(flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE) &&
		    !(status & EXTENT_STATUS_WRITTEN) &&
		    ext4_find_delalloc_range(inode, map->m_lblk,
					     map->m_lblk + map->m_len - 1))
			status |= EXTENT_STATUS_DELAYED;
		ret = ext4_es_insert_extent(inode, map->m_lblk,
					    map->m_len, map->m_pblk, status);
		if (ret < 0)
			retval = ret;
	}
	if (!(flags & EXT4_GET_BLOCKS_NO_LOCK))
		up_read((&EXT4_I(inode)->i_data_sem));

found:
	if (retval > 0 && map->m_flags & EXT4_MAP_MAPPED) {
		int ret = check_block_validity(inode, map);
		if (ret != 0)
			return ret;
	}

	/* If it is only a block(s) look up */
	if ((flags & EXT4_GET_BLOCKS_CREATE) == 0)
		return retval;

	/*
	 * Returns if the blocks have already allocated
	 *
	 * Note that if blocks have been preallocated
	 * ext4_ext_get_block() returns the create = 0
	 * with buffer head unmapped.
	 */
	if (retval > 0 && map->m_flags & EXT4_MAP_MAPPED)
		return retval;

	/*
	 * Here we clear m_flags because after allocating an new extent,
	 * it will be set again.
	 */
	map->m_flags &= ~EXT4_MAP_FLAGS;

	/*
	 * New blocks allocate and/or writing to uninitialized extent
	 * will possibly result in updating i_data, so we take
	 * the write lock of i_data_sem, and call get_blocks()
	 * with create == 1 flag.
	 */
	down_write((&EXT4_I(inode)->i_data_sem));

	/*
	 * if the caller is from delayed allocation writeout path
	 * we have already reserved fs blocks for allocation
	 * let the underlying get_block() function know to
	 * avoid double accounting
	 */
	if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		ext4_set_inode_state(inode, EXT4_STATE_DELALLOC_RESERVED);
	/*
	 * We need to check for EXT4 here because migrate
	 * could have changed the inode type in between
	 */
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
		retval = ext4_ext_map_blocks(handle, inode, map, flags);
	} else {
		retval = ext4_ind_map_blocks(handle, inode, map, flags);

		if (retval > 0 && map->m_flags & EXT4_MAP_NEW) {
			/*
			 * We allocated new blocks which will result in
			 * i_data's format changing.  Force the migrate
			 * to fail by clearing migrate flags
			 */
			ext4_clear_inode_state(inode, EXT4_STATE_EXT_MIGRATE);
		}

		/*
		 * Update reserved blocks/metadata blocks after successful
		 * block allocation which had been deferred till now. We don't
		 * support fallocate for non extent files. So we can update
		 * reserve space here.
		 */
		if ((retval > 0) &&
			(flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE))
			ext4_da_update_reserve_space(inode, retval, 1);
	}
	if (flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE)
		ext4_clear_inode_state(inode, EXT4_STATE_DELALLOC_RESERVED);

	if (retval > 0) {
		int ret;
		unsigned long long status;

#ifdef ES_AGGRESSIVE_TEST
		if (retval != map->m_len) {
			printk("ES len assertation failed for inode: %lu "
			       "retval %d != map->m_len %d "
			       "in %s (allocation)\n", inode->i_ino, retval,
			       map->m_len, __func__);
		}
#endif

		/*
		 * If the extent has been zeroed out, we don't need to update
		 * extent status tree.
		 */
		if ((flags & EXT4_GET_BLOCKS_PRE_IO) &&
		    ext4_es_lookup_extent(inode, map->m_lblk, &es)) {
			if (ext4_es_is_written(&es))
				goto has_zeroout;
		}
		status = map->m_flags & EXT4_MAP_UNWRITTEN ?
				EXTENT_STATUS_UNWRITTEN : EXTENT_STATUS_WRITTEN;
		if (!(flags & EXT4_GET_BLOCKS_DELALLOC_RESERVE) &&
		    !(status & EXTENT_STATUS_WRITTEN) &&
		    ext4_find_delalloc_range(inode, map->m_lblk,
					     map->m_lblk + map->m_len - 1))
			status |= EXTENT_STATUS_DELAYED;
		ret = ext4_es_insert_extent(inode, map->m_lblk, map->m_len,
					    map->m_pblk, status);
		if (ret < 0)
			retval = ret;
	}

has_zeroout:
	up_write((&EXT4_I(inode)->i_data_sem));
	if (retval > 0 && map->m_flags & EXT4_MAP_MAPPED) {
		int ret = check_block_validity(inode, map);
		if (ret != 0)
			return ret;
	}
	return retval;
}

/* Maximum number of blocks we map for direct IO at once. */
#define DIO_MAX_BLOCKS 4096

static int _ext4_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int flags)
{
	handle_t *handle = ext4_journal_current_handle();
	struct ext4_map_blocks map;
	int ret = 0, started = 0;
	int dio_credits;

	if (ext4_has_inline_data(inode))
		return -ERANGE;

	map.m_lblk = iblock;
	map.m_len = bh->b_size >> inode->i_blkbits;

	if (flags && !(flags & EXT4_GET_BLOCKS_NO_LOCK) && !handle) {
		/* Direct IO write... */
		if (map.m_len > DIO_MAX_BLOCKS)
			map.m_len = DIO_MAX_BLOCKS;
		dio_credits = ext4_chunk_trans_blocks(inode, map.m_len);
		handle = ext4_journal_start(inode, EXT4_HT_MAP_BLOCKS,
					    dio_credits);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			return ret;
		}
		started = 1;
	}

	ret = ext4_map_blocks(handle, inode, &map, flags);
	if (ret > 0) {
		map_bh(bh, inode->i_sb, map.m_pblk);
		bh->b_state = (bh->b_state & ~EXT4_MAP_FLAGS) | map.m_flags;
		bh->b_size = inode->i_sb->s_blocksize * map.m_len;
		ret = 0;
	}
	if (started)
		ext4_journal_stop(handle);
	return ret;
}

int ext4_get_block(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh, int create)
{
	return _ext4_get_block(inode, iblock, bh,
			       create ? EXT4_GET_BLOCKS_CREATE : 0);
}

/*
 * `handle' can be NULL if create is zero
 */
struct buffer_head *ext4_getblk(handle_t *handle, struct inode *inode,
				ext4_lblk_t block, int create, int *errp)
{
	struct ext4_map_blocks map;
	struct buffer_head *bh;
	int fatal = 0, err;

	J_ASSERT(handle != NULL || create == 0);

	map.m_lblk = block;
	map.m_len = 1;
	err = ext4_map_blocks(handle, inode, &map,
			      create ? EXT4_GET_BLOCKS_CREATE : 0);

	/* ensure we send some value back into *errp */
	*errp = 0;

	if (create && err == 0)
		err = -ENOSPC;	/* should never happen */
	if (err < 0)
		*errp = err;
	if (err <= 0)
		return NULL;

	bh = sb_getblk(inode->i_sb, map.m_pblk);
	if (unlikely(!bh)) {
		*errp = -ENOMEM;
		return NULL;
	}
	if (map.m_flags & EXT4_MAP_NEW) {
		J_ASSERT(create != 0);
		J_ASSERT(handle != NULL);

		/*
		 * Now that we do not always journal data, we should
		 * keep in mind whether this should always journal the
		 * new buffer as metadata.  For now, regular file
		 * writes use ext4_get_block instead, so it's not a
		 * problem.
		 */
		lock_buffer(bh);
		BUFFER_TRACE(bh, "call get_create_access");
		fatal = ext4_journal_get_create_access(handle, bh);
		if (!fatal && !buffer_uptodate(bh)) {
			memset(bh->b_data, 0, inode->i_sb->s_blocksize);
			set_buffer_uptodate(bh);
		}
		unlock_buffer(bh);
		BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
		err = ext4_handle_dirty_metadata(handle, inode, bh);
		if (!fatal)
			fatal = err;
	} else {
		BUFFER_TRACE(bh, "not a new buffer");
	}
	if (fatal) {
		*errp = fatal;
		brelse(bh);
		bh = NULL;
	}
	return bh;
}

struct buffer_head *ext4_bread(handle_t *handle, struct inode *inode,
			       ext4_lblk_t block, int create, int *err)
{
	struct buffer_head *bh;

	bh = ext4_getblk(handle, inode, block, create, err);
	if (!bh)
		return bh;
	if (buffer_uptodate(bh))
		return bh;
	ll_rw_block(READ | REQ_META | REQ_PRIO, 1, &bh);
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		return bh;
	put_bh(bh);
	*err = -EIO;
	return NULL;
}

int ext4_walk_page_buffers(handle_t *handle,
			   struct buffer_head *head,
			   unsigned from,
			   unsigned to,
			   int *partial,
			   int (*fn)(handle_t *handle,
				     struct buffer_head *bh))
{
	struct buffer_head *bh;
	unsigned block_start, block_end;
	unsigned blocksize = head->b_size;
	int err, ret = 0;
	struct buffer_head *next;

	for (bh = head, block_start = 0;
	     ret == 0 && (bh != head || !block_start);
	     block_start = block_end, bh = next) {
		next = bh->b_this_page;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (partial && !buffer_uptodate(bh))
				*partial = 1;
			continue;
		}
		err = (*fn)(handle, bh);
		if (!ret)
			ret = err;
	}
	return ret;
}

/*
 * To preserve ordering, it is essential that the hole instantiation and
 * the data write be encapsulated in a single transaction.  We cannot
 * close off a transaction and start a new one between the ext4_get_block()
 * and the commit_write().  So doing the jbd2_journal_start at the start of
 * prepare_write() is the right place.
 *
 * Also, this function can nest inside ext4_writepage().  In that case, we
 * *know* that ext4_writepage() has generated enough buffer credits to do the
 * whole page.  So we won't block on the journal in that case, which is good,
 * because the caller may be PF_MEMALLOC.
 *
 * By accident, ext4 can be reentered when a transaction is open via
 * quota file writes.  If we were to commit the transaction while thus
 * reentered, there can be a deadlock - we would be holding a quota
 * lock, and the commit would never complete if another thread had a
 * transaction open and was blocking on the quota lock - a ranking
 * violation.
 *
 * So what we do is to rely on the fact that jbd2_journal_stop/journal_start
 * will _not_ run commit under these circumstances because handle->h_ref
 * is elevated.  We'll still have enough credits for the tiny quotafile
 * write.
 */
int do_journal_get_write_access(handle_t *handle,
				struct buffer_head *bh)
{
	int dirty = buffer_dirty(bh);
	int ret;

	if (!buffer_mapped(bh) || buffer_freed(bh))
		return 0;
	/*
	 * __block_write_begin() could have dirtied some buffers. Clean
	 * the dirty bit as jbd2_journal_get_write_access() could complain
	 * otherwise about fs integrity issues. Setting of the dirty bit
	 * by __block_write_begin() isn't a real problem here as we clear
	 * the bit before releasing a page lock and thus writeback cannot
	 * ever write the buffer.
	 */
	if (dirty)
		clear_buffer_dirty(bh);
	ret = ext4_journal_get_write_access(handle, bh);
	if (!ret && dirty)
		ret = ext4_handle_dirty_metadata(handle, NULL, bh);
	return ret;
}

static int ext4_get_block_write_nolock(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create);
static int ext4_write_begin(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned flags,
			    struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	int ret, needed_blocks;
	handle_t *handle;
	int retries = 0;
	struct page *page;
	pgoff_t index;
	unsigned from, to;

	trace_ext4_write_begin(inode, pos, len, flags);
	/*
	 * Reserve one block more for addition to orphan list in case
	 * we allocate blocks but write fails for some reason
	 */
	needed_blocks = ext4_writepage_trans_blocks(inode) + 1;
	index = pos >> PAGE_CACHE_SHIFT;
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

	if (ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA)) {
		ret = ext4_try_to_write_inline_data(mapping, inode, pos, len,
						    flags, pagep);
		if (ret < 0)
			return ret;
		if (ret == 1)
			return 0;
	}

	/*
	 * grab_cache_page_write_begin() can take a long time if the
	 * system is thrashing due to memory pressure, or if the page
	 * is being written back.  So grab it first before we start
	 * the transaction handle.  This also allows us to allocate
	 * the page (if needed) without using GFP_NOFS.
	 */
retry_grab:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	unlock_page(page);

retry_journal:
	handle = ext4_journal_start(inode, EXT4_HT_WRITE_PAGE, needed_blocks);
	if (IS_ERR(handle)) {
		page_cache_release(page);
		return PTR_ERR(handle);
	}

	lock_page(page);
	if (page->mapping != mapping) {
		/* The page got truncated from under us */
		unlock_page(page);
		page_cache_release(page);
		ext4_journal_stop(handle);
		goto retry_grab;
	}
	/* In case writeback began while the page was unlocked */
	wait_for_stable_page(page);

	if (ext4_should_dioread_nolock(inode))
		ret = __block_write_begin(page, pos, len, ext4_get_block_write);
	else
		ret = __block_write_begin(page, pos, len, ext4_get_block);

	if (!ret && ext4_should_journal_data(inode)) {
		ret = ext4_walk_page_buffers(handle, page_buffers(page),
					     from, to, NULL,
					     do_journal_get_write_access);
	}

	if (ret) {
		unlock_page(page);
		/*
		 * __block_write_begin may have instantiated a few blocks
		 * outside i_size.  Trim these off again. Don't need
		 * i_size_read because we hold i_mutex.
		 *
		 * Add inode to orphan list in case we crash before
		 * truncate finishes
		 */
		if (pos + len > inode->i_size && ext4_can_truncate(inode))
			ext4_orphan_add(handle, inode);

		ext4_journal_stop(handle);
		if (pos + len > inode->i_size) {
			ext4_truncate_failed_write(inode);
			/*
			 * If truncate failed early the inode might
			 * still be on the orphan list; we need to
			 * make sure the inode is removed from the
			 * orphan list in that case.
			 */
			if (inode->i_nlink)
				ext4_orphan_del(NULL, inode);
		}

		if (ret == -ENOSPC &&
		    ext4_should_retry_alloc(inode->i_sb, &retries))
			goto retry_journal;
		page_cache_release(page);
		return ret;
	}
	*pagep = page;
	return ret;
}

/* For write_end() in data=journal mode */
static int write_end_fn(handle_t *handle, struct buffer_head *bh)
{
	int ret;
	if (!buffer_mapped(bh) || buffer_freed(bh))
		return 0;
	set_buffer_uptodate(bh);
	ret = ext4_handle_dirty_metadata(handle, NULL, bh);
	clear_buffer_meta(bh);
	clear_buffer_prio(bh);
	return ret;
}

/*
 * We need to pick up the new inode size which generic_commit_write gave us
 * `file' can be NULL - eg, when called from page_symlink().
 *
 * ext4 never places buffers on inode->i_mapping->private_list.  metadata
 * buffers are managed internally.
 */
static int ext4_write_end(struct file *file,
			  struct address_space *mapping,
			  loff_t pos, unsigned len, unsigned copied,
			  struct page *page, void *fsdata)
{
	handle_t *handle = ext4_journal_current_handle();
	struct inode *inode = mapping->host;
	int ret = 0, ret2;
	int i_size_changed = 0;

	trace_ext4_write_end(inode, pos, len, copied);
	if (ext4_test_inode_state(inode, EXT4_STATE_ORDERED_MODE)) {
		ret = ext4_jbd2_file_inode(handle, inode);
		if (ret) {
			unlock_page(page);
			page_cache_release(page);
			goto errout;
		}
	}

	if (ext4_has_inline_data(inode)) {
		ret = ext4_write_inline_data_end(inode, pos, len,
						 copied, page);
		if (ret < 0)
			goto errout;
		copied = ret;
	} else
		copied = block_write_end(file, mapping, pos,
					 len, copied, page, fsdata);

	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hole i_mutex.
	 *
	 * But it's important to update i_size while still holding page lock:
	 * page writeout could otherwise come in and zero beyond i_size.
	 */
	if (pos + copied > inode->i_size) {
		i_size_write(inode, pos + copied);
		i_size_changed = 1;
	}

	if (pos + copied > EXT4_I(inode)->i_disksize) {
		/* We need to mark inode dirty even if
		 * new_i_size is less that inode->i_size
		 * but greater than i_disksize. (hint delalloc)
		 */
		ext4_update_i_disksize(inode, (pos + copied));
		i_size_changed = 1;
	}
	unlock_page(page);
	page_cache_release(page);

	/*
	 * Don't mark the inode dirty under page lock. First, it unnecessarily
	 * makes the holding time of page lock longer. Second, it forces lock
	 * ordering of page lock and transaction start for journaling
	 * filesystems.
	 */
	if (i_size_changed)
		ext4_mark_inode_dirty(handle, inode);

	if (copied < 0)
		ret = copied;
	if (pos + len > inode->i_size && ext4_can_truncate(inode))
		/* if we have allocated more blocks and copied
		 * less. We will have blocks allocated outside
		 * inode->i_size. So truncate them
		 */
		ext4_orphan_add(handle, inode);
errout:
	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;

	if (pos + len > inode->i_size) {
		ext4_truncate_failed_write(inode);
		/*
		 * If truncate failed early the inode might still be
		 * on the orphan list; we need to make sure the inode
		 * is removed from the orphan list in that case.
		 */
		if (inode->i_nlink)
			ext4_orphan_del(NULL, inode);
	}

	return ret ? ret : copied;
}

static int ext4_journalled_write_end(struct file *file,
				     struct address_space *mapping,
				     loff_t pos, unsigned len, unsigned copied,
				     struct page *page, void *fsdata)
{
	handle_t *handle = ext4_journal_current_handle();
	struct inode *inode = mapping->host;
	int ret = 0, ret2;
	int partial = 0;
	unsigned from, to;
	loff_t new_i_size;

	trace_ext4_journalled_write_end(inode, pos, len, copied);
	from = pos & (PAGE_CACHE_SIZE - 1);
	to = from + len;

	BUG_ON(!ext4_handle_valid(handle));

	if (ext4_has_inline_data(inode))
		copied = ext4_write_inline_data_end(inode, pos, len,
						    copied, page);
	else {
		if (copied < len) {
			if (!PageUptodate(page))
				copied = 0;
			page_zero_new_buffers(page, from+copied, to);
		}

		ret = ext4_walk_page_buffers(handle, page_buffers(page), from,
					     to, &partial, write_end_fn);
		if (!partial)
			SetPageUptodate(page);
	}
	new_i_size = pos + copied;
	if (new_i_size > inode->i_size)
		i_size_write(inode, pos+copied);
	ext4_set_inode_state(inode, EXT4_STATE_JDATA);
	EXT4_I(inode)->i_datasync_tid = handle->h_transaction->t_tid;
	if (new_i_size > EXT4_I(inode)->i_disksize) {
		ext4_update_i_disksize(inode, new_i_size);
		ret2 = ext4_mark_inode_dirty(handle, inode);
		if (!ret)
			ret = ret2;
	}

	unlock_page(page);
	page_cache_release(page);
	if (pos + len > inode->i_size && ext4_can_truncate(inode))
		/* if we have allocated more blocks and copied
		 * less. We will have blocks allocated outside
		 * inode->i_size. So truncate them
		 */
		ext4_orphan_add(handle, inode);

	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;
	if (pos + len > inode->i_size) {
		ext4_truncate_failed_write(inode);
		/*
		 * If truncate failed early the inode might still be
		 * on the orphan list; we need to make sure the inode
		 * is removed from the orphan list in that case.
		 */
		if (inode->i_nlink)
			ext4_orphan_del(NULL, inode);
	}

	return ret ? ret : copied;
}

/*
 * Reserve a metadata for a single block located at lblock
 */
static int ext4_da_reserve_metadata(struct inode *inode, ext4_lblk_t lblock)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned int md_needed;
	ext4_lblk_t save_last_lblock;
	int save_len;

	/*
	 * recalculate the amount of metadata blocks to reserve
	 * in order to allocate nrblocks
	 * worse case is one extent per block
	 */
	spin_lock(&ei->i_block_reservation_lock);
	/*
	 * ext4_calc_metadata_amount() has side effects, which we have
	 * to be prepared undo if we fail to claim space.
	 */
	save_len = ei->i_da_metadata_calc_len;
	save_last_lblock = ei->i_da_metadata_calc_last_lblock;
	md_needed = EXT4_NUM_B2C(sbi,
				 ext4_calc_metadata_amount(inode, lblock));
	trace_ext4_da_reserve_space(inode, md_needed);

	/*
	 * We do still charge estimated metadata to the sb though;
	 * we cannot afford to run out of free blocks.
	 */
	if (ext4_claim_free_clusters(sbi, md_needed, 0)) {
		ei->i_da_metadata_calc_len = save_len;
		ei->i_da_metadata_calc_last_lblock = save_last_lblock;
		spin_unlock(&ei->i_block_reservation_lock);
		return -ENOSPC;
	}
	ei->i_reserved_meta_blocks += md_needed;
	spin_unlock(&ei->i_block_reservation_lock);

	return 0;       /* success */
}

/*
 * Reserve a single cluster located at lblock
 */
static int ext4_da_reserve_space(struct inode *inode, ext4_lblk_t lblock)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned int md_needed;
	int ret;
	ext4_lblk_t save_last_lblock;
	int save_len;

	/*
	 * We will charge metadata quota at writeout time; this saves
	 * us from metadata over-estimation, though we may go over by
	 * a small amount in the end.  Here we just reserve for data.
	 */
	ret = dquot_reserve_block(inode, EXT4_C2B(sbi, 1));
	if (ret)
		return ret;

	/*
	 * recalculate the amount of metadata blocks to reserve
	 * in order to allocate nrblocks
	 * worse case is one extent per block
	 */
	spin_lock(&ei->i_block_reservation_lock);
	/*
	 * ext4_calc_metadata_amount() has side effects, which we have
	 * to be prepared undo if we fail to claim space.
	 */
	save_len = ei->i_da_metadata_calc_len;
	save_last_lblock = ei->i_da_metadata_calc_last_lblock;
	md_needed = EXT4_NUM_B2C(sbi,
				 ext4_calc_metadata_amount(inode, lblock));
	trace_ext4_da_reserve_space(inode, md_needed);

	/*
	 * We do still charge estimated metadata to the sb though;
	 * we cannot afford to run out of free blocks.
	 */
	if (ext4_claim_free_clusters(sbi, md_needed + 1, 0)) {
		ei->i_da_metadata_calc_len = save_len;
		ei->i_da_metadata_calc_last_lblock = save_last_lblock;
		spin_unlock(&ei->i_block_reservation_lock);
		dquot_release_reservation_block(inode, EXT4_C2B(sbi, 1));
		return -ENOSPC;
	}
	ei->i_reserved_data_blocks++;
	ei->i_reserved_meta_blocks += md_needed;
	spin_unlock(&ei->i_block_reservation_lock);

	return 0;       /* success */
}

static void ext4_da_release_space(struct inode *inode, int to_free)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);

	if (!to_free)
		return;		/* Nothing to release, exit */

	spin_lock(&EXT4_I(inode)->i_block_reservation_lock);

	trace_ext4_da_release_space(inode, to_free);
	if (unlikely(to_free > ei->i_reserved_data_blocks)) {
		/*
		 * if there aren't enough reserved blocks, then the
		 * counter is messed up somewhere.  Since this
		 * function is called from invalidate page, it's
		 * harmless to return without any action.
		 */
		ext4_warning(inode->i_sb, "ext4_da_release_space: "
			 "ino %lu, to_free %d with only %d reserved "
			 "data blocks", inode->i_ino, to_free,
			 ei->i_reserved_data_blocks);
		WARN_ON(1);
		to_free = ei->i_reserved_data_blocks;
	}
	ei->i_reserved_data_blocks -= to_free;

	if (ei->i_reserved_data_blocks == 0) {
		/*
		 * We can release all of the reserved metadata blocks
		 * only when we have written all of the delayed
		 * allocation blocks.
		 * Note that in case of bigalloc, i_reserved_meta_blocks,
		 * i_reserved_data_blocks, etc. refer to number of clusters.
		 */
		percpu_counter_sub(&sbi->s_dirtyclusters_counter,
				   ei->i_reserved_meta_blocks);
		ei->i_reserved_meta_blocks = 0;
		ei->i_da_metadata_calc_len = 0;
	}

	/* update fs dirty data blocks counter */
	percpu_counter_sub(&sbi->s_dirtyclusters_counter, to_free);

	spin_unlock(&EXT4_I(inode)->i_block_reservation_lock);

	dquot_release_reservation_block(inode, EXT4_C2B(sbi, to_free));
}

static void ext4_da_page_release_reservation(struct page *page,
					     unsigned long offset)
{
	int to_release = 0;
	struct buffer_head *head, *bh;
	unsigned int curr_off = 0;
	struct inode *inode = page->mapping->host;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	int num_clusters;
	ext4_fsblk_t lblk;

	head = page_buffers(page);
	bh = head;
	do {
		unsigned int next_off = curr_off + bh->b_size;

		if ((offset <= curr_off) && (buffer_delay(bh))) {
			to_release++;
			clear_buffer_delay(bh);
		}
		curr_off = next_off;
	} while ((bh = bh->b_this_page) != head);

	if (to_release) {
		lblk = page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
		ext4_es_remove_extent(inode, lblk, to_release);
	}

	/* If we have released all the blocks belonging to a cluster, then we
	 * need to release the reserved space for that cluster. */
	num_clusters = EXT4_NUM_B2C(sbi, to_release);
	while (num_clusters > 0) {
		lblk = (page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits)) +
			((num_clusters - 1) << sbi->s_cluster_bits);
		if (sbi->s_cluster_ratio == 1 ||
		    !ext4_find_delalloc_cluster(inode, lblk))
			ext4_da_release_space(inode, 1);

		num_clusters--;
	}
}

/*
 * Delayed allocation stuff
 */

/*
 * mpage_da_submit_io - walks through extent of pages and try to write
 * them with writepage() call back
 *
 * @mpd->inode: inode
 * @mpd->first_page: first page of the extent
 * @mpd->next_page: page after the last page of the extent
 *
 * By the time mpage_da_submit_io() is called we expect all blocks
 * to be allocated. this may be wrong if allocation failed.
 *
 * As pages are already locked by write_cache_pages(), we can't use it
 */
static int mpage_da_submit_io(struct mpage_da_data *mpd,
			      struct ext4_map_blocks *map)
{
	struct pagevec pvec;
	unsigned long index, end;
	int ret = 0, err, nr_pages, i;
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;
	loff_t size = i_size_read(inode);
	unsigned int len, block_start;
	struct buffer_head *bh, *page_bufs = NULL;
	sector_t pblock = 0, cur_logical = 0;
	struct ext4_io_submit io_submit;

	BUG_ON(mpd->next_page <= mpd->first_page);
	memset(&io_submit, 0, sizeof(io_submit));
	/*
	 * We need to start from the first_page to the next_page - 1
	 * to make sure we also write the mapped dirty buffer_heads.
	 * If we look at mpd->b_blocknr we would only be looking
	 * at the currently mapped buffer_heads.
	 */
	index = mpd->first_page;
	end = mpd->next_page - 1;

	pagevec_init(&pvec, 0);
	while (index <= end) {
		nr_pages = pagevec_lookup(&pvec, mapping, index, PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			int skip_page = 0;
			struct page *page = pvec.pages[i];

			index = page->index;
			if (index > end)
				break;

			if (index == size >> PAGE_CACHE_SHIFT)
				len = size & ~PAGE_CACHE_MASK;
			else
				len = PAGE_CACHE_SIZE;
			if (map) {
				cur_logical = index << (PAGE_CACHE_SHIFT -
							inode->i_blkbits);
				pblock = map->m_pblk + (cur_logical -
							map->m_lblk);
			}
			index++;

			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));

			bh = page_bufs = page_buffers(page);
			block_start = 0;
			do {
				if (map && (cur_logical >= map->m_lblk) &&
				    (cur_logical <= (map->m_lblk +
						     (map->m_len - 1)))) {
					if (buffer_delay(bh)) {
						clear_buffer_delay(bh);
						bh->b_blocknr = pblock;
					}
					if (buffer_unwritten(bh) ||
					    buffer_mapped(bh))
						BUG_ON(bh->b_blocknr != pblock);
					if (map->m_flags & EXT4_MAP_UNINIT)
						set_buffer_uninit(bh);
					clear_buffer_unwritten(bh);
				}

				/*
				 * skip page if block allocation undone and
				 * block is dirty
				 */
				if (ext4_bh_delay_or_unwritten(NULL, bh))
					skip_page = 1;
				bh = bh->b_this_page;
				block_start += bh->b_size;
				cur_logical++;
				pblock++;
			} while (bh != page_bufs);

			if (skip_page) {
				unlock_page(page);
				continue;
			}

			clear_page_dirty_for_io(page);
			err = ext4_bio_write_page(&io_submit, page, len,
						  mpd->wbc);
			if (!err)
				mpd->pages_written++;
			/*
			 * In error case, we have to continue because
			 * remaining pages are still locked
			 */
			if (ret == 0)
				ret = err;
		}
		pagevec_release(&pvec);
	}
	ext4_io_submit(&io_submit);
	return ret;
}

static void ext4_da_block_invalidatepages(struct mpage_da_data *mpd)
{
	int nr_pages, i;
	pgoff_t index, end;
	struct pagevec pvec;
	struct inode *inode = mpd->inode;
	struct address_space *mapping = inode->i_mapping;
	ext4_lblk_t start, last;

	index = mpd->first_page;
	end   = mpd->next_page - 1;

	start = index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	last = end << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	ext4_es_remove_extent(inode, start, last - start + 1);

	pagevec_init(&pvec, 0);
	while (index <= end) {
		nr_pages = pagevec_lookup(&pvec, mapping, index, PAGEVEC_SIZE);
		if (nr_pages == 0)
			break;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			if (page->index > end)
				break;
			BUG_ON(!PageLocked(page));
			BUG_ON(PageWriteback(page));
			block_invalidatepage(page, 0);
			ClearPageUptodate(page);
			unlock_page(page);
		}
		index = pvec.pages[nr_pages - 1]->index + 1;
		pagevec_release(&pvec);
	}
	return;
}

static void ext4_print_free_blocks(struct inode *inode)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = EXT4_I(inode);

	ext4_msg(sb, KERN_CRIT, "Total free blocks count %lld",
	       EXT4_C2B(EXT4_SB(inode->i_sb),
			ext4_count_free_clusters(sb)));
	ext4_msg(sb, KERN_CRIT, "Free/Dirty block details");
	ext4_msg(sb, KERN_CRIT, "free_blocks=%lld",
	       (long long) EXT4_C2B(EXT4_SB(sb),
		percpu_counter_sum(&sbi->s_freeclusters_counter)));
	ext4_msg(sb, KERN_CRIT, "dirty_blocks=%lld",
	       (long long) EXT4_C2B(EXT4_SB(sb),
		percpu_counter_sum(&sbi->s_dirtyclusters_counter)));
	ext4_msg(sb, KERN_CRIT, "Block reservation details");
	ext4_msg(sb, KERN_CRIT, "i_reserved_data_blocks=%u",
		 ei->i_reserved_data_blocks);
	ext4_msg(sb, KERN_CRIT, "i_reserved_meta_blocks=%u",
	       ei->i_reserved_meta_blocks);
	ext4_msg(sb, KERN_CRIT, "i_allocated_meta_blocks=%u",
	       ei->i_allocated_meta_blocks);
	return;
}

/*
 * mpage_da_map_and_submit - go through given space, map them
 *       if necessary, and then submit them for I/O
 *
 * @mpd - bh describing space
 *
 * The function skips space we know is already mapped to disk blocks.
 *
 */
static void mpage_da_map_and_submit(struct mpage_da_data *mpd)
{
	int err, blks, get_blocks_flags;
	struct ext4_map_blocks map, *mapp = NULL;
	sector_t next = mpd->b_blocknr;
	unsigned max_blocks = mpd->b_size >> mpd->inode->i_blkbits;
	loff_t disksize = EXT4_I(mpd->inode)->i_disksize;
	handle_t *handle = NULL;

	/*
	 * If the blocks are mapped already, or we couldn't accumulate
	 * any blocks, then proceed immediately to the submission stage.
	 */
	if ((mpd->b_size == 0) ||
	    ((mpd->b_state  & (1 << BH_Mapped)) &&
	     !(mpd->b_state & (1 << BH_Delay)) &&
	     !(mpd->b_state & (1 << BH_Unwritten))))
		goto submit_io;

	handle = ext4_journal_current_handle();
	BUG_ON(!handle);

	/*
	 * Call ext4_map_blocks() to allocate any delayed allocation
	 * blocks, or to convert an uninitialized extent to be
	 * initialized (in the case where we have written into
	 * one or more preallocated blocks).
	 *
	 * We pass in the magic EXT4_GET_BLOCKS_DELALLOC_RESERVE to
	 * indicate that we are on the delayed allocation path.  This
	 * affects functions in many different parts of the allocation
	 * call path.  This flag exists primarily because we don't
	 * want to change *many* call functions, so ext4_map_blocks()
	 * will set the EXT4_STATE_DELALLOC_RESERVED flag once the
	 * inode's allocation semaphore is taken.
	 *
	 * If the blocks in questions were delalloc blocks, set
	 * EXT4_GET_BLOCKS_DELALLOC_RESERVE so the delalloc accounting
	 * variables are updated after the blocks have been allocated.
	 */
	map.m_lblk = next;
	map.m_len = max_blocks;
	/*
	 * We're in delalloc path and it is possible that we're going to
	 * need more metadata blocks than previously reserved. However
	 * we must not fail because we're in writeback and there is
	 * nothing we can do about it so it might result in data loss.
	 * So use reserved blocks to allocate metadata if possible.
	 */
	get_blocks_flags = EXT4_GET_BLOCKS_CREATE |
			   EXT4_GET_BLOCKS_METADATA_NOFAIL;
	if (ext4_should_dioread_nolock(mpd->inode))
		get_blocks_flags |= EXT4_GET_BLOCKS_IO_CREATE_EXT;
	if (mpd->b_state & (1 << BH_Delay))
		get_blocks_flags |= EXT4_GET_BLOCKS_DELALLOC_RESERVE;


	blks = ext4_map_blocks(handle, mpd->inode, &map, get_blocks_flags);
	if (blks < 0) {
		struct super_block *sb = mpd->inode->i_sb;

		err = blks;
		/*
		 * If get block returns EAGAIN or ENOSPC and there
		 * appears to be free blocks we will just let
		 * mpage_da_submit_io() unlock all of the pages.
		 */
		if (err == -EAGAIN)
			goto submit_io;

		if (err == -ENOSPC && ext4_count_free_clusters(sb)) {
			mpd->retval = err;
			goto submit_io;
		}

		/*
		 * get block failure will cause us to loop in
		 * writepages, because a_ops->writepage won't be able
		 * to make progress. The page will be redirtied by
		 * writepage and writepages will again try to write
		 * the same.
		 */
		if (!(EXT4_SB(sb)->s_mount_flags & EXT4_MF_FS_ABORTED)) {
			ext4_msg(sb, KERN_CRIT,
				 "delayed block allocation failed for inode %lu "
				 "at logical offset %llu with max blocks %zd "
				 "with error %d", mpd->inode->i_ino,
				 (unsigned long long) next,
				 mpd->b_size >> mpd->inode->i_blkbits, err);
			ext4_msg(sb, KERN_CRIT,
				"This should not happen!! Data will be lost");
			if (err == -ENOSPC)
				ext4_print_free_blocks(mpd->inode);
		}
		/* invalidate all the pages */
		ext4_da_block_invalidatepages(mpd);

		/* Mark this page range as having been completed */
		mpd->io_done = 1;
		return;
	}
	BUG_ON(blks == 0);

	mapp = &map;
	if (map.m_flags & EXT4_MAP_NEW) {
		struct block_device *bdev = mpd->inode->i_sb->s_bdev;
		int i;

		for (i = 0; i < map.m_len; i++)
			unmap_underlying_metadata(bdev, map.m_pblk + i);
	}

	/*
	 * Update on-disk size along with block allocation.
	 */
	disksize = ((loff_t) next + blks) << mpd->inode->i_blkbits;
	if (disksize > i_size_read(mpd->inode))
		disksize = i_size_read(mpd->inode);
	if (disksize > EXT4_I(mpd->inode)->i_disksize) {
		ext4_update_i_disksize(mpd->inode, disksize);
		err = ext4_mark_inode_dirty(handle, mpd->inode);
		if (err)
			ext4_error(mpd->inode->i_sb,
				   "Failed to mark inode %lu dirty",
				   mpd->inode->i_ino);
	}

submit_io:
	mpage_da_submit_io(mpd, mapp);
	mpd->io_done = 1;
}

#define BH_FLAGS ((1 << BH_Uptodate) | (1 << BH_Mapped) | \
		(1 << BH_Delay) | (1 << BH_Unwritten))

/*
 * mpage_add_bh_to_extent - try to add one more block to extent of blocks
 *
 * @mpd->lbh - extent of blocks
 * @logical - logical number of the block in the file
 * @b_state - b_state of the buffer head added
 *
 * the function is used to collect contig. blocks in same state
 */
static void mpage_add_bh_to_extent(struct mpage_da_data *mpd, sector_t logical,
				   unsigned long b_state)
{
	sector_t next;
	int blkbits = mpd->inode->i_blkbits;
	int nrblocks = mpd->b_size >> blkbits;

	/*
	 * XXX Don't go larger than mballoc is willing to allocate
	 * This is a stopgap solution.  We eventually need to fold
	 * mpage_da_submit_io() into this function and then call
	 * ext4_map_blocks() multiple times in a loop
	 */
	if (nrblocks >= (8*1024*1024 >> blkbits))
		goto flush_it;

	/* check if the reserved journal credits might overflow */
	if (!ext4_test_inode_flag(mpd->inode, EXT4_INODE_EXTENTS)) {
		if (nrblocks >= EXT4_MAX_TRANS_DATA) {
			/*
			 * With non-extent format we are limited by the journal
			 * credit available.  Total credit needed to insert
			 * nrblocks contiguous blocks is dependent on the
			 * nrblocks.  So limit nrblocks.
			 */
			goto flush_it;
		}
	}
	/*
	 * First block in the extent
	 */
	if (mpd->b_size == 0) {
		mpd->b_blocknr = logical;
		mpd->b_size = 1 << blkbits;
		mpd->b_state = b_state & BH_FLAGS;
		return;
	}

	next = mpd->b_blocknr + nrblocks;
	/*
	 * Can we merge the block to our big extent?
	 */
	if (logical == next && (b_state & BH_FLAGS) == mpd->b_state) {
		mpd->b_size += 1 << blkbits;
		return;
	}

flush_it:
	/*
	 * We couldn't merge the block to our extent, so we
	 * need to flush current  extent and start new one
	 */
	mpage_da_map_and_submit(mpd);
	return;
}

static int ext4_bh_delay_or_unwritten(handle_t *handle, struct buffer_head *bh)
{
	return (buffer_delay(bh) || buffer_unwritten(bh)) && buffer_dirty(bh);
}

/*
 * This function is grabs code from the very beginning of
 * ext4_map_blocks, but assumes that the caller is from delayed write
 * time. This function looks up the requested blocks and sets the
 * buffer delay bit under the protection of i_data_sem.
 */
static int ext4_da_map_blocks(struct inode *inode, sector_t iblock,
			      struct ext4_map_blocks *map,
			      struct buffer_head *bh)
{
	struct extent_status es;
	int retval;
	sector_t invalid_block = ~((sector_t) 0xffff);
#ifdef ES_AGGRESSIVE_TEST
	struct ext4_map_blocks orig_map;

	memcpy(&orig_map, map, sizeof(*map));
#endif

	if (invalid_block < ext4_blocks_count(EXT4_SB(inode->i_sb)->s_es))
		invalid_block = ~0;

	map->m_flags = 0;
	ext_debug("ext4_da_map_blocks(): inode %lu, max_blocks %u,"
		  "logical block %lu\n", inode->i_ino, map->m_len,
		  (unsigned long) map->m_lblk);

	/* Lookup extent status tree firstly */
	if (ext4_es_lookup_extent(inode, iblock, &es)) {

		if (ext4_es_is_hole(&es)) {
			retval = 0;
			down_read((&EXT4_I(inode)->i_data_sem));
			goto add_delayed;
		}

		/*
		 * Delayed extent could be allocated by fallocate.
		 * So we need to check it.
		 */
		if (ext4_es_is_delayed(&es) && !ext4_es_is_unwritten(&es)) {
			map_bh(bh, inode->i_sb, invalid_block);
			set_buffer_new(bh);
			set_buffer_delay(bh);
			return 0;
		}

		map->m_pblk = ext4_es_pblock(&es) + iblock - es.es_lblk;
		retval = es.es_len - (iblock - es.es_lblk);
		if (retval > map->m_len)
			retval = map->m_len;
		map->m_len = retval;
		if (ext4_es_is_written(&es))
			map->m_flags |= EXT4_MAP_MAPPED;
		else if (ext4_es_is_unwritten(&es))
			map->m_flags |= EXT4_MAP_UNWRITTEN;
		else
			BUG_ON(1);

#ifdef ES_AGGRESSIVE_TEST
		ext4_map_blocks_es_recheck(NULL, inode, map, &orig_map, 0);
#endif
		return retval;
	}

	/*
	 * Try to see if we can get the block without requesting a new
	 * file system block.
	 */
	down_read((&EXT4_I(inode)->i_data_sem));
	if (ext4_has_inline_data(inode)) {
		/*
		 * We will soon create blocks for this page, and let
		 * us pretend as if the blocks aren't allocated yet.
		 * In case of clusters, we have to handle the work
		 * of mapping from cluster so that the reserved space
		 * is calculated properly.
		 */
		if ((EXT4_SB(inode->i_sb)->s_cluster_ratio > 1) &&
		    ext4_find_delalloc_cluster(inode, map->m_lblk))
			map->m_flags |= EXT4_MAP_FROM_CLUSTER;
		retval = 0;
	} else if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		retval = ext4_ext_map_blocks(NULL, inode, map,
					     EXT4_GET_BLOCKS_NO_PUT_HOLE);
	else
		retval = ext4_ind_map_blocks(NULL, inode, map,
					     EXT4_GET_BLOCKS_NO_PUT_HOLE);

add_delayed:
	if (retval == 0) {
		int ret;
		/*
		 * XXX: __block_prepare_write() unmaps passed block,
		 * is it OK?
		 */
		/*
		 * If the block was allocated from previously allocated cluster,
		 * then we don't need to reserve it again. However we still need
		 * to reserve metadata for every block we're going to write.
		 */
		if (!(map->m_flags & EXT4_MAP_FROM_CLUSTER)) {
			ret = ext4_da_reserve_space(inode, iblock);
			if (ret) {
				/* not enough space to reserve */
				retval = ret;
				goto out_unlock;
			}
		} else {
			ret = ext4_da_reserve_metadata(inode, iblock);
			if (ret) {
				/* not enough space to reserve */
				retval = ret;
				goto out_unlock;
			}
		}

		ret = ext4_es_insert_extent(inode, map->m_lblk, map->m_len,
					    ~0, EXTENT_STATUS_DELAYED);
		if (ret) {
			retval = ret;
			goto out_unlock;
		}

		/* Clear EXT4_MAP_FROM_CLUSTER flag since its purpose is served
		 * and it should not appear on the bh->b_state.
		 */
		map->m_flags &= ~EXT4_MAP_FROM_CLUSTER;

		map_bh(bh, inode->i_sb, invalid_block);
		set_buffer_new(bh);
		set_buffer_delay(bh);
	} else if (retval > 0) {
		int ret;
		unsigned long long status;

#ifdef ES_AGGRESSIVE_TEST
		if (retval != map->m_len) {
			printk("ES len assertation failed for inode: %lu "
			       "retval %d != map->m_len %d "
			       "in %s (lookup)\n", inode->i_ino, retval,
			       map->m_len, __func__);
		}
#endif

		status = map->m_flags & EXT4_MAP_UNWRITTEN ?
				EXTENT_STATUS_UNWRITTEN : EXTENT_STATUS_WRITTEN;
		ret = ext4_es_insert_extent(inode, map->m_lblk, map->m_len,
					    map->m_pblk, status);
		if (ret != 0)
			retval = ret;
	}

out_unlock:
	up_read((&EXT4_I(inode)->i_data_sem));

	return retval;
}

/*
 * This is a special get_blocks_t callback which is used by
 * ext4_da_write_begin().  It will either return mapped block or
 * reserve space for a single block.
 *
 * For delayed buffer_head we have BH_Mapped, BH_New, BH_Delay set.
 * We also have b_blocknr = -1 and b_bdev initialized properly
 *
 * For unwritten buffer_head we have BH_Mapped, BH_New, BH_Unwritten set.
 * We also have b_blocknr = physicalblock mapping unwritten extent and b_bdev
 * initialized properly.
 */
int ext4_da_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	struct ext4_map_blocks map;
	int ret = 0;

	BUG_ON(create == 0);
	BUG_ON(bh->b_size != inode->i_sb->s_blocksize);

	map.m_lblk = iblock;
	map.m_len = 1;

	/*
	 * first, we need to know whether the block is allocated already
	 * preallocated blocks are unmapped but should treated
	 * the same as allocated blocks.
	 */
	ret = ext4_da_map_blocks(inode, iblock, &map, bh);
	if (ret <= 0)
		return ret;

	map_bh(bh, inode->i_sb, map.m_pblk);
	bh->b_state = (bh->b_state & ~EXT4_MAP_FLAGS) | map.m_flags;

	if (buffer_unwritten(bh)) {
		/* A delayed write to unwritten bh should be marked
		 * new and mapped.  Mapped ensures that we don't do
		 * get_block multiple times when we write to the same
		 * offset and new ensures that we do proper zero out
		 * for partial write.
		 */
		set_buffer_new(bh);
		set_buffer_mapped(bh);
	}
	return 0;
}

static int bget_one(handle_t *handle, struct buffer_head *bh)
{
	get_bh(bh);
	return 0;
}

static int bput_one(handle_t *handle, struct buffer_head *bh)
{
	put_bh(bh);
	return 0;
}

static int __ext4_journalled_writepage(struct page *page,
				       unsigned int len)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct buffer_head *page_bufs = NULL;
	handle_t *handle = NULL;
	int ret = 0, err = 0;
	int inline_data = ext4_has_inline_data(inode);
	struct buffer_head *inode_bh = NULL;

	ClearPageChecked(page);

	if (inline_data) {
		BUG_ON(page->index != 0);
		BUG_ON(len > ext4_get_max_inline_size(inode));
		inode_bh = ext4_journalled_write_inline_data(inode, len, page);
		if (inode_bh == NULL)
			goto out;
	} else {
		page_bufs = page_buffers(page);
		if (!page_bufs) {
			BUG();
			goto out;
		}
		ext4_walk_page_buffers(handle, page_bufs, 0, len,
				       NULL, bget_one);
	}
	/* As soon as we unlock the page, it can go away, but we have
	 * references to buffers so we are safe */
	unlock_page(page);

	handle = ext4_journal_start(inode, EXT4_HT_WRITE_PAGE,
				    ext4_writepage_trans_blocks(inode));
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	BUG_ON(!ext4_handle_valid(handle));

	if (inline_data) {
		ret = ext4_journal_get_write_access(handle, inode_bh);

		err = ext4_handle_dirty_metadata(handle, inode, inode_bh);

	} else {
		ret = ext4_walk_page_buffers(handle, page_bufs, 0, len, NULL,
					     do_journal_get_write_access);

		err = ext4_walk_page_buffers(handle, page_bufs, 0, len, NULL,
					     write_end_fn);
	}
	if (ret == 0)
		ret = err;
	EXT4_I(inode)->i_datasync_tid = handle->h_transaction->t_tid;
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;

	if (!ext4_has_inline_data(inode))
		ext4_walk_page_buffers(handle, page_bufs, 0, len,
				       NULL, bput_one);
	ext4_set_inode_state(inode, EXT4_STATE_JDATA);
out:
	brelse(inode_bh);
	return ret;
}

/*
 * Note that we don't need to start a transaction unless we're journaling data
 * because we should have holes filled from ext4_page_mkwrite(). We even don't
 * need to file the inode to the transaction's list in ordered mode because if
 * we are writing back data added by write(), the inode is already there and if
 * we are writing back data modified via mmap(), no one guarantees in which
 * transaction the data will hit the disk. In case we are journaling data, we
 * cannot start transaction directly because transaction start ranks above page
 * lock so we have to do some magic.
 *
 * This function can get called via...
 *   - ext4_da_writepages after taking page lock (have journal handle)
 *   - journal_submit_inode_data_buffers (no journal handle)
 *   - shrink_page_list via the kswapd/direct reclaim (no journal handle)
 *   - grab_page_cache when doing write_begin (have journal handle)
 *
 * We don't do any block allocation in this function. If we have page with
 * multiple blocks we need to write those buffer_heads that are mapped. This
 * is important for mmaped based write. So if we do with blocksize 1K
 * truncate(f, 1024);
 * a = mmap(f, 0, 4096);
 * a[0] = 'a';
 * truncate(f, 4096);
 * we have in the page first buffer_head mapped via page_mkwrite call back
 * but other buffer_heads would be unmapped but dirty (dirty done via the
 * do_wp_page). So writepage should write the first block. If we modify
 * the mmap area beyond 1024 we will again get a page_fault and the
 * page_mkwrite callback will do the block allocation and mark the
 * buffer_heads mapped.
 *
 * We redirty the page if we have any buffer_heads that is either delay or
 * unwritten in the page.
 *
 * We can get recursively called as show below.
 *
 *	ext4_writepage() -> kmalloc() -> __alloc_pages() -> page_launder() ->
 *		ext4_writepage()
 *
 * But since we don't do any block allocation we should not deadlock.
 * Page also have the dirty flag cleared so we don't get recurive page_lock.
 */
static int ext4_writepage(struct page *page,
			  struct writeback_control *wbc)
{
	int ret = 0;
	loff_t size;
	unsigned int len;
	struct buffer_head *page_bufs = NULL;
	struct inode *inode = page->mapping->host;
	struct ext4_io_submit io_submit;

	trace_ext4_writepage(page);
	size = i_size_read(inode);
	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;

	page_bufs = page_buffers(page);
	/*
	 * We cannot do block allocation or other extent handling in this
	 * function. If there are buffers needing that, we have to redirty
	 * the page. But we may reach here when we do a journal commit via
	 * journal_submit_inode_data_buffers() and in that case we must write
	 * allocated buffers to achieve data=ordered mode guarantees.
	 */
	if (ext4_walk_page_buffers(NULL, page_bufs, 0, len, NULL,
				   ext4_bh_delay_or_unwritten)) {
		redirty_page_for_writepage(wbc, page);
		if (current->flags & PF_MEMALLOC) {
			/*
			 * For memory cleaning there's no point in writing only
			 * some buffers. So just bail out. Warn if we came here
			 * from direct reclaim.
			 */
			WARN_ON_ONCE((current->flags & (PF_MEMALLOC|PF_KSWAPD))
							== PF_MEMALLOC);
			unlock_page(page);
			return 0;
		}
	}

	if (PageChecked(page) && ext4_should_journal_data(inode))
		/*
		 * It's mmapped pagecache.  Add buffers and journal it.  There
		 * doesn't seem much point in redirtying the page here.
		 */
		return __ext4_journalled_writepage(page, len);

	memset(&io_submit, 0, sizeof(io_submit));
	ret = ext4_bio_write_page(&io_submit, page, len, wbc);
	ext4_io_submit(&io_submit);
	return ret;
}

/*
 * This is called via ext4_da_writepages() to
 * calculate the total number of credits to reserve to fit
 * a single extent allocation into a single transaction,
 * ext4_da_writpeages() will loop calling this before
 * the block allocation.
 */

static int ext4_da_writepages_trans_blocks(struct inode *inode)
{
	int max_blocks = EXT4_I(inode)->i_reserved_data_blocks;

	/*
	 * With non-extent format the journal credit needed to
	 * insert nrblocks contiguous block is dependent on
	 * number of contiguous block. So we will limit
	 * number of contiguous block to a sane value
	 */
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) &&
	    (max_blocks > EXT4_MAX_TRANS_DATA))
		max_blocks = EXT4_MAX_TRANS_DATA;

	return ext4_chunk_trans_blocks(inode, max_blocks);
}

/*
 * write_cache_pages_da - walk the list of dirty pages of the given
 * address space and accumulate pages that need writing, and call
 * mpage_da_map_and_submit to map a single contiguous memory region
 * and then write them.
 */
static int write_cache_pages_da(handle_t *handle,
				struct address_space *mapping,
				struct writeback_control *wbc,
				struct mpage_da_data *mpd,
				pgoff_t *done_index)
{
	struct buffer_head	*bh, *head;
	struct inode		*inode = mapping->host;
	struct pagevec		pvec;
	unsigned int		nr_pages;
	sector_t		logical;
	pgoff_t			index, end;
	long			nr_to_write = wbc->nr_to_write;
	int			i, tag, ret = 0;

	memset(mpd, 0, sizeof(struct mpage_da_data));
	mpd->wbc = wbc;
	mpd->inode = inode;
	pagevec_init(&pvec, 0);
	index = wbc->range_start >> PAGE_CACHE_SHIFT;
	end = wbc->range_end >> PAGE_CACHE_SHIFT;

	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;

	*done_index = index;
	while (index <= end) {
		nr_pages = pagevec_lookup_tag(&pvec, mapping, &index, tag,
			      min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
		if (nr_pages == 0)
			return 0;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * At this point, the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or
			 * even swizzled back from swapper_space to tmpfs file
			 * mapping. However, page->index will not change
			 * because we have a reference on the page.
			 */
			if (page->index > end)
				goto out;

			*done_index = page->index + 1;

			/*
			 * If we can't merge this page, and we have
			 * accumulated an contiguous region, write it
			 */
			if ((mpd->next_page != page->index) &&
			    (mpd->next_page != mpd->first_page)) {
				mpage_da_map_and_submit(mpd);
				goto ret_extent_tail;
			}

			lock_page(page);

			/*
			 * If the page is no longer dirty, or its
			 * mapping no longer corresponds to inode we
			 * are writing (which means it has been
			 * truncated or invalidated), or the page is
			 * already under writeback and we are not
			 * doing a data integrity writeback, skip the page
			 */
			if (!PageDirty(page) ||
			    (PageWriteback(page) &&
			     (wbc->sync_mode == WB_SYNC_NONE)) ||
			    unlikely(page->mapping != mapping)) {
				unlock_page(page);
				continue;
			}

			wait_on_page_writeback(page);
			BUG_ON(PageWriteback(page));

			/*
			 * If we have inline data and arrive here, it means that
			 * we will soon create the block for the 1st page, so
			 * we'd better clear the inline data here.
			 */
			if (ext4_has_inline_data(inode)) {
				BUG_ON(ext4_test_inode_state(inode,
						EXT4_STATE_MAY_INLINE_DATA));
				ext4_destroy_inline_data(handle, inode);
			}

			if (mpd->next_page != page->index)
				mpd->first_page = page->index;
			mpd->next_page = page->index + 1;
			logical = (sector_t) page->index <<
				(PAGE_CACHE_SHIFT - inode->i_blkbits);

			/* Add all dirty buffers to mpd */
			head = page_buffers(page);
			bh = head;
			do {
				BUG_ON(buffer_locked(bh));
				/*
				 * We need to try to allocate unmapped blocks
				 * in the same page.  Otherwise we won't make
				 * progress with the page in ext4_writepage
				 */
				if (ext4_bh_delay_or_unwritten(NULL, bh)) {
					mpage_add_bh_to_extent(mpd, logical,
							       bh->b_state);
					if (mpd->io_done)
						goto ret_extent_tail;
				} else if (buffer_dirty(bh) &&
					   buffer_mapped(bh)) {
					/*
					 * mapped dirty buffer. We need to
					 * update the b_state because we look
					 * at b_state in mpage_da_map_blocks.
					 * We don't update b_size because if we
					 * find an unmapped buffer_head later
					 * we need to use the b_state flag of
					 * that buffer_head.
					 */
					if (mpd->b_size == 0)
						mpd->b_state =
							bh->b_state & BH_FLAGS;
				}
				logical++;
			} while ((bh = bh->b_this_page) != head);

			if (nr_to_write > 0) {
				nr_to_write--;
				if (nr_to_write == 0 &&
				    wbc->sync_mode == WB_SYNC_NONE)
					/*
					 * We stop writing back only if we are
					 * not doing integrity sync. In case of
					 * integrity sync we have to keep going
					 * because someone may be concurrently
					 * dirtying pages, and we might have
					 * synced a lot of newly appeared dirty
					 * pages, but have not synced all of the
					 * old dirty pages.
					 */
					goto out;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	return 0;
ret_extent_tail:
	ret = MPAGE_DA_EXTENT_TAIL;
out:
	pagevec_release(&pvec);
	cond_resched();
	return ret;
}


static int ext4_da_writepages(struct address_space *mapping,
			      struct writeback_control *wbc)
{
	pgoff_t	index;
	int range_whole = 0;
	handle_t *handle = NULL;
	struct mpage_da_data mpd;
	struct inode *inode = mapping->host;
	int pages_written = 0;
	unsigned int max_pages;
	int range_cyclic, cycled = 1, io_done = 0;
	int needed_blocks, ret = 0;
	long desired_nr_to_write, nr_to_writebump = 0;
	loff_t range_start = wbc->range_start;
	struct ext4_sb_info *sbi = EXT4_SB(mapping->host->i_sb);
	pgoff_t done_index = 0;
	pgoff_t end;
	struct blk_plug plug;

	trace_ext4_da_writepages(inode, wbc);

	/*
	 * No pages to write? This is mainly a kludge to avoid starting
	 * a transaction for special inodes like journal inode on last iput()
	 * because that could violate lock ordering on umount
	 */
	if (!mapping->nrpages || !mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		return 0;

	/*
	 * If the filesystem has aborted, it is read-only, so return
	 * right away instead of dumping stack traces later on that
	 * will obscure the real source of the problem.  We test
	 * EXT4_MF_FS_ABORTED instead of sb->s_flag's MS_RDONLY because
	 * the latter could be true if the filesystem is mounted
	 * read-only, and in that case, ext4_da_writepages should
	 * *never* be called, so if that ever happens, we would want
	 * the stack trace.
	 */
	if (unlikely(sbi->s_mount_flags & EXT4_MF_FS_ABORTED))
		return -EROFS;

	if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
		range_whole = 1;

	range_cyclic = wbc->range_cyclic;
	if (wbc->range_cyclic) {
		index = mapping->writeback_index;
		if (index)
			cycled = 0;
		wbc->range_start = index << PAGE_CACHE_SHIFT;
		wbc->range_end  = LLONG_MAX;
		wbc->range_cyclic = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
	}

	/*
	 * This works around two forms of stupidity.  The first is in
	 * the writeback code, which caps the maximum number of pages
	 * written to be 1024 pages.  This is wrong on multiple
	 * levels; different architectues have a different page size,
	 * which changes the maximum amount of data which gets
	 * written.  Secondly, 4 megabytes is way too small.  XFS
	 * forces this value to be 16 megabytes by multiplying
	 * nr_to_write parameter by four, and then relies on its
	 * allocator to allocate larger extents to make them
	 * contiguous.  Unfortunately this brings us to the second
	 * stupidity, which is that ext4's mballoc code only allocates
	 * at most 2048 blocks.  So we force contiguous writes up to
	 * the number of dirty blocks in the inode, or
	 * sbi->max_writeback_mb_bump whichever is smaller.
	 */
	max_pages = sbi->s_max_writeback_mb_bump << (20 - PAGE_CACHE_SHIFT);
	if (!range_cyclic && range_whole) {
		if (wbc->nr_to_write == LONG_MAX)
			desired_nr_to_write = wbc->nr_to_write;
		else
			desired_nr_to_write = wbc->nr_to_write * 8;
	} else
		desired_nr_to_write = ext4_num_dirty_pages(inode, index,
							   max_pages);
	if (desired_nr_to_write > max_pages)
		desired_nr_to_write = max_pages;

	if (wbc->nr_to_write < desired_nr_to_write) {
		nr_to_writebump = desired_nr_to_write - wbc->nr_to_write;
		wbc->nr_to_write = desired_nr_to_write;
	}

retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);

	blk_start_plug(&plug);
	while (!ret && wbc->nr_to_write > 0) {

		/*
		 * we  insert one extent at a time. So we need
		 * credit needed for single extent allocation.
		 * journalled mode is currently not supported
		 * by delalloc
		 */
		BUG_ON(ext4_should_journal_data(inode));
		needed_blocks = ext4_da_writepages_trans_blocks(inode);

		/* start a new transaction*/
		handle = ext4_journal_start(inode, EXT4_HT_WRITE_PAGE,
					    needed_blocks);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			ext4_msg(inode->i_sb, KERN_CRIT, "%s: jbd2_start: "
			       "%ld pages, ino %lu; err %d", __func__,
				wbc->nr_to_write, inode->i_ino, ret);
			blk_finish_plug(&plug);
			goto out_writepages;
		}

		/*
		 * Now call write_cache_pages_da() to find the next
		 * contiguous region of logical blocks that need
		 * blocks to be allocated by ext4 and submit them.
		 */
		ret = write_cache_pages_da(handle, mapping,
					   wbc, &mpd, &done_index);
		/*
		 * If we have a contiguous extent of pages and we
		 * haven't done the I/O yet, map the blocks and submit
		 * them for I/O.
		 */
		if (!mpd.io_done && mpd.next_page != mpd.first_page) {
			mpage_da_map_and_submit(&mpd);
			ret = MPAGE_DA_EXTENT_TAIL;
		}
		trace_ext4_da_write_pages(inode, &mpd);
		wbc->nr_to_write -= mpd.pages_written;

		ext4_journal_stop(handle);

		if ((mpd.retval == -ENOSPC) && sbi->s_journal) {
			/* commit the transaction which would
			 * free blocks released in the transaction
			 * and try again
			 */
			jbd2_journal_force_commit_nested(sbi->s_journal);
			ret = 0;
		} else if (ret == MPAGE_DA_EXTENT_TAIL) {
			/*
			 * Got one extent now try with rest of the pages.
			 * If mpd.retval is set -EIO, journal is aborted.
			 * So we don't need to write any more.
			 */
			pages_written += mpd.pages_written;
			ret = mpd.retval;
			io_done = 1;
		} else if (wbc->nr_to_write)
			/*
			 * There is no more writeout needed
			 * or we requested for a noblocking writeout
			 * and we found the device congested
			 */
			break;
	}
	blk_finish_plug(&plug);
	if (!io_done && !cycled) {
		cycled = 1;
		index = 0;
		wbc->range_start = index << PAGE_CACHE_SHIFT;
		wbc->range_end  = mapping->writeback_index - 1;
		goto retry;
	}

	/* Update index */
	wbc->range_cyclic = range_cyclic;
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		/*
		 * set the writeback_index so that range_cyclic
		 * mode will write it back later
		 */
		mapping->writeback_index = done_index;

out_writepages:
	wbc->nr_to_write -= nr_to_writebump;
	wbc->range_start = range_start;
	trace_ext4_da_writepages_result(inode, wbc, ret, pages_written);
	return ret;
}

static int ext4_nonda_switch(struct super_block *sb)
{
	s64 free_clusters, dirty_clusters;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	/*
	 * switch to non delalloc mode if we are running low
	 * on free block. The free block accounting via percpu
	 * counters can get slightly wrong with percpu_counter_batch getting
	 * accumulated on each CPU without updating global counters
	 * Delalloc need an accurate free block accounting. So switch
	 * to non delalloc when we are near to error range.
	 */
	free_clusters =
		percpu_counter_read_positive(&sbi->s_freeclusters_counter);
	dirty_clusters =
		percpu_counter_read_positive(&sbi->s_dirtyclusters_counter);
	/*
	 * Start pushing delalloc when 1/2 of free blocks are dirty.
	 */
	if (dirty_clusters && (free_clusters < 2 * dirty_clusters))
		try_to_writeback_inodes_sb(sb, WB_REASON_FS_FREE_SPACE);

	if (2 * free_clusters < 3 * dirty_clusters ||
	    free_clusters < (dirty_clusters + EXT4_FREECLUSTERS_WATERMARK)) {
		/*
		 * free block count is less than 150% of dirty blocks
		 * or free blocks is less than watermark
		 */
		return 1;
	}
	return 0;
}

/* We always reserve for an inode update; the superblock could be there too */
static int ext4_da_write_credits(struct inode *inode, loff_t pos, unsigned len)
{
	if (likely(EXT4_HAS_RO_COMPAT_FEATURE(inode->i_sb,
				EXT4_FEATURE_RO_COMPAT_LARGE_FILE)))
		return 1;

	if (pos + len <= 0x7fffffffULL)
		return 1;

	/* We might need to update the superblock to set LARGE_FILE */
	return 2;
}

static int ext4_da_write_begin(struct file *file, struct address_space *mapping,
			       loff_t pos, unsigned len, unsigned flags,
			       struct page **pagep, void **fsdata)
{
	int ret, retries = 0;
	struct page *page;
	pgoff_t index;
	struct inode *inode = mapping->host;
	handle_t *handle;

	index = pos >> PAGE_CACHE_SHIFT;

	if (ext4_nonda_switch(inode->i_sb)) {
		*fsdata = (void *)FALL_BACK_TO_NONDELALLOC;
		return ext4_write_begin(file, mapping, pos,
					len, flags, pagep, fsdata);
	}
	*fsdata = (void *)0;
	trace_ext4_da_write_begin(inode, pos, len, flags);

	if (ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA)) {
		ret = ext4_da_write_inline_data_begin(mapping, inode,
						      pos, len, flags,
						      pagep, fsdata);
		if (ret < 0)
			return ret;
		if (ret == 1)
			return 0;
	}

	/*
	 * grab_cache_page_write_begin() can take a long time if the
	 * system is thrashing due to memory pressure, or if the page
	 * is being written back.  So grab it first before we start
	 * the transaction handle.  This also allows us to allocate
	 * the page (if needed) without using GFP_NOFS.
	 */
retry_grab:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	unlock_page(page);

	/*
	 * With delayed allocation, we don't log the i_disksize update
	 * if there is delayed block allocation. But we still need
	 * to journalling the i_disksize update if writes to the end
	 * of file which has an already mapped buffer.
	 */
retry_journal:
	handle = ext4_journal_start(inode, EXT4_HT_WRITE_PAGE,
				ext4_da_write_credits(inode, pos, len));
	if (IS_ERR(handle)) {
		page_cache_release(page);
		return PTR_ERR(handle);
	}

	lock_page(page);
	if (page->mapping != mapping) {
		/* The page got truncated from under us */
		unlock_page(page);
		page_cache_release(page);
		ext4_journal_stop(handle);
		goto retry_grab;
	}
	/* In case writeback began while the page was unlocked */
	wait_for_stable_page(page);

	ret = __block_write_begin(page, pos, len, ext4_da_get_block_prep);
	if (ret < 0) {
		unlock_page(page);
		ext4_journal_stop(handle);
		/*
		 * block_write_begin may have instantiated a few blocks
		 * outside i_size.  Trim these off again. Don't need
		 * i_size_read because we hold i_mutex.
		 */
		if (pos + len > inode->i_size)
			ext4_truncate_failed_write(inode);

		if (ret == -ENOSPC &&
		    ext4_should_retry_alloc(inode->i_sb, &retries))
			goto retry_journal;

		page_cache_release(page);
		return ret;
	}

	*pagep = page;
	return ret;
}

/*
 * Check if we should update i_disksize
 * when write to the end of file but not require block allocation
 */
static int ext4_da_should_update_i_disksize(struct page *page,
					    unsigned long offset)
{
	struct buffer_head *bh;
	struct inode *inode = page->mapping->host;
	unsigned int idx;
	int i;

	bh = page_buffers(page);
	idx = offset >> inode->i_blkbits;

	for (i = 0; i < idx; i++)
		bh = bh->b_this_page;

	if (!buffer_mapped(bh) || (buffer_delay(bh)) || buffer_unwritten(bh))
		return 0;
	return 1;
}

static int ext4_da_write_end(struct file *file,
			     struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned copied,
			     struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	int ret = 0, ret2;
	handle_t *handle = ext4_journal_current_handle();
	loff_t new_i_size;
	unsigned long start, end;
	int write_mode = (int)(unsigned long)fsdata;

	if (write_mode == FALL_BACK_TO_NONDELALLOC)
		return ext4_write_end(file, mapping, pos,
				      len, copied, page, fsdata);

	trace_ext4_da_write_end(inode, pos, len, copied);
	start = pos & (PAGE_CACHE_SIZE - 1);
	end = start + copied - 1;

	/*
	 * generic_write_end() will run mark_inode_dirty() if i_size
	 * changes.  So let's piggyback the i_disksize mark_inode_dirty
	 * into that.
	 */
	new_i_size = pos + copied;
	if (copied && new_i_size > EXT4_I(inode)->i_disksize) {
		if (ext4_has_inline_data(inode) ||
		    ext4_da_should_update_i_disksize(page, end)) {
			down_write(&EXT4_I(inode)->i_data_sem);
			if (new_i_size > EXT4_I(inode)->i_disksize)
				EXT4_I(inode)->i_disksize = new_i_size;
			up_write(&EXT4_I(inode)->i_data_sem);
			/* We need to mark inode dirty even if
			 * new_i_size is less that inode->i_size
			 * bu greater than i_disksize.(hint delalloc)
			 */
			ext4_mark_inode_dirty(handle, inode);
		}
	}

	if (write_mode != CONVERT_INLINE_DATA &&
	    ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA) &&
	    ext4_has_inline_data(inode))
		ret2 = ext4_da_write_inline_data_end(inode, pos, len, copied,
						     page);
	else
		ret2 = generic_write_end(file, mapping, pos, len, copied,
							page, fsdata);

	copied = ret2;
	if (ret2 < 0)
		ret = ret2;
	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;

	return ret ? ret : copied;
}

static void ext4_da_invalidatepage(struct page *page, unsigned long offset)
{
	/*
	 * Drop reserved blocks
	 */
	BUG_ON(!PageLocked(page));
	if (!page_has_buffers(page))
		goto out;

	ext4_da_page_release_reservation(page, offset);

out:
	ext4_invalidatepage(page, offset);

	return;
}

/*
 * Force all delayed allocation blocks to be allocated for a given inode.
 */
int ext4_alloc_da_blocks(struct inode *inode)
{
	trace_ext4_alloc_da_blocks(inode);

	if (!EXT4_I(inode)->i_reserved_data_blocks &&
	    !EXT4_I(inode)->i_reserved_meta_blocks)
		return 0;

	/*
	 * We do something simple for now.  The filemap_flush() will
	 * also start triggering a write of the data blocks, which is
	 * not strictly speaking necessary (and for users of
	 * laptop_mode, not even desirable).  However, to do otherwise
	 * would require replicating code paths in:
	 *
	 * ext4_da_writepages() ->
	 *    write_cache_pages() ---> (via passed in callback function)
	 *        __mpage_da_writepage() -->
	 *           mpage_add_bh_to_extent()
	 *           mpage_da_map_blocks()
	 *
	 * The problem is that write_cache_pages(), located in
	 * mm/page-writeback.c, marks pages clean in preparation for
	 * doing I/O, which is not desirable if we're not planning on
	 * doing I/O at all.
	 *
	 * We could call write_cache_pages(), and then redirty all of
	 * the pages by calling redirty_page_for_writepage() but that
	 * would be ugly in the extreme.  So instead we would need to
	 * replicate parts of the code in the above functions,
	 * simplifying them because we wouldn't actually intend to
	 * write out the pages, but rather only collect contiguous
	 * logical block extents, call the multi-block allocator, and
	 * then update the buffer heads with the block allocations.
	 *
	 * For now, though, we'll cheat by calling filemap_flush(),
	 * which will map the blocks, and start the I/O, but not
	 * actually wait for the I/O to complete.
	 */
	return filemap_flush(inode->i_mapping);
}

/*
 * bmap() is special.  It gets used by applications such as lilo and by
 * the swapper to find the on-disk block of a specific piece of data.
 *
 * Naturally, this is dangerous if the block concerned is still in the
 * journal.  If somebody makes a swapfile on an ext4 data-journaling
 * filesystem and enables swap, then they may get a nasty shock when the
 * data getting swapped to that swapfile suddenly gets overwritten by
 * the original zero's written out previously to the journal and
 * awaiting writeback in the kernel's buffer cache.
 *
 * So, if we see any bmap calls here on a modified, data-journaled file,
 * take extra steps to flush any blocks which might be in the cache.
 */
static sector_t ext4_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;
	journal_t *journal;
	int err;

	/*
	 * We can get here for an inline file via the FIBMAP ioctl
	 */
	if (ext4_has_inline_data(inode))
		return 0;

	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) &&
			test_opt(inode->i_sb, DELALLOC)) {
		/*
		 * With delalloc we want to sync the file
		 * so that we can make sure we allocate
		 * blocks for file
		 */
		filemap_write_and_wait(mapping);
	}

	if (EXT4_JOURNAL(inode) &&
	    ext4_test_inode_state(inode, EXT4_STATE_JDATA)) {
		/*
		 * This is a REALLY heavyweight approach, but the use of
		 * bmap on dirty files is expected to be extremely rare:
		 * only if we run lilo or swapon on a freshly made file
		 * do we expect this to happen.
		 *
		 * (bmap requires CAP_SYS_RAWIO so this does not
		 * represent an unprivileged user DOS attack --- we'd be
		 * in trouble if mortal users could trigger this path at
		 * will.)
		 *
		 * NB. EXT4_STATE_JDATA is not set on files other than
		 * regular files.  If somebody wants to bmap a directory
		 * or symlink and gets confused because the buffer
		 * hasn't yet been flushed to disk, they deserve
		 * everything they get.
		 */

		ext4_clear_inode_state(inode, EXT4_STATE_JDATA);
		journal = EXT4_JOURNAL(inode);
		jbd2_journal_lock_updates(journal);
		err = jbd2_journal_flush(journal);
		jbd2_journal_unlock_updates(journal);

		if (err)
			return 0;
	}

	return generic_block_bmap(mapping, block, ext4_get_block);
}

static int ext4_readpage(struct file *file, struct page *page)
{
	int ret = -EAGAIN;
	struct inode *inode = page->mapping->host;

	trace_ext4_readpage(page);

	if (ext4_has_inline_data(inode))
		ret = ext4_readpage_inline(inode, page);

	if (ret == -EAGAIN)
		return mpage_readpage(page, ext4_get_block);

	return ret;
}

static int
ext4_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	struct inode *inode = mapping->host;

	/* If the file has inline data, no need to do readpages. */
	if (ext4_has_inline_data(inode))
		return 0;

	return mpage_readpages(mapping, pages, nr_pages, ext4_get_block);
}

static void ext4_invalidatepage(struct page *page, unsigned long offset)
{
	trace_ext4_invalidatepage(page, offset);

	/* No journalling happens on data buffers when this function is used */
	WARN_ON(page_has_buffers(page) && buffer_jbd(page_buffers(page)));

	block_invalidatepage(page, offset);
}

static int __ext4_journalled_invalidatepage(struct page *page,
					    unsigned long offset)
{
	journal_t *journal = EXT4_JOURNAL(page->mapping->host);

	trace_ext4_journalled_invalidatepage(page, offset);

	/*
	 * If it's a full truncate we just forget about the pending dirtying
	 */
	if (offset == 0)
		ClearPageChecked(page);

	return jbd2_journal_invalidatepage(journal, page, offset);
}

/* Wrapper for aops... */
static void ext4_journalled_invalidatepage(struct page *page,
					   unsigned long offset)
{
	WARN_ON(__ext4_journalled_invalidatepage(page, offset) < 0);
}

static int ext4_releasepage(struct page *page, gfp_t wait)
{
	journal_t *journal = EXT4_JOURNAL(page->mapping->host);

	trace_ext4_releasepage(page);

	/* Page has dirty journalled data -> cannot release */
	if (PageChecked(page))
		return 0;
	if (journal)
		return jbd2_journal_try_to_free_buffers(journal, page, wait);
	else
		return try_to_free_buffers(page);
}

/*
 * ext4_get_block used when preparing for a DIO write or buffer write.
 * We allocate an uinitialized extent if blocks haven't been allocated.
 * The extent will be converted to initialized after the IO is complete.
 */
int ext4_get_block_write(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
	ext4_debug("ext4_get_block_write: inode %lu, create flag %d\n",
		   inode->i_ino, create);
	return _ext4_get_block(inode, iblock, bh_result,
			       EXT4_GET_BLOCKS_IO_CREATE_EXT);
}

static int ext4_get_block_write_nolock(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
	ext4_debug("ext4_get_block_write_nolock: inode %lu, create flag %d\n",
		   inode->i_ino, create);
	return _ext4_get_block(inode, iblock, bh_result,
			       EXT4_GET_BLOCKS_NO_LOCK);
}

static void ext4_end_io_dio(struct kiocb *iocb, loff_t offset,
			    ssize_t size, void *private, int ret,
			    bool is_async)
{
	struct inode *inode = file_inode(iocb->ki_filp);
        ext4_io_end_t *io_end = iocb->private;

	/* if not async direct IO or dio with 0 bytes write, just return */
	if (!io_end || !size)
		goto out;

	ext_debug("ext4_end_io_dio(): io_end 0x%p "
		  "for inode %lu, iocb 0x%p, offset %llu, size %zd\n",
 		  iocb->private, io_end->inode->i_ino, iocb, offset,
		  size);

	iocb->private = NULL;

	/* if not aio dio with unwritten extents, just free io and return */
	if (!(io_end->flag & EXT4_IO_END_UNWRITTEN)) {
		ext4_free_io_end(io_end);
out:
		inode_dio_done(inode);
		if (is_async)
			aio_complete(iocb, ret, 0);
		return;
	}

	io_end->offset = offset;
	io_end->size = size;
	if (is_async) {
		io_end->iocb = iocb;
		io_end->result = ret;
	}

	ext4_add_complete_io(io_end);
}

/*
 * For ext4 extent files, ext4 will do direct-io write to holes,
 * preallocated extents, and those write extend the file, no need to
 * fall back to buffered IO.
 *
 * For holes, we fallocate those blocks, mark them as uninitialized
 * If those blocks were preallocated, we mark sure they are split, but
 * still keep the range to write as uninitialized.
 *
 * The unwritten extents will be converted to written when DIO is completed.
 * For async direct IO, since the IO may still pending when return, we
 * set up an end_io call back function, which will do the conversion
 * when async direct IO completed.
 *
 * If the O_DIRECT write will extend the file then add this inode to the
 * orphan list.  So recovery will truncate it back to the original size
 * if the machine crashes during the write.
 *
 */
static ssize_t ext4_ext_direct_IO(int rw, struct kiocb *iocb,
			      const struct iovec *iov, loff_t offset,
			      unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;
	size_t count = iov_length(iov, nr_segs);
	int overwrite = 0;
	get_block_t *get_block_func = NULL;
	int dio_flags = 0;
	loff_t final_size = offset + count;

	/* Use the old path for reads and writes beyond i_size. */
	if (rw != WRITE || final_size > inode->i_size)
		return ext4_ind_direct_IO(rw, iocb, iov, offset, nr_segs);

	BUG_ON(iocb->private == NULL);

	/* If we do a overwrite dio, i_mutex locking can be released */
	overwrite = *((int *)iocb->private);

	if (overwrite) {
		atomic_inc(&inode->i_dio_count);
		down_read(&EXT4_I(inode)->i_data_sem);
		mutex_unlock(&inode->i_mutex);
	}

	/*
	 * We could direct write to holes and fallocate.
	 *
	 * Allocated blocks to fill the hole are marked as
	 * uninitialized to prevent parallel buffered read to expose
	 * the stale data before DIO complete the data IO.
	 *
	 * As to previously fallocated extents, ext4 get_block will
	 * just simply mark the buffer mapped but still keep the
	 * extents uninitialized.
	 *
	 * For non AIO case, we will convert those unwritten extents
	 * to written after return back from blockdev_direct_IO.
	 *
	 * For async DIO, the conversion needs to be deferred when the
	 * IO is completed. The ext4 end_io callback function will be
	 * called to take care of the conversion work.  Here for async
	 * case, we allocate an io_end structure to hook to the iocb.
	 */
	iocb->private = NULL;
	ext4_inode_aio_set(inode, NULL);
	if (!is_sync_kiocb(iocb)) {
		ext4_io_end_t *io_end = ext4_init_io_end(inode, GFP_NOFS);
		if (!io_end) {
			ret = -ENOMEM;
			goto retake_lock;
		}
		io_end->flag |= EXT4_IO_END_DIRECT;
		iocb->private = io_end;
		/*
		 * we save the io structure for current async direct
		 * IO, so that later ext4_map_blocks() could flag the
		 * io structure whether there is a unwritten extents
		 * needs to be converted when IO is completed.
		 */
		ext4_inode_aio_set(inode, io_end);
	}

	if (overwrite) {
		get_block_func = ext4_get_block_write_nolock;
	} else {
		get_block_func = ext4_get_block_write;
		dio_flags = DIO_LOCKING;
	}
	ret = __blockdev_direct_IO(rw, iocb, inode,
				   inode->i_sb->s_bdev, iov,
				   offset, nr_segs,
				   get_block_func,
				   ext4_end_io_dio,
				   NULL,
				   dio_flags);

	if (iocb->private)
		ext4_inode_aio_set(inode, NULL);
	/*
	 * The io_end structure takes a reference to the inode, that
	 * structure needs to be destroyed and the reference to the
	 * inode need to be dropped, when IO is complete, even with 0
	 * byte write, or failed.
	 *
	 * In the successful AIO DIO case, the io_end structure will
	 * be destroyed and the reference to the inode will be dropped
	 * after the end_io call back function is called.
	 *
	 * In the case there is 0 byte write, or error case, since VFS
	 * direct IO won't invoke the end_io call back function, we
	 * need to free the end_io structure here.
	 */
	if (ret != -EIOCBQUEUED && ret <= 0 && iocb->private) {
		ext4_free_io_end(iocb->private);
		iocb->private = NULL;
	} else if (ret > 0 && !overwrite && ext4_test_inode_state(inode,
						EXT4_STATE_DIO_UNWRITTEN)) {
		int err;
		/*
		 * for non AIO case, since the IO is already
		 * completed, we could do the conversion right here
		 */
		err = ext4_convert_unwritten_extents(inode,
						     offset, ret);
		if (err < 0)
			ret = err;
		ext4_clear_inode_state(inode, EXT4_STATE_DIO_UNWRITTEN);
	}

retake_lock:
	/* take i_mutex locking again if we do a ovewrite dio */
	if (overwrite) {
		inode_dio_done(inode);
		up_read(&EXT4_I(inode)->i_data_sem);
		mutex_lock(&inode->i_mutex);
	}

	return ret;
}

static ssize_t ext4_direct_IO(int rw, struct kiocb *iocb,
			      const struct iovec *iov, loff_t offset,
			      unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	/*
	 * If we are doing data journalling we don't support O_DIRECT
	 */
	if (ext4_should_journal_data(inode))
		return 0;

	/* Let buffer I/O handle the inline data case. */
	if (ext4_has_inline_data(inode))
		return 0;

	trace_ext4_direct_IO_enter(inode, offset, iov_length(iov, nr_segs), rw);
	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		ret = ext4_ext_direct_IO(rw, iocb, iov, offset, nr_segs);
	else
		ret = ext4_ind_direct_IO(rw, iocb, iov, offset, nr_segs);
	trace_ext4_direct_IO_exit(inode, offset,
				iov_length(iov, nr_segs), rw, ret);
	return ret;
}

/*
 * Pages can be marked dirty completely asynchronously from ext4's journalling
 * activity.  By filemap_sync_pte(), try_to_unmap_one(), etc.  We cannot do
 * much here because ->set_page_dirty is called under VFS locks.  The page is
 * not necessarily locked.
 *
 * We cannot just dirty the page and leave attached buffers clean, because the
 * buffers' dirty state is "definitive".  We cannot just set the buffers dirty
 * or jbddirty because all the journalling code will explode.
 *
 * So what we do is to mark the page "pending dirty" and next time writepage
 * is called, propagate that into the buffers appropriately.
 */
static int ext4_journalled_set_page_dirty(struct page *page)
{
	SetPageChecked(page);
	return __set_page_dirty_nobuffers(page);
}

static const struct address_space_operations ext4_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.write_begin		= ext4_write_begin,
	.write_end		= ext4_write_end,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_invalidatepage,
	.releasepage		= ext4_releasepage,
	.direct_IO		= ext4_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext4_journalled_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.write_begin		= ext4_write_begin,
	.write_end		= ext4_journalled_write_end,
	.set_page_dirty		= ext4_journalled_set_page_dirty,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_journalled_invalidatepage,
	.releasepage		= ext4_releasepage,
	.direct_IO		= ext4_direct_IO,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

static const struct address_space_operations ext4_da_aops = {
	.readpage		= ext4_readpage,
	.readpages		= ext4_readpages,
	.writepage		= ext4_writepage,
	.writepages		= ext4_da_writepages,
	.write_begin		= ext4_da_write_begin,
	.write_end		= ext4_da_write_end,
	.bmap			= ext4_bmap,
	.invalidatepage		= ext4_da_invalidatepage,
	.releasepage		= ext4_releasepage,
	.direct_IO		= ext4_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};

void ext4_set_aops(struct inode *inode)
{
	switch (ext4_inode_journal_mode(inode)) {
	case EXT4_INODE_ORDERED_DATA_MODE:
		ext4_set_inode_state(inode, EXT4_STATE_ORDERED_MODE);
		break;
	case EXT4_INODE_WRITEBACK_DATA_MODE:
		ext4_clear_inode_state(inode, EXT4_STATE_ORDERED_MODE);
		break;
	case EXT4_INODE_JOURNAL_DATA_MODE:
		inode->i_mapping->a_ops = &ext4_journalled_aops;
		return;
	default:
		BUG();
	}
	if (test_opt(inode->i_sb, DELALLOC))
		inode->i_mapping->a_ops = &ext4_da_aops;
	else
		inode->i_mapping->a_ops = &ext4_aops;
}


/*
 * ext4_discard_partial_page_buffers()
 * Wrapper function for ext4_discard_partial_page_buffers_no_lock.
 * This function finds and locks the page containing the offset
 * "from" and passes it to ext4_discard_partial_page_buffers_no_lock.
 * Calling functions that already have the page locked should call
 * ext4_discard_partial_page_buffers_no_lock directly.
 */
int ext4_discard_partial_page_buffers(handle_t *handle,
		struct address_space *mapping, loff_t from,
		loff_t length, int flags)
{
	struct inode *inode = mapping->host;
	struct page *page;
	int err = 0;

	page = find_or_create_page(mapping, from >> PAGE_CACHE_SHIFT,
				   mapping_gfp_mask(mapping) & ~__GFP_FS);
	if (!page)
		return -ENOMEM;

	err = ext4_discard_partial_page_buffers_no_lock(handle, inode, page,
		from, length, flags);

	unlock_page(page);
	page_cache_release(page);
	return err;
}

/*
 * ext4_discard_partial_page_buffers_no_lock()
 * Zeros a page range of length 'length' starting from offset 'from'.
 * Buffer heads that correspond to the block aligned regions of the
 * zeroed range will be unmapped.  Unblock aligned regions
 * will have the corresponding buffer head mapped if needed so that
 * that region of the page can be updated with the partial zero out.
 *
 * This function assumes that the page has already been  locked.  The
 * The range to be discarded must be contained with in the given page.
 * If the specified range exceeds the end of the page it will be shortened
 * to the end of the page that corresponds to 'from'.  This function is
 * appropriate for updating a page and it buffer heads to be unmapped and
 * zeroed for blocks that have been either released, or are going to be
 * released.
 *
 * handle: The journal handle
 * inode:  The files inode
 * page:   A locked page that contains the offset "from"
 * from:   The starting byte offset (from the beginning of the file)
 *         to begin discarding
 * len:    The length of bytes to discard
 * flags:  Optional flags that may be used:
 *
 *         EXT4_DISCARD_PARTIAL_PG_ZERO_UNMAPPED
 *         Only zero the regions of the page whose buffer heads
 *         have already been unmapped.  This flag is appropriate
 *         for updating the contents of a page whose blocks may
 *         have already been released, and we only want to zero
 *         out the regions that correspond to those released blocks.
 *
 * Returns zero on success or negative on failure.
 */
static int ext4_discard_partial_page_buffers_no_lock(handle_t *handle,
		struct inode *inode, struct page *page, loff_t from,
		loff_t length, int flags)
{
	ext4_fsblk_t index = from >> PAGE_CACHE_SHIFT;
	unsigned int offset = from & (PAGE_CACHE_SIZE-1);
	unsigned int blocksize, max, pos;
	ext4_lblk_t iblock;
	struct buffer_head *bh;
	int err = 0;

	blocksize = inode->i_sb->s_blocksize;
	max = PAGE_CACHE_SIZE - offset;

	if (index != page->index)
		return -EINVAL;

	/*
	 * correct length if it does not fall between
	 * 'from' and the end of the page
	 */
	if (length > max || length < 0)
		length = max;

	iblock = index << (PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);

	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	/* Find the buffer that contains "offset" */
	bh = page_buffers(page);
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	pos = offset;
	while (pos < offset + length) {
		unsigned int end_of_block, range_to_discard;

		err = 0;

		/* The length of space left to zero and unmap */
		range_to_discard = offset + length - pos;

		/* The length of space until the end of the block */
		end_of_block = blocksize - (pos & (blocksize-1));

		/*
		 * Do not unmap or zero past end of block
		 * for this buffer head
		 */
		if (range_to_discard > end_of_block)
			range_to_discard = end_of_block;


		/*
		 * Skip this buffer head if we are only zeroing unampped
		 * regions of the page
		 */
		if (flags & EXT4_DISCARD_PARTIAL_PG_ZERO_UNMAPPED &&
			buffer_mapped(bh))
				goto next;

		/* If the range is block aligned, unmap */
		if (range_to_discard == blocksize) {
			clear_buffer_dirty(bh);
			bh->b_bdev = NULL;
			clear_buffer_mapped(bh);
			clear_buffer_req(bh);
			clear_buffer_new(bh);
			clear_buffer_delay(bh);
			clear_buffer_unwritten(bh);
			clear_buffer_uptodate(bh);
			zero_user(page, pos, range_to_discard);
			BUFFER_TRACE(bh, "Buffer discarded");
			goto next;
		}

		/*
		 * If this block is not completely contained in the range
		 * to be discarded, then it is not going to be released. Because
		 * we need to keep this block, we need to make sure this part
		 * of the page is uptodate before we modify it by writeing
		 * partial zeros on it.
		 */
		if (!buffer_mapped(bh)) {
			/*
			 * Buffer head must be mapped before we can read
			 * from the block
			 */
			BUFFER_TRACE(bh, "unmapped");
			ext4_get_block(inode, iblock, bh, 0);
			/* unmapped? It's a hole - nothing to do */
			if (!buffer_mapped(bh)) {
				BUFFER_TRACE(bh, "still unmapped");
				goto next;
			}
		}

		/* Ok, it's mapped. Make sure it's up-to-date */
		if (PageUptodate(page))
			set_buffer_uptodate(bh);

		if (!buffer_uptodate(bh)) {
			err = -EIO;
			ll_rw_block(READ, 1, &bh);
			wait_on_buffer(bh);
			/* Uhhuh. Read error. Complain and punt.*/
			if (!buffer_uptodate(bh))
				goto next;
		}

		if (ext4_should_journal_data(inode)) {
			BUFFER_TRACE(bh, "get write access");
			err = ext4_journal_get_write_access(handle, bh);
			if (err)
				goto next;
		}

		zero_user(page, pos, range_to_discard);

		err = 0;
		if (ext4_should_journal_data(inode)) {
			err = ext4_handle_dirty_metadata(handle, inode, bh);
		} else
			mark_buffer_dirty(bh);

		BUFFER_TRACE(bh, "Partial buffer zeroed");
next:
		bh = bh->b_this_page;
		iblock++;
		pos += range_to_discard;
	}

	return err;
}

int ext4_can_truncate(struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		return 1;
	if (S_ISDIR(inode->i_mode))
		return 1;
	if (S_ISLNK(inode->i_mode))
		return !ext4_inode_is_fast_symlink(inode);
	return 0;
}

/*
 * ext4_punch_hole: punches a hole in a file by releaseing the blocks
 * associated with the given offset and length
 *
 * @inode:  File inode
 * @offset: The offset where the hole will begin
 * @len:    The length of the hole
 *
 * Returns: 0 on success or negative on failure
 */

int ext4_punch_hole(struct file *file, loff_t offset, loff_t length)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	ext4_lblk_t first_block, stop_block;
	struct address_space *mapping = inode->i_mapping;
	loff_t first_page, last_page, page_len;
	loff_t first_page_offset, last_page_offset;
	handle_t *handle;
	unsigned int credits;
	int ret = 0;

	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	if (EXT4_SB(sb)->s_cluster_ratio > 1) {
		/* TODO: Add support for bigalloc file systems */
		return -EOPNOTSUPP;
	}

	trace_ext4_punch_hole(inode, offset, length);

	/*
	 * Write out all dirty pages to avoid race conditions
	 * Then release them.
	 */
	if (mapping->nrpages && mapping_tagged(mapping, PAGECACHE_TAG_DIRTY)) {
		ret = filemap_write_and_wait_range(mapping, offset,
						   offset + length - 1);
		if (ret)
			return ret;
	}

	mutex_lock(&inode->i_mutex);
	/* It's not possible punch hole on append only file */
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode)) {
		ret = -EPERM;
		goto out_mutex;
	}
	if (IS_SWAPFILE(inode)) {
		ret = -ETXTBSY;
		goto out_mutex;
	}

	/* No need to punch hole beyond i_size */
	if (offset >= inode->i_size)
		goto out_mutex;

	/*
	 * If the hole extends beyond i_size, set the hole
	 * to end after the page that contains i_size
	 */
	if (offset + length > inode->i_size) {
		length = inode->i_size +
		   PAGE_CACHE_SIZE - (inode->i_size & (PAGE_CACHE_SIZE - 1)) -
		   offset;
	}

	first_page = (offset + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	last_page = (offset + length) >> PAGE_CACHE_SHIFT;

	first_page_offset = first_page << PAGE_CACHE_SHIFT;
	last_page_offset = last_page << PAGE_CACHE_SHIFT;

	/* Now release the pages */
	if (last_page_offset > first_page_offset) {
		truncate_pagecache_range(inode, first_page_offset,
					 last_page_offset - 1);
	}

	/* Wait all existing dio workers, newcomers will block on i_mutex */
	ext4_inode_block_unlocked_dio(inode);
	ret = ext4_flush_unwritten_io(inode);
	if (ret)
		goto out_dio;
	inode_dio_wait(inode);

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		credits = ext4_writepage_trans_blocks(inode);
	else
		credits = ext4_blocks_for_truncate(inode);
	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		ext4_std_error(sb, ret);
		goto out_dio;
	}

	/*
	 * Now we need to zero out the non-page-aligned data in the
	 * pages at the start and tail of the hole, and unmap the
	 * buffer heads for the block aligned regions of the page that
	 * were completely zeroed.
	 */
	if (first_page > last_page) {
		/*
		 * If the file space being truncated is contained
		 * within a page just zero out and unmap the middle of
		 * that page
		 */
		ret = ext4_discard_partial_page_buffers(handle,
			mapping, offset, length, 0);

		if (ret)
			goto out_stop;
	} else {
		/*
		 * zero out and unmap the partial page that contains
		 * the start of the hole
		 */
		page_len = first_page_offset - offset;
		if (page_len > 0) {
			ret = ext4_discard_partial_page_buffers(handle, mapping,
						offset, page_len, 0);
			if (ret)
				goto out_stop;
		}

		/*
		 * zero out and unmap the partial page that contains
		 * the end of the hole
		 */
		page_len = offset + length - last_page_offset;
		if (page_len > 0) {
			ret = ext4_discard_partial_page_buffers(handle, mapping,
					last_page_offset, page_len, 0);
			if (ret)
				goto out_stop;
		}
	}

	/*
	 * If i_size is contained in the last page, we need to
	 * unmap and zero the partial page after i_size
	 */
	if (inode->i_size >> PAGE_CACHE_SHIFT == last_page &&
	   inode->i_size % PAGE_CACHE_SIZE != 0) {
		page_len = PAGE_CACHE_SIZE -
			(inode->i_size & (PAGE_CACHE_SIZE - 1));

		if (page_len > 0) {
			ret = ext4_discard_partial_page_buffers(handle,
					mapping, inode->i_size, page_len, 0);

			if (ret)
				goto out_stop;
		}
	}

	first_block = (offset + sb->s_blocksize - 1) >>
		EXT4_BLOCK_SIZE_BITS(sb);
	stop_block = (offset + length) >> EXT4_BLOCK_SIZE_BITS(sb);

	/* If there are no blocks to remove, return now */
	if (first_block >= stop_block)
		goto out_stop;

	down_write(&EXT4_I(inode)->i_data_sem);
	ext4_discard_preallocations(inode);

	ret = ext4_es_remove_extent(inode, first_block,
				    stop_block - first_block);
	if (ret) {
		up_write(&EXT4_I(inode)->i_data_sem);
		goto out_stop;
	}

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		ret = ext4_ext_remove_space(inode, first_block,
					    stop_block - 1);
	else
		ret = ext4_free_hole_blocks(handle, inode, first_block,
					    stop_block);

	ext4_discard_preallocations(inode);
	up_write(&EXT4_I(inode)->i_data_sem);
	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
	inode->i_mtime = inode->i_ctime = ext4_current_time(inode);
	ext4_mark_inode_dirty(handle, inode);
out_stop:
	ext4_journal_stop(handle);
out_dio:
	ext4_inode_resume_unlocked_dio(inode);
out_mutex:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

/*
 * ext4_truncate()
 *
 * We block out ext4_get_block() block instantiations across the entire
 * transaction, and VFS/VM ensures that ext4_truncate() cannot run
 * simultaneously on behalf of the same inode.
 *
 * As we work through the truncate and commit bits of it to the journal there
 * is one core, guiding principle: the file's tree must always be consistent on
 * disk.  We must be able to restart the truncate after a crash.
 *
 * The file's tree may be transiently inconsistent in memory (although it
 * probably isn't), but whenever we close off and commit a journal transaction,
 * the contents of (the filesystem + the journal) must be consistent and
 * restartable.  It's pretty simple, really: bottom up, right to left (although
 * left-to-right works OK too).
 *
 * Note that at recovery time, journal replay occurs *before* the restart of
 * truncate against the orphan inode list.
 *
 * The committed inode has the new, desired i_size (which is the same as
 * i_disksize in this case).  After a crash, ext4_orphan_cleanup() will see
 * that this inode's truncate did not complete and it will again call
 * ext4_truncate() to have another go.  So there will be instantiated blocks
 * to the right of the truncation point in a crashed ext4 filesystem.  But
 * that's fine - as long as they are linked from the inode, the post-crash
 * ext4_truncate() run will find them and release them.
 */
void ext4_truncate(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned int credits;
	handle_t *handle;
	struct address_space *mapping = inode->i_mapping;
	loff_t page_len;

	/*
	 * There is a possibility that we're either freeing the inode
	 * or it completely new indode. In those cases we might not
	 * have i_mutex locked because it's not necessary.
	 */
	if (!(inode->i_state & (I_NEW|I_FREEING)))
		WARN_ON(!mutex_is_locked(&inode->i_mutex));
	trace_ext4_truncate_enter(inode);

	if (!ext4_can_truncate(inode))
		return;

	ext4_clear_inode_flag(inode, EXT4_INODE_EOFBLOCKS);

	if (inode->i_size == 0 && !test_opt(inode->i_sb, NO_AUTO_DA_ALLOC))
		ext4_set_inode_state(inode, EXT4_STATE_DA_ALLOC_CLOSE);

	if (ext4_has_inline_data(inode)) {
		int has_inline = 1;

		ext4_inline_data_truncate(inode, &has_inline);
		if (has_inline)
			return;
	}

	/*
	 * finish any pending end_io work so we won't run the risk of
	 * converting any truncated blocks to initialized later
	 */
	ext4_flush_unwritten_io(inode);

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		credits = ext4_writepage_trans_blocks(inode);
	else
		credits = ext4_blocks_for_truncate(inode);

	handle = ext4_journal_start(inode, EXT4_HT_TRUNCATE, credits);
	if (IS_ERR(handle)) {
		ext4_std_error(inode->i_sb, PTR_ERR(handle));
		return;
	}

	if (inode->i_size % PAGE_CACHE_SIZE != 0) {
		page_len = PAGE_CACHE_SIZE -
			(inode->i_size & (PAGE_CACHE_SIZE - 1));

		if (ext4_discard_partial_page_buffers(handle,
				mapping, inode->i_size, page_len, 0))
			goto out_stop;
	}

	/*
	 * We add the inode to the orphan list, so that if this
	 * truncate spans multiple transactions, and we crash, we will
	 * resume the truncate when the filesystem recovers.  It also
	 * marks the inode dirty, to catch the new size.
	 *
	 * Implication: the file must always be in a sane, consistent
	 * truncatable state while each transaction commits.
	 */
	if (ext4_orphan_add(handle, inode))
		goto out_stop;

	down_write(&EXT4_I(inode)->i_data_sem);

	ext4_discard_preallocations(inode);

	if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))
		ext4_ext_truncate(handle, inode);
	else
		ext4_ind_truncate(handle, inode);

	up_write(&ei->i_data_sem);

	if (IS_SYNC(inode))
		ext4_handle_sync(handle);

out_stop:
	/*
	 * If this was a simple ftruncate() and the file will remain alive,
	 * then we need to clear up the orphan record which we created above.
	 * However, if this was a real unlink then we were called by
	 * ext4_delete_inode(), and we allow that function to clean up the
	 * orphan info for us.
	 */
	if (inode->i_nlink)
		ext4_orphan_del(handle, inode);

	inode->i_mtime = inode->i_ctime = ext4_current_time(inode);
	ext4_mark_inode_dirty(handle, inode);
	ext4_journal_stop(handle);

	trace_ext4_truncate_exit(inode);
}

/*
 * ext4_get_inode_loc returns with an extra refcount against the inode's
 * underlying buffer_head on success. If 'in_mem' is true, we have all
 * data in memory that is needed to recreate the on-disk version of this
 * inode.
 */
static int __ext4_get_inode_loc(struct inode *inode,
				struct ext4_iloc *iloc, int in_mem)
{
	struct ext4_group_desc	*gdp;
	struct buffer_head	*bh;
	struct super_block	*sb = inode->i_sb;
	ext4_fsblk_t		block;
	int			inodes_per_block, inode_offset;

	iloc->bh = NULL;
	if (!ext4_valid_inum(sb, inode->i_ino))
		return -EIO;

	iloc->block_group = (inode->i_ino - 1) / EXT4_INODES_PER_GROUP(sb);
	gdp = ext4_get_group_desc(sb, iloc->block_group, NULL);
	if (!gdp)
		return -EIO;

	/*
	 * Figure out the offset within the block group inode table
	 */
	inodes_per_block = EXT4_SB(sb)->s_inodes_per_block;
	inode_offset = ((inode->i_ino - 1) %
			EXT4_INODES_PER_GROUP(sb));
	block = ext4_inode_table(sb, gdp) + (inode_offset / inodes_per_block);
	iloc->offset = (inode_offset % inodes_per_block) * EXT4_INODE_SIZE(sb);

	bh = sb_getblk(sb, block);
	if (unlikely(!bh))
		return -ENOMEM;
	if (!buffer_uptodate(bh)) {
		lock_buffer(bh);

		/*
		 * If the buffer has the write error flag, we have failed
		 * to write out another inode in the same block.  In this
		 * case, we don't have to read the block because we may
		 * read the old inode data successfully.
		 */
		if (buffer_write_io_error(bh) && !buffer_uptodate(bh))
			set_buffer_uptodate(bh);

		if (buffer_uptodate(bh)) {
			/* someone brought it uptodate while we waited */
			unlock_buffer(bh);
			goto has_buffer;
		}

		/*
		 * If we have all information of the inode in memory and this
		 * is the only valid inode in the block, we need not read the
		 * block.
		 */
		if (in_mem) {
			struct buffer_head *bitmap_bh;
			int i, start;

			start = inode_offset & ~(inodes_per_block - 1);

			/* Is the inode bitmap in cache? */
			bitmap_bh = sb_getblk(sb, ext4_inode_bitmap(sb, gdp));
			if (unlikely(!bitmap_bh))
				goto make_io;

			/*
			 * If the inode bitmap isn't in cache then the
			 * optimisation may end up performing two reads instead
			 * of one, so skip it.
			 */
			if (!buffer_uptodate(bitmap_bh)) {
				brelse(bitmap_bh);
				goto make_io;
			}
			for (i = start; i < start + inodes_per_block; i++) {
				if (i == inode_offset)
					continue;
				if (ext4_test_bit(i, bitmap_bh->b_data))
					break;
			}
			brelse(bitmap_bh);
			if (i == start + inodes_per_block) {
				/* all other inodes are free, so skip I/O */
				memset(bh->b_data, 0, bh->b_size);
				set_buffer_uptodate(bh);
				unlock_buffer(bh);
				goto has_buffer;
			}
		}

make_io:
		/*
		 * If we need to do any I/O, try to pre-readahead extra
		 * blocks from the inode table.
		 */
		if (EXT4_SB(sb)->s_inode_readahead_blks) {
			ext4_fsblk_t b, end, table;
			unsigned num;
			__u32 ra_blks = EXT4_SB(sb)->s_inode_readahead_blks;

			table = ext4_inode_table(sb, gdp);
			/* s_inode_readahead_blks is always a power of 2 */
			b = block & ~((ext4_fsblk_t) ra_blks - 1);
			if (table > b)
				b = table;
			end = b + ra_blks;
			num = EXT4_INODES_PER_GROUP(sb);
			if (ext4_has_group_desc_csum(sb))
				num -= ext4_itable_unused_count(sb, gdp);
			table += num / inodes_per_block;
			if (end > table)
				end = table;
			while (b <= end)
				sb_breadahead(sb, b++);
		}

		/*
		 * There are other valid inodes in the buffer, this inode
		 * has in-inode xattrs, or we don't have this inode in memory.
		 * Read the block from disk.
		 */
		trace_ext4_load_inode(inode);
		get_bh(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ | REQ_META | REQ_PRIO, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			EXT4_ERROR_INODE_BLOCK(inode, block,
					       "unable to read itable block");
			brelse(bh);
			return -EIO;
		}
	}
has_buffer:
	iloc->bh = bh;
	return 0;
}

int ext4_get_inode_loc(struct inode *inode, struct ext4_iloc *iloc)
{
	/* We have all inode data except xattrs in memory here. */
	return __ext4_get_inode_loc(inode, iloc,
		!ext4_test_inode_state(inode, EXT4_STATE_XATTR));
}

void ext4_set_inode_flags(struct inode *inode)
{
	unsigned int flags = EXT4_I(inode)->i_flags;
	unsigned int new_fl = 0;

	if (flags & EXT4_SYNC_FL)
		new_fl |= S_SYNC;
	if (flags & EXT4_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & EXT4_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & EXT4_NOATIME_FL)
		new_fl |= S_NOATIME;
	if (flags & EXT4_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	set_mask_bits(&inode->i_flags,
		      S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC, new_fl);
}

/* Propagate flags from i_flags to EXT4_I(inode)->i_flags */
void ext4_get_inode_flags(struct ext4_inode_info *ei)
{
	unsigned int vfs_fl;
	unsigned long old_fl, new_fl;

	do {
		vfs_fl = ei->vfs_inode.i_flags;
		old_fl = ei->i_flags;
		new_fl = old_fl & ~(EXT4_SYNC_FL|EXT4_APPEND_FL|
				EXT4_IMMUTABLE_FL|EXT4_NOATIME_FL|
				EXT4_DIRSYNC_FL);
		if (vfs_fl & S_SYNC)
			new_fl |= EXT4_SYNC_FL;
		if (vfs_fl & S_APPEND)
			new_fl |= EXT4_APPEND_FL;
		if (vfs_fl & S_IMMUTABLE)
			new_fl |= EXT4_IMMUTABLE_FL;
		if (vfs_fl & S_NOATIME)
			new_fl |= EXT4_NOATIME_FL;
		if (vfs_fl & S_DIRSYNC)
			new_fl |= EXT4_DIRSYNC_FL;
	} while (cmpxchg(&ei->i_flags, old_fl, new_fl) != old_fl);
}

static blkcnt_t ext4_inode_blocks(struct ext4_inode *raw_inode,
				  struct ext4_inode_info *ei)
{
	blkcnt_t i_blocks ;
	struct inode *inode = &(ei->vfs_inode);
	struct super_block *sb = inode->i_sb;

	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_HUGE_FILE)) {
		/* we are using combined 48 bit field */
		i_blocks = ((u64)le16_to_cpu(raw_inode->i_blocks_high)) << 32 |
					le32_to_cpu(raw_inode->i_blocks_lo);
		if (ext4_test_inode_flag(inode, EXT4_INODE_HUGE_FILE)) {
			/* i_blocks represent file system block size */
			return i_blocks  << (inode->i_blkbits - 9);
		} else {
			return i_blocks;
		}
	} else {
		return le32_to_cpu(raw_inode->i_blocks_lo);
	}
}

static inline void ext4_iget_extra_inode(struct inode *inode,
					 struct ext4_inode *raw_inode,
					 struct ext4_inode_info *ei)
{
	__le32 *magic = (void *)raw_inode +
			EXT4_GOOD_OLD_INODE_SIZE + ei->i_extra_isize;
	if (*magic == cpu_to_le32(EXT4_XATTR_MAGIC)) {
		ext4_set_inode_state(inode, EXT4_STATE_XATTR);
		ext4_find_inline_data_nolock(inode);
	} else
		EXT4_I(inode)->i_inline_off = 0;
}

struct inode *ext4_iget(struct super_block *sb, unsigned long ino)
{
	struct ext4_iloc iloc;
	struct ext4_inode *raw_inode;
	struct ext4_inode_info *ei;
	struct inode *inode;
	journal_t *journal = EXT4_SB(sb)->s_journal;
	long ret;
	int block;
	uid_t i_uid;
	gid_t i_gid;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ei = EXT4_I(inode);
	iloc.bh = NULL;

	ret = __ext4_get_inode_loc(inode, &iloc, 0);
	if (ret < 0)
		goto bad_inode;
	raw_inode = ext4_raw_inode(&iloc);

	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE) {
		ei->i_extra_isize = le16_to_cpu(raw_inode->i_extra_isize);
		if (EXT4_GOOD_OLD_INODE_SIZE + ei->i_extra_isize >
		    EXT4_INODE_SIZE(inode->i_sb)) {
			EXT4_ERROR_INODE(inode, "bad extra_isize (%u != %u)",
				EXT4_GOOD_OLD_INODE_SIZE + ei->i_extra_isize,
				EXT4_INODE_SIZE(inode->i_sb));
			ret = -EIO;
			goto bad_inode;
		}
	} else
		ei->i_extra_isize = 0;

	/* Precompute checksum seed for inode metadata */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
			EXT4_FEATURE_RO_COMPAT_METADATA_CSUM)) {
		struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
		__u32 csum;
		__le32 inum = cpu_to_le32(inode->i_ino);
		__le32 gen = raw_inode->i_generation;
		csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&inum,
				   sizeof(inum));
		ei->i_csum_seed = ext4_chksum(sbi, csum, (__u8 *)&gen,
					      sizeof(gen));
	}

	if (!ext4_inode_csum_verify(inode, raw_inode, ei)) {
		EXT4_ERROR_INODE(inode, "checksum invalid");
		ret = -EIO;
		goto bad_inode;
	}

	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid_low);
	i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid_low);
	if (!(test_opt(inode->i_sb, NO_UID32))) {
		i_uid |= le16_to_cpu(raw_inode->i_uid_high) << 16;
		i_gid |= le16_to_cpu(raw_inode->i_gid_high) << 16;
	}
	i_uid_write(inode, i_uid);
	i_gid_write(inode, i_gid);
	set_nlink(inode, le16_to_cpu(raw_inode->i_links_count));

	ext4_clear_state_flags(ei);	/* Only relevant on 32-bit archs */
	ei->i_inline_off = 0;
	ei->i_dir_start_lookup = 0;
	ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);
	/* We now have enough fields to check if the inode was active or not.
	 * This is needed because nfsd might try to access dead inodes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (inode->i_nlink == 0) {
		if ((inode->i_mode == 0 ||
		     !(EXT4_SB(inode->i_sb)->s_mount_state & EXT4_ORPHAN_FS)) &&
		    ino != EXT4_BOOT_LOADER_INO) {
			/* this inode is deleted */
			ret = -ESTALE;
			goto bad_inode;
		}
		/* The only unlinked inodes we let through here have
		 * valid i_mode and are being read by the orphan
		 * recovery code: that's fine, we're about to complete
		 * the process of deleting those.
		 * OR it is the EXT4_BOOT_LOADER_INO which is
		 * not initialized on a new filesystem. */
	}
	ei->i_flags = le32_to_cpu(raw_inode->i_flags);
	inode->i_blocks = ext4_inode_blocks(raw_inode, ei);
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl_lo);
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_64BIT))
		ei->i_file_acl |=
			((__u64)le16_to_cpu(raw_inode->i_file_acl_high)) << 32;
	inode->i_size = ext4_isize(raw_inode);
	ei->i_disksize = inode->i_size;
#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = 0;
#endif
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);
	ei->i_block_group = iloc.block_group;
	ei->i_last_alloc_group = ~0;
	/*
	 * NOTE! The in-memory inode i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (block = 0; block < EXT4_N_BLOCKS; block++)
		ei->i_data[block] = raw_inode->i_block[block];
	INIT_LIST_HEAD(&ei->i_orphan);

	/*
	 * Set transaction id's of transactions that have to be committed
	 * to finish f[data]sync. We set them to currently running transaction
	 * as we cannot be sure that the inode or some of its metadata isn't
	 * part of the transaction - the inode could have been reclaimed and
	 * now it is reread from disk.
	 */
	if (journal) {
		transaction_t *transaction;
		tid_t tid;

		read_lock(&journal->j_state_lock);
		if (journal->j_running_transaction)
			transaction = journal->j_running_transaction;
		else
			transaction = journal->j_committing_transaction;
		if (transaction)
			tid = transaction->t_tid;
		else
			tid = journal->j_commit_sequence;
		read_unlock(&journal->j_state_lock);
		ei->i_sync_tid = tid;
		ei->i_datasync_tid = tid;
	}

	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE) {
		if (ei->i_extra_isize == 0) {
			/* The extra space is currently unused. Use it. */
			ei->i_extra_isize = sizeof(struct ext4_inode) -
					    EXT4_GOOD_OLD_INODE_SIZE;
		} else {
			ext4_iget_extra_inode(inode, raw_inode, ei);
		}
	}

	EXT4_INODE_GET_XTIME(i_ctime, inode, raw_inode);
	EXT4_INODE_GET_XTIME(i_mtime, inode, raw_inode);
	EXT4_INODE_GET_XTIME(i_atime, inode, raw_inode);
	EXT4_EINODE_GET_XTIME(i_crtime, ei, raw_inode);

	inode->i_version = le32_to_cpu(raw_inode->i_disk_version);
	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE) {
		if (EXT4_FITS_IN_INODE(raw_inode, ei, i_version_hi))
			inode->i_version |=
			(__u64)(le32_to_cpu(raw_inode->i_version_hi)) << 32;
	}

	ret = 0;
	if (ei->i_file_acl &&
	    !ext4_data_block_valid(EXT4_SB(sb), ei->i_file_acl, 1)) {
		EXT4_ERROR_INODE(inode, "bad extended attribute block %llu",
				 ei->i_file_acl);
		ret = -EIO;
		goto bad_inode;
	} else if (!ext4_has_inline_data(inode)) {
		if (ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)) {
			if ((S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			    (S_ISLNK(inode->i_mode) &&
			     !ext4_inode_is_fast_symlink(inode))))
				/* Validate extent which is part of inode */
				ret = ext4_ext_check_inode(inode);
		} else if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			   (S_ISLNK(inode->i_mode) &&
			    !ext4_inode_is_fast_symlink(inode))) {
			/* Validate block references which are part of inode */
			ret = ext4_ind_check_inode(inode);
		}
	}
	if (ret)
		goto bad_inode;

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = &ext4_file_operations;
		ext4_set_aops(inode);
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ext4_dir_inode_operations;
		inode->i_fop = &ext4_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		if (ext4_inode_is_fast_symlink(inode)) {
			inode->i_op = &ext4_fast_symlink_inode_operations;
			nd_terminate_link(ei->i_data, inode->i_size,
				sizeof(ei->i_data) - 1);
		} else {
			inode->i_op = &ext4_symlink_inode_operations;
			ext4_set_aops(inode);
		}
	} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
	      S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		inode->i_op = &ext4_special_inode_operations;
		if (raw_inode->i_block[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(raw_inode->i_block[0])));
		else
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(raw_inode->i_block[1])));
	} else if (ino == EXT4_BOOT_LOADER_INO) {
		make_bad_inode(inode);
	} else {
		ret = -EIO;
		EXT4_ERROR_INODE(inode, "bogus i_mode (%o)", inode->i_mode);
		goto bad_inode;
	}
	brelse(iloc.bh);
	ext4_set_inode_flags(inode);
	unlock_new_inode(inode);
	return inode;

bad_inode:
	brelse(iloc.bh);
	iget_failed(inode);
	return ERR_PTR(ret);
}

struct inode *ext4_iget_normal(struct super_block *sb, unsigned long ino)
{
	if (ino < EXT4_FIRST_INO(sb) && ino != EXT4_ROOT_INO)
		return ERR_PTR(-EIO);
	return ext4_iget(sb, ino);
}

static int ext4_inode_blocks_set(handle_t *handle,
				struct ext4_inode *raw_inode,
				struct ext4_inode_info *ei)
{
	struct inode *inode = &(ei->vfs_inode);
	u64 i_blocks = inode->i_blocks;
	struct super_block *sb = inode->i_sb;

	if (i_blocks <= ~0U) {
		/*
		 * i_blocks can be represented in a 32 bit variable
		 * as multiple of 512 bytes
		 */
		raw_inode->i_blocks_lo   = cpu_to_le32(i_blocks);
		raw_inode->i_blocks_high = 0;
		ext4_clear_inode_flag(inode, EXT4_INODE_HUGE_FILE);
		return 0;
	}
	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_HUGE_FILE))
		return -EFBIG;

	if (i_blocks <= 0xffffffffffffULL) {
		/*
		 * i_blocks can be represented in a 48 bit variable
		 * as multiple of 512 bytes
		 */
		raw_inode->i_blocks_lo   = cpu_to_le32(i_blocks);
		raw_inode->i_blocks_high = cpu_to_le16(i_blocks >> 32);
		ext4_clear_inode_flag(inode, EXT4_INODE_HUGE_FILE);
	} else {
		ext4_set_inode_flag(inode, EXT4_INODE_HUGE_FILE);
		/* i_block is stored in file system block size */
		i_blocks = i_blocks >> (inode->i_blkbits - 9);
		raw_inode->i_blocks_lo   = cpu_to_le32(i_blocks);
		raw_inode->i_blocks_high = cpu_to_le16(i_blocks >> 32);
	}
	return 0;
}

/*
 * Post the struct inode info into an on-disk inode location in the
 * buffer-cache.  This gobbles the caller's reference to the
 * buffer_head in the inode location struct.
 *
 * The caller must have write access to iloc->bh.
 */
static int ext4_do_update_inode(handle_t *handle,
				struct inode *inode,
				struct ext4_iloc *iloc)
{
	struct ext4_inode *raw_inode = ext4_raw_inode(iloc);
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct buffer_head *bh = iloc->bh;
	int err = 0, rc, block;
	int need_datasync = 0;
	uid_t i_uid;
	gid_t i_gid;

	/* For fields not not tracking in the in-memory inode,
	 * initialise them to zero for new inodes. */
	if (ext4_test_inode_state(inode, EXT4_STATE_NEW))
		memset(raw_inode, 0, EXT4_SB(inode->i_sb)->s_inode_size);

	ext4_get_inode_flags(ei);
	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	i_uid = i_uid_read(inode);
	i_gid = i_gid_read(inode);
	if (!(test_opt(inode->i_sb, NO_UID32))) {
		raw_inode->i_uid_low = cpu_to_le16(low_16_bits(i_uid));
		raw_inode->i_gid_low = cpu_to_le16(low_16_bits(i_gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old inodes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if (!ei->i_dtime) {
			raw_inode->i_uid_high =
				cpu_to_le16(high_16_bits(i_uid));
			raw_inode->i_gid_high =
				cpu_to_le16(high_16_bits(i_gid));
		} else {
			raw_inode->i_uid_high = 0;
			raw_inode->i_gid_high = 0;
		}
	} else {
		raw_inode->i_uid_low = cpu_to_le16(fs_high2lowuid(i_uid));
		raw_inode->i_gid_low = cpu_to_le16(fs_high2lowgid(i_gid));
		raw_inode->i_uid_high = 0;
		raw_inode->i_gid_high = 0;
	}
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);

	EXT4_INODE_SET_XTIME(i_ctime, inode, raw_inode);
	EXT4_INODE_SET_XTIME(i_mtime, inode, raw_inode);
	EXT4_INODE_SET_XTIME(i_atime, inode, raw_inode);
	EXT4_EINODE_SET_XTIME(i_crtime, ei, raw_inode);

	if (ext4_inode_blocks_set(handle, raw_inode, ei))
		goto out_brelse;
	raw_inode->i_dtime = cpu_to_le32(ei->i_dtime);
	raw_inode->i_flags = cpu_to_le32(ei->i_flags & 0xFFFFFFFF);
	if (EXT4_SB(inode->i_sb)->s_es->s_creator_os !=
	    cpu_to_le32(EXT4_OS_HURD))
		raw_inode->i_file_acl_high =
			cpu_to_le16(ei->i_file_acl >> 32);
	raw_inode->i_file_acl_lo = cpu_to_le32(ei->i_file_acl);
	if (ei->i_disksize != ext4_isize(raw_inode)) {
		ext4_isize_set(raw_inode, ei->i_disksize);
		need_datasync = 1;
	}
	if (ei->i_disksize > 0x7fffffffULL) {
		struct super_block *sb = inode->i_sb;
		if (!EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_LARGE_FILE) ||
				EXT4_SB(sb)->s_es->s_rev_level ==
				cpu_to_le32(EXT4_GOOD_OLD_REV)) {
			/* If this is the first large file
			 * created, add a flag to the superblock.
			 */
			err = ext4_journal_get_write_access(handle,
					EXT4_SB(sb)->s_sbh);
			if (err)
				goto out_brelse;
			ext4_update_dynamic_rev(sb);
			EXT4_SET_RO_COMPAT_FEATURE(sb,
					EXT4_FEATURE_RO_COMPAT_LARGE_FILE);
			ext4_handle_sync(handle);
			err = ext4_handle_dirty_super(handle, sb);
		}
	}
	raw_inode->i_generation = cpu_to_le32(inode->i_generation);
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (old_valid_dev(inode->i_rdev)) {
			raw_inode->i_block[0] =
				cpu_to_le32(old_encode_dev(inode->i_rdev));
			raw_inode->i_block[1] = 0;
		} else {
			raw_inode->i_block[0] = 0;
			raw_inode->i_block[1] =
				cpu_to_le32(new_encode_dev(inode->i_rdev));
			raw_inode->i_block[2] = 0;
		}
	} else if (!ext4_has_inline_data(inode)) {
		for (block = 0; block < EXT4_N_BLOCKS; block++)
			raw_inode->i_block[block] = ei->i_data[block];
	}

	raw_inode->i_disk_version = cpu_to_le32(inode->i_version);
	if (ei->i_extra_isize) {
		if (EXT4_FITS_IN_INODE(raw_inode, ei, i_version_hi))
			raw_inode->i_version_hi =
			cpu_to_le32(inode->i_version >> 32);
		raw_inode->i_extra_isize = cpu_to_le16(ei->i_extra_isize);
	}

	ext4_inode_csum_set(inode, raw_inode, ei);

	BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
	rc = ext4_handle_dirty_metadata(handle, NULL, bh);
	if (!err)
		err = rc;
	ext4_clear_inode_state(inode, EXT4_STATE_NEW);

	ext4_update_inode_fsync_trans(handle, inode, need_datasync);
out_brelse:
	brelse(bh);
	ext4_std_error(inode->i_sb, err);
	return err;
}

/*
 * ext4_write_inode()
 *
 * We are called from a few places:
 *
 * - Within generic_file_write() for O_SYNC files.
 *   Here, there will be no transaction running. We wait for any running
 *   transaction to commit.
 *
 * - Within sys_sync(), kupdate and such.
 *   We wait on commit, if tol to.
 *
 * - Within prune_icache() (PF_MEMALLOC == true)
 *   Here we simply return.  We can't afford to block kswapd on the
 *   journal commit.
 *
 * In all cases it is actually safe for us to return without doing anything,
 * because the inode has been copied into a raw inode buffer in
 * ext4_mark_inode_dirty().  This is a correctness thing for O_SYNC and for
 * knfsd.
 *
 * Note that we are absolutely dependent upon all inode dirtiers doing the
 * right thing: they *must* call mark_inode_dirty() after dirtying info in
 * which we are interested.
 *
 * It would be a bug for them to not do this.  The code:
 *
 *	mark_inode_dirty(inode)
 *	stuff();
 *	inode->i_size = expr;
 *
 * is in error because a kswapd-driven write_inode() could occur while
 * `stuff()' is running, and the new i_size will be lost.  Plus the inode
 * will no longer be on the superblock's dirty inode list.
 */
int ext4_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err;

	if (current->flags & PF_MEMALLOC)
		return 0;

	if (EXT4_SB(inode->i_sb)->s_journal) {
		if (ext4_journal_current_handle()) {
			jbd_debug(1, "called recursively, non-PF_MEMALLOC!\n");
			dump_stack();
			return -EIO;
		}

		if (wbc->sync_mode != WB_SYNC_ALL)
			return 0;

		err = ext4_force_commit(inode->i_sb);
	} else {
		struct ext4_iloc iloc;

		err = __ext4_get_inode_loc(inode, &iloc, 0);
		if (err)
			return err;
		if (wbc->sync_mode == WB_SYNC_ALL)
			sync_dirty_buffer(iloc.bh);
		if (buffer_req(iloc.bh) && !buffer_uptodate(iloc.bh)) {
			EXT4_ERROR_INODE_BLOCK(inode, iloc.bh->b_blocknr,
					 "IO error syncing inode");
			err = -EIO;
		}
		brelse(iloc.bh);
	}
	return err;
}

/*
 * In data=journal mode ext4_journalled_invalidatepage() may fail to invalidate
 * buffers that are attached to a page stradding i_size and are undergoing
 * commit. In that case we have to wait for commit to finish and try again.
 */
static void ext4_wait_for_tail_page_commit(struct inode *inode)
{
	struct page *page;
	unsigned offset;
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	tid_t commit_tid = 0;
	int ret;

	offset = inode->i_size & (PAGE_CACHE_SIZE - 1);
	/*
	 * All buffers in the last page remain valid? Then there's nothing to
	 * do. We do the check mainly to optimize the common PAGE_CACHE_SIZE ==
	 * blocksize case
	 */
	if (offset > PAGE_CACHE_SIZE - (1 << inode->i_blkbits))
		return;
	while (1) {
		page = find_lock_page(inode->i_mapping,
				      inode->i_size >> PAGE_CACHE_SHIFT);
		if (!page)
			return;
		ret = __ext4_journalled_invalidatepage(page, offset);
		unlock_page(page);
		page_cache_release(page);
		if (ret != -EBUSY)
			return;
		commit_tid = 0;
		read_lock(&journal->j_state_lock);
		if (journal->j_committing_transaction)
			commit_tid = journal->j_committing_transaction->t_tid;
		read_unlock(&journal->j_state_lock);
		if (commit_tid)
			jbd2_log_wait_commit(journal, commit_tid);
	}
}

/*
 * ext4_setattr()
 *
 * Called from notify_change.
 *
 * We want to trap VFS attempts to truncate the file as soon as
 * possible.  In particular, we want to make sure that when the VFS
 * shrinks i_size, we put the inode on the orphan list and modify
 * i_disksize immediately, so that during the subsequent flushing of
 * dirty pages and freeing of disk blocks, we can guarantee that any
 * commit will leave the blocks being flushed in an unused state on
 * disk.  (On recovery, the inode will get truncated and the blocks will
 * be freed, so we have a strong guarantee that no future commit will
 * leave these blocks visible to the user.)
 *
 * Another thing we have to assure is that if we are in ordered mode
 * and inode is still attached to the committing transaction, we must
 * we start writeout of all the dirty pages which are being truncated.
 * This way we are sure that all the data written in the previous
 * transaction are already on disk (truncate waits for pages under
 * writeback).
 *
 * Called with inode->i_mutex down.
 */
int ext4_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error, rc = 0;
	int orphan = 0;
	const unsigned int ia_valid = attr->ia_valid;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (is_quota_modification(inode, attr))
		dquot_initialize(inode);
	if ((ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, inode->i_uid)) ||
	    (ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, inode->i_gid))) {
		handle_t *handle;

		/* (user+group)*(old+new) structure, inode write (sb,
		 * inode block, ? - but truncate inode update has it) */
		handle = ext4_journal_start(inode, EXT4_HT_QUOTA,
			(EXT4_MAXQUOTAS_INIT_BLOCKS(inode->i_sb) +
			 EXT4_MAXQUOTAS_DEL_BLOCKS(inode->i_sb)) + 3);
		if (IS_ERR(handle)) {
			error = PTR_ERR(handle);
			goto err_out;
		}
		error = dquot_transfer(inode, attr);
		if (error) {
			ext4_journal_stop(handle);
			return error;
		}
		/* Update corresponding info in inode so that everything is in
		 * one transaction */
		if (attr->ia_valid & ATTR_UID)
			inode->i_uid = attr->ia_uid;
		if (attr->ia_valid & ATTR_GID)
			inode->i_gid = attr->ia_gid;
		error = ext4_mark_inode_dirty(handle, inode);
		ext4_journal_stop(handle);
	}

	if (attr->ia_valid & ATTR_SIZE && attr->ia_size != inode->i_size) {
		handle_t *handle;
		loff_t oldsize = inode->i_size;

		if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))) {
			struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

			if (attr->ia_size > sbi->s_bitmap_maxbytes)
				return -EFBIG;
		}

		if (IS_I_VERSION(inode) && attr->ia_size != inode->i_size)
			inode_inc_iversion(inode);

		if (S_ISREG(inode->i_mode) &&
		    (attr->ia_size < inode->i_size)) {
			if (ext4_should_order_data(inode)) {
				error = ext4_begin_ordered_truncate(inode,
							    attr->ia_size);
				if (error)
					goto err_out;
			}
			handle = ext4_journal_start(inode, EXT4_HT_INODE, 3);
			if (IS_ERR(handle)) {
				error = PTR_ERR(handle);
				goto err_out;
			}
			if (ext4_handle_valid(handle)) {
				error = ext4_orphan_add(handle, inode);
				orphan = 1;
			}
			EXT4_I(inode)->i_disksize = attr->ia_size;
			rc = ext4_mark_inode_dirty(handle, inode);
			if (!error)
				error = rc;
			ext4_journal_stop(handle);
			if (error) {
				ext4_orphan_del(NULL, inode);
				goto err_out;
			}
		}

		i_size_write(inode, attr->ia_size);
		/*
		 * Blocks are going to be removed from the inode. Wait
		 * for dio in flight.  Temporarily disable
		 * dioread_nolock to prevent livelock.
		 */
		if (orphan) {
			if (!ext4_should_journal_data(inode)) {
				ext4_inode_block_unlocked_dio(inode);
				inode_dio_wait(inode);
				ext4_inode_resume_unlocked_dio(inode);
			} else
				ext4_wait_for_tail_page_commit(inode);
		}
		/*
		 * Truncate pagecache after we've waited for commit
		 * in data=journal mode to make pages freeable.
		 */
		truncate_pagecache(inode, oldsize, inode->i_size);
	}
	/*
	 * We want to call ext4_truncate() even if attr->ia_size ==
	 * inode->i_size for cases like truncation of fallocated space
	 */
	if (attr->ia_valid & ATTR_SIZE)
		ext4_truncate(inode);

	if (!rc) {
		setattr_copy(inode, attr);
		mark_inode_dirty(inode);
	}

	/*
	 * If the call to ext4_truncate failed to get a transaction handle at
	 * all, we need to clean up the in-core orphan list manually.
	 */
	if (orphan && inode->i_nlink)
		ext4_orphan_del(NULL, inode);

	if (!rc && (ia_valid & ATTR_MODE))
		rc = ext4_acl_chmod(inode);

err_out:
	ext4_std_error(inode->i_sb, error);
	if (!error)
		error = rc;
	return error;
}

int ext4_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct inode *inode;
	unsigned long long delalloc_blocks;

	inode = dentry->d_inode;
	generic_fillattr(inode, stat);

	/*
	 * We can't update i_blocks if the block allocation is delayed
	 * otherwise in the case of system crash before the real block
	 * allocation is done, we will have i_blocks inconsistent with
	 * on-disk file blocks.
	 * We always keep i_blocks updated together with real
	 * allocation. But to not confuse with user, stat
	 * will return the blocks that include the delayed allocation
	 * blocks for this file.
	 */
	delalloc_blocks = EXT4_C2B(EXT4_SB(inode->i_sb),
				EXT4_I(inode)->i_reserved_data_blocks);

	stat->blocks += delalloc_blocks << (inode->i_sb->s_blocksize_bits-9);
	return 0;
}

static int ext4_index_trans_blocks(struct inode *inode, int nrblocks, int chunk)
{
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)))
		return ext4_ind_trans_blocks(inode, nrblocks, chunk);
	return ext4_ext_index_trans_blocks(inode, nrblocks, chunk);
}

/*
 * Account for index blocks, block groups bitmaps and block group
 * descriptor blocks if modify datablocks and index blocks
 * worse case, the indexs blocks spread over different block groups
 *
 * If datablocks are discontiguous, they are possible to spread over
 * different block groups too. If they are contiguous, with flexbg,
 * they could still across block group boundary.
 *
 * Also account for superblock, inode, quota and xattr blocks
 */
static int ext4_meta_trans_blocks(struct inode *inode, int nrblocks, int chunk)
{
	ext4_group_t groups, ngroups = ext4_get_groups_count(inode->i_sb);
	int gdpblocks;
	int idxblocks;
	int ret = 0;

	/*
	 * How many index blocks need to touch to modify nrblocks?
	 * The "Chunk" flag indicating whether the nrblocks is
	 * physically contiguous on disk
	 *
	 * For Direct IO and fallocate, they calls get_block to allocate
	 * one single extent at a time, so they could set the "Chunk" flag
	 */
	idxblocks = ext4_index_trans_blocks(inode, nrblocks, chunk);

	ret = idxblocks;

	/*
	 * Now let's see how many group bitmaps and group descriptors need
	 * to account
	 */
	groups = idxblocks;
	if (chunk)
		groups += 1;
	else
		groups += nrblocks;

	gdpblocks = groups;
	if (groups > ngroups)
		groups = ngroups;
	if (groups > EXT4_SB(inode->i_sb)->s_gdb_count)
		gdpblocks = EXT4_SB(inode->i_sb)->s_gdb_count;

	/* bitmaps and block group descriptor blocks */
	ret += groups + gdpblocks;

	/* Blocks for super block, inode, quota and xattr blocks */
	ret += EXT4_META_TRANS_BLOCKS(inode->i_sb);

	return ret;
}

/*
 * Calculate the total number of credits to reserve to fit
 * the modification of a single pages into a single transaction,
 * which may include multiple chunks of block allocations.
 *
 * This could be called via ext4_write_begin()
 *
 * We need to consider the worse case, when
 * one new block per extent.
 */
int ext4_writepage_trans_blocks(struct inode *inode)
{
	int bpp = ext4_journal_blocks_per_page(inode);
	int ret;

	ret = ext4_meta_trans_blocks(inode, bpp, 0);

	/* Account for data blocks for journalled mode */
	if (ext4_should_journal_data(inode))
		ret += bpp;
	return ret;
}

/*
 * Calculate the journal credits for a chunk of data modification.
 *
 * This is called from DIO, fallocate or whoever calling
 * ext4_map_blocks() to map/allocate a chunk of contiguous disk blocks.
 *
 * journal buffers for data blocks are not included here, as DIO
 * and fallocate do no need to journal data buffers.
 */
int ext4_chunk_trans_blocks(struct inode *inode, int nrblocks)
{
	return ext4_meta_trans_blocks(inode, nrblocks, 1);
}

/*
 * The caller must have previously called ext4_reserve_inode_write().
 * Give this, we know that the caller already has write access to iloc->bh.
 */
int ext4_mark_iloc_dirty(handle_t *handle,
			 struct inode *inode, struct ext4_iloc *iloc)
{
	int err = 0;

	if (IS_I_VERSION(inode))
		inode_inc_iversion(inode);

	/* the do_update_inode consumes one bh->b_count */
	get_bh(iloc->bh);

	/* ext4_do_update_inode() does jbd2_journal_dirty_metadata */
	err = ext4_do_update_inode(handle, inode, iloc);
	put_bh(iloc->bh);
	return err;
}

/*
 * On success, We end up with an outstanding reference count against
 * iloc->bh.  This _must_ be cleaned up later.
 */

int
ext4_reserve_inode_write(handle_t *handle, struct inode *inode,
			 struct ext4_iloc *iloc)
{
	int err;

	err = ext4_get_inode_loc(inode, iloc);
	if (!err) {
		BUFFER_TRACE(iloc->bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, iloc->bh);
		if (err) {
			brelse(iloc->bh);
			iloc->bh = NULL;
		}
	}
	ext4_std_error(inode->i_sb, err);
	return err;
}

/*
 * Expand an inode by new_extra_isize bytes.
 * Returns 0 on success or negative error number on failure.
 */
static int ext4_expand_extra_isize(struct inode *inode,
				   unsigned int new_extra_isize,
				   struct ext4_iloc iloc,
				   handle_t *handle)
{
	struct ext4_inode *raw_inode;
	struct ext4_xattr_ibody_header *header;

	if (EXT4_I(inode)->i_extra_isize >= new_extra_isize)
		return 0;

	raw_inode = ext4_raw_inode(&iloc);

	header = IHDR(inode, raw_inode);

	/* No extended attributes present */
	if (!ext4_test_inode_state(inode, EXT4_STATE_XATTR) ||
	    header->h_magic != cpu_to_le32(EXT4_XATTR_MAGIC)) {
		memset((void *)raw_inode + EXT4_GOOD_OLD_INODE_SIZE, 0,
			new_extra_isize);
		EXT4_I(inode)->i_extra_isize = new_extra_isize;
		return 0;
	}

	/* try to expand with EAs present */
	return ext4_expand_extra_isize_ea(inode, new_extra_isize,
					  raw_inode, handle);
}

/*
 * What we do here is to mark the in-core inode as clean with respect to inode
 * dirtiness (it may still be data-dirty).
 * This means that the in-core inode may be reaped by prune_icache
 * without having to perform any I/O.  This is a very good thing,
 * because *any* task may call prune_icache - even ones which
 * have a transaction open against a different journal.
 *
 * Is this cheating?  Not really.  Sure, we haven't written the
 * inode out, but prune_icache isn't a user-visible syncing function.
 * Whenever the user wants stuff synced (sys_sync, sys_msync, sys_fsync)
 * we start and wait on commits.
 */
int ext4_mark_inode_dirty(handle_t *handle, struct inode *inode)
{
	struct ext4_iloc iloc;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	static unsigned int mnt_count;
	int err, ret;

	might_sleep();
	trace_ext4_mark_inode_dirty(inode, _RET_IP_);
	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (ext4_handle_valid(handle) &&
	    EXT4_I(inode)->i_extra_isize < sbi->s_want_extra_isize &&
	    !ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND)) {
		/*
		 * We need extra buffer credits since we may write into EA block
		 * with this same handle. If journal_extend fails, then it will
		 * only result in a minor loss of functionality for that inode.
		 * If this is felt to be critical, then e2fsck should be run to
		 * force a large enough s_min_extra_isize.
		 */
		if ((jbd2_journal_extend(handle,
			     EXT4_DATA_TRANS_BLOCKS(inode->i_sb))) == 0) {
			ret = ext4_expand_extra_isize(inode,
						      sbi->s_want_extra_isize,
						      iloc, handle);
			if (ret) {
				ext4_set_inode_state(inode,
						     EXT4_STATE_NO_EXPAND);
				if (mnt_count !=
					le16_to_cpu(sbi->s_es->s_mnt_count)) {
					ext4_warning(inode->i_sb,
					"Unable to expand inode %lu. Delete"
					" some EAs or run e2fsck.",
					inode->i_ino);
					mnt_count =
					  le16_to_cpu(sbi->s_es->s_mnt_count);
				}
			}
		}
	}
	if (!err)
		err = ext4_mark_iloc_dirty(handle, inode, &iloc);
	return err;
}

/*
 * ext4_dirty_inode() is called from __mark_inode_dirty()
 *
 * We're really interested in the case where a file is being extended.
 * i_size has been changed by generic_commit_write() and we thus need
 * to include the updated inode in the current transaction.
 *
 * Also, dquot_alloc_block() will always dirty the inode when blocks
 * are allocated to the file.
 *
 * If the inode is marked synchronous, we don't honour that here - doing
 * so would cause a commit on atime updates, which we don't bother doing.
 * We handle synchronous inodes at the highest possible level.
 */
void ext4_dirty_inode(struct inode *inode, int flags)
{
	handle_t *handle;

	handle = ext4_journal_start(inode, EXT4_HT_INODE, 2);
	if (IS_ERR(handle))
		goto out;

	ext4_mark_inode_dirty(handle, inode);

	ext4_journal_stop(handle);
out:
	return;
}

#if 0
/*
 * Bind an inode's backing buffer_head into this transaction, to prevent
 * it from being flushed to disk early.  Unlike
 * ext4_reserve_inode_write, this leaves behind no bh reference and
 * returns no iloc structure, so the caller needs to repeat the iloc
 * lookup to mark the inode dirty later.
 */
static int ext4_pin_inode(handle_t *handle, struct inode *inode)
{
	struct ext4_iloc iloc;

	int err = 0;
	if (handle) {
		err = ext4_get_inode_loc(inode, &iloc);
		if (!err) {
			BUFFER_TRACE(iloc.bh, "get_write_access");
			err = jbd2_journal_get_write_access(handle, iloc.bh);
			if (!err)
				err = ext4_handle_dirty_metadata(handle,
								 NULL,
								 iloc.bh);
			brelse(iloc.bh);
		}
	}
	ext4_std_error(inode->i_sb, err);
	return err;
}
#endif

int ext4_change_inode_journal_flag(struct inode *inode, int val)
{
	journal_t *journal;
	handle_t *handle;
	int err;

	/*
	 * We have to be very careful here: changing a data block's
	 * journaling status dynamically is dangerous.  If we write a
	 * data block to the journal, change the status and then delete
	 * that block, we risk forgetting to revoke the old log record
	 * from the journal and so a subsequent replay can corrupt data.
	 * So, first we make sure that the journal is empty and that
	 * nobody is changing anything.
	 */

	journal = EXT4_JOURNAL(inode);
	if (!journal)
		return 0;
	if (is_journal_aborted(journal))
		return -EROFS;
	/* We have to allocate physical blocks for delalloc blocks
	 * before flushing journal. otherwise delalloc blocks can not
	 * be allocated any more. even more truncate on delalloc blocks
	 * could trigger BUG by flushing delalloc blocks in journal.
	 * There is no delalloc block in non-journal data mode.
	 */
	if (val && test_opt(inode->i_sb, DELALLOC)) {
		err = ext4_alloc_da_blocks(inode);
		if (err < 0)
			return err;
	}

	/* Wait for all existing dio workers */
	ext4_inode_block_unlocked_dio(inode);
	inode_dio_wait(inode);

	jbd2_journal_lock_updates(journal);

	/*
	 * OK, there are no updates running now, and all cached data is
	 * synced to disk.  We are now in a completely consistent state
	 * which doesn't have anything in the journal, and we know that
	 * no filesystem updates are running, so it is safe to modify
	 * the inode's in-core data-journaling state flag now.
	 */

	if (val)
		ext4_set_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	else {
		jbd2_journal_flush(journal);
		ext4_clear_inode_flag(inode, EXT4_INODE_JOURNAL_DATA);
	}
	ext4_set_aops(inode);

	jbd2_journal_unlock_updates(journal);
	ext4_inode_resume_unlocked_dio(inode);

	/* Finally we can mark the inode as dirty. */

	handle = ext4_journal_start(inode, EXT4_HT_INODE, 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_mark_inode_dirty(handle, inode);
	ext4_handle_sync(handle);
	ext4_journal_stop(handle);
	ext4_std_error(inode->i_sb, err);

	return err;
}

static int ext4_bh_unmapped(handle_t *handle, struct buffer_head *bh)
{
	return !buffer_mapped(bh);
}

int ext4_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	loff_t size;
	unsigned long len;
	int ret;
	struct file *file = vma->vm_file;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	handle_t *handle;
	get_block_t *get_block;
	int retries = 0;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vma->vm_file);
	/* Delalloc case is easy... */
	if (test_opt(inode->i_sb, DELALLOC) &&
	    !ext4_should_journal_data(inode) &&
	    !ext4_nonda_switch(inode->i_sb)) {
		do {
			ret = __block_page_mkwrite(vma, vmf,
						   ext4_da_get_block_prep);
		} while (ret == -ENOSPC &&
		       ext4_should_retry_alloc(inode->i_sb, &retries));
		goto out_ret;
	}

	lock_page(page);
	size = i_size_read(inode);
	/* Page got truncated from under us? */
	if (page->mapping != mapping || page_offset(page) > size) {
		unlock_page(page);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;
	/*
	 * Return if we have all the buffers mapped. This avoids the need to do
	 * journal_start/journal_stop which can block and take a long time
	 */
	if (page_has_buffers(page)) {
		if (!ext4_walk_page_buffers(NULL, page_buffers(page),
					    0, len, NULL,
					    ext4_bh_unmapped)) {
			/* Wait so that we don't change page under IO */
			wait_for_stable_page(page);
			ret = VM_FAULT_LOCKED;
			goto out;
		}
	}
	unlock_page(page);
	/* OK, we need to fill the hole... */
	if (ext4_should_dioread_nolock(inode))
		get_block = ext4_get_block_write;
	else
		get_block = ext4_get_block;
retry_alloc:
	handle = ext4_journal_start(inode, EXT4_HT_WRITE_PAGE,
				    ext4_writepage_trans_blocks(inode));
	if (IS_ERR(handle)) {
		ret = VM_FAULT_SIGBUS;
		goto out;
	}
	ret = __block_page_mkwrite(vma, vmf, get_block);
	if (!ret && ext4_should_journal_data(inode)) {
		if (ext4_walk_page_buffers(handle, page_buffers(page), 0,
			  PAGE_CACHE_SIZE, NULL, do_journal_get_write_access)) {
			unlock_page(page);
			ret = VM_FAULT_SIGBUS;
			ext4_journal_stop(handle);
			goto out;
		}
		ext4_set_inode_state(inode, EXT4_STATE_JDATA);
	}
	ext4_journal_stop(handle);
	if (ret == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry_alloc;
out_ret:
	ret = block_page_mkwrite_return(ret);
out:
	sb_end_pagefault(inode->i_sb);
	return ret;
}
