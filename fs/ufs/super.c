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
 * Gertjan van Wingerde <gertjan@cs.vu.nl>
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


#include <linux/module.h>
#include <linux/bitops.h>

#include <stdarg.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/parser.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/log2.h>
#include <linux/mount.h>
#include <linux/seq_file.h>

#include "ufs.h"
#include "swab.h"
#include "util.h"

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

	printk("ufs_print_super_stuff\n");
	printk("  magic:     0x%x\n", magic);
	if (fs32_to_cpu(sb, usb3->fs_magic) == UFS2_MAGIC) {
		printk("  fs_size:   %llu\n", (unsigned long long)
		       fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_size));
		printk("  fs_dsize:  %llu\n", (unsigned long long)
		       fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize));
		printk("  bsize:         %u\n",
		       fs32_to_cpu(sb, usb1->fs_bsize));
		printk("  fsize:         %u\n",
		       fs32_to_cpu(sb, usb1->fs_fsize));
		printk("  fs_volname:  %s\n", usb2->fs_un.fs_u2.fs_volname);
		printk("  fs_sblockloc: %llu\n", (unsigned long long)
		       fs64_to_cpu(sb, usb2->fs_un.fs_u2.fs_sblockloc));
		printk("  cs_ndir(No of dirs):  %llu\n", (unsigned long long)
		       fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_ndir));
		printk("  cs_nbfree(No of free blocks):  %llu\n",
		       (unsigned long long)
		       fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_nbfree));
		printk(KERN_INFO"  cs_nifree(Num of free inodes): %llu\n",
		       (unsigned long long)
		       fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nifree));
		printk(KERN_INFO"  cs_nffree(Num of free frags): %llu\n",
		       (unsigned long long)
		       fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nffree));
	} else {
		printk(" sblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_sblkno));
		printk(" cblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_cblkno));
		printk(" iblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_iblkno));
		printk(" dblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_dblkno));
		printk(" cgoffset:    %u\n",
		       fs32_to_cpu(sb, usb1->fs_cgoffset));
		printk(" ~cgmask:     0x%x\n",
		       ~fs32_to_cpu(sb, usb1->fs_cgmask));
		printk(" size:        %u\n", fs32_to_cpu(sb, usb1->fs_size));
		printk(" dsize:       %u\n", fs32_to_cpu(sb, usb1->fs_dsize));
		printk(" ncg:         %u\n", fs32_to_cpu(sb, usb1->fs_ncg));
		printk(" bsize:       %u\n", fs32_to_cpu(sb, usb1->fs_bsize));
		printk(" fsize:       %u\n", fs32_to_cpu(sb, usb1->fs_fsize));
		printk(" frag:        %u\n", fs32_to_cpu(sb, usb1->fs_frag));
		printk(" fragshift:   %u\n",
		       fs32_to_cpu(sb, usb1->fs_fragshift));
		printk(" ~fmask:      %u\n", ~fs32_to_cpu(sb, usb1->fs_fmask));
		printk(" fshift:      %u\n", fs32_to_cpu(sb, usb1->fs_fshift));
		printk(" sbsize:      %u\n", fs32_to_cpu(sb, usb1->fs_sbsize));
		printk(" spc:         %u\n", fs32_to_cpu(sb, usb1->fs_spc));
		printk(" cpg:         %u\n", fs32_to_cpu(sb, usb1->fs_cpg));
		printk(" ipg:         %u\n", fs32_to_cpu(sb, usb1->fs_ipg));
		printk(" fpg:         %u\n", fs32_to_cpu(sb, usb1->fs_fpg));
		printk(" csaddr:      %u\n", fs32_to_cpu(sb, usb1->fs_csaddr));
		printk(" cssize:      %u\n", fs32_to_cpu(sb, usb1->fs_cssize));
		printk(" cgsize:      %u\n", fs32_to_cpu(sb, usb1->fs_cgsize));
		printk(" fstodb:      %u\n",
		       fs32_to_cpu(sb, usb1->fs_fsbtodb));
		printk(" nrpos:       %u\n", fs32_to_cpu(sb, usb3->fs_nrpos));
		printk(" ndir         %u\n",
		       fs32_to_cpu(sb, usb1->fs_cstotal.cs_ndir));
		printk(" nifree       %u\n",
		       fs32_to_cpu(sb, usb1->fs_cstotal.cs_nifree));
		printk(" nbfree       %u\n",
		       fs32_to_cpu(sb, usb1->fs_cstotal.cs_nbfree));
		printk(" nffree       %u\n",
		       fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree));
	}
	printk("\n");
}

/*
 * Print contents of ufs_cylinder_group, useful for debugging
 */
static void ufs_print_cylinder_stuff(struct super_block *sb,
				     struct ufs_cylinder_group *cg)
{
	printk("\nufs_print_cylinder_stuff\n");
	printk("size of ucg: %zu\n", sizeof(struct ufs_cylinder_group));
	printk("  magic:        %x\n", fs32_to_cpu(sb, cg->cg_magic));
	printk("  time:         %u\n", fs32_to_cpu(sb, cg->cg_time));
	printk("  cgx:          %u\n", fs32_to_cpu(sb, cg->cg_cgx));
	printk("  ncyl:         %u\n", fs16_to_cpu(sb, cg->cg_ncyl));
	printk("  niblk:        %u\n", fs16_to_cpu(sb, cg->cg_niblk));
	printk("  ndblk:        %u\n", fs32_to_cpu(sb, cg->cg_ndblk));
	printk("  cs_ndir:      %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_ndir));
	printk("  cs_nbfree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nbfree));
	printk("  cs_nifree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nifree));
	printk("  cs_nffree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nffree));
	printk("  rotor:        %u\n", fs32_to_cpu(sb, cg->cg_rotor));
	printk("  frotor:       %u\n", fs32_to_cpu(sb, cg->cg_frotor));
	printk("  irotor:       %u\n", fs32_to_cpu(sb, cg->cg_irotor));
	printk("  frsum:        %u, %u, %u, %u, %u, %u, %u, %u\n",
	    fs32_to_cpu(sb, cg->cg_frsum[0]), fs32_to_cpu(sb, cg->cg_frsum[1]),
	    fs32_to_cpu(sb, cg->cg_frsum[2]), fs32_to_cpu(sb, cg->cg_frsum[3]),
	    fs32_to_cpu(sb, cg->cg_frsum[4]), fs32_to_cpu(sb, cg->cg_frsum[5]),
	    fs32_to_cpu(sb, cg->cg_frsum[6]), fs32_to_cpu(sb, cg->cg_frsum[7]));
	printk("  btotoff:      %u\n", fs32_to_cpu(sb, cg->cg_btotoff));
	printk("  boff:         %u\n", fs32_to_cpu(sb, cg->cg_boff));
	printk("  iuseoff:      %u\n", fs32_to_cpu(sb, cg->cg_iusedoff));
	printk("  freeoff:      %u\n", fs32_to_cpu(sb, cg->cg_freeoff));
	printk("  nextfreeoff:  %u\n", fs32_to_cpu(sb, cg->cg_nextfreeoff));
	printk("  clustersumoff %u\n",
	       fs32_to_cpu(sb, cg->cg_u.cg_44.cg_clustersumoff));
	printk("  clusteroff    %u\n",
	       fs32_to_cpu(sb, cg->cg_u.cg_44.cg_clusteroff));
	printk("  nclusterblks  %u\n",
	       fs32_to_cpu(sb, cg->cg_u.cg_44.cg_nclusterblks));
	printk("\n");
}
#else
#  define ufs_print_super_stuff(sb, usb1, usb2, usb3) /**/
#  define ufs_print_cylinder_stuff(sb, cg) /**/
#endif /* CONFIG_UFS_DEBUG */

static const struct super_operations ufs_super_ops;

static char error_buf[1024];

void ufs_error (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	va_list args;

	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH(uspi));
		sb->s_dirt = 1;
		sb->s_flags |= MS_RDONLY;
	}
	va_start (args, fmt);
	vsnprintf (error_buf, sizeof(error_buf), fmt, args);
	va_end (args);
	switch (UFS_SB(sb)->s_mount_opt & UFS_MOUNT_ONERROR) {
	case UFS_MOUNT_ONERROR_PANIC:
		panic ("UFS-fs panic (device %s): %s: %s\n", 
			sb->s_id, function, error_buf);

	case UFS_MOUNT_ONERROR_LOCK:
	case UFS_MOUNT_ONERROR_UMOUNT:
	case UFS_MOUNT_ONERROR_REPAIR:
		printk (KERN_CRIT "UFS-fs error (device %s): %s: %s\n",
			sb->s_id, function, error_buf);
	}		
}

void ufs_panic (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	va_list args;
	
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH(uspi));
		sb->s_dirt = 1;
	}
	va_start (args, fmt);
	vsnprintf (error_buf, sizeof(error_buf), fmt, args);
	va_end (args);
	sb->s_flags |= MS_RDONLY;
	printk (KERN_CRIT "UFS-fs panic (device %s): %s: %s\n",
		sb->s_id, function, error_buf);
}

void ufs_warning (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vsnprintf (error_buf, sizeof(error_buf), fmt, args);
	va_end (args);
	printk (KERN_WARNING "UFS-fs warning (device %s): %s: %s\n",
		sb->s_id, function, error_buf);
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

static match_table_t tokens = {
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
			printk("UFS-fs: Unable to do repair on error, "
				"will lock lock instead\n");
			ufs_clear_opt (*mount_options, ONERROR);
			ufs_set_opt (*mount_options, ONERROR_REPAIR);
			break;
		default:
			printk("UFS-fs: Invalid option: \"%s\" "
					"or missing value\n", p);
			return 0;
		}
	}
	return 1;
}

/*
 * Diffrent types of UFS hold fs_cstotal in different
 * places, and use diffrent data structure for it.
 * To make things simplier we just copy fs_cstotal to ufs_sb_private_info
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
	     (usb1->fs_flags & UFS_FLAGS_UPDATED)) ||
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
	struct ufs_super_block_third *usb3;

	UFSD("ENTER\n");

	usb3 = ubh_get_usb_third(uspi);
	/*
	 * Read cs structures from (usually) first data block
	 * on the device. 
	 */
	size = uspi->s_cssize;
	blks = (size + uspi->s_fsize - 1) >> uspi->s_fshift;
	base = space = kmalloc(size, GFP_KERNEL);
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
	if (!(sbi->s_ucg = kmalloc (sizeof(struct buffer_head *) * uspi->s_ncg, GFP_KERNEL)))
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
		if (!(sbi->s_ucpi[i] = kmalloc (sizeof(struct ufs_cg_private_info), GFP_KERNEL)))
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

	if ((mtype == UFS_MOUNT_UFSTYPE_44BSD &&
	     (usb1->fs_flags & UFS_FLAGS_UPDATED)) ||
	    mtype == UFS_MOUNT_UFSTYPE_UFS2) {
		/*we have statistic in different place, then usual*/
		usb2->fs_un.fs_u2.cs_ndir =
			cpu_to_fs64(sb, uspi->cs_total.cs_ndir);
		usb2->fs_un.fs_u2.cs_nbfree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nbfree);
		usb3->fs_un1.fs_u2.cs_nifree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nifree);
		usb3->fs_un1.fs_u2.cs_nffree =
			cpu_to_fs64(sb, uspi->cs_total.cs_nffree);
	} else {
		usb1->fs_cstotal.cs_ndir =
			cpu_to_fs32(sb, uspi->cs_total.cs_ndir);
		usb1->fs_cstotal.cs_nbfree =
			cpu_to_fs32(sb, uspi->cs_total.cs_nbfree);
		usb1->fs_cstotal.cs_nifree =
			cpu_to_fs32(sb, uspi->cs_total.cs_nifree);
		usb1->fs_cstotal.cs_nffree =
			cpu_to_fs32(sb, uspi->cs_total.cs_nffree);
	}
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

	uspi = NULL;
	ubh = NULL;
	flags = 0;
	
	UFSD("ENTER\n");
		
	sbi = kzalloc(sizeof(struct ufs_sb_info), GFP_KERNEL);
	if (!sbi)
		goto failed_nomem;
	sb->s_fs_info = sbi;

	UFSD("flag %u\n", (int)(sb->s_flags & MS_RDONLY));
	
