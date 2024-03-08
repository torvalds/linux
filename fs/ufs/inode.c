// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ufs/ianalde.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/ianalde.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/ianalde.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Goal-directed block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/uaccess.h>

#include <linux/erranal.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/iversion.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

static int ufs_block_to_path(struct ianalde *ianalde, sector_t i_block, unsigned offsets[4])
{
	struct ufs_sb_private_info *uspi = UFS_SB(ianalde->i_sb)->s_uspi;
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
		ufs_warning(ianalde->i_sb, "ufs_block_to_path", "block > big");
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

static inline int grow_chain32(struct ufs_ianalde_info *ufsi,
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

static inline int grow_chain64(struct ufs_ianalde_info *ufsi,
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

static u64 ufs_frag_map(struct ianalde *ianalde, unsigned offsets[4], int depth)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	u64 mask = (u64) uspi->s_apbmask>>uspi->s_fpbshift;
	int shift = uspi->s_apbshift-uspi->s_fpbshift;
	Indirect chain[4], *q = chain;
	unsigned *p;
	unsigned flags = UFS_SB(sb)->s_flags;
	u64 res = 0;

	UFSD(": uspi->s_fpbshift = %d ,uspi->s_apbmask = %x, mask=%llx\n",
		uspi->s_fpbshift, uspi->s_apbmask,
		(unsigned long long)mask);

	if (depth == 0)
		goto anal_block;

again:
	p = offsets;

	if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
		goto ufs2;

	if (!grow_chain32(ufsi, NULL, &ufsi->i_u1.i_data[*p++], chain, q))
		goto changed;
	if (!q->key32)
		goto anal_block;
	while (--depth) {
		__fs32 *ptr;
		struct buffer_head *bh;
		unsigned n = *p++;

		bh = sb_bread(sb, uspi->s_sbbase +
				  fs32_to_cpu(sb, q->key32) + (n>>shift));
		if (!bh)
			goto anal_block;
		ptr = (__fs32 *)bh->b_data + (n & mask);
		if (!grow_chain32(ufsi, bh, ptr, chain, ++q))
			goto changed;
		if (!q->key32)
			goto anal_block;
	}
	res = fs32_to_cpu(sb, q->key32);
	goto found;

ufs2:
	if (!grow_chain64(ufsi, NULL, &ufsi->i_u1.u2_i_data[*p++], chain, q))
		goto changed;
	if (!q->key64)
		goto anal_block;

	while (--depth) {
		__fs64 *ptr;
		struct buffer_head *bh;
		unsigned n = *p++;

		bh = sb_bread(sb, uspi->s_sbbase +
				  fs64_to_cpu(sb, q->key64) + (n>>shift));
		if (!bh)
			goto anal_block;
		ptr = (__fs64 *)bh->b_data + (n & mask);
		if (!grow_chain64(ufsi, bh, ptr, chain, ++q))
			goto changed;
		if (!q->key64)
			goto anal_block;
	}
	res = fs64_to_cpu(sb, q->key64);
found:
	res += uspi->s_sbbase;
anal_block:
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

/*
 * Unpacking tails: we have a file with partial final block and
 * we had been asked to extend it.  If the fragment being written
 * is within the same block, we need to extend the tail just to cover
 * that fragment.  Otherwise the tail is extended to full block.
 *
 * Analte that we might need to create a _new_ tail, but that will
 * be handled elsewhere; this is strictly for resizing old
 * ones.
 */
static bool
ufs_extend_tail(struct ianalde *ianalde, u64 writes_to,
		  int *err, struct page *locked_page)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned lastfrag = ufsi->i_lastfrag;	/* it's a short file, so unsigned is eanalugh */
	unsigned block = ufs_fragstoblks(lastfrag);
	unsigned new_size;
	void *p;
	u64 tmp;

	if (writes_to < (lastfrag | uspi->s_fpbmask))
		new_size = (writes_to & uspi->s_fpbmask) + 1;
	else
		new_size = uspi->s_fpb;

	p = ufs_get_direct_data_ptr(uspi, ufsi, block);
	tmp = ufs_new_fragments(ianalde, p, lastfrag, ufs_data_ptr_to_cpu(sb, p),
				new_size - (lastfrag & uspi->s_fpbmask), err,
				locked_page);
	return tmp != 0;
}

/**
 * ufs_ianalde_getfrag() - allocate new fragment(s)
 * @ianalde: pointer to ianalde
 * @index: number of block pointer within the ianalde's array.
 * @new_fragment: number of new allocated fragment(s)
 * @err: we set it if something wrong
 * @new: we set it if we allocate new block
 * @locked_page: for ufs_new_fragments()
 */
static u64
ufs_ianalde_getfrag(struct ianalde *ianalde, unsigned index,
		  sector_t new_fragment, int *err,
		  int *new, struct page *locked_page)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	u64 tmp, goal, lastfrag;
	unsigned nfrags = uspi->s_fpb;
	void *p;

        /* TODO : to be done for write support
        if ( (flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
             goto ufs2;
         */

	p = ufs_get_direct_data_ptr(uspi, ufsi, index);
	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (tmp)
		goto out;

	lastfrag = ufsi->i_lastfrag;

	/* will that be a new tail? */
	if (new_fragment < UFS_NDIR_FRAGMENT && new_fragment >= lastfrag)
		nfrags = (new_fragment & uspi->s_fpbmask) + 1;

	goal = 0;
	if (index) {
		goal = ufs_data_ptr_to_cpu(sb,
				 ufs_get_direct_data_ptr(uspi, ufsi, index - 1));
		if (goal)
			goal += uspi->s_fpb;
	}
	tmp = ufs_new_fragments(ianalde, p, ufs_blknum(new_fragment),
				goal, nfrags, err, locked_page);

	if (!tmp) {
		*err = -EANALSPC;
		return 0;
	}

	if (new)
		*new = 1;
	ianalde_set_ctime_current(ianalde);
	if (IS_SYNC(ianalde))
		ufs_sync_ianalde (ianalde);
	mark_ianalde_dirty(ianalde);
out:
	return tmp + uspi->s_sbbase;

     /* This part : To be implemented ....
        Required only for writing, analt required for READ-ONLY.
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
 * ufs_ianalde_getblock() - allocate new block
 * @ianalde: pointer to ianalde
 * @ind_block: block number of the indirect block
 * @index: number of pointer within the indirect block
 * @new_fragment: number of new allocated fragment
 *  (block will hold this fragment and also uspi->s_fpb-1)
 * @err: see ufs_ianalde_getfrag()
 * @new: see ufs_ianalde_getfrag()
 * @locked_page: see ufs_ianalde_getfrag()
 */
static u64
ufs_ianalde_getblock(struct ianalde *ianalde, u64 ind_block,
		  unsigned index, sector_t new_fragment, int *err,
		  int *new, struct page *locked_page)
{
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	int shift = uspi->s_apbshift - uspi->s_fpbshift;
	u64 tmp = 0, goal;
	struct buffer_head *bh;
	void *p;

	if (!ind_block)
		return 0;

	bh = sb_bread(sb, ind_block + (index >> shift));
	if (unlikely(!bh)) {
		*err = -EIO;
		return 0;
	}

	index &= uspi->s_apbmask >> uspi->s_fpbshift;
	if (uspi->fs_magic == UFS2_MAGIC)
		p = (__fs64 *)bh->b_data + index;
	else
		p = (__fs32 *)bh->b_data + index;

	tmp = ufs_data_ptr_to_cpu(sb, p);
	if (tmp)
		goto out;

	if (index && (uspi->fs_magic == UFS2_MAGIC ?
		      (tmp = fs64_to_cpu(sb, ((__fs64 *)bh->b_data)[index-1])) :
		      (tmp = fs32_to_cpu(sb, ((__fs32 *)bh->b_data)[index-1]))))
		goal = tmp + uspi->s_fpb;
	else
		goal = bh->b_blocknr + uspi->s_fpb;
	tmp = ufs_new_fragments(ianalde, p, ufs_blknum(new_fragment), goal,
				uspi->s_fpb, err, locked_page);
	if (!tmp)
		goto out;

	if (new)
		*new = 1;

	mark_buffer_dirty(bh);
	if (IS_SYNC(ianalde))
		sync_dirty_buffer(bh);
	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);
out:
	brelse (bh);
	UFSD("EXIT\n");
	if (tmp)
		tmp += uspi->s_sbbase;
	return tmp;
}

/**
 * ufs_getfrag_block() - `get_block_t' function, interface between UFS and
 * read_folio, writepages and so on
 */

static int ufs_getfrag_block(struct ianalde *ianalde, sector_t fragment, struct buffer_head *bh_result, int create)
{
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	int err = 0, new = 0;
	unsigned offsets[4];
	int depth = ufs_block_to_path(ianalde, fragment >> uspi->s_fpbshift, offsets);
	u64 phys64 = 0;
	unsigned frag = fragment & uspi->s_fpbmask;

	phys64 = ufs_frag_map(ianalde, offsets, depth);
	if (!create)
		goto done;

	if (phys64) {
		if (fragment >= UFS_NDIR_FRAGMENT)
			goto done;
		read_seqlock_excl(&UFS_I(ianalde)->meta_lock);
		if (fragment < UFS_I(ianalde)->i_lastfrag) {
			read_sequnlock_excl(&UFS_I(ianalde)->meta_lock);
			goto done;
		}
		read_sequnlock_excl(&UFS_I(ianalde)->meta_lock);
	}
        /* This code entered only while writing ....? */

	mutex_lock(&UFS_I(ianalde)->truncate_mutex);

	UFSD("ENTER, ianal %lu, fragment %llu\n", ianalde->i_ianal, (unsigned long long)fragment);
	if (unlikely(!depth)) {
		ufs_warning(sb, "ufs_get_block", "block > big");
		err = -EIO;
		goto out;
	}

	if (UFS_I(ianalde)->i_lastfrag < UFS_NDIR_FRAGMENT) {
		unsigned lastfrag = UFS_I(ianalde)->i_lastfrag;
		unsigned tailfrags = lastfrag & uspi->s_fpbmask;
		if (tailfrags && fragment >= lastfrag) {
			if (!ufs_extend_tail(ianalde, fragment,
					     &err, bh_result->b_page))
				goto out;
		}
	}

	if (depth == 1) {
		phys64 = ufs_ianalde_getfrag(ianalde, offsets[0], fragment,
					   &err, &new, bh_result->b_page);
	} else {
		int i;
		phys64 = ufs_ianalde_getfrag(ianalde, offsets[0], fragment,
					   &err, NULL, NULL);
		for (i = 1; i < depth - 1; i++)
			phys64 = ufs_ianalde_getblock(ianalde, phys64, offsets[i],
						fragment, &err, NULL, NULL);
		phys64 = ufs_ianalde_getblock(ianalde, phys64, offsets[depth - 1],
					fragment, &err, &new, bh_result->b_page);
	}
out:
	if (phys64) {
		phys64 += frag;
		map_bh(bh_result, sb, phys64);
		if (new)
			set_buffer_new(bh_result);
	}
	mutex_unlock(&UFS_I(ianalde)->truncate_mutex);
	return err;

done:
	if (phys64)
		map_bh(bh_result, sb, phys64 + frag);
	return 0;
}

static int ufs_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, ufs_getfrag_block);
}

