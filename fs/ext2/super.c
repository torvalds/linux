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
 *  linux/fs/minix/iyesde.c
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
#include <linux/parser.h>
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
static int ext2_remount (struct super_block * sb, int * flags, char * data);
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

	es->s_first_iyes = cpu_to_le32(EXT2_GOOD_OLD_FIRST_INO);
	es->s_iyesde_size = cpu_to_le16(EXT2_GOOD_OLD_INODE_SIZE);
	es->s_rev_level = cpu_to_le32(EXT2_DYNAMIC_REV);
	/* leave es->s_feature_*compat flags alone */
	/* es->s_uuid will be set by e2fsck if empty */

	/*
	 * The rest of the superblock fields should be zero, and if yest it
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
	kfree(sbi->s_group_desc);
	kfree(sbi->s_debts);
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeiyesdes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	brelse (sbi->s_sbh);
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	fs_put_dax(sbi->s_daxdev);
	kfree(sbi);
}

static struct kmem_cache * ext2_iyesde_cachep;

static struct iyesde *ext2_alloc_iyesde(struct super_block *sb)
{
	struct ext2_iyesde_info *ei;
	ei = kmem_cache_alloc(ext2_iyesde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	ei->i_block_alloc_info = NULL;
	iyesde_set_iversion(&ei->vfs_iyesde, 1);
#ifdef CONFIG_QUOTA
	memset(&ei->i_dquot, 0, sizeof(ei->i_dquot));
#endif

	return &ei->vfs_iyesde;
}

static void ext2_free_in_core_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(ext2_iyesde_cachep, EXT2_I(iyesde));
}

static void init_once(void *foo)
{
	struct ext2_iyesde_info *ei = (struct ext2_iyesde_info *) foo;

	rwlock_init(&ei->i_meta_lock);
#ifdef CONFIG_EXT2_FS_XATTR
	init_rwsem(&ei->xattr_sem);
#endif
	mutex_init(&ei->truncate_mutex);
#ifdef CONFIG_FS_DAX
	init_rwsem(&ei->dax_sem);
#endif
	iyesde_init_once(&ei->vfs_iyesde);
}

static int __init init_iyesdecache(void)
{
	ext2_iyesde_cachep = kmem_cache_create_usercopy("ext2_iyesde_cache",
				sizeof(struct ext2_iyesde_info), 0,
				(SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|
					SLAB_ACCOUNT),
				offsetof(struct ext2_iyesde_info, i_data),
				sizeof_field(struct ext2_iyesde_info, i_data),
				init_once);
	if (ext2_iyesde_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_iyesdecache(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ext2_iyesde_cachep);
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
		seq_puts(seq, ",yesgrpid");
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
		seq_puts(seq, ",yesuid32");
	if (test_opt(sb, DEBUG))
		seq_puts(seq, ",debug");
	if (test_opt(sb, OLDALLOC))
		seq_puts(seq, ",oldalloc");

#ifdef CONFIG_EXT2_FS_XATTR
	if (test_opt(sb, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	if (!test_opt(sb, XATTR_USER) &&
	    (def_mount_opts & EXT2_DEFM_XATTR_USER)) {
		seq_puts(seq, ",yesuser_xattr");
	}
#endif

#ifdef CONFIG_EXT2_FS_POSIX_ACL
	if (test_opt(sb, POSIX_ACL))
		seq_puts(seq, ",acl");
	if (!test_opt(sb, POSIX_ACL) && (def_mount_opts & EXT2_DEFM_ACL))
		seq_puts(seq, ",yesacl");
#endif

	if (test_opt(sb, NOBH))
		seq_puts(seq, ",yesbh");

	if (test_opt(sb, USRQUOTA))
		seq_puts(seq, ",usrquota");

	if (test_opt(sb, GRPQUOTA))
		seq_puts(seq, ",grpquota");

	if (test_opt(sb, XIP))
		seq_puts(seq, ",xip");

	if (test_opt(sb, DAX))
		seq_puts(seq, ",dax");

	if (!test_opt(sb, RESERVATION))
		seq_puts(seq, ",yesreservation");

	spin_unlock(&sbi->s_lock);
	return 0;
}

#ifdef CONFIG_QUOTA
static ssize_t ext2_quota_read(struct super_block *sb, int type, char *data, size_t len, loff_t off);
static ssize_t ext2_quota_write(struct super_block *sb, int type, const char *data, size_t len, loff_t off);
static int ext2_quota_on(struct super_block *sb, int type, int format_id,
			 const struct path *path);
static struct dquot **ext2_get_dquots(struct iyesde *iyesde)
{
	return EXT2_I(iyesde)->i_dquot;
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
	.alloc_iyesde	= ext2_alloc_iyesde,
	.free_iyesde	= ext2_free_in_core_iyesde,
	.write_iyesde	= ext2_write_iyesde,
	.evict_iyesde	= ext2_evict_iyesde,
	.put_super	= ext2_put_super,
	.sync_fs	= ext2_sync_fs,
	.freeze_fs	= ext2_freeze,
	.unfreeze_fs	= ext2_unfreeze,
	.statfs		= ext2_statfs,
	.remount_fs	= ext2_remount,
	.show_options	= ext2_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ext2_quota_read,
	.quota_write	= ext2_quota_write,
	.get_dquots	= ext2_get_dquots,
#endif
};

static struct iyesde *ext2_nfs_get_iyesde(struct super_block *sb,
		u64 iyes, u32 generation)
{
	struct iyesde *iyesde;

	if (iyes < EXT2_FIRST_INO(sb) && iyes != EXT2_ROOT_INO)
		return ERR_PTR(-ESTALE);
	if (iyes > le32_to_cpu(EXT2_SB(sb)->s_es->s_iyesdes_count))
		return ERR_PTR(-ESTALE);

	/*
	 * ext2_iget isn't quite right if the iyesde is currently unallocated!
	 * However ext2_iget currently does appropriate checks to handle stale
	 * iyesdes so everything is OK.
	 */
	iyesde = ext2_iget(sb, iyes);
	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);
	if (generation && iyesde->i_generation != generation) {
		/* we didn't find the right iyesde.. */
		iput(iyesde);
		return ERR_PTR(-ESTALE);
	}
	return iyesde;
}

