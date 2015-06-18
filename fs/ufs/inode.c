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

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

static int ufs_block_to_path(struct inode *inode, sector_t i_block, unsigned offsets[4])
{
	struct ufs_sb_private_info *uspi = UFS_SB(inode->i_sb)->s_uspi;
	int ptrs = uspi->s_apb;
	int ptrs_bits = uspi->s_apbshift;
	const long direct_blocks = UFS_NDADDR,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;


	UFSD("ptrs=uspi->s_apb = %d,double_blocks=%ld \n",ptrs,double_blocks);
	if (i_block < direct_blocks) {
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

typedef struct {
	void	*p;
	union {
		__fs32	key32;
		__fs64	key64;
	};
	struct buffer_head *bh;
} Indirect;

static inline int grow_chain32(struct ufs_inode_info *ufsi,
			       struct buffer_head *bh, __fs32 *v,
			       Indirect *from, Indirect *to)
{
	Indirect *p;
	unsigned seq;
	to->bh = bh;
	do {
		seq = read_seqbegin(&ufsi->meta_lock);
		to->key32 = *(__fs32 *)(to->p = v);
		for (p = from; p <= to && p->key32 == *(__fs32 *)p->p; p++)
			;
	} while (read_seqretry(&ufsi->meta_lock, seq));
	return (p > to);
}

static inline int grow_chain64(struct ufs_inode_info *ufsi,
			       struct buffer_head *bh, __fs64 *v,
			       Indirect *from, Indirect *to)
{
	Indirect *p;
	unsigned seq;
	to->bh = bh;
	do {
		seq = read_seqbegin(&ufsi->meta_lock);
		to->key64 = *(__fs64 *)(to->p = v);
		for (p = from; p <= to && p->key64 == *(__fs64 *)p->p; p++)
			;
	} while (read_seqretry(&ufsi->meta_lock, seq));
	return (p > to);
}

/*
 * Returns the location of the fragment from
 * the beginning of the filesystem.
 */

static u64 ufs_frag_map(struct inode *inode, sector_t frag)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	u64 mask = (u64) uspi->s_apbmask>>uspi->s_fpbshift;
	int shift = uspi->s_apbshift-uspi->s_fpbshift;
	unsigned offsets[4], *p;
	Indirect chain[4], *q = chain;
	int depth = ufs_block_to_path(inode, frag >> uspi->s_fpbshift, offsets);
	unsigned flags = UFS_SB(sb)->s_flags;
	u64 res = 0;

	UFSD(": frag = %llu  depth = %d\n", (unsigned long long)frag, depth);
	UFSD(": uspi->s_fpbshift = %d ,uspi->s_apbmask = %x, mask=%llx\n",
		uspi->s_fpbshift, uspi->s_apbmask,
		(unsigned long long)mask);

	if (depth == 0)
		goto no_block;

again:
	p = offsets;

	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
		goto ufs2;

	if (!grow_chain32(ufsi, NULL, &ufsi->i_u1.i_data[*p++], chain, q))
		goto changed;
	if (!q->key32)
		goto no_block;
	while (--depth) {
		__fs32 *ptr;
		struct buffer_head *bh;
		unsigned n = *p++;

		bh = sb_bread(sb, uspi->s_sbbase +
				  fs32_to_cpu(sb, q->key32) + (n>>shift));
		if (!bh)
			goto no_block;
		ptr = (__fs32 *)bh->b_data + (n & mask);
		if (!grow_chain32(ufsi, bh, ptr, chain, ++q))
			goto changed;
		if (!q->key32)
			goto no_block;
	}
	res = fs32_to_cpu(sb, q->key32);
	goto found;

ufs2:
	if (!grow_chain64(ufsi, NULL, &ufsi->i_u1.u2_i_data[*p++], chain, q))
		goto changed;
	if (!q->key64)
		goto no_block;

	while (--depth) {
		__fs64 *ptr;
		struct buffer_head *bh;
		unsigned n = *p++;

		bh = sb_bread(sb, uspi->s_sbbase +
				  fs64_to_cpu(sb, q->key64) + (n>>shift));
		if (!bh)
			goto no_block;
		ptr = (__fs64 *)bh->b_data + (n & mask);
		if (!grow_chain64(ufsi, bh, ptr, chain, ++q))
			goto changed;
		if (!q->key64)
			goto no_block;
	}
	res = fs64_to_cpu(sb, q->key64);
found:
	res += uspi->s_sbbase + (frag & uspi->s_fpbmask);
no_block:
	while (q > chain) {
		brelse(q->bh);
		q--;
	}
	return res;

changed:
	while (q > chain) {
		brelse(q->bh);
		q--;
	}
	goto again;
}

/**
 * ufs_inode_getfrag() - allocate new fragment(s)
 * @inode: pointer to inode
 * @fragment: number of `fragment' which hold pointer
 *   to new allocated fragment(s)
 * @new_fragment: number of new allocated fragment(s)
 * @required: how many fragment(s) we require
 * @err: we set it if something wrong
 * @phys: pointer to where we save physical number of new allocated fragments,
 *   NULL if we allocate not data(indirect blocks for example).
 * @new: we set it if we allocate new block
 * @locked_page: for ufs_new_fragments()
 */
static struct buffer_head *
ufs_inode_getfrag(struct inode *inode, u64 fragment,
		  sector_t new_fragment, unsigned int required, int *err,
		  long *phys, int *new, struct page *locked_page)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * result;
	unsigned blockoff, lastblockoff;
	u64 tmp, goal, lastfrag, block, lastblock;
	void *p, *p2;

	UFSD("ENTER, ino %lu, fragment %llu, new_fragment %llu, required %u, "
	     "metadata %d\n", inode->i_ino, (unsigned long long)fragment,
	     (unsigned long long)new_fragment, required, !phys);

        /* TODO : to be done for write support
        if ( (flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
             goto ufs2;
         */

	block = ufs_fragstoblks (fragment);
	blockoff = ufs_fragnum (fragment);
	p = ufs_get_direct_data_ptr(uspi, ufsi, block);

	goal = 0;

repeat:
	tmp = ufs_data_ptr_to_cpu(sb, p);

	lastfrag = ufsi->i_lastfrag;
	if (tmp && fragment < lastfrag) {
		if (!phys) {
			result = sb_getblk(sb, uspi->s_sbbase + tmp + blockoff);
			if (tmp == ufs_data_ptr_to_cpu(sb, p)) {
				UFSD("EXIT, result %llu\n",
				     (unsigned long long)tmp + blockoff);
				return result;
			}
			brelse (result);
			goto repeat;
		} else {
			*phys = uspi->s_sbbase + tmp + blockoff;
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
			p2 = ufs_get_direct_data_ptr(uspi, ufsi, lastblock);
			tmp = ufs_new_fragments(inode, p2, lastfrag,
						ufs_data_ptr_to_cpu(sb, p2),
						uspi->s_fpb - lastblockoff,
						err, locked_page);
			if (!tmp) {
				if (lastfrag != ufsi->i_lastfrag)
					goto repeat;
				else
					return NULL;
			}
			lastfrag = ufsi->i_lastfrag;

		}
		tmp = ufs_data_ptr_to_cpu(sb,
					 ufs_get_direct_data_ptr(uspi, ufsi,
								 lastblock));
		if (tmp)
			goal = tmp + uspi->s_fpb;
		tmp = ufs_new_fragments (inode, p, fragment - blockoff,
					 goal, required + blockoff,
					 err,
					 phys != NULL ? locked_page : NULL);
	} else if (lastblock == block) {
	/*
	 * We will extend last allocated block
	 */
		tmp = ufs_new_fragments(inode, p, fragment -
					(blockoff - lastblockoff),
					ufs_data_ptr_to_cpu(sb, p),
					required +  (blockoff - lastblockoff),
					err, phys != NULL ? locked_page : NULL);
	} else /* (lastblock > block) */ {
	/*
	 * We will allocate new block before last allocated block
	 */
		if (block) {
			tmp = ufs_data_ptr_to_cpu(sb,
						 ufs_get_direct_data_ptr(uspi, ufsi, block - 1));
			if (tmp)
				goal = tmp + uspi->s_fpb;
		}
		tmp = ufs_new_fragments(inode, p, fragment - blockoff,
					goal, uspi->s_fpb, err,
					phys != NULL ? locked_page : NULL);
	}
	if (!tmp) {
		if ((!blockoff && ufs_data_ptr_to_cpu(sb, p)) ||
		    (blockoff && lastfrag != ufsi->i_lastfrag))
			goto repeat;
		*err = -ENOSPC;
		return NULL;
	}

	if (!phys) {
		result = sb_getblk(sb, uspi->s_sbbase + tmp + blockoff);
	} else {
		*phys = uspi->s_sbbase + tmp + blockoff;
		result = NULL;
		*err = 0;
		*new = 1;
	}

	inode->i_ctime = CURRENT_TIME_SEC;
	if (IS_SYNC(inode))
		ufs_sync_inode (inode);
	mark_inode_dirty(inode);
	UFSD("EXIT, result %llu\n", (unsigned long long)tmp + blockoff);
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
 * @inode: pointer to inode
 * @bh: pointer to block which hold "pointer" to new allocated block
 * @fragment: number of `fragment' which hold pointer
 *   to new allocated block
 * @new_fragment: number of new allocated fragment
 *  (block will hold this fragment and also uspi->s_fpb-1)
 * @err: see ufs_inode_getfrag()
 * @phys: see ufs_inode_getfrag()
 * @new: see ufs_inode_getfrag()
 * @locked_page: see ufs_inode_getfrag()
 */
static struct buffer_head *
ufs_inode_getblock(struct inode *inode, struct buffer_head *bh,
		  u64 fragment, sector_t new_fragment, int *err,
		  long *phys, int *new, struct page *locked_page)
{
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * result;
	unsigned blockoff;
	u64 tmp, goal, block;
	void *p;

	block = ufs_fragstoblks (fragment);
	blockoff = ufs_fragnum (fragment);

	UFSD("ENTER, ino %lu, fragment %llu, new_fragment %llu, metadata %d\n",
	     inode->i_ino, (unsigned long long)fragment,
	     (unsigned long long)new_fragment, !phys);

	result = NULL;
	if (!bh)
		goto out;
	if (!buffer_uptodate(bh)) {
		ll_rw_block (READ, 1, &bh);
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh))
			goto out;
	}
	if (uspi->fs_magic == UFS2_MAGIC)
		p = (__fs64 *)bh->b_data + block;
	else
		p = (__fs32 *)bh->b_data + block;
repeat:
	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (tmp) {
		if (!phys) {
			result = sb_getblk(sb, uspi->s_sbbase + tmp + blockoff);
			if (tmp == ufs_data_ptr_to_cpu(sb, p))
				goto out;
			brelse (result);
			goto repeat;
		} else {
			*phys = uspi->s_sbbase + tmp + blockoff;
			goto out;
		}
	}

	if (block && (uspi->fs_magic == UFS2_MAGIC ?
		      (tmp = fs64_to_cpu(sb, ((__fs64 *)bh->b_data)[block-1])) :
		      (tmp = fs32_to_cpu(sb, ((__fs32 *)bh->b_data)[block-1]))))
		goal = tmp + uspi->s_fpb;
	else
		goal = bh->b_blocknr + uspi->s_fpb;
	tmp = ufs_new_fragments(inode, p, ufs_blknum(new_fragment), goal,
				uspi->s_fpb, err, locked_page);
	if (!tmp) {
		if (ufs_data_ptr_to_cpu(sb, p))
			goto repeat;
		goto out;
	}


	if (!phys) {
		result = sb_getblk(sb, uspi->s_sbbase + tmp + blockoff);
	} else {
		*phys = uspi->s_sbbase + tmp + blockoff;
		*new = 1;
	}

	mark_buffer_dirty(bh);
	if (IS_SYNC(inode))
		sync_dirty_buffer(bh);
	inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	UFSD("result %llu\n", (unsigned long long)tmp + blockoff);
out:
	brelse (bh);
	UFSD("EXIT\n");
	return result;
}

/**
 * ufs_getfrag_block() - `get_block_t' function, interface between UFS and
 * readpage, writepage and so on
 */

static int ufs_getfrag_block(struct inode *inode, sector_t fragment, struct buffer_head *bh_result, int create)
{
	struct super_block * sb = inode->i_sb;
	struct ufs_sb_info * sbi = UFS_SB(sb);
	struct ufs_sb_private_info * uspi = sbi->s_uspi;
	struct buffer_head * bh;
	int ret, err, new;
	unsigned long ptr,phys;
	u64 phys64 = 0;

	if (!create) {
		phys64 = ufs_frag_map(inode, fragment);
		UFSD("phys64 = %llu\n", (unsigned long long)phys64);
		if (phys64)
			map_bh(bh_result, sb, phys64);
		return 0;
	}

        /* This code entered only while writing ....? */

	err = -EIO;
	new = 0;
	ret = 0;
	bh = NULL;

	mutex_lock(&UFS_I(inode)->truncate_mutex);

	UFSD("ENTER, ino %lu, fragment %llu\n", inode->i_ino, (unsigned long long)fragment);
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
	ufs_inode_getfrag(inode, x, fragment, 1, &err, &phys, &new,\
			  bh_result->b_page)
#define GET_INODE_PTR(x) \
	ufs_inode_getfrag(inode, x, fragment, uspi->s_fpb, &err, NULL, NULL,\
			  bh_result->b_page)
#define GET_INDIRECT_DATABLOCK(x) \
	ufs_inode_getblock(inode, bh, x, fragment,	\
			  &err, &phys, &new, bh_result->b_page)
#define GET_INDIRECT_PTR(x) \
	ufs_inode_getblock(inode, bh, x, fragment,	\
			  &err, NULL, NULL, NULL)

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
	mutex_unlock(&UFS_I(inode)->truncate_mutex);

	return err;

abort_too_big:
	ufs_warning(sb, "ufs_get_block", "block > big");
	goto abort;
}

static int ufs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page,ufs_getfrag_block,wbc);
}