#ifndef CONFIG_UFS_FS_WRITE
	if (!(sb->s_flags & MS_RDONLY)) {
		printk("ufs was compiled with read-only support, "
		"can't be mounted as read-write\n");
		goto failed;
	}
#endif
	/*
	 * Set default mount options
	 * Parse mount options
	 */
	sbi->s_mount_opt = 0;
	ufs_set_opt (sbi->s_mount_opt, ONERROR_LOCK);
	if (!ufs_parse_options ((char *) data, &sbi->s_mount_opt)) {
		printk("wrong mount options\n");
		goto failed;
	}
	if (!(sbi->s_mount_opt & UFS_MOUNT_UFSTYPE)) {
		if (!silent)
			printk("You didn't specify the type of your ufs filesystem\n\n"
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

	/* Keep 2Gig file limit. Some UFS variants need to override 
	   this but as I don't know which I'll let those in the know loosen
	   the rules */
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
		UFSD(("ufstype=sunos\n"))
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
		if (!(sb->s_flags & MS_RDONLY)) {
			if (!silent)
				printk(KERN_INFO "ufstype=old is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_NEXTSTEP:
		/*TODO: check may be we need set special dir block size?*/
		UFSD("ufstype=nextstep\n");
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!(sb->s_flags & MS_RDONLY)) {
			if (!silent)
				printk(KERN_INFO "ufstype=nextstep is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_NEXTSTEP_CD:
		/*TODO: check may be we need set special dir block size?*/
		UFSD("ufstype=nextstep-cd\n");
		uspi->s_fsize = block_size = 2048;
		uspi->s_fmask = ~(2048 - 1);
		uspi->s_fshift = 11;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!(sb->s_flags & MS_RDONLY)) {
			if (!silent)
				printk(KERN_INFO "ufstype=nextstep-cd is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
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
		if (!(sb->s_flags & MS_RDONLY)) {
			if (!silent)
				printk(KERN_INFO "ufstype=openstep is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
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
		if (!(sb->s_flags & MS_RDONLY)) {
			if (!silent)
				printk(KERN_INFO "ufstype=hp is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
 		}
 		break;
	default:
		if (!silent)
			printk("unknown ufstype\n");
		goto failed;
	}
	
again:	
	if (!sb_set_blocksize(sb, block_size)) {
		printk(KERN_ERR "UFS: failed to set blocksize\n");
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

	/*
	 * Check ufs magic number
	 */
	sbi->s_bytesex = BYTESEX_LE;
	switch ((uspi->fs_magic = fs32_to_cpu(sb, usb3->fs_magic))) {
		case UFS_MAGIC:
		case UFS2_MAGIC:
		case UFS_MAGIC_LFN:
	        case UFS_MAGIC_FEA:
	        case UFS_MAGIC_4GB:
			goto magic_found;
	}
	sbi->s_bytesex = BYTESEX_BE;
	switch ((uspi->fs_magic = fs32_to_cpu(sb, usb3->fs_magic))) {
		case UFS_MAGIC:
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
		printk("ufs_read_super: bad magic number\n");
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
		printk(KERN_ERR "ufs_read_super: fragment size %u is not a power of 2\n",
			uspi->s_fsize);
			goto failed;
	}
	if (uspi->s_fsize < 512) {
		printk(KERN_ERR "ufs_read_super: fragment size %u is too small\n",
			uspi->s_fsize);
		goto failed;
	}
	if (uspi->s_fsize > 4096) {
		printk(KERN_ERR "ufs_read_super: fragment size %u is too large\n",
			uspi->s_fsize);
		goto failed;
	}
	if (!is_power_of_2(uspi->s_bsize)) {
		printk(KERN_ERR "ufs_read_super: block size %u is not a power of 2\n",
			uspi->s_bsize);
		goto failed;
	}
	if (uspi->s_bsize < 4096) {
		printk(KERN_ERR "ufs_read_super: block size %u is too small\n",
			uspi->s_bsize);
		goto failed;
	}
	if (uspi->s_bsize / uspi->s_fsize > 8) {
		printk(KERN_ERR "ufs_read_super: too many fragments per block (%u)\n",
			uspi->s_bsize / uspi->s_fsize);
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

	/* Set sbi->s_flags here, used by ufs_get_fs_state() below */
	sbi->s_flags = flags;
	ufs_print_super_stuff(sb, usb1, usb2, usb3);

	/*
	 * Check, if file system was correctly unmounted.
	 * If not, make it read only.
	 */
	if ((((flags & UFS_ST_MASK) == UFS_ST_44BSD)	||
	     ((flags & UFS_ST_MASK) == UFS_ST_OLD)	||
	     ((flags & UFS_ST_MASK) == UFS_ST_SUN)	||
	     ((flags & UFS_ST_MASK) == UFS_ST_SUNOS)	||
	     ((flags & UFS_ST_MASK) == UFS_ST_SUNx86))	&&
	    (ufs_get_fs_state(sb, usb1, usb3) == (UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time)))) {
		switch(usb1->fs_clean) {
		case UFS_FSCLEAN:
			UFSD("fs is clean\n");
			break;
		case UFS_FSSTABLE:
			UFSD("fs is stable\n");
			break;
		case UFS_FSOSF1:
			UFSD("fs is DEC OSF/1\n");
			break;
		case UFS_FSACTIVE:
			printk("ufs_read_super: fs is active\n");
			sb->s_flags |= MS_RDONLY;
			break;
		case UFS_FSBAD:
			printk("ufs_read_super: fs is bad\n");
			sb->s_flags |= MS_RDONLY;
			break;
		default:
			printk("ufs_read_super: can't grok fs_clean 0x%x\n", usb1->fs_clean);
			sb->s_flags |= MS_RDONLY;
			break;
		}
	} else {
		printk("ufs_read_super: fs needs fsck\n");
		sb->s_flags |= MS_RDONLY;
	}

	/*
	 * Read ufs_super_block into internal data structures
	 */
	sb->s_op = &ufs_super_ops;
	sb->dq_op = NULL; /***/
	sb->s_magic = fs32_to_cpu(sb, usb3->fs_magic);

	uspi->s_sblkno = fs32_to_cpu(sb, usb1->fs_sblkno);
	uspi->s_cblkno = fs32_to_cpu(sb, usb1->fs_cblkno);
	uspi->s_iblkno = fs32_to_cpu(sb, usb1->fs_iblkno);
	uspi->s_dblkno = fs32_to_cpu(sb, usb1->fs_dblkno);
	uspi->s_cgoffset = fs32_to_cpu(sb, usb1->fs_cgoffset);
	uspi->s_cgmask = fs32_to_cpu(sb, usb1->fs_cgmask);

	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
		uspi->s_u2_size  = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_size);
		uspi->s_u2_dsize = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize);
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
	if ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) ==
	    UFS_MOUNT_UFSTYPE_44BSD)
		uspi->s_maxsymlinklen =
		    fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_maxsymlinklen);

	inode = iget(sb, UFS_ROOTINO);
	if (!inode || is_bad_inode(inode))
		goto failed;
	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root)
		goto dalloc_failed;

	ufs_setup_cstotal(sb);
	/*
	 * Read cylinder group structures
	 */
	if (!(sb->s_flags & MS_RDONLY))
		if (!ufs_read_cylinder_structures(sb))
			goto failed;

	UFSD("EXIT\n");
	return 0;

dalloc_failed:
	iput(inode);
failed:
	if (ubh)
		ubh_brelse_uspi (uspi);
	kfree (uspi);
	kfree(sbi);
	sb->s_fs_info = NULL;
	UFSD("EXIT (FAILED)\n");
	return -EINVAL;

failed_nomem:
	UFSD("EXIT (NOMEM)\n");
	return -ENOMEM;
}

static void ufs_write_super(struct super_block *sb)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned flags;

	lock_kernel();
	UFSD("ENTER\n");
	flags = UFS_SB(sb)->s_flags;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	usb3 = ubh_get_usb_third(uspi);

	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_time = cpu_to_fs32(sb, get_seconds());
		if ((flags & UFS_ST_MASK) == UFS_ST_SUN 
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNOS
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNx86)
			ufs_set_fs_state(sb, usb1, usb3,
					UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time));
		ufs_put_cstotal(sb);
	}
	sb->s_dirt = 0;
	UFSD("EXIT\n");
	unlock_kernel();
}