static struct dentry *ext2_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    ext2_nfs_get_iyesde);
}

static struct dentry *ext2_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    ext2_nfs_get_iyesde);
}

static const struct export_operations ext2_export_ops = {
	.fh_to_dentry = ext2_fh_to_dentry,
	.fh_to_parent = ext2_fh_to_parent,
	.get_parent = ext2_get_parent,
};

static unsigned long get_sb_block(void **data)
{
	unsigned long 	sb_block;
	char 		*options = (char *) *data;

	if (!options || strncmp(options, "sb=", 3) != 0)
		return 1;	/* Default location */
	options += 3;
	sb_block = simple_strtoul(options, &options, 0);
	if (*options && *options != ',') {
		printk("EXT2-fs: Invalid sb specification: %s\n",
		       (char *) *data);
		return 1;
	}
	if (*options == ',')
		options++;
	*data = (void *) options;
	return sb_block;
}

enum {
	Opt_bsd_df, Opt_minix_df, Opt_grpid, Opt_yesgrpid,
	Opt_resgid, Opt_resuid, Opt_sb, Opt_err_cont, Opt_err_panic,
	Opt_err_ro, Opt_yesuid32, Opt_yescheck, Opt_debug,
	Opt_oldalloc, Opt_orlov, Opt_yesbh, Opt_user_xattr, Opt_yesuser_xattr,
	Opt_acl, Opt_yesacl, Opt_xip, Opt_dax, Opt_igyesre, Opt_err, Opt_quota,
	Opt_usrquota, Opt_grpquota, Opt_reservation, Opt_yesreservation
};