static int ufs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,ufs_getfrag_block);
}

int ufs_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, ufs_getfrag_block);
}

static void ufs_truncate_blocks(struct inode *);

static void ufs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		ufs_truncate_blocks(inode);
	}
}

static int ufs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, flags, pagep,
				ufs_getfrag_block);
	if (unlikely(ret))
		ufs_write_failed(mapping, pos + len);

	return ret;
}

static int ufs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int ret;

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len)
		ufs_write_failed(mapping, pos + len);
	return ret;
}

static sector_t ufs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,ufs_getfrag_block);
}

const struct address_space_operations ufs_aops = {
	.readpage = ufs_readpage,
	.writepage = ufs_writepage,
	.write_begin = ufs_write_begin,
	.write_end = ufs_write_end,
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
		if (!inode->i_blocks) {
			inode->i_op = &ufs_fast_symlink_inode_operations;
			inode->i_link = (char *)UFS_I(inode)->i_u1.i_symlink;
		} else {
			inode->i_op = &ufs_symlink_inode_operations;
			inode->i_mapping->a_ops = &ufs_aops;
		}
	} else
		init_special_inode(inode, inode->i_mode,
				   ufs_get_inode_dev(inode->i_sb, UFS_I(inode)));
}

static int ufs1_read_inode(struct inode *inode, struct ufs_inode *ufs_inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	umode_t mode;

	/*
	 * Copy data to the in-core inode.
	 */
	inode->i_mode = mode = fs16_to_cpu(sb, ufs_inode->ui_mode);
	set_nlink(inode, fs16_to_cpu(sb, ufs_inode->ui_nlink));
	if (inode->i_nlink == 0) {
		ufs_error (sb, "ufs_read_inode", "inode %lu has zero nlink\n", inode->i_ino);
		return -1;
	}

	/*
	 * Linux now has 32-bit uid and gid, so we can support EFT.
	 */
	i_uid_write(inode, ufs_get_inode_uid(sb, ufs_inode));
	i_gid_write(inode, ufs_get_inode_gid(sb, ufs_inode));

	inode->i_size = fs64_to_cpu(sb, ufs_inode->ui_size);
	inode->i_atime.tv_sec = fs32_to_cpu(sb, ufs_inode->ui_atime.tv_sec);
	inode->i_ctime.tv_sec = fs32_to_cpu(sb, ufs_inode->ui_ctime.tv_sec);
	inode->i_mtime.tv_sec = fs32_to_cpu(sb, ufs_inode->ui_mtime.tv_sec);
	inode->i_mtime.tv_nsec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blocks = fs32_to_cpu(sb, ufs_inode->ui_blocks);
	inode->i_generation = fs32_to_cpu(sb, ufs_inode->ui_gen);
	ufsi->i_flags = fs32_to_cpu(sb, ufs_inode->ui_flags);
	ufsi->i_shadow = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_shadow);
	ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_oeftflag);


	if (S_ISCHR(mode) || S_ISBLK(mode) || inode->i_blocks) {
		memcpy(ufsi->i_u1.i_data, &ufs_inode->ui_u2.ui_addr,
		       sizeof(ufs_inode->ui_u2.ui_addr));
	} else {
		memcpy(ufsi->i_u1.i_symlink, ufs_inode->ui_u2.ui_symlink,
		       sizeof(ufs_inode->ui_u2.ui_symlink) - 1);
		ufsi->i_u1.i_symlink[sizeof(ufs_inode->ui_u2.ui_symlink) - 1] = 0;
	}
	return 0;
}

