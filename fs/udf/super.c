/*
 * super.c
 *
 * PURPOSE
 *  Super block routines for the OSTA-UDF(tm) filesystem.
 *
 * DESCRIPTION
 *  OSTA-UDF(tm) = Optical Storage Technology Association
 *  Universal Disk Format.
 *
 *  This code is based on version 2.00 of the UDF specification,
 *  and revision 3 of the ECMA 167 standard [equivalent to ISO 13346].
 *    http://www.osta.org/
 *    http://www.ecma.ch/
 *    http://www.iso.org/
 *
 * COPYRIGHT
 *  This file is distributed under the terms of the GNU General Public
 *  License (GPL). Copies of the GPL can be obtained from:
 *    ftp://prep.ai.mit.edu/pub/gnu/GPL
 *  Each contributing author retains all rights to their own work.
 *
 *  (C) 1998 Dave Boynton
 *  (C) 1998-2004 Ben Fennema
 *  (C) 2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  09/24/98 dgb  changed to allow compiling outside of kernel, and
 *                added some debugging.
 *  10/01/98 dgb  updated to allow (some) possibility of compiling w/2.0.34
 *  10/16/98      attempting some multi-session support
 *  10/17/98      added freespace count for "df"
 *  11/11/98 gr   added novrs option
 *  11/26/98 dgb  added fileset,anchor mount options
 *  12/06/98 blf  really hosed things royally. vat/sparing support. sequenced
 *                vol descs. rewrote option handling based on isofs
 *  12/20/98      find the free space bitmap (if it exists)
 */

#include "udfdecl.h"

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/stat.h>
#include <linux/cdrom.h>
#include <linux/nls.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/bitmap.h>
#include <linux/crc-itu-t.h>
#include <asm/byteorder.h>

#include "udf_sb.h"
#include "udf_i.h"

#include <linux/init.h>
#include <asm/uaccess.h>

#define VDS_POS_PRIMARY_VOL_DESC	0
#define VDS_POS_UNALLOC_SPACE_DESC	1
#define VDS_POS_LOGICAL_VOL_DESC	2
#define VDS_POS_PARTITION_DESC		3
#define VDS_POS_IMP_USE_VOL_DESC	4
#define VDS_POS_VOL_DESC_PTR		5
#define VDS_POS_TERMINATING_DESC	6
#define VDS_POS_LENGTH			7

#define UDF_DEFAULT_BLOCKSIZE 2048

static char error_buf[1024];

/* These are the "meat" - everything else is stuffing */
static int udf_fill_super(struct super_block *, void *, int);
static void udf_put_super(struct super_block *);
static int udf_sync_fs(struct super_block *, int);
static int udf_remount_fs(struct super_block *, int *, char *);
static void udf_load_logicalvolint(struct super_block *, struct kernel_extent_ad);
static int udf_find_fileset(struct super_block *, struct kernel_lb_addr *,
			    struct kernel_lb_addr *);
static void udf_load_fileset(struct super_block *, struct buffer_head *,
			     struct kernel_lb_addr *);
static void udf_open_lvid(struct super_block *);
static void udf_close_lvid(struct super_block *);
static unsigned int udf_count_free(struct super_block *);
static int udf_statfs(struct dentry *, struct kstatfs *);
static int udf_show_options(struct seq_file *, struct vfsmount *);
static void udf_error(struct super_block *sb, const char *function,
		      const char *fmt, ...);

struct logicalVolIntegrityDescImpUse *udf_sb_lvidiu(struct udf_sb_info *sbi)
{
	struct logicalVolIntegrityDesc *lvid =
		(struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
	__u32 number_of_partitions = le32_to_cpu(lvid->numOfPartitions);
	__u32 offset = number_of_partitions * 2 *
				sizeof(uint32_t)/sizeof(uint8_t);
	return (struct logicalVolIntegrityDescImpUse *)&(lvid->impUse[offset]);
}

/* UDF filesystem type */
static struct dentry *udf_mount(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, udf_fill_super);
}

static struct file_system_type udf_fstype = {
	.owner		= THIS_MODULE,
	.name		= "udf",
	.mount		= udf_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static struct kmem_cache *udf_inode_cachep;

static struct inode *udf_alloc_inode(struct super_block *sb)
{
	struct udf_inode_info *ei;
	ei = kmem_cache_alloc(udf_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;

	ei->i_unique = 0;
	ei->i_lenExtents = 0;
	ei->i_next_alloc_block = 0;
	ei->i_next_alloc_goal = 0;
	ei->i_strat4096 = 0;
	init_rwsem(&ei->i_data_sem);

	return &ei->vfs_inode;
}

static void udf_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	INIT_LIST_HEAD(&inode->i_dentry);
	kmem_cache_free(udf_inode_cachep, UDF_I(inode));
}

static void udf_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, udf_i_callback);
}

static void init_once(void *foo)
{
	struct udf_inode_info *ei = (struct udf_inode_info *)foo;

	ei->i_ext.i_data = NULL;
	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	udf_inode_cachep = kmem_cache_create("udf_inode_cache",
					     sizeof(struct udf_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT |
						 SLAB_MEM_SPREAD),
					     init_once);
	if (!udf_inode_cachep)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(udf_inode_cachep);
}

/* Superblock operations */
static const struct super_operations udf_sb_ops = {
	.alloc_inode	= udf_alloc_inode,
	.destroy_inode	= udf_destroy_inode,
	.write_inode	= udf_write_inode,
	.evict_inode	= udf_evict_inode,
	.put_super	= udf_put_super,
	.sync_fs	= udf_sync_fs,
	.statfs		= udf_statfs,
	.remount_fs	= udf_remount_fs,
	.show_options	= udf_show_options,
};

struct udf_options {
	unsigned char novrs;
	unsigned int blocksize;
	unsigned int session;
	unsigned int lastblock;
	unsigned int anchor;
	unsigned int volume;
	unsigned short partition;
	unsigned int fileset;
	unsigned int rootdir;
	unsigned int flags;
	mode_t umask;
	gid_t gid;
	uid_t uid;
	mode_t fmode;
	mode_t dmode;
	struct nls_table *nls_map;
};

static int __init init_udf_fs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&udf_fstype);
	if (err)
		goto out;

	return 0;

out:
	destroy_inodecache();

out1:
	return err;
}

static void __exit exit_udf_fs(void)
{
	unregister_filesystem(&udf_fstype);
	destroy_inodecache();
}

module_init(init_udf_fs)
module_exit(exit_udf_fs)

static int udf_sb_alloc_partition_maps(struct super_block *sb, u32 count)
{
	struct udf_sb_info *sbi = UDF_SB(sb);

	sbi->s_partmaps = kcalloc(count, sizeof(struct udf_part_map),
				  GFP_KERNEL);
	if (!sbi->s_partmaps) {
		udf_error(sb, __func__,
			  "Unable to allocate space for %d partition maps",
			  count);
		sbi->s_partitions = 0;
		return -ENOMEM;
	}

	sbi->s_partitions = count;
	return 0;
}

static int udf_show_options(struct seq_file *seq, struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);

	if (!UDF_QUERY_FLAG(sb, UDF_FLAG_STRICT))
		seq_puts(seq, ",nostrict");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_BLOCKSIZE_SET))
		seq_printf(seq, ",bs=%lu", sb->s_blocksize);
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UNHIDE))
		seq_puts(seq, ",unhide");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UNDELETE))
		seq_puts(seq, ",undelete");
	if (!UDF_QUERY_FLAG(sb, UDF_FLAG_USE_AD_IN_ICB))
		seq_puts(seq, ",noadinicb");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_USE_SHORT_AD))
		seq_puts(seq, ",shortad");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UID_FORGET))
		seq_puts(seq, ",uid=forget");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UID_IGNORE))
		seq_puts(seq, ",uid=ignore");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_GID_FORGET))
		seq_puts(seq, ",gid=forget");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_GID_IGNORE))
		seq_puts(seq, ",gid=ignore");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UID_SET))
		seq_printf(seq, ",uid=%u", sbi->s_uid);
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_GID_SET))
		seq_printf(seq, ",gid=%u", sbi->s_gid);
	if (sbi->s_umask != 0)
		seq_printf(seq, ",umask=%o", sbi->s_umask);
	if (sbi->s_fmode != UDF_INVALID_MODE)
		seq_printf(seq, ",mode=%o", sbi->s_fmode);
	if (sbi->s_dmode != UDF_INVALID_MODE)
		seq_printf(seq, ",dmode=%o", sbi->s_dmode);
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_SESSION_SET))
		seq_printf(seq, ",session=%u", sbi->s_session);
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_LASTBLOCK_SET))
		seq_printf(seq, ",lastblock=%u", sbi->s_last_block);
	if (sbi->s_anchor != 0)
		seq_printf(seq, ",anchor=%u", sbi->s_anchor);
	/*
	 * volume, partition, fileset and rootdir seem to be ignored
	 * currently
	 */
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UTF8))
		seq_puts(seq, ",utf8");
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP) && sbi->s_nls_map)
		seq_printf(seq, ",iocharset=%s", sbi->s_nls_map->charset);

	return 0;
}

/*
 * udf_parse_options
 *
 * PURPOSE
 *	Parse mount options.
 *
 * DESCRIPTION
 *	The following mount options are supported:
 *
 *	gid=		Set the default group.
 *	umask=		Set the default umask.
 *	mode=		Set the default file permissions.
 *	dmode=		Set the default directory permissions.
 *	uid=		Set the default user.
 *	bs=		Set the block size.
 *	unhide		Show otherwise hidden files.
 *	undelete	Show deleted files in lists.
 *	adinicb		Embed data in the inode (default)
 *	noadinicb	Don't embed data in the inode
 *	shortad		Use short ad's
 *	longad		Use long ad's (default)
 *	nostrict	Unset strict conformance
 *	iocharset=	Set the NLS character set
 *
 *	The remaining are for debugging and disaster recovery:
 *
 *	novrs		Skip volume sequence recognition
 *
 *	The following expect a offset from 0.
 *
 *	session=	Set the CDROM session (default= last session)
 *	anchor=		Override standard anchor location. (default= 256)
 *	volume=		Override the VolumeDesc location. (unused)
 *	partition=	Override the PartitionDesc location. (unused)
 *	lastblock=	Set the last block of the filesystem/
 *
 *	The following expect a offset from the partition root.
 *
 *	fileset=	Override the fileset block location. (unused)
 *	rootdir=	Override the root directory location. (unused)
 *		WARNING: overriding the rootdir to a non-directory may
 *		yield highly unpredictable results.
 *
 * PRE-CONDITIONS
 *	options		Pointer to mount options string.
 *	uopts		Pointer to mount options variable.
 *
 * POST-CONDITIONS
 *	<return>	1	Mount options parsed okay.
 *	<return>	0	Error parsing mount options.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */

