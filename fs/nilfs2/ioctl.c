/*
 * ioctl.c - NILFS ioctl operations.
 *
 * Copyright (C) 2007, 2008 Nippon Telegraph and Telephone Corporation.
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
 * Written by Koji Sato <koji@osrg.net>.
 */

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/smp_lock.h>	/* lock_kernel(), unlock_kernel() */
#include <linux/capability.h>	/* capable() */
#include <linux/uaccess.h>	/* copy_from_user(), copy_to_user() */
#include <linux/nilfs2_fs.h>
#include "nilfs.h"
#include "segment.h"
#include "bmap.h"
#include "cpfile.h"
#include "sufile.h"
#include "dat.h"


static int nilfs_ioctl_wrap_copy(struct the_nilfs *nilfs,
				 struct nilfs_argv *argv, int dir,
				 ssize_t (*dofunc)(struct the_nilfs *,
						   int, int,
						   void *, size_t, size_t))
{
	void *buf;
	size_t maxmembs, total, n;
	ssize_t nr;
	int ret, i;

	if (argv->v_nmembs == 0)
		return 0;

	if (argv->v_size > PAGE_SIZE)
		return -EINVAL;

	buf = (void *)__get_free_pages(GFP_NOFS, 0);
	if (unlikely(!buf))
		return -ENOMEM;
	maxmembs = PAGE_SIZE / argv->v_size;

	ret = 0;
	total = 0;
	for (i = 0; i < argv->v_nmembs; i += n) {
		n = (argv->v_nmembs - i < maxmembs) ?
			argv->v_nmembs - i : maxmembs;
		if ((dir & _IOC_WRITE) &&
		    copy_from_user(buf,
			    (void __user *)argv->v_base + argv->v_size * i,
			    argv->v_size * n)) {
			ret = -EFAULT;
			break;
		}
		nr = (*dofunc)(nilfs, argv->v_index + i, argv->v_flags, buf,
			       argv->v_size, n);
		if (nr < 0) {
			ret = nr;
			break;
		}
		if ((dir & _IOC_READ) &&
		    copy_to_user(
			    (void __user *)argv->v_base + argv->v_size * i,
			    buf, argv->v_size * nr)) {
			ret = -EFAULT;
			break;
		}
		total += nr;
	}
	argv->v_nmembs = total;

	free_pages((unsigned long)buf, 0);
	return ret;
}

static int nilfs_ioctl_change_cpmode(struct inode *inode, struct file *filp,
				     unsigned int cmd, void __user *argp)
{
	struct inode *cpfile = NILFS_SB(inode->i_sb)->s_nilfs->ns_cpfile;
	struct nilfs_transaction_info ti;
	struct nilfs_cpmode cpmode;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&cpmode, argp, sizeof(cpmode)))
		return -EFAULT;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_cpfile_change_cpmode(
		cpfile, cpmode.cm_cno, cpmode.cm_mode);
	nilfs_transaction_end(inode->i_sb, !ret);
	return ret;
}

static int
nilfs_ioctl_delete_checkpoint(struct inode *inode, struct file *filp,
			      unsigned int cmd, void __user *argp)
{
	struct inode *cpfile = NILFS_SB(inode->i_sb)->s_nilfs->ns_cpfile;
	struct nilfs_transaction_info ti;
	__u64 cno;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&cno, argp, sizeof(cno)))
		return -EFAULT;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_cpfile_delete_checkpoint(cpfile, cno);
	nilfs_transaction_end(inode->i_sb, !ret);
	return ret;
}

static ssize_t
nilfs_ioctl_do_get_cpinfo(struct the_nilfs *nilfs, int index, int flags,
			  void *buf, size_t size, size_t nmembs)
{
	return nilfs_cpfile_get_cpinfo(nilfs->ns_cpfile, index, flags, buf,
				       nmembs);
}