static int ufs2_read_inode(struct inode *inode, struct ufs2_inode *ufs2_inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	umode_t mode;

	UFSD("Reading ufs2 inode, ino %lu\n", inode->i_ino);
	/*
	 * Copy data to the in-core inode.
	 */
	inode->i_mode = mode = fs16_to_cpu(sb, ufs2_inode->ui_mode);
	set_nlink(inode, fs16_to_cpu(sb, ufs2_inode->ui_nlink));
	if (inode->i_nlink == 0) {
		ufs_error (sb, "ufs_read_inode", "inode %lu has zero nlink\n", inode->i_ino);
		return -1;
	}

        /*
         * Linux now has 32-bit uid and gid, so we can support EFT.
         */
	i_uid_write(inode, fs32_to_cpu(sb, ufs2_inode->ui_uid));
	i_gid_write(inode, fs32_to_cpu(sb, ufs2_inode->ui_gid));

	inode->i_size = fs64_to_cpu(sb, ufs2_inode->ui_size);
	inode->i_atime.tv_sec = fs64_to_cpu(sb, ufs2_inode->ui_atime);
	inode->i_ctime.tv_sec = fs64_to_cpu(sb, ufs2_inode->ui_ctime);
	inode->i_mtime.tv_sec = fs64_to_cpu(sb, ufs2_inode->ui_mtime);
	inode->i_atime.tv_nsec = fs32_to_cpu(sb, ufs2_inode->ui_atimensec);
	inode->i_ctime.tv_nsec = fs32_to_cpu(sb, ufs2_inode->ui_ctimensec);
	inode->i_mtime.tv_nsec = fs32_to_cpu(sb, ufs2_inode->ui_mtimensec);
	inode->i_blocks = fs64_to_cpu(sb, ufs2_inode->ui_blocks);
	inode->i_generation = fs32_to_cpu(sb, ufs2_inode->ui_gen);
	ufsi->i_flags = fs32_to_cpu(sb, ufs2_inode->ui_flags);
	/*
	ufsi->i_shadow = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_shadow);
	ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_oeftflag);
	*/

	if (S_ISCHR(mode) || S_ISBLK(mode) || inode->i_blocks) {
		memcpy(ufsi->i_u1.u2_i_data, &ufs2_inode->ui_u2.ui_addr,
		       sizeof(ufs2_inode->ui_u2.ui_addr));
	} else {
		memcpy(ufsi->i_u1.i_symlink, ufs2_inode->ui_u2.ui_symlink,
		       sizeof(ufs2_inode->ui_u2.ui_symlink) - 1);
		ufsi->i_u1.i_symlink[sizeof(ufs2_inode->ui_u2.ui_symlink) - 1] = 0;
	}
	return 0;
}

