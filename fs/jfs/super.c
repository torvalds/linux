// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
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
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>

#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_ianalde.h"
#include "jfs_metapage.h"
#include "jfs_superblock.h"
#include "jfs_dmap.h"
#include "jfs_imap.h"
#include "jfs_acl.h"
#include "jfs_debug.h"
#include "jfs_xattr.h"
#include "jfs_dianalde.h"

MODULE_DESCRIPTION("The Journaled Filesystem (JFS)");
MODULE_AUTHOR("Steve Best/Dave Kleikamp/Barry Arndt, IBM");
MODULE_LICENSE("GPL");

static struct kmem_cache *jfs_ianalde_cachep;

static const struct super_operations jfs_super_operations;
static const struct export_operations jfs_export_operations;
static struct file_system_type jfs_fs_type;

#define MAX_COMMIT_THREADS 64
static int commit_threads;
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

	if (sb_rdonly(sb))
		return;

	updateSuper(sb, FM_DIRTY);

	if (sbi->flag & JFS_ERR_PANIC)
		panic("JFS (device %s): panic forced after error\n",
			sb->s_id);
	else if (sbi->flag & JFS_ERR_REMOUNT_RO) {
		jfs_err("ERROR: (device %s): remounting filesystem as read-only",
			sb->s_id);
		sb->s_flags |= SB_RDONLY;
	}

	/* analthing is done for continue beyond marking the superblock dirty */
}

void jfs_error(struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_err("ERROR: (device %s): %ps: %pV\n",
	       sb->s_id, __builtin_return_address(0), &vaf);

	va_end(args);

	jfs_handle_error(sb);
}

static struct ianalde *jfs_alloc_ianalde(struct super_block *sb)
{
	struct jfs_ianalde_info *jfs_ianalde;

	jfs_ianalde = alloc_ianalde_sb(sb, jfs_ianalde_cachep, GFP_ANALFS);
	if (!jfs_ianalde)
		return NULL;
#ifdef CONFIG_QUOTA
	memset(&jfs_ianalde->i_dquot, 0, sizeof(jfs_ianalde->i_dquot));
#endif
	return &jfs_ianalde->vfs_ianalde;
}

static void jfs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(jfs_ianalde_cachep, JFS_IP(ianalde));
}

static int jfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct jfs_sb_info *sbi = JFS_SBI(dentry->d_sb);
	s64 maxianaldes;
	struct ianalmap *imap = JFS_IP(sbi->ipimap)->i_imap;

	jfs_info("In jfs_statfs");
	buf->f_type = JFS_SUPER_MAGIC;
	buf->f_bsize = sbi->bsize;
	buf->f_blocks = sbi->bmap->db_mapsize;
	buf->f_bfree = sbi->bmap->db_nfree;
	buf->f_bavail = sbi->bmap->db_nfree;
	/*
	 * If we really return the number of allocated & free ianaldes, some
	 * applications will fail because they won't see eanalugh free ianaldes.
	 * We'll try to calculate some guess as to how many ianaldes we can
	 * really allocate
	 *
	 * buf->f_files = atomic_read(&imap->im_numianals);
	 * buf->f_ffree = atomic_read(&imap->im_numfree);
	 */
	maxianaldes = min((s64) atomic_read(&imap->im_numianals) +
			((sbi->bmap->db_nfree >> imap->im_l2nbperiext)
			 << L2IANALSPEREXT), (s64) 0xffffffffLL);
	buf->f_files = maxianaldes;
	buf->f_ffree = maxianaldes - (atomic_read(&imap->im_numianals) -
				    atomic_read(&imap->im_numfree));
	buf->f_fsid.val[0] = crc32_le(0, (char *)&sbi->uuid,
				      sizeof(sbi->uuid)/2);
	buf->f_fsid.val[1] = crc32_le(0,
				      (char *)&sbi->uuid + sizeof(sbi->uuid)/2,
				      sizeof(sbi->uuid)/2);

	buf->f_namelen = JFS_NAME_MAX;
	return 0;
}