static int nilfs_ioctl_get_cpinfo(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = NILFS_SB(inode->i_sb)->s_nilfs;
	struct nilfs_argv argv;
	struct nilfs_transaction_info ti;
	int ret;

	if (copy_from_user(&argv, argp, sizeof(argv)))
		return -EFAULT;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_ioctl_wrap_copy(nilfs, &argv, _IOC_DIR(cmd),
				    nilfs_ioctl_do_get_cpinfo);
	nilfs_transaction_end(inode->i_sb, 0);

	if (copy_to_user(argp, &argv, sizeof(argv)))
		ret = -EFAULT;
	return ret;
}

static int nilfs_ioctl_get_cpstat(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct inode *cpfile = NILFS_SB(inode->i_sb)->s_nilfs->ns_cpfile;
	struct nilfs_cpstat cpstat;
	struct nilfs_transaction_info ti;
	int ret;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_cpfile_get_stat(cpfile, &cpstat);
	nilfs_transaction_end(inode->i_sb, 0);
	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &cpstat, sizeof(cpstat)))
		ret = -EFAULT;
	return ret;
}

static ssize_t
nilfs_ioctl_do_get_suinfo(struct the_nilfs *nilfs, int index, int flags,
			  void *buf, size_t size, size_t nmembs)
{
	return nilfs_sufile_get_suinfo(nilfs->ns_sufile, index, buf, nmembs);
}

static int nilfs_ioctl_get_suinfo(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = NILFS_SB(inode->i_sb)->s_nilfs;
	struct nilfs_argv argv;
	struct nilfs_transaction_info ti;
	int ret;

	if (copy_from_user(&argv, argp, sizeof(argv)))
		return -EFAULT;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_ioctl_wrap_copy(nilfs, &argv, _IOC_DIR(cmd),
				    nilfs_ioctl_do_get_suinfo);
	nilfs_transaction_end(inode->i_sb, 0);

	if (copy_to_user(argp, &argv, sizeof(argv)))
		ret = -EFAULT;
	return ret;
}

static int nilfs_ioctl_get_sustat(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct inode *sufile = NILFS_SB(inode->i_sb)->s_nilfs->ns_sufile;
	struct nilfs_sustat sustat;
	struct nilfs_transaction_info ti;
	int ret;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_sufile_get_stat(sufile, &sustat);
	nilfs_transaction_end(inode->i_sb, 0);
	if (ret < 0)
		return ret;

	if (copy_to_user(argp, &sustat, sizeof(sustat)))
		ret = -EFAULT;
	return ret;
}

static ssize_t
nilfs_ioctl_do_get_vinfo(struct the_nilfs *nilfs, int index, int flags,
			 void *buf, size_t size, size_t nmembs)
{
	return nilfs_dat_get_vinfo(nilfs_dat_inode(nilfs), buf, nmembs);
}

static int nilfs_ioctl_get_vinfo(struct inode *inode, struct file *filp,
				 unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = NILFS_SB(inode->i_sb)->s_nilfs;
	struct nilfs_argv argv;
	struct nilfs_transaction_info ti;
	int ret;

	if (copy_from_user(&argv, argp, sizeof(argv)))
		return -EFAULT;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_ioctl_wrap_copy(nilfs, &argv, _IOC_DIR(cmd),
				    nilfs_ioctl_do_get_vinfo);
	nilfs_transaction_end(inode->i_sb, 0);

	if (copy_to_user(argp, &argv, sizeof(argv)))
		ret = -EFAULT;
	return ret;
}

static ssize_t
nilfs_ioctl_do_get_bdescs(struct the_nilfs *nilfs, int index, int flags,
			  void *buf, size_t size, size_t nmembs)
{
	struct inode *dat = nilfs_dat_inode(nilfs);
	struct nilfs_bmap *bmap = NILFS_I(dat)->i_bmap;
	struct nilfs_bdesc *bdescs = buf;
	int ret, i;

	for (i = 0; i < nmembs; i++) {
		ret = nilfs_bmap_lookup_at_level(bmap,
						 bdescs[i].bd_offset,
						 bdescs[i].bd_level + 1,
						 &bdescs[i].bd_blocknr);
		if (ret < 0) {
			if (ret != -ENOENT)
				return ret;
			bdescs[i].bd_blocknr = 0;
		}
	}
	return nmembs;
}