struct inode *ufs_iget(struct super_block *sb, unsigned long ino)
{
	struct ufs_inode_info *ufsi;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * bh;
	struct inode *inode;
	int err;

	UFSD("ENTER, ino %lu\n", ino);

	if (ino < UFS_ROOTINO || ino > (uspi->s_ncg * uspi->s_ipg)) {
		ufs_warning(sb, "ufs_read_inode", "bad inode number (%lu)\n",
			    ino);
		return ERR_PTR(-EIO);
	}

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ufsi = UFS_I(inode);

	bh = sb_bread(sb, uspi->s_sbbase + ufs_inotofsba(inode->i_ino));
	if (!bh) {
		ufs_warning(sb, "ufs_read_inode", "unable to read inode %lu\n",
			    inode->i_ino);
		goto bad_inode;
	}
	if ((UFS_SB(sb)->s_flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
		struct ufs2_inode *ufs2_inode = (struct ufs2_inode *)bh->b_data;

		err = ufs2_read_inode(inode,
				      ufs2_inode + ufs_inotofsbo(inode->i_ino));
	} else {
		struct ufs_inode *ufs_inode = (struct ufs_inode *)bh->b_data;

		err = ufs1_read_inode(inode,
				      ufs_inode + ufs_inotofsbo(inode->i_ino));
	}

	if (err)
		goto bad_inode;
	inode->i_version++;
	ufsi->i_lastfrag =
		(inode->i_size + uspi->s_fsize - 1) >> uspi->s_fshift;
	ufsi->i_dir_start_lookup = 0;
	ufsi->i_osync = 0;

	ufs_set_inode_ops(inode);

	brelse(bh);

	UFSD("EXIT\n");
	unlock_new_inode(inode);
	return inode;

bad_inode:
	iget_failed(inode);
	return ERR_PTR(-EIO);
}

static void ufs1_update_inode(struct inode *inode, struct ufs_inode *ufs_inode)
{
	struct super_block *sb = inode->i_sb;
 	struct ufs_inode_info *ufsi = UFS_I(inode);

	ufs_inode->ui_mode = cpu_to_fs16(sb, inode->i_mode);
	ufs_inode->ui_nlink = cpu_to_fs16(sb, inode->i_nlink);

	ufs_set_inode_uid(sb, ufs_inode, i_uid_read(inode));
	ufs_set_inode_gid(sb, ufs_inode, i_gid_read(inode));

	ufs_inode->ui_size = cpu_to_fs64(sb, inode->i_size);
	ufs_inode->ui_atime.tv_sec = cpu_to_fs32(sb, inode->i_atime.tv_sec);
	ufs_inode->ui_atime.tv_usec = 0;
	ufs_inode->ui_ctime.tv_sec = cpu_to_fs32(sb, inode->i_ctime.tv_sec);
	ufs_inode->ui_ctime.tv_usec = 0;
	ufs_inode->ui_mtime.tv_sec = cpu_to_fs32(sb, inode->i_mtime.tv_sec);
	ufs_inode->ui_mtime.tv_usec = 0;
	ufs_inode->ui_blocks = cpu_to_fs32(sb, inode->i_blocks);
	ufs_inode->ui_flags = cpu_to_fs32(sb, ufsi->i_flags);
	ufs_inode->ui_gen = cpu_to_fs32(sb, inode->i_generation);

	if ((UFS_SB(sb)->s_flags & UFS_UID_MASK) == UFS_UID_EFT) {
		ufs_inode->ui_u3.ui_sun.ui_shadow = cpu_to_fs32(sb, ufsi->i_shadow);
		ufs_inode->ui_u3.ui_sun.ui_oeftflag = cpu_to_fs32(sb, ufsi->i_oeftflag);
	}

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		/* ufs_inode->ui_u2.ui_addr.ui_db[0] = cpu_to_fs32(sb, inode->i_rdev); */
		ufs_inode->ui_u2.ui_addr.ui_db[0] = ufsi->i_u1.i_data[0];
	} else if (inode->i_blocks) {
		memcpy(&ufs_inode->ui_u2.ui_addr, ufsi->i_u1.i_data,
		       sizeof(ufs_inode->ui_u2.ui_addr));
	}
	else {
		memcpy(&ufs_inode->ui_u2.ui_symlink, ufsi->i_u1.i_symlink,
		       sizeof(ufs_inode->ui_u2.ui_symlink));
	}

