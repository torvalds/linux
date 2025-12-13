/*
 * Ext4 orphan inode handling
 */
#include <linux/fs.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>

#include "ext4.h"
#include "ext4_jbd2.h"

static int ext4_orphan_file_add(handle_t *handle, struct inode *inode)
{
	int i, j, start;
	struct ext4_orphan_info *oi = &EXT4_SB(inode->i_sb)->s_orphan_info;
	int ret = 0;
	bool found = false;
	__le32 *bdata;
	int inodes_per_ob = ext4_inodes_per_orphan_block(inode->i_sb);
	int looped = 0;

	/*
	 * Find block with free orphan entry. Use CPU number for a naive hash
	 * for a search start in the orphan file
	 */
	start = raw_smp_processor_id()*13 % oi->of_blocks;
	i = start;
	do {
		if (atomic_dec_if_positive(&oi->of_binfo[i].ob_free_entries)
		    >= 0) {
			found = true;
			break;
		}
		if (++i >= oi->of_blocks)
			i = 0;
	} while (i != start);

	if (!found) {
		/*
		 * For now we don't grow or shrink orphan file. We just use
		 * whatever was allocated at mke2fs time. The additional
		 * credits we would have to reserve for each orphan inode
		 * operation just don't seem worth it.
		 */
		return -ENOSPC;
	}

	ret = ext4_journal_get_write_access(handle, inode->i_sb,
				oi->of_binfo[i].ob_bh, EXT4_JTR_ORPHAN_FILE);
	if (ret) {
		atomic_inc(&oi->of_binfo[i].ob_free_entries);
		return ret;
	}

	bdata = (__le32 *)(oi->of_binfo[i].ob_bh->b_data);
	/* Find empty slot in a block */
	j = 0;
	do {
		if (looped) {
			/*
			 * Did we walk through the block several times without
			 * finding free entry? It is theoretically possible
			 * if entries get constantly allocated and freed or
			 * if the block is corrupted. Avoid indefinite looping
			 * and bail. We'll use orphan list instead.
			 */
			if (looped > 3) {
				atomic_inc(&oi->of_binfo[i].ob_free_entries);
				return -ENOSPC;
			}
			cond_resched();
		}
		while (bdata[j]) {
			if (++j >= inodes_per_ob) {
				j = 0;
				looped++;
			}
		}
	} while (cmpxchg(&bdata[j], (__le32)0, cpu_to_le32(inode->i_ino)) !=
		 (__le32)0);

	EXT4_I(inode)->i_orphan_idx = i * inodes_per_ob + j;
	ext4_set_inode_state(inode, EXT4_STATE_ORPHAN_FILE);

	return ext4_handle_dirty_metadata(handle, NULL, oi->of_binfo[i].ob_bh);
}

/*
 * ext4_orphan_add() links an unlinked or truncated inode into a list of
 * such inodes, starting at the superblock, in case we crash before the
 * file is closed/deleted, or in case the inode truncate spans multiple
 * transactions and the last transaction is not recovered after a crash.
 *
 * At filesystem recovery time, we walk this list deleting unlinked
 * inodes and truncating linked inodes in ext4_orphan_cleanup().
 *
 * Orphan list manipulation functions must be called under i_rwsem unless
 * we are just creating the inode or deleting it.
 */