static int nilfs_ioctl_get_bdescs(struct inode *inode, struct file *filp,
				  unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = NILFS_SB(inode->i_sb)->s_nilfs;
	struct nilfs_argv argv;
	struct nilfs_transaction_info ti;
	int ret;

	if (copy_from_user(&argv, argp, sizeof(argv)))
		return -EFAULT;

	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	ret = nilfs_ioctl_wrap_copy(nilfs, &argv, _IOC_DIR(cmd),
				    nilfs_ioctl_do_get_bdescs);
	nilfs_transaction_end(inode->i_sb, 0);

	if (copy_to_user(argp, &argv, sizeof(argv)))
		ret = -EFAULT;
	return ret;
}

static int nilfs_ioctl_move_inode_block(struct inode *inode,
					struct nilfs_vdesc *vdesc,
					struct list_head *buffers)
{
	struct buffer_head *bh;
	int ret;

	if (vdesc->vd_flags == 0)
		ret = nilfs_gccache_submit_read_data(
			inode, vdesc->vd_offset, vdesc->vd_blocknr,
			vdesc->vd_vblocknr, &bh);
	else
		ret = nilfs_gccache_submit_read_node(
			inode, vdesc->vd_blocknr, vdesc->vd_vblocknr, &bh);

	if (unlikely(ret < 0)) {
		if (ret == -ENOENT)
			printk(KERN_CRIT
			       "%s: invalid virtual block address (%s): "
			       "ino=%llu, cno=%llu, offset=%llu, "
			       "blocknr=%llu, vblocknr=%llu\n",
			       __func__, vdesc->vd_flags ? "node" : "data",
			       (unsigned long long)vdesc->vd_ino,
			       (unsigned long long)vdesc->vd_cno,
			       (unsigned long long)vdesc->vd_offset,
			       (unsigned long long)vdesc->vd_blocknr,
			       (unsigned long long)vdesc->vd_vblocknr);
		return ret;
	}
	bh->b_private = vdesc;
	list_add_tail(&bh->b_assoc_buffers, buffers);
	return 0;
}

static ssize_t
nilfs_ioctl_do_move_blocks(struct the_nilfs *nilfs, int index, int flags,
			   void *buf, size_t size, size_t nmembs)
{
	struct inode *inode;
	struct nilfs_vdesc *vdesc;
	struct buffer_head *bh, *n;
	LIST_HEAD(buffers);
	ino_t ino;
	__u64 cno;
	int i, ret;

	for (i = 0, vdesc = buf; i < nmembs; ) {
		ino = vdesc->vd_ino;
		cno = vdesc->vd_cno;
		inode = nilfs_gc_iget(nilfs, ino, cno);
		if (unlikely(inode == NULL)) {
			ret = -ENOMEM;
			goto failed;
		}
		do {
			ret = nilfs_ioctl_move_inode_block(inode, vdesc,
							   &buffers);
			if (unlikely(ret < 0))
				goto failed;
			vdesc++;
		} while (++i < nmembs &&
			 vdesc->vd_ino == ino && vdesc->vd_cno == cno);
	}

	list_for_each_entry_safe(bh, n, &buffers, b_assoc_buffers) {
		ret = nilfs_gccache_wait_and_mark_dirty(bh);
		if (unlikely(ret < 0)) {
			if (ret == -EEXIST) {
				vdesc = bh->b_private;
				printk(KERN_CRIT
				       "%s: conflicting %s buffer: "
				       "ino=%llu, cno=%llu, offset=%llu, "
				       "blocknr=%llu, vblocknr=%llu\n",
				       __func__,
				       vdesc->vd_flags ? "node" : "data",
				       (unsigned long long)vdesc->vd_ino,
				       (unsigned long long)vdesc->vd_cno,
				       (unsigned long long)vdesc->vd_offset,
				       (unsigned long long)vdesc->vd_blocknr,
				       (unsigned long long)vdesc->vd_vblocknr);
			}
			goto failed;
		}
		list_del_init(&bh->b_assoc_buffers);
		bh->b_private = NULL;
		brelse(bh);
	}
	return nmembs;

 failed:
	list_for_each_entry_safe(bh, n, &buffers, b_assoc_buffers) {
		list_del_init(&bh->b_assoc_buffers);
		bh->b_private = NULL;
		brelse(bh);
	}
	return ret;
}

