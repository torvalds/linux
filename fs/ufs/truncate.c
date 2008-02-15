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
 * Adoptation to use page cache and UFS2 write support by
 * Evgeniy Dushistov <dushistov@mail.ru>, 2006-2007
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/sched.h>

#include "ufs_fs.h"
#include "ufs.h"
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


static int ufs_trunc_direct(struct inode *inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	void *p;
	u64 frag1, frag2, frag3, frag4, block1, block2;
	unsigned frag_to_free, free_count;
	unsigned i, tmp;
	int retry;
	
	UFSD("ENTER: ino %lu\n", inode->i_ino);

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
	} else if (frag2 < frag3) {
		block1 = ufs_fragstoblks (frag2);
		block2 = ufs_fragstoblks (frag3);
	}

	UFSD("ino %lu, frag1 %llu, frag2 %llu, block1 %llu, block2 %llu,"
	     " frag3 %llu, frag4 %llu\n", inode->i_ino,
	     (unsigned long long)frag1, (unsigned long long)frag2,
	     (unsigned long long)block1, (unsigned long long)block2,
	     (unsigned long long)frag3, (unsigned long long)frag4);

	if (frag1 >= frag2)
		goto next1;		

	/*
	 * Free first free fragments
	 */
	p = ufs_get_direct_data_ptr(uspi, ufsi, ufs_fragstoblks(frag1));
	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (!tmp )
		ufs_panic (sb, "ufs_trunc_direct", "internal error");
	frag2 -= frag1;
	frag1 = ufs_fragnum (frag1);

	ufs_free_fragments(inode, tmp + frag1, frag2);
	mark_inode_dirty(inode);
	frag_to_free = tmp + frag1;

next1:
	/*
	 * Free whole blocks
	 */
	for (i = block1 ; i < block2; i++) {
		p = ufs_get_direct_data_ptr(uspi, ufsi, i);
		tmp = ufs_data_ptr_to_cpu(sb, p);
		if (!tmp)
			continue;
		ufs_data_ptr_clear(uspi, p);

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
	p = ufs_get_direct_data_ptr(uspi, ufsi, ufs_fragstoblks(frag3));
	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (!tmp )
		ufs_panic(sb, "ufs_truncate_direct", "internal error");
	frag4 = ufs_fragnum (frag4);
	ufs_data_ptr_clear(uspi, p);

	ufs_free_fragments (inode, tmp, frag4);
	mark_inode_dirty(inode);
 next3:

	UFSD("EXIT: ino %lu\n", inode->i_ino);
	return retry;
}


