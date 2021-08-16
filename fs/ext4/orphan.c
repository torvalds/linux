/*
 * Ext4 orphan inode handling
 */
#include <linux/fs.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>

#include "ext4.h"
#include "ext4_jbd2.h"

/*
 * ext4_orphan_add() links an unlinked or truncated inode into a list of
 * such inodes, starting at the superblock, in case we crash before the
 * file is closed/deleted, or in case the inode truncate spans multiple
 * transactions and the last transaction is not recovered after a crash.
 *
 * At filesystem recovery time, we walk this list deleting unlinked
 * inodes and truncating linked inodes in ext4_orphan_cleanup().
 *
 * Orphan list manipulation functions must be called under i_mutex unless
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
	/*
	 * Exit early if inode already is on orphan list. This is a big speedup
	 * since we don't have to contend on the global s_orphan_lock.
	 */
	if (!list_empty(&EXT4_I(inode)->i_orphan))
		return 0;

	/*
	 * Orphan handling is only valid for files with data blocks
	 * being truncated, or files being unlinked. Note that we either
	 * hold i_mutex, or the inode can not be referenced from outside,
	 * so i_nlink should not be bumped due to race
	 */
	ASSERT((S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
		  S_ISLNK(inode->i_mode)) || inode->i_nlink == 0);

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

	jbd_debug(4, "superblock will point to %lu\n", inode->i_ino);
	jbd_debug(4, "orphan inode %lu will point to %d\n",
			inode->i_ino, NEXT_ORPHAN(inode));
out:
	ext4_std_error(sb, err);
	return err;
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
	/* Do this quick check before taking global s_orphan_lock. */
	if (list_empty(&ei->i_orphan))
		return 0;

	if (handle) {
		/* Grab inode buffer early before taking global s_orphan_lock */
		err = ext4_reserve_inode_write(handle, inode, &iloc);
	}

	mutex_lock(&sbi->s_orphan_lock);
	jbd_debug(4, "remove inode %lu from orphan list\n", inode->i_ino);

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
		jbd_debug(4, "superblock will point to %u\n", ino_next);
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

		jbd_debug(4, "orphan inode %lu will point to %u\n",
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
	int ret, nr_orphans = 0, nr_truncates = 0;
#ifdef CONFIG_QUOTA
	int quota_update = 0;
	int i;
#endif
	if (!es->s_last_orphan) {
		jbd_debug(4, "no orphan inodes to clean up\n");
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
				  "clearing orphan list.\n");
			es->s_last_orphan = 0;
		}
		jbd_debug(1, "Skipping orphan recovery on fs with errors.\n");
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
		struct inode *inode;

		/*
		 * We may have encountered an error during cleanup; if
		 * so, skip the rest.
		 */
		if (EXT4_SB(sb)->s_mount_state & EXT4_ERROR_FS) {
			jbd_debug(1, "Skipping orphan recovery on fs with errors.\n");
			es->s_last_orphan = 0;
			break;
		}

		inode = ext4_orphan_get(sb, le32_to_cpu(es->s_last_orphan));
		if (IS_ERR(inode)) {
			es->s_last_orphan = 0;
			break;
		}

		list_add(&EXT4_I(inode)->i_orphan, &EXT4_SB(sb)->s_orphan);
		dquot_initialize(inode);
		if (inode->i_nlink) {
			if (test_opt(sb, DEBUG))
				ext4_msg(sb, KERN_DEBUG,
					"%s: truncating inode %lu to %lld bytes",
					__func__, inode->i_ino, inode->i_size);
			jbd_debug(2, "truncating inode %lu to %lld bytes\n",
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
			nr_truncates++;
		} else {
			if (test_opt(sb, DEBUG))
				ext4_msg(sb, KERN_DEBUG,
					"%s: deleting unreferenced inode %lu",
					__func__, inode->i_ino);
			jbd_debug(2, "deleting unreferenced inode %lu\n",
				  inode->i_ino);
			nr_orphans++;
		}
		iput(inode);  /* The delete magic happens here! */
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