int ext4_orphan_add(handle_t *handle, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_iloc iloc;
	int err = 0, rc;
	bool dirty = false;

	if (!sbi->s_journal || is_bad_inode(inode))
		return 0;

	WARN_ON_ONCE(!(inode->i_state & (I_NEW | I_FREEING)) &&
		     !inode_is_locked(inode));
	if (ext4_inode_orphan_tracked(inode))
		return 0;

	/*
	 * Orphan handling is only valid for files with data blocks
	 * being truncated, or files being unlinked. Note that we either
	 * hold i_rwsem, or the inode can not be referenced from outside,
	 * so i_nlink should not be bumped due to race
	 */
	ASSERT((S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
		  S_ISLNK(inode->i_mode)) || inode->i_nlink == 0);

	if (sbi->s_orphan_info.of_blocks) {
		err = ext4_orphan_file_add(handle, inode);
		/*
		 * Fallback to normal orphan list of orphan file is
		 * out of space
		 */
		if (err != -ENOSPC)
			return err;
	}

	BUFFER_TRACE(sbi->s_sbh, "get_write_access");
	err = ext4_journal_get_write_access(handle, sb, sbi->s_sbh,
					    EXT4_JTR_NONE);
	if (err)
		goto out;

	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (err)
		goto out;

	mutex_lock(&sbi->s_orphan_lock);
	/*
	 * Due to previous errors inode may be already a part of on-disk
	 * orphan list. If so skip on-disk list modification.
	 */
	if (!NEXT_ORPHAN(inode) || NEXT_ORPHAN(inode) >
	    (le32_to_cpu(sbi->s_es->s_inodes_count))) {
		/* Insert this inode at the head of the on-disk orphan list */
		NEXT_ORPHAN(inode) = le32_to_cpu(sbi->s_es->s_last_orphan);
		lock_buffer(sbi->s_sbh);
		sbi->s_es->s_last_orphan = cpu_to_le32(inode->i_ino);
		ext4_superblock_csum_set(sb);
		unlock_buffer(sbi->s_sbh);
		dirty = true;
	}
	list_add(&EXT4_I(inode)->i_orphan, &sbi->s_orphan);
	mutex_unlock(&sbi->s_orphan_lock);

	if (dirty) {
		err = ext4_handle_dirty_metadata(handle, NULL, sbi->s_sbh);
		rc = ext4_mark_iloc_dirty(handle, inode, &iloc);
		if (!err)
			err = rc;
		if (err) {
			/*
			 * We have to remove inode from in-memory list if
			 * addition to on disk orphan list failed. Stray orphan
			 * list entries can cause panics at unmount time.
			 */
			mutex_lock(&sbi->s_orphan_lock);
			list_del_init(&EXT4_I(inode)->i_orphan);
			mutex_unlock(&sbi->s_orphan_lock);
		}
	} else
		brelse(iloc.bh);

	ext4_debug("superblock will point to %lu\n", inode->i_ino);
	ext4_debug("orphan inode %lu will point to %d\n",
			inode->i_ino, NEXT_ORPHAN(inode));
out:
	ext4_std_error(sb, err);
	return err;
}

static int ext4_orphan_file_del(handle_t *handle, struct inode *inode)
{
	struct ext4_orphan_info *oi = &EXT4_SB(inode->i_sb)->s_orphan_info;
	__le32 *bdata;
	int blk, off;
	int inodes_per_ob = ext4_inodes_per_orphan_block(inode->i_sb);
	int ret = 0;

	if (!handle)
		goto out;
	blk = EXT4_I(inode)->i_orphan_idx / inodes_per_ob;
	off = EXT4_I(inode)->i_orphan_idx % inodes_per_ob;
	if (WARN_ON_ONCE(blk >= oi->of_blocks))
		goto out;

	ret = ext4_journal_get_write_access(handle, inode->i_sb,
				oi->of_binfo[blk].ob_bh, EXT4_JTR_ORPHAN_FILE);
	if (ret)
		goto out;

	bdata = (__le32 *)(oi->of_binfo[blk].ob_bh->b_data);
	bdata[off] = 0;
	atomic_inc(&oi->of_binfo[blk].ob_free_entries);
	ret = ext4_handle_dirty_metadata(handle, NULL, oi->of_binfo[blk].ob_bh);
out:
	ext4_clear_inode_state(inode, EXT4_STATE_ORPHAN_FILE);
	INIT_LIST_HEAD(&EXT4_I(inode)->i_orphan);

	return ret;
}

/*
 * ext4_orphan_del() removes an unlinked or truncated inode from the list
 * of such inodes stored on disk, because it is finally being cleaned up.
 */
