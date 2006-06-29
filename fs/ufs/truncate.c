/*
 *  linux/fs/ufs/truncate.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/truncate.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/truncate.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

/*
 * Real random numbers for secure rm added 94/02/18
 * Idea from Pierre del Perugia <delperug@gla.ecoledoc.ibp.fr>
 */

/*
 * Modified to avoid infinite loop on 2006 by
 * Evgeniy Dushistov <dushistov@mail.ru>
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/fcntl.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/sched.h>

#include "swab.h"
#include "util.h"

/*
 * Secure deletion currently doesn't work. It interacts very badly
 * with buffers shared with memory mappings, and for that reason
 * can't be done in the truncate() routines. It should instead be
 * done separately in "release()" before calling the truncate routines
 * that will release the actual file blocks.
 *
 *		Linus
 */

#define DIRECT_BLOCK ((inode->i_size + uspi->s_bsize - 1) >> uspi->s_bshift)
#define DIRECT_FRAGMENT ((inode->i_size + uspi->s_fsize - 1) >> uspi->s_fshift)


static int ufs_trunc_direct (struct inode * inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	__fs32 * p;
	unsigned frag1, frag2, frag3, frag4, block1, block2;
	unsigned frag_to_free, free_count;
	unsigned i, tmp;
	int retry;
	
	UFSD("ENTER\n");

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	
	frag_to_free = 0;
	free_count = 0;
	retry = 0;
	
	frag1 = DIRECT_FRAGMENT;
	frag4 = min_t(u32, UFS_NDIR_FRAGMENT, ufsi->i_lastfrag);
	frag2 = ((frag1 & uspi->s_fpbmask) ? ((frag1 | uspi->s_fpbmask) + 1) : frag1);
	frag3 = frag4 & ~uspi->s_fpbmask;
	block1 = block2 = 0;
	if (frag2 > frag3) {
		frag2 = frag4;
		frag3 = frag4 = 0;
	}
	else if (frag2 < frag3) {
		block1 = ufs_fragstoblks (frag2);
		block2 = ufs_fragstoblks (frag3);
	}

	UFSD("frag1 %u, frag2 %u, block1 %u, block2 %u, frag3 %u, frag4 %u\n", frag1, frag2, block1, block2, frag3, frag4);

	if (frag1 >= frag2)
		goto next1;		

	/*
	 * Free first free fragments
	 */
	p = ufsi->i_u1.i_data + ufs_fragstoblks (frag1);
	tmp = fs32_to_cpu(sb, *p);
	if (!tmp )
		ufs_panic (sb, "ufs_trunc_direct", "internal error");
	frag1 = ufs_fragnum (frag1);
	frag2 = ufs_fragnum (frag2);

	ufs_free_fragments (inode, tmp + frag1, frag2 - frag1);
	mark_inode_dirty(inode);
	frag_to_free = tmp + frag1;

next1:
	/*
	 * Free whole blocks
	 */
	for (i = block1 ; i < block2; i++) {
		p = ufsi->i_u1.i_data + i;
		tmp = fs32_to_cpu(sb, *p);
		if (!tmp)
			continue;

		*p = 0;

		if (free_count == 0) {
			frag_to_free = tmp;
			free_count = uspi->s_fpb;
		} else if (free_count > 0 && frag_to_free == tmp - free_count)
			free_count += uspi->s_fpb;
		else {
			ufs_free_blocks (inode, frag_to_free, free_count);
			frag_to_free = tmp;
			free_count = uspi->s_fpb;
		}
		mark_inode_dirty(inode);
	}
	
	if (free_count > 0)
		ufs_free_blocks (inode, frag_to_free, free_count);

	if (frag3 >= frag4)
		goto next3;

	/*
	 * Free last free fragments
	 */
	p = ufsi->i_u1.i_data + ufs_fragstoblks (frag3);
	tmp = fs32_to_cpu(sb, *p);
	if (!tmp )
		ufs_panic(sb, "ufs_truncate_direct", "internal error");
	frag4 = ufs_fragnum (frag4);

	*p = 0;

	ufs_free_fragments (inode, tmp, frag4);
	mark_inode_dirty(inode);
 next3:

	UFSD("EXIT\n");
	return retry;
}