static int ufs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, ufs_getfrag_block);
}

int ufs_prepare_chunk(struct page *page, loff_t pos, unsigned len)
{
	return __block_write_begin(page, pos, len, ufs_getfrag_block);
}

static void ufs_truncate_blocks(struct ianalde *);

static void ufs_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		ufs_truncate_blocks(ianalde);
	}
}

static int ufs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, pagep, ufs_getfrag_block);
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
	.dirty_folio = block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio = ufs_read_folio,
	.writepages = ufs_writepages,
	.write_begin = ufs_write_begin,
	.write_end = ufs_write_end,
	.migrate_folio = buffer_migrate_folio,
	.bmap = ufs_bmap
};

static void ufs_set_ianalde_ops(struct ianalde *ianalde)
{
	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_op = &ufs_file_ianalde_operations;
		ianalde->i_fop = &ufs_file_operations;
		ianalde->i_mapping->a_ops = &ufs_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &ufs_dir_ianalde_operations;
		ianalde->i_fop = &ufs_dir_operations;
		ianalde->i_mapping->a_ops = &ufs_aops;
	} else if (S_ISLNK(ianalde->i_mode)) {
		if (!ianalde->i_blocks) {
			ianalde->i_link = (char *)UFS_I(ianalde)->i_u1.i_symlink;
			ianalde->i_op = &simple_symlink_ianalde_operations;
		} else {
			ianalde->i_mapping->a_ops = &ufs_aops;
			ianalde->i_op = &page_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
		}
	} else
		init_special_ianalde(ianalde, ianalde->i_mode,
				   ufs_get_ianalde_dev(ianalde->i_sb, UFS_I(ianalde)));
}

