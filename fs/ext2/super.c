// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/ext2/super.c
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
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/log2.h>
#include <linux/quotaops.h>
#include <linux/uaccess.h>
#include <linux/dax.h>
#include <linux/iversion.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

static void ext2_write_super(struct super_block *sb);
static int ext2_statfs (struct dentry * dentry, struct kstatfs * buf);
static int ext2_sync_fs(struct super_block *sb, int wait);
static int ext2_freeze(struct super_block *sb);
static int ext2_unfreeze(struct super_block *sb);

void ext2_error(struct super_block *sb, const char *function,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = sbi->s_es;

	if (!sb_rdonly(sb)) {
		spin_lock(&sbi->s_lock);
		sbi->s_mount_state |= EXT2_ERROR_FS;
		es->s_state |= cpu_to_le16(EXT2_ERROR_FS);
		spin_unlock(&sbi->s_lock);
		ext2_sync_super(sb, es, 1);
	}

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_CRIT "EXT2-fs (%s): error: %s: %pV\n",
	       sb->s_id, function, &vaf);

	va_end(args);

	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT2-fs: panic from previous error\n");
	if (!sb_rdonly(sb) && test_opt(sb, ERRORS_RO)) {
		ext2_msg(sb, KERN_CRIT,
			     "error: remounting filesystem read-only");
		sb->s_flags |= SB_RDONLY;
	}
}

static void ext2_msg_fc(struct fs_context *fc, const char *prefix,
			const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	const char *s_id;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		s_id = fc->root->d_sb->s_id;
	} else {
		/* get last path component of source */
		s_id = strrchr(fc->source, '/');
		if (s_id)
			s_id++;
		else
			s_id = fc->source;
	}
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sEXT2-fs (%s): %pV\n", prefix, s_id, &vaf);

	va_end(args);
}

void ext2_msg(struct super_block *sb, const char *prefix,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sEXT2-fs (%s): %pV\n", prefix, sb->s_id, &vaf);

	va_end(args);
}

/*
 * This must be called with sbi->s_lock held.
 */
void ext2_update_dynamic_rev(struct super_block *sb)
{
	struct ext2_super_block *es = EXT2_SB(sb)->s_es;

	if (le32_to_cpu(es->s_rev_level) > EXT2_GOOD_OLD_REV)
		return;

	ext2_msg(sb, KERN_WARNING,
		     "warning: updating to rev %d because of "
		     "new feature flag, running e2fsck is recommended",
		     EXT2_DYNAMIC_REV);

	es->s_first_ino = cpu_to_le32(EXT2_GOOD_OLD_FIRST_INO);
	es->s_inode_size = cpu_to_le16(EXT2_GOOD_OLD_INODE_SIZE);
	es->s_rev_level = cpu_to_le32(EXT2_DYNAMIC_REV);
	/* leave es->s_feature_*compat flags alone */
	/* es->s_uuid will be set by e2fsck if empty */

	/*
	 * The rest of the superblock fields should be zero, and if not it
	 * means they are likely already in use, so leave them alone.  We
	 * can leave it up to e2fsck to clean up any inconsistencies there.
	 */
}

#ifdef CONFIG_QUOTA
static int ext2_quota_off(struct super_block *sb, int type);

static void ext2_quota_off_umount(struct super_block *sb)
{
	int type;

	for (type = 0; type < MAXQUOTAS; type++)
		ext2_quota_off(sb, type);
}
#else
static inline void ext2_quota_off_umount(struct super_block *sb)
{
}
#endif

static void ext2_put_super (struct super_block * sb)
{
	int db_count;
	int i;
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	ext2_quota_off_umount(sb);

	ext2_xattr_destroy_cache(sbi->s_ea_block_cache);
	sbi->s_ea_block_cache = NULL;

	if (!sb_rdonly(sb)) {
		struct ext2_super_block *es = sbi->s_es;

		spin_lock(&sbi->s_lock);
		es->s_state = cpu_to_le16(sbi->s_mount_state);
		spin_unlock(&sbi->s_lock);
		ext2_sync_super(sb, es, 1);
	}
	db_count = sbi->s_gdb_count;
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
	kvfree(sbi->s_group_desc);
	kfree(sbi->s_debts);
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	brelse (sbi->s_sbh);
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	fs_put_dax(sbi->s_daxdev, NULL);
	kfree(sbi);
}

static struct kmem_cache * ext2_inode_cachep;

static struct inode *ext2_alloc_inode(struct super_block *sb)
{
	struct ext2_inode_info *ei;
	ei = alloc_inode_sb(sb, ext2_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	ei->i_block_alloc_info = NULL;
	inode_set_iversion(&ei->vfs_inode, 1);
#ifdef CONFIG_QUOTA
	memset(&ei->i_dquot, 0, sizeof(ei->i_dquot));
#endif

	return &ei->vfs_inode;
}

static void ext2_free_in_core_inode(struct inode *inode)
{
	kmem_cache_free(ext2_inode_cachep, EXT2_I(inode));
}

static void init_once(void *foo)
{
	struct ext2_inode_info *ei = (struct ext2_inode_info *) foo;

	rwlock_init(&ei->i_meta_lock);
#ifdef CONFIG_EXT2_FS_XATTR
	init_rwsem(&ei->xattr_sem);
#endif
	mutex_init(&ei->truncate_mutex);
	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	ext2_inode_cachep = kmem_cache_create_usercopy("ext2_inode_cache",
				sizeof(struct ext2_inode_info), 0,
				SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
				offsetof(struct ext2_inode_info, i_data),
				sizeof_field(struct ext2_inode_info, i_data),
				init_once);
	if (ext2_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ext2_inode_cachep);
}

static int ext2_show_options(struct seq_file *seq, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = sbi->s_es;
	unsigned long def_mount_opts;

	spin_lock(&sbi->s_lock);
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);