static int ufs_trunc_indirect (struct inode * inode, unsigned offset, __fs32 *p)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * ind_ubh;
	__fs32 * ind;
	unsigned indirect_block, i, tmp;
	unsigned frag_to_free, free_count;
	int retry;

	UFSD("ENTER\n");
		
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	frag_to_free = 0;
	free_count = 0;
	retry = 0;
	
	tmp = fs32_to_cpu(sb, *p);
	if (!tmp)
		return 0;
	ind_ubh = ubh_bread(sb, tmp, uspi->s_bsize);
	if (tmp != fs32_to_cpu(sb, *p)) {
		ubh_brelse (ind_ubh);
		return 1;
	}
	if (!ind_ubh) {
		*p = 0;
		return 0;
	}

	indirect_block = (DIRECT_BLOCK > offset) ? (DIRECT_BLOCK - offset) : 0;
	for (i = indirect_block; i < uspi->s_apb; i++) {
		ind = ubh_get_addr32 (ind_ubh, i);
		tmp = fs32_to_cpu(sb, *ind);
		if (!tmp)
			continue;

		*ind = 0;
		ubh_mark_buffer_dirty(ind_ubh);
		if (free_count == 0) {
			frag_to_free = tmp;
			free_count = uspi->s_fpb;
		} else if (free_count > 0 && frag_to_free == tmp - free_count)
			free_count += uspi->s_fpb;
		else {
			ufs_free_blocks (inode, frag_to_free, free_count);
			frag_to_free = tmp;
			free_count = uspi->s_fpb;
		}

		mark_inode_dirty(inode);
	}

	if (free_count > 0) {
		ufs_free_blocks (inode, frag_to_free, free_count);
	}
	for (i = 0; i < uspi->s_apb; i++)
		if (*ubh_get_addr32(ind_ubh,i))
			break;
	if (i >= uspi->s_apb) {
		tmp = fs32_to_cpu(sb, *p);
		*p = 0;

		ufs_free_blocks (inode, tmp, uspi->s_fpb);
		mark_inode_dirty(inode);
		ubh_bforget(ind_ubh);
		ind_ubh = NULL;
	}
	if (IS_SYNC(inode) && ind_ubh && ubh_buffer_dirty(ind_ubh)) {
		ubh_ll_rw_block(SWRITE, ind_ubh);
		ubh_wait_on_buffer (ind_ubh);
	}
	ubh_brelse (ind_ubh);
	
	UFSD("EXIT\n");
	
	return retry;
}

static int ufs_trunc_dindirect (struct inode *inode, unsigned offset, __fs32 *p)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * dind_bh;
	unsigned i, tmp, dindirect_block;
	__fs32 * dind;
	int retry = 0;
	
	UFSD("ENTER\n");
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	dindirect_block = (DIRECT_BLOCK > offset) 
		? ((DIRECT_BLOCK - offset) >> uspi->s_apbshift) : 0;
	retry = 0;
	
	tmp = fs32_to_cpu(sb, *p);
	if (!tmp)
		return 0;
	dind_bh = ubh_bread(sb, tmp, uspi->s_bsize);
	if (tmp != fs32_to_cpu(sb, *p)) {
		ubh_brelse (dind_bh);
		return 1;
	}
	if (!dind_bh) {
		*p = 0;
		return 0;
	}

	for (i = dindirect_block ; i < uspi->s_apb ; i++) {
		dind = ubh_get_addr32 (dind_bh, i);
		tmp = fs32_to_cpu(sb, *dind);
		if (!tmp)
			continue;
		retry |= ufs_trunc_indirect (inode, offset + (i << uspi->s_apbshift), dind);
		ubh_mark_buffer_dirty(dind_bh);
	}

	for (i = 0; i < uspi->s_apb; i++)
		if (*ubh_get_addr32 (dind_bh, i))
			break;
	if (i >= uspi->s_apb) {
		tmp = fs32_to_cpu(sb, *p);
		*p = 0;

		ufs_free_blocks(inode, tmp, uspi->s_fpb);
		mark_inode_dirty(inode);
		ubh_bforget(dind_bh);
		dind_bh = NULL;
	}
	if (IS_SYNC(inode) && dind_bh && ubh_buffer_dirty(dind_bh)) {
		ubh_ll_rw_block(SWRITE, dind_bh);
		ubh_wait_on_buffer (dind_bh);
	}
	ubh_brelse (dind_bh);
	
	UFSD("EXIT\n");
	
	return retry;
}