int ext4_orphan_del(handle_t *handle, struct inode *inode)
{
	struct list_head *prev;
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	__u32 ino_next;
	struct ext4_iloc iloc;
	int err = 0;

	if (!sbi->s_journal && !(sbi->s_mount_state & EXT4_ORPHAN_FS))
		return 0;

	WARN_ON_ONCE(!(inode->i_state & (I_NEW | I_FREEING)) &&
		     !inode_is_locked(inode));
	if (ext4_test_inode_state(inode, EXT4_STATE_ORPHAN_FILE))
		return ext4_orphan_file_del(handle, inode);

	/* Do this quick check before taking global s_orphan_lock. */
	if (list_empty(&ei->i_orphan))
		return 0;

	if (handle) {
		/* Grab inode buffer early before taking global s_orphan_lock */
		err = ext4_reserve_inode_write(handle, inode, &iloc);
	}

	mutex_lock(&sbi->s_orphan_lock);
	ext4_debug("remove inode %lu from orphan list\n", inode->i_ino);

	prev = ei->i_orphan.prev;
	list_del_init(&ei->i_orphan);

	/* If we're on an error path, we may not have a valid
	 * transaction handle with which to update the orphan list on
	 * disk, but we still need to remove the inode from the linked
	 * list in memory. */
	if (!handle || err) {
		mutex_unlock(&sbi->s_orphan_lock);
		goto out_err;
	}

	ino_next = NEXT_ORPHAN(inode);
	if (prev == &sbi->s_orphan) {
		ext4_debug("superblock will point to %u\n", ino_next);
		BUFFER_TRACE(sbi->s_sbh, "get_write_access");
		err = ext4_journal_get_write_access(handle, inode->i_sb,
						    sbi->s_sbh, EXT4_JTR_NONE);
		if (err) {
			mutex_unlock(&sbi->s_orphan_lock);
			goto out_brelse;
		}
		lock_buffer(sbi->s_sbh);
		sbi->s_es->s_last_orphan = cpu_to_le32(ino_next);
		ext4_superblock_csum_set(inode->i_sb);
		unlock_buffer(sbi->s_sbh);
		mutex_unlock(&sbi->s_orphan_lock);
		err = ext4_handle_dirty_metadata(handle, NULL, sbi->s_sbh);
	} else {
		struct ext4_iloc iloc2;
		struct inode *i_prev =
			&list_entry(prev, struct ext4_inode_info, i_orphan)->vfs_inode;

		ext4_debug("orphan inode %lu will point to %u\n",
			  i_prev->i_ino, ino_next);
		err = ext4_reserve_inode_write(handle, i_prev, &iloc2);
		if (err) {
			mutex_unlock(&sbi->s_orphan_lock);
			goto out_brelse;
		}
		NEXT_ORPHAN(i_prev) = ino_next;
		err = ext4_mark_iloc_dirty(handle, i_prev, &iloc2);
		mutex_unlock(&sbi->s_orphan_lock);
	}
	if (err)
		goto out_brelse;
	NEXT_ORPHAN(inode) = 0;
	err = ext4_mark_iloc_dirty(handle, inode, &iloc);
out_err:
	ext4_std_error(inode->i_sb, err);
	return err;

out_brelse:
	brelse(iloc.bh);
	goto out_err;
}

#ifdef CONFIG_QUOTA
static int ext4_quota_on_mount(struct super_block *sb, int type)
{
	return dquot_quota_on_mount(sb,
		rcu_dereference_protected(EXT4_SB(sb)->s_qf_names[type],
					  lockdep_is_held(&sb->s_umount)),
		EXT4_SB(sb)->s_jquota_fmt, type);
}
#endif

static void ext4_process_orphan(struct inode *inode,
				int *nr_truncates, int *nr_orphans)
{
	struct super_block *sb = inode->i_sb;
	int ret;

	dquot_initialize(inode);
	if (inode->i_nlink) {
		if (test_opt(sb, DEBUG))
			ext4_msg(sb, KERN_DEBUG,
				"%s: truncating inode %lu to %lld bytes",
				__func__, inode->i_ino, inode->i_size);
		ext4_debug("truncating inode %lu to %lld bytes\n",
			   inode->i_ino, inode->i_size);
		inode_lock(inode);
		truncate_inode_pages(inode->i_mapping, inode->i_size);
		ret = ext4_truncate(inode);
		if (ret) {
			/*
			 * We need to clean up the in-core orphan list
			 * manually if ext4_truncate() failed to get a
			 * transaction handle.
			 */
			ext4_orphan_del(NULL, inode);
			ext4_std_error(inode->i_sb, ret);
		}
		inode_unlock(inode);
		(*nr_truncates)++;
	} else {
		if (test_opt(sb, DEBUG))
			ext4_msg(sb, KERN_DEBUG,
				"%s: deleting unreferenced inode %lu",
				__func__, inode->i_ino);
		ext4_debug("deleting unreferenced inode %lu\n",
			   inode->i_ino);
		(*nr_orphans)++;
	}
	iput(inode);  /* The delete magic happens here! */
}

