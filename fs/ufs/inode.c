/*
 *  linux/fs/ufs/inode.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/inode.c
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
 *  Goal-directed block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>

#include "swab.h"
#include "util.h"

static int ufs_block_to_path(struct inode *inode, sector_t i_block, sector_t offsets[4])
{
	struct ufs_sb_private_info *uspi = UFS_SB(inode->i_sb)->s_uspi;
	int ptrs = uspi->s_apb;
	int ptrs_bits = uspi->s_apbshift;
	const long direct_blocks = UFS_NDADDR,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;


	UFSD("ptrs=uspi->s_apb = %d,double_blocks=%ld \n",ptrs,double_blocks);
	if (i_block < 0) {
		ufs_warning(inode->i_sb, "ufs_block_to_path", "block < 0");
	} else if (i_block < direct_blocks) {
		offsets[n++] = i_block;
	} else if ((i_block -= direct_blocks) < indirect_blocks) {
		offsets[n++] = UFS_IND_BLOCK;
		offsets[n++] = i_block;
	} else if ((i_block -= indirect_blocks) < double_blocks) {
		offsets[n++] = UFS_DIND_BLOCK;
		offsets[n++] = i_block >> ptrs_bits;
		offsets[n++] = i_block & (ptrs - 1);
	} else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
		offsets[n++] = UFS_TIND_BLOCK;
		offsets[n++] = i_block >> (ptrs_bits * 2);
		offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
		offsets[n++] = i_block & (ptrs - 1);
	} else {
		ufs_warning(inode->i_sb, "ufs_block_to_path", "block > big");
	}
	return n;
}

/*
 * Returns the location of the fragment from
 * the begining of the filesystem.
 */

u64  ufs_frag_map(struct inode *inode, sector_t frag)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	u64 mask = (u64) uspi->s_apbmask>>uspi->s_fpbshift;
	int shift = uspi->s_apbshift-uspi->s_fpbshift;
	sector_t offsets[4], *p;
	int depth = ufs_block_to_path(inode, frag >> uspi->s_fpbshift, offsets);
	u64  ret = 0L;
	__fs32 block;
	__fs64 u2_block = 0L;
	unsigned flags = UFS_SB(sb)->s_flags;
	u64 temp = 0L;

	UFSD(": frag = %llu  depth = %d\n", (unsigned long long)frag, depth);
	UFSD(": uspi->s_fpbshift = %d ,uspi->s_apbmask = %x, mask=%llx\n",uspi->s_fpbshift,uspi->s_apbmask,mask);

	if (depth == 0)
		return 0;

	p = offsets;

	lock_kernel();
	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
		goto ufs2;

	block = ufsi->i_u1.i_data[*p++];
	if (!block)
		goto out;
	while (--depth) {
		struct buffer_head *bh;
		sector_t n = *p++;

		bh = sb_bread(sb, uspi->s_sbbase + fs32_to_cpu(sb, block)+(n>>shift));
		if (!bh)
			goto out;
		block = ((__fs32 *) bh->b_data)[n & mask];
		brelse (bh);
		if (!block)
			goto out;
	}
	ret = (u64) (uspi->s_sbbase + fs32_to_cpu(sb, block) + (frag & uspi->s_fpbmask));
	goto out;
ufs2:
	u2_block = ufsi->i_u1.u2_i_data[*p++];
	if (!u2_block)
		goto out;


	while (--depth) {
		struct buffer_head *bh;
		sector_t n = *p++;


		temp = (u64)(uspi->s_sbbase) + fs64_to_cpu(sb, u2_block);
		bh = sb_bread(sb, temp +(u64) (n>>shift));
		if (!bh)
			goto out;
		u2_block = ((__fs64 *)bh->b_data)[n & mask];
		brelse(bh);
		if (!u2_block)
			goto out;
	}
	temp = (u64)uspi->s_sbbase + fs64_to_cpu(sb, u2_block);
	ret = temp + (u64) (frag & uspi->s_fpbmask);

out:
	unlock_kernel();
	return ret;
}

static void ufs_clear_frag(struct inode *inode, struct buffer_head *bh)
{
	lock_buffer(bh);
	memset(bh->b_data, 0, inode->i_sb->s_blocksize);
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	unlock_buffer(bh);
	if (IS_SYNC(inode))
		sync_dirty_buffer(bh);
}