	if (sbi->s_sb_block != 1)
		seq_printf(seq, ",sb=%lu", sbi->s_sb_block);
	if (test_opt(sb, MINIX_DF))
		seq_puts(seq, ",minixdf");
	if (test_opt(sb, GRPID))
		seq_puts(seq, ",grpid");
	if (!test_opt(sb, GRPID) && (def_mount_opts & EXT2_DEFM_BSDGROUPS))
		seq_puts(seq, ",nogrpid");
	if (!uid_eq(sbi->s_resuid, make_kuid(&init_user_ns, EXT2_DEF_RESUID)) ||
	    le16_to_cpu(es->s_def_resuid) != EXT2_DEF_RESUID) {
		seq_printf(seq, ",resuid=%u",
				from_kuid_munged(&init_user_ns, sbi->s_resuid));
	}
	if (!gid_eq(sbi->s_resgid, make_kgid(&init_user_ns, EXT2_DEF_RESGID)) ||
	    le16_to_cpu(es->s_def_resgid) != EXT2_DEF_RESGID) {
		seq_printf(seq, ",resgid=%u",
				from_kgid_munged(&init_user_ns, sbi->s_resgid));
	}
	if (test_opt(sb, ERRORS_RO)) {
		int def_errors = le16_to_cpu(es->s_errors);

		if (def_errors == EXT2_ERRORS_PANIC ||
		    def_errors == EXT2_ERRORS_CONTINUE) {
			seq_puts(seq, ",errors=remount-ro");
		}
	}
	if (test_opt(sb, ERRORS_CONT))
		seq_puts(seq, ",errors=continue");
	if (test_opt(sb, ERRORS_PANIC))
		seq_puts(seq, ",errors=panic");
	if (test_opt(sb, NO_UID32))
		seq_puts(seq, ",nouid32");
	if (test_opt(sb, DEBUG))
		seq_puts(seq, ",debug");
	if (test_opt(sb, OLDALLOC))
		seq_puts(seq, ",oldalloc");

#ifdef CONFIG_EXT2_FS_XATTR
	if (test_opt(sb, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	if (!test_opt(sb, XATTR_USER) &&
	    (def_mount_opts & EXT2_DEFM_XATTR_USER)) {
		seq_puts(seq, ",nouser_xattr");
	}
#endif

#ifdef CONFIG_EXT2_FS_POSIX_ACL
	if (test_opt(sb, POSIX_ACL))
		seq_puts(seq, ",acl");
	if (!test_opt(sb, POSIX_ACL) && (def_mount_opts & EXT2_DEFM_ACL))
		seq_puts(seq, ",noacl");
#endif

	if (test_opt(sb, USRQUOTA))
		seq_puts(seq, ",usrquota");

	if (test_opt(sb, GRPQUOTA))
		seq_puts(seq, ",grpquota");

	if (test_opt(sb, XIP))
		seq_puts(seq, ",xip");

	if (test_opt(sb, DAX))
		seq_puts(seq, ",dax");

	if (!test_opt(sb, RESERVATION))
		seq_puts(seq, ",noreservation");

	spin_unlock(&sbi->s_lock);
	return 0;
}

#ifdef CONFIG_QUOTA
static ssize_t ext2_quota_read(struct super_block *sb, int type, char *data, size_t len, loff_t off);
static ssize_t ext2_quota_write(struct super_block *sb, int type, const char *data, size_t len, loff_t off);
static int ext2_quota_on(struct super_block *sb, int type, int format_id,
			 const struct path *path);
static struct dquot __rcu **ext2_get_dquots(struct inode *inode)
{
	return EXT2_I(inode)->i_dquot;
}

static const struct quotactl_ops ext2_quotactl_ops = {
	.quota_on	= ext2_quota_on,
	.quota_off	= ext2_quota_off,
	.quota_sync	= dquot_quota_sync,
	.get_state	= dquot_get_state,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk,
	.get_nextdqblk	= dquot_get_next_dqblk,
};
#endif

static const struct super_operations ext2_sops = {
	.alloc_inode	= ext2_alloc_inode,
	.free_inode	= ext2_free_in_core_inode,
	.write_inode	= ext2_write_inode,
	.evict_inode	= ext2_evict_inode,
	.put_super	= ext2_put_super,
	.sync_fs	= ext2_sync_fs,
	.freeze_fs	= ext2_freeze,
	.unfreeze_fs	= ext2_unfreeze,
	.statfs		= ext2_statfs,
	.show_options	= ext2_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ext2_quota_read,
	.quota_write	= ext2_quota_write,
	.get_dquots	= ext2_get_dquots,
#endif
};

static struct inode *ext2_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct inode *inode;

	if (ino < EXT2_FIRST_INO(sb) && ino != EXT2_ROOT_INO)
		return ERR_PTR(-ESTALE);
	if (ino > le32_to_cpu(EXT2_SB(sb)->s_es->s_inodes_count))
		return ERR_PTR(-ESTALE);

	/*
	 * ext2_iget isn't quite right if the inode is currently unallocated!
	 * However ext2_iget currently does appropriate checks to handle stale
	 * inodes so everything is OK.
	 */
	inode = ext2_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *ext2_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    ext2_nfs_get_inode);
}

static struct dentry *ext2_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    ext2_nfs_get_inode);
}

static const struct export_operations ext2_export_ops = {
	.encode_fh = generic_encode_ino32_fh,
	.fh_to_dentry = ext2_fh_to_dentry,
	.fh_to_parent = ext2_fh_to_parent,
	.get_parent = ext2_get_parent,
};

enum {
	Opt_bsd_df, Opt_minix_df, Opt_grpid, Opt_nogrpid, Opt_resgid, Opt_resuid,
	Opt_sb, Opt_errors, Opt_nouid32, Opt_debug, Opt_oldalloc, Opt_orlov,
	Opt_nobh, Opt_user_xattr, Opt_acl, Opt_xip, Opt_dax, Opt_ignore,
	Opt_quota, Opt_usrquota, Opt_grpquota, Opt_reservation,
};

static const struct constant_table ext2_param_errors[] = {
	{"continue",	EXT2_MOUNT_ERRORS_CONT},
	{"panic",	EXT2_MOUNT_ERRORS_PANIC},
	{"remount-ro",	EXT2_MOUNT_ERRORS_RO},
	{}
};

static const struct fs_parameter_spec ext2_param_spec[] = {
	fsparam_flag	("bsddf", Opt_bsd_df),
	fsparam_flag	("minixdf", Opt_minix_df),
	fsparam_flag	("grpid", Opt_grpid),
	fsparam_flag	("bsdgroups", Opt_grpid),
	fsparam_flag	("nogrpid", Opt_nogrpid),
	fsparam_flag	("sysvgroups", Opt_nogrpid),
	fsparam_gid	("resgid", Opt_resgid),
	fsparam_uid	("resuid", Opt_resuid),
	fsparam_u32	("sb", Opt_sb),
	fsparam_enum	("errors", Opt_errors, ext2_param_errors),
	fsparam_flag	("nouid32", Opt_nouid32),
	fsparam_flag	("debug", Opt_debug),
	fsparam_flag	("oldalloc", Opt_oldalloc),
	fsparam_flag	("orlov", Opt_orlov),
	fsparam_flag	("nobh", Opt_nobh),
	fsparam_flag_no	("user_xattr", Opt_user_xattr),
	fsparam_flag_no	("acl", Opt_acl),
	fsparam_flag	("xip", Opt_xip),
	fsparam_flag	("dax", Opt_dax),
	fsparam_flag	("grpquota", Opt_grpquota),
	fsparam_flag	("noquota", Opt_ignore),
	fsparam_flag	("quota", Opt_quota),
	fsparam_flag	("usrquota", Opt_usrquota),
	fsparam_flag_no	("reservation", Opt_reservation),
	{}
};

#define EXT2_SPEC_s_resuid                      (1 << 0)
#define EXT2_SPEC_s_resgid                      (1 << 1)

struct ext2_fs_context {
	unsigned long	vals_s_flags;	/* Bits to set in s_flags */
	unsigned long	mask_s_flags;	/* Bits changed in s_flags */
	unsigned int	vals_s_mount_opt;
	unsigned int	mask_s_mount_opt;
	kuid_t		s_resuid;
	kgid_t		s_resgid;
	unsigned long	s_sb_block;
	unsigned int	spec;

};

static inline void ctx_set_mount_opt(struct ext2_fs_context *ctx,
				  unsigned long flag)
{
	ctx->mask_s_mount_opt |= flag;
	ctx->vals_s_mount_opt |= flag;
}