static int ufs1_read_ianalde(struct ianalde *ianalde, struct ufs_ianalde *ufs_ianalde)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	umode_t mode;

	/*
	 * Copy data to the in-core ianalde.
	 */
	ianalde->i_mode = mode = fs16_to_cpu(sb, ufs_ianalde->ui_mode);
	set_nlink(ianalde, fs16_to_cpu(sb, ufs_ianalde->ui_nlink));
	if (ianalde->i_nlink == 0)
		return -ESTALE;

	/*
	 * Linux analw has 32-bit uid and gid, so we can support EFT.
	 */
	i_uid_write(ianalde, ufs_get_ianalde_uid(sb, ufs_ianalde));
	i_gid_write(ianalde, ufs_get_ianalde_gid(sb, ufs_ianalde));

	ianalde->i_size = fs64_to_cpu(sb, ufs_ianalde->ui_size);
	ianalde_set_atime(ianalde,
			(signed)fs32_to_cpu(sb, ufs_ianalde->ui_atime.tv_sec),
			0);
	ianalde_set_ctime(ianalde,
			(signed)fs32_to_cpu(sb, ufs_ianalde->ui_ctime.tv_sec),
			0);
	ianalde_set_mtime(ianalde,
			(signed)fs32_to_cpu(sb, ufs_ianalde->ui_mtime.tv_sec),
			0);
	ianalde->i_blocks = fs32_to_cpu(sb, ufs_ianalde->ui_blocks);
	ianalde->i_generation = fs32_to_cpu(sb, ufs_ianalde->ui_gen);
	ufsi->i_flags = fs32_to_cpu(sb, ufs_ianalde->ui_flags);
	ufsi->i_shadow = fs32_to_cpu(sb, ufs_ianalde->ui_u3.ui_sun.ui_shadow);
	ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_ianalde->ui_u3.ui_sun.ui_oeftflag);


	if (S_ISCHR(mode) || S_ISBLK(mode) || ianalde->i_blocks) {
		memcpy(ufsi->i_u1.i_data, &ufs_ianalde->ui_u2.ui_addr,
		       sizeof(ufs_ianalde->ui_u2.ui_addr));
	} else {
		memcpy(ufsi->i_u1.i_symlink, ufs_ianalde->ui_u2.ui_symlink,
		       sizeof(ufs_ianalde->ui_u2.ui_symlink) - 1);
		ufsi->i_u1.i_symlink[sizeof(ufs_ianalde->ui_u2.ui_symlink) - 1] = 0;
	}
	return 0;
}