	if (!inode->i_nlink)
		memset (ufs_inode, 0, sizeof(struct ufs_inode));
}

static void ufs2_update_inode(struct inode *inode, struct ufs2_inode *ufs_inode)
{
	struct super_block *sb = inode->i_sb;
 	struct ufs_inode_info *ufsi = UFS_I(inode);

	UFSD("ENTER\n");
	ufs_inode->ui_mode = cpu_to_fs16(sb, inode->i_mode);
	ufs_inode->ui_nlink = cpu_to_fs16(sb, inode->i_nlink);

	ufs_inode->ui_uid = cpu_to_fs32(sb, i_uid_read(inode));
	ufs_inode->ui_gid = cpu_to_fs32(sb, i_gid_read(inode));

	ufs_inode->ui_size = cpu_to_fs64(sb, inode->i_size);
	ufs_inode->ui_atime = cpu_to_fs64(sb, inode->i_atime.tv_sec);
	ufs_inode->ui_atimensec = cpu_to_fs32(sb, inode->i_atime.tv_nsec);
	ufs_inode->ui_ctime = cpu_to_fs64(sb, inode->i_ctime.tv_sec);
	ufs_inode->ui_ctimensec = cpu_to_fs32(sb, inode->i_ctime.tv_nsec);
	ufs_inode->ui_mtime = cpu_to_fs64(sb, inode->i_mtime.tv_sec);
	ufs_inode->ui_mtimensec = cpu_to_fs32(sb, inode->i_mtime.tv_nsec);

	ufs_inode->ui_blocks = cpu_to_fs64(sb, inode->i_blocks);
	ufs_inode->ui_flags = cpu_to_fs32(sb, ufsi->i_flags);
	ufs_inode->ui_gen = cpu_to_fs32(sb, inode->i_generation);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		/* ufs_inode->ui_u2.ui_addr.ui_db[0] = cpu_to_fs32(sb, inode->i_rdev); */
		ufs_inode->ui_u2.ui_addr.ui_db[0] = ufsi->i_u1.u2_i_data[0];
	} else if (inode->i_blocks) {
		memcpy(&ufs_inode->ui_u2.ui_addr, ufsi->i_u1.u2_i_data,
		       sizeof(ufs_inode->ui_u2.ui_addr));
	} else {
		memcpy(&ufs_inode->ui_u2.ui_symlink, ufsi->i_u1.i_symlink,
		       sizeof(ufs_inode->ui_u2.ui_symlink));
 	}

	if (!inode->i_nlink)
		memset (ufs_inode, 0, sizeof(struct ufs2_inode));
	UFSD("EXIT\n");
}