static struct buffer_head *
ufs_clear_frags(struct inode *inode, sector_t beg,
		unsigned int n)
{
	struct buffer_head *res, *bh;
	sector_t end = beg + n;

	res = sb_getblk(inode->i_sb, beg);
	ufs_clear_frag(inode, res);
	for (++beg; beg < end; ++beg) {
		bh = sb_getblk(inode->i_sb, beg);
		ufs_clear_frag(inode, bh);
	}
	return res;
}

/**
 * ufs_inode_getfrag() - allocate new fragment(s)
 * @inode - pointer to inode
 * @fragment - number of `fragment' which hold pointer
 *   to new allocated fragment(s)
 * @new_fragment - number of new allocated fragment(s)
 * @required - how many fragment(s) we require
 * @err - we set it if something wrong
 * @phys - pointer to where we save physical number of new allocated fragments,
 *   NULL if we allocate not data(indirect blocks for example).
 * @new - we set it if we allocate new block
 * @locked_page - for ufs_new_fragments()
 */
static struct buffer_head *
ufs_inode_getfrag(struct inode *inode, unsigned int fragment,
		  sector_t new_fragment, unsigned int required, int *err,
		  long *phys, int *new, struct page *locked_page)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * result;
	unsigned block, blockoff, lastfrag, lastblock, lastblockoff;
	unsigned tmp, goal;
	__fs32 * p, * p2;

	UFSD("ENTER, ino %lu, fragment %u, new_fragment %llu, required %u, "
	     "metadata %d\n", inode->i_ino, fragment,
	     (unsigned long long)new_fragment, required, !phys);

        /* TODO : to be done for write support
        if ( (flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
             goto ufs2;
         */

	block = ufs_fragstoblks (fragment);
	blockoff = ufs_fragnum (fragment);
	p = ufsi->i_u1.i_data + block;
	goal = 0;

repeat:
	tmp = fs32_to_cpu(sb, *p);
	lastfrag = ufsi->i_lastfrag;
	if (tmp && fragment < lastfrag) {
		if (!phys) {
			result = sb_getblk(sb, uspi->s_sbbase + tmp + blockoff);
			if (tmp == fs32_to_cpu(sb, *p)) {
				UFSD("EXIT, result %u\n", tmp + blockoff);
				return result;
			}
			brelse (result);
			goto repeat;
		} else {
			*phys = tmp + blockoff;
			return NULL;
		}
	}

	lastblock = ufs_fragstoblks (lastfrag);
	lastblockoff = ufs_fragnum (lastfrag);
	/*
	 * We will extend file into new block beyond last allocated block
	 */
	if (lastblock < block) {
		/*
		 * We must reallocate last allocated block
		 */
		if (lastblockoff) {
			p2 = ufsi->i_u1.i_data + lastblock;
			tmp = ufs_new_fragments (inode, p2, lastfrag, 
						 fs32_to_cpu(sb, *p2), uspi->s_fpb - lastblockoff,
						 err, locked_page);
			if (!tmp) {
				if (lastfrag != ufsi->i_lastfrag)
					goto repeat;
				else
					return NULL;
			}
			lastfrag = ufsi->i_lastfrag;
			
		}
		goal = fs32_to_cpu(sb, ufsi->i_u1.i_data[lastblock]) + uspi->s_fpb;
		tmp = ufs_new_fragments (inode, p, fragment - blockoff, 
					 goal, required + blockoff,
					 err, locked_page);
	}
	/*
	 * We will extend last allocated block
	 */
	else if (lastblock == block) {
		tmp = ufs_new_fragments(inode, p, fragment - (blockoff - lastblockoff),
					fs32_to_cpu(sb, *p), required +  (blockoff - lastblockoff),
					err, locked_page);
	}
	/*
	 * We will allocate new block before last allocated block
	 */
	else /* (lastblock > block) */ {
		if (lastblock && (tmp = fs32_to_cpu(sb, ufsi->i_u1.i_data[lastblock-1])))
			goal = tmp + uspi->s_fpb;
		tmp = ufs_new_fragments(inode, p, fragment - blockoff,
					goal, uspi->s_fpb, err, locked_page);
	}
	if (!tmp) {
		if ((!blockoff && *p) || 
		    (blockoff && lastfrag != ufsi->i_lastfrag))
			goto repeat;
		*err = -ENOSPC;
		return NULL;
	}

	if (!phys) {
		result = ufs_clear_frags(inode, tmp + blockoff, required);
	} else {
		*phys = tmp + blockoff;
		result = NULL;
		*err = 0;
		*new = 1;
	}

	inode->i_ctime = CURRENT_TIME_SEC;
	if (IS_SYNC(inode))
		ufs_sync_inode (inode);
	mark_inode_dirty(inode);
	UFSD("EXIT, result %u\n", tmp + blockoff);
	return result;

     /* This part : To be implemented ....
        Required only for writing, not required for READ-ONLY.
ufs2:

	u2_block = ufs_fragstoblks(fragment);
	u2_blockoff = ufs_fragnum(fragment);
	p = ufsi->i_u1.u2_i_data + block;
	goal = 0;

repeat2:
	tmp = fs32_to_cpu(sb, *p);
	lastfrag = ufsi->i_lastfrag;

     */
}