static inline void ctx_clear_mount_opt(struct ext2_fs_context *ctx,
				    unsigned long flag)
{
	ctx->mask_s_mount_opt |= flag;
	ctx->vals_s_mount_opt &= ~flag;
}

static inline unsigned long
ctx_test_mount_opt(struct ext2_fs_context *ctx, unsigned long flag)
{
	return (ctx->vals_s_mount_opt & flag);
}

static inline bool
ctx_parsed_mount_opt(struct ext2_fs_context *ctx, unsigned long flag)
{
	return (ctx->mask_s_mount_opt & flag);
}

static void ext2_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static int ext2_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct ext2_fs_context *ctx = fc->fs_private;
	int opt;
	struct fs_parse_result result;

	opt = fs_parse(fc, ext2_param_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_bsd_df:
		ctx_clear_mount_opt(ctx, EXT2_MOUNT_MINIX_DF);
		break;
	case Opt_minix_df:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_MINIX_DF);
		break;
	case Opt_grpid:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_GRPID);
		break;
	case Opt_nogrpid:
		ctx_clear_mount_opt(ctx, EXT2_MOUNT_GRPID);
		break;
	case Opt_resuid:
		ctx->s_resuid = result.uid;
		ctx->spec |= EXT2_SPEC_s_resuid;
		break;
	case Opt_resgid:
		ctx->s_resgid = result.gid;
		ctx->spec |= EXT2_SPEC_s_resgid;
		break;
	case Opt_sb:
		/* Note that this is silently ignored on remount */
		ctx->s_sb_block = result.uint_32;
		break;
	case Opt_errors:
		ctx_clear_mount_opt(ctx, EXT2_MOUNT_ERRORS_MASK);
		ctx_set_mount_opt(ctx, result.uint_32);
		break;
	case Opt_nouid32:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_NO_UID32);
		break;
	case Opt_debug:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_DEBUG);
		break;
	case Opt_oldalloc:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_OLDALLOC);
		break;
	case Opt_orlov:
		ctx_clear_mount_opt(ctx, EXT2_MOUNT_OLDALLOC);
		break;
	case Opt_nobh:
		ext2_msg_fc(fc, KERN_INFO, "nobh option not supported\n");
		break;
#ifdef CONFIG_EXT2_FS_XATTR
	case Opt_user_xattr:
		if (!result.negated)
			ctx_set_mount_opt(ctx, EXT2_MOUNT_XATTR_USER);
		else
			ctx_clear_mount_opt(ctx, EXT2_MOUNT_XATTR_USER);
		break;
#else
	case Opt_user_xattr:
		ext2_msg_fc(fc, KERN_INFO, "(no)user_xattr options not supported");
		break;
#endif
#ifdef CONFIG_EXT2_FS_POSIX_ACL
	case Opt_acl:
		if (!result.negated)
			ctx_set_mount_opt(ctx, EXT2_MOUNT_POSIX_ACL);
		else
			ctx_clear_mount_opt(ctx, EXT2_MOUNT_POSIX_ACL);
		break;
#else
	case Opt_acl:
		ext2_msg_fc(fc, KERN_INFO, "(no)acl options not supported");
		break;
#endif
	case Opt_xip:
		ext2_msg_fc(fc, KERN_INFO, "use dax instead of xip");
		ctx_set_mount_opt(ctx, EXT2_MOUNT_XIP);
		fallthrough;
	case Opt_dax:
#ifdef CONFIG_FS_DAX
		ext2_msg_fc(fc, KERN_WARNING,
		    "DAX enabled. Warning: DAX support in ext2 driver is deprecated"
		    " and will be removed at the end of 2025. Please use ext4 driver instead.");
		ctx_set_mount_opt(ctx, EXT2_MOUNT_DAX);
#else
		ext2_msg_fc(fc, KERN_INFO, "dax option not supported");
#endif
		break;

#if defined(CONFIG_QUOTA)
	case Opt_quota:
	case Opt_usrquota:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_USRQUOTA);
		break;

	case Opt_grpquota:
		ctx_set_mount_opt(ctx, EXT2_MOUNT_GRPQUOTA);
		break;
#else
	case Opt_quota:
	case Opt_usrquota:
	case Opt_grpquota:
		ext2_msg_fc(fc, KERN_INFO, "quota operations not supported");
		break;
#endif
	case Opt_reservation:
		if (!result.negated) {
			ctx_set_mount_opt(ctx, EXT2_MOUNT_RESERVATION);
			ext2_msg_fc(fc, KERN_INFO, "reservations ON");
		} else {
			ctx_clear_mount_opt(ctx, EXT2_MOUNT_RESERVATION);
			ext2_msg_fc(fc, KERN_INFO, "reservations OFF");
		}
		break;
	case Opt_ignore:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ext2_setup_super (struct super_block * sb,
			      struct ext2_super_block * es,
			      int read_only)
{
	int res = 0;
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	if (le32_to_cpu(es->s_rev_level) > EXT2_MAX_SUPP_REV) {
		ext2_msg(sb, KERN_ERR,
			"error: revision level too high, "
			"forcing read-only mode");
		res = SB_RDONLY;
	}
	if (read_only)
		return res;
	if (!(sbi->s_mount_state & EXT2_VALID_FS))
		ext2_msg(sb, KERN_WARNING,
			"warning: mounting unchecked fs, "
			"running e2fsck is recommended");
	else if ((sbi->s_mount_state & EXT2_ERROR_FS))
		ext2_msg(sb, KERN_WARNING,
			"warning: mounting fs with errors, "
			"running e2fsck is recommended");
	else if ((__s16) le16_to_cpu(es->s_max_mnt_count) >= 0 &&
		 le16_to_cpu(es->s_mnt_count) >=
		 (unsigned short) (__s16) le16_to_cpu(es->s_max_mnt_count))
		ext2_msg(sb, KERN_WARNING,
			"warning: maximal mount count reached, "
			"running e2fsck is recommended");
	else if (le32_to_cpu(es->s_checkinterval) &&
		(le32_to_cpu(es->s_lastcheck) +
			le32_to_cpu(es->s_checkinterval) <=
			ktime_get_real_seconds()))
		ext2_msg(sb, KERN_WARNING,
			"warning: checktime reached, "
			"running e2fsck is recommended");
	if (!le16_to_cpu(es->s_max_mnt_count))
		es->s_max_mnt_count = cpu_to_le16(EXT2_DFL_MAX_MNT_COUNT);
	le16_add_cpu(&es->s_mnt_count, 1);
	if (test_opt (sb, DEBUG))
		ext2_msg(sb, KERN_INFO, "%s, %s, bs=%lu, gc=%lu, "
			"bpg=%lu, ipg=%lu, mo=%04lx]",
			EXT2FS_VERSION, EXT2FS_DATE, sb->s_blocksize,
			sbi->s_groups_count,
			EXT2_BLOCKS_PER_GROUP(sb),
			EXT2_INODES_PER_GROUP(sb),
			sbi->s_mount_opt);
	return res;
}