static int ufs_trunc_indirect(struct inode *inode, u64 offset, void *p)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * ind_ubh;
	void *ind;
	u64 tmp, indirect_block, i, frag_to_free;
	unsigned free_count;
	int retry;

	UFSD("ENTER: ino %lu, offset %llu, p: %p\n",
	     inode->i_ino, (unsigned long long)offset, p);

	BUG_ON(!p);
		
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	frag_to_free = 0;
	free_count = 0;
	retry = 0;
	
	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (!tmp)
		return 0;
	ind_ubh = ubh_bread(sb, tmp, uspi->s_bsize);
	if (tmp != ufs_data_ptr_to_cpu(sb, p)) {
		ubh_brelse (ind_ubh);
		return 1;
	}
	if (!ind_ubh) {
		ufs_data_ptr_clear(uspi, p);
		return 0;
	}

	indirect_block = (DIRECT_BLOCK > offset) ? (DIRECT_BLOCK - offset) : 0;
	for (i = indirect_block; i < uspi->s_apb; i++) {
		ind = ubh_get_data_ptr(uspi, ind_ubh, i);
		tmp = ufs_data_ptr_to_cpu(sb, ind);
		if (!tmp)
			continue;

		ufs_data_ptr_clear(uspi, ind);
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
		if (!ufs_is_data_ptr_zero(uspi,
					  ubh_get_data_ptr(uspi, ind_ubh, i)))
			break;
	if (i >= uspi->s_apb) {
		tmp = ufs_data_ptr_to_cpu(sb, p);
		ufs_data_ptr_clear(uspi, p);

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
	
	UFSD("EXIT: ino %lu\n", inode->i_ino);
	
	return retry;
}

static int ufs_trunc_dindirect(struct inode *inode, u64 offset, void *p)
{
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head *dind_bh;
	u64 i, tmp, dindirect_block;
	void *dind;
	int retry = 0;
	
	UFSD("ENTER: ino %lu\n", inode->i_ino);
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	dindirect_block = (DIRECT_BLOCK > offset) 
		? ((DIRECT_BLOCK - offset) >> uspi->s_apbshift) : 0;
	retry = 0;
	
	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (!tmp)
		return 0;
	dind_bh = ubh_bread(sb, tmp, uspi->s_bsize);
	if (tmp != ufs_data_ptr_to_cpu(sb, p)) {
		ubh_brelse (dind_bh);
		return 1;
	}
	if (!dind_bh) {
		ufs_data_ptr_clear(uspi, p);
		return 0;
	}

	for (i = dindirect_block ; i < uspi->s_apb ; i++) {
		dind = ubh_get_data_ptr(uspi, dind_bh, i);
		tmp = ufs_data_ptr_to_cpu(sb, dind);
		if (!tmp)
			continue;
		retry |= ufs_trunc_indirect (inode, offset + (i << uspi->s_apbshift), dind);
		ubh_mark_buffer_dirty(dind_bh);
	}

	for (i = 0; i < uspi->s_apb; i++)
		if (!ufs_is_data_ptr_zero(uspi,
					  ubh_get_data_ptr(uspi, dind_bh, i)))
			break;
	if (i >= uspi->s_apb) {
		tmp = ufs_data_ptr_to_cpu(sb, p);
		ufs_data_ptr_clear(uspi, p);

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
	
	UFSD("EXIT: ino %lu\n", inode->i_ino);
	
	return retry;
}

static int ufs_trunc_tindirect(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct ufs_buffer_head * tind_bh;
	u64 tindirect_block, tmp, i;
	void *tind, *p;
	int retry;
	
	UFSD("ENTER: ino %lu\n", inode->i_ino);

	retry = 0;
	
	tindirect_block = (DIRECT_BLOCK > (UFS_NDADDR + uspi->s_apb + uspi->s_2apb))
		? ((DIRECT_BLOCK - UFS_NDADDR - uspi->s_apb - uspi->s_2apb) >> uspi->s_2apbshift) : 0;

	p = ufs_get_direct_data_ptr(uspi, ufsi, UFS_TIND_BLOCK);
	if (!(tmp = ufs_data_ptr_to_cpu(sb, p)))
		return 0;
	tind_bh = ubh_bread (sb, tmp, uspi->s_bsize);
	if (tmp != ufs_data_ptr_to_cpu(sb, p)) {
		ubh_brelse (tind_bh);
		return 1;
	}
	if (!tind_bh) {
		ufs_data_ptr_clear(uspi, p);
		return 0;
	}

	for (i = tindirect_block ; i < uspi->s_apb ; i++) {
		tind = ubh_get_data_ptr(uspi, tind_bh, i);
		retry |= ufs_trunc_dindirect(inode, UFS_NDADDR + 
			uspi->s_apb + ((i + 1) << uspi->s_2apbshift), tind);
		ubh_mark_buffer_dirty(tind_bh);
	}
	for (i = 0; i < uspi->s_apb; i++)
		if (!ufs_is_data_ptr_zero(uspi,
					  ubh_get_data_ptr(uspi, tind_bh, i)))
			break;
	if (i >= uspi->s_apb) {
		tmp = ufs_data_ptr_to_cpu(sb, p);
		ufs_data_ptr_clear(uspi, p);

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
	
	UFSD("EXIT: ino %lu\n", inode->i_ino);
	return retry;
}

static int ufs_alloc_lastblock(struct inode *inode)
{
	int err = 0;
	struct super_block *sb = inode->i_sb;
	struct address_space *mapping = inode->i_mapping;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned i, end;
	sector_t lastfrag;
	struct page *lastpage;
	struct buffer_head *bh;
	u64 phys64;

	lastfrag = (i_size_read(inode) + uspi->s_fsize - 1) >> uspi->s_fshift;

	if (!lastfrag)
		goto out;

	lastfrag--;

	lastpage = ufs_get_locked_page(mapping, lastfrag >>
				       (PAGE_CACHE_SHIFT - inode->i_blkbits));
       if (IS_ERR(lastpage)) {
               err = -EIO;
               goto out;
       }

       end = lastfrag & ((1 << (PAGE_CACHE_SHIFT - inode->i_blkbits)) - 1);
       bh = page_buffers(lastpage);
       for (i = 0; i < end; ++i)
               bh = bh->b_this_page;


       err = ufs_getfrag_block(inode, lastfrag, bh, 1);

       if (unlikely(err))
	       goto out_unlock;

       if (buffer_new(bh)) {
	       clear_buffer_new(bh);
	       unmap_underlying_metadata(bh->b_bdev,
					 bh->b_blocknr);
	       /*
		* we do not zeroize fragment, because of
		* if it maped to hole, it already contains zeroes
		*/
	       set_buffer_uptodate(bh);
	       mark_buffer_dirty(bh);
	       set_page_dirty(lastpage);
       }

       if (lastfrag >= UFS_IND_FRAGMENT) {
	       end = uspi->s_fpb - ufs_fragnum(lastfrag) - 1;
	       phys64 = bh->b_blocknr + 1;
	       for (i = 0; i < end; ++i) {
		       bh = sb_getblk(sb, i + phys64);
		       lock_buffer(bh);
		       memset(bh->b_data, 0, sb->s_blocksize);
		       set_buffer_uptodate(bh);
		       mark_buffer_dirty(bh);
		       unlock_buffer(bh);
		       sync_dirty_buffer(bh);
		       brelse(bh);
	       }
       }
out_unlock:
       ufs_put_locked_page(lastpage);
out:
       return err;
}

int ufs_truncate(struct inode *inode, loff_t old_i_size)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	int retry, err = 0;
	
	UFSD("ENTER: ino %lu, i_size: %llu, old_i_size: %llu\n",
	     inode->i_ino, (unsigned long long)i_size_read(inode),
	     (unsigned long long)old_i_size);

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	err = ufs_alloc_lastblock(inode);

	if (err) {
		i_size_write(inode, old_i_size);
		goto out;
	}

	block_truncate_page(inode->i_mapping, inode->i_size, ufs_getfrag_block);

	lock_kernel();
	while (1) {
		retry = ufs_trunc_direct(inode);
		retry |= ufs_trunc_indirect(inode, UFS_IND_BLOCK,
					    ufs_get_direct_data_ptr(uspi, ufsi,
								    UFS_IND_BLOCK));
		retry |= ufs_trunc_dindirect(inode, UFS_IND_BLOCK + uspi->s_apb,
					     ufs_get_direct_data_ptr(uspi, ufsi,
								     UFS_DIND_BLOCK));
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
out:
	UFSD("EXIT: err %d\n", err);
	return err;
}


/*
 * We don't define our `inode->i_op->truncate', and call it here,
 * because of:
 * - there is no way to know old size
 * - there is no way inform user about error, if it happens in `truncate'
 */
static int ufs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	unsigned int ia_valid = attr->ia_valid;
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (ia_valid & ATTR_SIZE &&
	    attr->ia_size != i_size_read(inode)) {
		loff_t old_i_size = inode->i_size;
		error = vmtruncate(inode, attr->ia_size);
		if (error)
			return error;
		error = ufs_truncate(inode, old_i_size);
		if (error)
			return error;
	}
	return inode_setattr(inode, attr);
}

const struct inode_operations ufs_file_inode_operations = {
	.setattr = ufs_setattr,
};
