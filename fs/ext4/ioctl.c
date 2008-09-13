/*
 * linux/fs/ext4/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/smp_lock.h>
#include <linux/mount.h>
#include <asm/uaccess.h>
#include "ext4_jbd2.h"
#include "ext4.h"

long ext4_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned int flags;
	unsigned short rsv_window_size;

	ext4_debug("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case EXT4_IOC_GETFLAGS:
		ext4_get_inode_flags(ei);
		flags = ei->i_flags & EXT4_FL_USER_VISIBLE;
		return put_user(flags, (int __user *) arg);
	case EXT4_IOC_SETFLAGS: {
		handle_t *handle = NULL;
		int err;
		struct ext4_iloc iloc;
		unsigned int oldflags;
		unsigned int jflag;

		if (!is_owner_or_cap(inode))
			return -EACCES;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		if (!S_ISDIR(inode->i_mode))
			flags &= ~EXT4_DIRSYNC_FL;

		err = -EPERM;
		mutex_lock(&inode->i_mutex);
		/* Is it quota file? Do not allow user to mess with it */
		if (IS_NOQUOTA(inode))
			goto flags_out;

		oldflags = ei->i_flags;

		/* The JOURNAL_DATA flag is modifiable only by root */
		jflag = flags & EXT4_JOURNAL_DATA_FL;

		/*
		 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
		 * the relevant capability.
		 *
		 * This test looks nicer. Thanks to Pauline Middelink
		 */
		if ((flags ^ oldflags) & (EXT4_APPEND_FL | EXT4_IMMUTABLE_FL)) {
			if (!capable(CAP_LINUX_IMMUTABLE))
				goto flags_out;
		}

		/*
		 * The JOURNAL_DATA flag can only be changed by
		 * the relevant capability.
		 */
		if ((jflag ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
			if (!capable(CAP_SYS_RESOURCE))
				goto flags_out;
		}

		handle = ext4_journal_start(inode, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto flags_out;
		}
		if (IS_SYNC(inode))
			handle->h_sync = 1;
		err = ext4_reserve_inode_write(handle, inode, &iloc);
		if (err)
			goto flags_err;

		flags = flags & EXT4_FL_USER_MODIFIABLE;
		flags |= oldflags & ~EXT4_FL_USER_MODIFIABLE;
		ei->i_flags = flags;

		ext4_set_inode_flags(inode);
		inode->i_ctime = ext4_current_time(inode);

		err = ext4_mark_iloc_dirty(handle, inode, &iloc);
flags_err:
		ext4_journal_stop(handle);
		if (err)
			goto flags_out;

		if ((jflag ^ oldflags) & (EXT4_JOURNAL_DATA_FL))
			err = ext4_change_inode_journal_flag(inode, jflag);
flags_out:
		mutex_unlock(&inode->i_mutex);
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
	case EXT4_IOC_GETVERSION:
	case EXT4_IOC_GETVERSION_OLD:
		return put_user(inode->i_generation, (int __user *) arg);
	case EXT4_IOC_SETVERSION:
	case EXT4_IOC_SETVERSION_OLD: {
		handle_t *handle;
		struct ext4_iloc iloc;
		__u32 generation;
		int err;

		if (!is_owner_or_cap(inode))
			return -EPERM;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;
		if (get_user(generation, (int __user *) arg)) {
			err = -EFAULT;
			goto setversion_out;
		}

		handle = ext4_journal_start(inode, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto setversion_out;
		}
		err = ext4_reserve_inode_write(handle, inode, &iloc);
		if (err == 0) {
			inode->i_ctime = ext4_current_time(inode);
			inode->i_generation = generation;
			err = ext4_mark_iloc_dirty(handle, inode, &iloc);
		}
		ext4_journal_stop(handle);
setversion_out:
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}
#ifdef CONFIG_JBD2_DEBUG
	case EXT4_IOC_WAIT_FOR_READONLY:
		/*
		 * This is racy - by the time we're woken up and running,
		 * the superblock could be released.  And the module could
		 * have been unloaded.  So sue me.
		 *
		 * Returns 1 if it slept, else zero.
		 */
		{
			struct super_block *sb = inode->i_sb;
			DECLARE_WAITQUEUE(wait, current);
			int ret = 0;

			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&EXT4_SB(sb)->ro_wait_queue, &wait);
			if (timer_pending(&EXT4_SB(sb)->turn_ro_timer)) {
				schedule();
				ret = 1;
			}
			remove_wait_queue(&EXT4_SB(sb)->ro_wait_queue, &wait);
			return ret;
		}
