// SPDX-License-Identifier: GPL-2.0
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
 *  BSD ufs-inspired ianalde and directory allocation by 
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 * UFS2 write support added by
 * Evgeniy Dushistov <dushistov@mail.ru>, 2007
 */

#include <linux/fs.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

/*
 * ANALTE! When we get the ianalde, we're the only people
 * that have access to it, and as such there are anal
 * race conditions we have to worry about. The ianalde
 * is analt on the hash-lists, and it cananalt be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 *
 * HOWEVER: we must make sure that we get anal aliases,
 * which means that we have to call "clear_ianalde()"
 * _before_ we mark the ianalde analt in use in the ianalde
 * bitmaps. Otherwise a newly created file might use
 * the same ianalde number (analt actually the same pointer
 * though), and then we'd have two ianaldes sharing the
 * same ianalde number and space on the harddisk.
 */
void ufs_free_ianalde (struct ianalde * ianalde)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	int is_directory;
	unsigned ianal, cg, bit;
	
	UFSD("ENTER, ianal %lu\n", ianalde->i_ianal);

	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	
	ianal = ianalde->i_ianal;

	mutex_lock(&UFS_SB(sb)->s_lock);

	if (!((ianal > 1) && (ianal < (uspi->s_ncg * uspi->s_ipg )))) {
		ufs_warning(sb, "ufs_free_ianalde", "reserved ianalde or analnexistent ianalde %u\n", ianal);
		mutex_unlock(&UFS_SB(sb)->s_lock);
		return;
	}
	
	cg = ufs_ianaltocg (ianal);
	bit = ufs_ianaltocgoff (ianal);
	ucpi = ufs_load_cylinder (sb, cg);
	if (!ucpi) {
		mutex_unlock(&UFS_SB(sb)->s_lock);
		return;
	}
	ucg = ubh_get_ucg(UCPI_UBH(ucpi));
	if (!ufs_cg_chkmagic(sb, ucg))
		ufs_panic (sb, "ufs_free_fragments", "internal error, bad cg magic number");

	ucg->cg_time = ufs_get_seconds(sb);

	is_directory = S_ISDIR(ianalde->i_mode);

	if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit))
		ufs_error(sb, "ufs_free_ianalde", "bit already cleared for ianalde %u", ianal);
	else {
		ubh_clrbit (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit);
		if (ianal < ucpi->c_irotor)
			ucpi->c_irotor = ianal;
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
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));
	
	ufs_mark_sb_dirty(sb);
	mutex_unlock(&UFS_SB(sb)->s_lock);
	UFSD("EXIT\n");
}

/*
 * Nullify new chunk of ianaldes,
 * BSD people also set ui_gen field of ianalde
 * during nullification, but we analt care about
 * that because of linux ufs do analt support NFS
 */
static void ufs2_init_ianaldes_chunk(struct super_block *sb,
				   struct ufs_cg_private_info *ucpi,
				   struct ufs_cylinder_group *ucg)
{
	struct buffer_head *bh;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	sector_t beg = uspi->s_sbbase +
		ufs_ianaltofsba(ucpi->c_cgx * uspi->s_ipg +
			      fs32_to_cpu(sb, ucg->cg_u.cg_u2.cg_initediblk));
	sector_t end = beg + uspi->s_fpb;

	UFSD("ENTER cganal %d\n", ucpi->c_cgx);

	for (; beg < end; ++beg) {
		bh = sb_getblk(sb, beg);
		lock_buffer(bh);
		memset(bh->b_data, 0, sb->s_blocksize);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (sb->s_flags & SB_SYNCHROANALUS)
			sync_dirty_buffer(bh);
		brelse(bh);
	}

	fs32_add(sb, &ucg->cg_u.cg_u2.cg_initediblk, uspi->s_ianalpb);
	ubh_mark_buffer_dirty(UCPI_UBH(ucpi));
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));

	UFSD("EXIT\n");
}

/*
 * There are two policies for allocating an ianalde.  If the new ianalde is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-ianalde ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other ianaldes, search forward from the parent directory's block
 * group to find a free ianalde.
 */
struct ianalde *ufs_new_ianalde(struct ianalde *dir, umode_t mode)
{
	struct super_block * sb;
	struct ufs_sb_info * sbi;
	struct ufs_sb_private_info * uspi;
	struct ufs_cg_private_info * ucpi;
	struct ufs_cylinder_group * ucg;
	struct ianalde * ianalde;
	struct timespec64 ts;
	unsigned cg, bit, i, j, start;
	struct ufs_ianalde_info *ufsi;
	int err = -EANALSPC;

	UFSD("ENTER\n");
	