static int ext2_check_descriptors(struct super_block *sb)
{
	int i;
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	ext2_debug ("Checking group descriptors");

	for (i = 0; i < sbi->s_groups_count; i++) {
		struct ext2_group_desc *gdp = ext2_get_group_desc(sb, i, NULL);
		ext2_fsblk_t first_block = ext2_group_first_block_no(sb, i);
		ext2_fsblk_t last_block = ext2_group_last_block_no(sb, i);

		if (le32_to_cpu(gdp->bg_block_bitmap) < first_block ||
		    le32_to_cpu(gdp->bg_block_bitmap) > last_block)
		{
			ext2_error (sb, "ext2_check_descriptors",
				    "Block bitmap for group %d"
				    " not in group (block %lu)!",
				    i, (unsigned long) le32_to_cpu(gdp->bg_block_bitmap));
			return 0;
		}
		if (le32_to_cpu(gdp->bg_inode_bitmap) < first_block ||
		    le32_to_cpu(gdp->bg_inode_bitmap) > last_block)
		{
			ext2_error (sb, "ext2_check_descriptors",
				    "Inode bitmap for group %d"
				    " not in group (block %lu)!",
				    i, (unsigned long) le32_to_cpu(gdp->bg_inode_bitmap));
			return 0;
		}
		if (le32_to_cpu(gdp->bg_inode_table) < first_block ||
		    le32_to_cpu(gdp->bg_inode_table) + sbi->s_itb_per_group - 1 >
		    last_block)
		{
			ext2_error (sb, "ext2_check_descriptors",
				    "Inode table for group %d"
				    " not in group (block %lu)!",
				    i, (unsigned long) le32_to_cpu(gdp->bg_inode_table));
			return 0;
		}
	}
	return 1;
}

/*
 * Maximal file size.  There is a direct, and {,double-,triple-}indirect
 * block limit, and also a limit of (2^32 - 1) 512-byte sectors in i_blocks.
 * We need to be 1 filesystem block less than the 2^32 sector limit.
 */
static loff_t ext2_max_size(int bits)
{
	loff_t res = EXT2_NDIR_BLOCKS;
	int meta_blocks;
	unsigned int upper_limit;
	unsigned int ppb = 1 << (bits-2);

	/* This is calculated to be the largest file size for a
	 * dense, file such that the total number of
	 * sectors in the file, including data and all indirect blocks,
	 * does not exceed 2^32 -1
	 * __u32 i_blocks representing the total number of
	 * 512 bytes blocks of the file
	 */
	upper_limit = (1LL << 32) - 1;

	/* total blocks in file system block size */
	upper_limit >>= (bits - 9);

	/* Compute how many blocks we can address by block tree */
	res += 1LL << (bits-2);
	res += 1LL << (2*(bits-2));
	res += 1LL << (3*(bits-2));
	/* Compute how many metadata blocks are needed */
	meta_blocks = 1;
	meta_blocks += 1 + ppb;
	meta_blocks += 1 + ppb + ppb * ppb;
	/* Does block tree limit file size? */
	if (res + meta_blocks <= upper_limit)
		goto check_lfs;

	res = upper_limit;
	/* How many metadata blocks are needed for addressing upper_limit? */
	upper_limit -= EXT2_NDIR_BLOCKS;
	/* indirect blocks */
	meta_blocks = 1;
	upper_limit -= ppb;
	/* double indirect blocks */
	if (upper_limit < ppb * ppb) {
		meta_blocks += 1 + DIV_ROUND_UP(upper_limit, ppb);
		res -= meta_blocks;
		goto check_lfs;
	}
	meta_blocks += 1 + ppb;
	upper_limit -= ppb * ppb;
	/* tripple indirect blocks for the rest */
	meta_blocks += 1 + DIV_ROUND_UP(upper_limit, ppb) +
		DIV_ROUND_UP(upper_limit, ppb*ppb);
	res -= meta_blocks;
check_lfs:
	res <<= bits;
	if (res > MAX_LFS_FILESIZE)
		res = MAX_LFS_FILESIZE;

	return res;
}

static unsigned long descriptor_loc(struct super_block *sb,
				    unsigned long logic_sb_block,
				    int nr)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	unsigned long bg, first_meta_bg;
	
	first_meta_bg = le32_to_cpu(sbi->s_es->s_first_meta_bg);

	if (!EXT2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_META_BG) ||
	    nr < first_meta_bg)
		return (logic_sb_block + nr + 1);
	bg = sbi->s_desc_per_block * nr;

	return ext2_group_first_block_no(sb, bg) + ext2_bg_has_super(sb, bg);
}

/*
 * Set all mount options either from defaults on disk, or from parsed
 * options. Parsed/specified options override on-disk defaults.
 */
static void ext2_set_options(struct fs_context *fc, struct ext2_sb_info *sbi)
{
	struct ext2_fs_context *ctx = fc->fs_private;
	struct ext2_super_block *es = sbi->s_es;
	unsigned long def_mount_opts = le32_to_cpu(es->s_default_mount_opts);

	/* Copy parsed mount options to sbi */
	sbi->s_mount_opt = ctx->vals_s_mount_opt;

	/* Use in-superblock defaults only if not specified during parsing */
	if (!ctx_parsed_mount_opt(ctx, EXT2_MOUNT_DEBUG) &&
	    def_mount_opts & EXT2_DEFM_DEBUG)
		set_opt(sbi->s_mount_opt, DEBUG);

	if (!ctx_parsed_mount_opt(ctx, EXT2_MOUNT_GRPID) &&
	    def_mount_opts & EXT2_DEFM_BSDGROUPS)
		set_opt(sbi->s_mount_opt, GRPID);

	if (!ctx_parsed_mount_opt(ctx, EXT2_MOUNT_NO_UID32) &&
	    def_mount_opts & EXT2_DEFM_UID16)
		set_opt(sbi->s_mount_opt, NO_UID32);

#ifdef CONFIG_EXT2_FS_XATTR
	if (!ctx_parsed_mount_opt(ctx, EXT2_MOUNT_XATTR_USER) &&
	    def_mount_opts & EXT2_DEFM_XATTR_USER)
		set_opt(sbi->s_mount_opt, XATTR_USER);
#endif
#ifdef CONFIG_EXT2_FS_POSIX_ACL
	if (!ctx_parsed_mount_opt(ctx, EXT2_MOUNT_POSIX_ACL) &&
	    def_mount_opts & EXT2_DEFM_ACL)
		set_opt(sbi->s_mount_opt, POSIX_ACL);
#endif

	if (!ctx_parsed_mount_opt(ctx, EXT2_MOUNT_ERRORS_MASK)) {
		if (le16_to_cpu(sbi->s_es->s_errors) == EXT2_ERRORS_PANIC)
			set_opt(sbi->s_mount_opt, ERRORS_PANIC);
		else if (le16_to_cpu(sbi->s_es->s_errors) == EXT2_ERRORS_CONTINUE)
			set_opt(sbi->s_mount_opt, ERRORS_CONT);
		else
			set_opt(sbi->s_mount_opt, ERRORS_RO);
	}

	if (ctx->spec & EXT2_SPEC_s_resuid)
		sbi->s_resuid = ctx->s_resuid;
	else
		sbi->s_resuid = make_kuid(&init_user_ns,
					   le16_to_cpu(es->s_def_resuid));

	if (ctx->spec & EXT2_SPEC_s_resgid)
		sbi->s_resgid = ctx->s_resgid;
	else
		sbi->s_resgid = make_kgid(&init_user_ns,
					   le16_to_cpu(es->s_def_resgid));
}

