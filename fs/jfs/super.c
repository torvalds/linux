/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/completion.h>
#include <linux/vfs.h>
#include <linux/quotaops.h>
#include <linux/mount.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/posix_acl.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>
#include <linux/smp_lock.h>

#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_inode.h"
#include "jfs_metapage.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_acl.h"
#include "jfs_debug.h"

MODULE_DESCRIPTION("The Journaled Filesystem (JFS)");
MODULE_AUTHOR("Steve Best/Dave Kleikamp/Barry Arndt, IBM");
MODULE_LICENSE("GPL");

static struct kmem_cache * jfs_inode_cachep;

static const struct super_operations jfs_super_operations;
static const struct export_operations jfs_export_operations;
static struct file_system_type jfs_fs_type;

#define MAX_COMMIT_THREADS 64
static int commit_threads = 0;
module_param(commit_threads, int, 0);
MODULE_PARM_DESC(commit_threads, "Number of commit threads");

static struct task_struct *jfsCommitThread[MAX_COMMIT_THREADS];
struct task_struct *jfsIOthread;
struct task_struct *jfsSyncThread;

#ifdef CONFIG_JFS_DEBUG
int jfsloglevel = JFS_LOGLEVEL_WARN;
module_param(jfsloglevel, int, 0644);
MODULE_PARM_DESC(jfsloglevel, "Specify JFS loglevel (0, 1 or 2)");
#endif

static void jfs_handle_error(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);

	if (sb->s_flags & MS_RDONLY)
		return;

	updateSuper(sb, FM_DIRTY);

	if (sbi->flag & JFS_ERR_PANIC)
		panic("JFS (device %s): panic forced after error\n",
			sb->s_id);
	else if (sbi->flag & JFS_ERR_REMOUNT_RO) {
		jfs_err("ERROR: (device %s): remounting filesystem "
			"as read-only\n",
			sb->s_id);
		sb->s_flags |= MS_RDONLY;
	}

	/* nothing is done for continue beyond marking the superblock dirty */
}

void jfs_error(struct super_block *sb, const char * function, ...)
{
	static char error_buf[256];
	va_list args;

	va_start(args, function);
	vsnprintf(error_buf, sizeof(error_buf), function, args);
	va_end(args);

	printk(KERN_ERR "ERROR: (device %s): %s\n", sb->s_id, error_buf);

	jfs_handle_error(sb);
}

static struct inode *jfs_alloc_inode(struct super_block *sb)
{
	struct jfs_inode_info *jfs_inode;

	jfs_inode = kmem_cache_alloc(jfs_inode_cachep, GFP_NOFS);
	if (!jfs_inode)
		return NULL;
	return &jfs_inode->vfs_inode;
}

static void jfs_destroy_inode(struct inode *inode)
{
	struct jfs_inode_info *ji = JFS_IP(inode);

	BUG_ON(!list_empty(&ji->anon_inode_list));

	spin_lock_irq(&ji->ag_lock);
	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(inode->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}
	spin_unlock_irq(&ji->ag_lock);
	kmem_cache_free(jfs_inode_cachep, ji);
}

static int jfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct jfs_sb_info *sbi = JFS_SBI(dentry->d_sb);
	s64 maxinodes;
	struct inomap *imap = JFS_IP(sbi->ipimap)->i_imap;

	jfs_info("In jfs_statfs");
	buf->f_type = JFS_SUPER_MAGIC;
	buf->f_bsize = sbi->bsize;
	buf->f_blocks = sbi->bmap->db_mapsize;
	buf->f_bfree = sbi->bmap->db_nfree;
	buf->f_bavail = sbi->bmap->db_nfree;
	/*
	 * If we really return the number of allocated & free inodes, some
	 * applications will fail because they won't see enough free inodes.
	 * We'll try to calculate some guess as to how may inodes we can
	 * really allocate
	 *
	 * buf->f_files = atomic_read(&imap->im_numinos);
	 * buf->f_ffree = atomic_read(&imap->im_numfree);
	 */
	maxinodes = min((s64) atomic_read(&imap->im_numinos) +
			((sbi->bmap->db_nfree >> imap->im_l2nbperiext)
			 << L2INOSPEREXT), (s64) 0xffffffffLL);
	buf->f_files = maxinodes;
	buf->f_ffree = maxinodes - (atomic_read(&imap->im_numinos) -
				    atomic_read(&imap->im_numfree));
	buf->f_fsid.val[0] = (u32)crc32_le(0, sbi->uuid, sizeof(sbi->uuid)/2);
	buf->f_fsid.val[1] = (u32)crc32_le(0, sbi->uuid + sizeof(sbi->uuid)/2,
					sizeof(sbi->uuid)/2);

	buf->f_namelen = JFS_NAME_MAX;
	return 0;
}