enum {
	Opt_novrs, Opt_nostrict, Opt_bs, Opt_unhide, Opt_undelete,
	Opt_noadinicb, Opt_adinicb, Opt_shortad, Opt_longad,
	Opt_gid, Opt_uid, Opt_umask, Opt_session, Opt_lastblock,
	Opt_anchor, Opt_volume, Opt_partition, Opt_fileset,
	Opt_rootdir, Opt_utf8, Opt_iocharset,
	Opt_err, Opt_uforget, Opt_uignore, Opt_gforget, Opt_gignore,
	Opt_fmode, Opt_dmode
};

static const match_table_t tokens = {
	{Opt_novrs,	"novrs"},
	{Opt_nostrict,	"nostrict"},
	{Opt_bs,	"bs=%u"},
	{Opt_unhide,	"unhide"},
	{Opt_undelete,	"undelete"},
	{Opt_noadinicb,	"noadinicb"},
	{Opt_adinicb,	"adinicb"},
	{Opt_shortad,	"shortad"},
	{Opt_longad,	"longad"},
	{Opt_uforget,	"uid=forget"},
	{Opt_uignore,	"uid=ignore"},
	{Opt_gforget,	"gid=forget"},
	{Opt_gignore,	"gid=ignore"},
	{Opt_gid,	"gid=%u"},
	{Opt_uid,	"uid=%u"},
	{Opt_umask,	"umask=%o"},
	{Opt_session,	"session=%u"},
	{Opt_lastblock,	"lastblock=%u"},
	{Opt_anchor,	"anchor=%u"},
	{Opt_volume,	"volume=%u"},
	{Opt_partition,	"partition=%u"},
	{Opt_fileset,	"fileset=%u"},
	{Opt_rootdir,	"rootdir=%u"},
	{Opt_utf8,	"utf8"},
	{Opt_iocharset,	"iocharset=%s"},
	{Opt_fmode,     "mode=%o"},
	{Opt_dmode,     "dmode=%o"},
	{Opt_err,	NULL}
};

static int udf_parse_options(char *options, struct udf_options *uopt,
			     bool remount)
{
	char *p;
	int option;

	uopt->novrs = 0;
	uopt->partition = 0xFFFF;
	uopt->session = 0xFFFFFFFF;
	uopt->lastblock = 0;
	uopt->anchor = 0;
	uopt->volume = 0xFFFFFFFF;
	uopt->rootdir = 0xFFFFFFFF;
	uopt->fileset = 0xFFFFFFFF;
	uopt->nls_map = NULL;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_novrs:
			uopt->novrs = 1;
			break;
		case Opt_bs:
			if (match_int(&args[0], &option))
				return 0;
			uopt->blocksize = option;
			uopt->flags |= (1 << UDF_FLAG_BLOCKSIZE_SET);
			break;
		case Opt_unhide:
			uopt->flags |= (1 << UDF_FLAG_UNHIDE);
			break;
		case Opt_undelete:
			uopt->flags |= (1 << UDF_FLAG_UNDELETE);
			break;
		case Opt_noadinicb:
			uopt->flags &= ~(1 << UDF_FLAG_USE_AD_IN_ICB);
			break;
		case Opt_adinicb:
			uopt->flags |= (1 << UDF_FLAG_USE_AD_IN_ICB);
			break;
		case Opt_shortad:
			uopt->flags |= (1 << UDF_FLAG_USE_SHORT_AD);
			break;
		case Opt_longad:
			uopt->flags &= ~(1 << UDF_FLAG_USE_SHORT_AD);
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return 0;
			uopt->gid = option;
			uopt->flags |= (1 << UDF_FLAG_GID_SET);
			break;
		case Opt_uid:
			if (match_int(args, &option))
				return 0;
			uopt->uid = option;
			uopt->flags |= (1 << UDF_FLAG_UID_SET);
			break;
		case Opt_umask:
			if (match_octal(args, &option))
				return 0;
			uopt->umask = option;
			break;
		case Opt_nostrict:
			uopt->flags &= ~(1 << UDF_FLAG_STRICT);
			break;
		case Opt_session:
			if (match_int(args, &option))
				return 0;
			uopt->session = option;
			if (!remount)
				uopt->flags |= (1 << UDF_FLAG_SESSION_SET);
			break;
		case Opt_lastblock:
			if (match_int(args, &option))
				return 0;
			uopt->lastblock = option;
			if (!remount)
				uopt->flags |= (1 << UDF_FLAG_LASTBLOCK_SET);
			break;
		case Opt_anchor:
			if (match_int(args, &option))
				return 0;
			uopt->anchor = option;
			break;
		case Opt_volume:
			if (match_int(args, &option))
				return 0;
			uopt->volume = option;
			break;
		case Opt_partition:
			if (match_int(args, &option))
				return 0;
			uopt->partition = option;
			break;
		case Opt_fileset:
			if (match_int(args, &option))
				return 0;
			uopt->fileset = option;
			break;
		case Opt_rootdir:
			if (match_int(args, &option))
				return 0;
			uopt->rootdir = option;
			break;
		case Opt_utf8:
			uopt->flags |= (1 << UDF_FLAG_UTF8);
			break;
#ifdef CONFIG_UDF_NLS
		case Opt_iocharset:
			uopt->nls_map = load_nls(args[0].from);
			uopt->flags |= (1 << UDF_FLAG_NLS_MAP);
			break;
#endif
		case Opt_uignore:
			uopt->flags |= (1 << UDF_FLAG_UID_IGNORE);
			break;
		case Opt_uforget:
			uopt->flags |= (1 << UDF_FLAG_UID_FORGET);
			break;
		case Opt_gignore:
			uopt->flags |= (1 << UDF_FLAG_GID_IGNORE);
			break;
		case Opt_gforget:
			uopt->flags |= (1 << UDF_FLAG_GID_FORGET);
			break;
		case Opt_fmode:
			if (match_octal(args, &option))
				return 0;
			uopt->fmode = option & 0777;
			break;
		case Opt_dmode:
			if (match_octal(args, &option))
				return 0;
			uopt->dmode = option & 0777;
			break;
		default:
			printk(KERN_ERR "udf: bad mount option \"%s\" "
			       "or missing value\n", p);
			return 0;
		}
	}
	return 1;
}

static int udf_remount_fs(struct super_block *sb, int *flags, char *options)
{
	struct udf_options uopt;
	struct udf_sb_info *sbi = UDF_SB(sb);
	int error = 0;

	uopt.flags = sbi->s_flags;
	uopt.uid   = sbi->s_uid;
	uopt.gid   = sbi->s_gid;
	uopt.umask = sbi->s_umask;
	uopt.fmode = sbi->s_fmode;
	uopt.dmode = sbi->s_dmode;

	if (!udf_parse_options(options, &uopt, true))
		return -EINVAL;

	write_lock(&sbi->s_cred_lock);
	sbi->s_flags = uopt.flags;
	sbi->s_uid   = uopt.uid;
	sbi->s_gid   = uopt.gid;
	sbi->s_umask = uopt.umask;
	sbi->s_fmode = uopt.fmode;
	sbi->s_dmode = uopt.dmode;
	write_unlock(&sbi->s_cred_lock);

	if (sbi->s_lvid_bh) {
		int write_rev = le16_to_cpu(udf_sb_lvidiu(sbi)->minUDFWriteRev);
		if (write_rev > UDF_MAX_WRITE_VERSION)
			*flags |= MS_RDONLY;
	}

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		goto out_unlock;

	if (*flags & MS_RDONLY)
		udf_close_lvid(sb);
	else
		udf_open_lvid(sb);

out_unlock:
	return error;
}

/* Check Volume Structure Descriptors (ECMA 167 2/9.1) */
/* We also check any "CD-ROM Volume Descriptor Set" (ECMA 167 2/8.3.1) */
static loff_t udf_check_vsd(struct super_block *sb)
{
	struct volStructDesc *vsd = NULL;
	loff_t sector = 32768;
	int sectorsize;
	struct buffer_head *bh = NULL;
	int nsr02 = 0;
	int nsr03 = 0;
	struct udf_sb_info *sbi;

	sbi = UDF_SB(sb);
	if (sb->s_blocksize < sizeof(struct volStructDesc))
		sectorsize = sizeof(struct volStructDesc);
	else
		sectorsize = sb->s_blocksize;

	sector += (sbi->s_session << sb->s_blocksize_bits);

	udf_debug("Starting at sector %u (%ld byte sectors)\n",
		  (unsigned int)(sector >> sb->s_blocksize_bits),
		  sb->s_blocksize);
	/* Process the sequence (if applicable) */
	for (; !nsr02 && !nsr03; sector += sectorsize) {
		/* Read a block */
		bh = udf_tread(sb, sector >> sb->s_blocksize_bits);
		if (!bh)
			break;

		/* Look for ISO  descriptors */
		vsd = (struct volStructDesc *)(bh->b_data +
					      (sector & (sb->s_blocksize - 1)));

		if (vsd->stdIdent[0] == 0) {
			brelse(bh);
			break;
		} else if (!strncmp(vsd->stdIdent, VSD_STD_ID_CD001,
				    VSD_STD_ID_LEN)) {
			switch (vsd->structType) {
			case 0:
				udf_debug("ISO9660 Boot Record found\n");
				break;
			case 1:
				udf_debug("ISO9660 Primary Volume Descriptor "
					  "found\n");
				break;
			case 2:
				udf_debug("ISO9660 Supplementary Volume "
					  "Descriptor found\n");
				break;
			case 3:
				udf_debug("ISO9660 Volume Partition Descriptor "
					  "found\n");
				break;
			case 255:
				udf_debug("ISO9660 Volume Descriptor Set "
					  "Terminator found\n");
				break;
			default:
				udf_debug("ISO9660 VRS (%u) found\n",
					  vsd->structType);
				break;
			}
		} else if (!strncmp(vsd->stdIdent, VSD_STD_ID_BEA01,
				    VSD_STD_ID_LEN))
			; /* nothing */
		else if (!strncmp(vsd->stdIdent, VSD_STD_ID_TEA01,
				    VSD_STD_ID_LEN)) {
			brelse(bh);
			break;
		} else if (!strncmp(vsd->stdIdent, VSD_STD_ID_NSR02,
				    VSD_STD_ID_LEN))
			nsr02 = sector;
		else if (!strncmp(vsd->stdIdent, VSD_STD_ID_NSR03,
				    VSD_STD_ID_LEN))
			nsr03 = sector;
		brelse(bh);
	}

	if (nsr03)
		return nsr03;
	else if (nsr02)
		return nsr02;
	else if (sector - (sbi->s_session << sb->s_blocksize_bits) == 32768)
		return -1;
	else
		return 0;
}