static int ufs_trunc_tindirect (struct inode * inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * tind_bh;
	unsigned tindirect_block, tmp, i;
	__fs32 * tind, * p;
	int retry;
	
	UFSD("ENTER\n");

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	retry = 0;
	
	tindirect_block = (DIRECT_BLOCK > (UFS_NDADDR + uspi->s_apb + uspi->s_2apb))
		? ((DIRECT_BLOCK - UFS_NDADDR - uspi->s_apb - uspi->s_2apb) >> uspi->s_2apbshift) : 0;
	p = ufsi->i_u1.i_data + UFS_TIND_BLOCK;
	if (!(tmp = fs32_to_cpu(sb, *p)))
		return 0;
	tind_bh = ubh_bread (sb, tmp, uspi->s_bsize);
	if (tmp != fs32_to_cpu(sb, *p)) {
		ubh_brelse (tind_bh);
		return 1;
	}
	if (!tind_bh) {
		*p = 0;
		return 0;
	}

	for (i = tindirect_block ; i < uspi->s_apb ; i++) {
		tind = ubh_get_addr32 (tind_bh, i);
		retry |= ufs_trunc_dindirect(inode, UFS_NDADDR + 
			uspi->s_apb + ((i + 1) << uspi->s_2apbshift), tind);
		ubh_mark_buffer_dirty(tind_bh);
	}
	for (i = 0; i < uspi->s_apb; i++)
		if (*ubh_get_addr32 (tind_bh, i))
			break;
	if (i >= uspi->s_apb) {
		tmp = fs32_to_cpu(sb, *p);
		*p = 0;

		ufs_free_blocks(inode, tmp, uspi->s_fpb);
		mark_inode_dirty(inode);
		ubh_bforget(tind_bh);
		tind_bh = NULL;
	}
	if (IS_SYNC(inode) && tind_bh && ubh_buffer_dirty(tind_bh)) {
		ubh_ll_rw_block(SWRITE, tind_bh);
		ubh_wait_on_buffer (tind_bh);
	}
	ubh_brelse (tind_bh);
	
	UFSD("EXIT\n");
	return retry;
}
		
void ufs_truncate (struct inode * inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	int retry;
	
	UFSD("ENTER\n");
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	block_truncate_page(inode->i_mapping,	inode->i_size, ufs_getfrag_block);

	lock_kernel();
	while (1) {
		retry = ufs_trunc_direct(inode);
		retry |= ufs_trunc_indirect (inode, UFS_IND_BLOCK,
			(__fs32 *) &ufsi->i_u1.i_data[UFS_IND_BLOCK]);
		retry |= ufs_trunc_dindirect (inode, UFS_IND_BLOCK + uspi->s_apb,
			(__fs32 *) &ufsi->i_u1.i_data[UFS_DIND_BLOCK]);
		retry |= ufs_trunc_tindirect (inode);
		if (!retry)
			break;
		if (IS_SYNC(inode) && (inode->i_state & I_DIRTY))
			ufs_sync_inode (inode);
		blk_run_address_space(inode->i_mapping);
		yield();
	}

	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	ufsi->i_lastfrag = DIRECT_FRAGMENT;
	unlock_kernel();
	mark_inode_dirty(inode);
	UFSD("EXIT\n");
}