static inline int nilfs_ioctl_move_blocks(struct the_nilfs *nilfs,
					  struct nilfs_argv *argv,
					  int dir)
{
	return nilfs_ioctl_wrap_copy(nilfs, argv, dir,
				     nilfs_ioctl_do_move_blocks);
}

static ssize_t
nilfs_ioctl_do_delete_checkpoints(struct the_nilfs *nilfs, int index,
				  int flags, void *buf, size_t size,
				  size_t nmembs)
{
	struct inode *cpfile = nilfs->ns_cpfile;
	struct nilfs_period *periods = buf;
	int ret, i;

	for (i = 0; i < nmembs; i++) {
		ret = nilfs_cpfile_delete_checkpoints(
			cpfile, periods[i].p_start, periods[i].p_end);
		if (ret < 0)
			return ret;
	}
	return nmembs;
}

static inline int nilfs_ioctl_delete_checkpoints(struct the_nilfs *nilfs,
						 struct nilfs_argv *argv,
						 int dir)
{
	return nilfs_ioctl_wrap_copy(nilfs, argv, dir,
				     nilfs_ioctl_do_delete_checkpoints);
}

static ssize_t
nilfs_ioctl_do_free_vblocknrs(struct the_nilfs *nilfs, int index, int flags,
			      void *buf, size_t size, size_t nmembs)
{
	int ret = nilfs_dat_freev(nilfs_dat_inode(nilfs), buf, nmembs);

	return (ret < 0) ? ret : nmembs;
}

static inline int nilfs_ioctl_free_vblocknrs(struct the_nilfs *nilfs,
					     struct nilfs_argv *argv,
					     int dir)
{
	return nilfs_ioctl_wrap_copy(nilfs, argv, dir,
				     nilfs_ioctl_do_free_vblocknrs);
}

static ssize_t
nilfs_ioctl_do_mark_blocks_dirty(struct the_nilfs *nilfs, int index, int flags,
				 void *buf, size_t size, size_t nmembs)
{
	struct inode *dat = nilfs_dat_inode(nilfs);
	struct nilfs_bmap *bmap = NILFS_I(dat)->i_bmap;
	struct nilfs_bdesc *bdescs = buf;
	int ret, i;

	for (i = 0; i < nmembs; i++) {
		/* XXX: use macro or inline func to check liveness */
		ret = nilfs_bmap_lookup_at_level(bmap,
						 bdescs[i].bd_offset,
						 bdescs[i].bd_level + 1,
						 &bdescs[i].bd_blocknr);
		if (ret < 0) {
			if (ret != -ENOENT)
				return ret;
			bdescs[i].bd_blocknr = 0;
		}
		if (bdescs[i].bd_blocknr != bdescs[i].bd_oblocknr)
			/* skip dead block */
			continue;
		if (bdescs[i].bd_level == 0) {
			ret = nilfs_mdt_mark_block_dirty(dat,
							 bdescs[i].bd_offset);
			if (ret < 0) {
				BUG_ON(ret == -ENOENT);
				return ret;
			}
		} else {
			ret = nilfs_bmap_mark(bmap, bdescs[i].bd_offset,
					      bdescs[i].bd_level);
			if (ret < 0) {
				BUG_ON(ret == -ENOENT);
				return ret;
			}
		}
	}
	return nmembs;
}

static inline int nilfs_ioctl_mark_blocks_dirty(struct the_nilfs *nilfs,
						struct nilfs_argv *argv,
						int dir)
{
	return nilfs_ioctl_wrap_copy(nilfs, argv, dir,
				     nilfs_ioctl_do_mark_blocks_dirty);
}