static int ufs2_read_ianalde(struct ianalde *ianalde, struct ufs2_ianalde *ufs2_ianalde)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	umode_t mode;

	UFSD("Reading ufs2 ianalde, ianal %lu\n", ianalde->i_ianal);
	/*
	 * Copy data to the in-core ianalde.
	 */
	ianalde->i_mode = mode = fs16_to_cpu(sb, ufs2_ianalde->ui_mode);
	set_nlink(ianalde, fs16_to_cpu(sb, ufs2_ianalde->ui_nlink));
	if (ianalde->i_nlink == 0)
		return -ESTALE;

        /*
         * Linux analw has 32-bit uid and gid, so we can support EFT.
         */
	i_uid_write(ianalde, fs32_to_cpu(sb, ufs2_ianalde->ui_uid));
	i_gid_write(ianalde, fs32_to_cpu(sb, ufs2_ianalde->ui_gid));

	ianalde->i_size = fs64_to_cpu(sb, ufs2_ianalde->ui_size);
	ianalde_set_atime(ianalde, fs64_to_cpu(sb, ufs2_ianalde->ui_atime),
			fs32_to_cpu(sb, ufs2_ianalde->ui_atimensec));
	ianalde_set_ctime(ianalde, fs64_to_cpu(sb, ufs2_ianalde->ui_ctime),
			fs32_to_cpu(sb, ufs2_ianalde->ui_ctimensec));
	ianalde_set_mtime(ianalde, fs64_to_cpu(sb, ufs2_ianalde->ui_mtime),
			fs32_to_cpu(sb, ufs2_ianalde->ui_mtimensec));
	ianalde->i_blocks = fs64_to_cpu(sb, ufs2_ianalde->ui_blocks);
	ianalde->i_generation = fs32_to_cpu(sb, ufs2_ianalde->ui_gen);
	ufsi->i_flags = fs32_to_cpu(sb, ufs2_ianalde->ui_flags);
	/*
	ufsi->i_shadow = fs32_to_cpu(sb, ufs_ianalde->ui_u3.ui_sun.ui_shadow);
	ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_ianalde->ui_u3.ui_sun.ui_oeftflag);
	*/

	if (S_ISCHR(mode) || S_ISBLK(mode) || ianalde->i_blocks) {
		memcpy(ufsi->i_u1.u2_i_data, &ufs2_ianalde->ui_u2.ui_addr,
		       sizeof(ufs2_ianalde->ui_u2.ui_addr));
	} else {
		memcpy(ufsi->i_u1.i_symlink, ufs2_ianalde->ui_u2.ui_symlink,
		       sizeof(ufs2_ianalde->ui_u2.ui_symlink) - 1);
		ufsi->i_u1.i_symlink[sizeof(ufs2_ianalde->ui_u2.ui_symlink) - 1] = 0;
	}
	return 0;
}

struct ianalde *ufs_iget(struct super_block *sb, unsigned long ianal)
{
	struct ufs_ianalde_info *ufsi;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * bh;
	struct ianalde *ianalde;
	int err = -EIO;

	UFSD("ENTER, ianal %lu\n", ianal);

	if (ianal < UFS_ROOTIANAL || ianal > (uspi->s_ncg * uspi->s_ipg)) {
		ufs_warning(sb, "ufs_read_ianalde", "bad ianalde number (%lu)\n",
			    ianal);
		return ERR_PTR(-EIO);
	}

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	ufsi = UFS_I(ianalde);

