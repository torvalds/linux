// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/ufs/super.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 */

/* Derived from
 *
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
 
/*
 * Inspired by
 *
 *  linux/fs/ufs/super.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Copyright (C) 1996  Eddie C. Dost  (ecd@skynet.be)
 *
 * Kernel module support added on 96/04/26 by
 * Stefan Reinauer <stepan@home.culture.mipt.ru>
 *
 * Module usage counts added on 96/04/29 by
 * Gertjan van Wingerde <gwingerde@gmail.com>
 *
 * Clean swab support on 19970406 by
 * Francois-Rene Rideau <fare@tunes.org>
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * NeXTstep support added on February 5th 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk>.
 *
 * write support Daniel Pirkl <daniel.pirkl@email.cz> 1998
 * 
 * HP/UX hfs filesystem support added by
 * Martin K. Petersen <mkp@mkp.net>, August 1999
 *
 * UFS2 (of FreeBSD 5.x) support added by
 * Niraj Kumar <niraj17@iitbombay.org>, Jan 2004
 *
 * UFS2 write support added by
 * Evgeniy Dushistov <dushistov@mail.ru>, 2007
 */

#include <linux/exportfs.h>
#include <linux/module.h>
#include <linux/bitops.h>

#include <linux/stdarg.h>

#include <linux/uaccess.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/init.h>
#include <linux/parser.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/log2.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/iversion.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

static struct inode *ufs_nfs_get_inode(struct super_block *sb, u64 ino, u32 generation)
{
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct inode *inode;

	if (ino < UFS_ROOTINO || ino > (u64)uspi->s_ncg * uspi->s_ipg)
		return ERR_PTR(-ESTALE);

	inode = ufs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *ufs_fh_to_dentry(struct super_block *sb, struct fid *fid,
				       int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type, ufs_nfs_get_inode);
}

static struct dentry *ufs_fh_to_parent(struct super_block *sb, struct fid *fid,
				       int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type, ufs_nfs_get_inode);
}

static struct dentry *ufs_get_parent(struct dentry *child)
{
	ino_t ino;

	ino = ufs_inode_by_name(d_inode(child), &dotdot_name);
	if (!ino)
		return ERR_PTR(-ENOENT);
	return d_obtain_alias(ufs_iget(child->d_sb, ino));
}

static const struct export_operations ufs_export_ops = {
	.encode_fh = generic_encode_ino32_fh,
	.fh_to_dentry	= ufs_fh_to_dentry,
	.fh_to_parent	= ufs_fh_to_parent,
	.get_parent	= ufs_get_parent,
};

#ifdef CONFIG_UFS_DEBUG
/*
 * Print contents of ufs_super_block, useful for debugging
 */
static void ufs_print_super_stuff(struct super_block *sb,
				  struct ufs_super_block_first *usb1,
				  struct ufs_super_block_second *usb2,
				  struct ufs_super_block_third *usb3)
{
	u32 magic = fs32_to_cpu(sb, usb3->fs_magic);