/**
 * ufs_inode_getblock() - allocate new block
 * @inode - pointer to inode
 * @bh - pointer to block which hold "pointer" to new allocated block
 * @fragment - number of `fragment' which hold pointer
 *   to new allocated block
 * @new_fragment - number of new allocated fragment
 *  (block will hold this fragment and also uspi->s_fpb-1)
 * @err - see ufs_inode_getfrag()
 * @phys - see ufs_inode_getfrag()
 * @new - see ufs_inode_getfrag()
 * @locked_page - see ufs_inode_getfrag()
 */
static struct buffer_head *
ufs_inode_getblock(struct inode *inode, struct buffer_head *bh,
		  unsigned int fragment, sector_t new_fragment, int *err,
		  long *phys, int *new, struct page *locked_page)
{
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * result;
	unsigned tmp, goal, block, blockoff;
	__fs32 * p;

	block = ufs_fragstoblks (fragment);
	blockoff = ufs_fragnum (fragment);

	UFSD("ENTER, ino %lu, fragment %u, new_fragment %llu, metadata %d\n",
	     inode->i_ino, fragment, (unsigned long long)new_fragment, !phys);

	result = NULL;
	if (!bh)
		goto out;
	if (!buffer_uptodate(bh)) {
		ll_rw_block (READ, 1, &bh);
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh))
			goto out;
	}

	p = (__fs32 *) bh->b_data + block;
repeat:
	tmp = fs32_to_cpu(sb, *p);
	if (tmp) {
		if (!phys) {
			result = sb_getblk(sb, uspi->s_sbbase + tmp + blockoff);
			if (tmp == fs32_to_cpu(sb, *p))
				goto out;
			brelse (result);
			goto repeat;
		} else {
			*phys = tmp + blockoff;
			goto out;
		}
	}

	if (block && (tmp = fs32_to_cpu(sb, ((__fs32*)bh->b_data)[block-1]) + uspi->s_fpb))
		goal = tmp + uspi->s_fpb;
	else
		goal = bh->b_blocknr + uspi->s_fpb;
	tmp = ufs_new_fragments(inode, p, ufs_blknum(new_fragment), goal,
				uspi->s_fpb, err, locked_page);
	if (!tmp) {
		if (fs32_to_cpu(sb, *p))
			goto repeat;
		goto out;
	}		


	if (!phys) {
		result = ufs_clear_frags(inode, tmp + blockoff, uspi->s_fpb);
	} else {
		*phys = tmp + blockoff;
		*new = 1;
	}

	mark_buffer_dirty(bh);
	if (IS_SYNC(inode))
		sync_dirty_buffer(bh);
	inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	UFSD("result %u\n", tmp + blockoff);
out:
	brelse (bh);
	UFSD("EXIT\n");
	return result;
}

/**
 * ufs_getfrag_bloc() - `get_block_t' function, interface between UFS and
 * readpage, writepage and so on
 */