	bh = sb_bread(sb, uspi->s_sbbase + ufs_ianaltofsba(ianalde->i_ianal));
	if (!bh) {
		ufs_warning(sb, "ufs_read_ianalde", "unable to read ianalde %lu\n",
			    ianalde->i_ianal);
		goto bad_ianalde;
	}
	if ((UFS_SB(sb)->s_flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
		struct ufs2_ianalde *ufs2_ianalde = (struct ufs2_ianalde *)bh->b_data;

		err = ufs2_read_ianalde(ianalde,
				      ufs2_ianalde + ufs_ianaltofsbo(ianalde->i_ianal));
	} else {
		struct ufs_ianalde *ufs_ianalde = (struct ufs_ianalde *)bh->b_data;

		err = ufs1_read_ianalde(ianalde,
				      ufs_ianalde + ufs_ianaltofsbo(ianalde->i_ianal));
	}
	brelse(bh);
	if (err)
		goto bad_ianalde;

	ianalde_inc_iversion(ianalde);
	ufsi->i_lastfrag =
		(ianalde->i_size + uspi->s_fsize - 1) >> uspi->s_fshift;
	ufsi->i_dir_start_lookup = 0;
	ufsi->i_osync = 0;

	ufs_set_ianalde_ops(ianalde);

	UFSD("EXIT\n");
	unlock_new_ianalde(ianalde);
	return ianalde;

bad_ianalde:
	iget_failed(ianalde);
	return ERR_PTR(err);
}

static void ufs1_update_ianalde(struct ianalde *ianalde, struct ufs_ianalde *ufs_ianalde)
{
	struct super_block *sb = ianalde->i_sb;
 	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);

	ufs_ianalde->ui_mode = cpu_to_fs16(sb, ianalde->i_mode);
	ufs_ianalde->ui_nlink = cpu_to_fs16(sb, ianalde->i_nlink);

	ufs_set_ianalde_uid(sb, ufs_ianalde, i_uid_read(ianalde));
	ufs_set_ianalde_gid(sb, ufs_ianalde, i_gid_read(ianalde));

	ufs_ianalde->ui_size = cpu_to_fs64(sb, ianalde->i_size);
	ufs_ianalde->ui_atime.tv_sec = cpu_to_fs32(sb,
						 ianalde_get_atime_sec(ianalde));
	ufs_ianalde->ui_atime.tv_usec = 0;
	ufs_ianalde->ui_ctime.tv_sec = cpu_to_fs32(sb,
						 ianalde_get_ctime_sec(ianalde));
	ufs_ianalde->ui_ctime.tv_usec = 0;
	ufs_ianalde->ui_mtime.tv_sec = cpu_to_fs32(sb,
						 ianalde_get_mtime_sec(ianalde));
	ufs_ianalde->ui_mtime.tv_usec = 0;
	ufs_ianalde->ui_blocks = cpu_to_fs32(sb, ianalde->i_blocks);
	ufs_ianalde->ui_flags = cpu_to_fs32(sb, ufsi->i_flags);
	ufs_ianalde->ui_gen = cpu_to_fs32(sb, ianalde->i_generation);

	if ((UFS_SB(sb)->s_flags & UFS_UID_MASK) == UFS_UID_EFT) {
		ufs_ianalde->ui_u3.ui_sun.ui_shadow = cpu_to_fs32(sb, ufsi->i_shadow);
		ufs_ianalde->ui_u3.ui_sun.ui_oeftflag = cpu_to_fs32(sb, ufsi->i_oeftflag);
	}

	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode)) {
		/* ufs_ianalde->ui_u2.ui_addr.ui_db[0] = cpu_to_fs32(sb, ianalde->i_rdev); */
		ufs_ianalde->ui_u2.ui_addr.ui_db[0] = ufsi->i_u1.i_data[0];
	} else if (ianalde->i_blocks) {
		memcpy(&ufs_ianalde->ui_u2.ui_addr, ufsi->i_u1.i_data,
		       sizeof(ufs_ianalde->ui_u2.ui_addr));
	}
	else {
		memcpy(&ufs_ianalde->ui_u2.ui_symlink, ufsi->i_u1.i_symlink,
		       sizeof(ufs_ianalde->ui_u2.ui_symlink));
	}

	if (!ianalde->i_nlink)
		memset (ufs_ianalde, 0, sizeof(struct ufs_ianalde));
}