static ssize_t
nilfs_ioctl_do_free_segments(struct the_nilfs *nilfs, int index, int flags,
			     void *buf, size_t size, size_t nmembs)
{
	struct nilfs_sb_info *sbi = nilfs_get_writer(nilfs);
	int ret;

	BUG_ON(!sbi);
	ret = nilfs_segctor_add_segments_to_be_freed(
		NILFS_SC(sbi), buf, nmembs);
	nilfs_put_writer(nilfs);

	return (ret < 0) ? ret : nmembs;
}

static inline int nilfs_ioctl_free_segments(struct the_nilfs *nilfs,
					     struct nilfs_argv *argv,
					     int dir)
{
	return nilfs_ioctl_wrap_copy(nilfs, argv, dir,
				     nilfs_ioctl_do_free_segments);
}

int nilfs_ioctl_prepare_clean_segments(struct the_nilfs *nilfs,
				       void __user *argp)
{
	struct nilfs_argv argv[5];
	int dir, ret;

	if (copy_from_user(argv, argp, sizeof(argv)))
		return -EFAULT;

	dir = _IOC_WRITE;
	ret = nilfs_ioctl_move_blocks(nilfs, &argv[0], dir);
	if (ret < 0)
		goto out_move_blks;
	ret = nilfs_ioctl_delete_checkpoints(nilfs, &argv[1], dir);
	if (ret < 0)
		goto out_del_cps;
	ret = nilfs_ioctl_free_vblocknrs(nilfs, &argv[2], dir);
	if (ret < 0)
		goto out_free_vbns;
	ret = nilfs_ioctl_mark_blocks_dirty(nilfs, &argv[3], dir);
	if (ret < 0)
		goto out_free_vbns;
	ret = nilfs_ioctl_free_segments(nilfs, &argv[4], dir);
	if (ret < 0)
		goto out_free_segs;

	return 0;

 out_free_segs:
	BUG(); /* XXX: not implemented yet */
 out_free_vbns:
	BUG();/* XXX: not implemented yet */
 out_del_cps:
	BUG();/* XXX: not implemented yet */
 out_move_blks:
	nilfs_remove_all_gcinode(nilfs);
	return ret;
}

static int nilfs_ioctl_clean_segments(struct inode *inode, struct file *filp,
				      unsigned int cmd, void __user *argp)
{
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = nilfs_clean_segments(inode->i_sb, argp);
	clear_nilfs_cond_nongc_write(NILFS_SB(inode->i_sb)->s_nilfs);
	return ret;
}

static int nilfs_ioctl_test_cond(struct the_nilfs *nilfs, int cond)
{
	return (cond & NILFS_TIMEDWAIT_SEG_WRITE) &&
		nilfs_cond_nongc_write(nilfs);
}

static void nilfs_ioctl_clear_cond(struct the_nilfs *nilfs, int cond)
{
	if (cond & NILFS_TIMEDWAIT_SEG_WRITE)
		clear_nilfs_cond_nongc_write(nilfs);
}

static int nilfs_ioctl_timedwait(struct inode *inode, struct file *filp,
				 unsigned int cmd, void __user *argp)
{
	struct the_nilfs *nilfs = NILFS_SB(inode->i_sb)->s_nilfs;
	struct nilfs_wait_cond wc;
	long ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&wc, argp, sizeof(wc)))
		return -EFAULT;

	unlock_kernel();
	ret = wc.wc_flags ?
		wait_event_interruptible_timeout(
			nilfs->ns_cleanerd_wq,
			nilfs_ioctl_test_cond(nilfs, wc.wc_cond),
			timespec_to_jiffies(&wc.wc_timeout)) :
		wait_event_interruptible(
			nilfs->ns_cleanerd_wq,
			nilfs_ioctl_test_cond(nilfs, wc.wc_cond));
	lock_kernel();
	nilfs_ioctl_clear_cond(nilfs, wc.wc_cond);

	if (ret > 0) {
		jiffies_to_timespec(ret, &wc.wc_timeout);
		if (copy_to_user(argp, &wc, sizeof(wc)))
			return -EFAULT;
		return 0;
	}
	if (ret != 0)
		return -EINTR;

	return wc.wc_flags ? -ETIME : 0;
}