int ufs_getfrag_block(struct inode *inode, sector_t fragment, struct buffer_head *bh_result, int create)
{
	struct super_block * sb = inode->i_sb;
	struct ufs_sb_private_info * uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * bh;
	int ret, err, new;
	unsigned long ptr,phys;
	u64 phys64 = 0;
	
	if (!create) {
		phys64 = ufs_frag_map(inode, fragment);
		UFSD("phys64 = %llu \n",phys64);
		if (phys64)
			map_bh(bh_result, sb, phys64);
		return 0;
	}

        /* This code entered only while writing ....? */

	err = -EIO;
	new = 0;
	ret = 0;
	bh = NULL;

	lock_kernel();

	UFSD("ENTER, ino %lu, fragment %llu\n", inode->i_ino, (unsigned long long)fragment);
	if (fragment < 0)
		goto abort_negative;
	if (fragment >
	    ((UFS_NDADDR + uspi->s_apb + uspi->s_2apb + uspi->s_3apb)
	     << uspi->s_fpbshift))
		goto abort_too_big;

	err = 0;
	ptr = fragment;
	  
	/*
	 * ok, these macros clean the logic up a bit and make
	 * it much more readable:
	 */
#define GET_INODE_DATABLOCK(x) \
	ufs_inode_getfrag(inode, x, fragment, 1, &err, &phys, &new, bh_result->b_page)
#define GET_INODE_PTR(x) \
	ufs_inode_getfrag(inode, x, fragment, uspi->s_fpb, &err, NULL, NULL, bh_result->b_page)
#define GET_INDIRECT_DATABLOCK(x) \
	ufs_inode_getblock(inode, bh, x, fragment,	\
			  &err, &phys, &new, bh_result->b_page);
#define GET_INDIRECT_PTR(x) \
	ufs_inode_getblock(inode, bh, x, fragment,	\
			  &err, NULL, NULL, bh_result->b_page);

	if (ptr < UFS_NDIR_FRAGMENT) {
		bh = GET_INODE_DATABLOCK(ptr);
		goto out;
	}
	ptr -= UFS_NDIR_FRAGMENT;
	if (ptr < (1 << (uspi->s_apbshift + uspi->s_fpbshift))) {
		bh = GET_INODE_PTR(UFS_IND_FRAGMENT + (ptr >> uspi->s_apbshift));
		goto get_indirect;
	}
	ptr -= 1 << (uspi->s_apbshift + uspi->s_fpbshift);
	if (ptr < (1 << (uspi->s_2apbshift + uspi->s_fpbshift))) {
		bh = GET_INODE_PTR(UFS_DIND_FRAGMENT + (ptr >> uspi->s_2apbshift));
		goto get_double;
	}
	ptr -= 1 << (uspi->s_2apbshift + uspi->s_fpbshift);
	bh = GET_INODE_PTR(UFS_TIND_FRAGMENT + (ptr >> uspi->s_3apbshift));
	bh = GET_INDIRECT_PTR((ptr >> uspi->s_2apbshift) & uspi->s_apbmask);
get_double:
	bh = GET_INDIRECT_PTR((ptr >> uspi->s_apbshift) & uspi->s_apbmask);
get_indirect:
	bh = GET_INDIRECT_DATABLOCK(ptr & uspi->s_apbmask);

#undef GET_INODE_DATABLOCK
#undef GET_INODE_PTR
#undef GET_INDIRECT_DATABLOCK
#undef GET_INDIRECT_PTR

out:
	if (err)
		goto abort;
	if (new)
		set_buffer_new(bh_result);
	map_bh(bh_result, sb, phys);
abort:
	unlock_kernel();
	return err;

abort_negative:
	ufs_warning(sb, "ufs_get_block", "block < 0");
	goto abort;

abort_too_big:
	ufs_warning(sb, "ufs_get_block", "block > big");
	goto abort;
}

struct buffer_head *ufs_getfrag(struct inode *inode, unsigned int fragment,
				int create, int *err)
{
	struct buffer_head dummy;
	int error;

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	error = ufs_getfrag_block(inode, fragment, &dummy, create);
	*err = error;
	if (!error && buffer_mapped(&dummy)) {
		struct buffer_head *bh;
		bh = sb_getblk(inode->i_sb, dummy.b_blocknr);
		if (buffer_new(&dummy)) {
			memset(bh->b_data, 0, inode->i_sb->s_blocksize);
			set_buffer_uptodate(bh);
			mark_buffer_dirty(bh);
		}
		return bh;
	}
	return NULL;
}