static int ufs_update_inode(struct inode * inode, int do_sync)
{
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * bh;

	UFSD("ENTER, ino %lu\n", inode->i_ino);

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
	if (uspi->fs_magic == UFS2_MAGIC) {
		struct ufs2_inode *ufs2_inode = (struct ufs2_inode *)bh->b_data;

		ufs2_update_inode(inode,
				  ufs2_inode + ufs_inotofsbo(inode->i_ino));
	} else {
		struct ufs_inode *ufs_inode = (struct ufs_inode *) bh->b_data;

		ufs1_update_inode(inode, ufs_inode + ufs_inotofsbo(inode->i_ino));
	}

	mark_buffer_dirty(bh);
	if (do_sync)
		sync_dirty_buffer(bh);
	brelse (bh);

	UFSD("EXIT\n");
	return 0;
}

int ufs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return ufs_update_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
}

int ufs_sync_inode (struct inode *inode)
{
	return ufs_update_inode (inode, 1);
}

void ufs_evict_inode(struct inode * inode)
{
	int want_delete = 0;

	if (!inode->i_nlink && !is_bad_inode(inode))
		want_delete = 1;

	truncate_inode_pages_final(&inode->i_data);
	if (want_delete) {
		inode->i_size = 0;
		if (inode->i_blocks)
			ufs_truncate_blocks(inode);
	}

	invalidate_inode_buffers(inode);
	clear_inode(inode);

	if (want_delete)
		ufs_free_inode(inode);
}