static const match_table_t tokens = {
	{Opt_bsd_df, "bsddf"},
	{Opt_minix_df, "minixdf"},
	{Opt_grpid, "grpid"},
	{Opt_grpid, "bsdgroups"},
	{Opt_yesgrpid, "yesgrpid"},
	{Opt_yesgrpid, "sysvgroups"},
	{Opt_resgid, "resgid=%u"},
	{Opt_resuid, "resuid=%u"},
	{Opt_sb, "sb=%u"},
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_yesuid32, "yesuid32"},
	{Opt_yescheck, "check=yesne"},
	{Opt_yescheck, "yescheck"},
	{Opt_debug, "debug"},
	{Opt_oldalloc, "oldalloc"},
	{Opt_orlov, "orlov"},
	{Opt_yesbh, "yesbh"},
	{Opt_user_xattr, "user_xattr"},
	{Opt_yesuser_xattr, "yesuser_xattr"},
	{Opt_acl, "acl"},
	{Opt_yesacl, "yesacl"},
	{Opt_xip, "xip"},
	{Opt_dax, "dax"},
	{Opt_grpquota, "grpquota"},
	{Opt_igyesre, "yesquota"},
	{Opt_quota, "quota"},
	{Opt_usrquota, "usrquota"},
	{Opt_reservation, "reservation"},
	{Opt_yesreservation, "yesreservation"},
	{Opt_err, NULL}
};

static int parse_options(char *options, struct super_block *sb,
			 struct ext2_mount_options *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	kuid_t uid;
	kgid_t gid;

	if (!options)
		return 1;

	while ((p = strsep (&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_bsd_df:
			clear_opt (opts->s_mount_opt, MINIX_DF);
			break;
		case Opt_minix_df:
			set_opt (opts->s_mount_opt, MINIX_DF);
			break;
		case Opt_grpid:
			set_opt (opts->s_mount_opt, GRPID);
			break;
		case Opt_yesgrpid:
			clear_opt (opts->s_mount_opt, GRPID);
			break;
		case Opt_resuid:
			if (match_int(&args[0], &option))
				return 0;
			uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid)) {
				ext2_msg(sb, KERN_ERR, "Invalid uid value %d", option);
				return 0;

			}
			opts->s_resuid = uid;
			break;
		case Opt_resgid:
			if (match_int(&args[0], &option))
				return 0;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid)) {
				ext2_msg(sb, KERN_ERR, "Invalid gid value %d", option);
				return 0;
			}
			opts->s_resgid = gid;
			break;
		case Opt_sb:
			/* handled by get_sb_block() instead of here */
			/* *sb_block = match_int(&args[0]); */
			break;
		case Opt_err_panic:
			clear_opt (opts->s_mount_opt, ERRORS_CONT);
			clear_opt (opts->s_mount_opt, ERRORS_RO);
			set_opt (opts->s_mount_opt, ERRORS_PANIC);
			break;
		case Opt_err_ro:
			clear_opt (opts->s_mount_opt, ERRORS_CONT);
			clear_opt (opts->s_mount_opt, ERRORS_PANIC);
			set_opt (opts->s_mount_opt, ERRORS_RO);
			break;
		case Opt_err_cont:
			clear_opt (opts->s_mount_opt, ERRORS_RO);
			clear_opt (opts->s_mount_opt, ERRORS_PANIC);
			set_opt (opts->s_mount_opt, ERRORS_CONT);
			break;
		case Opt_yesuid32:
			set_opt (opts->s_mount_opt, NO_UID32);
			break;
		case Opt_yescheck:
			ext2_msg(sb, KERN_WARNING,
				"Option yescheck/check=yesne is deprecated and"
				" will be removed in June 2020.");
			clear_opt (opts->s_mount_opt, CHECK);
			break;
		case Opt_debug:
			set_opt (opts->s_mount_opt, DEBUG);
			break;
		case Opt_oldalloc:
			set_opt (opts->s_mount_opt, OLDALLOC);
			break;
		case Opt_orlov:
			clear_opt (opts->s_mount_opt, OLDALLOC);
			break;
		case Opt_yesbh:
			set_opt (opts->s_mount_opt, NOBH);
			break;