static void jfs_put_super(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	int rc;

	jfs_info("In jfs_put_super");

	dquot_disable(sb, -1, DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);

	lock_kernel();

	rc = jfs_umount(sb);
	if (rc)
		jfs_err("jfs_umount failed with return code %d", rc);

	unload_nls(sbi->nls_tab);

	truncate_inode_pages(sbi->direct_inode->i_mapping, 0);
	iput(sbi->direct_inode);

	kfree(sbi);

	unlock_kernel();
}

enum {
	Opt_integrity, Opt_nointegrity, Opt_iocharset, Opt_resize,
	Opt_resize_nosize, Opt_errors, Opt_ignore, Opt_err, Opt_quota,
	Opt_usrquota, Opt_grpquota, Opt_uid, Opt_gid, Opt_umask
};

static const match_table_t tokens = {
	{Opt_integrity, "integrity"},
	{Opt_nointegrity, "nointegrity"},
	{Opt_iocharset, "iocharset=%s"},
	{Opt_resize, "resize=%u"},
	{Opt_resize_nosize, "resize"},
	{Opt_errors, "errors=%s"},
	{Opt_ignore, "noquota"},
	{Opt_ignore, "quota"},
	{Opt_usrquota, "usrquota"},
	{Opt_grpquota, "grpquota"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%u"},
	{Opt_err, NULL}
};

static int parse_options(char *options, struct super_block *sb, s64 *newLVSize,
			 int *flag)
{
	void *nls_map = (void *)-1;	/* -1: no change;  NULL: none */
	char *p;
	struct jfs_sb_info *sbi = JFS_SBI(sb);

	*newLVSize = 0;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_integrity:
			*flag &= ~JFS_NOINTEGRITY;
			break;
		case Opt_nointegrity:
			*flag |= JFS_NOINTEGRITY;
			break;
		case Opt_ignore:
			/* Silently ignore the quota options */
			/* Don't do anything ;-) */
			break;
		case Opt_iocharset:
			if (nls_map && nls_map != (void *) -1)
				unload_nls(nls_map);
			if (!strcmp(args[0].from, "none"))
				nls_map = NULL;
			else {
				nls_map = load_nls(args[0].from);
				if (!nls_map) {
					printk(KERN_ERR
					       "JFS: charset not found\n");
					goto cleanup;
				}
			}
			break;
		case Opt_resize:
		{
			char *resize = args[0].from;
			*newLVSize = simple_strtoull(resize, &resize, 0);
			break;
		}
		case Opt_resize_nosize:
		{
			*newLVSize = sb->s_bdev->bd_inode->i_size >>
				sb->s_blocksize_bits;
			if (*newLVSize == 0)
				printk(KERN_ERR
				       "JFS: Cannot determine volume size\n");
			break;
		}
		case Opt_errors:
		{
			char *errors = args[0].from;
			if (!errors || !*errors)
				goto cleanup;
			if (!strcmp(errors, "continue")) {
				*flag &= ~JFS_ERR_REMOUNT_RO;
				*flag &= ~JFS_ERR_PANIC;
				*flag |= JFS_ERR_CONTINUE;
			} else if (!strcmp(errors, "remount-ro")) {
				*flag &= ~JFS_ERR_CONTINUE;
				*flag &= ~JFS_ERR_PANIC;
				*flag |= JFS_ERR_REMOUNT_RO;
			} else if (!strcmp(errors, "panic")) {
				*flag &= ~JFS_ERR_CONTINUE;
				*flag &= ~JFS_ERR_REMOUNT_RO;
				*flag |= JFS_ERR_PANIC;
			} else {
				printk(KERN_ERR
				       "JFS: %s is an invalid error handler\n",
				       errors);
				goto cleanup;
			}
			break;
		}

#ifdef CONFIG_QUOTA
		case Opt_quota:
		case Opt_usrquota:
			*flag |= JFS_USRQUOTA;
			break;
		case Opt_grpquota:
			*flag |= JFS_GRPQUOTA;
			break;
#else
		case Opt_usrquota:
		case Opt_grpquota:
		case Opt_quota:
			printk(KERN_ERR
			       "JFS: quota operations not supported\n");
			break;
#endif
		case Opt_uid:
		{
			char *uid = args[0].from;
			sbi->uid = simple_strtoul(uid, &uid, 0);
			break;
		}
		case Opt_gid:
		{
			char *gid = args[0].from;
			sbi->gid = simple_strtoul(gid, &gid, 0);
			break;
		}
		case Opt_umask:
		{
			char *umask = args[0].from;
			sbi->umask = simple_strtoul(umask, &umask, 8);
			if (sbi->umask & ~0777) {
				printk(KERN_ERR
				       "JFS: Invalid value of umask\n");
				goto cleanup;
			}
			break;
		}
		default:
			printk("jfs: Unrecognized mount option \"%s\" "
					" or missing value\n", p);
			goto cleanup;
		}
	}

	if (nls_map != (void *) -1) {
		/* Discard old (if remount) */
		unload_nls(sbi->nls_tab);
		sbi->nls_tab = nls_map;
	}
	return 1;