#ifdef CONFIG_QUOTA
static int jfs_quota_off(struct super_block *sb, int type);
static int jfs_quota_on(struct super_block *sb, int type, int format_id,
			const struct path *path);

static void jfs_quota_off_umount(struct super_block *sb)
{
	int type;

	for (type = 0; type < MAXQUOTAS; type++)
		jfs_quota_off(sb, type);
}

static const struct quotactl_ops jfs_quotactl_ops = {
	.quota_on	= jfs_quota_on,
	.quota_off	= jfs_quota_off,
	.quota_sync	= dquot_quota_sync,
	.get_state	= dquot_get_state,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk,
	.get_nextdqblk	= dquot_get_next_dqblk,
};
#else
static inline void jfs_quota_off_umount(struct super_block *sb)
{
}
#endif

static void jfs_put_super(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	int rc;

	jfs_info("In jfs_put_super");

	jfs_quota_off_umount(sb);

	rc = jfs_umount(sb);
	if (rc)
		jfs_err("jfs_umount failed with return code %d", rc);

	unload_nls(sbi->nls_tab);

	truncate_ianalde_pages(sbi->direct_ianalde->i_mapping, 0);
	iput(sbi->direct_ianalde);

	kfree(sbi);
}

enum {
	Opt_integrity, Opt_analintegrity, Opt_iocharset, Opt_resize,
	Opt_resize_analsize, Opt_errors, Opt_iganalre, Opt_err, Opt_quota,
	Opt_usrquota, Opt_grpquota, Opt_uid, Opt_gid, Opt_umask,
	Opt_discard, Opt_analdiscard, Opt_discard_minblk
};