struct buffer_head * ufs_bread (struct inode * inode, unsigned fragment,
	int create, int * err)
{
	struct buffer_head * bh;

	UFSD("ENTER, ino %lu, fragment %u\n", inode->i_ino, fragment);
	bh = ufs_getfrag (inode, fragment, create, err);
	if (!bh || buffer_uptodate(bh)) 		
		return bh;
	ll_rw_block (READ, 1, &bh);
	wait_on_buffer (bh);
	if (buffer_uptodate(bh))
		return bh;
	brelse (bh);
	*err = -EIO;
	return NULL;
}

static int ufs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page,ufs_getfrag_block,wbc);
}
static int ufs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,ufs_getfrag_block);
}
static int ufs_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return block_prepare_write(page,from,to,ufs_getfrag_block);
}
static sector_t ufs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,ufs_getfrag_block);
}
struct address_space_operations ufs_aops = {
	.readpage = ufs_readpage,
	.writepage = ufs_writepage,
	.sync_page = block_sync_page,
	.prepare_write = ufs_prepare_write,
	.commit_write = generic_commit_write,
	.bmap = ufs_bmap
};

static void ufs_set_inode_ops(struct inode *inode)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &ufs_file_inode_operations;
		inode->i_fop = &ufs_file_operations;
		inode->i_mapping->a_ops = &ufs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ufs_dir_inode_operations;
		inode->i_fop = &ufs_dir_operations;
		inode->i_mapping->a_ops = &ufs_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		if (!inode->i_blocks)
			inode->i_op = &ufs_fast_symlink_inode_operations;
		else {
			inode->i_op = &page_symlink_inode_operations;
			inode->i_mapping->a_ops = &ufs_aops;
		}
	} else
		init_special_inode(inode, inode->i_mode,
				   ufs_get_inode_dev(inode->i_sb, UFS_I(inode)));
}

void ufs_read_inode (struct inode * inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct ufs_inode * ufs_inode;	
	struct ufs2_inode *ufs2_inode;
	struct buffer_head * bh;
	mode_t mode;
	unsigned i;
	unsigned flags;
	
	UFSD("ENTER, ino %lu\n", inode->i_ino);
	
	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	flags = UFS_SB(sb)->s_flags;

	if (inode->i_ino < UFS_ROOTINO || 
	    inode->i_ino > (uspi->s_ncg * uspi->s_ipg)) {
		ufs_warning (sb, "ufs_read_inode", "bad inode number (%lu)\n", inode->i_ino);
		goto bad_inode;
	}
	
	bh = sb_bread(sb, uspi->s_sbbase + ufs_inotofsba(inode->i_ino));
	if (!bh) {
		ufs_warning (sb, "ufs_read_inode", "unable to read inode %lu\n", inode->i_ino);
		goto bad_inode;
	}
	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
		goto ufs2_inode;

	ufs_inode = (struct ufs_inode *) (bh->b_data + sizeof(struct ufs_inode) * ufs_inotofsbo(inode->i_ino));

	/*
	 * Copy data to the in-core inode.
	 */
	inode->i_mode = mode = fs16_to_cpu(sb, ufs_inode->ui_mode);
	inode->i_nlink = fs16_to_cpu(sb, ufs_inode->ui_nlink);
	if (inode->i_nlink == 0)
		ufs_error (sb, "ufs_read_inode", "inode %lu has zero nlink\n", inode->i_ino);
	
	/*
	 * Linux now has 32-bit uid and gid, so we can support EFT.
	 */
	inode->i_uid = ufs_get_inode_uid(sb, ufs_inode);
	inode->i_gid = ufs_get_inode_gid(sb, ufs_inode);

	inode->i_size = fs64_to_cpu(sb, ufs_inode->ui_size);
	inode->i_atime.tv_sec = fs32_to_cpu(sb, ufs_inode->ui_atime.tv_sec);
	inode->i_ctime.tv_sec = fs32_to_cpu(sb, ufs_inode->ui_ctime.tv_sec);
	inode->i_mtime.tv_sec = fs32_to_cpu(sb, ufs_inode->ui_mtime.tv_sec);
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = fs32_to_cpu(sb, ufs_inode->ui_blocks);
	inode->i_blksize = PAGE_SIZE;   /* This is the optimal IO size (for stat) */
	inode->i_version++;
	ufsi->i_flags = fs32_to_cpu(sb, ufs_inode->ui_flags);
	ufsi->i_gen = fs32_to_cpu(sb, ufs_inode->ui_gen);
	ufsi->i_shadow = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_shadow);
	ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_oeftflag);
	ufsi->i_lastfrag = (inode->i_size + uspi->s_fsize - 1) >> uspi->s_fshift;
	ufsi->i_dir_start_lookup = 0;
	
	if (S_ISCHR(mode) || S_ISBLK(mode) || inode->i_blocks) {
		for (i = 0; i < (UFS_NDADDR + UFS_NINDIR); i++)
			ufsi->i_u1.i_data[i] = ufs_inode->ui_u2.ui_addr.ui_db[i];
	} else {
		for (i = 0; i < (UFS_NDADDR + UFS_NINDIR) * 4; i++)
			ufsi->i_u1.i_symlink[i] = ufs_inode->ui_u2.ui_symlink[i];
	}
	ufsi->i_osync = 0;

	ufs_set_inode_ops(inode);

	brelse (bh);

	UFSD("EXIT\n");
	return;