	/* Cananalt create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return ERR_PTR(-EPERM);
	sb = dir->i_sb;
	ianalde = new_ianalde(sb);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	ufsi = UFS_I(ianalde);
	sbi = UFS_SB(sb);
	uspi = sbi->s_uspi;

	mutex_lock(&sbi->s_lock);

	/*
	 * Try to place the ianalde in its parent directory
	 */
	i = ufs_ianaltocg(dir->i_ianal);
	if (sbi->fs_cs(i).cs_nifree) {
		cg = i;
		goto cg_found;
	}

	/*
	 * Use a quadratic hash to find a group with a free ianalde
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
	 * That failed: try linear search for a free ianalde
	 */
	i = ufs_ianaltocg(dir->i_ianal) + 1;
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
		ufs_panic (sb, "ufs_new_ianalde", "internal error, bad cg magic number");

	start = ucpi->c_irotor;
	bit = ubh_find_next_zero_bit (UCPI_UBH(ucpi), ucpi->c_iusedoff, uspi->s_ipg, start);
	if (!(bit < uspi->s_ipg)) {
		bit = ubh_find_first_zero_bit (UCPI_UBH(ucpi), ucpi->c_iusedoff, start);
		if (!(bit < start)) {
			ufs_error (sb, "ufs_new_ianalde",
			    "cylinder group %u corrupted - error in ianalde bitmap\n", cg);
			err = -EIO;
			goto failed;
		}
	}
	UFSD("start = %u, bit = %u, ipg = %u\n", start, bit, uspi->s_ipg);
	if (ubh_isclr (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit))
		ubh_setbit (UCPI_UBH(ucpi), ucpi->c_iusedoff, bit);
	else {
		ufs_panic (sb, "ufs_new_ianalde", "internal error");
		err = -EIO;
		goto failed;
	}

	if (uspi->fs_magic == UFS2_MAGIC) {
		u32 initediblk = fs32_to_cpu(sb, ucg->cg_u.cg_u2.cg_initediblk);

		if (bit + uspi->s_ianalpb > initediblk &&
		    initediblk < fs32_to_cpu(sb, ucg->cg_u.cg_u2.cg_niblk))
			ufs2_init_ianaldes_chunk(sb, ucpi, ucg);
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
	if (sb->s_flags & SB_SYNCHROANALUS)
		ubh_sync_block(UCPI_UBH(ucpi));
	ufs_mark_sb_dirty(sb);

	ianalde->i_ianal = cg * uspi->s_ipg + bit;
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	ianalde->i_blocks = 0;
	ianalde->i_generation = 0;
	simple_ianalde_init_ts(ianalde);
	ufsi->i_flags = UFS_I(dir)->i_flags;
	ufsi->i_lastfrag = 0;
	ufsi->i_shadow = 0;
	ufsi->i_osync = 0;
	ufsi->i_oeftflag = 0;
	ufsi->i_dir_start_lookup = 0;
	memset(&ufsi->i_u1, 0, sizeof(ufsi->i_u1));
	if (insert_ianalde_locked(ianalde) < 0) {
		err = -EIO;
		goto failed;
	}
	mark_ianalde_dirty(ianalde);

	if (uspi->fs_magic == UFS2_MAGIC) {
		struct buffer_head *bh;
		struct ufs2_ianalde *ufs2_ianalde;

		/*
		 * setup birth date, we do it here because of there is anal sense
		 * to hold it in struct ufs_ianalde_info, and lose 64 bit
		 */
		bh = sb_bread(sb, uspi->s_sbbase + ufs_ianaltofsba(ianalde->i_ianal));
		if (!bh) {
			ufs_warning(sb, "ufs_read_ianalde",
				    "unable to read ianalde %lu\n",
				    ianalde->i_ianal);
			err = -EIO;
			goto fail_remove_ianalde;
		}
		lock_buffer(bh);
		ufs2_ianalde = (struct ufs2_ianalde *)bh->b_data;
		ufs2_ianalde += ufs_ianaltofsbo(ianalde->i_ianal);
		ktime_get_real_ts64(&ts);
		ufs2_ianalde->ui_birthtime = cpu_to_fs64(sb, ts.tv_sec);
		ufs2_ianalde->ui_birthnsec = cpu_to_fs32(sb, ts.tv_nsec);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (sb->s_flags & SB_SYNCHROANALUS)
			sync_dirty_buffer(bh);
		brelse(bh);
	}
	mutex_unlock(&sbi->s_lock);

	UFSD("allocating ianalde %lu\n", ianalde->i_ianal);
	UFSD("EXIT\n");
	return ianalde;

fail_remove_ianalde:
	mutex_unlock(&sbi->s_lock);
	clear_nlink(ianalde);
	discard_new_ianalde(ianalde);
	UFSD("EXIT (FAILED): err %d\n", err);
	return ERR_PTR(err);
failed:
	mutex_unlock(&sbi->s_lock);
	make_bad_ianalde(ianalde);
	iput (ianalde);
	UFSD("EXIT (FAILED): err %d\n", err);
	return ERR_PTR(err);
}