	pr_debug("ufs_print_super_stuff\n");
	pr_debug("  magic:     0x%x\n", magic);
	if (fs32_to_cpu(sb, usb3->fs_magic) == UFS2_MAGIC) {
		pr_debug("  fs_size:   %llu\n", (unsigned long long)
			 fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_size));
		pr_debug("  fs_dsize:  %llu\n", (unsigned long long)
			 fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize));
		pr_debug("  bsize:         %u\n",
			 fs32_to_cpu(sb, usb1->fs_bsize));
		pr_debug("  fsize:         %u\n",
			 fs32_to_cpu(sb, usb1->fs_fsize));
		pr_debug("  fs_volname:  %s\n", usb2->fs_un.fs_u2.fs_volname);
		pr_debug("  fs_sblockloc: %llu\n", (unsigned long long)
			 fs64_to_cpu(sb, usb2->fs_un.fs_u2.fs_sblockloc));
		pr_debug("  cs_ndir(No of dirs):  %llu\n", (unsigned long long)
			 fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_ndir));
		pr_debug("  cs_nbfree(No of free blocks):  %llu\n",
			 (unsigned long long)
			 fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_nbfree));
		pr_info("  cs_nifree(Num of free inodes): %llu\n",
			(unsigned long long)
			fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nifree));
		pr_info("  cs_nffree(Num of free frags): %llu\n",
			(unsigned long long)
			fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nffree));
		pr_info("  fs_maxsymlinklen: %u\n",
			fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_maxsymlinklen));
	} else {
		pr_debug(" sblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_sblkno));
		pr_debug(" cblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_cblkno));
		pr_debug(" iblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_iblkno));
		pr_debug(" dblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_dblkno));
		pr_debug(" cgoffset:    %u\n",
			 fs32_to_cpu(sb, usb1->fs_cgoffset));
		pr_debug(" ~cgmask:     0x%x\n",
			 ~fs32_to_cpu(sb, usb1->fs_cgmask));
		pr_debug(" size:        %u\n", fs32_to_cpu(sb, usb1->fs_size));
		pr_debug(" dsize:       %u\n", fs32_to_cpu(sb, usb1->fs_dsize));
		pr_debug(" ncg:         %u\n", fs32_to_cpu(sb, usb1->fs_ncg));
		pr_debug(" bsize:       %u\n", fs32_to_cpu(sb, usb1->fs_bsize));
		pr_debug(" fsize:       %u\n", fs32_to_cpu(sb, usb1->fs_fsize));
		pr_debug(" frag:        %u\n", fs32_to_cpu(sb, usb1->fs_frag));
		pr_debug(" fragshift:   %u\n",
			 fs32_to_cpu(sb, usb1->fs_fragshift));
		pr_debug(" ~fmask:      %u\n", ~fs32_to_cpu(sb, usb1->fs_fmask));
		pr_debug(" fshift:      %u\n", fs32_to_cpu(sb, usb1->fs_fshift));
		pr_debug(" sbsize:      %u\n", fs32_to_cpu(sb, usb1->fs_sbsize));
		pr_debug(" spc:         %u\n", fs32_to_cpu(sb, usb1->fs_spc));
		pr_debug(" cpg:         %u\n", fs32_to_cpu(sb, usb1->fs_cpg));
		pr_debug(" ipg:         %u\n", fs32_to_cpu(sb, usb1->fs_ipg));
		pr_debug(" fpg:         %u\n", fs32_to_cpu(sb, usb1->fs_fpg));
		pr_debug(" csaddr:      %u\n", fs32_to_cpu(sb, usb1->fs_csaddr));
		pr_debug(" cssize:      %u\n", fs32_to_cpu(sb, usb1->fs_cssize));
		pr_debug(" cgsize:      %u\n", fs32_to_cpu(sb, usb1->fs_cgsize));
		pr_debug(" fstodb:      %u\n",
			 fs32_to_cpu(sb, usb1->fs_fsbtodb));
		pr_debug(" nrpos:       %u\n", fs32_to_cpu(sb, usb3->fs_nrpos));
		pr_debug(" ndir         %u\n",
			 fs32_to_cpu(sb, usb1->fs_cstotal.cs_ndir));
		pr_debug(" nifree       %u\n",
			 fs32_to_cpu(sb, usb1->fs_cstotal.cs_nifree));
		pr_debug(" nbfree       %u\n",
			 fs32_to_cpu(sb, usb1->fs_cstotal.cs_nbfree));
		pr_debug(" nffree       %u\n",
			 fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree));
	}
	pr_debug("\n");
}

/*
 * Print contents of ufs_cylinder_group, useful for debugging
 */
static void ufs_print_cylinder_stuff(struct super_block *sb,
				     struct ufs_cylinder_group *cg)
{
	pr_debug("\nufs_print_cylinder_stuff\n");
	pr_debug("size of ucg: %zu\n", sizeof(struct ufs_cylinder_group));
	pr_debug("  magic:        %x\n", fs32_to_cpu(sb, cg->cg_magic));
	pr_debug("  time:         %u\n", fs32_to_cpu(sb, cg->cg_time));
	pr_debug("  cgx:          %u\n", fs32_to_cpu(sb, cg->cg_cgx));
	pr_debug("  ncyl:         %u\n", fs16_to_cpu(sb, cg->cg_ncyl));
	pr_debug("  niblk:        %u\n", fs16_to_cpu(sb, cg->cg_niblk));
	pr_debug("  ndblk:        %u\n", fs32_to_cpu(sb, cg->cg_ndblk));
	pr_debug("  cs_ndir:      %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_ndir));
	pr_debug("  cs_nbfree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nbfree));
	pr_debug("  cs_nifree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nifree));
	pr_debug("  cs_nffree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nffree));
	pr_debug("  rotor:        %u\n", fs32_to_cpu(sb, cg->cg_rotor));
	pr_debug("  frotor:       %u\n", fs32_to_cpu(sb, cg->cg_frotor));
	pr_debug("  irotor:       %u\n", fs32_to_cpu(sb, cg->cg_irotor));
	pr_debug("  frsum:        %u, %u, %u, %u, %u, %u, %u, %u\n",
	    fs32_to_cpu(sb, cg->cg_frsum[0]), fs32_to_cpu(sb, cg->cg_frsum[1]),
	    fs32_to_cpu(sb, cg->cg_frsum[2]), fs32_to_cpu(sb, cg->cg_frsum[3]),
	    fs32_to_cpu(sb, cg->cg_frsum[4]), fs32_to_cpu(sb, cg->cg_frsum[5]),
	    fs32_to_cpu(sb, cg->cg_frsum[6]), fs32_to_cpu(sb, cg->cg_frsum[7]));
	pr_debug("  btotoff:      %u\n", fs32_to_cpu(sb, cg->cg_btotoff));
	pr_debug("  boff:         %u\n", fs32_to_cpu(sb, cg->cg_boff));
	pr_debug("  iuseoff:      %u\n", fs32_to_cpu(sb, cg->cg_iusedoff));
	pr_debug("  freeoff:      %u\n", fs32_to_cpu(sb, cg->cg_freeoff));
	pr_debug("  nextfreeoff:  %u\n", fs32_to_cpu(sb, cg->cg_nextfreeoff));
	pr_debug("  clustersumoff %u\n",
		 fs32_to_cpu(sb, cg->cg_u.cg_44.cg_clustersumoff));
	pr_debug("  clusteroff    %u\n",
		 fs32_to_cpu(sb, cg->cg_u.cg_44.cg_clusteroff));
	pr_debug("  nclusterblks  %u\n",
		 fs32_to_cpu(sb, cg->cg_u.cg_44.cg_nclusterblks));
	pr_debug("\n");
}
#else
#  define ufs_print_super_stuff(sb, usb1, usb2, usb3) /**/
#  define ufs_print_cylinder_stuff(sb, cg) /**/
#endif /* CONFIG_UFS_DEBUG */

static const struct super_operations ufs_super_ops;

void ufs_error (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct va_format vaf;
	va_list args;

	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	
	if (!sb_rdonly(sb)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH(uspi));
		ufs_mark_sb_dirty(sb);
		sb->s_flags |= SB_RDONLY;
	}
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	switch (UFS_SB(sb)->s_mount_opt & UFS_MOUNT_ONERROR) {
	case UFS_MOUNT_ONERROR_PANIC:
		panic("panic (device %s): %s: %pV\n",
		      sb->s_id, function, &vaf);

	case UFS_MOUNT_ONERROR_LOCK:
	case UFS_MOUNT_ONERROR_UMOUNT:
	case UFS_MOUNT_ONERROR_REPAIR:
		pr_crit("error (device %s): %s: %pV\n",
			sb->s_id, function, &vaf);
	}
	va_end(args);
}

void ufs_panic (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct va_format vaf;
	va_list args;
	
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	
	if (!sb_rdonly(sb)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH(uspi));
		ufs_mark_sb_dirty(sb);
	}
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	sb->s_flags |= SB_RDONLY;
	pr_crit("panic (device %s): %s: %pV\n",
		sb->s_id, function, &vaf);
	va_end(args);
}

void ufs_warning (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_warn("(device %s): %s: %pV\n",
		sb->s_id, function, &vaf);
	va_end(args);
}

enum {
       Opt_type_old = UFS_MOUNT_UFSTYPE_OLD,
       Opt_type_sunx86 = UFS_MOUNT_UFSTYPE_SUNx86,
       Opt_type_sun = UFS_MOUNT_UFSTYPE_SUN,
       Opt_type_sunos = UFS_MOUNT_UFSTYPE_SUNOS,
       Opt_type_44bsd = UFS_MOUNT_UFSTYPE_44BSD,
       Opt_type_ufs2 = UFS_MOUNT_UFSTYPE_UFS2,
       Opt_type_hp = UFS_MOUNT_UFSTYPE_HP,
       Opt_type_nextstepcd = UFS_MOUNT_UFSTYPE_NEXTSTEP_CD,
       Opt_type_nextstep = UFS_MOUNT_UFSTYPE_NEXTSTEP,
       Opt_type_openstep = UFS_MOUNT_UFSTYPE_OPENSTEP,
       Opt_onerror_panic = UFS_MOUNT_ONERROR_PANIC,
       Opt_onerror_lock = UFS_MOUNT_ONERROR_LOCK,
       Opt_onerror_umount = UFS_MOUNT_ONERROR_UMOUNT,
       Opt_onerror_repair = UFS_MOUNT_ONERROR_REPAIR,
       Opt_err
};

static const match_table_t tokens = {
	{Opt_type_old, "ufstype=old"},
	{Opt_type_sunx86, "ufstype=sunx86"},
	{Opt_type_sun, "ufstype=sun"},
	{Opt_type_sunos, "ufstype=sunos"},
	{Opt_type_44bsd, "ufstype=44bsd"},
	{Opt_type_ufs2, "ufstype=ufs2"},
	{Opt_type_ufs2, "ufstype=5xbsd"},
	{Opt_type_hp, "ufstype=hp"},
	{Opt_type_nextstepcd, "ufstype=nextstep-cd"},
	{Opt_type_nextstep, "ufstype=nextstep"},
	{Opt_type_openstep, "ufstype=openstep"},
/*end of possible ufs types */
	{Opt_onerror_panic, "onerror=panic"},
	{Opt_onerror_lock, "onerror=lock"},
	{Opt_onerror_umount, "onerror=umount"},
	{Opt_onerror_repair, "onerror=repair"},
	{Opt_err, NULL}
};

static int ufs_parse_options (char * options, unsigned * mount_options)
{
	char * p;
	
	UFSD("ENTER\n");
	
	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_type_old:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_OLD);
			break;
		case Opt_type_sunx86:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_SUNx86);
			break;
		case Opt_type_sun:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_SUN);
			break;
		case Opt_type_sunos:
			ufs_clear_opt(*mount_options, UFSTYPE);
			ufs_set_opt(*mount_options, UFSTYPE_SUNOS);
			break;
		case Opt_type_44bsd:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_44BSD);
			break;
		case Opt_type_ufs2:
			ufs_clear_opt(*mount_options, UFSTYPE);
			ufs_set_opt(*mount_options, UFSTYPE_UFS2);
			break;
		case Opt_type_hp:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_HP);
			break;
		case Opt_type_nextstepcd:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_NEXTSTEP_CD);
			break;
		case Opt_type_nextstep:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_NEXTSTEP);
			break;
		case Opt_type_openstep:
			ufs_clear_opt (*mount_options, UFSTYPE);
			ufs_set_opt (*mount_options, UFSTYPE_OPENSTEP);
			break;
		case Opt_onerror_panic:
			ufs_clear_opt (*mount_options, ONERROR);
			ufs_set_opt (*mount_options, ONERROR_PANIC);
			break;
		case Opt_onerror_lock:
			ufs_clear_opt (*mount_options, ONERROR);
			ufs_set_opt (*mount_options, ONERROR_LOCK);
			break;
		case Opt_onerror_umount:
			ufs_clear_opt (*mount_options, ONERROR);
			ufs_set_opt (*mount_options, ONERROR_UMOUNT);
			break;
		case Opt_onerror_repair:
			pr_err("Unable to do repair on error, will lock lock instead\n");
			ufs_clear_opt (*mount_options, ONERROR);
			ufs_set_opt (*mount_options, ONERROR_REPAIR);
			break;
		default:
			pr_err("Invalid option: \"%s\" or missing value\n", p);
			return 0;
		}
	}
	return 1;
}