#define DIRECT_BLOCK ((inode->i_size + uspi->s_bsize - 1) >> uspi->s_bshift)
#define DIRECT_FRAGMENT ((inode->i_size + uspi->s_fsize - 1) >> uspi->s_fshift)

static void ufs_trunc_direct(struct inode *inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	void *p;
	u64 frag1, frag2, frag3, frag4, block1, block2;
	unsigned frag_to_free, free_count;
	unsigned i, tmp;

	UFSD("ENTER: ino %lu\n", inode->i_ino);

	sb = inode->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

	frag_to_free = 0;
	free_count = 0;

	frag1 = DIRECT_FRAGMENT;
	frag4 = min_t(u64, UFS_NDIR_FRAGMENT, ufsi->i_lastfrag);
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
		write_seqlock(&ufsi->meta_lock);
		ufs_data_ptr_clear(uspi, p);
		write_sequnlock(&ufsi->meta_lock);

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
	write_seqlock(&ufsi->meta_lock);
	ufs_data_ptr_clear(uspi, p);
	write_sequnlock(&ufsi->meta_lock);

	ufs_free_fragments (inode, tmp, frag4);
	mark_inode_dirty(inode);
 next3:

	UFSD("EXIT: ino %lu\n", inode->i_ino);
}

static void ufs_trunc_branch(struct inode *inode, unsigned *offsets, int depth2, int depth, void *p)
{
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct ufs_buffer_head *ubh;
	u64 tmp;
	bool free_it = !offsets || !depth2;
	unsigned from = offsets ? *offsets++ : 0;
	unsigned i;

	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (!tmp)
		return;
	ubh = ubh_bread (sb, tmp, uspi->s_bsize);
	if (!ubh) {
		write_seqlock(&ufsi->meta_lock);
		ufs_data_ptr_clear(uspi, p);
		write_sequnlock(&ufsi->meta_lock);
		return;
	}

	if (--depth) {
		for (i = from ; i < uspi->s_apb ; i++, offsets = NULL) {
			void *ind = ubh_get_data_ptr(uspi, ubh, i);
			ufs_trunc_branch(inode, offsets, depth2 - 1, depth, ind);
			ubh_mark_buffer_dirty(ubh);
		}
	} else {
		u64 frag_to_free = 0;
		unsigned free_count = 0;

		for (i = from; i < uspi->s_apb; i++) {
			void *ind = ubh_get_data_ptr(uspi, ubh, i);
			tmp = ufs_data_ptr_to_cpu(sb, ind);
			if (!tmp)
				continue;

			write_seqlock(&UFS_I(inode)->meta_lock);
			ufs_data_ptr_clear(uspi, ind);
			write_sequnlock(&UFS_I(inode)->meta_lock);
			ubh_mark_buffer_dirty(ubh);
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
	}
	if (free_it) {
		tmp = ufs_data_ptr_to_cpu(sb, p);
		write_seqlock(&ufsi->meta_lock);
		ufs_data_ptr_clear(uspi, p);
		write_sequnlock(&ufsi->meta_lock);

		ubh_bforget(ubh);
		ufs_free_blocks(inode, tmp, uspi->s_fpb);
		mark_inode_dirty(inode);
		return;
	}
	if (IS_SYNC(inode) && ubh_buffer_dirty(ubh))
		ubh_sync_block(ubh);
	ubh_brelse(ubh);
}

static int ufs_alloc_lastblock(struct inode *inode, loff_t size)
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

	lastfrag = (size + uspi->s_fsize - 1) >> uspi->s_fshift;

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

static void __ufs_truncate_blocks(struct inode *inode)
{
	struct ufs_inode_info *ufsi = UFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned offsets[4];
	int depth = ufs_block_to_path(inode, DIRECT_BLOCK, offsets);
	int depth2;

	if (!depth)
		return;

	/* find the last non-zero in offsets[] */
	for (depth2 = depth - 1; depth2; depth2--)
		if (offsets[depth2])
			break;

	mutex_lock(&ufsi->truncate_mutex);
	switch (depth) {
	case 1:
		ufs_trunc_direct(inode);
		ufs_trunc_branch(inode, NULL, 0, 1,
			   ufs_get_direct_data_ptr(uspi, ufsi, UFS_IND_BLOCK));
		ufs_trunc_branch(inode, NULL, 0, 2,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_DIND_BLOCK));
		ufs_trunc_branch(inode, NULL, 0, 3,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_TIND_BLOCK));
		break;
	case 2:
		ufs_trunc_branch(inode, offsets + 1, depth2, 1,
			   ufs_get_direct_data_ptr(uspi, ufsi, UFS_IND_BLOCK));
		ufs_trunc_branch(inode, NULL, 0, 2,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_DIND_BLOCK));
		ufs_trunc_branch(inode, NULL, 0, 3,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_TIND_BLOCK));
		break;
	case 3:
		ufs_trunc_branch(inode, offsets + 1, depth2, 2,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_DIND_BLOCK));
		ufs_trunc_branch(inode, NULL, 0, 3,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_TIND_BLOCK));
		break;
	case 4:
		ufs_trunc_branch(inode, offsets + 1, depth2, 3,
			    ufs_get_direct_data_ptr(uspi, ufsi, UFS_TIND_BLOCK));
	}
	ufsi->i_lastfrag = DIRECT_FRAGMENT;
	mutex_unlock(&ufsi->truncate_mutex);
}

static int ufs_truncate(struct inode *inode, loff_t size)
{
	int err = 0;

	UFSD("ENTER: ino %lu, i_size: %llu, old_i_size: %llu\n",
	     inode->i_ino, (unsigned long long)size,
	     (unsigned long long)i_size_read(inode));

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	err = ufs_alloc_lastblock(inode, size);

	if (err)
		goto out;

	block_truncate_page(inode->i_mapping, size, ufs_getfrag_block);

	truncate_setsize(inode, size);

	__ufs_truncate_blocks(inode);
	inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
out:
	UFSD("EXIT: err %d\n", err);
	return err;
}

void ufs_truncate_blocks(struct inode *inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;
	__ufs_truncate_blocks(inode);
}

int ufs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	unsigned int ia_valid = attr->ia_valid;
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (ia_valid & ATTR_SIZE && attr->ia_size != inode->i_size) {
		error = ufs_truncate(inode, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

const struct inode_operations ufs_file_inode_operations = {
	.setattr = ufs_setattr,
};