#ifdef CONFIG_EXT2_FS_XATTR
		case Opt_user_xattr:
			set_opt (opts->s_mount_opt, XATTR_USER);
			break;
		case Opt_yesuser_xattr:
			clear_opt (opts->s_mount_opt, XATTR_USER);
			break;
#else
		case Opt_user_xattr:
		case Opt_yesuser_xattr:
			ext2_msg(sb, KERN_INFO, "(yes)user_xattr options"
				"yest supported");
			break;
#endif
#ifdef CONFIG_EXT2_FS_POSIX_ACL
		case Opt_acl:
			set_opt(opts->s_mount_opt, POSIX_ACL);
			break;
		case Opt_yesacl:
			clear_opt(opts->s_mount_opt, POSIX_ACL);
			break;
#else
		case Opt_acl:
		case Opt_yesacl:
			ext2_msg(sb, KERN_INFO,
				"(yes)acl options yest supported");
			break;
#endif
		case Opt_xip:
			ext2_msg(sb, KERN_INFO, "use dax instead of xip");
			set_opt(opts->s_mount_opt, XIP);
			/* Fall through */
		case Opt_dax:
#ifdef CONFIG_FS_DAX
			ext2_msg(sb, KERN_WARNING,
		"DAX enabled. Warning: EXPERIMENTAL, use at your own risk");
			set_opt(opts->s_mount_opt, DAX);
#else
			ext2_msg(sb, KERN_INFO, "dax option yest supported");
#endif
			break;

#if defined(CONFIG_QUOTA)
		case Opt_quota:
		case Opt_usrquota:
			set_opt(opts->s_mount_opt, USRQUOTA);
			break;

		case Opt_grpquota:
			set_opt(opts->s_mount_opt, GRPQUOTA);
			break;
#else
		case Opt_quota:
		case Opt_usrquota:
		case Opt_grpquota:
			ext2_msg(sb, KERN_INFO,
				"quota operations yest supported");
			break;