static int ext2_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct ext2_fs_context *ctx = fc->fs_private;
	int silent = fc->sb_flags & SB_SILENT;
	struct buffer_head * bh;
	struct ext2_sb_info * sbi;
	struct ext2_super_block * es;
	struct inode *root;
	unsigned long block;
	unsigned long sb_block = ctx->s_sb_block;
	unsigned long logic_sb_block;
	unsigned long offset = 0;
	long ret = -ENOMEM;
	int blocksize = BLOCK_SIZE;
	int db_count;
	int i, j;
	__le32 features;
	int err;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sbi->s_blockgroup_lock =
		kzalloc(sizeof(struct blockgroup_lock), GFP_KERNEL);
	if (!sbi->s_blockgroup_lock) {
		kfree(sbi);
		return -ENOMEM;
	}
	sb->s_fs_info = sbi;
	sbi->s_sb_block = sb_block;
	sbi->s_daxdev = fs_dax_get_by_bdev(sb->s_bdev, &sbi->s_dax_part_off,
					   NULL, NULL);

	spin_lock_init(&sbi->s_lock);
	ret = -EINVAL;

	/*
	 * See what the current blocksize for the device is, and
	 * use that as the blocksize.  Otherwise (or if the blocksize
	 * is smaller than the default) use the default.
	 * This is important for devices that have a hardware
	 * sectorsize that is larger than the default.
	 */
	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
	if (!blocksize) {
		ext2_msg(sb, KERN_ERR, "error: unable to set blocksize");
		goto failed_sbi;
	}

	/*
	 * If the superblock doesn't start on a hardware sector boundary,
	 * calculate the offset.  
	 */
	if (blocksize != BLOCK_SIZE) {
		logic_sb_block = (sb_block*BLOCK_SIZE) / blocksize;
		offset = (sb_block*BLOCK_SIZE) % blocksize;
	} else {
		logic_sb_block = sb_block;
	}

	if (!(bh = sb_bread(sb, logic_sb_block))) {
		ext2_msg(sb, KERN_ERR, "error: unable to read superblock");
		goto failed_sbi;
	}
	/*
	 * Note: s_es must be initialized as soon as possible because
	 *       some ext2 macro-instructions depend on its value
	 */
	es = (struct ext2_super_block *) (((char *)bh->b_data) + offset);
	sbi->s_es = es;
	sb->s_magic = le16_to_cpu(es->s_magic);

	if (sb->s_magic != EXT2_SUPER_MAGIC)
		goto cantfind_ext2;

	ext2_set_options(fc, sbi);

	sb->s_flags = (sb->s_flags & ~SB_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? SB_POSIXACL : 0);
	sb->s_iflags |= SB_I_CGROUPWB;

	if (le32_to_cpu(es->s_rev_level) == EXT2_GOOD_OLD_REV &&
	    (EXT2_HAS_COMPAT_FEATURE(sb, ~0U) ||
	     EXT2_HAS_RO_COMPAT_FEATURE(sb, ~0U) ||
	     EXT2_HAS_INCOMPAT_FEATURE(sb, ~0U)))
		ext2_msg(sb, KERN_WARNING,
			"warning: feature flags set on rev 0 fs, "
			"running e2fsck is recommended");
	/*
	 * Check feature flags regardless of the revision level, since we
	 * previously didn't change the revision level when setting the flags,
	 * so there is a chance incompat flags are set on a rev 0 filesystem.
	 */
	features = EXT2_HAS_INCOMPAT_FEATURE(sb, ~EXT2_FEATURE_INCOMPAT_SUPP);
	if (features) {
		ext2_msg(sb, KERN_ERR,	"error: couldn't mount because of "
		       "unsupported optional features (%x)",
			le32_to_cpu(features));
		goto failed_mount;
	}
	if (!sb_rdonly(sb) && (features = EXT2_HAS_RO_COMPAT_FEATURE(sb, ~EXT2_FEATURE_RO_COMPAT_SUPP))){
		ext2_msg(sb, KERN_ERR, "error: couldn't mount RDWR because of "
		       "unsupported optional features (%x)",
		       le32_to_cpu(features));
		goto failed_mount;
	}

	if (le32_to_cpu(es->s_log_block_size) >
	    (EXT2_MAX_BLOCK_LOG_SIZE - BLOCK_SIZE_BITS)) {
		ext2_msg(sb, KERN_ERR,
			 "Invalid log block size: %u",
			 le32_to_cpu(es->s_log_block_size));
		goto failed_mount;
	}
	blocksize = BLOCK_SIZE << le32_to_cpu(sbi->s_es->s_log_block_size);

	if (test_opt(sb, DAX)) {
		if (!sbi->s_daxdev) {
			ext2_msg(sb, KERN_ERR,
				"DAX unsupported by block device. Turning off DAX.");
			clear_opt(sbi->s_mount_opt, DAX);
		} else if (blocksize != PAGE_SIZE) {
			ext2_msg(sb, KERN_ERR, "unsupported blocksize for DAX\n");
			clear_opt(sbi->s_mount_opt, DAX);
		}
	}

	/* If the blocksize doesn't match, re-read the thing.. */
	if (sb->s_blocksize != blocksize) {
		brelse(bh);

		if (!sb_set_blocksize(sb, blocksize)) {
			ext2_msg(sb, KERN_ERR,
				"error: bad blocksize %d", blocksize);
			goto failed_sbi;
		}

		logic_sb_block = (sb_block*BLOCK_SIZE) / blocksize;
		offset = (sb_block*BLOCK_SIZE) % blocksize;
		bh = sb_bread(sb, logic_sb_block);
		if(!bh) {
			ext2_msg(sb, KERN_ERR, "error: couldn't read"
				"superblock on 2nd try");
			goto failed_sbi;
		}
		es = (struct ext2_super_block *) (((char *)bh->b_data) + offset);
		sbi->s_es = es;
		if (es->s_magic != cpu_to_le16(EXT2_SUPER_MAGIC)) {
			ext2_msg(sb, KERN_ERR, "error: magic mismatch");
			goto failed_mount;
		}
	}

	sb->s_maxbytes = ext2_max_size(sb->s_blocksize_bits);
	sb->s_max_links = EXT2_LINK_MAX;
	sb->s_time_min = S32_MIN;
	sb->s_time_max = S32_MAX;

	if (le32_to_cpu(es->s_rev_level) == EXT2_GOOD_OLD_REV) {
		sbi->s_inode_size = EXT2_GOOD_OLD_INODE_SIZE;
		sbi->s_first_ino = EXT2_GOOD_OLD_FIRST_INO;
	} else {
		sbi->s_inode_size = le16_to_cpu(es->s_inode_size);
		sbi->s_first_ino = le32_to_cpu(es->s_first_ino);
		if ((sbi->s_inode_size < EXT2_GOOD_OLD_INODE_SIZE) ||
		    !is_power_of_2(sbi->s_inode_size) ||
		    (sbi->s_inode_size > blocksize)) {
			ext2_msg(sb, KERN_ERR,
				"error: unsupported inode size: %d",
				sbi->s_inode_size);
			goto failed_mount;
		}
	}

	sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
	sbi->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);

	sbi->s_inodes_per_block = sb->s_blocksize / EXT2_INODE_SIZE(sb);
	if (sbi->s_inodes_per_block == 0 || sbi->s_inodes_per_group == 0)
		goto cantfind_ext2;
	sbi->s_itb_per_group = sbi->s_inodes_per_group /
					sbi->s_inodes_per_block;
	sbi->s_desc_per_block = sb->s_blocksize /
					sizeof (struct ext2_group_desc);
	sbi->s_sbh = bh;
	sbi->s_mount_state = le16_to_cpu(es->s_state);
	sbi->s_addr_per_block_bits =
		ilog2 (EXT2_ADDR_PER_BLOCK(sb));
	sbi->s_desc_per_block_bits =
		ilog2 (EXT2_DESC_PER_BLOCK(sb));

	if (sb->s_magic != EXT2_SUPER_MAGIC)
		goto cantfind_ext2;

	if (sb->s_blocksize != bh->b_size) {
		if (!silent)
			ext2_msg(sb, KERN_ERR, "error: unsupported blocksize");
		goto failed_mount;
	}

	if (es->s_log_frag_size != es->s_log_block_size) {
		ext2_msg(sb, KERN_ERR,
			"error: fragsize log %u != blocksize log %u",
			le32_to_cpu(es->s_log_frag_size), sb->s_blocksize_bits);
		goto failed_mount;
	}

	if (sbi->s_blocks_per_group > sb->s_blocksize * 8) {
		ext2_msg(sb, KERN_ERR,
			"error: #blocks per group too big: %lu",
			sbi->s_blocks_per_group);
		goto failed_mount;
	}
	/* At least inode table, bitmaps, and sb have to fit in one group */
	if (sbi->s_blocks_per_group <= sbi->s_itb_per_group + 3) {
		ext2_msg(sb, KERN_ERR,
			"error: #blocks per group smaller than metadata size: %lu <= %lu",
			sbi->s_blocks_per_group, sbi->s_inodes_per_group + 3);
		goto failed_mount;
	}
	if (sbi->s_inodes_per_group < sbi->s_inodes_per_block ||
	    sbi->s_inodes_per_group > sb->s_blocksize * 8) {
		ext2_msg(sb, KERN_ERR,
			"error: invalid #inodes per group: %lu",
			sbi->s_inodes_per_group);
		goto failed_mount;
	}
	if (sb_bdev_nr_blocks(sb) < le32_to_cpu(es->s_blocks_count)) {
		ext2_msg(sb, KERN_ERR,
			 "bad geometry: block count %u exceeds size of device (%u blocks)",
			 le32_to_cpu(es->s_blocks_count),
			 (unsigned)sb_bdev_nr_blocks(sb));
		goto failed_mount;
	}

	sbi->s_groups_count = ((le32_to_cpu(es->s_blocks_count) -
				le32_to_cpu(es->s_first_data_block) - 1)
					/ EXT2_BLOCKS_PER_GROUP(sb)) + 1;
	if ((u64)sbi->s_groups_count * sbi->s_inodes_per_group !=
	    le32_to_cpu(es->s_inodes_count)) {
		ext2_msg(sb, KERN_ERR, "error: invalid #inodes: %u vs computed %llu",
			 le32_to_cpu(es->s_inodes_count),
			 (u64)sbi->s_groups_count * sbi->s_inodes_per_group);
		goto failed_mount;
	}
	db_count = (sbi->s_groups_count + EXT2_DESC_PER_BLOCK(sb) - 1) /
		   EXT2_DESC_PER_BLOCK(sb);
	sbi->s_group_desc = kvmalloc_array(db_count,
					   sizeof(struct buffer_head *),
					   GFP_KERNEL);
	if (sbi->s_group_desc == NULL) {
		ret = -ENOMEM;
		ext2_msg(sb, KERN_ERR, "error: not enough memory");
		goto failed_mount;
	}
	bgl_lock_init(sbi->s_blockgroup_lock);
	sbi->s_debts = kcalloc(sbi->s_groups_count, sizeof(*sbi->s_debts), GFP_KERNEL);
	if (!sbi->s_debts) {
		ret = -ENOMEM;
		ext2_msg(sb, KERN_ERR, "error: not enough memory");
		goto failed_mount_group_desc;
	}
	for (i = 0; i < db_count; i++) {
		block = descriptor_loc(sb, logic_sb_block, i);
		sbi->s_group_desc[i] = sb_bread(sb, block);
		if (!sbi->s_group_desc[i]) {
			for (j = 0; j < i; j++)
				brelse (sbi->s_group_desc[j]);
			ext2_msg(sb, KERN_ERR,
				"error: unable to read group descriptors");
			goto failed_mount_group_desc;
		}
	}
	if (!ext2_check_descriptors (sb)) {
		ext2_msg(sb, KERN_ERR, "group descriptors corrupted");
		goto failed_mount2;
	}
	sbi->s_gdb_count = db_count;
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);

	/* per filesystem reservation list head & lock */
	spin_lock_init(&sbi->s_rsv_window_lock);
	sbi->s_rsv_window_root = RB_ROOT;
	/*
	 * Add a single, static dummy reservation to the start of the
	 * reservation window list --- it gives us a placeholder for
	 * append-at-start-of-list which makes the allocation logic
	 * _much_ simpler.
	 */
	sbi->s_rsv_window_head.rsv_start = EXT2_RESERVE_WINDOW_NOT_ALLOCATED;
	sbi->s_rsv_window_head.rsv_end = EXT2_RESERVE_WINDOW_NOT_ALLOCATED;
	sbi->s_rsv_window_head.rsv_alloc_hit = 0;
	sbi->s_rsv_window_head.rsv_goal_size = 0;
	ext2_rsv_window_add(sb, &sbi->s_rsv_window_head);

	err = percpu_counter_init(&sbi->s_freeblocks_counter,
				ext2_count_free_blocks(sb), GFP_KERNEL);
	if (!err) {
		err = percpu_counter_init(&sbi->s_freeinodes_counter,
				ext2_count_free_inodes(sb), GFP_KERNEL);
	}
	if (!err) {
		err = percpu_counter_init(&sbi->s_dirs_counter,
				ext2_count_dirs(sb), GFP_KERNEL);
	}
	if (err) {
		ret = err;
		ext2_msg(sb, KERN_ERR, "error: insufficient memory");
		goto failed_mount3;
	}

