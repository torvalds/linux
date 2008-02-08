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
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#include <linux/udf_fs.h>
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

static char error_buf[1024];

/* These are the "meat" - everything else is stuffing */
static int udf_fill_super(struct super_block *, void *, int);
static void udf_put_super(struct super_block *);
static void udf_write_super(struct super_block *);
static int udf_remount_fs(struct super_block *, int *, char *);
static int udf_check_valid(struct super_block *, int, int);
static int udf_vrs(struct super_block *sb, int silent);
static int udf_load_partition(struct super_block *, kernel_lb_addr *);
static int udf_load_logicalvol(struct super_block *, struct buffer_head *,
			       kernel_lb_addr *);
static void udf_load_logicalvolint(struct super_block *, kernel_extent_ad);
static void udf_find_anchor(struct super_block *);
static int udf_find_fileset(struct super_block *, kernel_lb_addr *,
			    kernel_lb_addr *);
static void udf_load_pvoldesc(struct super_block *, struct buffer_head *);
static void udf_load_fileset(struct super_block *, struct buffer_head *,
			     kernel_lb_addr *);
static int udf_load_partdesc(struct super_block *, struct buffer_head *);
static void udf_open_lvid(struct super_block *);
static void udf_close_lvid(struct super_block *);
static unsigned int udf_count_free(struct super_block *);
static int udf_statfs(struct dentry *, struct kstatfs *);

struct logicalVolIntegrityDescImpUse *udf_sb_lvidiu(struct udf_sb_info *sbi)
{
	struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
	__u32 number_of_partitions = le32_to_cpu(lvid->numOfPartitions);
	__u32 offset = number_of_partitions * 2 * sizeof(uint32_t)/sizeof(uint8_t);
	return (struct logicalVolIntegrityDescImpUse *)&(lvid->impUse[offset]);
}

/* UDF filesystem type */
static int udf_get_sb(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data,
		      struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, udf_fill_super, mnt);
}

static struct file_system_type udf_fstype = {
	.owner		= THIS_MODULE,
	.name		= "udf",
	.get_sb		= udf_get_sb,
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

	return &ei->vfs_inode;
}

static void udf_destroy_inode(struct inode *inode)
{
	kmem_cache_free(udf_inode_cachep, UDF_I(inode));
}

static void init_once(struct kmem_cache *cachep, void *foo)
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
	.delete_inode	= udf_delete_inode,
	.clear_inode	= udf_clear_inode,
	.put_super	= udf_put_super,
	.write_super	= udf_write_super,
	.statfs		= udf_statfs,
	.remount_fs	= udf_remount_fs,
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
		udf_error(sb, __FUNCTION__,
			  "Unable to allocate space for %d partition maps",
			  count);
		sbi->s_partitions = 0;
		return -ENOMEM;
	}

	sbi->s_partitions = count;
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
	Opt_err, Opt_uforget, Opt_uignore, Opt_gforget, Opt_gignore
};

static match_table_t tokens = {
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
	{Opt_err,	NULL}
};

static int udf_parse_options(char *options, struct udf_options *uopt)
{
	char *p;
	int option;

	uopt->novrs = 0;
	uopt->blocksize = 2048;
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
		case Opt_bs:
			if (match_int(&args[0], &option))
				return 0;
			uopt->blocksize = option;
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
			break;
		case Opt_lastblock:
			if (match_int(args, &option))
				return 0;
			uopt->lastblock = option;
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
		default:
			printk(KERN_ERR "udf: bad mount option \"%s\" "
			       "or missing value\n", p);
			return 0;
		}
	}
	return 1;
}

static void udf_write_super(struct super_block *sb)
{
	lock_kernel();

	if (!(sb->s_flags & MS_RDONLY))
		udf_open_lvid(sb);
	sb->s_dirt = 0;

	unlock_kernel();
}

static int udf_remount_fs(struct super_block *sb, int *flags, char *options)
{
	struct udf_options uopt;
	struct udf_sb_info *sbi = UDF_SB(sb);

	uopt.flags = sbi->s_flags;
	uopt.uid   = sbi->s_uid;
	uopt.gid   = sbi->s_gid;
	uopt.umask = sbi->s_umask;

	if (!udf_parse_options(options, &uopt))
		return -EINVAL;

	sbi->s_flags = uopt.flags;
	sbi->s_uid   = uopt.uid;
	sbi->s_gid   = uopt.gid;
	sbi->s_umask = uopt.umask;

	if (sbi->s_lvid_bh) {
		int write_rev = le16_to_cpu(udf_sb_lvidiu(sbi)->minUDFWriteRev);
		if (write_rev > UDF_MAX_WRITE_VERSION)
			*flags |= MS_RDONLY;
	}

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (*flags & MS_RDONLY)
		udf_close_lvid(sb);
	else
		udf_open_lvid(sb);

	return 0;
}