/* ext4_orphan_cleanup() walks a singly-linked list of inodes (starting at
 * the superblock) which were deleted from all directories, but held open by
 * a process at the time of a crash.  We walk the list and try to delete these
 * inodes at recovery time (only with a read-write filesystem).
 *
 * In order to keep the orphan inode chain consistent during traversal (in
 * case of crash during recovery), we link each inode into the superblock
 * orphan list_head and handle it the same way as an inode deletion during
 * normal operation (which journals the operations for us).
 *
 * We only do an iget() and an iput() on each inode, which is very safe if we
 * accidentally point at an in-use or already deleted inode.  The worst that
 * can happen in this case is that we get a "bit already cleared" message from
 * ext4_free_inode().  The only reason we would point at a wrong inode is if
 * e2fsck was run on this filesystem, and it must have already done the orphan
 * inode cleanup for us, so we can safely abort without any further action.
 */
void ext4_orphan_cleanup(struct super_block *sb, struct ext4_super_block *es)
{
	unsigned int s_flags = sb->s_flags;
	int nr_orphans = 0, nr_truncates = 0;
	struct inode *inode;
	int i, j;
#ifdef CONFIG_QUOTA
	int quota_update = 0;
#endif
	__le32 *bdata;
	struct ext4_orphan_info *oi = &EXT4_SB(sb)->s_orphan_info;
	int inodes_per_ob = ext4_inodes_per_orphan_block(sb);

	if (!es->s_last_orphan && !oi->of_blocks) {
		ext4_debug("no orphan inodes to clean up\n");
		return;
	}

	if (bdev_read_only(sb->s_bdev)) {
		ext4_msg(sb, KERN_ERR, "write access "
			"unavailable, skipping orphan cleanup");
		return;
	}

	/* Check if feature set would not allow a r/w mount */
	if (!ext4_feature_set_ok(sb, 0)) {
		ext4_msg(sb, KERN_INFO, "Skipping orphan cleanup due to "
			 "unknown ROCOMPAT features");
		return;
	}

	if (EXT4_SB(sb)->s_mount_state & EXT4_ERROR_FS) {
		/* don't clear list on RO mount w/ errors */
		if (es->s_last_orphan && !(s_flags & SB_RDONLY)) {
			ext4_msg(sb, KERN_INFO, "Errors on filesystem, "
				  "clearing orphan list.");
			es->s_last_orphan = 0;
		}
		ext4_debug("Skipping orphan recovery on fs with errors.\n");
		return;
	}

	if (s_flags & SB_RDONLY) {
		ext4_msg(sb, KERN_INFO, "orphan cleanup on readonly fs");
		sb->s_flags &= ~SB_RDONLY;
	}
#ifdef CONFIG_QUOTA
	/*
	 * Turn on quotas which were not enabled for read-only mounts if
	 * filesystem has quota feature, so that they are updated correctly.
	 */
	if (ext4_has_feature_quota(sb) && (s_flags & SB_RDONLY)) {
		int ret = ext4_enable_quotas(sb);

		if (!ret)
			quota_update = 1;
		else
			ext4_msg(sb, KERN_ERR,
				"Cannot turn on quotas: error %d", ret);
	}

	/* Turn on journaled quotas used for old sytle */
	for (i = 0; i < EXT4_MAXQUOTAS; i++) {
		if (EXT4_SB(sb)->s_qf_names[i]) {
			int ret = ext4_quota_on_mount(sb, i);

			if (!ret)
				quota_update = 1;
			else
				ext4_msg(sb, KERN_ERR,
					"Cannot turn on journaled "
					"quota: type %d: error %d", i, ret);
		}
	}
#endif

	while (es->s_last_orphan) {
		/*
		 * We may have encountered an error during cleanup; if
		 * so, skip the rest.
		 */
		if (EXT4_SB(sb)->s_mount_state & EXT4_ERROR_FS) {
			ext4_debug("Skipping orphan recovery on fs with errors.\n");
			es->s_last_orphan = 0;
			break;
		}

		inode = ext4_orphan_get(sb, le32_to_cpu(es->s_last_orphan));
		if (IS_ERR(inode)) {
			es->s_last_orphan = 0;
			break;
		}

		list_add(&EXT4_I(inode)->i_orphan, &EXT4_SB(sb)->s_orphan);
		ext4_process_orphan(inode, &nr_truncates, &nr_orphans);
	}

	for (i = 0; i < oi->of_blocks; i++) {
		bdata = (__le32 *)(oi->of_binfo[i].ob_bh->b_data);
		for (j = 0; j < inodes_per_ob; j++) {
			if (!bdata[j])
				continue;
			inode = ext4_orphan_get(sb, le32_to_cpu(bdata[j]));
			if (IS_ERR(inode))
				continue;
			ext4_set_inode_state(inode, EXT4_STATE_ORPHAN_FILE);
			EXT4_I(inode)->i_orphan_idx = i * inodes_per_ob + j;
			ext4_process_orphan(inode, &nr_truncates, &nr_orphans);
		}
	}

#define PLURAL(x) (x), ((x) == 1) ? "" : "s"

	if (nr_orphans)
		ext4_msg(sb, KERN_INFO, "%d orphan inode%s deleted",
		       PLURAL(nr_orphans));
	if (nr_truncates)
		ext4_msg(sb, KERN_INFO, "%d truncate%s cleaned up",
		       PLURAL(nr_truncates));
#ifdef CONFIG_QUOTA
	/* Turn off quotas if they were enabled for orphan cleanup */
	if (quota_update) {
		for (i = 0; i < EXT4_MAXQUOTAS; i++) {
			if (sb_dqopt(sb)->files[i])
				dquot_quota_off(sb, i);
		}
	}
#endif
	sb->s_flags = s_flags; /* Restore SB_RDONLY status */
}