static int udf_find_fileset(struct super_block *sb,
			    struct kernel_lb_addr *fileset,
			    struct kernel_lb_addr *root)
{
	struct buffer_head *bh = NULL;
	long lastblock;
	uint16_t ident;
	struct udf_sb_info *sbi;

	if (fileset->logicalBlockNum != 0xFFFFFFFF ||
	    fileset->partitionReferenceNum != 0xFFFF) {
		bh = udf_read_ptagged(sb, fileset, 0, &ident);

		if (!bh) {
			return 1;
		} else if (ident != TAG_IDENT_FSD) {
			brelse(bh);
			return 1;
		}

	}

	sbi = UDF_SB(sb);
	if (!bh) {
		/* Search backwards through the partitions */
		struct kernel_lb_addr newfileset;

/* --> cvg: FIXME - is it reasonable? */
		return 1;

		for (newfileset.partitionReferenceNum = sbi->s_partitions - 1;
		     (newfileset.partitionReferenceNum != 0xFFFF &&
		      fileset->logicalBlockNum == 0xFFFFFFFF &&
		      fileset->partitionReferenceNum == 0xFFFF);
		     newfileset.partitionReferenceNum--) {
			lastblock = sbi->s_partmaps
					[newfileset.partitionReferenceNum]
						.s_partition_len;
			newfileset.logicalBlockNum = 0;

			do {
				bh = udf_read_ptagged(sb, &newfileset, 0,
						      &ident);
				if (!bh) {
					newfileset.logicalBlockNum++;
					continue;
				}

				switch (ident) {
				case TAG_IDENT_SBD:
				{
					struct spaceBitmapDesc *sp;
					sp = (struct spaceBitmapDesc *)
								bh->b_data;
					newfileset.logicalBlockNum += 1 +
						((le32_to_cpu(sp->numOfBytes) +
						  sizeof(struct spaceBitmapDesc)
						  - 1) >> sb->s_blocksize_bits);
					brelse(bh);
					break;
				}
				case TAG_IDENT_FSD:
					*fileset = newfileset;
					break;
				default:
					newfileset.logicalBlockNum++;
					brelse(bh);
					bh = NULL;
					break;
				}
			} while (newfileset.logicalBlockNum < lastblock &&
				 fileset->logicalBlockNum == 0xFFFFFFFF &&
				 fileset->partitionReferenceNum == 0xFFFF);
		}
	}

	if ((fileset->logicalBlockNum != 0xFFFFFFFF ||
	     fileset->partitionReferenceNum != 0xFFFF) && bh) {
		udf_debug("Fileset at block=%d, partition=%d\n",
			  fileset->logicalBlockNum,
			  fileset->partitionReferenceNum);

		sbi->s_partition = fileset->partitionReferenceNum;
		udf_load_fileset(sb, bh, root);
		brelse(bh);
		return 0;
	}
	return 1;
}

static int udf_load_pvoldesc(struct super_block *sb, sector_t block)
{
	struct primaryVolDesc *pvoldesc;
	struct ustr *instr, *outstr;
	struct buffer_head *bh;
	uint16_t ident;
	int ret = 1;

	instr = kmalloc(sizeof(struct ustr), GFP_NOFS);
	if (!instr)
		return 1;

	outstr = kmalloc(sizeof(struct ustr), GFP_NOFS);
	if (!outstr)
		goto out1;

	bh = udf_read_tagged(sb, block, block, &ident);
	if (!bh)
		goto out2;

	BUG_ON(ident != TAG_IDENT_PVD);

	pvoldesc = (struct primaryVolDesc *)bh->b_data;

	if (udf_disk_stamp_to_time(&UDF_SB(sb)->s_record_time,
			      pvoldesc->recordingDateAndTime)) {
#ifdef UDFFS_DEBUG
		struct timestamp *ts = &pvoldesc->recordingDateAndTime;
		udf_debug("recording time %04u/%02u/%02u"
			  " %02u:%02u (%x)\n",
			  le16_to_cpu(ts->year), ts->month, ts->day, ts->hour,
			  ts->minute, le16_to_cpu(ts->typeAndTimezone));
#endif
	}

	if (!udf_build_ustr(instr, pvoldesc->volIdent, 32))
		if (udf_CS0toUTF8(outstr, instr)) {
			strncpy(UDF_SB(sb)->s_volume_ident, outstr->u_name,
				outstr->u_len > 31 ? 31 : outstr->u_len);
			udf_debug("volIdent[] = '%s'\n",
					UDF_SB(sb)->s_volume_ident);
		}

	if (!udf_build_ustr(instr, pvoldesc->volSetIdent, 128))
		if (udf_CS0toUTF8(outstr, instr))
			udf_debug("volSetIdent[] = '%s'\n", outstr->u_name);

	brelse(bh);
	ret = 0;
out2:
	kfree(outstr);
out1:
	kfree(instr);
	return ret;
}

static int udf_load_metadata_files(struct super_block *sb, int partition)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct udf_part_map *map;
	struct udf_meta_data *mdata;
	struct kernel_lb_addr addr;
	int fe_error = 0;

	map = &sbi->s_partmaps[partition];
	mdata = &map->s_type_specific.s_metadata;

	/* metadata address */
	addr.logicalBlockNum =  mdata->s_meta_file_loc;
	addr.partitionReferenceNum = map->s_partition_num;

	udf_debug("Metadata file location: block = %d part = %d\n",
			  addr.logicalBlockNum, addr.partitionReferenceNum);

	mdata->s_metadata_fe = udf_iget(sb, &addr);

	if (mdata->s_metadata_fe == NULL) {
		udf_warning(sb, __func__, "metadata inode efe not found, "
				"will try mirror inode.");
		fe_error = 1;
	} else if (UDF_I(mdata->s_metadata_fe)->i_alloc_type !=
		 ICBTAG_FLAG_AD_SHORT) {
		udf_warning(sb, __func__, "metadata inode efe does not have "
			"short allocation descriptors!");
		fe_error = 1;
		iput(mdata->s_metadata_fe);
		mdata->s_metadata_fe = NULL;
	}

	/* mirror file entry */
	addr.logicalBlockNum = mdata->s_mirror_file_loc;
	addr.partitionReferenceNum = map->s_partition_num;

	udf_debug("Mirror metadata file location: block = %d part = %d\n",
			  addr.logicalBlockNum, addr.partitionReferenceNum);

	mdata->s_mirror_fe = udf_iget(sb, &addr);

	if (mdata->s_mirror_fe == NULL) {
		if (fe_error) {
			udf_error(sb, __func__, "mirror inode efe not found "
			"and metadata inode is missing too, exiting...");
			goto error_exit;
		} else
			udf_warning(sb, __func__, "mirror inode efe not found,"
					" but metadata inode is OK");
	} else if (UDF_I(mdata->s_mirror_fe)->i_alloc_type !=
		 ICBTAG_FLAG_AD_SHORT) {
		udf_warning(sb, __func__, "mirror inode efe does not have "
			"short allocation descriptors!");
		iput(mdata->s_mirror_fe);
		mdata->s_mirror_fe = NULL;
		if (fe_error)
			goto error_exit;
	}

	/*
	 * bitmap file entry
	 * Note:
	 * Load only if bitmap file location differs from 0xFFFFFFFF (DCN-5102)
	*/
	if (mdata->s_bitmap_file_loc != 0xFFFFFFFF) {
		addr.logicalBlockNum = mdata->s_bitmap_file_loc;
		addr.partitionReferenceNum = map->s_partition_num;

		udf_debug("Bitmap file location: block = %d part = %d\n",
			addr.logicalBlockNum, addr.partitionReferenceNum);

		mdata->s_bitmap_fe = udf_iget(sb, &addr);

		if (mdata->s_bitmap_fe == NULL) {
			if (sb->s_flags & MS_RDONLY)
				udf_warning(sb, __func__, "bitmap inode efe "
					"not found but it's ok since the disc"
					" is mounted read-only");
			else {
				udf_error(sb, __func__, "bitmap inode efe not "
					"found and attempted read-write mount");
				goto error_exit;
			}
		}
	}

	udf_debug("udf_load_metadata_files Ok\n");

	return 0;

error_exit:
	return 1;
}

static void udf_load_fileset(struct super_block *sb, struct buffer_head *bh,
			     struct kernel_lb_addr *root)
{
	struct fileSetDesc *fset;

	fset = (struct fileSetDesc *)bh->b_data;

	*root = lelb_to_cpu(fset->rootDirectoryICB.extLocation);

	UDF_SB(sb)->s_serial_number = le16_to_cpu(fset->descTag.tagSerialNum);