#ifdef CONFIG_EXT2_FS_XATTR
	sbi->s_ea_block_cache = ext2_xattr_create_cache();
	if (!sbi->s_ea_block_cache) {
		ret = -ENOMEM;
		ext2_msg(sb, KERN_ERR, "Failed to create ea_block_cache");
		goto failed_mount3;
	}
#endif
	/*
	 * set up enough so that it can read an inode
	 */
	sb->s_op = &ext2_sops;
	sb->s_export_op = &ext2_export_ops;
	sb->s_xattr = ext2_xattr_handlers;

#ifdef CONFIG_QUOTA
	sb->dq_op = &dquot_operations;
	sb->s_qcop = &ext2_quotactl_ops;
	sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP;
#endif

	root = ext2_iget(sb, EXT2_ROOT_INO);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto failed_mount3;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		iput(root);
		ext2_msg(sb, KERN_ERR, "error: corrupt root inode, run e2fsck");
		goto failed_mount3;
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		ext2_msg(sb, KERN_ERR, "error: get root inode failed");
		ret = -ENOMEM;
		goto failed_mount3;
	}
	if (EXT2_HAS_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_HAS_JOURNAL))
		ext2_msg(sb, KERN_WARNING,
			"warning: mounting ext3 filesystem as ext2");
	if (ext2_setup_super (sb, es, sb_rdonly(sb)))
		sb->s_flags |= SB_RDONLY;
	ext2_write_super(sb);
	return 0;