/*
 * Different types of UFS hold fs_cstotal in different
 * places, and use different data structure for it.
 * To make things simpler we just copy fs_cstotal to ufs_sb_private_info
 */
static void ufs_setup_cstotal(struct super_block *sb)
{
	struct ufs_sb_info *sbi = UFS_SB(sb);
	struct ufs_sb_private_info *uspi = sbi->s_uspi;
	struct ufs_super_block_first *usb1;
	struct ufs_super_block_second *usb2;
	struct ufs_super_block_third *usb3;
	unsigned mtype = sbi->s_mount_opt & UFS_MOUNT_UFSTYPE;

	UFSD("ENTER, mtype=%u\n", mtype);
	usb1 = ubh_get_usb_first(uspi);
	usb2 = ubh_get_usb_second(uspi);
	usb3 = ubh_get_usb_third(uspi);

	if ((mtype == UFS_MOUNT_UFSTYPE_44BSD &&
	     (usb2->fs_un.fs_u2.fs_maxbsize == usb1->fs_bsize)) ||
	    mtype == UFS_MOUNT_UFSTYPE_UFS2) {
		/*we have statistic in different place, then usual*/
		uspi->cs_total.cs_ndir = fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_ndir);
		uspi->cs_total.cs_nbfree = fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_nbfree);
		uspi->cs_total.cs_nifree = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nifree);
		uspi->cs_total.cs_nffree = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nffree);
	} else {
		uspi->cs_total.cs_ndir = fs32_to_cpu(sb, usb1->fs_cstotal.cs_ndir);
		uspi->cs_total.cs_nbfree = fs32_to_cpu(sb, usb1->fs_cstotal.cs_nbfree);
		uspi->cs_total.cs_nifree = fs32_to_cpu(sb, usb1->fs_cstotal.cs_nifree);
		uspi->cs_total.cs_nffree = fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree);
	}
	UFSD("EXIT\n");
}

/*
 * Read on-disk structures associated with cylinder groups
 */
static int ufs_read_cylinder_structures(struct super_block *sb)
{
	struct ufs_sb_info *sbi = UFS_SB(sb);
	struct ufs_sb_private_info *uspi = sbi->s_uspi;
	struct ufs_buffer_head * ubh;
	unsigned char * base, * space;
	unsigned size, blks, i;

	UFSD("ENTER\n");

	/*
	 * Read cs structures from (usually) first data block
	 * on the device. 
	 */
	size = uspi->s_cssize;
	blks = (size + uspi->s_fsize - 1) >> uspi->s_fshift;
	base = space = kmalloc(size, GFP_NOFS);
	if (!base)
		goto failed; 
	sbi->s_csp = (struct ufs_csum *)space;
	for (i = 0; i < blks; i += uspi->s_fpb) {
		size = uspi->s_bsize;
		if (i + uspi->s_fpb > blks)
			size = (blks - i) * uspi->s_fsize;

		ubh = ubh_bread(sb, uspi->s_csaddr + i, size);
		
		if (!ubh)
			goto failed;

		ubh_ubhcpymem (space, ubh, size);

		space += size;
		ubh_brelse (ubh);
		ubh = NULL;
	}

	/*
	 * Read cylinder group (we read only first fragment from block
	 * at this time) and prepare internal data structures for cg caching.
	 */
	sbi->s_ucg = kmalloc_array(uspi->s_ncg, sizeof(struct buffer_head *),
				   GFP_NOFS);
	if (!sbi->s_ucg)
		goto failed;
	for (i = 0; i < uspi->s_ncg; i++) 
		sbi->s_ucg[i] = NULL;
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		sbi->s_ucpi[i] = NULL;
		sbi->s_cgno[i] = UFS_CGNO_EMPTY;
	}
	for (i = 0; i < uspi->s_ncg; i++) {
		UFSD("read cg %u\n", i);
		if (!(sbi->s_ucg[i] = sb_bread(sb, ufs_cgcmin(i))))
			goto failed;
		if (!ufs_cg_chkmagic (sb, (struct ufs_cylinder_group *) sbi->s_ucg[i]->b_data))
			goto failed;

		ufs_print_cylinder_stuff(sb, (struct ufs_cylinder_group *) sbi->s_ucg[i]->b_data);
	}
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		if (!(sbi->s_ucpi[i] = kmalloc (sizeof(struct ufs_cg_private_info), GFP_NOFS)))
			goto failed;
		sbi->s_cgno[i] = UFS_CGNO_EMPTY;
	}
	sbi->s_cg_loaded = 0;
	UFSD("EXIT\n");
	return 1;