	udf_debug("Rootdir at block=%d, partition=%d\n",
		  root->logicalBlockNum, root->partitionReferenceNum);
}

int udf_compute_nr_groups(struct super_block *sb, u32 partition)
{
	struct udf_part_map *map = &UDF_SB(sb)->s_partmaps[partition];
	return DIV_ROUND_UP(map->s_partition_len +
			    (sizeof(struct spaceBitmapDesc) << 3),
			    sb->s_blocksize * 8);
}

static struct udf_bitmap *udf_sb_alloc_bitmap(struct super_block *sb, u32 index)
{
	struct udf_bitmap *bitmap;
	int nr_groups;
	int size;

	nr_groups = udf_compute_nr_groups(sb, index);
	size = sizeof(struct udf_bitmap) +
		(sizeof(struct buffer_head *) * nr_groups);

	if (size <= PAGE_SIZE)
		bitmap = kzalloc(size, GFP_KERNEL);
	else
		bitmap = vzalloc(size); /* TODO: get rid of vzalloc */

	if (bitmap == NULL) {
		udf_error(sb, __func__,
			  "Unable to allocate space for bitmap "
			  "and %d buffer_head pointers", nr_groups);
		return NULL;
	}

	bitmap->s_block_bitmap = (struct buffer_head **)(bitmap + 1);
	bitmap->s_nr_groups = nr_groups;
	return bitmap;
}

static int udf_fill_partdesc_info(struct super_block *sb,
		struct partitionDesc *p, int p_index)
{
	struct udf_part_map *map;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct partitionHeaderDesc *phd;

	map = &sbi->s_partmaps[p_index];

	map->s_partition_len = le32_to_cpu(p->partitionLength); /* blocks */
	map->s_partition_root = le32_to_cpu(p->partitionStartingLocation);

	if (p->accessType == cpu_to_le32(PD_ACCESS_TYPE_READ_ONLY))
		map->s_partition_flags |= UDF_PART_FLAG_READ_ONLY;
	if (p->accessType == cpu_to_le32(PD_ACCESS_TYPE_WRITE_ONCE))
		map->s_partition_flags |= UDF_PART_FLAG_WRITE_ONCE;
	if (p->accessType == cpu_to_le32(PD_ACCESS_TYPE_REWRITABLE))
		map->s_partition_flags |= UDF_PART_FLAG_REWRITABLE;
	if (p->accessType == cpu_to_le32(PD_ACCESS_TYPE_OVERWRITABLE))
		map->s_partition_flags |= UDF_PART_FLAG_OVERWRITABLE;

	udf_debug("Partition (%d type %x) starts at physical %d, "
		  "block length %d\n", p_index,
		  map->s_partition_type, map->s_partition_root,
		  map->s_partition_len);

	if (strcmp(p->partitionContents.ident, PD_PARTITION_CONTENTS_NSR02) &&
	    strcmp(p->partitionContents.ident, PD_PARTITION_CONTENTS_NSR03))
		return 0;

	phd = (struct partitionHeaderDesc *)p->partitionContentsUse;
	if (phd->unallocSpaceTable.extLength) {
		struct kernel_lb_addr loc = {
			.logicalBlockNum = le32_to_cpu(
				phd->unallocSpaceTable.extPosition),
			.partitionReferenceNum = p_index,
		};

		map->s_uspace.s_table = udf_iget(sb, &loc);
		if (!map->s_uspace.s_table) {
			udf_debug("cannot load unallocSpaceTable (part %d)\n",
					p_index);
			return 1;
		}
		map->s_partition_flags |= UDF_PART_FLAG_UNALLOC_TABLE;
		udf_debug("unallocSpaceTable (part %d) @ %ld\n",
				p_index, map->s_uspace.s_table->i_ino);
	}

	if (phd->unallocSpaceBitmap.extLength) {
		struct udf_bitmap *bitmap = udf_sb_alloc_bitmap(sb, p_index);
		if (!bitmap)
			return 1;
		map->s_uspace.s_bitmap = bitmap;
		bitmap->s_extLength = le32_to_cpu(
				phd->unallocSpaceBitmap.extLength);
		bitmap->s_extPosition = le32_to_cpu(
				phd->unallocSpaceBitmap.extPosition);
		map->s_partition_flags |= UDF_PART_FLAG_UNALLOC_BITMAP;
		udf_debug("unallocSpaceBitmap (part %d) @ %d\n", p_index,
						bitmap->s_extPosition);
	}

	if (phd->partitionIntegrityTable.extLength)
		udf_debug("partitionIntegrityTable (part %d)\n", p_index);

	if (phd->freedSpaceTable.extLength) {
		struct kernel_lb_addr loc = {
			.logicalBlockNum = le32_to_cpu(
				phd->freedSpaceTable.extPosition),
			.partitionReferenceNum = p_index,
		};

		map->s_fspace.s_table = udf_iget(sb, &loc);
		if (!map->s_fspace.s_table) {
			udf_debug("cannot load freedSpaceTable (part %d)\n",
				p_index);
			return 1;
		}

		map->s_partition_flags |= UDF_PART_FLAG_FREED_TABLE;
		udf_debug("freedSpaceTable (part %d) @ %ld\n",
				p_index, map->s_fspace.s_table->i_ino);
	}

	if (phd->freedSpaceBitmap.extLength) {
		struct udf_bitmap *bitmap = udf_sb_alloc_bitmap(sb, p_index);
		if (!bitmap)
			return 1;
		map->s_fspace.s_bitmap = bitmap;
		bitmap->s_extLength = le32_to_cpu(
				phd->freedSpaceBitmap.extLength);
		bitmap->s_extPosition = le32_to_cpu(
				phd->freedSpaceBitmap.extPosition);
		map->s_partition_flags |= UDF_PART_FLAG_FREED_BITMAP;
		udf_debug("freedSpaceBitmap (part %d) @ %d\n", p_index,
					bitmap->s_extPosition);
	}
	return 0;
}

static void udf_find_vat_block(struct super_block *sb, int p_index,
			       int type1_index, sector_t start_block)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct udf_part_map *map = &sbi->s_partmaps[p_index];
	sector_t vat_block;
	struct kernel_lb_addr ino;

	/*
	 * VAT file entry is in the last recorded block. Some broken disks have
	 * it a few blocks before so try a bit harder...
	 */
	ino.partitionReferenceNum = type1_index;
	for (vat_block = start_block;
	     vat_block >= map->s_partition_root &&
	     vat_block >= start_block - 3 &&
	     !sbi->s_vat_inode; vat_block--) {
		ino.logicalBlockNum = vat_block - map->s_partition_root;
		sbi->s_vat_inode = udf_iget(sb, &ino);
	}
}

static int udf_load_vat(struct super_block *sb, int p_index, int type1_index)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct udf_part_map *map = &sbi->s_partmaps[p_index];
	struct buffer_head *bh = NULL;
	struct udf_inode_info *vati;
	uint32_t pos;
	struct virtualAllocationTable20 *vat20;
	sector_t blocks = sb->s_bdev->bd_inode->i_size >> sb->s_blocksize_bits;

	udf_find_vat_block(sb, p_index, type1_index, sbi->s_last_block);
	if (!sbi->s_vat_inode &&
	    sbi->s_last_block != blocks - 1) {
		printk(KERN_NOTICE "UDF-fs: Failed to read VAT inode from the"
		       " last recorded block (%lu), retrying with the last "
		       "block of the device (%lu).\n",
		       (unsigned long)sbi->s_last_block,
		       (unsigned long)blocks - 1);
		udf_find_vat_block(sb, p_index, type1_index, blocks - 1);
	}
	if (!sbi->s_vat_inode)
		return 1;

	if (map->s_partition_type == UDF_VIRTUAL_MAP15) {
		map->s_type_specific.s_virtual.s_start_offset = 0;
		map->s_type_specific.s_virtual.s_num_entries =
			(sbi->s_vat_inode->i_size - 36) >> 2;
	} else if (map->s_partition_type == UDF_VIRTUAL_MAP20) {
		vati = UDF_I(sbi->s_vat_inode);
		if (vati->i_alloc_type != ICBTAG_FLAG_AD_IN_ICB) {
			pos = udf_block_map(sbi->s_vat_inode, 0);
			bh = sb_bread(sb, pos);
			if (!bh)
				return 1;
			vat20 = (struct virtualAllocationTable20 *)bh->b_data;
		} else {
			vat20 = (struct virtualAllocationTable20 *)
							vati->i_ext.i_data;
		}

		map->s_type_specific.s_virtual.s_start_offset =
			le16_to_cpu(vat20->lengthHeader);
		map->s_type_specific.s_virtual.s_num_entries =
			(sbi->s_vat_inode->i_size -
				map->s_type_specific.s_virtual.
					s_start_offset) >> 2;
		brelse(bh);
	}
	return 0;
}