cantfind_ext2:
	if (!silent)
		ext2_msg(sb, KERN_ERR,
			"error: can't find an ext2 filesystem on dev %s.",
			sb->s_id);
	goto failed_mount;
failed_mount3:
	ext2_xattr_destroy_cache(sbi->s_ea_block_cache);
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
failed_mount2:
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
failed_mount_group_desc:
	kvfree(sbi->s_group_desc);
	kfree(sbi->s_debts);
failed_mount:
	brelse(bh);
failed_sbi:
	fs_put_dax(sbi->s_daxdev, NULL);
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
	return ret;
}

static void ext2_clear_super_error(struct super_block *sb)
{
	struct buffer_head *sbh = EXT2_SB(sb)->s_sbh;

	if (buffer_write_io_error(sbh)) {
		/*
		 * Oh, dear.  A previous attempt to write the
		 * superblock failed.  This could happen because the
		 * USB device was yanked out.  Or it could happen to
		 * be a transient write error and maybe the block will
		 * be remapped.  Nothing we can do but to retry the
		 * write and hope for the best.
		 */
		ext2_msg(sb, KERN_ERR,
		       "previous I/O error to superblock detected");
		clear_buffer_write_io_error(sbh);
		set_buffer_uptodate(sbh);
	}
}

void ext2_sync_super(struct super_block *sb, struct ext2_super_block *es,
		     int wait)
{
	ext2_clear_super_error(sb);
	spin_lock(&EXT2_SB(sb)->s_lock);
	es->s_free_blocks_count = cpu_to_le32(ext2_count_free_blocks(sb));
	es->s_free_inodes_count = cpu_to_le32(ext2_count_free_inodes(sb));
	es->s_wtime = cpu_to_le32(ktime_get_real_seconds());
	/* unlock before we do IO */
	spin_unlock(&EXT2_SB(sb)->s_lock);
	mark_buffer_dirty(EXT2_SB(sb)->s_sbh);
	if (wait)
		sync_dirty_buffer(EXT2_SB(sb)->s_sbh);
}

/*
 * In the second extended file system, it is not necessary to
 * write the super block since we use a mapping of the
 * disk super block in a buffer.
 *
 * However, this function is still used to set the fs valid
 * flags to 0.  We need to set this flag to 0 since the fs
 * may have been checked while mounted and e2fsck may have
 * set s_state to EXT2_VALID_FS after some corrections.
 */
static int ext2_sync_fs(struct super_block *sb, int wait)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = EXT2_SB(sb)->s_es;

	/*
	 * Write quota structures to quota file, sync_blockdev() will write
	 * them to disk later
	 */
	dquot_writeback_dquots(sb, -1);

	spin_lock(&sbi->s_lock);
	if (es->s_state & cpu_to_le16(EXT2_VALID_FS)) {
		ext2_debug("setting valid to 0\n");
		es->s_state &= cpu_to_le16(~EXT2_VALID_FS);
	}
	spin_unlock(&sbi->s_lock);
	ext2_sync_super(sb, es, wait);
	return 0;
}

static int ext2_freeze(struct super_block *sb)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	/*
	 * Open but unlinked files present? Keep EXT2_VALID_FS flag cleared
	 * because we have unattached inodes and thus filesystem is not fully
	 * consistent.
	 */
	if (atomic_long_read(&sb->s_remove_count)) {
		ext2_sync_fs(sb, 1);
		return 0;
	}
	/* Set EXT2_FS_VALID flag */
	spin_lock(&sbi->s_lock);
	sbi->s_es->s_state = cpu_to_le16(sbi->s_mount_state);
	spin_unlock(&sbi->s_lock);
	ext2_sync_super(sb, sbi->s_es, 1);

	return 0;
}

static int ext2_unfreeze(struct super_block *sb)
{
	/* Just write sb to clear EXT2_VALID_FS flag */
	ext2_write_super(sb);

	return 0;
}

static void ext2_write_super(struct super_block *sb)
{
	if (!sb_rdonly(sb))
		ext2_sync_fs(sb, 1);
}

static int ext2_reconfigure(struct fs_context *fc)
{
	struct ext2_fs_context *ctx = fc->fs_private;
	struct super_block *sb = fc->root->d_sb;
	struct ext2_sb_info * sbi = EXT2_SB(sb);
	struct ext2_super_block * es;
	struct ext2_mount_options new_opts;
	int flags = fc->sb_flags;
	int err;

	sync_filesystem(sb);

	new_opts.s_mount_opt = ctx->vals_s_mount_opt;
	new_opts.s_resuid = ctx->s_resuid;
	new_opts.s_resgid = ctx->s_resgid;

	spin_lock(&sbi->s_lock);
	es = sbi->s_es;
	if ((sbi->s_mount_opt ^ new_opts.s_mount_opt) & EXT2_MOUNT_DAX) {
		ext2_msg(sb, KERN_WARNING, "warning: refusing change of "
			 "dax flag with busy inodes while remounting");
		new_opts.s_mount_opt ^= EXT2_MOUNT_DAX;
	}
	if ((bool)(flags & SB_RDONLY) == sb_rdonly(sb))
		goto out_set;
	if (flags & SB_RDONLY) {
		if (le16_to_cpu(es->s_state) & EXT2_VALID_FS ||
		    !(sbi->s_mount_state & EXT2_VALID_FS))
			goto out_set;

		/*
		 * OK, we are remounting a valid rw partition rdonly, so set
		 * the rdonly flag and then mark the partition as valid again.
		 */
		es->s_state = cpu_to_le16(sbi->s_mount_state);
		es->s_mtime = cpu_to_le32(ktime_get_real_seconds());
		spin_unlock(&sbi->s_lock);

		err = dquot_suspend(sb, -1);
		if (err < 0)
			return err;

		ext2_sync_super(sb, es, 1);
	} else {
		__le32 ret = EXT2_HAS_RO_COMPAT_FEATURE(sb,
					       ~EXT2_FEATURE_RO_COMPAT_SUPP);
		if (ret) {
			spin_unlock(&sbi->s_lock);
			ext2_msg(sb, KERN_WARNING,
				"warning: couldn't remount RDWR because of "
				"unsupported optional features (%x).",
				le32_to_cpu(ret));
			return -EROFS;
		}
		/*
		 * Mounting a RDONLY partition read-write, so reread and
		 * store the current valid flag.  (It may have been changed
		 * by e2fsck since we originally mounted the partition.)
		 */
		sbi->s_mount_state = le16_to_cpu(es->s_state);
		if (!ext2_setup_super (sb, es, 0))
			sb->s_flags &= ~SB_RDONLY;
		spin_unlock(&sbi->s_lock);

		ext2_write_super(sb);

		dquot_resume(sb, -1);
	}

	spin_lock(&sbi->s_lock);
out_set:
	sbi->s_mount_opt = new_opts.s_mount_opt;
	sbi->s_resuid = new_opts.s_resuid;
	sbi->s_resgid = new_opts.s_resgid;
	sb->s_flags = (sb->s_flags & ~SB_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? SB_POSIXACL : 0);
	spin_unlock(&sbi->s_lock);

	return 0;
}