failed:
	kfree (base);
	if (sbi->s_ucg) {
		for (i = 0; i < uspi->s_ncg; i++)
			if (sbi->s_ucg[i])
				brelse (sbi->s_ucg[i]);
		kfree (sbi->s_ucg);
		for (i = 0; i < UFS_MAX_GROUP_LOADED; i++)
			kfree (sbi->s_ucpi[i]);
	}
	UFSD("EXIT (FAILED)\n");
	return 0;
}

/*
 * Sync our internal copy of fs_cstotal with disk
 */
static void ufs_put_cstotal(struct super_block *sb)
{
	unsigned mtype = UFS_SB(sb)->s_mount_opt & UFS_MOUNT_UFSTYPE;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct ufs_super_block_first *usb1;
	struct ufs_super_block_second *usb2;
	struct ufs_super_block_third *usb3;

	UFSD("ENTER\n");
	usb1 = ubh_get_usb_first(uspi);
	usb2 = ubh_get_usb_second(uspi);
	usb3 = ubh_get_usb_third(uspi);

	if (mtype == UFS_MOUNT_UFSTYPE_UFS2) {
		/*we have statistic in different place, then usual*/
		usb2->fs_un.fs_u2.cs_ndir =
			cpu_to_fs64(sb, uspi->cs_total.cs_ndir);
		usb2->fs_un.fs_u2.cs_nbfree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nbfree);
		usb3->fs_un1.fs_u2.cs_nifree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nifree);
		usb3->fs_un1.fs_u2.cs_nffree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nffree);
		goto out;
	}

	if (mtype == UFS_MOUNT_UFSTYPE_44BSD &&
	     (usb2->fs_un.fs_u2.fs_maxbsize == usb1->fs_bsize)) {
		/* store stats in both old and new places */
		usb2->fs_un.fs_u2.cs_ndir =
			cpu_to_fs64(sb, uspi->cs_total.cs_ndir);
		usb2->fs_un.fs_u2.cs_nbfree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nbfree);
		usb3->fs_un1.fs_u2.cs_nifree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nifree);
		usb3->fs_un1.fs_u2.cs_nffree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nffree);
	}
	usb1->fs_cstotal.cs_ndir = cpu_to_fs32(sb, uspi->cs_total.cs_ndir);
	usb1->fs_cstotal.cs_nbfree = cpu_to_fs32(sb, uspi->cs_total.cs_nbfree);
	usb1->fs_cstotal.cs_nifree = cpu_to_fs32(sb, uspi->cs_total.cs_nifree);
	usb1->fs_cstotal.cs_nffree = cpu_to_fs32(sb, uspi->cs_total.cs_nffree);
out:
	ubh_mark_buffer_dirty(USPI_UBH(uspi));
	ufs_print_super_stuff(sb, usb1, usb2, usb3);
	UFSD("EXIT\n");
}

/**
 * ufs_put_super_internal() - put on-disk intrenal structures
 * @sb: pointer to super_block structure
 * Put on-disk structures associated with cylinder groups
 * and write them back to disk, also update cs_total on disk
 */
static void ufs_put_super_internal(struct super_block *sb)
{
	struct ufs_sb_info *sbi = UFS_SB(sb);
	struct ufs_sb_private_info *uspi = sbi->s_uspi;
	struct ufs_buffer_head * ubh;
	unsigned char * base, * space;
	unsigned blks, size, i;

	
	UFSD("ENTER\n");

	ufs_put_cstotal(sb);
	size = uspi->s_cssize;
	blks = (size + uspi->s_fsize - 1) >> uspi->s_fshift;
	base = space = (char*) sbi->s_csp;
	for (i = 0; i < blks; i += uspi->s_fpb) {
		size = uspi->s_bsize;
		if (i + uspi->s_fpb > blks)
			size = (blks - i) * uspi->s_fsize;

		ubh = ubh_bread(sb, uspi->s_csaddr + i, size);

		ubh_memcpyubh (ubh, space, size);
		space += size;
		ubh_mark_buffer_uptodate (ubh, 1);
		ubh_mark_buffer_dirty (ubh);
		ubh_brelse (ubh);
	}
	for (i = 0; i < sbi->s_cg_loaded; i++) {
		ufs_put_cylinder (sb, i);
		kfree (sbi->s_ucpi[i]);
	}
	for (; i < UFS_MAX_GROUP_LOADED; i++) 
		kfree (sbi->s_ucpi[i]);
	for (i = 0; i < uspi->s_ncg; i++) 
		brelse (sbi->s_ucg[i]);
	kfree (sbi->s_ucg);
	kfree (base);

	UFSD("EXIT\n");
}

static int ufs_sync_fs(struct super_block *sb, int wait)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned flags;

	mutex_lock(&UFS_SB(sb)->s_lock);

	UFSD("ENTER\n");

	flags = UFS_SB(sb)->s_flags;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	usb3 = ubh_get_usb_third(uspi);

	usb1->fs_time = ufs_get_seconds(sb);
	if ((flags & UFS_ST_MASK) == UFS_ST_SUN  ||
	    (flags & UFS_ST_MASK) == UFS_ST_SUNOS ||
	    (flags & UFS_ST_MASK) == UFS_ST_SUNx86)
		ufs_set_fs_state(sb, usb1, usb3,
				UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time));
	ufs_put_cstotal(sb);

	UFSD("EXIT\n");
	mutex_unlock(&UFS_SB(sb)->s_lock);

	return 0;
}

static void delayed_sync_fs(struct work_struct *work)
{
	struct ufs_sb_info *sbi;

	sbi = container_of(work, struct ufs_sb_info, sync_work.work);

	spin_lock(&sbi->work_lock);
	sbi->work_queued = 0;
	spin_unlock(&sbi->work_lock);

	ufs_sync_fs(sbi->sb, 1);
}

void ufs_mark_sb_dirty(struct super_block *sb)
{
	struct ufs_sb_info *sbi = UFS_SB(sb);
	unsigned long delay;

	spin_lock(&sbi->work_lock);
	if (!sbi->work_queued) {
		delay = msecs_to_jiffies(dirty_writeback_interval * 10);
		queue_delayed_work(system_long_wq, &sbi->sync_work, delay);
		sbi->work_queued = 1;
	}
	spin_unlock(&sbi->work_lock);
}

static void ufs_put_super(struct super_block *sb)
{
	struct ufs_sb_info * sbi = UFS_SB(sb);

	UFSD("ENTER\n");

	if (!sb_rdonly(sb))
		ufs_put_super_internal(sb);
	cancel_delayed_work_sync(&sbi->sync_work);

	ubh_brelse_uspi (sbi->s_uspi);
	kfree (sbi->s_uspi);
	kfree (sbi);
	sb->s_fs_info = NULL;
	UFSD("EXIT\n");
	return;
}