cleanup:
	if (nls_map && nls_map != (void *) -1)
		unload_nls(nls_map);
	return 0;
}

static int jfs_remount(struct super_block *sb, int *flags, char *data)
{
	s64 newLVSize = 0;
	int rc = 0;
	int flag = JFS_SBI(sb)->flag;
	int ret;

	if (!parse_options(data, sb, &newLVSize, &flag)) {
		return -EINVAL;
	}
	lock_kernel();
	if (newLVSize) {
		if (sb->s_flags & MS_RDONLY) {
			printk(KERN_ERR
		  "JFS: resize requires volume to be mounted read-write\n");
			unlock_kernel();
			return -EROFS;
		}
		rc = jfs_extendfs(sb, newLVSize, 0);
		if (rc) {
			unlock_kernel();
			return rc;
		}
	}

	if ((sb->s_flags & MS_RDONLY) && !(*flags & MS_RDONLY)) {
		/*
		 * Invalidate any previously read metadata.  fsck may have
		 * changed the on-disk data since we mounted r/o
		 */
		truncate_inode_pages(JFS_SBI(sb)->direct_inode->i_mapping, 0);

		JFS_SBI(sb)->flag = flag;
		ret = jfs_mount_rw(sb, 1);

		/* mark the fs r/w for quota activity */
		sb->s_flags &= ~MS_RDONLY;

		unlock_kernel();
		dquot_resume(sb, -1);
		return ret;
	}
	if ((!(sb->s_flags & MS_RDONLY)) && (*flags & MS_RDONLY)) {
		rc = dquot_suspend(sb, -1);
		if (rc < 0) {
			unlock_kernel();
			return rc;
		}
		rc = jfs_umount_rw(sb);
		JFS_SBI(sb)->flag = flag;
		unlock_kernel();
		return rc;
	}
	if ((JFS_SBI(sb)->flag & JFS_NOINTEGRITY) != (flag & JFS_NOINTEGRITY))
		if (!(sb->s_flags & MS_RDONLY)) {
			rc = jfs_umount_rw(sb);
			if (rc) {
				unlock_kernel();
				return rc;
			}
			JFS_SBI(sb)->flag = flag;
			ret = jfs_mount_rw(sb, 1);
			unlock_kernel();
			return ret;
		}
	JFS_SBI(sb)->flag = flag;

	unlock_kernel();
	return 0;
}

static int jfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct jfs_sb_info *sbi;
	struct inode *inode;
	int rc;
	s64 newLVSize = 0;
	int flag, ret = -EINVAL;

	lock_kernel();

	jfs_info("In jfs_read_super: s_flags=0x%lx", sb->s_flags);

	if (!new_valid_dev(sb->s_bdev->bd_dev)) {
		unlock_kernel();
		return -EOVERFLOW;
	}

	sbi = kzalloc(sizeof (struct jfs_sb_info), GFP_KERNEL);
	if (!sbi) {
		unlock_kernel();
		return -ENOMEM;
	}
	sb->s_fs_info = sbi;
	sbi->sb = sb;
	sbi->uid = sbi->gid = sbi->umask = -1;

	/* initialize the mount flag and determine the default error handler */
	flag = JFS_ERR_REMOUNT_RO;

	if (!parse_options((char *) data, sb, &newLVSize, &flag))
		goto out_kfree;
	sbi->flag = flag;