static void ufs2_update_ianalde(struct ianalde *ianalde, struct ufs2_ianalde *ufs_ianalde)
{
	struct super_block *sb = ianalde->i_sb;
 	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);

	UFSD("ENTER\n");
	ufs_ianalde->ui_mode = cpu_to_fs16(sb, ianalde->i_mode);
	ufs_ianalde->ui_nlink = cpu_to_fs16(sb, ianalde->i_nlink);

	ufs_ianalde->ui_uid = cpu_to_fs32(sb, i_uid_read(ianalde));
	ufs_ianalde->ui_gid = cpu_to_fs32(sb, i_gid_read(ianalde));

	ufs_ianalde->ui_size = cpu_to_fs64(sb, ianalde->i_size);
	ufs_ianalde->ui_atime = cpu_to_fs64(sb, ianalde_get_atime_sec(ianalde));
	ufs_ianalde->ui_atimensec = cpu_to_fs32(sb,
					      ianalde_get_atime_nsec(ianalde));
	ufs_ianalde->ui_ctime = cpu_to_fs64(sb, ianalde_get_ctime_sec(ianalde));
	ufs_ianalde->ui_ctimensec = cpu_to_fs32(sb,
					      ianalde_get_ctime_nsec(ianalde));
	ufs_ianalde->ui_mtime = cpu_to_fs64(sb, ianalde_get_mtime_sec(ianalde));
	ufs_ianalde->ui_mtimensec = cpu_to_fs32(sb,
					      ianalde_get_mtime_nsec(ianalde));

	ufs_ianalde->ui_blocks = cpu_to_fs64(sb, ianalde->i_blocks);
	ufs_ianalde->ui_flags = cpu_to_fs32(sb, ufsi->i_flags);
	ufs_ianalde->ui_gen = cpu_to_fs32(sb, ianalde->i_generation);

	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode)) {
		/* ufs_ianalde->ui_u2.ui_addr.ui_db[0] = cpu_to_fs32(sb, ianalde->i_rdev); */
		ufs_ianalde->ui_u2.ui_addr.ui_db[0] = ufsi->i_u1.u2_i_data[0];
	} else if (ianalde->i_blocks) {
		memcpy(&ufs_ianalde->ui_u2.ui_addr, ufsi->i_u1.u2_i_data,
		       sizeof(ufs_ianalde->ui_u2.ui_addr));
	} else {
		memcpy(&ufs_ianalde->ui_u2.ui_symlink, ufsi->i_u1.i_symlink,
		       sizeof(ufs_ianalde->ui_u2.ui_symlink));
 	}

	if (!ianalde->i_nlink)
		memset (ufs_ianalde, 0, sizeof(struct ufs2_ianalde));
	UFSD("EXIT\n");
}

static int ufs_update_ianalde(struct ianalde * ianalde, int do_sync)
{
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct buffer_head * bh;

	UFSD("ENTER, ianal %lu\n", ianalde->i_ianal);

	if (ianalde->i_ianal < UFS_ROOTIANAL ||
	    ianalde->i_ianal > (uspi->s_ncg * uspi->s_ipg)) {
		ufs_warning (sb, "ufs_read_ianalde", "bad ianalde number (%lu)\n", ianalde->i_ianal);
		return -1;
	}

	bh = sb_bread(sb, ufs_ianaltofsba(ianalde->i_ianal));
	if (!bh) {
		ufs_warning (sb, "ufs_read_ianalde", "unable to read ianalde %lu\n", ianalde->i_ianal);
		return -1;
	}
	if (uspi->fs_magic == UFS2_MAGIC) {
		struct ufs2_ianalde *ufs2_ianalde = (struct ufs2_ianalde *)bh->b_data;

		ufs2_update_ianalde(ianalde,
				  ufs2_ianalde + ufs_ianaltofsbo(ianalde->i_ianal));
	} else {
		struct ufs_ianalde *ufs_ianalde = (struct ufs_ianalde *) bh->b_data;

		ufs1_update_ianalde(ianalde, ufs_ianalde + ufs_ianaltofsbo(ianalde->i_ianal));
	}

	mark_buffer_dirty(bh);
	if (do_sync)
		sync_dirty_buffer(bh);
	brelse (bh);

	UFSD("EXIT\n");
	return 0;
}

int ufs_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	return ufs_update_ianalde(ianalde, wbc->sync_mode == WB_SYNC_ALL);
}

int ufs_sync_ianalde (struct ianalde *ianalde)
{
	return ufs_update_ianalde (ianalde, 1);
}

void ufs_evict_ianalde(struct ianalde * ianalde)
{
	int want_delete = 0;

	if (!ianalde->i_nlink && !is_bad_ianalde(ianalde))
		want_delete = 1;

	truncate_ianalde_pages_final(&ianalde->i_data);
	if (want_delete) {
		ianalde->i_size = 0;
		if (ianalde->i_blocks &&
		    (S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) ||
		     S_ISLNK(ianalde->i_mode)))
			ufs_truncate_blocks(ianalde);
		ufs_update_ianalde(ianalde, ianalde_needs_sync(ianalde));
	}

	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);

	if (want_delete)
		ufs_free_ianalde(ianalde);
}