static u64 ufs_max_bytes(struct super_block *sb)
{
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	int bits = uspi->s_apbshift;
	u64 res;

	if (bits > 21)
		res = ~0ULL;
	else
		res = UFS_NDADDR + (1LL << bits) + (1LL << (2*bits)) +
			(1LL << (3*bits));

	if (res >= (MAX_LFS_FILESIZE >> uspi->s_bshift))
		return MAX_LFS_FILESIZE;
	return res << uspi->s_bshift;
}

static int ufs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct ufs_sb_info * sbi;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_second * usb2;
	struct ufs_super_block_third * usb3;
	struct ufs_buffer_head * ubh;	
	struct inode *inode;
	unsigned block_size, super_block_size;
	unsigned flags;
	unsigned super_block_offset;
	unsigned maxsymlen;
	int ret = -EINVAL;

	uspi = NULL;
	ubh = NULL;
	flags = 0;
	
	UFSD("ENTER\n");

#ifndef CONFIG_UFS_FS_WRITE
	if (!sb_rdonly(sb)) {
		pr_err("ufs was compiled with read-only support, can't be mounted as read-write\n");
		return -EROFS;
	}
#endif
		
	sbi = kzalloc(sizeof(struct ufs_sb_info), GFP_KERNEL);
	if (!sbi)
		goto failed_nomem;
	sb->s_fs_info = sbi;
	sbi->sb = sb;

	UFSD("flag %u\n", (int)(sb_rdonly(sb)));
	
	mutex_init(&sbi->s_lock);
	spin_lock_init(&sbi->work_lock);
	INIT_DELAYED_WORK(&sbi->sync_work, delayed_sync_fs);
	/*
	 * Set default mount options
	 * Parse mount options
	 */
	sbi->s_mount_opt = 0;
	ufs_set_opt (sbi->s_mount_opt, ONERROR_LOCK);
	if (!ufs_parse_options ((char *) data, &sbi->s_mount_opt)) {
		pr_err("wrong mount options\n");
		goto failed;
	}
	if (!(sbi->s_mount_opt & UFS_MOUNT_UFSTYPE)) {
		if (!silent)
			pr_err("You didn't specify the type of your ufs filesystem\n\n"
			"mount -t ufs -o ufstype="
			"sun|sunx86|44bsd|ufs2|5xbsd|old|hp|nextstep|nextstep-cd|openstep ...\n\n"
			">>>WARNING<<< Wrong ufstype may corrupt your filesystem, "
			"default is ufstype=old\n");
		ufs_set_opt (sbi->s_mount_opt, UFSTYPE_OLD);
	}

	uspi = kzalloc(sizeof(struct ufs_sb_private_info), GFP_KERNEL);
	sbi->s_uspi = uspi;
	if (!uspi)
		goto failed;
	uspi->s_dirblksize = UFS_SECTOR_SIZE;
	super_block_offset=UFS_SBLOCK;

	sb->s_maxbytes = MAX_LFS_FILESIZE;

	sb->s_time_gran = NSEC_PER_SEC;
	sb->s_time_min = S32_MIN;
	sb->s_time_max = S32_MAX;

	switch (sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) {
	case UFS_MOUNT_UFSTYPE_44BSD:
		UFSD("ufstype=44bsd\n");
		uspi->s_fsize = block_size = 512;
		uspi->s_fmask = ~(512 - 1);
		uspi->s_fshift = 9;
		uspi->s_sbsize = super_block_size = 1536;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
		break;
	case UFS_MOUNT_UFSTYPE_UFS2:
		UFSD("ufstype=ufs2\n");
		super_block_offset=SBLOCK_UFS2;
		uspi->s_fsize = block_size = 512;
		uspi->s_fmask = ~(512 - 1);
		uspi->s_fshift = 9;
		uspi->s_sbsize = super_block_size = 1536;
		uspi->s_sbbase =  0;
		sb->s_time_gran = 1;
		sb->s_time_min = S64_MIN;
		sb->s_time_max = S64_MAX;
		flags |= UFS_TYPE_UFS2 | UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
		break;
		
	case UFS_MOUNT_UFSTYPE_SUN:
		UFSD("ufstype=sun\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_maxsymlinklen = 0; /* Not supported on disk */
		flags |= UFS_DE_OLD | UFS_UID_EFT | UFS_ST_SUN | UFS_CG_SUN;
		break;

	case UFS_MOUNT_UFSTYPE_SUNOS:
		UFSD("ufstype=sunos\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = 2048;
		super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_maxsymlinklen = 0; /* Not supported on disk */
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_SUNOS | UFS_CG_SUN;
		break;

	case UFS_MOUNT_UFSTYPE_SUNx86:
		UFSD("ufstype=sunx86\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_maxsymlinklen = 0; /* Not supported on disk */
		flags |= UFS_DE_OLD | UFS_UID_EFT | UFS_ST_SUNx86 | UFS_CG_SUN;
		break;

	case UFS_MOUNT_UFSTYPE_OLD:
		UFSD("ufstype=old\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!sb_rdonly(sb)) {
			if (!silent)
				pr_info("ufstype=old is supported read-only\n");
			sb->s_flags |= SB_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_NEXTSTEP:
		UFSD("ufstype=nextstep\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_dirblksize = 1024;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!sb_rdonly(sb)) {
			if (!silent)
				pr_info("ufstype=nextstep is supported read-only\n");
			sb->s_flags |= SB_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_NEXTSTEP_CD:
		UFSD("ufstype=nextstep-cd\n");
		uspi->s_fsize = block_size = 2048;
		uspi->s_fmask = ~(2048 - 1);
		uspi->s_fshift = 11;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_dirblksize = 1024;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!sb_rdonly(sb)) {
			if (!silent)
				pr_info("ufstype=nextstep-cd is supported read-only\n");
			sb->s_flags |= SB_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_OPENSTEP:
		UFSD("ufstype=openstep\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_dirblksize = 1024;
		flags |= UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
		if (!sb_rdonly(sb)) {
			if (!silent)
				pr_info("ufstype=openstep is supported read-only\n");
			sb->s_flags |= SB_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_HP:
		UFSD("ufstype=hp\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!sb_rdonly(sb)) {
			if (!silent)
				pr_info("ufstype=hp is supported read-only\n");
			sb->s_flags |= SB_RDONLY;
 		}
 		break;
	default:
		if (!silent)
			pr_err("unknown ufstype\n");
		goto failed;
	}
	
again:	
	if (!sb_set_blocksize(sb, block_size)) {
		pr_err("failed to set blocksize\n");
		goto failed;
	}

	/*
	 * read ufs super block from device
	 */

	ubh = ubh_bread_uspi(uspi, sb, uspi->s_sbbase + super_block_offset/block_size, super_block_size);
	
	if (!ubh) 
            goto failed;

	usb1 = ubh_get_usb_first(uspi);
	usb2 = ubh_get_usb_second(uspi);
	usb3 = ubh_get_usb_third(uspi);

	/* Sort out mod used on SunOS 4.1.3 for fs_state */
	uspi->s_postblformat = fs32_to_cpu(sb, usb3->fs_postblformat);
	if (((flags & UFS_ST_MASK) == UFS_ST_SUNOS) &&
	    (uspi->s_postblformat != UFS_42POSTBLFMT)) {
		flags &= ~UFS_ST_MASK;
		flags |=  UFS_ST_SUN;
	}

	if ((flags & UFS_ST_MASK) == UFS_ST_44BSD &&
	    uspi->s_postblformat == UFS_42POSTBLFMT) {
		if (!silent)
			pr_err("this is not a 44bsd filesystem");
		goto failed;
	}

	/*
	 * Check ufs magic number
	 */
	sbi->s_bytesex = BYTESEX_LE;
	switch ((uspi->fs_magic = fs32_to_cpu(sb, usb3->fs_magic))) {
		case UFS_MAGIC:
		case UFS_MAGIC_BW:
		case UFS2_MAGIC:
		case UFS_MAGIC_LFN:
	        case UFS_MAGIC_FEA:
	        case UFS_MAGIC_4GB:
			goto magic_found;
	}
	sbi->s_bytesex = BYTESEX_BE;
	switch ((uspi->fs_magic = fs32_to_cpu(sb, usb3->fs_magic))) {
		case UFS_MAGIC:
		case UFS_MAGIC_BW:
		case UFS2_MAGIC:
		case UFS_MAGIC_LFN:
	        case UFS_MAGIC_FEA:
	        case UFS_MAGIC_4GB:
			goto magic_found;
	}

	if ((((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_NEXTSTEP) 
	  || ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_NEXTSTEP_CD) 
	  || ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_OPENSTEP)) 
	  && uspi->s_sbbase < 256) {
		ubh_brelse_uspi(uspi);
		ubh = NULL;
		uspi->s_sbbase += 8;
		goto again;
	}
	if (!silent)
		pr_err("%s(): bad magic number\n", __func__);
	goto failed;

magic_found:
	/*
	 * Check block and fragment sizes
	 */
	uspi->s_bsize = fs32_to_cpu(sb, usb1->fs_bsize);
	uspi->s_fsize = fs32_to_cpu(sb, usb1->fs_fsize);
	uspi->s_sbsize = fs32_to_cpu(sb, usb1->fs_sbsize);
	uspi->s_fmask = fs32_to_cpu(sb, usb1->fs_fmask);
	uspi->s_fshift = fs32_to_cpu(sb, usb1->fs_fshift);

	if (!is_power_of_2(uspi->s_fsize)) {
		pr_err("%s(): fragment size %u is not a power of 2\n",
		       __func__, uspi->s_fsize);
		goto failed;
	}
	if (uspi->s_fsize < 512) {
		pr_err("%s(): fragment size %u is too small\n",
		       __func__, uspi->s_fsize);
		goto failed;
	}
	if (uspi->s_fsize > 4096) {
		pr_err("%s(): fragment size %u is too large\n",
		       __func__, uspi->s_fsize);
		goto failed;
	}
	if (!is_power_of_2(uspi->s_bsize)) {
		pr_err("%s(): block size %u is not a power of 2\n",
		       __func__, uspi->s_bsize);
		goto failed;
	}
	if (uspi->s_bsize < 4096) {
		pr_err("%s(): block size %u is too small\n",
		       __func__, uspi->s_bsize);
		goto failed;
	}
	if (uspi->s_bsize / uspi->s_fsize > 8) {
		pr_err("%s(): too many fragments per block (%u)\n",
		       __func__, uspi->s_bsize / uspi->s_fsize);
		goto failed;
	}
	if (uspi->s_fsize != block_size || uspi->s_sbsize != super_block_size) {
		ubh_brelse_uspi(uspi);
		ubh = NULL;
		block_size = uspi->s_fsize;
		super_block_size = uspi->s_sbsize;
		UFSD("another value of block_size or super_block_size %u, %u\n", block_size, super_block_size);
		goto again;
	}

	sbi->s_flags = flags;/*after that line some functions use s_flags*/
	ufs_print_super_stuff(sb, usb1, usb2, usb3);

	/*
	 * Check, if file system was correctly unmounted.
	 * If not, make it read only.
	 */
	if (((flags & UFS_ST_MASK) == UFS_ST_44BSD) ||
	  ((flags & UFS_ST_MASK) == UFS_ST_OLD) ||
	  (((flags & UFS_ST_MASK) == UFS_ST_SUN ||
	    (flags & UFS_ST_MASK) == UFS_ST_SUNOS ||
	  (flags & UFS_ST_MASK) == UFS_ST_SUNx86) &&
	  (ufs_get_fs_state(sb, usb1, usb3) == (UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time))))) {
		switch(usb1->fs_clean) {
		case UFS_FSCLEAN:
			UFSD("fs is clean\n");
			break;
		case UFS_FSSTABLE:
			UFSD("fs is stable\n");
			break;
		case UFS_FSLOG:
			UFSD("fs is logging fs\n");
			break;
		case UFS_FSOSF1:
			UFSD("fs is DEC OSF/1\n");
			break;
		case UFS_FSACTIVE:
			pr_err("%s(): fs is active\n", __func__);
			sb->s_flags |= SB_RDONLY;
			break;
		case UFS_FSBAD:
			pr_err("%s(): fs is bad\n", __func__);
			sb->s_flags |= SB_RDONLY;
			break;
		default:
			pr_err("%s(): can't grok fs_clean 0x%x\n",
			       __func__, usb1->fs_clean);
			sb->s_flags |= SB_RDONLY;
			break;
		}
	} else {
		pr_err("%s(): fs needs fsck\n", __func__);
		sb->s_flags |= SB_RDONLY;
	}

	/*
	 * Read ufs_super_block into internal data structures
	 */
	sb->s_op = &ufs_super_ops;
	sb->s_export_op = &ufs_export_ops;

	sb->s_magic = fs32_to_cpu(sb, usb3->fs_magic);

	uspi->s_sblkno = fs32_to_cpu(sb, usb1->fs_sblkno);
	uspi->s_cblkno = fs32_to_cpu(sb, usb1->fs_cblkno);
	uspi->s_iblkno = fs32_to_cpu(sb, usb1->fs_iblkno);
	uspi->s_dblkno = fs32_to_cpu(sb, usb1->fs_dblkno);
	uspi->s_cgoffset = fs32_to_cpu(sb, usb1->fs_cgoffset);
	uspi->s_cgmask = fs32_to_cpu(sb, usb1->fs_cgmask);

	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
		uspi->s_size  = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_size);
		uspi->s_dsize = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize);
	} else {
		uspi->s_size  =  fs32_to_cpu(sb, usb1->fs_size);
		uspi->s_dsize =  fs32_to_cpu(sb, usb1->fs_dsize);
	}

	uspi->s_ncg = fs32_to_cpu(sb, usb1->fs_ncg);
	/* s_bsize already set */
	/* s_fsize already set */
	uspi->s_fpb = fs32_to_cpu(sb, usb1->fs_frag);
	uspi->s_minfree = fs32_to_cpu(sb, usb1->fs_minfree);
	uspi->s_bmask = fs32_to_cpu(sb, usb1->fs_bmask);
	uspi->s_fmask = fs32_to_cpu(sb, usb1->fs_fmask);
	uspi->s_bshift = fs32_to_cpu(sb, usb1->fs_bshift);
	uspi->s_fshift = fs32_to_cpu(sb, usb1->fs_fshift);
	UFSD("uspi->s_bshift = %d,uspi->s_fshift = %d", uspi->s_bshift,
		uspi->s_fshift);
	uspi->s_fpbshift = fs32_to_cpu(sb, usb1->fs_fragshift);
	uspi->s_fsbtodb = fs32_to_cpu(sb, usb1->fs_fsbtodb);
	/* s_sbsize already set */
	uspi->s_csmask = fs32_to_cpu(sb, usb1->fs_csmask);
	uspi->s_csshift = fs32_to_cpu(sb, usb1->fs_csshift);
	uspi->s_nindir = fs32_to_cpu(sb, usb1->fs_nindir);
	uspi->s_inopb = fs32_to_cpu(sb, usb1->fs_inopb);
	uspi->s_nspf = fs32_to_cpu(sb, usb1->fs_nspf);
	uspi->s_npsect = ufs_get_fs_npsect(sb, usb1, usb3);
	uspi->s_interleave = fs32_to_cpu(sb, usb1->fs_interleave);
	uspi->s_trackskew = fs32_to_cpu(sb, usb1->fs_trackskew);

	if (uspi->fs_magic == UFS2_MAGIC)
		uspi->s_csaddr = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_csaddr);
	else
		uspi->s_csaddr = fs32_to_cpu(sb, usb1->fs_csaddr);

	uspi->s_cssize = fs32_to_cpu(sb, usb1->fs_cssize);
	uspi->s_cgsize = fs32_to_cpu(sb, usb1->fs_cgsize);
	uspi->s_ntrak = fs32_to_cpu(sb, usb1->fs_ntrak);
	uspi->s_nsect = fs32_to_cpu(sb, usb1->fs_nsect);
	uspi->s_spc = fs32_to_cpu(sb, usb1->fs_spc);
	uspi->s_ipg = fs32_to_cpu(sb, usb1->fs_ipg);
	uspi->s_fpg = fs32_to_cpu(sb, usb1->fs_fpg);
	uspi->s_cpc = fs32_to_cpu(sb, usb2->fs_un.fs_u1.fs_cpc);
	uspi->s_contigsumsize = fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_contigsumsize);
	uspi->s_qbmask = ufs_get_fs_qbmask(sb, usb3);
	uspi->s_qfmask = ufs_get_fs_qfmask(sb, usb3);
	uspi->s_nrpos = fs32_to_cpu(sb, usb3->fs_nrpos);
	uspi->s_postbloff = fs32_to_cpu(sb, usb3->fs_postbloff);
	uspi->s_rotbloff = fs32_to_cpu(sb, usb3->fs_rotbloff);

	uspi->s_root_blocks = mul_u64_u32_div(uspi->s_dsize,
					      uspi->s_minfree, 100);
	if (uspi->s_minfree <= 5) {
		uspi->s_time_to_space = ~0ULL;
		uspi->s_space_to_time = 0;
		usb1->fs_optim = cpu_to_fs32(sb, UFS_OPTSPACE);
	} else {
		uspi->s_time_to_space = (uspi->s_root_blocks / 2) + 1;
		uspi->s_space_to_time = mul_u64_u32_div(uspi->s_dsize,
					      uspi->s_minfree - 2, 100) - 1;
	}

	/*
	 * Compute another frequently used values
	 */
	uspi->s_fpbmask = uspi->s_fpb - 1;
	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
		uspi->s_apbshift = uspi->s_bshift - 3;
	else
		uspi->s_apbshift = uspi->s_bshift - 2;

	uspi->s_2apbshift = uspi->s_apbshift * 2;
	uspi->s_3apbshift = uspi->s_apbshift * 3;
	uspi->s_apb = 1 << uspi->s_apbshift;
	uspi->s_2apb = 1 << uspi->s_2apbshift;
	uspi->s_3apb = 1 << uspi->s_3apbshift;
	uspi->s_apbmask = uspi->s_apb - 1;
	uspi->s_nspfshift = uspi->s_fshift - UFS_SECTOR_BITS;
	uspi->s_nspb = uspi->s_nspf << uspi->s_fpbshift;
	uspi->s_inopf = uspi->s_inopb >> uspi->s_fpbshift;
	uspi->s_bpf = uspi->s_fsize << 3;
	uspi->s_bpfshift = uspi->s_fshift + 3;
	uspi->s_bpfmask = uspi->s_bpf - 1;
	if ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_44BSD ||
	    (sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_UFS2)
		uspi->s_maxsymlinklen =
		    fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_maxsymlinklen);

	if (uspi->fs_magic == UFS2_MAGIC)
		maxsymlen = 2 * 4 * (UFS_NDADDR + UFS_NINDIR);
	else
		maxsymlen = 4 * (UFS_NDADDR + UFS_NINDIR);
	if (uspi->s_maxsymlinklen > maxsymlen) {
		ufs_warning(sb, __func__, "ufs_read_super: excessive maximum "
			    "fast symlink size (%u)\n", uspi->s_maxsymlinklen);
		uspi->s_maxsymlinklen = maxsymlen;
	}
	sb->s_maxbytes = ufs_max_bytes(sb);
	sb->s_max_links = UFS_LINK_MAX;

	inode = ufs_iget(sb, UFS_ROOTINO);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto failed;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto failed;
	}

	ufs_setup_cstotal(sb);
	/*
	 * Read cylinder group structures
	 */
	if (!sb_rdonly(sb))
		if (!ufs_read_cylinder_structures(sb))
			goto failed;

	UFSD("EXIT\n");
	return 0;