#ifdef CONFIG_JFS_POSIX_ACL
	sb->s_flags |= MS_POSIXACL;
#endif

	if (newLVSize) {
		printk(KERN_ERR "resize option for remount only\n");
		goto out_kfree;
	}

	/*
	 * Initialize blocksize to 4K.
	 */
	sb_set_blocksize(sb, PSIZE);

	/*
	 * Set method vectors.
	 */
	sb->s_op = &jfs_super_operations;
	sb->s_export_op = &jfs_export_operations;
#ifdef CONFIG_QUOTA
	sb->dq_op = &dquot_operations;
	sb->s_qcop = &dquot_quotactl_ops;
#endif

	/*
	 * Initialize direct-mapping inode/address-space
	 */
	inode = new_inode(sb);
	if (inode == NULL) {
		ret = -ENOMEM;
		goto out_unload;
	}
	inode->i_ino = 0;
	inode->i_nlink = 1;
	inode->i_size = sb->s_bdev->bd_inode->i_size;
	inode->i_mapping->a_ops = &jfs_metapage_aops;
	insert_inode_hash(inode);
	mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS);

	sbi->direct_inode = inode;

	rc = jfs_mount(sb);
	if (rc) {
		if (!silent) {
			jfs_err("jfs_mount failed w/return code = %d", rc);
		}
		goto out_mount_failed;
	}
	if (sb->s_flags & MS_RDONLY)
		sbi->log = NULL;
	else {
		rc = jfs_mount_rw(sb, 0);
		if (rc) {
			if (!silent) {
				jfs_err("jfs_mount_rw failed, return code = %d",
					rc);
			}
			goto out_no_rw;
		}
	}

	sb->s_magic = JFS_SUPER_MAGIC;

	inode = jfs_iget(sb, ROOT_I);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto out_no_rw;
	}
	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root)
		goto out_no_root;

	if (sbi->mntflag & JFS_OS2)
		sb->s_root->d_op = &jfs_ci_dentry_operations;

	/* logical blocks are represented by 40 bits in pxd_t, etc. */
	sb->s_maxbytes = ((u64) sb->s_blocksize) << 40;
#if BITS_PER_LONG == 32
	/*
	 * Page cache is indexed by long.
	 * I would use MAX_LFS_FILESIZE, but it's only half as big
	 */
	sb->s_maxbytes = min(((u64) PAGE_CACHE_SIZE << 32) - 1, (u64)sb->s_maxbytes);
#endif
	sb->s_time_gran = 1;
	unlock_kernel();
	return 0;

out_no_root:
	jfs_err("jfs_read_super: get root dentry failed");
	iput(inode);

out_no_rw:
	rc = jfs_umount(sb);
	if (rc) {
		jfs_err("jfs_umount failed with return code %d", rc);
	}
out_mount_failed:
	filemap_write_and_wait(sbi->direct_inode->i_mapping);
	truncate_inode_pages(sbi->direct_inode->i_mapping, 0);
	make_bad_inode(sbi->direct_inode);
	iput(sbi->direct_inode);
	sbi->direct_inode = NULL;
out_unload:
	if (sbi->nls_tab)
		unload_nls(sbi->nls_tab);
out_kfree:
	kfree(sbi);
	unlock_kernel();
	return ret;
}

static int jfs_freeze(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct jfs_log *log = sbi->log;

	if (!(sb->s_flags & MS_RDONLY)) {
		txQuiesce(sb);
		lmLogShutdown(log);
		updateSuper(sb, FM_CLEAN);
	}
	return 0;
}

static int jfs_unfreeze(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct jfs_log *log = sbi->log;
	int rc = 0;

	if (!(sb->s_flags & MS_RDONLY)) {
		updateSuper(sb, FM_MOUNT);
		if ((rc = lmLogInit(log)))
			jfs_err("jfs_unlock failed with return code %d", rc);
		else
			txResume(sb);
	}
	return 0;
}

static int jfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, jfs_fill_super,
			   mnt);
}