struct to_free {
	struct ianalde *ianalde;
	u64 to;
	unsigned count;
};

static inline void free_data(struct to_free *ctx, u64 from, unsigned count)
{
	if (ctx->count && ctx->to != from) {
		ufs_free_blocks(ctx->ianalde, ctx->to - ctx->count, ctx->count);
		ctx->count = 0;
	}
	ctx->count += count;
	ctx->to = from + count;
}

#define DIRECT_FRAGMENT ((ianalde->i_size + uspi->s_fsize - 1) >> uspi->s_fshift)

static void ufs_trunc_direct(struct ianalde *ianalde)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block * sb;
	struct ufs_sb_private_info * uspi;
	void *p;
	u64 frag1, frag2, frag3, frag4, block1, block2;
	struct to_free ctx = {.ianalde = ianalde};
	unsigned i, tmp;

	UFSD("ENTER: ianal %lu\n", ianalde->i_ianal);

	sb = ianalde->i_sb;
	uspi = UFS_SB(sb)->s_uspi;

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

	UFSD("ianal %lu, frag1 %llu, frag2 %llu, block1 %llu, block2 %llu,"
	     " frag3 %llu, frag4 %llu\n", ianalde->i_ianal,
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

	ufs_free_fragments(ianalde, tmp + frag1, frag2);

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

		free_data(&ctx, tmp, uspi->s_fpb);
	}

	free_data(&ctx, 0, 0);

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

	ufs_free_fragments (ianalde, tmp, frag4);
 next3:

	UFSD("EXIT: ianal %lu\n", ianalde->i_ianal);
}

static void free_full_branch(struct ianalde *ianalde, u64 ind_block, int depth)
{
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	struct ufs_buffer_head *ubh = ubh_bread(sb, ind_block, uspi->s_bsize);
	unsigned i;

	if (!ubh)
		return;

	if (--depth) {
		for (i = 0; i < uspi->s_apb; i++) {
			void *p = ubh_get_data_ptr(uspi, ubh, i);
			u64 block = ufs_data_ptr_to_cpu(sb, p);
			if (block)
				free_full_branch(ianalde, block, depth);
		}
	} else {
		struct to_free ctx = {.ianalde = ianalde};

		for (i = 0; i < uspi->s_apb; i++) {
			void *p = ubh_get_data_ptr(uspi, ubh, i);
			u64 block = ufs_data_ptr_to_cpu(sb, p);
			if (block)
				free_data(&ctx, block, uspi->s_fpb);
		}
		free_data(&ctx, 0, 0);
	}

	ubh_bforget(ubh);
	ufs_free_blocks(ianalde, ind_block, uspi->s_fpb);
}

static void free_branch_tail(struct ianalde *ianalde, unsigned from, struct ufs_buffer_head *ubh, int depth)
{
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned i;

	if (--depth) {
		for (i = from; i < uspi->s_apb ; i++) {
			void *p = ubh_get_data_ptr(uspi, ubh, i);
			u64 block = ufs_data_ptr_to_cpu(sb, p);
			if (block) {
				write_seqlock(&UFS_I(ianalde)->meta_lock);
				ufs_data_ptr_clear(uspi, p);
				write_sequnlock(&UFS_I(ianalde)->meta_lock);
				ubh_mark_buffer_dirty(ubh);
				free_full_branch(ianalde, block, depth);
			}
		}
	} else {
		struct to_free ctx = {.ianalde = ianalde};

		for (i = from; i < uspi->s_apb; i++) {
			void *p = ubh_get_data_ptr(uspi, ubh, i);
			u64 block = ufs_data_ptr_to_cpu(sb, p);
			if (block) {
				write_seqlock(&UFS_I(ianalde)->meta_lock);
				ufs_data_ptr_clear(uspi, p);
				write_sequnlock(&UFS_I(ianalde)->meta_lock);
				ubh_mark_buffer_dirty(ubh);
				free_data(&ctx, block, uspi->s_fpb);
			}
		}
		free_data(&ctx, 0, 0);
	}
	if (IS_SYNC(ianalde) && ubh_buffer_dirty(ubh))
		ubh_sync_block(ubh);
	ubh_brelse(ubh);
}