failed:
	if (ubh)
		ubh_brelse_uspi (uspi);
	kfree (uspi);
	kfree(sbi);
	sb->s_fs_info = NULL;
	UFSD("EXIT (FAILED)\n");
	return ret;

failed_nomem:
	UFSD("EXIT (NOMEM)\n");
	return -ENOMEM;
}

static int ufs_remount (struct super_block *sb, int *mount_flags, char *data)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned new_mount_opt, ufstype;
	unsigned flags;

	sync_filesystem(sb);
	mutex_lock(&UFS_SB(sb)->s_lock);
	uspi = UFS_SB(sb)->s_uspi;
	flags = UFS_SB(sb)->s_flags;
	usb1 = ubh_get_usb_first(uspi);
	usb3 = ubh_get_usb_third(uspi);
	
	/*
	 * Allow the "check" option to be passed as a remount option.
	 * It is not possible to change ufstype option during remount
	 */
	ufstype = UFS_SB(sb)->s_mount_opt & UFS_MOUNT_UFSTYPE;
	new_mount_opt = 0;
	ufs_set_opt (new_mount_opt, ONERROR_LOCK);
	if (!ufs_parse_options (data, &new_mount_opt)) {
		mutex_unlock(&UFS_SB(sb)->s_lock);
		return -EINVAL;
	}
	if (!(new_mount_opt & UFS_MOUNT_UFSTYPE)) {
		new_mount_opt |= ufstype;
	} else if ((new_mount_opt & UFS_MOUNT_UFSTYPE) != ufstype) {
		pr_err("ufstype can't be changed during remount\n");
		mutex_unlock(&UFS_SB(sb)->s_lock);
		return -EINVAL;
	}

	if ((bool)(*mount_flags & SB_RDONLY) == sb_rdonly(sb)) {
		UFS_SB(sb)->s_mount_opt = new_mount_opt;
		mutex_unlock(&UFS_SB(sb)->s_lock);
		return 0;
	}
	
	/*
	 * fs was mouted as rw, remounting ro
	 */
	if (*mount_flags & SB_RDONLY) {
		ufs_put_super_internal(sb);
		usb1->fs_time = ufs_get_seconds(sb);
		if ((flags & UFS_ST_MASK) == UFS_ST_SUN
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNOS
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNx86) 
			ufs_set_fs_state(sb, usb1, usb3,
				UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time));
		ubh_mark_buffer_dirty (USPI_UBH(uspi));
		sb->s_flags |= SB_RDONLY;
	} else {
	/*
	 * fs was mounted as ro, remounting rw
	 */
#ifndef CONFIG_UFS_FS_WRITE
		pr_err("ufs was compiled with read-only support, can't be mounted as read-write\n");
		mutex_unlock(&UFS_SB(sb)->s_lock);
		return -EINVAL;
#else
		if (ufstype != UFS_MOUNT_UFSTYPE_SUN && 
		    ufstype != UFS_MOUNT_UFSTYPE_SUNOS &&
		    ufstype != UFS_MOUNT_UFSTYPE_44BSD &&
		    ufstype != UFS_MOUNT_UFSTYPE_SUNx86 &&
		    ufstype != UFS_MOUNT_UFSTYPE_UFS2) {
			pr_err("this ufstype is read-only supported\n");
			mutex_unlock(&UFS_SB(sb)->s_lock);
			return -EINVAL;
		}
		if (!ufs_read_cylinder_structures(sb)) {
			pr_err("failed during remounting\n");
			mutex_unlock(&UFS_SB(sb)->s_lock);
			return -EPERM;
		}
		sb->s_flags &= ~SB_RDONLY;
#endif
	}
	UFS_SB(sb)->s_mount_opt = new_mount_opt;
	mutex_unlock(&UFS_SB(sb)->s_lock);
	return 0;
}