static int jfs_sync_fs(struct super_block *sb, int wait)
{
	struct jfs_log *log = JFS_SBI(sb)->log;

	/* log == NULL indicates read-only mount */
	if (log) {
		jfs_flush_journal(log, wait);
		jfs_syncpt(log, 0);
	}

	return 0;
}

static int jfs_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct jfs_sb_info *sbi = JFS_SBI(vfs->mnt_sb);

	if (sbi->uid != -1)
		seq_printf(seq, ",uid=%d", sbi->uid);
	if (sbi->gid != -1)
		seq_printf(seq, ",gid=%d", sbi->gid);
	if (sbi->umask != -1)
		seq_printf(seq, ",umask=%03o", sbi->umask);
	if (sbi->flag & JFS_NOINTEGRITY)
		seq_puts(seq, ",nointegrity");
	if (sbi->nls_tab)
		seq_printf(seq, ",iocharset=%s", sbi->nls_tab->charset);
	if (sbi->flag & JFS_ERR_CONTINUE)
		seq_printf(seq, ",errors=continue");
	if (sbi->flag & JFS_ERR_PANIC)
		seq_printf(seq, ",errors=panic");

#ifdef CONFIG_QUOTA
	if (sbi->flag & JFS_USRQUOTA)
		seq_puts(seq, ",usrquota");

	if (sbi->flag & JFS_GRPQUOTA)
		seq_puts(seq, ",grpquota");
#endif

	return 0;
}

#ifdef CONFIG_QUOTA

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and noone else should touch the files)
 * we don't have to be afraid of races */
static ssize_t jfs_quota_read(struct super_block *sb, int type, char *data,
			      size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t toread;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;
	loff_t i_size = i_size_read(inode);

	if (off > i_size)
		return 0;
	if (off+len > i_size)
		len = i_size-off;
	toread = len;
	while (toread > 0) {
		tocopy = sb->s_blocksize - offset < toread ?
				sb->s_blocksize - offset : toread;

		tmp_bh.b_state = 0;
		tmp_bh.b_size = 1 << inode->i_blkbits;
		err = jfs_get_block(inode, blk, &tmp_bh, 0);
		if (err)
			return err;
		if (!buffer_mapped(&tmp_bh))	/* A hole? */
			memset(data, 0, tocopy);
		else {
			bh = sb_bread(sb, tmp_bh.b_blocknr);
			if (!bh)
				return -EIO;
			memcpy(data, bh->b_data+offset, tocopy);
			brelse(bh);
		}
		offset = 0;
		toread -= tocopy;
		data += tocopy;
		blk++;
	}
	return len;
}

/* Write to quotafile */
static ssize_t jfs_quota_write(struct super_block *sb, int type,
			       const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t towrite = len;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;

	mutex_lock(&inode->i_mutex);
	while (towrite > 0) {
		tocopy = sb->s_blocksize - offset < towrite ?
				sb->s_blocksize - offset : towrite;

		tmp_bh.b_state = 0;
		tmp_bh.b_size = 1 << inode->i_blkbits;
		err = jfs_get_block(inode, blk, &tmp_bh, 1);
		if (err)
			goto out;
		if (offset || tocopy != sb->s_blocksize)
			bh = sb_bread(sb, tmp_bh.b_blocknr);
		else
			bh = sb_getblk(sb, tmp_bh.b_blocknr);
		if (!bh) {
			err = -EIO;
			goto out;
		}
		lock_buffer(bh);
		memcpy(bh->b_data+offset, data, tocopy);
		flush_dcache_page(bh->b_page);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		brelse(bh);
		offset = 0;
		towrite -= tocopy;
		data += tocopy;
		blk++;
	}
out:
	if (len == towrite) {
		mutex_unlock(&inode->i_mutex);
		return err;
	}
	if (inode->i_size < off+len-towrite)
		i_size_write(inode, off+len-towrite);
	inode->i_version++;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	mutex_unlock(&inode->i_mutex);
	return len - towrite;
}

#endif

static const struct super_operations jfs_super_operations = {
	.alloc_inode	= jfs_alloc_inode,
	.destroy_inode	= jfs_destroy_inode,
	.dirty_inode	= jfs_dirty_inode,
	.write_inode	= jfs_write_inode,
	.evict_inode	= jfs_evict_inode,
	.put_super	= jfs_put_super,
	.sync_fs	= jfs_sync_fs,
	.freeze_fs	= jfs_freeze,
	.unfreeze_fs	= jfs_unfreeze,
	.statfs		= jfs_statfs,
	.remount_fs	= jfs_remount,
	.show_options	= jfs_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= jfs_quota_read,
	.quota_write	= jfs_quota_write,
#endif
};

