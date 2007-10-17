/*
 *  linux/fs/ufs/ialloc.c
 *
 * Copyright (c) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by 
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 * UFS2 write support added by
 * Evgeniy Dushistov <dushistov@mail.ru>, 2007
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

#include "ufs.h"
#include "swab.h"
#include "util.h"

/*
 * NOTE! When we get the inode, we're the only people
 * that have access to it, and as such there are no
 * race conditions we have to worry about. The inode
 * is not on the hash-lists, and it cannot be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 *
 * HOWEVER: we must make sure that we get no aliases,
 * which means that we have to call "clear_inode()"
 * _before_ we mark the inode not in use in the inode
 * bitmaps. Otherwise a newly created file might use
 * the same inode number (not actually the same pointer
 * though), and then we'd have two inodes sharing the
 * same inode number and space on the harddisk.
 */
void ufs_free_inode (struct inode * inode)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	int is_directory;
	unsigned ino, cg, bit;
	
	UFSD("ENTER, ino %lu\n", inode->i_ino);

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	usb1 = ubh_get_usb_first(uspi);
	
	ino = inode->i_ino;

	lock_super (sb);

	if (!((ino > 1) && (ino < (uspi->s_ncg * uspi->s_ipg )))) {
		ufs_warning(sb, "ufs_free_inode", "reserved inode or nonexistent inode %u\n", ino);
		unlock_super (sb);
		return;
	}
	
	cg = ufs_inotocg (ino);
	bit = ufs_inotocgoff (ino);
	ucpi = ufs_load_cylinder (sb, cg);
	if (!ucpi) {
		unlock_super (sb);
		return;
	}
	ucg = ubh_get_ucg(UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg))
		ufs_panic (sb, "ufs_free_fragments", "internal error, bad cg magic number");

	ucg->cg_time = cpu_to_fs32(sb, get_seconds());

	is_directory = S_ISDIR(inode->i_mode);

	DQUOT_FREE_INODE(inode);
	DQUOT_DROP(inode);

	clear_inode (inode);

	if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit))
		ufs_error(sb, "ufs_free_inode", "bit already cleared for inode %u", ino);
	else {
		ubh_clrbit (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit);
		if (ino < ucpi->c_irotor)
			ucpi->c_irotor = ino;
		fs32_add(sb, &ucg->cg_cs.cs_nifree, 1);
		uspi->cs_total.cs_nifree++;
		fs32_add(sb, &UFS_SB(sb)->fs_cs(cg).cs_nifree, 1);

		if (is_directory) {
			fs32_sub(sb, &ucg->cg_cs.cs_ndir, 1);
			uspi->cs_total.cs_ndir--;
			fs32_sub(sb, &UFS_SB(sb)->fs_cs(cg).cs_ndir, 1);
		}
	}

	ubh_mark_buffer_dirty (USPI_UBH(uspi));
	ubh_mark_buffer_dirty (UCPI_UBH(ucpi));
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block(SWRITE, UCPI_UBH(ucpi));
		ubh_wait_on_buffer (UCPI_UBH(ucpi));
	}
	
	sb->s_dirt = 1;
	unlock_super (sb);
	UFSD("EXIT\n");
}

/*
 * Nullify new chunk of inodes,
 * BSD people also set ui_gen field of inode
 * during nullification, but we not care about
 * that because of linux ufs do not support NFS
 */
static void ufs2_init_inodes_chunk(struct super_block *sb,
				   struct ufs_cg_private_info *ucpi,
				   struct ufs_cylinder_group *ucg)
{
	struct buffer_head *bh;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	sector_t beg = uspi->s_sbbase +
		ufs_inotofsba(ucpi->c_cgx * uspi->s_ipg +
			      fs32_to_cpu(sb, ucg->cg_u.cg_u2.cg_initediblk));
	sector_t end = beg + uspi->s_fpb;

	UFSD("ENTER cgno %d\n", ucpi->c_cgx);

	for (; beg < end; ++beg) {
		bh = sb_getblk(sb, beg);
		lock_buffer(bh);
		memset(bh->b_data, 0, sb->s_blocksize);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (sb->s_flags & MS_SYNCHRONOUS)
			sync_dirty_buffer(bh);
		brelse(bh);
	}

	fs32_add(sb, &ucg->cg_u.cg_u2.cg_initediblk, uspi->s_inopb);
	ubh_mark_buffer_dirty(UCPI_UBH(ucpi));
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block(SWRITE, UCPI_UBH(ucpi));
		ubh_wait_on_buffer(UCPI_UBH(ucpi));
	}

	UFSD("EXIT\n");
}

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory's block
 * group to find a free inode.
 */
struct inode * ufs_new_inode(struct inode * dir, int mode)
{
	struct super_block * sb;
	struct ufs_sb_info * sbi;
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	struct inode * inode;
	unsigned cg, bit, i, j, start;
	struct ufs_inode_info *ufsi;
	int err = -ENOSPC;

	UFSD("ENTER\n");
	
	/* Cannot create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return ERR_PTR(-EPERM);
	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	ufsi = UFS_I(inode);
	sbi = UFS_SB(sb);
	uspi = sbi->s_uspi;
	usb1 = ubh_get_usb_first(uspi);

	lock_super (sb);

	/*
	 * Try to place the inode in its parent directory
	 */
	i = ufs_inotocg(dir->i_ino);
	if (sbi->fs_cs(i).cs_nifree) {
		cg = i;
		goto cg_found;
	}