static int udf_load_partdesc(struct super_block *sb, sector_t block)
{
	struct buffer_head *bh;
	struct partitionDesc *p;
	struct udf_part_map *map;
	struct udf_sb_info *sbi = UDF_SB(sb);
	int i, type1_idx;
	uint16_t partitionNumber;
	uint16_t ident;
	int ret = 0;

	bh = udf_read_tagged(sb, block, block, &ident);
	if (!bh)
		return 1;
	if (ident != TAG_IDENT_PD)
		goto out_bh;

	p = (struct partitionDesc *)bh->b_data;
	partitionNumber = le16_to_cpu(p->partitionNumber);

	/* First scan for TYPE1, SPARABLE and METADATA partitions */
	for (i = 0; i < sbi->s_partitions; i++) {
		map = &sbi->s_partmaps[i];
		udf_debug("Searching map: (%d == %d)\n",
			  map->s_partition_num, partitionNumber);
		if (map->s_partition_num == partitionNumber &&
		    (map->s_partition_type == UDF_TYPE1_MAP15 ||
		     map->s_partition_type == UDF_SPARABLE_MAP15))
			break;
	}

	if (i >= sbi->s_partitions) {
		udf_debug("Partition (%d) not found in partition map\n",
			  partitionNumber);
		goto out_bh;
	}

	ret = udf_fill_partdesc_info(sb, p, i);

	/*
	 * Now rescan for VIRTUAL or METADATA partitions when SPARABLE and
	 * PHYSICAL partitions are already set up
	 */
	type1_idx = i;
	for (i = 0; i < sbi->s_partitions; i++) {
		map = &sbi->s_partmaps[i];

		if (map->s_partition_num == partitionNumber &&
		    (map->s_partition_type == UDF_VIRTUAL_MAP15 ||
		     map->s_partition_type == UDF_VIRTUAL_MAP20 ||
		     map->s_partition_type == UDF_METADATA_MAP25))
			break;
	}

	if (i >= sbi->s_partitions)
		goto out_bh;

	ret = udf_fill_partdesc_info(sb, p, i);
	if (ret)
		goto out_bh;

	if (map->s_partition_type == UDF_METADATA_MAP25) {
		ret = udf_load_metadata_files(sb, i);
		if (ret) {
			printk(KERN_ERR "UDF-fs: error loading MetaData "
			"partition map %d\n", i);
			goto out_bh;
		}
	} else {
		ret = udf_load_vat(sb, i, type1_idx);
		if (ret)
			goto out_bh;
		/*
		 * Mark filesystem read-only if we have a partition with
		 * virtual map since we don't handle writing to it (we
		 * overwrite blocks instead of relocating them).
		 */
		sb->s_flags |= MS_RDONLY;
		printk(KERN_NOTICE "UDF-fs: Filesystem marked read-only "
			"because writing to pseudooverwrite partition is "
			"not implemented.\n");
	}
out_bh:
	/* In case loading failed, we handle cleanup in udf_fill_super */
	brelse(bh);
	return ret;
}

static int udf_load_logicalvol(struct super_block *sb, sector_t block,
			       struct kernel_lb_addr *fileset)
{
	struct logicalVolDesc *lvd;
	int i, j, offset;
	uint8_t type;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct genericPartitionMap *gpm;
	uint16_t ident;
	struct buffer_head *bh;
	int ret = 0;

	bh = udf_read_tagged(sb, block, block, &ident);
	if (!bh)
		return 1;
	BUG_ON(ident != TAG_IDENT_LVD);
	lvd = (struct logicalVolDesc *)bh->b_data;

	ret = udf_sb_alloc_partition_maps(sb, le32_to_cpu(lvd->numPartitionMaps));
	if (ret)
		goto out_bh;

	for (i = 0, offset = 0;
	     i < sbi->s_partitions && offset < le32_to_cpu(lvd->mapTableLength);
	     i++, offset += gpm->partitionMapLength) {
		struct udf_part_map *map = &sbi->s_partmaps[i];
		gpm = (struct genericPartitionMap *)
				&(lvd->partitionMaps[offset]);
		type = gpm->partitionMapType;
		if (type == 1) {
			struct genericPartitionMap1 *gpm1 =
				(struct genericPartitionMap1 *)gpm;
			map->s_partition_type = UDF_TYPE1_MAP15;
			map->s_volumeseqnum = le16_to_cpu(gpm1->volSeqNum);
			map->s_partition_num = le16_to_cpu(gpm1->partitionNum);
			map->s_partition_func = NULL;
		} else if (type == 2) {
			struct udfPartitionMap2 *upm2 =
						(struct udfPartitionMap2 *)gpm;
			if (!strncmp(upm2->partIdent.ident, UDF_ID_VIRTUAL,
						strlen(UDF_ID_VIRTUAL))) {
				u16 suf =
					le16_to_cpu(((__le16 *)upm2->partIdent.
							identSuffix)[0]);
				if (suf < 0x0200) {
					map->s_partition_type =
							UDF_VIRTUAL_MAP15;
					map->s_partition_func =
							udf_get_pblock_virt15;
				} else {
					map->s_partition_type =
							UDF_VIRTUAL_MAP20;
					map->s_partition_func =
							udf_get_pblock_virt20;
				}
			} else if (!strncmp(upm2->partIdent.ident,
						UDF_ID_SPARABLE,
						strlen(UDF_ID_SPARABLE))) {
				uint32_t loc;
				struct sparingTable *st;
				struct sparablePartitionMap *spm =
					(struct sparablePartitionMap *)gpm;

				map->s_partition_type = UDF_SPARABLE_MAP15;
				map->s_type_specific.s_sparing.s_packet_len =
						le16_to_cpu(spm->packetLength);
				for (j = 0; j < spm->numSparingTables; j++) {
					struct buffer_head *bh2;

					loc = le32_to_cpu(
						spm->locSparingTable[j]);
					bh2 = udf_read_tagged(sb, loc, loc,
							     &ident);
					map->s_type_specific.s_sparing.
							s_spar_map[j] = bh2;

					if (bh2 == NULL)
						continue;

					st = (struct sparingTable *)bh2->b_data;
					if (ident != 0 || strncmp(
						st->sparingIdent.ident,
						UDF_ID_SPARING,
						strlen(UDF_ID_SPARING))) {
						brelse(bh2);
						map->s_type_specific.s_sparing.
							s_spar_map[j] = NULL;
					}
				}
				map->s_partition_func = udf_get_pblock_spar15;
			} else if (!strncmp(upm2->partIdent.ident,
						UDF_ID_METADATA,
						strlen(UDF_ID_METADATA))) {
				struct udf_meta_data *mdata =
					&map->s_type_specific.s_metadata;
				struct metadataPartitionMap *mdm =
						(struct metadataPartitionMap *)
						&(lvd->partitionMaps[offset]);
				udf_debug("Parsing Logical vol part %d "
					"type %d  id=%s\n", i, type,
					UDF_ID_METADATA);

				map->s_partition_type = UDF_METADATA_MAP25;
				map->s_partition_func = udf_get_pblock_meta25;

				mdata->s_meta_file_loc   =
					le32_to_cpu(mdm->metadataFileLoc);
				mdata->s_mirror_file_loc =
					le32_to_cpu(mdm->metadataMirrorFileLoc);
				mdata->s_bitmap_file_loc =
					le32_to_cpu(mdm->metadataBitmapFileLoc);
				mdata->s_alloc_unit_size =
					le32_to_cpu(mdm->allocUnitSize);
				mdata->s_align_unit_size =
					le16_to_cpu(mdm->alignUnitSize);
				mdata->s_dup_md_flag 	 =
					mdm->flags & 0x01;

				udf_debug("Metadata Ident suffix=0x%x\n",
					(le16_to_cpu(
					 ((__le16 *)
					      mdm->partIdent.identSuffix)[0])));
				udf_debug("Metadata part num=%d\n",
					le16_to_cpu(mdm->partitionNum));
				udf_debug("Metadata part alloc unit size=%d\n",
					le32_to_cpu(mdm->allocUnitSize));
				udf_debug("Metadata file loc=%d\n",
					le32_to_cpu(mdm->metadataFileLoc));
				udf_debug("Mirror file loc=%d\n",
				       le32_to_cpu(mdm->metadataMirrorFileLoc));
				udf_debug("Bitmap file loc=%d\n",
				       le32_to_cpu(mdm->metadataBitmapFileLoc));
				udf_debug("Duplicate Flag: %d %d\n",
					mdata->s_dup_md_flag, mdm->flags);
			} else {
				udf_debug("Unknown ident: %s\n",
					  upm2->partIdent.ident);
				continue;
			}
			map->s_volumeseqnum = le16_to_cpu(upm2->volSeqNum);
			map->s_partition_num = le16_to_cpu(upm2->partitionNum);
		}
		udf_debug("Partition (%d:%d) type %d on volume %d\n",
			  i, map->s_partition_num, type,
			  map->s_volumeseqnum);
	}

	if (fileset) {
		struct long_ad *la = (struct long_ad *)&(lvd->logicalVolContentsUse[0]);

		*fileset = lelb_to_cpu(la->extLocation);
		udf_debug("FileSet found in LogicalVolDesc at block=%d, "
			  "partition=%d\n", fileset->logicalBlockNum,
			  fileset->partitionReferenceNum);
	}
	if (lvd->integritySeqExt.extLength)
		udf_load_logicalvolint(sb, leea_to_cpu(lvd->integritySeqExt));

out_bh:
	brelse(bh);
	return ret;
}

/*
 * udf_load_logicalvolint
 *
 */
static void udf_load_logicalvolint(struct super_block *sb, struct kernel_extent_ad loc)
{
	struct buffer_head *bh = NULL;
	uint16_t ident;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct logicalVolIntegrityDesc *lvid;

	while (loc.extLength > 0 &&
	       (bh = udf_read_tagged(sb, loc.extLocation,
				     loc.extLocation, &ident)) &&
	       ident == TAG_IDENT_LVID) {
		sbi->s_lvid_bh = bh;
		lvid = (struct logicalVolIntegrityDesc *)bh->b_data;

		if (lvid->nextIntegrityExt.extLength)
			udf_load_logicalvolint(sb,
				leea_to_cpu(lvid->nextIntegrityExt));

		if (sbi->s_lvid_bh != bh)
			brelse(bh);
		loc.extLength -= sb->s_blocksize;
		loc.extLocation++;
	}
	if (sbi->s_lvid_bh != bh)
		brelse(bh);
}