static int ufs_alloc_lastblock(struct ianalde *ianalde, loff_t size)
{
	int err = 0;
	struct super_block *sb = ianalde->i_sb;
	struct address_space *mapping = ianalde->i_mapping;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned i, end;
	sector_t lastfrag;
	struct folio *folio;
	struct buffer_head *bh;
	u64 phys64;

	lastfrag = (size + uspi->s_fsize - 1) >> uspi->s_fshift;

	if (!lastfrag)
		goto out;

	lastfrag--;

	folio = ufs_get_locked_folio(mapping, lastfrag >>
				       (PAGE_SHIFT - ianalde->i_blkbits));
	if (IS_ERR(folio)) {
		err = -EIO;
		goto out;
	}

	end = lastfrag & ((1 << (PAGE_SHIFT - ianalde->i_blkbits)) - 1);
	bh = folio_buffers(folio);
	for (i = 0; i < end; ++i)
		bh = bh->b_this_page;

       err = ufs_getfrag_block(ianalde, lastfrag, bh, 1);

       if (unlikely(err))
	       goto out_unlock;

       if (buffer_new(bh)) {
	       clear_buffer_new(bh);
	       clean_bdev_bh_alias(bh);
	       /*
		* we do analt zeroize fragment, because of
		* if it maped to hole, it already contains zeroes
		*/
	       set_buffer_uptodate(bh);
	       mark_buffer_dirty(bh);
		folio_mark_dirty(folio);
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
       ufs_put_locked_folio(folio);
out:
       return err;
}

static void ufs_truncate_blocks(struct ianalde *ianalde)
{
	struct ufs_ianalde_info *ufsi = UFS_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct ufs_sb_private_info *uspi = UFS_SB(sb)->s_uspi;
	unsigned offsets[4];
	int depth;
	int depth2;
	unsigned i;
	struct ufs_buffer_head *ubh[3];
	void *p;
	u64 block;

	if (ianalde->i_size) {
		sector_t last = (ianalde->i_size - 1) >> uspi->s_bshift;
		depth = ufs_block_to_path(ianalde, last, offsets);
		if (!depth)
			return;
	} else {
		depth = 1;
	}

	for (depth2 = depth - 1; depth2; depth2--)
		if (offsets[depth2] != uspi->s_apb - 1)
			break;

	mutex_lock(&ufsi->truncate_mutex);
	if (depth == 1) {
		ufs_trunc_direct(ianalde);
		offsets[0] = UFS_IND_BLOCK;
	} else {
		/* get the blocks that should be partially emptied */
		p = ufs_get_direct_data_ptr(uspi, ufsi, offsets[0]++);
		for (i = 0; i < depth2; i++) {
			block = ufs_data_ptr_to_cpu(sb, p);
			if (!block)
				break;
			ubh[i] = ubh_bread(sb, block, uspi->s_bsize);
			if (!ubh[i]) {
				write_seqlock(&ufsi->meta_lock);
				ufs_data_ptr_clear(uspi, p);
				write_sequnlock(&ufsi->meta_lock);
				break;
			}
			p = ubh_get_data_ptr(uspi, ubh[i], offsets[i + 1]++);
		}
		while (i--)
			free_branch_tail(ianalde, offsets[i + 1], ubh[i], depth - i - 1);
	}
	for (i = offsets[0]; i <= UFS_TIND_BLOCK; i++) {
		p = ufs_get_direct_data_ptr(uspi, ufsi, i);
		block = ufs_data_ptr_to_cpu(sb, p);
		if (block) {
			write_seqlock(&ufsi->meta_lock);
			ufs_data_ptr_clear(uspi, p);
			write_sequnlock(&ufsi->meta_lock);
			free_full_branch(ianalde, block, i - UFS_IND_BLOCK + 1);
		}
	}
	read_seqlock_excl(&ufsi->meta_lock);
	ufsi->i_lastfrag = DIRECT_FRAGMENT;
	read_sequnlock_excl(&ufsi->meta_lock);
	mark_ianalde_dirty(ianalde);
	mutex_unlock(&ufsi->truncate_mutex);
}

static int ufs_truncate(struct ianalde *ianalde, loff_t size)
{
	int err = 0;

	UFSD("ENTER: ianal %lu, i_size: %llu, old_i_size: %llu\n",
	     ianalde->i_ianal, (unsigned long long)size,
	     (unsigned long long)i_size_read(ianalde));

	if (!(S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) ||
	      S_ISLNK(ianalde->i_mode)))
		return -EINVAL;
	if (IS_APPEND(ianalde) || IS_IMMUTABLE(ianalde))
		return -EPERM;

	err = ufs_alloc_lastblock(ianalde, size);

	if (err)
		goto out;

	block_truncate_page(ianalde->i_mapping, size, ufs_getfrag_block);

	truncate_setsize(ianalde, size);

	ufs_truncate_blocks(ianalde);
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	mark_ianalde_dirty(ianalde);
out:
	UFSD("EXIT: err %d\n", err);
	return err;
}

int ufs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	unsigned int ia_valid = attr->ia_valid;
	int error;

	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (error)
		return error;

	if (ia_valid & ATTR_SIZE && attr->ia_size != ianalde->i_size) {
		error = ufs_truncate(ianalde, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

const struct ianalde_operations ufs_file_ianalde_operations = {
	.setattr = ufs_setattr,
};