static int ufs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct ufs_sb_info *sbi = UFS_SB(root->d_sb);
	unsigned mval = sbi->s_mount_opt & UFS_MOUNT_UFSTYPE;
	const struct match_token *tp = tokens;

	while (tp->token != Opt_onerror_panic && tp->token != mval)
		++tp;
	BUG_ON(tp->token == Opt_onerror_panic);
	seq_printf(seq, ",%s", tp->pattern);

	mval = sbi->s_mount_opt & UFS_MOUNT_ONERROR;
	while (tp->token != Opt_err && tp->token != mval)
		++tp;
	BUG_ON(tp->token == Opt_err);
	seq_printf(seq, ",%s", tp->pattern);

	return 0;
}

static int ufs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ufs_sb_private_info *uspi= UFS_SB(sb)->s_uspi;
	unsigned  flags = UFS_SB(sb)->s_flags;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	mutex_lock(&UFS_SB(sb)->s_lock);
	
	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
		buf->f_type = UFS2_MAGIC;
	else
		buf->f_type = UFS_MAGIC;

	buf->f_blocks = uspi->s_dsize;
	buf->f_bfree = ufs_freefrags(uspi);
	buf->f_ffree = uspi->cs_total.cs_nifree;
	buf->f_bsize = sb->s_blocksize;
	buf->f_bavail = (buf->f_bfree > uspi->s_root_blocks)
		? (buf->f_bfree - uspi->s_root_blocks) : 0;
	buf->f_files = uspi->s_ncg * uspi->s_ipg;
	buf->f_namelen = UFS_MAXNAMLEN;
	buf->f_fsid = u64_to_fsid(id);

	mutex_unlock(&UFS_SB(sb)->s_lock);

	return 0;
}