/*
 * udf_process_sequence
 *
 * PURPOSE
 *	Process a main/reserve volume descriptor sequence.
 *
 * PRE-CONDITIONS
 *	sb			Pointer to _locked_ superblock.
 *	block			First block of first extent of the sequence.
 *	lastblock		Lastblock of first extent of the sequence.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static noinline int udf_process_sequence(struct super_block *sb, long block,
				long lastblock, struct kernel_lb_addr *fileset)
{
	struct buffer_head *bh = NULL;
	struct udf_vds_record vds[VDS_POS_LENGTH];
	struct udf_vds_record *curr;
	struct generic_desc *gd;
	struct volDescPtr *vdp;
	int done = 0;
	uint32_t vdsn;
	uint16_t ident;
	long next_s = 0, next_e = 0;

	memset(vds, 0, sizeof(struct udf_vds_record) * VDS_POS_LENGTH);

	/*
	 * Read the main descriptor sequence and find which descriptors
	 * are in it.
	 */
	for (; (!done && block <= lastblock); block++) {

		bh = udf_read_tagged(sb, block, block, &ident);
		if (!bh) {
			printk(KERN_ERR "udf: Block %Lu of volume descriptor "
			       "sequence is corrupted or we could not read "
			       "it.\n", (unsigned long long)block);
			return 1;
		}

		/* Process each descriptor (ISO 13346 3/8.3-8.4) */
		gd = (struct generic_desc *)bh->b_data;
		vdsn = le32_to_cpu(gd->volDescSeqNum);
		switch (ident) {
		case TAG_IDENT_PVD: /* ISO 13346 3/10.1 */
			curr = &vds[VDS_POS_PRIMARY_VOL_DESC];
			if (vdsn >= curr->volDescSeqNum) {
				curr->volDescSeqNum = vdsn;
				curr->block = block;
			}
			break;
		case TAG_IDENT_VDP: /* ISO 13346 3/10.3 */
			curr = &vds[VDS_POS_VOL_DESC_PTR];
			if (vdsn >= curr->volDescSeqNum) {
				curr->volDescSeqNum = vdsn;
				curr->block = block;

				vdp = (struct volDescPtr *)bh->b_data;
				next_s = le32_to_cpu(
					vdp->nextVolDescSeqExt.extLocation);
				next_e = le32_to_cpu(
					vdp->nextVolDescSeqExt.extLength);
				next_e = next_e >> sb->s_blocksize_bits;
				next_e += next_s;
			}
			break;
		case TAG_IDENT_IUVD: /* ISO 13346 3/10.4 */
			curr = &vds[VDS_POS_IMP_USE_VOL_DESC];
			if (vdsn >= curr->volDescSeqNum) {
				curr->volDescSeqNum = vdsn;
				curr->block = block;
			}
			break;
		case TAG_IDENT_PD: /* ISO 13346 3/10.5 */
			curr = &vds[VDS_POS_PARTITION_DESC];
			if (!curr->block)
				curr->block = block;
			break;
		case TAG_IDENT_LVD: /* ISO 13346 3/10.6 */
			curr = &vds[VDS_POS_LOGICAL_VOL_DESC];
			if (vdsn >= curr->volDescSeqNum) {
				curr->volDescSeqNum = vdsn;
				curr->block = block;
			}
			break;
		case TAG_IDENT_USD: /* ISO 13346 3/10.8 */
			curr = &vds[VDS_POS_UNALLOC_SPACE_DESC];
			if (vdsn >= curr->volDescSeqNum) {
				curr->volDescSeqNum = vdsn;
				curr->block = block;
			}
			break;
		case TAG_IDENT_TD: /* ISO 13346 3/10.9 */
			vds[VDS_POS_TERMINATING_DESC].block = block;
			if (next_e) {
				block = next_s;
				lastblock = next_e;
				next_s = next_e = 0;
			} else
				done = 1;
			break;
		}
		brelse(bh);
	}
	/*
	 * Now read interesting descriptors again and process them
	 * in a suitable order
	 */
	if (!vds[VDS_POS_PRIMARY_VOL_DESC].block) {
		printk(KERN_ERR "udf: Primary Volume Descriptor not found!\n");
		return 1;
	}
	if (udf_load_pvoldesc(sb, vds[VDS_POS_PRIMARY_VOL_DESC].block))
		return 1;

	if (vds[VDS_POS_LOGICAL_VOL_DESC].block && udf_load_logicalvol(sb,
	    vds[VDS_POS_LOGICAL_VOL_DESC].block, fileset))
		return 1;

	if (vds[VDS_POS_PARTITION_DESC].block) {
		/*
		 * We rescan the whole descriptor sequence to find
		 * partition descriptor blocks and process them.
		 */
		for (block = vds[VDS_POS_PARTITION_DESC].block;
		     block < vds[VDS_POS_TERMINATING_DESC].block;
		     block++)
			if (udf_load_partdesc(sb, block))
				return 1;
	}

	return 0;
}

static int udf_load_sequence(struct super_block *sb, struct buffer_head *bh,
			     struct kernel_lb_addr *fileset)
{
	struct anchorVolDescPtr *anchor;
	long main_s, main_e, reserve_s, reserve_e;

	anchor = (struct anchorVolDescPtr *)bh->b_data;

	/* Locate the main sequence */
	main_s = le32_to_cpu(anchor->mainVolDescSeqExt.extLocation);
	main_e = le32_to_cpu(anchor->mainVolDescSeqExt.extLength);
	main_e = main_e >> sb->s_blocksize_bits;
	main_e += main_s;

	/* Locate the reserve sequence */
	reserve_s = le32_to_cpu(anchor->reserveVolDescSeqExt.extLocation);
	reserve_e = le32_to_cpu(anchor->reserveVolDescSeqExt.extLength);
	reserve_e = reserve_e >> sb->s_blocksize_bits;
	reserve_e += reserve_s;

	/* Process the main & reserve sequences */
	/* responsible for finding the PartitionDesc(s) */
	if (!udf_process_sequence(sb, main_s, main_e, fileset))
		return 1;
	return !udf_process_sequence(sb, reserve_s, reserve_e, fileset);
}

/*
 * Check whether there is an anchor block in the given block and
 * load Volume Descriptor Sequence if so.
 */
static int udf_check_anchor_block(struct super_block *sb, sector_t block,
				  struct kernel_lb_addr *fileset)
{
	struct buffer_head *bh;
	uint16_t ident;
	int ret;

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_VARCONV) &&
	    udf_fixed_to_variable(block) >=
	    sb->s_bdev->bd_inode->i_size >> sb->s_blocksize_bits)
		return 0;

	bh = udf_read_tagged(sb, block, block, &ident);
	if (!bh)
		return 0;
	if (ident != TAG_IDENT_AVDP) {
		brelse(bh);
		return 0;
	}
	ret = udf_load_sequence(sb, bh, fileset);
	brelse(bh);
	return ret;
}

/* Search for an anchor volume descriptor pointer */
static sector_t udf_scan_anchors(struct super_block *sb, sector_t lastblock,
				 struct kernel_lb_addr *fileset)
{
	sector_t last[6];
	int i;
	struct udf_sb_info *sbi = UDF_SB(sb);
	int last_count = 0;

	/* First try user provided anchor */
	if (sbi->s_anchor) {
		if (udf_check_anchor_block(sb, sbi->s_anchor, fileset))
			return lastblock;
	}
	/*
	 * according to spec, anchor is in either:
	 *     block 256
	 *     lastblock-256
	 *     lastblock
	 *  however, if the disc isn't closed, it could be 512.
	 */
	if (udf_check_anchor_block(sb, sbi->s_session + 256, fileset))
		return lastblock;
	/*
	 * The trouble is which block is the last one. Drives often misreport
	 * this so we try various possibilities.
	 */
	last[last_count++] = lastblock;
	if (lastblock >= 1)
		last[last_count++] = lastblock - 1;
	last[last_count++] = lastblock + 1;
	if (lastblock >= 2)
		last[last_count++] = lastblock - 2;
	if (lastblock >= 150)
		last[last_count++] = lastblock - 150;
	if (lastblock >= 152)
		last[last_count++] = lastblock - 152;

	for (i = 0; i < last_count; i++) {
		if (last[i] >= sb->s_bdev->bd_inode->i_size >>
				sb->s_blocksize_bits)
			continue;
		if (udf_check_anchor_block(sb, last[i], fileset))
			return last[i];
		if (last[i] < 256)
			continue;
		if (udf_check_anchor_block(sb, last[i] - 256, fileset))
			return last[i];
	}

	/* Finally try block 512 in case media is open */
	if (udf_check_anchor_block(sb, sbi->s_session + 512, fileset))
		return last[0];
	return 0;
}

/*
 * Find an anchor volume descriptor and load Volume Descriptor Sequence from
 * area specified by it. The function expects sbi->s_lastblock to be the last
 * block on the media.
 *
 * Return 1 if ok, 0 if not found.
 *
 */
static int udf_find_anchor(struct super_block *sb,
			   struct kernel_lb_addr *fileset)
{
	sector_t lastblock;
	struct udf_sb_info *sbi = UDF_SB(sb);

	lastblock = udf_scan_anchors(sb, sbi->s_last_block, fileset);
	if (lastblock)
		goto out;

	/* No anchor found? Try VARCONV conversion of block numbers */
	UDF_SET_FLAG(sb, UDF_FLAG_VARCONV);
	/* Firstly, we try to not convert number of the last block */
	lastblock = udf_scan_anchors(sb,
				udf_variable_to_fixed(sbi->s_last_block),
				fileset);
	if (lastblock)
		goto out;

	/* Secondly, we try with converted number of the last block */
	lastblock = udf_scan_anchors(sb, sbi->s_last_block, fileset);
	if (!lastblock) {
		/* VARCONV didn't help. Clear it. */
		UDF_CLEAR_FLAG(sb, UDF_FLAG_VARCONV);
		return 0;
	}
out:
	sbi->s_last_block = lastblock;
	return 1;
}

/*
 * Check Volume Structure Descriptor, find Anchor block and load Volume
 * Descriptor Sequence
 */
static int udf_load_vrs(struct super_block *sb, struct udf_options *uopt,
			int silent, struct kernel_lb_addr *fileset)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	loff_t nsr_off;

	if (!sb_set_blocksize(sb, uopt->blocksize)) {
		if (!silent)
			printk(KERN_WARNING "UDF-fs: Bad block size\n");
		return 0;
	}
	sbi->s_last_block = uopt->lastblock;
	if (!uopt->novrs) {
		/* Check that it is NSR02 compliant */
		nsr_off = udf_check_vsd(sb);
		if (!nsr_off) {
			if (!silent)
				printk(KERN_WARNING "UDF-fs: No VRS found\n");
			return 0;
		}
		if (nsr_off == -1)
			udf_debug("Failed to read byte 32768. Assuming open "
				  "disc. Skipping validity check\n");
		if (!sbi->s_last_block)
			sbi->s_last_block = udf_get_last_block(sb);
	} else {
		udf_debug("Validity check skipped because of novrs option\n");
	}

	/* Look for anchor block and load Volume Descriptor Sequence */
	sbi->s_anchor = uopt->anchor;
	if (!udf_find_anchor(sb, fileset)) {
		if (!silent)
			printk(KERN_WARNING "UDF-fs: No anchor found\n");
		return 0;
	}
	return 1;
}