	/*
	 * Use a quadratic hash to find a group with a free inode
	 */
	for ( j = 1; j < uspi->s_ncg; j <<= 1 ) {
		i += j;
		if (i >= uspi->s_ncg)
			i -= uspi->s_ncg;
		if (sbi->fs_cs(i).cs_nifree) {
			cg = i;
			goto cg_found;
		}
	}

	/*
	 * That failed: try linear search for a free inode
	 */
	i = ufs_inotocg(dir->i_ino) + 1;
	for (j = 2; j < uspi->s_ncg; j++) {
		i++;
		if (i >= uspi->s_ncg)
			i = 0;
		if (sbi->fs_cs(i).cs_nifree) {
			cg = i;
			goto cg_found;
		}
	}

	goto failed;

cg_found:
	ucpi = ufs_load_cylinder (sb, cg);
	if (!ucpi) {
		err = -EIO;
		goto failed;
	}
	ucg = ubh_get_ucg(UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg)) 
		ufs_panic (sb, "ufs_new_inode", "internal error, bad cg magic number");

	start = ucpi->c_irotor;
	bit = ubh_find_next_zero_bit (UCPI_UBH(ucpi), ucpi->c_iusedoff, uspi->s_ipg, start);
	if (!(bit < uspi->s_ipg)) {
		bit = ubh_find_first_zero_bit (UCPI_UBH(ucpi), ucpi->c_iusedoff, start);
		if (!(bit < start)) {
			ufs_error (sb, "ufs_new_inode",
			    "cylinder group %u corrupted - error in inode bitmap\n", cg);
			err = -EIO;
			goto failed;
		}
	}
	UFSD("start = %u, bit = %u, ipg = %u\n", start, bit, uspi->s_ipg);
	if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit))
		ubh_setbit (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit);
	else {
		ufs_panic (sb, "ufs_new_inode", "internal error");
		err = -EIO;
		goto failed;
	}

	if (uspi->fs_magic == UFS2_MAGIC) {
		u32 initediblk = fs32_to_cpu(sb, ucg->cg_u.cg_u2.cg_initediblk);

		if (bit + uspi->s_inopb > initediblk &&
		    initediblk < fs32_to_cpu(sb, ucg->cg_u.cg_u2.cg_niblk))
			ufs2_init_inodes_chunk(sb, ucpi, ucg);
	}

	fs32_sub(sb, &ucg->cg_cs.cs_nifree, 1);
	uspi->cs_total.cs_nifree--;
	fs32_sub(sb, &sbi->fs_cs(cg).cs_nifree, 1);
	
	if (S_ISDIR(mode)) {
		fs32_add(sb, &ucg->cg_cs.cs_ndir, 1);
		uspi->cs_total.cs_ndir++;
		fs32_add(sb, &sbi->fs_cs(cg).cs_ndir, 1);
	}
	ubh_mark_buffer_dirty (USPI_UBH(uspi));
	ubh_mark_buffer_dirty (UCPI_UBH(ucpi));
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ubh_ll_rw_block(SWRITE, UCPI_UBH(ucpi));
		ubh_wait_on_buffer (UCPI_UBH(ucpi));
	}
	sb->s_dirt = 1;

	inode->i_ino = cg * uspi->s_ipg + bit;
	inode->i_mode = mode;
	inode->i_uid = current->fsuid;
	if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			inode->i_mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;

	inode->i_blocks = 0;
	inode->i_generation = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	ufsi->i_flags = UFS_I(dir)->i_flags;
	ufsi->i_lastfrag = 0;
	ufsi->i_shadow = 0;
	ufsi->i_osync = 0;
	ufsi->i_oeftflag = 0;
	ufsi->i_dir_start_lookup = 0;
	memset(&ufsi->i_u1, 0, sizeof(ufsi->i_u1));
	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	if (uspi->fs_magic == UFS2_MAGIC) {
		struct buffer_head *bh;
		struct ufs2_inode *ufs2_inode;

		/*
		 * setup birth date, we do it here because of there is no sense
		 * to hold it in struct ufs_inode_info, and lose 64 bit
		 */
		bh = sb_bread(sb, uspi->s_sbbase + ufs_inotofsba(inode->i_ino));
		if (!bh) {
			ufs_warning(sb, "ufs_read_inode",
				    "unable to read inode %lu\n",
				    inode->i_ino);
			err = -EIO;
			goto fail_remove_inode;
		}
		lock_buffer(bh);
		ufs2_inode = (struct ufs2_inode *)bh->b_data;
		ufs2_inode += ufs_inotofsbo(inode->i_ino);
		ufs2_inode->ui_birthtime = cpu_to_fs64(sb, CURRENT_TIME.tv_sec);
		ufs2_inode->ui_birthnsec = cpu_to_fs32(sb, CURRENT_TIME.tv_nsec);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (sb->s_flags & MS_SYNCHRONOUS)
			sync_dirty_buffer(bh);
		brelse(bh);
	}

	unlock_super (sb);

	if (DQUOT_ALLOC_INODE(inode)) {
		DQUOT_DROP(inode);
		err = -EDQUOT;
		goto fail_without_unlock;
	}

	UFSD("allocating inode %lu\n", inode->i_ino);
	UFSD("EXIT\n");
	return inode;

fail_remove_inode:
	unlock_super(sb);
fail_without_unlock:
	inode->i_flags |= S_NOQUOTA;
	inode->i_nlink = 0;
	iput(inode);
	UFSD("EXIT (FAILED): err %d\n", err);
	return ERR_PTR(err);
failed:
	unlock_super (sb);
	make_bad_inode(inode);
	iput (inode);
	UFSD("EXIT (FAILED): err %d\n", err);
	return ERR_PTR(err);
}