#endif
	case EXT4_IOC_GETRSVSZ:
		if (test_opt(inode->i_sb, RESERVATION)
			&& S_ISREG(inode->i_mode)
			&& ei->i_block_alloc_info) {
			rsv_window_size = ei->i_block_alloc_info->rsv_window_node.rsv_goal_size;
			return put_user(rsv_window_size, (int __user *)arg);
		}
		return -ENOTTY;
	case EXT4_IOC_SETRSVSZ: {
		int err;

		if (!test_opt(inode->i_sb, RESERVATION) || !S_ISREG(inode->i_mode))
			return -ENOTTY;

		if (!is_owner_or_cap(inode))
			return -EACCES;

		if (get_user(rsv_window_size, (int __user *)arg))
			return -EFAULT;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		if (rsv_window_size > EXT4_MAX_RESERVE_BLOCKS)
			rsv_window_size = EXT4_MAX_RESERVE_BLOCKS;

		/*
		 * need to allocate reservation structure for this inode
		 * before set the window size
		 */
		down_write(&ei->i_data_sem);
		if (!ei->i_block_alloc_info)
			ext4_init_block_alloc_info(inode);

		if (ei->i_block_alloc_info){
			struct ext4_reserve_window_node *rsv = &ei->i_block_alloc_info->rsv_window_node;
			rsv->rsv_goal_size = rsv_window_size;
		}
		up_write(&ei->i_data_sem);
		mnt_drop_write(filp->f_path.mnt);
		return 0;
	}
	case EXT4_IOC_GROUP_EXTEND: {
		ext4_fsblk_t n_blocks_count;
		struct super_block *sb = inode->i_sb;
		int err;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (get_user(n_blocks_count, (__u32 __user *)arg))
			return -EFAULT;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		err = ext4_group_extend(sb, EXT4_SB(sb)->s_es, n_blocks_count);
		jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
		jbd2_journal_flush(EXT4_SB(sb)->s_journal);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		mnt_drop_write(filp->f_path.mnt);

		return err;
	}
	case EXT4_IOC_GROUP_ADD: {
		struct ext4_new_group_data input;
		struct super_block *sb = inode->i_sb;
		int err;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		if (copy_from_user(&input, (struct ext4_new_group_input __user *)arg,
				sizeof(input)))
			return -EFAULT;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;

		err = ext4_group_add(sb, &input);
		jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
		jbd2_journal_flush(EXT4_SB(sb)->s_journal);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		mnt_drop_write(filp->f_path.mnt);

		return err;
	}

	case EXT4_IOC_MIGRATE:
	{
		int err;
		if (!is_owner_or_cap(inode))
			return -EACCES;

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			return err;
		/*
		 * inode_mutex prevent write and truncate on the file.
		 * Read still goes through. We take i_data_sem in
		 * ext4_ext_swap_inode_data before we switch the
		 * inode format to prevent read.
		 */
		mutex_lock(&(inode->i_mutex));
		err = ext4_ext_migrate(inode);
		mutex_unlock(&(inode->i_mutex));
		mnt_drop_write(filp->f_path.mnt);
		return err;
	}

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ext4_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT4_IOC32_GETFLAGS:
		cmd = EXT4_IOC_GETFLAGS;
		break;
	case EXT4_IOC32_SETFLAGS:
		cmd = EXT4_IOC_SETFLAGS;
		break;
	case EXT4_IOC32_GETVERSION:
		cmd = EXT4_IOC_GETVERSION;
		break;
	case EXT4_IOC32_SETVERSION:
		cmd = EXT4_IOC_SETVERSION;
		break;
	case EXT4_IOC32_GROUP_EXTEND:
		cmd = EXT4_IOC_GROUP_EXTEND;
		break;
	case EXT4_IOC32_GETVERSION_OLD:
		cmd = EXT4_IOC_GETVERSION_OLD;
		break;
	case EXT4_IOC32_SETVERSION_OLD:
		cmd = EXT4_IOC_SETVERSION_OLD;
		break;
#ifdef CONFIG_JBD2_DEBUG
	case EXT4_IOC32_WAIT_FOR_READONLY:
		cmd = EXT4_IOC_WAIT_FOR_READONLY;
		break;
#endif
	case EXT4_IOC32_GETRSVSZ:
		cmd = EXT4_IOC_GETRSVSZ;
		break;
	case EXT4_IOC32_SETRSVSZ:
		cmd = EXT4_IOC_SETRSVSZ;
		break;
	case EXT4_IOC_GROUP_ADD:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ext4_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