static int ext2_statfs (struct dentry * dentry, struct kstatfs * buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = sbi->s_es;

	spin_lock(&sbi->s_lock);

	if (test_opt (sb, MINIX_DF))
		sbi->s_overhead_last = 0;
	else if (sbi->s_blocks_last != le32_to_cpu(es->s_blocks_count)) {
		unsigned long i, overhead = 0;
		smp_rmb();

		/*
		 * Compute the overhead (FS structures). This is constant
		 * for a given filesystem unless the number of block groups
		 * changes so we cache the previous value until it does.
		 */

		/*
		 * All of the blocks before first_data_block are
		 * overhead
		 */
		overhead = le32_to_cpu(es->s_first_data_block);

		/*
		 * Add the overhead attributed to the superblock and
		 * block group descriptors.  If the sparse superblocks
		 * feature is turned on, then not all groups have this.
		 */
		for (i = 0; i < sbi->s_groups_count; i++)
			overhead += ext2_bg_has_super(sb, i) +
				ext2_bg_num_gdb(sb, i);

		/*
		 * Every block group has an inode bitmap, a block
		 * bitmap, and an inode table.
		 */
		overhead += (sbi->s_groups_count *
			     (2 + sbi->s_itb_per_group));
		sbi->s_overhead_last = overhead;
		smp_wmb();
		sbi->s_blocks_last = le32_to_cpu(es->s_blocks_count);
	}

	buf->f_type = EXT2_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = le32_to_cpu(es->s_blocks_count) - sbi->s_overhead_last;
	buf->f_bfree = ext2_count_free_blocks(sb);
	es->s_free_blocks_count = cpu_to_le32(buf->f_bfree);
	buf->f_bavail = buf->f_bfree - le32_to_cpu(es->s_r_blocks_count);
	if (buf->f_bfree < le32_to_cpu(es->s_r_blocks_count))
		buf->f_bavail = 0;
	buf->f_files = le32_to_cpu(es->s_inodes_count);
	buf->f_ffree = ext2_count_free_inodes(sb);
	es->s_free_inodes_count = cpu_to_le32(buf->f_ffree);
	buf->f_namelen = EXT2_NAME_LEN;
	buf->f_fsid = uuid_to_fsid(es->s_uuid);
	spin_unlock(&sbi->s_lock);
	return 0;
}

static int ext2_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ext2_fill_super);
}

#ifdef CONFIG_QUOTA

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and no one else should touch the files)
 * we don't have to be afraid of races */
static ssize_t ext2_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT2_BLOCK_SIZE_BITS(sb);
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
		tocopy = min_t(size_t, sb->s_blocksize - offset, toread);

		tmp_bh.b_state = 0;
		tmp_bh.b_size = sb->s_blocksize;
		err = ext2_get_block(inode, blk, &tmp_bh, 0);
		if (err < 0)
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
static ssize_t ext2_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT2_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t towrite = len;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;

	while (towrite > 0) {
		tocopy = min_t(size_t, sb->s_blocksize - offset, towrite);

		tmp_bh.b_state = 0;
		tmp_bh.b_size = sb->s_blocksize;
		err = ext2_get_block(inode, blk, &tmp_bh, 1);
		if (err < 0)
			goto out;
		if (offset || tocopy != EXT2_BLOCK_SIZE(sb))
			bh = sb_bread(sb, tmp_bh.b_blocknr);
		else
			bh = sb_getblk(sb, tmp_bh.b_blocknr);
		if (unlikely(!bh)) {
			err = -EIO;
			goto out;
		}
		lock_buffer(bh);
		memcpy(bh->b_data+offset, data, tocopy);
		flush_dcache_folio(bh->b_folio);
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
	if (len == towrite)
		return err;
	if (inode->i_size < off+len-towrite)
		i_size_write(inode, off+len-towrite);
	inode_inc_iversion(inode);
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	mark_inode_dirty(inode);
	return len - towrite;
}

static int ext2_quota_on(struct super_block *sb, int type, int format_id,
			 const struct path *path)
{
	int err;
	struct inode *inode;

	err = dquot_quota_on(sb, type, format_id, path);
	if (err)
		return err;

	inode = d_inode(path->dentry);
	inode_lock(inode);
	EXT2_I(inode)->i_flags |= EXT2_NOATIME_FL | EXT2_IMMUTABLE_FL;
	inode_set_flags(inode, S_NOATIME | S_IMMUTABLE,
			S_NOATIME | S_IMMUTABLE);
	inode_unlock(inode);
	mark_inode_dirty(inode);

	return 0;
}

static int ext2_quota_off(struct super_block *sb, int type)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	int err;

	if (!inode || !igrab(inode))
		goto out;

	err = dquot_quota_off(sb, type);
	if (err)
		goto out_put;

	inode_lock(inode);
	EXT2_I(inode)->i_flags &= ~(EXT2_NOATIME_FL | EXT2_IMMUTABLE_FL);
	inode_set_flags(inode, 0, S_NOATIME | S_IMMUTABLE);
	inode_unlock(inode);
	mark_inode_dirty(inode);
out_put:
	iput(inode);
	return err;
out:
	return dquot_quota_off(sb, type);
}

#endif

static const struct fs_context_operations ext2_context_ops = {
	.parse_param	= ext2_parse_param,
	.get_tree	= ext2_get_tree,
	.reconfigure	= ext2_reconfigure,
	.free		= ext2_free_fc,
};

static int ext2_init_fs_context(struct fs_context *fc)
{
	struct ext2_fs_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		struct super_block *sb = fc->root->d_sb;
		struct ext2_sb_info *sbi = EXT2_SB(sb);

		spin_lock(&sbi->s_lock);
		ctx->vals_s_mount_opt = sbi->s_mount_opt;
		ctx->vals_s_flags = sb->s_flags;
		ctx->s_resuid = sbi->s_resuid;
		ctx->s_resgid = sbi->s_resgid;
		spin_unlock(&sbi->s_lock);
	} else {
		ctx->s_sb_block = 1;
		ctx_set_mount_opt(ctx, EXT2_MOUNT_RESERVATION);
	}

	fc->fs_private = ctx;
	fc->ops = &ext2_context_ops;

	return 0;
}

static struct file_system_type ext2_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext2",
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
	.init_fs_context = ext2_init_fs_context,
	.parameters	= ext2_param_spec,
};
MODULE_ALIAS_FS("ext2");

static int __init init_ext2_fs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		return err;
	err = register_filesystem(&ext2_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
	return err;
}

static void __exit exit_ext2_fs(void)
{
	unregister_filesystem(&ext2_fs_type);
	destroy_inodecache();
}

MODULE_AUTHOR("Remy Card and others");
MODULE_DESCRIPTION("Second Extended Filesystem");
MODULE_LICENSE("GPL");
module_init(init_ext2_fs)
module_exit(exit_ext2_fs)