static void ufs_put_super(struct super_block *sb)
{
	struct ufs_sb_info * sbi = UFS_SB(sb);
		
	UFSD("ENTER\n");

	if (!(sb->s_flags & MS_RDONLY))
		ufs_put_super_internal(sb);
	
	ubh_brelse_uspi (sbi->s_uspi);
	kfree (sbi->s_uspi);
	kfree (sbi);
	sb->s_fs_info = NULL;
	UFSD("EXIT\n");
	return;
}


static int ufs_remount (struct super_block *sb, int *mount_flags, char *data)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned new_mount_opt, ufstype;
	unsigned flags;
	
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
	if (!ufs_parse_options (data, &new_mount_opt))
		return -EINVAL;
	if (!(new_mount_opt & UFS_MOUNT_UFSTYPE)) {
		new_mount_opt |= ufstype;
	} else if ((new_mount_opt & UFS_MOUNT_UFSTYPE) != ufstype) {
		printk("ufstype can't be changed during remount\n");
		return -EINVAL;
	}

	if ((*mount_flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY)) {
		UFS_SB(sb)->s_mount_opt = new_mount_opt;
		return 0;
	}
	
	/*
	 * fs was mouted as rw, remounting ro
	 */
	if (*mount_flags & MS_RDONLY) {
		ufs_put_super_internal(sb);
		usb1->fs_time = cpu_to_fs32(sb, get_seconds());
		if ((flags & UFS_ST_MASK) == UFS_ST_SUN
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNOS
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNx86) 
			ufs_set_fs_state(sb, usb1, usb3,
				UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time));
		ubh_mark_buffer_dirty (USPI_UBH(uspi));
		sb->s_dirt = 0;
		sb->s_flags |= MS_RDONLY;
	} else {
	/*
	 * fs was mounted as ro, remounting rw
	 */
#ifndef CONFIG_UFS_FS_WRITE
		printk("ufs was compiled with read-only support, "
		"can't be mounted as read-write\n");
		return -EINVAL;
#else
		if (ufstype != UFS_MOUNT_UFSTYPE_SUN && 
		    ufstype != UFS_MOUNT_UFSTYPE_SUNOS &&
		    ufstype != UFS_MOUNT_UFSTYPE_44BSD &&
		    ufstype != UFS_MOUNT_UFSTYPE_SUNx86 &&
		    ufstype != UFS_MOUNT_UFSTYPE_UFS2) {
			printk("this ufstype is read-only supported\n");
			return -EINVAL;
		}
		if (!ufs_read_cylinder_structures(sb)) {
			printk("failed during remounting\n");
			return -EPERM;
		}
		sb->s_flags &= ~MS_RDONLY;
#endif
	}
	UFS_SB(sb)->s_mount_opt = new_mount_opt;
	return 0;
}