static const struct export_operations jfs_export_operations = {
	.fh_to_dentry	= jfs_fh_to_dentry,
	.fh_to_parent	= jfs_fh_to_parent,
	.get_parent	= jfs_get_parent,
};

static struct file_system_type jfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "jfs",
	.get_sb		= jfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static void init_once(void *foo)
{
	struct jfs_inode_info *jfs_ip = (struct jfs_inode_info *) foo;

	memset(jfs_ip, 0, sizeof(struct jfs_inode_info));
	INIT_LIST_HEAD(&jfs_ip->anon_inode_list);
	init_rwsem(&jfs_ip->rdwrlock);
	mutex_init(&jfs_ip->commit_mutex);
	init_rwsem(&jfs_ip->xattr_sem);
	spin_lock_init(&jfs_ip->ag_lock);
	jfs_ip->active_ag = -1;
	inode_init_once(&jfs_ip->vfs_inode);
}

static int __init init_jfs_fs(void)
{
	int i;
	int rc;

	jfs_inode_cachep =
	    kmem_cache_create("jfs_ip", sizeof(struct jfs_inode_info), 0,
			    SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
			    init_once);
	if (jfs_inode_cachep == NULL)
		return -ENOMEM;

	/*
	 * Metapage initialization
	 */
	rc = metapage_init();
	if (rc) {
		jfs_err("metapage_init failed w/rc = %d", rc);
		goto free_slab;
	}

	/*
	 * Transaction Manager initialization
	 */
	rc = txInit();
	if (rc) {
		jfs_err("txInit failed w/rc = %d", rc);
		goto free_metapage;
	}

	/*
	 * I/O completion thread (endio)
	 */
	jfsIOthread = kthread_run(jfsIOWait, NULL, "jfsIO");
	if (IS_ERR(jfsIOthread)) {
		rc = PTR_ERR(jfsIOthread);
		jfs_err("init_jfs_fs: fork failed w/rc = %d", rc);
		goto end_txmngr;
	}

	if (commit_threads < 1)
		commit_threads = num_online_cpus();
	if (commit_threads > MAX_COMMIT_THREADS)
		commit_threads = MAX_COMMIT_THREADS;

	for (i = 0; i < commit_threads; i++) {
		jfsCommitThread[i] = kthread_run(jfs_lazycommit, NULL, "jfsCommit");
		if (IS_ERR(jfsCommitThread[i])) {
			rc = PTR_ERR(jfsCommitThread[i]);
			jfs_err("init_jfs_fs: fork failed w/rc = %d", rc);
			commit_threads = i;
			goto kill_committask;
		}
	}

	jfsSyncThread = kthread_run(jfs_sync, NULL, "jfsSync");
	if (IS_ERR(jfsSyncThread)) {
		rc = PTR_ERR(jfsSyncThread);
		jfs_err("init_jfs_fs: fork failed w/rc = %d", rc);
		goto kill_committask;
	}

#ifdef PROC_FS_JFS
	jfs_proc_init();
#endif

	return register_filesystem(&jfs_fs_type);

kill_committask:
	for (i = 0; i < commit_threads; i++)
		kthread_stop(jfsCommitThread[i]);
	kthread_stop(jfsIOthread);
end_txmngr:
	txExit();
free_metapage:
	metapage_exit();
free_slab:
	kmem_cache_destroy(jfs_inode_cachep);
	return rc;
}

static void __exit exit_jfs_fs(void)
{
	int i;

	jfs_info("exit_jfs_fs called");

	txExit();
	metapage_exit();

	kthread_stop(jfsIOthread);
	for (i = 0; i < commit_threads; i++)
		kthread_stop(jfsCommitThread[i]);
	kthread_stop(jfsSyncThread);
#ifdef PROC_FS_JFS
	jfs_proc_clean();
#endif
	unregister_filesystem(&jfs_fs_type);
	kmem_cache_destroy(jfs_inode_cachep);
}

module_init(init_jfs_fs)
module_exit(exit_jfs_fs)