static int nilfs_ioctl_sync(struct inode *inode, struct file *filp,
			    unsigned int cmd, void __user *argp)
{
	__u64 cno;
	int ret;

	ret = nilfs_construct_segment(inode->i_sb);
	if (ret < 0)
		return ret;

	if (argp != NULL) {
		cno = NILFS_SB(inode->i_sb)->s_nilfs->ns_cno - 1;
		if (copy_to_user(argp, &cno, sizeof(cno)))
			return -EFAULT;
	}
	return 0;
}

int nilfs_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	void __user *argp = (void * __user *)arg;

	switch (cmd) {
	case NILFS_IOCTL_CHANGE_CPMODE:
		return nilfs_ioctl_change_cpmode(inode, filp, cmd, argp);
	case NILFS_IOCTL_DELETE_CHECKPOINT:
		return nilfs_ioctl_delete_checkpoint(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_CPINFO:
		return nilfs_ioctl_get_cpinfo(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_CPSTAT:
		return nilfs_ioctl_get_cpstat(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_SUINFO:
		return nilfs_ioctl_get_suinfo(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_SUSTAT:
		return nilfs_ioctl_get_sustat(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_VINFO:
		/* XXX: rename to ??? */
		return nilfs_ioctl_get_vinfo(inode, filp, cmd, argp);
	case NILFS_IOCTL_GET_BDESCS:
		return nilfs_ioctl_get_bdescs(inode, filp, cmd, argp);
	case NILFS_IOCTL_CLEAN_SEGMENTS:
		return nilfs_ioctl_clean_segments(inode, filp, cmd, argp);
	case NILFS_IOCTL_TIMEDWAIT:
		return nilfs_ioctl_timedwait(inode, filp, cmd, argp);
	case NILFS_IOCTL_SYNC:
		return nilfs_ioctl_sync(inode, filp, cmd, argp);
	default:
		return -ENOTTY;
	}
}

/* compat_ioctl */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

static int nilfs_compat_locked_ioctl(struct inode *inode, struct file *filp,
				     unsigned int cmd, unsigned long arg)
{
	int ret;

	lock_kernel();
	ret = nilfs_ioctl(inode, filp, cmd, arg);
	unlock_kernel();
	return ret;
}

static int
nilfs_compat_ioctl_uargv32_to_uargv(struct nilfs_argv32 __user *uargv32,
				    struct nilfs_argv __user *uargv)
{
	compat_uptr_t base;
	compat_size_t nmembs, size;
	compat_int_t index, flags;

	if (get_user(base, &uargv32->v_base) ||
	    put_user(compat_ptr(base), &uargv->v_base) ||
	    get_user(nmembs, &uargv32->v_nmembs) ||
	    put_user(nmembs, &uargv->v_nmembs) ||
	    get_user(size, &uargv32->v_size) ||
	    put_user(size, &uargv->v_size) ||
	    get_user(index, &uargv32->v_index) ||
	    put_user(index, &uargv->v_index) ||
	    get_user(flags, &uargv32->v_flags) ||
	    put_user(flags, &uargv->v_flags))
		return -EFAULT;
	return 0;
}

static int
nilfs_compat_ioctl_uargv_to_uargv32(struct nilfs_argv __user *uargv,
				    struct nilfs_argv32 __user *uargv32)
{
	size_t nmembs;

	if (get_user(nmembs, &uargv->v_nmembs) ||
	    put_user(nmembs, &uargv32->v_nmembs))
		return -EFAULT;
	return 0;
}

static int
nilfs_compat_ioctl_get_by_argv(struct inode *inode, struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	struct nilfs_argv __user *uargv;
	struct nilfs_argv32 __user *uargv32;
	int ret;

	uargv = compat_alloc_user_space(sizeof(struct nilfs_argv));
	uargv32 = compat_ptr(arg);
	ret = nilfs_compat_ioctl_uargv32_to_uargv(uargv32, uargv);
	if (ret < 0)
		return ret;

	ret = nilfs_compat_locked_ioctl(inode, filp, cmd, (unsigned long)uargv);
	if (ret < 0)
		return ret;

	return nilfs_compat_ioctl_uargv_to_uargv32(uargv, uargv32);
}

static int
nilfs_compat_ioctl_change_cpmode(struct inode *inode, struct file *filp,
				 unsigned int cmd, unsigned long arg)
{
	struct nilfs_cpmode __user *ucpmode;
	struct nilfs_cpmode32 __user *ucpmode32;
	int mode;

	ucpmode = compat_alloc_user_space(sizeof(struct nilfs_cpmode));
	ucpmode32 = compat_ptr(arg);
	if (copy_in_user(&ucpmode->cm_cno, &ucpmode32->cm_cno,
			 sizeof(__u64)) ||
	    get_user(mode, &ucpmode32->cm_mode) ||
	    put_user(mode, &ucpmode->cm_mode))
		return -EFAULT;

	return nilfs_compat_locked_ioctl(
		inode, filp, cmd, (unsigned long)ucpmode);
}


static inline int
nilfs_compat_ioctl_delete_checkpoint(struct inode *inode, struct file *filp,
				     unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_locked_ioctl(inode, filp, cmd, arg);
}

static inline int
nilfs_compat_ioctl_get_cpinfo(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_ioctl_get_by_argv(inode, filp, cmd, arg);
}

static inline int
nilfs_compat_ioctl_get_cpstat(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_locked_ioctl(inode, filp, cmd, arg);
}

static inline int
nilfs_compat_ioctl_get_suinfo(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_ioctl_get_by_argv(inode, filp, cmd, arg);
}

static int
nilfs_compat_ioctl_get_sustat(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	struct nilfs_sustat __user *usustat;
	struct nilfs_sustat32 __user *usustat32;
	time_t ctime, nongc_ctime;
	int ret;

	usustat = compat_alloc_user_space(sizeof(struct nilfs_sustat));
	ret = nilfs_compat_locked_ioctl(inode, filp, cmd,
					(unsigned long)usustat);
	if (ret < 0)
		return ret;

	usustat32 = compat_ptr(arg);
	if (copy_in_user(&usustat32->ss_nsegs, &usustat->ss_nsegs,
			 sizeof(__u64)) ||
	    copy_in_user(&usustat32->ss_ncleansegs, &usustat->ss_ncleansegs,
			 sizeof(__u64)) ||
	    copy_in_user(&usustat32->ss_ndirtysegs, &usustat->ss_ndirtysegs,
			 sizeof(__u64)) ||
	    get_user(ctime, &usustat->ss_ctime) ||
	    put_user(ctime, &usustat32->ss_ctime) ||
	    get_user(nongc_ctime, &usustat->ss_nongc_ctime) ||
	    put_user(nongc_ctime, &usustat32->ss_nongc_ctime))
		return -EFAULT;
	return 0;
}

static inline int
nilfs_compat_ioctl_get_vinfo(struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_ioctl_get_by_argv(inode, filp, cmd, arg);
}

static inline int
nilfs_compat_ioctl_get_bdescs(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_ioctl_get_by_argv(inode, filp, cmd, arg);
}

static int
nilfs_compat_ioctl_clean_segments(struct inode *inode, struct file *filp,
				  unsigned int cmd, unsigned long arg)
{
	struct nilfs_argv __user *uargv;
	struct nilfs_argv32 __user *uargv32;
	int i, ret;

	uargv = compat_alloc_user_space(sizeof(struct nilfs_argv) * 5);
	uargv32 = compat_ptr(arg);
	for (i = 0; i < 5; i++) {
		ret = nilfs_compat_ioctl_uargv32_to_uargv(&uargv32[i],
							  &uargv[i]);
		if (ret < 0)
			return ret;
	}
	return nilfs_compat_locked_ioctl(
		inode, filp, cmd, (unsigned long)uargv);
}

static int
nilfs_compat_ioctl_timedwait(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct nilfs_wait_cond __user *uwcond;
	struct nilfs_wait_cond32 __user *uwcond32;
	struct timespec ts;
	int cond, flags, ret;

	uwcond = compat_alloc_user_space(sizeof(struct nilfs_wait_cond));
	uwcond32 = compat_ptr(arg);
	if (get_user(cond, &uwcond32->wc_cond) ||
	    put_user(cond, &uwcond->wc_cond) ||
	    get_user(flags, &uwcond32->wc_flags) ||
	    put_user(flags, &uwcond->wc_flags) ||
	    get_user(ts.tv_sec, &uwcond32->wc_timeout.tv_sec) ||
	    get_user(ts.tv_nsec, &uwcond32->wc_timeout.tv_nsec) ||
	    put_user(ts.tv_sec, &uwcond->wc_timeout.tv_sec) ||
	    put_user(ts.tv_nsec, &uwcond->wc_timeout.tv_nsec))
		return -EFAULT;

	ret = nilfs_compat_locked_ioctl(inode, filp, cmd,
					(unsigned long)uwcond);
	if (ret < 0)
		return ret;

	if (get_user(ts.tv_sec, &uwcond->wc_timeout.tv_sec) ||
	    get_user(ts.tv_nsec, &uwcond->wc_timeout.tv_nsec) ||
	    put_user(ts.tv_sec, &uwcond32->wc_timeout.tv_sec) ||
	    put_user(ts.tv_nsec, &uwcond32->wc_timeout.tv_nsec))
		return -EFAULT;

	return 0;
}

static int nilfs_compat_ioctl_sync(struct inode *inode, struct file *filp,
				   unsigned int cmd, unsigned long arg)
{
	return nilfs_compat_locked_ioctl(inode, filp, cmd, arg);
}

long nilfs_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_dentry->d_inode;

	switch (cmd) {
	case NILFS_IOCTL32_CHANGE_CPMODE:
		return nilfs_compat_ioctl_change_cpmode(
			inode, filp, NILFS_IOCTL_CHANGE_CPMODE, arg);
	case NILFS_IOCTL_DELETE_CHECKPOINT:
		return nilfs_compat_ioctl_delete_checkpoint(
			inode, filp, cmd, arg);
	case NILFS_IOCTL32_GET_CPINFO:
		return nilfs_compat_ioctl_get_cpinfo(
			inode, filp, NILFS_IOCTL_GET_CPINFO, arg);
	case NILFS_IOCTL_GET_CPSTAT:
		return nilfs_compat_ioctl_get_cpstat(inode, filp, cmd, arg);
	case NILFS_IOCTL32_GET_SUINFO:
		return nilfs_compat_ioctl_get_suinfo(
			inode, filp, NILFS_IOCTL_GET_SUINFO, arg);
	case NILFS_IOCTL32_GET_SUSTAT:
		return nilfs_compat_ioctl_get_sustat(
			inode, filp, NILFS_IOCTL_GET_SUSTAT, arg);
	case NILFS_IOCTL32_GET_VINFO:
		return nilfs_compat_ioctl_get_vinfo(
			inode, filp, NILFS_IOCTL_GET_VINFO, arg);
	case NILFS_IOCTL32_GET_BDESCS:
		return nilfs_compat_ioctl_get_bdescs(
			inode, filp, NILFS_IOCTL_GET_BDESCS, arg);
	case NILFS_IOCTL32_CLEAN_SEGMENTS:
		return nilfs_compat_ioctl_clean_segments(
			inode, filp, NILFS_IOCTL_CLEAN_SEGMENTS, arg);
	case NILFS_IOCTL32_TIMEDWAIT:
		return nilfs_compat_ioctl_timedwait(
			inode, filp, NILFS_IOCTL_TIMEDWAIT, arg);
	case NILFS_IOCTL_SYNC:
		return nilfs_compat_ioctl_sync(inode, filp, cmd, arg);
	default:
		return -ENOIOCTLCMD;
	}
}
#endif