static int ufs_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct ufs_sb_info *sbi = UFS_SB(vfs->mnt_sb);
	unsigned mval = sbi->s_mount_opt & UFS_MOUNT_UFSTYPE;
	struct match_token *tp = tokens;

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
	struct ufs_super_block_first *usb1;
	struct ufs_super_block_second *usb2;
	struct ufs_super_block_third *usb3;

	lock_kernel();

	usb1 = ubh_get_usb_first(uspi);
	usb2 = ubh_get_usb_second(uspi);
	usb3 = ubh_get_usb_third(uspi);
	
	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
		buf->f_type = UFS2_MAGIC;
		buf->f_blocks = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize);
	} else {
		buf->f_type = UFS_MAGIC;
		buf->f_blocks = uspi->s_dsize;
	}
	buf->f_bfree = ufs_blkstofrags(uspi->cs_total.cs_nbfree) +
		uspi->cs_total.cs_nffree;
	buf->f_ffree = uspi->cs_total.cs_nifree;
	buf->f_bsize = sb->s_blocksize;
	buf->f_bavail = (buf->f_bfree > (((long)buf->f_blocks / 100) * uspi->s_minfree))
		? (buf->f_bfree - (((long)buf->f_blocks / 100) * uspi->s_minfree)) : 0;
	buf->f_files = uspi->s_ncg * uspi->s_ipg;
	buf->f_namelen = UFS_MAXNAMLEN;

	unlock_kernel();

	return 0;
}