static void udf_open_lvid(struct super_block *sb)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct buffer_head *bh = sbi->s_lvid_bh;
	struct logicalVolIntegrityDesc *lvid;
	struct logicalVolIntegrityDescImpUse *lvidiu;

	if (!bh)
		return;

	mutex_lock(&sbi->s_alloc_mutex);
	lvid = (struct logicalVolIntegrityDesc *)bh->b_data;
	lvidiu = udf_sb_lvidiu(sbi);

	lvidiu->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
	lvidiu->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
	udf_time_to_disk_stamp(&lvid->recordingDateAndTime,
				CURRENT_TIME);
	lvid->integrityType = cpu_to_le32(LVID_INTEGRITY_TYPE_OPEN);

	lvid->descTag.descCRC = cpu_to_le16(
		crc_itu_t(0, (char *)lvid + sizeof(struct tag),
			le16_to_cpu(lvid->descTag.descCRCLength)));

	lvid->descTag.tagChecksum = udf_tag_checksum(&lvid->descTag);
	mark_buffer_dirty(bh);
	sbi->s_lvid_dirty = 0;
	mutex_unlock(&sbi->s_alloc_mutex);
}

static void udf_close_lvid(struct super_block *sb)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct buffer_head *bh = sbi->s_lvid_bh;
	struct logicalVolIntegrityDesc *lvid;
	struct logicalVolIntegrityDescImpUse *lvidiu;

	if (!bh)
		return;

	mutex_lock(&sbi->s_alloc_mutex);
	lvid = (struct logicalVolIntegrityDesc *)bh->b_data;
	lvidiu = udf_sb_lvidiu(sbi);
	lvidiu->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
	lvidiu->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
	udf_time_to_disk_stamp(&lvid->recordingDateAndTime, CURRENT_TIME);
	if (UDF_MAX_WRITE_VERSION > le16_to_cpu(lvidiu->maxUDFWriteRev))
		lvidiu->maxUDFWriteRev = cpu_to_le16(UDF_MAX_WRITE_VERSION);
	if (sbi->s_udfrev > le16_to_cpu(lvidiu->minUDFReadRev))
		lvidiu->minUDFReadRev = cpu_to_le16(sbi->s_udfrev);
	if (sbi->s_udfrev > le16_to_cpu(lvidiu->minUDFWriteRev))
		lvidiu->minUDFWriteRev = cpu_to_le16(sbi->s_udfrev);
	lvid->integrityType = cpu_to_le32(LVID_INTEGRITY_TYPE_CLOSE);

	lvid->descTag.descCRC = cpu_to_le16(
			crc_itu_t(0, (char *)lvid + sizeof(struct tag),
				le16_to_cpu(lvid->descTag.descCRCLength)));

	lvid->descTag.tagChecksum = udf_tag_checksum(&lvid->descTag);
	/*
	 * We set buffer uptodate unconditionally here to avoid spurious
	 * warnings from mark_buffer_dirty() when previous EIO has marked
	 * the buffer as !uptodate
	 */
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	sbi->s_lvid_dirty = 0;
	mutex_unlock(&sbi->s_alloc_mutex);
}

u64 lvid_get_unique_id(struct super_block *sb)
{
	struct buffer_head *bh;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct logicalVolIntegrityDesc *lvid;
	struct logicalVolHeaderDesc *lvhd;
	u64 uniqueID;
	u64 ret;

	bh = sbi->s_lvid_bh;
	if (!bh)
		return 0;

	lvid = (struct logicalVolIntegrityDesc *)bh->b_data;
	lvhd = (struct logicalVolHeaderDesc *)lvid->logicalVolContentsUse;

	mutex_lock(&sbi->s_alloc_mutex);
	ret = uniqueID = le64_to_cpu(lvhd->uniqueID);
	if (!(++uniqueID & 0xFFFFFFFF))
		uniqueID += 16;
	lvhd->uniqueID = cpu_to_le64(uniqueID);
	mutex_unlock(&sbi->s_alloc_mutex);
	mark_buffer_dirty(bh);

	return ret;
}

static void udf_sb_free_bitmap(struct udf_bitmap *bitmap)
{
	int i;
	int nr_groups = bitmap->s_nr_groups;
	int size = sizeof(struct udf_bitmap) + (sizeof(struct buffer_head *) *
						nr_groups);

	for (i = 0; i < nr_groups; i++)
		if (bitmap->s_block_bitmap[i])
			brelse(bitmap->s_block_bitmap[i]);

	if (size <= PAGE_SIZE)
		kfree(bitmap);
	else
		vfree(bitmap);
}

static void udf_free_partition(struct udf_part_map *map)
{
	int i;
	struct udf_meta_data *mdata;

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE)
		iput(map->s_uspace.s_table);
	if (map->s_partition_flags & UDF_PART_FLAG_FREED_TABLE)
		iput(map->s_fspace.s_table);
	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP)
		udf_sb_free_bitmap(map->s_uspace.s_bitmap);
	if (map->s_partition_flags & UDF_PART_FLAG_FREED_BITMAP)
		udf_sb_free_bitmap(map->s_fspace.s_bitmap);
	if (map->s_partition_type == UDF_SPARABLE_MAP15)
		for (i = 0; i < 4; i++)
			brelse(map->s_type_specific.s_sparing.s_spar_map[i]);
	else if (map->s_partition_type == UDF_METADATA_MAP25) {
		mdata = &map->s_type_specific.s_metadata;
		iput(mdata->s_metadata_fe);
		mdata->s_metadata_fe = NULL;

		iput(mdata->s_mirror_fe);
		mdata->s_mirror_fe = NULL;

		iput(mdata->s_bitmap_fe);
		mdata->s_bitmap_fe = NULL;
	}
}

static int udf_fill_super(struct super_block *sb, void *options, int silent)
{
	int i;
	int ret;
	struct inode *inode = NULL;
	struct udf_options uopt;
	struct kernel_lb_addr rootdir, fileset;
	struct udf_sb_info *sbi;

	uopt.flags = (1 << UDF_FLAG_USE_AD_IN_ICB) | (1 << UDF_FLAG_STRICT);
	uopt.uid = -1;
	uopt.gid = -1;
	uopt.umask = 0;
	uopt.fmode = UDF_INVALID_MODE;
	uopt.dmode = UDF_INVALID_MODE;

	sbi = kzalloc(sizeof(struct udf_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;

	mutex_init(&sbi->s_alloc_mutex);

	if (!udf_parse_options((char *)options, &uopt, false))
		goto error_out;

	if (uopt.flags & (1 << UDF_FLAG_UTF8) &&
	    uopt.flags & (1 << UDF_FLAG_NLS_MAP)) {
		udf_error(sb, "udf_read_super",
			  "utf8 cannot be combined with iocharset\n");
		goto error_out;
	}
#ifdef CONFIG_UDF_NLS
	if ((uopt.flags & (1 << UDF_FLAG_NLS_MAP)) && !uopt.nls_map) {
		uopt.nls_map = load_nls_default();
		if (!uopt.nls_map)
			uopt.flags &= ~(1 << UDF_FLAG_NLS_MAP);
		else
			udf_debug("Using default NLS map\n");
	}
#endif
	if (!(uopt.flags & (1 << UDF_FLAG_NLS_MAP)))
		uopt.flags |= (1 << UDF_FLAG_UTF8);

	fileset.logicalBlockNum = 0xFFFFFFFF;
	fileset.partitionReferenceNum = 0xFFFF;

	sbi->s_flags = uopt.flags;
	sbi->s_uid = uopt.uid;
	sbi->s_gid = uopt.gid;
	sbi->s_umask = uopt.umask;
	sbi->s_fmode = uopt.fmode;
	sbi->s_dmode = uopt.dmode;
	sbi->s_nls_map = uopt.nls_map;
	rwlock_init(&sbi->s_cred_lock);

	if (uopt.session == 0xFFFFFFFF)
		sbi->s_session = udf_get_last_session(sb);
	else
		sbi->s_session = uopt.session;

	udf_debug("Multi-session=%d\n", sbi->s_session);

	/* Fill in the rest of the superblock */
	sb->s_op = &udf_sb_ops;
	sb->s_export_op = &udf_export_ops;

	sb->s_dirt = 0;
	sb->s_magic = UDF_SUPER_MAGIC;
	sb->s_time_gran = 1000;

	if (uopt.flags & (1 << UDF_FLAG_BLOCKSIZE_SET)) {
		ret = udf_load_vrs(sb, &uopt, silent, &fileset);
	} else {
		uopt.blocksize = bdev_logical_block_size(sb->s_bdev);
		ret = udf_load_vrs(sb, &uopt, silent, &fileset);
		if (!ret && uopt.blocksize != UDF_DEFAULT_BLOCKSIZE) {
			if (!silent)
				printk(KERN_NOTICE
				       "UDF-fs: Rescanning with blocksize "
				       "%d\n", UDF_DEFAULT_BLOCKSIZE);
			uopt.blocksize = UDF_DEFAULT_BLOCKSIZE;
			ret = udf_load_vrs(sb, &uopt, silent, &fileset);
		}
	}
	if (!ret) {
		printk(KERN_WARNING "UDF-fs: No partition found (1)\n");
		goto error_out;
	}

	udf_debug("Lastblock=%d\n", sbi->s_last_block);

	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDescImpUse *lvidiu =
							udf_sb_lvidiu(sbi);
		uint16_t minUDFReadRev = le16_to_cpu(lvidiu->minUDFReadRev);
		uint16_t minUDFWriteRev = le16_to_cpu(lvidiu->minUDFWriteRev);
		/* uint16_t maxUDFWriteRev =
				le16_to_cpu(lvidiu->maxUDFWriteRev); */

		if (minUDFReadRev > UDF_MAX_READ_VERSION) {
			printk(KERN_ERR "UDF-fs: minUDFReadRev=%x "
					"(max is %x)\n",
			       le16_to_cpu(lvidiu->minUDFReadRev),
			       UDF_MAX_READ_VERSION);
			goto error_out;
		} else if (minUDFWriteRev > UDF_MAX_WRITE_VERSION)
			sb->s_flags |= MS_RDONLY;

		sbi->s_udfrev = minUDFWriteRev;

		if (minUDFReadRev >= UDF_VERS_USE_EXTENDED_FE)
			UDF_SET_FLAG(sb, UDF_FLAG_USE_EXTENDED_FE);
		if (minUDFReadRev >= UDF_VERS_USE_STREAMS)
			UDF_SET_FLAG(sb, UDF_FLAG_USE_STREAMS);
	}

	if (!sbi->s_partitions) {
		printk(KERN_WARNING "UDF-fs: No partition found (2)\n");
		goto error_out;
	}

	if (sbi->s_partmaps[sbi->s_partition].s_partition_flags &
			UDF_PART_FLAG_READ_ONLY) {
		printk(KERN_NOTICE "UDF-fs: Partition marked readonly; "
				   "forcing readonly mount\n");
		sb->s_flags |= MS_RDONLY;
	}

	if (udf_find_fileset(sb, &fileset, &rootdir)) {
		printk(KERN_WARNING "UDF-fs: No fileset found\n");
		goto error_out;
	}

	if (!silent) {
		struct timestamp ts;
		udf_time_to_disk_stamp(&ts, sbi->s_record_time);
		udf_info("UDF: Mounting volume '%s', "
			 "timestamp %04u/%02u/%02u %02u:%02u (%x)\n",
			 sbi->s_volume_ident, le16_to_cpu(ts.year), ts.month, ts.day,
			 ts.hour, ts.minute, le16_to_cpu(ts.typeAndTimezone));
	}
	if (!(sb->s_flags & MS_RDONLY))
		udf_open_lvid(sb);

	/* Assign the root inode */
	/* assign inodes by physical block number */
	/* perhaps it's not extensible enough, but for now ... */
	inode = udf_iget(sb, &rootdir);
	if (!inode) {
		printk(KERN_ERR "UDF-fs: Error in udf_iget, block=%d, "
				"partition=%d\n",
		       rootdir.logicalBlockNum, rootdir.partitionReferenceNum);
		goto error_out;
	}

	/* Allocate a dentry for the root inode */
	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		printk(KERN_ERR "UDF-fs: Couldn't allocate root dentry\n");
		iput(inode);
		goto error_out;
	}
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	return 0;

error_out:
	if (sbi->s_vat_inode)
		iput(sbi->s_vat_inode);
	if (sbi->s_partitions)
		for (i = 0; i < sbi->s_partitions; i++)
			udf_free_partition(&sbi->s_partmaps[i]);
#ifdef CONFIG_UDF_NLS
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP))
		unload_nls(sbi->s_nls_map);