void ext4_release_orphan_info(struct super_block *sb)
{
	int i;
	struct ext4_orphan_info *oi = &EXT4_SB(sb)->s_orphan_info;

	if (!oi->of_blocks)
		return;
	for (i = 0; i < oi->of_blocks; i++)
		brelse(oi->of_binfo[i].ob_bh);
	kvfree(oi->of_binfo);
}

static struct ext4_orphan_block_tail *ext4_orphan_block_tail(
						struct super_block *sb,
						struct buffer_head *bh)
{
	return (struct ext4_orphan_block_tail *)(bh->b_data + sb->s_blocksize -
				sizeof(struct ext4_orphan_block_tail));
}

static int ext4_orphan_file_block_csum_verify(struct super_block *sb,
					      struct buffer_head *bh)
{
	__u32 calculated;
	int inodes_per_ob = ext4_inodes_per_orphan_block(sb);
	struct ext4_orphan_info *oi = &EXT4_SB(sb)->s_orphan_info;
	struct ext4_orphan_block_tail *ot;
	__le64 dsk_block_nr = cpu_to_le64(bh->b_blocknr);

	if (!ext4_has_feature_metadata_csum(sb))
		return 1;

	ot = ext4_orphan_block_tail(sb, bh);
	calculated = ext4_chksum(oi->of_csum_seed, (__u8 *)&dsk_block_nr,
				 sizeof(dsk_block_nr));
	calculated = ext4_chksum(calculated, (__u8 *)bh->b_data,
				 inodes_per_ob * sizeof(__u32));
	return le32_to_cpu(ot->ob_checksum) == calculated;
}

/* This gets called only when checksumming is enabled */
void ext4_orphan_file_block_trigger(struct jbd2_buffer_trigger_type *triggers,
				    struct buffer_head *bh,
				    void *data, size_t size)
{
	struct super_block *sb = EXT4_TRIGGER(triggers)->sb;
	__u32 csum;
	int inodes_per_ob = ext4_inodes_per_orphan_block(sb);
	struct ext4_orphan_info *oi = &EXT4_SB(sb)->s_orphan_info;
	struct ext4_orphan_block_tail *ot;
	__le64 dsk_block_nr = cpu_to_le64(bh->b_blocknr);

	csum = ext4_chksum(oi->of_csum_seed, (__u8 *)&dsk_block_nr,
			   sizeof(dsk_block_nr));
	csum = ext4_chksum(csum, (__u8 *)data, inodes_per_ob * sizeof(__u32));
	ot = ext4_orphan_block_tail(sb, bh);
	ot->ob_checksum = cpu_to_le32(csum);
}