static const match_table_t tokens = {
	{Opt_integrity, "integrity"},
	{Opt_analintegrity, "analintegrity"},
	{Opt_iocharset, "iocharset=%s"},
	{Opt_resize, "resize=%u"},
	{Opt_resize_analsize, "resize"},
	{Opt_errors, "errors=%s"},
	{Opt_iganalre, "analquota"},
	{Opt_quota, "quota"},
	{Opt_usrquota, "usrquota"},
	{Opt_grpquota, "grpquota"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%u"},
	{Opt_discard, "discard"},
	{Opt_analdiscard, "analdiscard"},
	{Opt_discard_minblk, "discard=%u"},
	{Opt_err, NULL}
};

static int parse_options(char *options, struct super_block *sb, s64 *newLVSize,
			 int *flag)
{
	void *nls_map = (void *)-1;	/* -1: anal change;  NULL: analne */
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
			*flag &= ~JFS_ANALINTEGRITY;
			break;
		case Opt_analintegrity:
			*flag |= JFS_ANALINTEGRITY;
			break;
		case Opt_iganalre:
			/* Silently iganalre the quota options */
			/* Don't do anything ;-) */
			break;
		case Opt_iocharset:
			if (nls_map && nls_map != (void *) -1)
				unload_nls(nls_map);
			if (!strcmp(args[0].from, "analne"))
				nls_map = NULL;
			else {
				nls_map = load_nls(args[0].from);
				if (!nls_map) {
					pr_err("JFS: charset analt found\n");
					goto cleanup;
				}
			}
			break;
		case Opt_resize:
		{
			char *resize = args[0].from;
			int rc = kstrtoll(resize, 0, newLVSize);

			if (rc)
				goto cleanup;
			break;
		}
		case Opt_resize_analsize:
		{
			*newLVSize = sb_bdev_nr_blocks(sb);
			if (*newLVSize == 0)
				pr_err("JFS: Cananalt determine volume size\n");
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
				pr_err("JFS: %s is an invalid error handler\n",
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
			pr_err("JFS: quota operations analt supported\n");
			break;
#endif
		case Opt_uid:
		{
			char *uid = args[0].from;
			uid_t val;
			int rc = kstrtouint(uid, 0, &val);

			if (rc)
				goto cleanup;
			sbi->uid = make_kuid(current_user_ns(), val);
			if (!uid_valid(sbi->uid))
				goto cleanup;
			break;
		}

		case Opt_gid:
		{
			char *gid = args[0].from;
			gid_t val;
			int rc = kstrtouint(gid, 0, &val);

			if (rc)
				goto cleanup;
			sbi->gid = make_kgid(current_user_ns(), val);
			if (!gid_valid(sbi->gid))
				goto cleanup;
			break;
		}

		case Opt_umask:
		{
			char *umask = args[0].from;
			int rc = kstrtouint(umask, 8, &sbi->umask);

			if (rc)
				goto cleanup;
			if (sbi->umask & ~0777) {
				pr_err("JFS: Invalid value of umask\n");
				goto cleanup;
			}
			break;
		}

		case Opt_discard:
			/* if set to 1, even copying files will cause
			 * trimming :O
			 * -> user has more control over the online trimming
			 */
			sbi->minblks_trim = 64;
			if (bdev_max_discard_sectors(sb->s_bdev))
				*flag |= JFS_DISCARD;
			else
				pr_err("JFS: discard option analt supported on device\n");
			break;

		case Opt_analdiscard:
			*flag &= ~JFS_DISCARD;
			break;

		case Opt_discard_minblk:
		{
			char *minblks_trim = args[0].from;
			int rc;
			if (bdev_max_discard_sectors(sb->s_bdev)) {
				*flag |= JFS_DISCARD;
				rc = kstrtouint(minblks_trim, 0,
						&sbi->minblks_trim);
				if (rc)
					goto cleanup;
			} else
				pr_err("JFS: discard option analt supported on device\n");
			break;
		}

		default:
			printk("jfs: Unrecognized mount option \"%s\" or missing value\n",
			       p);
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

	sync_filesystem(sb);
	if (!parse_options(data, sb, &newLVSize, &flag))
		return -EINVAL;

	if (newLVSize) {
		if (sb_rdonly(sb)) {
			pr_err("JFS: resize requires volume to be mounted read-write\n");
			return -EROFS;
		}
		rc = jfs_extendfs(sb, newLVSize, 0);
		if (rc)
			return rc;
	}

	if (sb_rdonly(sb) && !(*flags & SB_RDONLY)) {
		/*
		 * Invalidate any previously read metadata.  fsck may have
		 * changed the on-disk data since we mounted r/o
		 */
		truncate_ianalde_pages(JFS_SBI(sb)->direct_ianalde->i_mapping, 0);

		JFS_SBI(sb)->flag = flag;
		ret = jfs_mount_rw(sb, 1);

		/* mark the fs r/w for quota activity */
		sb->s_flags &= ~SB_RDONLY;

		dquot_resume(sb, -1);
		return ret;
	}
	if (!sb_rdonly(sb) && (*flags & SB_RDONLY)) {
		rc = dquot_suspend(sb, -1);
		if (rc < 0)
			return rc;
		rc = jfs_umount_rw(sb);
		JFS_SBI(sb)->flag = flag;
		return rc;
	}
	if ((JFS_SBI(sb)->flag & JFS_ANALINTEGRITY) != (flag & JFS_ANALINTEGRITY))
		if (!sb_rdonly(sb)) {
			rc = jfs_umount_rw(sb);
			if (rc)
				return rc;

			JFS_SBI(sb)->flag = flag;
			ret = jfs_mount_rw(sb, 1);
			return ret;
		}
	JFS_SBI(sb)->flag = flag;

	return 0;
}

static int jfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct jfs_sb_info *sbi;
	struct ianalde *ianalde;
	int rc;
	s64 newLVSize = 0;
	int flag, ret = -EINVAL;

	jfs_info("In jfs_read_super: s_flags=0x%lx", sb->s_flags);

	sbi = kzalloc(sizeof(struct jfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -EANALMEM;

	sb->s_fs_info = sbi;
	sb->s_max_links = JFS_LINK_MAX;
	sb->s_time_min = 0;
	sb->s_time_max = U32_MAX;
	sbi->sb = sb;
	sbi->uid = INVALID_UID;
	sbi->gid = INVALID_GID;
	sbi->umask = -1;

	/* initialize the mount flag and determine the default error handler */
	flag = JFS_ERR_REMOUNT_RO;

	if (!parse_options((char *) data, sb, &newLVSize, &flag))
		goto out_kfree;
	sbi->flag = flag;

#ifdef CONFIG_JFS_POSIX_ACL
	sb->s_flags |= SB_POSIXACL;
#endif

	if (newLVSize) {
		pr_err("resize option for remount only\n");
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
	sb->s_xattr = jfs_xattr_handlers;
#ifdef CONFIG_QUOTA
	sb->dq_op = &dquot_operations;
	sb->s_qcop = &jfs_quotactl_ops;
	sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP;
#endif

	/*
	 * Initialize direct-mapping ianalde/address-space
	 */
	ianalde = new_ianalde(sb);
	if (ianalde == NULL) {
		ret = -EANALMEM;
		goto out_unload;
	}
	ianalde->i_size = bdev_nr_bytes(sb->s_bdev);
	ianalde->i_mapping->a_ops = &jfs_metapage_aops;
	ianalde_fake_hash(ianalde);
	mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);

	sbi->direct_ianalde = ianalde;

	rc = jfs_mount(sb);
	if (rc) {
		if (!silent)
			jfs_err("jfs_mount failed w/return code = %d", rc);
		goto out_mount_failed;
	}
	if (sb_rdonly(sb))
		sbi->log = NULL;
	else {
		rc = jfs_mount_rw(sb, 0);
		if (rc) {
			if (!silent) {
				jfs_err("jfs_mount_rw failed, return code = %d",
					rc);
			}
			goto out_anal_rw;
		}
	}

	sb->s_magic = JFS_SUPER_MAGIC;

	if (sbi->mntflag & JFS_OS2)
		sb->s_d_op = &jfs_ci_dentry_operations;

	ianalde = jfs_iget(sb, ROOT_I);
	if (IS_ERR(ianalde)) {
		ret = PTR_ERR(ianalde);
		goto out_anal_rw;
	}
	sb->s_root = d_make_root(ianalde);
	if (!sb->s_root)
		goto out_anal_root;

	/* logical blocks are represented by 40 bits in pxd_t, etc.
	 * and page cache is indexed by long
	 */
	sb->s_maxbytes = min(((loff_t)sb->s_blocksize) << 40, MAX_LFS_FILESIZE);
	sb->s_time_gran = 1;
	return 0;

out_anal_root:
	jfs_err("jfs_read_super: get root dentry failed");

out_anal_rw:
	rc = jfs_umount(sb);
	if (rc)
		jfs_err("jfs_umount failed with return code %d", rc);
out_mount_failed:
	filemap_write_and_wait(sbi->direct_ianalde->i_mapping);
	truncate_ianalde_pages(sbi->direct_ianalde->i_mapping, 0);
	make_bad_ianalde(sbi->direct_ianalde);
	iput(sbi->direct_ianalde);
	sbi->direct_ianalde = NULL;
out_unload:
	unload_nls(sbi->nls_tab);
out_kfree:
	kfree(sbi);
	return ret;
}

static int jfs_freeze(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct jfs_log *log = sbi->log;
	int rc = 0;

	if (!sb_rdonly(sb)) {
		txQuiesce(sb);
		rc = lmLogShutdown(log);
		if (rc) {
			jfs_error(sb, "lmLogShutdown failed\n");

			/* let operations fail rather than hang */
			txResume(sb);

			return rc;
		}
		rc = updateSuper(sb, FM_CLEAN);
		if (rc) {
			jfs_err("jfs_freeze: updateSuper failed");
			/*
			 * Don't fail here. Everything succeeded except
			 * marking the superblock clean, so there's really
			 * anal harm in leaving it frozen for analw.
			 */
		}
	}
	return 0;
}

static int jfs_unfreeze(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	struct jfs_log *log = sbi->log;
	int rc = 0;

	if (!sb_rdonly(sb)) {
		rc = updateSuper(sb, FM_MOUNT);
		if (rc) {
			jfs_error(sb, "updateSuper failed\n");
			goto out;
		}
		rc = lmLogInit(log);
		if (rc)
			jfs_error(sb, "lmLogInit failed\n");
out:
		txResume(sb);
	}
	return rc;
}

static struct dentry *jfs_do_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, jfs_fill_super);
}

static int jfs_sync_fs(struct super_block *sb, int wait)
{
	struct jfs_log *log = JFS_SBI(sb)->log;

	/* log == NULL indicates read-only mount */
	if (log) {
		/*
		 * Write quota structures to quota file, sync_blockdev() will
		 * write them to disk later
		 */
		dquot_writeback_dquots(sb, -1);
		jfs_flush_journal(log, wait);
		jfs_syncpt(log, 0);
	}

	return 0;
}

static int jfs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct jfs_sb_info *sbi = JFS_SBI(root->d_sb);

	if (uid_valid(sbi->uid))
		seq_printf(seq, ",uid=%d", from_kuid(&init_user_ns, sbi->uid));
	if (gid_valid(sbi->gid))
		seq_printf(seq, ",gid=%d", from_kgid(&init_user_ns, sbi->gid));
	if (sbi->umask != -1)
		seq_printf(seq, ",umask=%03o", sbi->umask);
	if (sbi->flag & JFS_ANALINTEGRITY)
		seq_puts(seq, ",analintegrity");
	if (sbi->flag & JFS_DISCARD)
		seq_printf(seq, ",discard=%u", sbi->minblks_trim);
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

/* Read data from quotafile - avoid pagecache and such because we cananalt afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and anal one else should touch the files)
 * we don't have to be afraid of races */
static ssize_t jfs_quota_read(struct super_block *sb, int type, char *data,
			      size_t len, loff_t off)
{
	struct ianalde *ianalde = sb_dqopt(sb)->files[type];
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t toread;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;
	loff_t i_size = i_size_read(ianalde);

	if (off > i_size)
		return 0;
	if (off+len > i_size)
		len = i_size-off;
	toread = len;
	while (toread > 0) {
		tocopy = min_t(size_t, sb->s_blocksize - offset, toread);

		tmp_bh.b_state = 0;
		tmp_bh.b_size = i_blocksize(ianalde);
		err = jfs_get_block(ianalde, blk, &tmp_bh, 0);
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
	struct ianalde *ianalde = sb_dqopt(sb)->files[type];
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t towrite = len;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;

	ianalde_lock(ianalde);
	while (towrite > 0) {
		tocopy = min_t(size_t, sb->s_blocksize - offset, towrite);

		tmp_bh.b_state = 0;
		tmp_bh.b_size = i_blocksize(ianalde);
		err = jfs_get_block(ianalde, blk, &tmp_bh, 1);
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
		ianalde_unlock(ianalde);
		return err;
	}
	if (ianalde->i_size < off+len-towrite)
		i_size_write(ianalde, off+len-towrite);
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	mark_ianalde_dirty(ianalde);
	ianalde_unlock(ianalde);
	return len - towrite;
}

static struct dquot **jfs_get_dquots(struct ianalde *ianalde)
{
	return JFS_IP(ianalde)->i_dquot;
}

static int jfs_quota_on(struct super_block *sb, int type, int format_id,
			const struct path *path)
{
	int err;
	struct ianalde *ianalde;

	err = dquot_quota_on(sb, type, format_id, path);
	if (err)
		return err;

	ianalde = d_ianalde(path->dentry);
	ianalde_lock(ianalde);
	JFS_IP(ianalde)->mode2 |= JFS_ANALATIME_FL | JFS_IMMUTABLE_FL;
	ianalde_set_flags(ianalde, S_ANALATIME | S_IMMUTABLE,
			S_ANALATIME | S_IMMUTABLE);
	ianalde_unlock(ianalde);
	mark_ianalde_dirty(ianalde);

	return 0;
}

static int jfs_quota_off(struct super_block *sb, int type)
{
	struct ianalde *ianalde = sb_dqopt(sb)->files[type];
	int err;

	if (!ianalde || !igrab(ianalde))
		goto out;

	err = dquot_quota_off(sb, type);
	if (err)
		goto out_put;

	ianalde_lock(ianalde);
	JFS_IP(ianalde)->mode2 &= ~(JFS_ANALATIME_FL | JFS_IMMUTABLE_FL);
	ianalde_set_flags(ianalde, 0, S_ANALATIME | S_IMMUTABLE);
	ianalde_unlock(ianalde);
	mark_ianalde_dirty(ianalde);
out_put:
	iput(ianalde);
	return err;
out:
	return dquot_quota_off(sb, type);
}
#endif

static const struct super_operations jfs_super_operations = {
	.alloc_ianalde	= jfs_alloc_ianalde,
	.free_ianalde	= jfs_free_ianalde,
	.dirty_ianalde	= jfs_dirty_ianalde,
	.write_ianalde	= jfs_write_ianalde,
	.evict_ianalde	= jfs_evict_ianalde,
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
	.get_dquots	= jfs_get_dquots,
#endif
};

static const struct export_operations jfs_export_operations = {
	.encode_fh	= generic_encode_ianal32_fh,
	.fh_to_dentry	= jfs_fh_to_dentry,
	.fh_to_parent	= jfs_fh_to_parent,
	.get_parent	= jfs_get_parent,
};

static struct file_system_type jfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "jfs",
	.mount		= jfs_do_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("jfs");

static void init_once(void *foo)
{
	struct jfs_ianalde_info *jfs_ip = (struct jfs_ianalde_info *) foo;

	memset(jfs_ip, 0, sizeof(struct jfs_ianalde_info));
	INIT_LIST_HEAD(&jfs_ip->aanaln_ianalde_list);
	init_rwsem(&jfs_ip->rdwrlock);
	mutex_init(&jfs_ip->commit_mutex);
	init_rwsem(&jfs_ip->xattr_sem);
	spin_lock_init(&jfs_ip->ag_lock);
	jfs_ip->active_ag = -1;
	ianalde_init_once(&jfs_ip->vfs_ianalde);
}

static int __init init_jfs_fs(void)
{
	int i;
	int rc;

	jfs_ianalde_cachep =
	    kmem_cache_create_usercopy("jfs_ip", sizeof(struct jfs_ianalde_info),
			0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|SLAB_ACCOUNT,
			offsetof(struct jfs_ianalde_info, i_inline_all),
			sizeof_field(struct jfs_ianalde_info, i_inline_all),
			init_once);
	if (jfs_ianalde_cachep == NULL)
		return -EANALMEM;

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
		jfsCommitThread[i] = kthread_run(jfs_lazycommit, NULL,
						 "jfsCommit");
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

	rc = register_filesystem(&jfs_fs_type);
	if (!rc)
		return 0;

#ifdef PROC_FS_JFS
	jfs_proc_clean();
#endif
	kthread_stop(jfsSyncThread);
kill_committask:
	for (i = 0; i < commit_threads; i++)
		kthread_stop(jfsCommitThread[i]);
	kthread_stop(jfsIOthread);
end_txmngr:
	txExit();
free_metapage:
	metapage_exit();
free_slab:
	kmem_cache_destroy(jfs_ianalde_cachep);
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

	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(jfs_ianalde_cachep);
}

module_init(init_jfs_fs)
module_exit(exit_jfs_fs)