/*
 * udf_set_blocksize
 *
 * PURPOSE
 *	Set the block size to be used in all transfers.
 *
 * DESCRIPTION
 *	To allow room for a DMA transfer, it is best to guess big when unsure.
 *	This routine picks 2048 bytes as the blocksize when guessing. This
 *	should be adequate until devices with larger block sizes become common.
 *
 *	Note that the Linux kernel can currently only deal with blocksizes of
 *	512, 1024, 2048, 4096, and 8192 bytes.
 *
 * PRE-CONDITIONS
 *	sb			Pointer to _locked_ superblock.
 *
 * POST-CONDITIONS
 *	sb->s_blocksize		Blocksize.
 *	sb->s_blocksize_bits	log2 of blocksize.
 *	<return>	0	Blocksize is valid.
 *	<return>	1	Blocksize is invalid.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static int udf_set_blocksize(struct super_block *sb, int bsize)
{
	if (!sb_min_blocksize(sb, bsize)) {
		udf_debug("Bad block size (%d)\n", bsize);
		printk(KERN_ERR "udf: bad block size (%d)\n", bsize);
		return 0;
	}

	return sb->s_blocksize;
}

static int udf_vrs(struct super_block *sb, int silent)
{
	struct volStructDesc *vsd = NULL;
	int sector = 32768;
	int sectorsize;
	struct buffer_head *bh = NULL;
	int iso9660 = 0;
	int nsr02 = 0;
	int nsr03 = 0;
	struct udf_sb_info *sbi;

	/* Block size must be a multiple of 512 */
	if (sb->s_blocksize & 511)
		return 0;
	sbi = UDF_SB(sb);

	if (sb->s_blocksize < sizeof(struct volStructDesc))
		sectorsize = sizeof(struct volStructDesc);
	else
		sectorsize = sb->s_blocksize;

	sector += (sbi->s_session << sb->s_blocksize_bits);

	udf_debug("Starting at sector %u (%ld byte sectors)\n",
		  (sector >> sb->s_blocksize_bits), sb->s_blocksize);
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
			iso9660 = sector;
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

/*
 * udf_find_anchor
 *
 * PURPOSE
 *	Find an anchor volume descriptor.
 *
 * PRE-CONDITIONS
 *	sb			Pointer to _locked_ superblock.
 *	lastblock		Last block on media.
 *
 * POST-CONDITIONS
 *	<return>		1 if not found, 0 if ok
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static void udf_find_anchor(struct super_block *sb)
{
	int lastblock;
	struct buffer_head *bh = NULL;
	uint16_t ident;
	uint32_t location;
	int i;
	struct udf_sb_info *sbi;

	sbi = UDF_SB(sb);
	lastblock = sbi->s_last_block;

	if (lastblock) {
		int varlastblock = udf_variable_to_fixed(lastblock);
		int last[] =  { lastblock, lastblock - 2,
				lastblock - 150, lastblock - 152,
				varlastblock, varlastblock - 2,
				varlastblock - 150, varlastblock - 152 };

		lastblock = 0;

		/* Search for an anchor volume descriptor pointer */

		/*  according to spec, anchor is in either:
		 *     block 256
		 *     lastblock-256
		 *     lastblock
		 *  however, if the disc isn't closed, it could be 512 */

		for (i = 0; !lastblock && i < ARRAY_SIZE(last); i++) {
			ident = location = 0;
			if (last[i] >= 0) {
				bh = sb_bread(sb, last[i]);
				if (bh) {
					tag *t = (tag *)bh->b_data;
					ident = le16_to_cpu(t->tagIdent);
					location = le32_to_cpu(t->tagLocation);
					brelse(bh);
				}
			}

			if (ident == TAG_IDENT_AVDP) {
				if (location == last[i] - sbi->s_session) {
					lastblock = last[i] - sbi->s_session;
					sbi->s_anchor[0] = lastblock;
					sbi->s_anchor[1] = lastblock - 256;
				} else if (location == udf_variable_to_fixed(last[i]) - sbi->s_session) {
					UDF_SET_FLAG(sb, UDF_FLAG_VARCONV);
					lastblock = udf_variable_to_fixed(last[i]) - sbi->s_session;
					sbi->s_anchor[0] = lastblock;
					sbi->s_anchor[1] = lastblock - 256 - sbi->s_session;
				} else {
					udf_debug("Anchor found at block %d, location mismatch %d.\n",
						  last[i], location);
				}
			} else if (ident == TAG_IDENT_FE || ident == TAG_IDENT_EFE) {
				lastblock = last[i];
				sbi->s_anchor[3] = 512;
			} else {
				ident = location = 0;
				if (last[i] >= 256) {
					bh = sb_bread(sb, last[i] - 256);
					if (bh) {
						tag *t = (tag *)bh->b_data;
						ident = le16_to_cpu(t->tagIdent);
						location = le32_to_cpu(t->tagLocation);
						brelse(bh);
					}
				}

				if (ident == TAG_IDENT_AVDP &&
				    location == last[i] - 256 - sbi->s_session) {
					lastblock = last[i];
					sbi->s_anchor[1] = last[i] - 256;
				} else {
					ident = location = 0;
					if (last[i] >= 312 + sbi->s_session) {
						bh = sb_bread(sb, last[i] - 312 - sbi->s_session);
						if (bh) {
							tag *t = (tag *)bh->b_data;
							ident = le16_to_cpu(t->tagIdent);
							location = le32_to_cpu(t->tagLocation);
							brelse(bh);
						}
					}

					if (ident == TAG_IDENT_AVDP &&
					    location == udf_variable_to_fixed(last[i]) - 256) {
						UDF_SET_FLAG(sb, UDF_FLAG_VARCONV);
						lastblock = udf_variable_to_fixed(last[i]);
						sbi->s_anchor[1] = lastblock - 256;
					}
				}
			}
		}
	}

	if (!lastblock) {
		/* We haven't found the lastblock. check 312 */
		bh = sb_bread(sb, 312 + sbi->s_session);
		if (bh) {
			tag *t = (tag *)bh->b_data;
			ident = le16_to_cpu(t->tagIdent);
			location = le32_to_cpu(t->tagLocation);
			brelse(bh);

			if (ident == TAG_IDENT_AVDP && location == 256)
				UDF_SET_FLAG(sb, UDF_FLAG_VARCONV);
		}
	}

	for (i = 0; i < ARRAY_SIZE(sbi->s_anchor); i++) {
		if (sbi->s_anchor[i]) {
			bh = udf_read_tagged(sb, sbi->s_anchor[i],
					     sbi->s_anchor[i], &ident);
			if (!bh)
				sbi->s_anchor[i] = 0;
			else {
				brelse(bh);
				if ((ident != TAG_IDENT_AVDP) &&
				    (i || (ident != TAG_IDENT_FE && ident != TAG_IDENT_EFE)))
					sbi->s_anchor[i] = 0;
			}
		}
	}

	sbi->s_last_block = lastblock;
}