int ext4_init_orphan_info(struct super_block *sb)
{
	struct ext4_orphan_info *oi = &EXT4_SB(sb)->s_orphan_info;
	struct inode *inode;
	int i, j;
	int ret;
	int free;
	__le32 *bdata;
	int inodes_per_ob = ext4_inodes_per_orphan_block(sb);
	struct ext4_orphan_block_tail *ot;
	ino_t orphan_ino = le32_to_cpu(EXT4_SB(sb)->s_es->s_orphan_file_inum);

	if (!ext4_has_feature_orphan_file(sb))
		return 0;

	inode = ext4_iget(sb, orphan_ino, EXT4_IGET_SPECIAL);
	if (IS_ERR(inode)) {
		ext4_msg(sb, KERN_ERR, "get orphan inode failed");
		return PTR_ERR(inode);
	}
	/*
	 * This is just an artificial limit to prevent corrupted fs from
	 * consuming absurd amounts of memory when pinning blocks of orphan
	 * file in memory.
	 */
	if (inode->i_size > 8 << 20) {
		ext4_msg(sb, KERN_ERR, "orphan file too big: %llu",
			 (unsigned long long)inode->i_size);
		ret = -EFSCORRUPTED;
		goto out_put;
	}
	oi->of_blocks = inode->i_size >> sb->s_blocksize_bits;
	oi->of_csum_seed = EXT4_I(inode)->i_csum_seed;
	oi->of_binfo = kvmalloc_array(oi->of_blocks,
				     sizeof(struct ext4_orphan_block),
				     GFP_KERNEL);
	if (!oi->of_binfo) {
		ret = -ENOMEM;
		goto out_put;
	}
	for (i = 0; i < oi->of_blocks; i++) {
		oi->of_binfo[i].ob_bh = ext4_bread(NULL, inode, i, 0);
		if (IS_ERR(oi->of_binfo[i].ob_bh)) {
			ret = PTR_ERR(oi->of_binfo[i].ob_bh);
			goto out_free;
		}
		if (!oi->of_binfo[i].ob_bh) {
			ret = -EIO;
			goto out_free;
		}
		ot = ext4_orphan_block_tail(sb, oi->of_binfo[i].ob_bh);
		if (le32_to_cpu(ot->ob_magic) != EXT4_ORPHAN_BLOCK_MAGIC) {
			ext4_error(sb, "orphan file block %d: bad magic", i);
			ret = -EIO;
			goto out_free;
		}
		if (!ext4_orphan_file_block_csum_verify(sb,
						oi->of_binfo[i].ob_bh)) {
			ext4_error(sb, "orphan file block %d: bad checksum", i);
			ret = -EIO;
			goto out_free;
		}
		bdata = (__le32 *)(oi->of_binfo[i].ob_bh->b_data);
		free = 0;
		for (j = 0; j < inodes_per_ob; j++)
			if (bdata[j] == 0)
				free++;
		atomic_set(&oi->of_binfo[i].ob_free_entries, free);
	}
	iput(inode);
	return 0;
out_free:
	for (i--; i >= 0; i--)
		brelse(oi->of_binfo[i].ob_bh);
	kvfree(oi->of_binfo);
out_put:
	iput(inode);
	return ret;
}

int ext4_orphan_file_empty(struct super_block *sb)
{
	struct ext4_orphan_info *oi = &EXT4_SB(sb)->s_orphan_info;
	int i;
	int inodes_per_ob = ext4_inodes_per_orphan_block(sb);

	if (!ext4_has_feature_orphan_file(sb))
		return 1;
	for (i = 0; i < oi->of_blocks; i++)
		if (atomic_read(&oi->of_binfo[i].ob_free_entries) !=
		    inodes_per_ob)
			return 0;
	return 1;
}