static struct kmem_cache * ufs_inode_cachep;

static struct inode *ufs_alloc_inode(struct super_block *sb)
{
	struct ufs_inode_info *ei;
	ei = (struct ufs_inode_info *)kmem_cache_alloc(ufs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	ei->vfs_inode.i_version = 1;
	return &ei->vfs_inode;
}

static void ufs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(ufs_inode_cachep, UFS_I(inode));
}

static void init_once(struct kmem_cache * cachep, void *foo)
{
	struct ufs_inode_info *ei = (struct ufs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	ufs_inode_cachep = kmem_cache_create("ufs_inode_cache",
					     sizeof(struct ufs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (ufs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(ufs_inode_cachep);
}

#ifdef CONFIG_QUOTA
static ssize_t ufs_quota_read(struct super_block *, int, char *,size_t, loff_t);
static ssize_t ufs_quota_write(struct super_block *, int, const char *, size_t, loff_t);
#endif

static const struct super_operations ufs_super_ops = {
	.alloc_inode	= ufs_alloc_inode,
	.destroy_inode	= ufs_destroy_inode,
	.read_inode	= ufs_read_inode,
	.write_inode	= ufs_write_inode,
	.delete_inode	= ufs_delete_inode,
	.put_super	= ufs_put_super,
	.write_super	= ufs_write_super,
	.statfs		= ufs_statfs,
	.remount_fs	= ufs_remount,
	.show_options   = ufs_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ufs_quota_read,
	.quota_write	= ufs_quota_write,
#endif
};

#ifdef CONFIG_QUOTA

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and noone else should touch the files)
 * we don't have to be afraid of races */
static ssize_t ufs_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t toread;
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

		bh = ufs_bread(inode, blk, 0, &err);
		if (err)
			return err;
		if (!bh)	/* A hole? */
			memset(data, 0, tocopy);
		else {
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
static ssize_t ufs_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> sb->s_blocksize_bits;
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t towrite = len;
	struct buffer_head *bh;

	mutex_lock_nested(&inode->i_mutex, I_MUTEX_QUOTA);
	while (towrite > 0) {
		tocopy = sb->s_blocksize - offset < towrite ?
				sb->s_blocksize - offset : towrite;

		bh = ufs_bread(inode, blk, 1, &err);
		if (!bh)
			goto out;
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
	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	mutex_unlock(&inode->i_mutex);
	return len - towrite;
}

#endif

static int ufs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, ufs_fill_super, mnt);
}

static struct file_system_type ufs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ufs",
	.get_sb		= ufs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

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