bad_inode:
	make_bad_inode(inode);
	return;

ufs2_inode :
	UFSD("Reading ufs2 inode, ino %lu\n", inode->i_ino);

	ufs2_inode = (struct ufs2_inode *)(bh->b_data + sizeof(struct ufs2_inode) * ufs_inotofsbo(inode->i_ino));

	/*
	 * Copy data to the in-core inode.
	 */
	inode->i_mode = mode = fs16_to_cpu(sb, ufs2_inode->ui_mode);
	inode->i_nlink = fs16_to_cpu(sb, ufs2_inode->ui_nlink);
	if (inode->i_nlink == 0)
		ufs_error (sb, "ufs_read_inode", "inode %lu has zero nlink\n", inode->i_ino);

        /*
         * Linux now has 32-bit uid and gid, so we can support EFT.
         */
	inode->i_uid = fs32_to_cpu(sb, ufs2_inode->ui_uid);
	inode->i_gid = fs32_to_cpu(sb, ufs2_inode->ui_gid);

	inode->i_size = fs64_to_cpu(sb, ufs2_inode->ui_size);
	inode->i_atime.tv_sec = fs32_to_cpu(sb, ufs2_inode->ui_atime.tv_sec);
	inode->i_ctime.tv_sec = fs32_to_cpu(sb, ufs2_inode->ui_ctime.tv_sec);
	inode->i_mtime.tv_sec = fs32_to_cpu(sb, ufs2_inode->ui_mtime.tv_sec);
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = fs64_to_cpu(sb, ufs2_inode->ui_blocks);
	inode->i_blksize = PAGE_SIZE; /*This is the optimal IO size(for stat)*/

	inode->i_version++;
	ufsi->i_flags = fs32_to_cpu(sb, ufs2_inode->ui_flags);
	ufsi->i_gen = fs32_to_cpu(sb, ufs2_inode->ui_gen);
	/*
	ufsi->i_shadow = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_shadow);
	ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_oeftflag);
	*/
	ufsi->i_lastfrag= (inode->i_size + uspi->s_fsize- 1) >> uspi->s_fshift;

	if (S_ISCHR(mode) || S_ISBLK(mode) || inode->i_blocks) {
		for (i = 0; i < (UFS_NDADDR + UFS_NINDIR); i++)
			ufsi->i_u1.u2_i_data[i] =
				ufs2_inode->ui_u2.ui_addr.ui_db[i];
	}
	else {
		for (i = 0; i < (UFS_NDADDR + UFS_NINDIR) * 4; i++)
			ufsi->i_u1.i_symlink[i] = ufs2_inode->ui_u2.ui_symlink[i];
	}
	ufsi->i_osync = 0;

	ufs_set_inode_ops(inode);

	brelse(bh);

	UFSD("EXIT\n");
	return;
}