#endif

		case Opt_reservation:
			set_opt(opts->s_mount_opt, RESERVATION);
			ext2_msg(sb, KERN_INFO, "reservations ON");
			break;
		case Opt_yesreservation:
			clear_opt(opts->s_mount_opt, RESERVATION);
			ext2_msg(sb, KERN_INFO, "reservations OFF");
			break;
		case Opt_igyesre:
			break;
		default:
			return 0;
		}
	}
	return 1;
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
		ext2_msg(sb, KERN_INFO, "%s, %s, bs=%lu, fs=%lu, gc=%lu, "
			"bpg=%lu, ipg=%lu, mo=%04lx]",
			EXT2FS_VERSION, EXT2FS_DATE, sb->s_blocksize,
			sbi->s_frag_size,
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
		ext2_fsblk_t first_block = ext2_group_first_block_yes(sb, i);
		ext2_fsblk_t last_block = ext2_group_last_block_yes(sb, i);

		if (le32_to_cpu(gdp->bg_block_bitmap) < first_block ||
		    le32_to_cpu(gdp->bg_block_bitmap) > last_block)
		{
			ext2_error (sb, "ext2_check_descriptors",
				    "Block bitmap for group %d"
				    " yest in group (block %lu)!",
				    i, (unsigned long) le32_to_cpu(gdp->bg_block_bitmap));
			return 0;
		}
		if (le32_to_cpu(gdp->bg_iyesde_bitmap) < first_block ||
		    le32_to_cpu(gdp->bg_iyesde_bitmap) > last_block)
		{
			ext2_error (sb, "ext2_check_descriptors",
				    "Iyesde bitmap for group %d"
				    " yest in group (block %lu)!",
				    i, (unsigned long) le32_to_cpu(gdp->bg_iyesde_bitmap));
			return 0;
		}
		if (le32_to_cpu(gdp->bg_iyesde_table) < first_block ||
		    le32_to_cpu(gdp->bg_iyesde_table) + sbi->s_itb_per_group - 1 >
		    last_block)
		{
			ext2_error (sb, "ext2_check_descriptors",
				    "Iyesde table for group %d"
				    " yest in group (block %lu)!",
				    i, (unsigned long) le32_to_cpu(gdp->bg_iyesde_table));
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
	 * does yest exceed 2^32 -1
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
	/* Does block tree limit file size? */
	if (res < upper_limit)
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

	return ext2_group_first_block_yes(sb, bg) + ext2_bg_has_super(sb, bg);
}

static int ext2_fill_super(struct super_block *sb, void *data, int silent)
{
	struct dax_device *dax_dev = fs_dax_get_by_bdev(sb->s_bdev);
	struct buffer_head * bh;
	struct ext2_sb_info * sbi;
	struct ext2_super_block * es;
	struct iyesde *root;
	unsigned long block;
	unsigned long sb_block = get_sb_block(&data);
	unsigned long logic_sb_block;
	unsigned long offset = 0;
	unsigned long def_mount_opts;
	long ret = -ENOMEM;
	int blocksize = BLOCK_SIZE;
	int db_count;
	int i, j;
	__le32 features;
	int err;
	struct ext2_mount_options opts;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		goto failed;

	sbi->s_blockgroup_lock =
		kzalloc(sizeof(struct blockgroup_lock), GFP_KERNEL);
	if (!sbi->s_blockgroup_lock) {
		kfree(sbi);
		goto failed;
	}
	sb->s_fs_info = sbi;
	sbi->s_sb_block = sb_block;
	sbi->s_daxdev = dax_dev;

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

	opts.s_mount_opt = 0;
	/* Set defaults before we parse the mount options */
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	if (def_mount_opts & EXT2_DEFM_DEBUG)
		set_opt(opts.s_mount_opt, DEBUG);
	if (def_mount_opts & EXT2_DEFM_BSDGROUPS)
		set_opt(opts.s_mount_opt, GRPID);
	if (def_mount_opts & EXT2_DEFM_UID16)
		set_opt(opts.s_mount_opt, NO_UID32);
#ifdef CONFIG_EXT2_FS_XATTR
	if (def_mount_opts & EXT2_DEFM_XATTR_USER)
		set_opt(opts.s_mount_opt, XATTR_USER);
#endif
#ifdef CONFIG_EXT2_FS_POSIX_ACL
	if (def_mount_opts & EXT2_DEFM_ACL)
		set_opt(opts.s_mount_opt, POSIX_ACL);
#endif
	
	if (le16_to_cpu(sbi->s_es->s_errors) == EXT2_ERRORS_PANIC)
		set_opt(opts.s_mount_opt, ERRORS_PANIC);
	else if (le16_to_cpu(sbi->s_es->s_errors) == EXT2_ERRORS_CONTINUE)
		set_opt(opts.s_mount_opt, ERRORS_CONT);
	else
		set_opt(opts.s_mount_opt, ERRORS_RO);

	opts.s_resuid = make_kuid(&init_user_ns, le16_to_cpu(es->s_def_resuid));
	opts.s_resgid = make_kgid(&init_user_ns, le16_to_cpu(es->s_def_resgid));
	
	set_opt(opts.s_mount_opt, RESERVATION);

	if (!parse_options((char *) data, sb, &opts))
		goto failed_mount;

	sbi->s_mount_opt = opts.s_mount_opt;
	sbi->s_resuid = opts.s_resuid;
	sbi->s_resgid = opts.s_resgid;

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

	blocksize = BLOCK_SIZE << le32_to_cpu(sbi->s_es->s_log_block_size);

	if (test_opt(sb, DAX)) {
		if (!bdev_dax_supported(sb->s_bdev, blocksize)) {
			ext2_msg(sb, KERN_ERR,
				"DAX unsupported by block device. Turning off DAX.");
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
		sbi->s_iyesde_size = EXT2_GOOD_OLD_INODE_SIZE;
		sbi->s_first_iyes = EXT2_GOOD_OLD_FIRST_INO;
	} else {
		sbi->s_iyesde_size = le16_to_cpu(es->s_iyesde_size);
		sbi->s_first_iyes = le32_to_cpu(es->s_first_iyes);
		if ((sbi->s_iyesde_size < EXT2_GOOD_OLD_INODE_SIZE) ||
		    !is_power_of_2(sbi->s_iyesde_size) ||
		    (sbi->s_iyesde_size > blocksize)) {
			ext2_msg(sb, KERN_ERR,
				"error: unsupported iyesde size: %d",
				sbi->s_iyesde_size);
			goto failed_mount;
		}
	}

	sbi->s_frag_size = EXT2_MIN_FRAG_SIZE <<
				   le32_to_cpu(es->s_log_frag_size);
	if (sbi->s_frag_size == 0)
		goto cantfind_ext2;
	sbi->s_frags_per_block = sb->s_blocksize / sbi->s_frag_size;

	sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
	sbi->s_frags_per_group = le32_to_cpu(es->s_frags_per_group);
	sbi->s_iyesdes_per_group = le32_to_cpu(es->s_iyesdes_per_group);

	sbi->s_iyesdes_per_block = sb->s_blocksize / EXT2_INODE_SIZE(sb);
	if (sbi->s_iyesdes_per_block == 0 || sbi->s_iyesdes_per_group == 0)
		goto cantfind_ext2;
	sbi->s_itb_per_group = sbi->s_iyesdes_per_group /
					sbi->s_iyesdes_per_block;
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

	if (sb->s_blocksize != sbi->s_frag_size) {
		ext2_msg(sb, KERN_ERR,
			"error: fragsize %lu != blocksize %lu"
			"(yest supported yet)",
			sbi->s_frag_size, sb->s_blocksize);
		goto failed_mount;
	}

	if (sbi->s_blocks_per_group > sb->s_blocksize * 8) {
		ext2_msg(sb, KERN_ERR,
			"error: #blocks per group too big: %lu",
			sbi->s_blocks_per_group);
		goto failed_mount;
	}
	if (sbi->s_frags_per_group > sb->s_blocksize * 8) {
		ext2_msg(sb, KERN_ERR,
			"error: #fragments per group too big: %lu",
			sbi->s_frags_per_group);
		goto failed_mount;
	}
	if (sbi->s_iyesdes_per_group > sb->s_blocksize * 8) {
		ext2_msg(sb, KERN_ERR,
			"error: #iyesdes per group too big: %lu",
			sbi->s_iyesdes_per_group);
		goto failed_mount;
	}

	if (EXT2_BLOCKS_PER_GROUP(sb) == 0)
		goto cantfind_ext2;
 	sbi->s_groups_count = ((le32_to_cpu(es->s_blocks_count) -
 				le32_to_cpu(es->s_first_data_block) - 1)
 					/ EXT2_BLOCKS_PER_GROUP(sb)) + 1;
	db_count = (sbi->s_groups_count + EXT2_DESC_PER_BLOCK(sb) - 1) /
		   EXT2_DESC_PER_BLOCK(sb);
	sbi->s_group_desc = kmalloc_array (db_count,
					   sizeof(struct buffer_head *),
					   GFP_KERNEL);
	if (sbi->s_group_desc == NULL) {
		ret = -ENOMEM;
		ext2_msg(sb, KERN_ERR, "error: yest eyesugh memory");
		goto failed_mount;
	}
	bgl_lock_init(sbi->s_blockgroup_lock);
	sbi->s_debts = kcalloc(sbi->s_groups_count, sizeof(*sbi->s_debts), GFP_KERNEL);
	if (!sbi->s_debts) {
		ret = -ENOMEM;
		ext2_msg(sb, KERN_ERR, "error: yest eyesugh memory");
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

	/* per fileystem reservation list head & lock */
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
		err = percpu_counter_init(&sbi->s_freeiyesdes_counter,
				ext2_count_free_iyesdes(sb), GFP_KERNEL);
	}
	if (!err) {
		err = percpu_counter_init(&sbi->s_dirs_counter,
				ext2_count_dirs(sb), GFP_KERNEL);
	}
	if (err) {
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
	 * set up eyesugh so that it can read an iyesde
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
		ext2_msg(sb, KERN_ERR, "error: corrupt root iyesde, run e2fsck");
		goto failed_mount3;
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		ext2_msg(sb, KERN_ERR, "error: get root iyesde failed");
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
	percpu_counter_destroy(&sbi->s_freeiyesdes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
failed_mount2:
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
failed_mount_group_desc:
	kfree(sbi->s_group_desc);
	kfree(sbi->s_debts);
failed_mount:
	brelse(bh);
failed_sbi:
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
failed:
	fs_put_dax(dax_dev);
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
	es->s_free_iyesdes_count = cpu_to_le32(ext2_count_free_iyesdes(sb));
	es->s_wtime = cpu_to_le32(ktime_get_real_seconds());
	/* unlock before we do IO */
	spin_unlock(&EXT2_SB(sb)->s_lock);
	mark_buffer_dirty(EXT2_SB(sb)->s_sbh);
	if (wait)
		sync_dirty_buffer(EXT2_SB(sb)->s_sbh);
}

/*
 * In the second extended file system, it is yest necessary to
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
	 * because we have unattached iyesdes and thus filesystem is yest fully
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

static int ext2_remount (struct super_block * sb, int * flags, char * data)
{
	struct ext2_sb_info * sbi = EXT2_SB(sb);
	struct ext2_super_block * es;
	struct ext2_mount_options new_opts;
	int err;

	sync_filesystem(sb);

	spin_lock(&sbi->s_lock);
	new_opts.s_mount_opt = sbi->s_mount_opt;
	new_opts.s_resuid = sbi->s_resuid;
	new_opts.s_resgid = sbi->s_resgid;
	spin_unlock(&sbi->s_lock);

	if (!parse_options(data, sb, &new_opts))
		return -EINVAL;

	spin_lock(&sbi->s_lock);
	es = sbi->s_es;
	if ((sbi->s_mount_opt ^ new_opts.s_mount_opt) & EXT2_MOUNT_DAX) {
		ext2_msg(sb, KERN_WARNING, "warning: refusing change of "
			 "dax flag with busy iyesdes while remounting");
		new_opts.s_mount_opt ^= EXT2_MOUNT_DAX;
	}
	if ((bool)(*flags & SB_RDONLY) == sb_rdonly(sb))
		goto out_set;
	if (*flags & SB_RDONLY) {
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
	u64 fsid;

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
		 * feature is turned on, then yest all groups have this.
		 */
		for (i = 0; i < sbi->s_groups_count; i++)
			overhead += ext2_bg_has_super(sb, i) +
				ext2_bg_num_gdb(sb, i);

		/*
		 * Every block group has an iyesde bitmap, a block
		 * bitmap, and an iyesde table.
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
	buf->f_files = le32_to_cpu(es->s_iyesdes_count);
	buf->f_ffree = ext2_count_free_iyesdes(sb);
	es->s_free_iyesdes_count = cpu_to_le32(buf->f_ffree);
	buf->f_namelen = EXT2_NAME_LEN;
	fsid = le64_to_cpup((void *)es->s_uuid) ^
	       le64_to_cpup((void *)es->s_uuid + sizeof(u64));
	buf->f_fsid.val[0] = fsid & 0xFFFFFFFFUL;
	buf->f_fsid.val[1] = (fsid >> 32) & 0xFFFFFFFFUL;
	spin_unlock(&sbi->s_lock);
	return 0;
}

static struct dentry *ext2_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, ext2_fill_super);
}

#ifdef CONFIG_QUOTA

/* Read data from quotafile - avoid pagecache and such because we canyest afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and yes one else should touch the files)
 * we don't have to be afraid of races */
static ssize_t ext2_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct iyesde *iyesde = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT2_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t toread;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;
	loff_t i_size = i_size_read(iyesde);

	if (off > i_size)
		return 0;
	if (off+len > i_size)
		len = i_size-off;
	toread = len;
	while (toread > 0) {
		tocopy = sb->s_blocksize - offset < toread ?
				sb->s_blocksize - offset : toread;

		tmp_bh.b_state = 0;
		tmp_bh.b_size = sb->s_blocksize;
		err = ext2_get_block(iyesde, blk, &tmp_bh, 0);
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
	struct iyesde *iyesde = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT2_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t towrite = len;
	struct buffer_head tmp_bh;
	struct buffer_head *bh;

	while (towrite > 0) {
		tocopy = sb->s_blocksize - offset < towrite ?
				sb->s_blocksize - offset : towrite;

		tmp_bh.b_state = 0;
		tmp_bh.b_size = sb->s_blocksize;
		err = ext2_get_block(iyesde, blk, &tmp_bh, 1);
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
	if (len == towrite)
		return err;
	if (iyesde->i_size < off+len-towrite)
		i_size_write(iyesde, off+len-towrite);
	iyesde_inc_iversion(iyesde);
	iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	mark_iyesde_dirty(iyesde);
	return len - towrite;
}

static int ext2_quota_on(struct super_block *sb, int type, int format_id,
			 const struct path *path)
{
	int err;
	struct iyesde *iyesde;

	err = dquot_quota_on(sb, type, format_id, path);
	if (err)
		return err;

	iyesde = d_iyesde(path->dentry);
	iyesde_lock(iyesde);
	EXT2_I(iyesde)->i_flags |= EXT2_NOATIME_FL | EXT2_IMMUTABLE_FL;
	iyesde_set_flags(iyesde, S_NOATIME | S_IMMUTABLE,
			S_NOATIME | S_IMMUTABLE);
	iyesde_unlock(iyesde);
	mark_iyesde_dirty(iyesde);

	return 0;
}

static int ext2_quota_off(struct super_block *sb, int type)
{
	struct iyesde *iyesde = sb_dqopt(sb)->files[type];
	int err;

	if (!iyesde || !igrab(iyesde))
		goto out;

	err = dquot_quota_off(sb, type);
	if (err)
		goto out_put;

	iyesde_lock(iyesde);
	EXT2_I(iyesde)->i_flags &= ~(EXT2_NOATIME_FL | EXT2_IMMUTABLE_FL);
	iyesde_set_flags(iyesde, 0, S_NOATIME | S_IMMUTABLE);
	iyesde_unlock(iyesde);
	mark_iyesde_dirty(iyesde);
out_put:
	iput(iyesde);
	return err;
out:
	return dquot_quota_off(sb, type);
}

#endif

static struct file_system_type ext2_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext2",
	.mount		= ext2_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ext2");

static int __init init_ext2_fs(void)
{
	int err;

	err = init_iyesdecache();
	if (err)
		return err;
        err = register_filesystem(&ext2_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_iyesdecache();
	return err;
}

static void __exit exit_ext2_fs(void)
{
	unregister_filesystem(&ext2_fs_type);
	destroy_iyesdecache();
}

MODULE_AUTHOR("Remy Card and others");
MODULE_DESCRIPTION("Second Extended Filesystem");
MODULE_LICENSE("GPL");
module_init(init_ext2_fs)
module_exit(exit_ext2_fs)