#endif
	if (!(sb->s_flags & MS_RDONLY))
		udf_close_lvid(sb);
	brelse(sbi->s_lvid_bh);

	kfree(sbi->s_partmaps);
	kfree(sbi);
	sb->s_fs_info = NULL;

	return -EINVAL;
}

static void udf_error(struct super_block *sb, const char *function,
		      const char *fmt, ...)
{
	va_list args;

	if (!(sb->s_flags & MS_RDONLY)) {
		/* mark sb error */
		sb->s_dirt = 1;
	}
	va_start(args, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, args);
	va_end(args);
	printk(KERN_CRIT "UDF-fs error (device %s): %s: %s\n",
		sb->s_id, function, error_buf);
}

void udf_warning(struct super_block *sb, const char *function,
		 const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, args);
	va_end(args);
	printk(KERN_WARNING "UDF-fs warning (device %s): %s: %s\n",
	       sb->s_id, function, error_buf);
}

static void udf_put_super(struct super_block *sb)
{
	int i;
	struct udf_sb_info *sbi;

	sbi = UDF_SB(sb);

	if (sbi->s_vat_inode)
		iput(sbi->s_vat_inode);
	if (sbi->s_partitions)
		for (i = 0; i < sbi->s_partitions; i++)
			udf_free_partition(&sbi->s_partmaps[i]);
#ifdef CONFIG_UDF_NLS
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP))
		unload_nls(sbi->s_nls_map);
#endif
	if (!(sb->s_flags & MS_RDONLY))
		udf_close_lvid(sb);
	brelse(sbi->s_lvid_bh);
	kfree(sbi->s_partmaps);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

static int udf_sync_fs(struct super_block *sb, int wait)
{
	struct udf_sb_info *sbi = UDF_SB(sb);

	mutex_lock(&sbi->s_alloc_mutex);
	if (sbi->s_lvid_dirty) {
		/*
		 * Blockdevice will be synced later so we don't have to submit
		 * the buffer for IO
		 */
		mark_buffer_dirty(sbi->s_lvid_bh);
		sb->s_dirt = 0;
		sbi->s_lvid_dirty = 0;
	}
	mutex_unlock(&sbi->s_alloc_mutex);

	return 0;
}

static int udf_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct logicalVolIntegrityDescImpUse *lvidiu;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	if (sbi->s_lvid_bh != NULL)
		lvidiu = udf_sb_lvidiu(sbi);
	else
		lvidiu = NULL;

	buf->f_type = UDF_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->s_partmaps[sbi->s_partition].s_partition_len;
	buf->f_bfree = udf_count_free(sb);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = (lvidiu != NULL ? (le32_to_cpu(lvidiu->numFiles) +
					  le32_to_cpu(lvidiu->numDirs)) : 0)
			+ buf->f_bfree;
	buf->f_ffree = buf->f_bfree;
	buf->f_namelen = UDF_NAME_LEN - 2;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static unsigned int udf_count_free_bitmap(struct super_block *sb,
					  struct udf_bitmap *bitmap)
{
	struct buffer_head *bh = NULL;
	unsigned int accum = 0;
	int index;
	int block = 0, newblock;
	struct kernel_lb_addr loc;
	uint32_t bytes;
	uint8_t *ptr;
	uint16_t ident;
	struct spaceBitmapDesc *bm;

	loc.logicalBlockNum = bitmap->s_extPosition;
	loc.partitionReferenceNum = UDF_SB(sb)->s_partition;
	bh = udf_read_ptagged(sb, &loc, 0, &ident);

	if (!bh) {
		printk(KERN_ERR "udf: udf_count_free failed\n");
		goto out;
	} else if (ident != TAG_IDENT_SBD) {
		brelse(bh);
		printk(KERN_ERR "udf: udf_count_free failed\n");
		goto out;
	}

	bm = (struct spaceBitmapDesc *)bh->b_data;
	bytes = le32_to_cpu(bm->numOfBytes);
	index = sizeof(struct spaceBitmapDesc); /* offset in first block only */
	ptr = (uint8_t *)bh->b_data;

	while (bytes > 0) {
		u32 cur_bytes = min_t(u32, bytes, sb->s_blocksize - index);
		accum += bitmap_weight((const unsigned long *)(ptr + index),
					cur_bytes * 8);
		bytes -= cur_bytes;
		if (bytes) {
			brelse(bh);
			newblock = udf_get_lb_pblock(sb, &loc, ++block);
			bh = udf_tread(sb, newblock);
			if (!bh) {
				udf_debug("read failed\n");
				goto out;
			}
			index = 0;
			ptr = (uint8_t *)bh->b_data;
		}
	}
	brelse(bh);
out:
	return accum;
}

static unsigned int udf_count_free_table(struct super_block *sb,
					 struct inode *table)
{
	unsigned int accum = 0;
	uint32_t elen;
	struct kernel_lb_addr eloc;
	int8_t etype;
	struct extent_position epos;

	mutex_lock(&UDF_SB(sb)->s_alloc_mutex);
	epos.block = UDF_I(table)->i_location;
	epos.offset = sizeof(struct unallocSpaceEntry);
	epos.bh = NULL;

	while ((etype = udf_next_aext(table, &epos, &eloc, &elen, 1)) != -1)
		accum += (elen >> table->i_sb->s_blocksize_bits);

	brelse(epos.bh);
	mutex_unlock(&UDF_SB(sb)->s_alloc_mutex);

	return accum;
}

static unsigned int udf_count_free(struct super_block *sb)
{
	unsigned int accum = 0;
	struct udf_sb_info *sbi;
	struct udf_part_map *map;

	sbi = UDF_SB(sb);
	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid =
			(struct logicalVolIntegrityDesc *)
			sbi->s_lvid_bh->b_data;
		if (le32_to_cpu(lvid->numOfPartitions) > sbi->s_partition) {
			accum = le32_to_cpu(
					lvid->freeSpaceTable[sbi->s_partition]);
			if (accum == 0xFFFFFFFF)
				accum = 0;
		}
	}

	if (accum)
		return accum;

	map = &sbi->s_partmaps[sbi->s_partition];
	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_BITMAP) {
		accum += udf_count_free_bitmap(sb,
					       map->s_uspace.s_bitmap);
	}
	if (map->s_partition_flags & UDF_PART_FLAG_FREED_BITMAP) {
		accum += udf_count_free_bitmap(sb,
					       map->s_fspace.s_bitmap);
	}
	if (accum)
		return accum;

	if (map->s_partition_flags & UDF_PART_FLAG_UNALLOC_TABLE) {
		accum += udf_count_free_table(sb,
					      map->s_uspace.s_table);
	}
	if (map->s_partition_flags & UDF_PART_FLAG_FREED_TABLE) {
		accum += udf_count_free_table(sb,
					      map->s_fspace.s_table);
	}

	return accum;
}