static int ufs_update_inode(struct inode * inode, int do_sync)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	struct buffer_head * bh;
	struct ufs_inode * ufs_inode;
	unsigned i;
	unsigned flags;

	UFSD("ENTER, ino %lu\n", inode->i_ino);

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;
	flags = UFS_SB(sb)->s_flags;

	if (inode->i_ino < UFS_ROOTINO || 
	    inode->i_ino > (uspi->s_ncg * uspi->s_ipg)) {
		ufs_warning (sb, "ufs_read_inode", "bad inode number (%lu)\n", inode->i_ino);
		return -1;
	}

	bh = sb_bread(sb, ufs_inotofsba(inode->i_ino));
	if (!bh) {
		ufs_warning (sb, "ufs_read_inode", "unable to read inode %lu\n", inode->i_ino);
		return -1;
	}
	ufs_inode = (struct ufs_inode *) (bh->b_data + ufs_inotofsbo(inode->i_ino) * sizeof(struct ufs_inode));

	ufs_inode->ui_mode = cpu_to_fs16(sb, inode->i_mode);
	ufs_inode->ui_nlink = cpu_to_fs16(sb, inode->i_nlink);

	ufs_set_inode_uid(sb, ufs_inode, inode->i_uid);
	ufs_set_inode_gid(sb, ufs_inode, inode->i_gid);
		
	ufs_inode->ui_size = cpu_to_fs64(sb, inode->i_size);
	ufs_inode->ui_atime.tv_sec = cpu_to_fs32(sb, inode->i_atime.tv_sec);
	ufs_inode->ui_atime.tv_usec = 0;
	ufs_inode->ui_ctime.tv_sec = cpu_to_fs32(sb, inode->i_ctime.tv_sec);
	ufs_inode->ui_ctime.tv_usec = 0;
	ufs_inode->ui_mtime.tv_sec = cpu_to_fs32(sb, inode->i_mtime.tv_sec);
	ufs_inode->ui_mtime.tv_usec = 0;
	ufs_inode->ui_blocks = cpu_to_fs32(sb, inode->i_blocks);
	ufs_inode->ui_flags = cpu_to_fs32(sb, ufsi->i_flags);
	ufs_inode->ui_gen = cpu_to_fs32(sb, ufsi->i_gen);

	if ((flags & UFS_UID_MASK) == UFS_UID_EFT) {
		ufs_inode->ui_u3.ui_sun.ui_shadow = cpu_to_fs32(sb, ufsi->i_shadow);
		ufs_inode->ui_u3.ui_sun.ui_oeftflag = cpu_to_fs32(sb, ufsi->i_oeftflag);
	}

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		/* ufs_inode->ui_u2.ui_addr.ui_db[0] = cpu_to_fs32(sb, inode->i_rdev); */
		ufs_inode->ui_u2.ui_addr.ui_db[0] = ufsi->i_u1.i_data[0];
	} else if (inode->i_blocks) {
		for (i = 0; i < (UFS_NDADDR + UFS_NINDIR); i++)
			ufs_inode->ui_u2.ui_addr.ui_db[i] = ufsi->i_u1.i_data[i];
	}
	else {
		for (i = 0; i < (UFS_NDADDR + UFS_NINDIR) * 4; i++)
			ufs_inode->ui_u2.ui_symlink[i] = ufsi->i_u1.i_symlink[i];
	}

	if (!inode->i_nlink)
		memset (ufs_inode, 0, sizeof(struct ufs_inode));
		
	mark_buffer_dirty(bh);
	if (do_sync)
		sync_dirty_buffer(bh);
	brelse (bh);
	
	UFSD("EXIT\n");
	return 0;
}

int ufs_write_inode (struct inode * inode, int wait)
{
	int ret;
	lock_kernel();
	ret = ufs_update_inode (inode, wait);
	unlock_kernel();
	return ret;
}

int ufs_sync_inode (struct inode *inode)
{
	return ufs_update_inode (inode, 1);
}

void ufs_delete_inode (struct inode * inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	/*UFS_I(inode)->i_dtime = CURRENT_TIME;*/
	lock_kernel();
	mark_inode_dirty(inode);
	ufs_update_inode(inode, IS_SYNC(inode));
	inode->i_size = 0;
	if (inode->i_blocks)
		ufs_truncate (inode);
	ufs_free_inode (inode);
	unlock_kernel();
}