static int udf_find_fileset(struct super_block *sb,
			    kernel_lb_addr *fileset,
			    kernel_lb_addr *root)
{
	struct buffer_head *bh = NULL;
	long lastblock;
	uint16_t ident;
	struct udf_sb_info *sbi;

	if (fileset->logicalBlockNum != 0xFFFFFFFF ||
	    fileset->partitionReferenceNum != 0xFFFF) {
		bh = udf_read_ptagged(sb, *fileset, 0, &ident);

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
		kernel_lb_addr newfileset;

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
				bh = udf_read_ptagged(sb, newfileset, 0,
						      &ident);
				if (!bh) {
					newfileset.logicalBlockNum++;
					continue;
				}

				switch (ident) {
				case TAG_IDENT_SBD:
				{
					struct spaceBitmapDesc *sp;
					sp = (struct spaceBitmapDesc *)bh->b_data;
					newfileset.logicalBlockNum += 1 +
						((le32_to_cpu(sp->numOfBytes) +
						  sizeof(struct spaceBitmapDesc) - 1)
						 >> sb->s_blocksize_bits);
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

static void udf_load_pvoldesc(struct super_block *sb, struct buffer_head *bh)
{
	struct primaryVolDesc *pvoldesc;
	time_t recording;
	long recording_usec;
	struct ustr instr;
	struct ustr outstr;

	pvoldesc = (struct primaryVolDesc *)bh->b_data;

	if (udf_stamp_to_time(&recording, &recording_usec,
			      lets_to_cpu(pvoldesc->recordingDateAndTime))) {
		kernel_timestamp ts;
		ts = lets_to_cpu(pvoldesc->recordingDateAndTime);
		udf_debug("recording time %ld/%ld, %04u/%02u/%02u"
			  " %02u:%02u (%x)\n",
			  recording, recording_usec,
			  ts.year, ts.month, ts.day, ts.hour,
			  ts.minute, ts.typeAndTimezone);
		UDF_SB(sb)->s_record_time.tv_sec = recording;
		UDF_SB(sb)->s_record_time.tv_nsec = recording_usec * 1000;
	}

	if (!udf_build_ustr(&instr, pvoldesc->volIdent, 32)) {
		if (udf_CS0toUTF8(&outstr, &instr)) {
			strncpy(UDF_SB(sb)->s_volume_ident, outstr.u_name,
				outstr.u_len > 31 ? 31 : outstr.u_len);
			udf_debug("volIdent[] = '%s'\n", UDF_SB(sb)->s_volume_ident);
		}
	}

	if (!udf_build_ustr(&instr, pvoldesc->volSetIdent, 128)) {
		if (udf_CS0toUTF8(&outstr, &instr))
			udf_debug("volSetIdent[] = '%s'\n", outstr.u_name);
	}
}

static void udf_load_fileset(struct super_block *sb, struct buffer_head *bh,
			     kernel_lb_addr *root)
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
	return (map->s_partition_len +
		(sizeof(struct spaceBitmapDesc) << 3) +
		(sb->s_blocksize * 8) - 1) /
		(sb->s_blocksize * 8);
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
		bitmap = kmalloc(size, GFP_KERNEL);
	else
		bitmap = vmalloc(size); /* TODO: get rid of vmalloc */

	if (bitmap == NULL) {
		udf_error(sb, __FUNCTION__,
			  "Unable to allocate space for bitmap "
			  "and %d buffer_head pointers", nr_groups);
		return NULL;
	}

	memset(bitmap, 0x00, size);
	bitmap->s_block_bitmap = (struct buffer_head **)(bitmap + 1);
	bitmap->s_nr_groups = nr_groups;
	return bitmap;
}

static int udf_load_partdesc(struct super_block *sb, struct buffer_head *bh)
{
	struct partitionDesc *p;
	int i;
	struct udf_part_map *map;
	struct udf_sb_info *sbi;

	p = (struct partitionDesc *)bh->b_data;
	sbi = UDF_SB(sb);

	for (i = 0; i < sbi->s_partitions; i++) {
		map = &sbi->s_partmaps[i];
		udf_debug("Searching map: (%d == %d)\n",
			  map->s_partition_num, le16_to_cpu(p->partitionNumber));
		if (map->s_partition_num == le16_to_cpu(p->partitionNumber)) {
			map->s_partition_len = le32_to_cpu(p->partitionLength); /* blocks */
			map->s_partition_root = le32_to_cpu(p->partitionStartingLocation);
			if (le32_to_cpu(p->accessType) == PD_ACCESS_TYPE_READ_ONLY)
				map->s_partition_flags |= UDF_PART_FLAG_READ_ONLY;
			if (le32_to_cpu(p->accessType) == PD_ACCESS_TYPE_WRITE_ONCE)
				map->s_partition_flags |= UDF_PART_FLAG_WRITE_ONCE;
			if (le32_to_cpu(p->accessType) == PD_ACCESS_TYPE_REWRITABLE)
				map->s_partition_flags |= UDF_PART_FLAG_REWRITABLE;
			if (le32_to_cpu(p->accessType) == PD_ACCESS_TYPE_OVERWRITABLE)
				map->s_partition_flags |= UDF_PART_FLAG_OVERWRITABLE;

			if (!strcmp(p->partitionContents.ident,
				    PD_PARTITION_CONTENTS_NSR02) ||
			    !strcmp(p->partitionContents.ident,
				    PD_PARTITION_CONTENTS_NSR03)) {
				struct partitionHeaderDesc *phd;

				phd = (struct partitionHeaderDesc *)(p->partitionContentsUse);
				if (phd->unallocSpaceTable.extLength) {
					kernel_lb_addr loc = {
						.logicalBlockNum = le32_to_cpu(phd->unallocSpaceTable.extPosition),
						.partitionReferenceNum = i,
					};

					map->s_uspace.s_table =
						udf_iget(sb, loc);
					if (!map->s_uspace.s_table) {
						udf_debug("cannot load unallocSpaceTable (part %d)\n", i);
						return 1;
					}
					map->s_partition_flags |= UDF_PART_FLAG_UNALLOC_TABLE;
					udf_debug("unallocSpaceTable (part %d) @ %ld\n",
						  i, map->s_uspace.s_table->i_ino);
				}
				if (phd->unallocSpaceBitmap.extLength) {
					map->s_uspace.s_bitmap = udf_sb_alloc_bitmap(sb, i);
					if (map->s_uspace.s_bitmap != NULL) {
						map->s_uspace.s_bitmap->s_extLength =
							le32_to_cpu(phd->unallocSpaceBitmap.extLength);
						map->s_uspace.s_bitmap->s_extPosition =
							le32_to_cpu(phd->unallocSpaceBitmap.extPosition);
						map->s_partition_flags |= UDF_PART_FLAG_UNALLOC_BITMAP;
						udf_debug("unallocSpaceBitmap (part %d) @ %d\n",
							  i, map->s_uspace.s_bitmap->s_extPosition);
					}
				}
				if (phd->partitionIntegrityTable.extLength)
					udf_debug("partitionIntegrityTable (part %d)\n", i);
				if (phd->freedSpaceTable.extLength) {
					kernel_lb_addr loc = {
						.logicalBlockNum = le32_to_cpu(phd->freedSpaceTable.extPosition),
						.partitionReferenceNum = i,
					};

					map->s_fspace.s_table =
						udf_iget(sb, loc);
					if (!map->s_fspace.s_table) {
						udf_debug("cannot load freedSpaceTable (part %d)\n", i);
						return 1;
					}
					map->s_partition_flags |= UDF_PART_FLAG_FREED_TABLE;
					udf_debug("freedSpaceTable (part %d) @ %ld\n",
						  i, map->s_fspace.s_table->i_ino);
				}
				if (phd->freedSpaceBitmap.extLength) {
					map->s_fspace.s_bitmap = udf_sb_alloc_bitmap(sb, i);
					if (map->s_fspace.s_bitmap != NULL) {
						map->s_fspace.s_bitmap->s_extLength =
							le32_to_cpu(phd->freedSpaceBitmap.extLength);
						map->s_fspace.s_bitmap->s_extPosition =
							le32_to_cpu(phd->freedSpaceBitmap.extPosition);
						map->s_partition_flags |= UDF_PART_FLAG_FREED_BITMAP;
						udf_debug("freedSpaceBitmap (part %d) @ %d\n",
							  i, map->s_fspace.s_bitmap->s_extPosition);
					}
				}
			}
			break;
		}
	}
	if (i == sbi->s_partitions) {
		udf_debug("Partition (%d) not found in partition map\n",
			  le16_to_cpu(p->partitionNumber));
	} else {
		udf_debug("Partition (%d:%d type %x) starts at physical %d, "
			  "block length %d\n",
			  le16_to_cpu(p->partitionNumber), i,
			  map->s_partition_type,
			  map->s_partition_root,
			  map->s_partition_len);
	}
	return 0;
}

static int udf_load_logicalvol(struct super_block *sb, struct buffer_head *bh,
			       kernel_lb_addr *fileset)
{
	struct logicalVolDesc *lvd;
	int i, j, offset;
	uint8_t type;
	struct udf_sb_info *sbi = UDF_SB(sb);

	lvd = (struct logicalVolDesc *)bh->b_data;

	i = udf_sb_alloc_partition_maps(sb, le32_to_cpu(lvd->numPartitionMaps));
	if (i != 0)
		return i;

	for (i = 0, offset = 0;
	     i < sbi->s_partitions && offset < le32_to_cpu(lvd->mapTableLength);
	     i++, offset += ((struct genericPartitionMap *)&(lvd->partitionMaps[offset]))->partitionMapLength) {
	     	struct udf_part_map *map = &sbi->s_partmaps[i];
		type = ((struct genericPartitionMap *)&(lvd->partitionMaps[offset]))->partitionMapType;
		if (type == 1) {
			struct genericPartitionMap1 *gpm1 = (struct genericPartitionMap1 *)&(lvd->partitionMaps[offset]);
			map->s_partition_type = UDF_TYPE1_MAP15;
			map->s_volumeseqnum = le16_to_cpu(gpm1->volSeqNum);
			map->s_partition_num = le16_to_cpu(gpm1->partitionNum);
			map->s_partition_func = NULL;
		} else if (type == 2) {
			struct udfPartitionMap2 *upm2 = (struct udfPartitionMap2 *)&(lvd->partitionMaps[offset]);
			if (!strncmp(upm2->partIdent.ident, UDF_ID_VIRTUAL, strlen(UDF_ID_VIRTUAL))) {
				if (le16_to_cpu(((__le16 *)upm2->partIdent.identSuffix)[0]) == 0x0150) {
					map->s_partition_type = UDF_VIRTUAL_MAP15;
					map->s_partition_func = udf_get_pblock_virt15;
				} else if (le16_to_cpu(((__le16 *)upm2->partIdent.identSuffix)[0]) == 0x0200) {
					map->s_partition_type = UDF_VIRTUAL_MAP20;
					map->s_partition_func = udf_get_pblock_virt20;
				}
			} else if (!strncmp(upm2->partIdent.ident, UDF_ID_SPARABLE, strlen(UDF_ID_SPARABLE))) {
				uint32_t loc;
				uint16_t ident;
				struct sparingTable *st;
				struct sparablePartitionMap *spm = (struct sparablePartitionMap *)&(lvd->partitionMaps[offset]);

				map->s_partition_type = UDF_SPARABLE_MAP15;
				map->s_type_specific.s_sparing.s_packet_len = le16_to_cpu(spm->packetLength);
				for (j = 0; j < spm->numSparingTables; j++) {
					loc = le32_to_cpu(spm->locSparingTable[j]);
					map->s_type_specific.s_sparing.s_spar_map[j] =
						udf_read_tagged(sb, loc, loc, &ident);
					if (map->s_type_specific.s_sparing.s_spar_map[j] != NULL) {
						st = (struct sparingTable *)map->s_type_specific.s_sparing.s_spar_map[j]->b_data;
						if (ident != 0 ||
						    strncmp(st->sparingIdent.ident, UDF_ID_SPARING, strlen(UDF_ID_SPARING))) {
							brelse(map->s_type_specific.s_sparing.s_spar_map[j]);
							map->s_type_specific.s_sparing.s_spar_map[j] = NULL;
						}
					}
				}
				map->s_partition_func = udf_get_pblock_spar15;
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
		long_ad *la = (long_ad *)&(lvd->logicalVolContentsUse[0]);

		*fileset = lelb_to_cpu(la->extLocation);
		udf_debug("FileSet found in LogicalVolDesc at block=%d, "
			  "partition=%d\n", fileset->logicalBlockNum,
			  fileset->partitionReferenceNum);
	}
	if (lvd->integritySeqExt.extLength)
		udf_load_logicalvolint(sb, leea_to_cpu(lvd->integritySeqExt));

	return 0;
}

/*
 * udf_load_logicalvolint
 *
 */
static void udf_load_logicalvolint(struct super_block *sb, kernel_extent_ad loc)
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
static int udf_process_sequence(struct super_block *sb, long block,
				long lastblock, kernel_lb_addr *fileset)
{
	struct buffer_head *bh = NULL;
	struct udf_vds_record vds[VDS_POS_LENGTH];
	struct generic_desc *gd;
	struct volDescPtr *vdp;
	int done = 0;
	int i, j;
	uint32_t vdsn;
	uint16_t ident;
	long next_s = 0, next_e = 0;

	memset(vds, 0, sizeof(struct udf_vds_record) * VDS_POS_LENGTH);

	/* Read the main descriptor sequence */
	for (; (!done && block <= lastblock); block++) {

		bh = udf_read_tagged(sb, block, block, &ident);
		if (!bh)
			break;

		/* Process each descriptor (ISO 13346 3/8.3-8.4) */
		gd = (struct generic_desc *)bh->b_data;
		vdsn = le32_to_cpu(gd->volDescSeqNum);
		switch (ident) {
		case TAG_IDENT_PVD: /* ISO 13346 3/10.1 */
			if (vdsn >= vds[VDS_POS_PRIMARY_VOL_DESC].volDescSeqNum) {
				vds[VDS_POS_PRIMARY_VOL_DESC].volDescSeqNum = vdsn;
				vds[VDS_POS_PRIMARY_VOL_DESC].block = block;
			}
			break;
		case TAG_IDENT_VDP: /* ISO 13346 3/10.3 */
			if (vdsn >= vds[VDS_POS_VOL_DESC_PTR].volDescSeqNum) {
				vds[VDS_POS_VOL_DESC_PTR].volDescSeqNum = vdsn;
				vds[VDS_POS_VOL_DESC_PTR].block = block;

				vdp = (struct volDescPtr *)bh->b_data;
				next_s = le32_to_cpu(vdp->nextVolDescSeqExt.extLocation);
				next_e = le32_to_cpu(vdp->nextVolDescSeqExt.extLength);
				next_e = next_e >> sb->s_blocksize_bits;
				next_e += next_s;
			}
			break;
		case TAG_IDENT_IUVD: /* ISO 13346 3/10.4 */
			if (vdsn >= vds[VDS_POS_IMP_USE_VOL_DESC].volDescSeqNum) {
				vds[VDS_POS_IMP_USE_VOL_DESC].volDescSeqNum = vdsn;
				vds[VDS_POS_IMP_USE_VOL_DESC].block = block;
			}
			break;
		case TAG_IDENT_PD: /* ISO 13346 3/10.5 */
			if (!vds[VDS_POS_PARTITION_DESC].block)
				vds[VDS_POS_PARTITION_DESC].block = block;
			break;
		case TAG_IDENT_LVD: /* ISO 13346 3/10.6 */
			if (vdsn >= vds[VDS_POS_LOGICAL_VOL_DESC].volDescSeqNum) {
				vds[VDS_POS_LOGICAL_VOL_DESC].volDescSeqNum = vdsn;
				vds[VDS_POS_LOGICAL_VOL_DESC].block = block;
			}
			break;
		case TAG_IDENT_USD: /* ISO 13346 3/10.8 */
			if (vdsn >= vds[VDS_POS_UNALLOC_SPACE_DESC].volDescSeqNum) {
				vds[VDS_POS_UNALLOC_SPACE_DESC].volDescSeqNum = vdsn;
				vds[VDS_POS_UNALLOC_SPACE_DESC].block = block;
			}
			break;
		case TAG_IDENT_TD: /* ISO 13346 3/10.9 */
			vds[VDS_POS_TERMINATING_DESC].block = block;
			if (next_e) {
				block = next_s;
				lastblock = next_e;
				next_s = next_e = 0;
			} else {
				done = 1;
			}
			break;
		}
		brelse(bh);
	}
	for (i = 0; i < VDS_POS_LENGTH; i++) {
		if (vds[i].block) {
			bh = udf_read_tagged(sb, vds[i].block, vds[i].block,
					     &ident);

			if (i == VDS_POS_PRIMARY_VOL_DESC) {
				udf_load_pvoldesc(sb, bh);
			} else if (i == VDS_POS_LOGICAL_VOL_DESC) {
				if (udf_load_logicalvol(sb, bh, fileset)) {
					brelse(bh);
					return 1;
				}
			} else if (i == VDS_POS_PARTITION_DESC) {
				struct buffer_head *bh2 = NULL;
				if (udf_load_partdesc(sb, bh)) {
					brelse(bh);
					return 1;
				}
				for (j = vds[i].block + 1;
				     j <  vds[VDS_POS_TERMINATING_DESC].block;
				     j++) {
					bh2 = udf_read_tagged(sb, j, j, &ident);
					gd = (struct generic_desc *)bh2->b_data;
					if (ident == TAG_IDENT_PD)
						if (udf_load_partdesc(sb,
								      bh2)) {
							brelse(bh);
							brelse(bh2);
							return 1;
						}
					brelse(bh2);
				}
			}
			brelse(bh);
		}
	}

	return 0;
}

/*
 * udf_check_valid()
 */
static int udf_check_valid(struct super_block *sb, int novrs, int silent)
{
	long block;

	if (novrs) {
		udf_debug("Validity check skipped because of novrs option\n");
		return 0;
	}
	/* Check that it is NSR02 compliant */
	/* Process any "CD-ROM Volume Descriptor Set" (ECMA 167 2/8.3.1) */
	else {
		block = udf_vrs(sb, silent);
		if (block == -1) {
			struct udf_sb_info *sbi = UDF_SB(sb);
			udf_debug("Failed to read byte 32768. Assuming open "
				  "disc. Skipping validity check\n");
			if (!sbi->s_last_block)
				sbi->s_last_block = udf_get_last_block(sb);
			return 0;
		} else
			return !block;
	}
}

static int udf_load_partition(struct super_block *sb, kernel_lb_addr *fileset)
{
	struct anchorVolDescPtr *anchor;
	uint16_t ident;
	struct buffer_head *bh;
	long main_s, main_e, reserve_s, reserve_e;
	int i, j;
	struct udf_sb_info *sbi;

	if (!sb)
		return 1;
	sbi = UDF_SB(sb);

	for (i = 0; i < ARRAY_SIZE(sbi->s_anchor); i++) {
		if (sbi->s_anchor[i] &&
		    (bh = udf_read_tagged(sb, sbi->s_anchor[i],
					  sbi->s_anchor[i], &ident))) {
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

			brelse(bh);

			/* Process the main & reserve sequences */
			/* responsible for finding the PartitionDesc(s) */
			if (!(udf_process_sequence(sb, main_s, main_e, fileset) &&
			      udf_process_sequence(sb, reserve_s, reserve_e, fileset)))
				break;
		}
	}

	if (i == ARRAY_SIZE(sbi->s_anchor)) {
		udf_debug("No Anchor block found\n");
		return 1;
	} else
		udf_debug("Using anchor in block %d\n", sbi->s_anchor[i]);

	for (i = 0; i < sbi->s_partitions; i++) {
		kernel_lb_addr uninitialized_var(ino);
		struct udf_part_map *map = &sbi->s_partmaps[i];
		switch (map->s_partition_type) {
		case UDF_VIRTUAL_MAP15:
		case UDF_VIRTUAL_MAP20:
			if (!sbi->s_last_block) {
				sbi->s_last_block = udf_get_last_block(sb);
				udf_find_anchor(sb);
			}

			if (!sbi->s_last_block) {
				udf_debug("Unable to determine Lastblock (For "
					  "Virtual Partition)\n");
				return 1;
			}

			for (j = 0; j < sbi->s_partitions; j++) {
				struct udf_part_map *map2 = &sbi->s_partmaps[j];
				if (j != i &&
				    map->s_volumeseqnum == map2->s_volumeseqnum &&
				    map->s_partition_num == map2->s_partition_num) {
					ino.partitionReferenceNum = j;
					ino.logicalBlockNum = sbi->s_last_block - map2->s_partition_root;
					break;
				}
			}

			if (j == sbi->s_partitions)
				return 1;

			sbi->s_vat_inode = udf_iget(sb, ino);
			if (!sbi->s_vat_inode)
				return 1;

			if (map->s_partition_type == UDF_VIRTUAL_MAP15) {
				map->s_type_specific.s_virtual.s_start_offset =
					udf_ext0_offset(sbi->s_vat_inode);
				map->s_type_specific.s_virtual.s_num_entries =
					(sbi->s_vat_inode->i_size - 36) >> 2;
			} else if (map->s_partition_type == UDF_VIRTUAL_MAP20) {
				uint32_t pos;

				pos = udf_block_map(sbi->s_vat_inode, 0);
				bh = sb_bread(sb, pos);
				if (!bh)
					return 1;
				map->s_type_specific.s_virtual.s_start_offset =
					le16_to_cpu(((struct virtualAllocationTable20 *)bh->b_data +
						     udf_ext0_offset(sbi->s_vat_inode))->lengthHeader) +
					udf_ext0_offset(sbi->s_vat_inode);
				map->s_type_specific.s_virtual.s_num_entries = (sbi->s_vat_inode->i_size -
									map->s_type_specific.s_virtual.s_start_offset) >> 2;
				brelse(bh);
			}
			map->s_partition_root = udf_get_pblock(sb, 0, i, 0);
			map->s_partition_len =
				sbi->s_partmaps[ino.partitionReferenceNum].
								s_partition_len;
		}
	}
	return 0;
}

static void udf_open_lvid(struct super_block *sb)
{
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct buffer_head *bh = sbi->s_lvid_bh;
	if (bh) {
		int i;
		kernel_timestamp cpu_time;
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)bh->b_data;
		struct logicalVolIntegrityDescImpUse *lvidiu = udf_sb_lvidiu(sbi);

		lvidiu->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		lvidiu->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		if (udf_time_to_stamp(&cpu_time, CURRENT_TIME))
			lvid->recordingDateAndTime = cpu_to_lets(cpu_time);
		lvid->integrityType = LVID_INTEGRITY_TYPE_OPEN;

		lvid->descTag.descCRC = cpu_to_le16(udf_crc((char *)lvid + sizeof(tag),
								       le16_to_cpu(lvid->descTag.descCRCLength), 0));

		lvid->descTag.tagChecksum = 0;
		for (i = 0; i < 16; i++)
			if (i != 4)
				lvid->descTag.tagChecksum +=
					((uint8_t *) &(lvid->descTag))[i];

		mark_buffer_dirty(bh);
	}
}

static void udf_close_lvid(struct super_block *sb)
{
	kernel_timestamp cpu_time;
	int i;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct buffer_head *bh = sbi->s_lvid_bh;
	struct logicalVolIntegrityDesc *lvid;

	if (!bh)
		return;

	lvid = (struct logicalVolIntegrityDesc *)bh->b_data;

	if (lvid->integrityType == LVID_INTEGRITY_TYPE_OPEN) {
		struct logicalVolIntegrityDescImpUse *lvidiu = udf_sb_lvidiu(sbi);
		lvidiu->impIdent.identSuffix[0] = UDF_OS_CLASS_UNIX;
		lvidiu->impIdent.identSuffix[1] = UDF_OS_ID_LINUX;
		if (udf_time_to_stamp(&cpu_time, CURRENT_TIME))
			lvid->recordingDateAndTime = cpu_to_lets(cpu_time);
		if (UDF_MAX_WRITE_VERSION > le16_to_cpu(lvidiu->maxUDFWriteRev))
			lvidiu->maxUDFWriteRev = cpu_to_le16(UDF_MAX_WRITE_VERSION);
		if (sbi->s_udfrev > le16_to_cpu(lvidiu->minUDFReadRev))
			lvidiu->minUDFReadRev = cpu_to_le16(sbi->s_udfrev);
		if (sbi->s_udfrev > le16_to_cpu(lvidiu->minUDFWriteRev))
			lvidiu->minUDFWriteRev = cpu_to_le16(sbi->s_udfrev);
		lvid->integrityType = cpu_to_le32(LVID_INTEGRITY_TYPE_CLOSE);

		lvid->descTag.descCRC =
			cpu_to_le16(udf_crc((char *)lvid + sizeof(tag),
					    le16_to_cpu(lvid->descTag.descCRCLength), 0));

		lvid->descTag.tagChecksum = 0;
		for (i = 0; i < 16; i++)
			if (i != 4)
				lvid->descTag.tagChecksum +=
					((uint8_t *)&(lvid->descTag))[i];

		mark_buffer_dirty(bh);
	}
}

static void udf_sb_free_bitmap(struct udf_bitmap *bitmap)
{
	int i;
	int nr_groups = bitmap->s_nr_groups;
	int size = sizeof(struct udf_bitmap) + (sizeof(struct buffer_head *) * nr_groups);

	for (i = 0; i < nr_groups; i++)
		if (bitmap->s_block_bitmap[i])
			brelse(bitmap->s_block_bitmap[i]);

	if (size <= PAGE_SIZE)
		kfree(bitmap);
	else
		vfree(bitmap);
}

/*
 * udf_read_super
 *
 * PURPOSE
 *	Complete the specified super block.
 *
 * PRE-CONDITIONS
 *	sb			Pointer to superblock to complete - never NULL.
 *	sb->s_dev		Device to read suberblock from.
 *	options			Pointer to mount options.
 *	silent			Silent flag.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static int udf_fill_super(struct super_block *sb, void *options, int silent)
{
	int i;
	struct inode *inode = NULL;
	struct udf_options uopt;
	kernel_lb_addr rootdir, fileset;
	struct udf_sb_info *sbi;

	uopt.flags = (1 << UDF_FLAG_USE_AD_IN_ICB) | (1 << UDF_FLAG_STRICT);
	uopt.uid = -1;
	uopt.gid = -1;
	uopt.umask = 0;

	sbi = kzalloc(sizeof(struct udf_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;

	mutex_init(&sbi->s_alloc_mutex);

	if (!udf_parse_options((char *)options, &uopt))
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
	sbi->s_nls_map = uopt.nls_map;

	/* Set the block size for all transfers */
	if (!udf_set_blocksize(sb, uopt.blocksize))
		goto error_out;

	if (uopt.session == 0xFFFFFFFF)
		sbi->s_session = udf_get_last_session(sb);
	else
		sbi->s_session = uopt.session;

	udf_debug("Multi-session=%d\n", sbi->s_session);

	sbi->s_last_block = uopt.lastblock;
	sbi->s_anchor[0] = sbi->s_anchor[1] = 0;
	sbi->s_anchor[2] = uopt.anchor;
	sbi->s_anchor[3] = 256;

	if (udf_check_valid(sb, uopt.novrs, silent)) {
		/* read volume recognition sequences */
		printk(KERN_WARNING "UDF-fs: No VRS found\n");
		goto error_out;
	}

	udf_find_anchor(sb);

	/* Fill in the rest of the superblock */
	sb->s_op = &udf_sb_ops;
	sb->dq_op = NULL;
	sb->s_dirt = 0;
	sb->s_magic = UDF_SUPER_MAGIC;
	sb->s_time_gran = 1000;

	if (udf_load_partition(sb, &fileset)) {
		printk(KERN_WARNING "UDF-fs: No partition found (1)\n");
		goto error_out;
	}

	udf_debug("Lastblock=%d\n", sbi->s_last_block);

	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDescImpUse *lvidiu = udf_sb_lvidiu(sbi);
		uint16_t minUDFReadRev = le16_to_cpu(lvidiu->minUDFReadRev);
		uint16_t minUDFWriteRev = le16_to_cpu(lvidiu->minUDFWriteRev);
		/* uint16_t maxUDFWriteRev = le16_to_cpu(lvidiu->maxUDFWriteRev); */

		if (minUDFReadRev > UDF_MAX_READ_VERSION) {
			printk(KERN_ERR "UDF-fs: minUDFReadRev=%x (max is %x)\n",
			       le16_to_cpu(lvidiu->minUDFReadRev),
			       UDF_MAX_READ_VERSION);
			goto error_out;
		} else if (minUDFWriteRev > UDF_MAX_WRITE_VERSION) {
			sb->s_flags |= MS_RDONLY;
		}

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

	if (sbi->s_partmaps[sbi->s_partition].s_partition_flags & UDF_PART_FLAG_READ_ONLY) {
		printk(KERN_NOTICE "UDF-fs: Partition marked readonly; forcing readonly mount\n");
		sb->s_flags |= MS_RDONLY;
	}

	if (udf_find_fileset(sb, &fileset, &rootdir)) {
		printk(KERN_WARNING "UDF-fs: No fileset found\n");
		goto error_out;
	}

	if (!silent) {
		kernel_timestamp ts;
		udf_time_to_stamp(&ts, sbi->s_record_time);
		udf_info("UDF %s (%s) Mounting volume '%s', "
			 "timestamp %04u/%02u/%02u %02u:%02u (%x)\n",
			 UDFFS_VERSION, UDFFS_DATE,
			 sbi->s_volume_ident, ts.year, ts.month, ts.day,
			 ts.hour, ts.minute, ts.typeAndTimezone);
	}
	if (!(sb->s_flags & MS_RDONLY))
		udf_open_lvid(sb);

	/* Assign the root inode */
	/* assign inodes by physical block number */
	/* perhaps it's not extensible enough, but for now ... */
	inode = udf_iget(sb, rootdir);
	if (!inode) {
		printk(KERN_ERR "UDF-fs: Error in udf_iget, block=%d, partition=%d\n",
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
	if (sbi->s_partitions) {
		struct udf_part_map *map = &sbi->s_partmaps[sbi->s_partition];
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
	}
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

void udf_error(struct super_block *sb, const char *function,
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

/*
 * udf_put_super
 *
 * PURPOSE
 *	Prepare for destruction of the superblock.
 *
 * DESCRIPTION
 *	Called before the filesystem is unmounted.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static void udf_put_super(struct super_block *sb)
{
	int i;
	struct udf_sb_info *sbi;

	sbi = UDF_SB(sb);
	if (sbi->s_vat_inode)
		iput(sbi->s_vat_inode);
	if (sbi->s_partitions) {
		struct udf_part_map *map = &sbi->s_partmaps[sbi->s_partition];
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
	}
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

/*
 * udf_stat_fs
 *
 * PURPOSE
 *	Return info about the filesystem.
 *
 * DESCRIPTION
 *	Called by sys_statfs()
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static int udf_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct logicalVolIntegrityDescImpUse *lvidiu;

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
	/* __kernel_fsid_t f_fsid */
	buf->f_namelen = UDF_NAME_LEN - 2;

	return 0;
}

static unsigned char udf_bitmap_lookup[16] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
};

static unsigned int udf_count_free_bitmap(struct super_block *sb, struct udf_bitmap *bitmap)
{
	struct buffer_head *bh = NULL;
	unsigned int accum = 0;
	int index;
	int block = 0, newblock;
	kernel_lb_addr loc;
	uint32_t bytes;
	uint8_t value;
	uint8_t *ptr;
	uint16_t ident;
	struct spaceBitmapDesc *bm;

	lock_kernel();

	loc.logicalBlockNum = bitmap->s_extPosition;
	loc.partitionReferenceNum = UDF_SB(sb)->s_partition;
	bh = udf_read_ptagged(sb, loc, 0, &ident);

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
		while ((bytes > 0) && (index < sb->s_blocksize)) {
			value = ptr[index];
			accum += udf_bitmap_lookup[value & 0x0f];
			accum += udf_bitmap_lookup[value >> 4];
			index++;
			bytes--;
		}
		if (bytes) {
			brelse(bh);
			newblock = udf_get_lb_pblock(sb, loc, ++block);
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
	unlock_kernel();

	return accum;
}

static unsigned int udf_count_free_table(struct super_block *sb, struct inode *table)
{
	unsigned int accum = 0;
	uint32_t elen;
	kernel_lb_addr eloc;
	int8_t etype;
	struct extent_position epos;

	lock_kernel();

	epos.block = UDF_I_LOCATION(table);
	epos.offset = sizeof(struct unallocSpaceEntry);
	epos.bh = NULL;

	while ((etype = udf_next_aext(table, &epos, &eloc, &elen, 1)) != -1)
		accum += (elen >> table->i_sb->s_blocksize_bits);

	brelse(epos.bh);

	unlock_kernel();

	return accum;
}

static unsigned int udf_count_free(struct super_block *sb)
{
	unsigned int accum = 0;
	struct udf_sb_info *sbi;
	struct udf_part_map *map;

	sbi = UDF_SB(sb);
	if (sbi->s_lvid_bh) {
		struct logicalVolIntegrityDesc *lvid = (struct logicalVolIntegrityDesc *)sbi->s_lvid_bh->b_data;
		if (le32_to_cpu(lvid->numOfPartitions) > sbi->s_partition) {
			accum = le32_to_cpu(lvid->freeSpaceTable[sbi->s_partition]);
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