static struct kmem_cache * ufs_inode_cachep;

static struct inode *ufs_alloc_inode(struct super_block *sb)
{
	struct ufs_inode_info *ei;

	ei = alloc_inode_sb(sb, ufs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;

	inode_set_iversion(&ei->vfs_inode, 1);
	seqlock_init(&ei->meta_lock);
	mutex_init(&ei->truncate_mutex);
	return &ei->vfs_inode;
}

static void ufs_free_in_core_inode(struct inode *inode)
{
	kmem_cache_free(ufs_inode_cachep, UFS_I(inode));
}

static void init_once(void *foo)
{
	struct ufs_inode_info *ei = (struct ufs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	ufs_inode_cachep = kmem_cache_create_usercopy("ufs_inode_cache",
				sizeof(struct ufs_inode_info), 0,
				(SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|
					SLAB_ACCOUNT),
				offsetof(struct ufs_inode_info, i_u1.i_symlink),
				sizeof_field(struct ufs_inode_info,
					i_u1.i_symlink),
				init_once);
	if (ufs_inode_cachep == NULL)
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
	kmem_cache_destroy(ufs_inode_cachep);
}

static const struct super_operations ufs_super_ops = {
	.alloc_inode	= ufs_alloc_inode,
	.free_inode	= ufs_free_in_core_inode,
	.write_inode	= ufs_write_inode,
	.evict_inode	= ufs_evict_inode,
	.put_super	= ufs_put_super,
	.sync_fs	= ufs_sync_fs,
	.statfs		= ufs_statfs,
	.remount_fs	= ufs_remount,
	.show_options   = ufs_show_options,
};

static struct dentry *ufs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, ufs_fill_super);
}

static struct file_system_type ufs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ufs",
	.mount		= ufs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ufs");

static int __init init_ufs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&ufs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_ufs_fs(void)
{
	unregister_filesystem(&ufs_fs_type);
	destroy_inodecache();
}

module_init(init_ufs_fs)
module_exit(exit_ufs_fs)
MODULE_LICENSE("GPL");
