// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/nls.h>
#include <linux/uio.h>
#include <linux/writeback.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * ntfs_read_mft - Read record and parses MFT.
 */
static struct inode *ntfs_read_mft(struct inode *inode,
				   const struct cpu_str *name,
				   const struct MFT_REF *ref)
{
	int err = 0;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	mode_t mode = 0;
	struct ATTR_STD_INFO5 *std5 = NULL;
	struct ATTR_LIST_ENTRY *le;
	struct ATTRIB *attr;
	bool is_match = false;
	bool is_root = false;
	bool is_dir;
	unsigned long ino = inode->i_ino;
	u32 rp_fa = 0, asize, t32;
	u16 roff, rsize, names = 0;
	const struct ATTR_FILE_NAME *fname = NULL;
	const struct INDEX_ROOT *root;
	struct REPARSE_DATA_BUFFER rp; // 0x18 bytes
	u64 t64;
	struct MFT_REC *rec;
	struct runs_tree *run;

	inode->i_op = NULL;
	/* Setup 'uid' and 'gid' */
	inode->i_uid = sbi->options->fs_uid;
	inode->i_gid = sbi->options->fs_gid;

	err = mi_init(&ni->mi, sbi, ino);
	if (err)
		goto out;

	if (!sbi->mft.ni && ino == MFT_REC_MFT && !sb->s_root) {
		t64 = sbi->mft.lbo >> sbi->cluster_bits;
		t32 = bytes_to_cluster(sbi, MFT_REC_VOL * sbi->record_size);
		sbi->mft.ni = ni;
		init_rwsem(&ni->file.run_lock);

		if (!run_add_entry(&ni->file.run, 0, t64, t32, true)) {
			err = -ENOMEM;
			goto out;
		}
	}

	err = mi_read(&ni->mi, ino == MFT_REC_MFT);

	if (err)
		goto out;

	rec = ni->mi.mrec;

	if (sbi->flags & NTFS_FLAGS_LOG_REPLAYING) {
		;
	} else if (ref->seq != rec->seq) {
		err = -EINVAL;
		ntfs_err(sb, "MFT: r=%lx, expect seq=%x instead of %x!", ino,
			 le16_to_cpu(ref->seq), le16_to_cpu(rec->seq));
		goto out;
	} else if (!is_rec_inuse(rec)) {
		err = -ESTALE;
		ntfs_err(sb, "Inode r=%x is not in use!", (u32)ino);
		goto out;
	}

	if (le32_to_cpu(rec->total) != sbi->record_size) {
		/* Bad inode? */
		err = -EINVAL;
		goto out;
	}

	if (!is_rec_base(rec)) {
		err = -EINVAL;
		goto out;
	}

	/* Record should contain $I30 root. */
	is_dir = rec->flags & RECORD_FLAG_DIR;

	/* MFT_REC_MFT is not a dir */
	if (is_dir && ino == MFT_REC_MFT) {
		err = -EINVAL;
		goto out;
	}

	inode->i_generation = le16_to_cpu(rec->seq);

	/* Enumerate all struct Attributes MFT. */
	le = NULL;
	attr = NULL;

	/*
	 * To reduce tab pressure use goto instead of
	 * while( (attr = ni_enum_attr_ex(ni, attr, &le, NULL) ))
	 */
next_attr:
	run = NULL;
	err = -EINVAL;
	attr = ni_enum_attr_ex(ni, attr, &le, NULL);
	if (!attr)
		goto end_enum;

	if (le && le->vcn) {
		/* This is non primary attribute segment. Ignore if not MFT. */
		if (ino != MFT_REC_MFT || attr->type != ATTR_DATA)
			goto next_attr;

		run = &ni->file.run;
		asize = le32_to_cpu(attr->size);
		goto attr_unpack_run;
	}

	roff = attr->non_res ? 0 : le16_to_cpu(attr->res.data_off);
	rsize = attr->non_res ? 0 : le32_to_cpu(attr->res.data_size);
	asize = le32_to_cpu(attr->size);

	if (le16_to_cpu(attr->name_off) + attr->name_len > asize)
		goto out;

	if (attr->non_res) {
		t64 = le64_to_cpu(attr->nres.alloc_size);
		if (le64_to_cpu(attr->nres.data_size) > t64 ||
		    le64_to_cpu(attr->nres.valid_size) > t64)
			goto out;
	}

	switch (attr->type) {
	case ATTR_STD:
		if (attr->non_res ||
		    asize < sizeof(struct ATTR_STD_INFO) + roff ||
		    rsize < sizeof(struct ATTR_STD_INFO))
			goto out;

		if (std5)
			goto next_attr;

		std5 = Add2Ptr(attr, roff);

#ifdef STATX_BTIME
		nt2kernel(std5->cr_time, &ni->i_crtime);
#endif
		nt2kernel(std5->a_time, &inode->i_atime);
		nt2kernel(std5->c_time, &inode->i_ctime);
		nt2kernel(std5->m_time, &inode->i_mtime);

		ni->std_fa = std5->fa;

		if (asize >= sizeof(struct ATTR_STD_INFO5) + roff &&
		    rsize >= sizeof(struct ATTR_STD_INFO5))
			ni->std_security_id = std5->security_id;
		goto next_attr;

	case ATTR_LIST:
		if (attr->name_len || le || ino == MFT_REC_LOG)
			goto out;

		err = ntfs_load_attr_list(ni, attr);
		if (err)
			goto out;

		le = NULL;
		attr = NULL;
		goto next_attr;

	case ATTR_NAME:
		if (attr->non_res || asize < SIZEOF_ATTRIBUTE_FILENAME + roff ||
		    rsize < SIZEOF_ATTRIBUTE_FILENAME)
			goto out;

		fname = Add2Ptr(attr, roff);
		if (fname->type == FILE_NAME_DOS)
			goto next_attr;

		names += 1;
		if (name && name->len == fname->name_len &&
		    !ntfs_cmp_names_cpu(name, (struct le_str *)&fname->name_len,
					NULL, false))
			is_match = true;

		goto next_attr;

	case ATTR_DATA:
		if (is_dir) {
			/* Ignore data attribute in dir record. */
			goto next_attr;
		}

		if (ino == MFT_REC_BADCLUST && !attr->non_res)
			goto next_attr;

		if (attr->name_len &&
		    ((ino != MFT_REC_BADCLUST || !attr->non_res ||
		      attr->name_len != ARRAY_SIZE(BAD_NAME) ||
		      memcmp(attr_name(attr), BAD_NAME, sizeof(BAD_NAME))) &&
		     (ino != MFT_REC_SECURE || !attr->non_res ||
		      attr->name_len != ARRAY_SIZE(SDS_NAME) ||
		      memcmp(attr_name(attr), SDS_NAME, sizeof(SDS_NAME))))) {
			/* File contains stream attribute. Ignore it. */
			goto next_attr;
		}

		if (is_attr_sparsed(attr))
			ni->std_fa |= FILE_ATTRIBUTE_SPARSE_FILE;
		else
			ni->std_fa &= ~FILE_ATTRIBUTE_SPARSE_FILE;

		if (is_attr_compressed(attr))
			ni->std_fa |= FILE_ATTRIBUTE_COMPRESSED;
		else
			ni->std_fa &= ~FILE_ATTRIBUTE_COMPRESSED;

		if (is_attr_encrypted(attr))
			ni->std_fa |= FILE_ATTRIBUTE_ENCRYPTED;
		else
			ni->std_fa &= ~FILE_ATTRIBUTE_ENCRYPTED;

		if (!attr->non_res) {
			ni->i_valid = inode->i_size = rsize;
			inode_set_bytes(inode, rsize);
		}

		mode = S_IFREG | (0777 & sbi->options->fs_fmask_inv);

		if (!attr->non_res) {
			ni->ni_flags |= NI_FLAG_RESIDENT;
			goto next_attr;
		}

		inode_set_bytes(inode, attr_ondisk_size(attr));

		ni->i_valid = le64_to_cpu(attr->nres.valid_size);
		inode->i_size = le64_to_cpu(attr->nres.data_size);
		if (!attr->nres.alloc_size)
			goto next_attr;

		run = ino == MFT_REC_BITMAP ? &sbi->used.bitmap.run
					    : &ni->file.run;
		break;

	case ATTR_ROOT:
		if (attr->non_res)
			goto out;

		root = Add2Ptr(attr, roff);

		if (attr->name_len != ARRAY_SIZE(I30_NAME) ||
		    memcmp(attr_name(attr), I30_NAME, sizeof(I30_NAME)))
			goto next_attr;

		if (root->type != ATTR_NAME ||
		    root->rule != NTFS_COLLATION_TYPE_FILENAME)
			goto out;

		if (!is_dir)
			goto next_attr;

		is_root = true;
		ni->ni_flags |= NI_FLAG_DIR;

		err = indx_init(&ni->dir, sbi, attr, INDEX_MUTEX_I30);
		if (err)
			goto out;

		mode = sb->s_root
			       ? (S_IFDIR | (0777 & sbi->options->fs_dmask_inv))
			       : (S_IFDIR | 0777);
		goto next_attr;

	case ATTR_ALLOC:
		if (!is_root || attr->name_len != ARRAY_SIZE(I30_NAME) ||
		    memcmp(attr_name(attr), I30_NAME, sizeof(I30_NAME)))
			goto next_attr;

		inode->i_size = le64_to_cpu(attr->nres.data_size);
		ni->i_valid = le64_to_cpu(attr->nres.valid_size);
		inode_set_bytes(inode, le64_to_cpu(attr->nres.alloc_size));

		run = &ni->dir.alloc_run;
		break;

	case ATTR_BITMAP:
		if (ino == MFT_REC_MFT) {
			if (!attr->non_res)
				goto out;
#ifndef CONFIG_NTFS3_64BIT_CLUSTER
			/* 0x20000000 = 2^32 / 8 */
			if (le64_to_cpu(attr->nres.alloc_size) >= 0x20000000)
				goto out;
#endif
			run = &sbi->mft.bitmap.run;
			break;
		} else if (is_dir && attr->name_len == ARRAY_SIZE(I30_NAME) &&
			   !memcmp(attr_name(attr), I30_NAME,
				   sizeof(I30_NAME)) &&
			   attr->non_res) {
			run = &ni->dir.bitmap_run;
			break;
		}
		goto next_attr;

	case ATTR_REPARSE:
		if (attr->name_len)
			goto next_attr;

		rp_fa = ni_parse_reparse(ni, attr, &rp);
		switch (rp_fa) {
		case REPARSE_LINK:
			/*
			 * Normal symlink.
			 * Assume one unicode symbol == one utf8.
			 */
			inode->i_size = le16_to_cpu(rp.SymbolicLinkReparseBuffer
							    .PrintNameLength) /
					sizeof(u16);

			ni->i_valid = inode->i_size;

			/* Clear directory bit. */
			if (ni->ni_flags & NI_FLAG_DIR) {
				indx_clear(&ni->dir);
				memset(&ni->dir, 0, sizeof(ni->dir));
				ni->ni_flags &= ~NI_FLAG_DIR;
			} else {
				run_close(&ni->file.run);
			}
			mode = S_IFLNK | 0777;
			is_dir = false;
			if (attr->non_res) {
				run = &ni->file.run;
				goto attr_unpack_run; // Double break.
			}
			break;

		case REPARSE_COMPRESSED:
			break;

		case REPARSE_DEDUPLICATED:
			break;
		}
		goto next_attr;

	case ATTR_EA_INFO:
		if (!attr->name_len &&
		    resident_data_ex(attr, sizeof(struct EA_INFO))) {
			ni->ni_flags |= NI_FLAG_EA;
			/*
			 * ntfs_get_wsl_perm updates inode->i_uid, inode->i_gid, inode->i_mode
			 */
			inode->i_mode = mode;
			ntfs_get_wsl_perm(inode);
			mode = inode->i_mode;
		}
		goto next_attr;

	default:
		goto next_attr;
	}

attr_unpack_run:
	roff = le16_to_cpu(attr->nres.run_off);

	if (roff > asize) {
		err = -EINVAL;
		goto out;
	}

	t64 = le64_to_cpu(attr->nres.svcn);

	err = run_unpack_ex(run, sbi, ino, t64, le64_to_cpu(attr->nres.evcn),
			    t64, Add2Ptr(attr, roff), asize - roff);
	if (err < 0)
		goto out;
	err = 0;
	goto next_attr;

end_enum:

	if (!std5)
		goto out;

	if (!is_match && name) {
		/* Reuse rec as buffer for ascii name. */
		err = -ENOENT;
		goto out;
	}

	if (std5->fa & FILE_ATTRIBUTE_READONLY)
		mode &= ~0222;

	if (!names) {
		err = -EINVAL;
		goto out;
	}

	if (names != le16_to_cpu(rec->hard_links)) {
		/* Correct minor error on the fly. Do not mark inode as dirty. */
		rec->hard_links = cpu_to_le16(names);
		ni->mi.dirty = true;
	}

	set_nlink(inode, names);

	if (S_ISDIR(mode)) {
		ni->std_fa |= FILE_ATTRIBUTE_DIRECTORY;

		/*
		 * Dot and dot-dot should be included in count but was not
		 * included in enumeration.
		 * Usually a hard links to directories are disabled.
		 */
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = &ntfs_dir_operations;
		ni->i_valid = 0;
	} else if (S_ISLNK(mode)) {
		ni->std_fa &= ~FILE_ATTRIBUTE_DIRECTORY;
		inode->i_op = &ntfs_link_inode_operations;
		inode->i_fop = NULL;
		inode_nohighmem(inode);
	} else if (S_ISREG(mode)) {
		ni->std_fa &= ~FILE_ATTRIBUTE_DIRECTORY;
		inode->i_op = &ntfs_file_inode_operations;
		inode->i_fop = &ntfs_file_operations;
		inode->i_mapping->a_ops =
			is_compressed(ni) ? &ntfs_aops_cmpr : &ntfs_aops;
		if (ino != MFT_REC_MFT)
			init_rwsem(&ni->file.run_lock);
	} else if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) ||
		   S_ISSOCK(mode)) {
		inode->i_op = &ntfs_special_inode_operations;
		init_special_inode(inode, mode, inode->i_rdev);
	} else if (fname && fname->home.low == cpu_to_le32(MFT_REC_EXTEND) &&
		   fname->home.seq == cpu_to_le16(MFT_REC_EXTEND)) {
		/* Records in $Extend are not a files or general directories. */
		inode->i_op = &ntfs_file_inode_operations;
	} else {
		err = -EINVAL;
		goto out;
	}

	if ((sbi->options->sys_immutable &&
	     (std5->fa & FILE_ATTRIBUTE_SYSTEM)) &&
	    !S_ISFIFO(mode) && !S_ISSOCK(mode) && !S_ISLNK(mode)) {
		inode->i_flags |= S_IMMUTABLE;
	} else {
		inode->i_flags &= ~S_IMMUTABLE;
	}

	inode->i_mode = mode;
	if (!(ni->ni_flags & NI_FLAG_EA)) {
		/* If no xattr then no security (stored in xattr). */
		inode->i_flags |= S_NOSEC;
	}

	if (ino == MFT_REC_MFT && !sb->s_root)
		sbi->mft.ni = NULL;

	unlock_new_inode(inode);

	return inode;

out:
	if (ino == MFT_REC_MFT && !sb->s_root)
		sbi->mft.ni = NULL;

	iget_failed(inode);
	return ERR_PTR(err);
}

/*
 * ntfs_test_inode
 *
 * Return: 1 if match.
 */
static int ntfs_test_inode(struct inode *inode, void *data)
{
	struct MFT_REF *ref = data;

	return ino_get(ref) == inode->i_ino;
}

static int ntfs_set_inode(struct inode *inode, void *data)
{
	const struct MFT_REF *ref = data;

	inode->i_ino = ino_get(ref);
	return 0;
}

struct inode *ntfs_iget5(struct super_block *sb, const struct MFT_REF *ref,
			 const struct cpu_str *name)
{
	struct inode *inode;

	inode = iget5_locked(sb, ino_get(ref), ntfs_test_inode, ntfs_set_inode,
			     (void *)ref);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);

	/* If this is a freshly allocated inode, need to read it now. */
	if (inode->i_state & I_NEW)
		inode = ntfs_read_mft(inode, name, ref);
	else if (ref->seq != ntfs_i(inode)->mi.mrec->seq) {
		/* Inode overlaps? */
		_ntfs_bad_inode(inode);
	}

	if (IS_ERR(inode) && name)
		ntfs_set_state(sb->s_fs_info, NTFS_DIRTY_ERROR);

	return inode;
}

enum get_block_ctx {
	GET_BLOCK_GENERAL = 0,
	GET_BLOCK_WRITE_BEGIN = 1,
	GET_BLOCK_DIRECT_IO_R = 2,
	GET_BLOCK_DIRECT_IO_W = 3,
	GET_BLOCK_BMAP = 4,
};

static noinline int ntfs_get_block_vbo(struct inode *inode, u64 vbo,
				       struct buffer_head *bh, int create,
				       enum get_block_ctx ctx)
{
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct page *page = bh->b_page;
	u8 cluster_bits = sbi->cluster_bits;
	u32 block_size = sb->s_blocksize;
	u64 bytes, lbo, valid;
	u32 off;
	int err;
	CLST vcn, lcn, len;
	bool new;

	/* Clear previous state. */
	clear_buffer_new(bh);
	clear_buffer_uptodate(bh);

	if (is_resident(ni)) {
		ni_lock(ni);
		err = attr_data_read_resident(ni, page);
		ni_unlock(ni);

		if (!err)
			set_buffer_uptodate(bh);
		bh->b_size = block_size;
		return err;
	}

	vcn = vbo >> cluster_bits;
	off = vbo & sbi->cluster_mask;
	new = false;

	err = attr_data_get_block(ni, vcn, 1, &lcn, &len, create ? &new : NULL,
				  create && sbi->cluster_size > PAGE_SIZE);
	if (err)
		goto out;

	if (!len)
		return 0;

	bytes = ((u64)len << cluster_bits) - off;

	if (lcn == SPARSE_LCN) {
		if (!create) {
			if (bh->b_size > bytes)
				bh->b_size = bytes;
			return 0;
		}
		WARN_ON(1);
	}

	if (new)
		set_buffer_new(bh);

	lbo = ((u64)lcn << cluster_bits) + off;

	set_buffer_mapped(bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_blocknr = lbo >> sb->s_blocksize_bits;

	valid = ni->i_valid;

	if (ctx == GET_BLOCK_DIRECT_IO_W) {
		/* ntfs_direct_IO will update ni->i_valid. */
		if (vbo >= valid)
			set_buffer_new(bh);
	} else if (create) {
		/* Normal write. */
		if (bytes > bh->b_size)
			bytes = bh->b_size;

		if (vbo >= valid)
			set_buffer_new(bh);

		if (vbo + bytes > valid) {
			ni->i_valid = vbo + bytes;
			mark_inode_dirty(inode);
		}
	} else if (vbo >= valid) {
		/* Read out of valid data. */
		clear_buffer_mapped(bh);
	} else if (vbo + bytes <= valid) {
		/* Normal read. */
	} else if (vbo + block_size <= valid) {
		/* Normal short read. */
		bytes = block_size;
	} else {
		/*
		 * Read across valid size: vbo < valid && valid < vbo + block_size
		 */
		bytes = block_size;

		if (page) {
			u32 voff = valid - vbo;

			bh->b_size = block_size;
			off = vbo & (PAGE_SIZE - 1);
			set_bh_page(bh, page, off);
			err = bh_read(bh, 0);
			if (err < 0)
				goto out;
			zero_user_segment(page, off + voff, off + block_size);
		}
	}

	if (bh->b_size > bytes)
		bh->b_size = bytes;

#ifndef __LP64__
	if (ctx == GET_BLOCK_DIRECT_IO_W || ctx == GET_BLOCK_DIRECT_IO_R) {
		static_assert(sizeof(size_t) < sizeof(loff_t));
		if (bytes > 0x40000000u)
			bh->b_size = 0x40000000u;
	}
#endif

	return 0;

out:
	return err;
}

int ntfs_get_block(struct inode *inode, sector_t vbn,
		   struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)vbn << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_GENERAL);
}

static int ntfs_get_block_bmap(struct inode *inode, sector_t vsn,
			       struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode,
				  (u64)vsn << inode->i_sb->s_blocksize_bits,
				  bh_result, create, GET_BLOCK_BMAP);
}

static sector_t ntfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ntfs_get_block_bmap);
}

static int ntfs_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	int err;
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);

	if (is_resident(ni)) {
		ni_lock(ni);
		err = attr_data_read_resident(ni, page);
		ni_unlock(ni);
		if (err != E_NTFS_NONRESIDENT) {
			unlock_page(page);
			return err;
		}
	}

	if (is_compressed(ni)) {
		ni_lock(ni);
		err = ni_readpage_cmpr(ni, page);
		ni_unlock(ni);
		return err;
	}

	/* Normal + sparse files. */
	return mpage_read_folio(folio, ntfs_get_block);
}

static void ntfs_readahead(struct readahead_control *rac)
{
	struct address_space *mapping = rac->mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 valid;
	loff_t pos;

	if (is_resident(ni)) {
		/* No readahead for resident. */
		return;
	}

	if (is_compressed(ni)) {
		/* No readahead for compressed. */
		return;
	}

	valid = ni->i_valid;
	pos = readahead_pos(rac);

	if (valid < i_size_read(inode) && pos <= valid &&
	    valid < pos + readahead_length(rac)) {
		/* Range cross 'valid'. Read it page by page. */
		return;
	}

	mpage_readahead(rac, ntfs_get_block);
}

static int ntfs_get_block_direct_IO_R(struct inode *inode, sector_t iblock,
				      struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)iblock << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_DIRECT_IO_R);
}

static int ntfs_get_block_direct_IO_W(struct inode *inode, sector_t iblock,
				      struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)iblock << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_DIRECT_IO_W);
}

static ssize_t ntfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t vbo = iocb->ki_pos;
	loff_t end;
	int wr = iov_iter_rw(iter) & WRITE;
	size_t iter_count = iov_iter_count(iter);
	loff_t valid;
	ssize_t ret;

	if (is_resident(ni)) {
		/* Switch to buffered write. */
		ret = 0;
		goto out;
	}

	ret = blockdev_direct_IO(iocb, inode, iter,
				 wr ? ntfs_get_block_direct_IO_W
				    : ntfs_get_block_direct_IO_R);

	if (ret > 0)
		end = vbo + ret;
	else if (wr && ret == -EIOCBQUEUED)
		end = vbo + iter_count;
	else
		goto out;

	valid = ni->i_valid;
	if (wr) {
		if (end > valid && !S_ISBLK(inode->i_mode)) {
			ni->i_valid = end;
			mark_inode_dirty(inode);
		}
	} else if (vbo < valid && valid < end) {
		/* Fix page. */
		iov_iter_revert(iter, end - valid);
		iov_iter_zero(end - valid, iter);
	}

out:
	return ret;
}

int ntfs_set_size(struct inode *inode, u64 new_size)
{
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *ni = ntfs_i(inode);
	int err;

	/* Check for maximum file size. */
	if (is_sparsed(ni) || is_compressed(ni)) {
		if (new_size > sbi->maxbytes_sparse) {
			err = -EFBIG;
			goto out;
		}
	} else if (new_size > sbi->maxbytes) {
		err = -EFBIG;
		goto out;
	}

	ni_lock(ni);
	down_write(&ni->file.run_lock);

	err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run, new_size,
			    &ni->i_valid, true, NULL);

	up_write(&ni->file.run_lock);
	ni_unlock(ni);

	mark_inode_dirty(inode);

out:
	return err;
}

static int ntfs_resident_writepage(struct folio *folio,
		struct writeback_control *wbc, void *data)
{
	struct address_space *mapping = data;
	struct ntfs_inode *ni = ntfs_i(mapping->host);
	int ret;

	ni_lock(ni);
	ret = attr_data_write_resident(ni, &folio->page);
	ni_unlock(ni);

	if (ret != E_NTFS_NONRESIDENT)
		folio_unlock(folio);
	mapping_set_error(mapping, ret);
	return ret;
}

static int ntfs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	if (is_resident(ntfs_i(mapping->host)))
		return write_cache_pages(mapping, wbc, ntfs_resident_writepage,
					 mapping);
	return mpage_writepages(mapping, wbc, ntfs_get_block);
}

static int ntfs_get_block_write_begin(struct inode *inode, sector_t vbn,
				      struct buffer_head *bh_result, int create)
{
	return ntfs_get_block_vbo(inode, (u64)vbn << inode->i_blkbits,
				  bh_result, create, GET_BLOCK_WRITE_BEGIN);
}

int ntfs_write_begin(struct file *file, struct address_space *mapping,
		     loff_t pos, u32 len, struct page **pagep, void **fsdata)
{
	int err;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);

	*pagep = NULL;
	if (is_resident(ni)) {
		struct page *page = grab_cache_page_write_begin(
			mapping, pos >> PAGE_SHIFT);

		if (!page) {
			err = -ENOMEM;
			goto out;
		}

		ni_lock(ni);
		err = attr_data_read_resident(ni, page);
		ni_unlock(ni);

		if (!err) {
			*pagep = page;
			goto out;
		}
		unlock_page(page);
		put_page(page);

		if (err != E_NTFS_NONRESIDENT)
			goto out;
	}

	err = block_write_begin(mapping, pos, len, pagep,
				ntfs_get_block_write_begin);

out:
	return err;
}

/*
 * ntfs_write_end - Address_space_operations::write_end.
 */
int ntfs_write_end(struct file *file, struct address_space *mapping,
		   loff_t pos, u32 len, u32 copied, struct page *page,
		   void *fsdata)
{
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 valid = ni->i_valid;
	bool dirty = false;
	int err;

	if (is_resident(ni)) {
		ni_lock(ni);
		err = attr_data_write_resident(ni, page);
		ni_unlock(ni);
		if (!err) {
			dirty = true;
			/* Clear any buffers in page. */
			if (page_has_buffers(page)) {
				struct buffer_head *head, *bh;

				bh = head = page_buffers(page);
				do {
					clear_buffer_dirty(bh);
					clear_buffer_mapped(bh);
					set_buffer_uptodate(bh);
				} while (head != (bh = bh->b_this_page));
			}
			SetPageUptodate(page);
			err = copied;
		}
		unlock_page(page);
		put_page(page);
	} else {
		err = generic_write_end(file, mapping, pos, len, copied, page,
					fsdata);
	}

	if (err >= 0) {
		if (!(ni->std_fa & FILE_ATTRIBUTE_ARCHIVE)) {
			inode->i_ctime = inode->i_mtime = current_time(inode);
			ni->std_fa |= FILE_ATTRIBUTE_ARCHIVE;
			dirty = true;
		}

		if (valid != ni->i_valid) {
			/* ni->i_valid is changed in ntfs_get_block_vbo. */
			dirty = true;
		}

		if (pos + err > inode->i_size) {
			inode->i_size = pos + err;
			dirty = true;
		}

		if (dirty)
			mark_inode_dirty(inode);
	}

	return err;
}

int reset_log_file(struct inode *inode)
{
	int err;
	loff_t pos = 0;
	u32 log_size = inode->i_size;
	struct address_space *mapping = inode->i_mapping;

	for (;;) {
		u32 len;
		void *kaddr;
		struct page *page;

		len = pos + PAGE_SIZE > log_size ? (log_size - pos) : PAGE_SIZE;

		err = block_write_begin(mapping, pos, len, &page,
					ntfs_get_block_write_begin);
		if (err)
			goto out;

		kaddr = kmap_atomic(page);
		memset(kaddr, -1, len);
		kunmap_atomic(kaddr);
		flush_dcache_page(page);

		err = block_write_end(NULL, mapping, pos, len, len, page, NULL);
		if (err < 0)
			goto out;
		pos += len;

		if (pos >= log_size)
			break;
		balance_dirty_pages_ratelimited(mapping);
	}
out:
	mark_inode_dirty_sync(inode);

	return err;
}

int ntfs3_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return _ni_write_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
}

int ntfs_sync_inode(struct inode *inode)
{
	return _ni_write_inode(inode, 1);
}

/*
 * writeback_inode - Helper function for ntfs_flush_inodes().
 *
 * This writes both the inode and the file data blocks, waiting
 * for in flight data blocks before the start of the call.  It
 * does not wait for any io started during the call.
 */
static int writeback_inode(struct inode *inode)
{
	int ret = sync_inode_metadata(inode, 0);

	if (!ret)
		ret = filemap_fdatawrite(inode->i_mapping);
	return ret;
}

/*
 * ntfs_flush_inodes
 *
 * Write data and metadata corresponding to i1 and i2.  The io is
 * started but we do not wait for any of it to finish.
 *
 * filemap_flush() is used for the block device, so if there is a dirty
 * page for a block already in flight, we will not wait and start the
 * io over again.
 */
int ntfs_flush_inodes(struct super_block *sb, struct inode *i1,
		      struct inode *i2)
{
	int ret = 0;

	if (i1)
		ret = writeback_inode(i1);
	if (!ret && i2)
		ret = writeback_inode(i2);
	if (!ret)
		ret = sync_blockdev_nowait(sb->s_bdev);
	return ret;
}

int inode_write_data(struct inode *inode, const void *data, size_t bytes)
{
	pgoff_t idx;

	/* Write non resident data. */
	for (idx = 0; bytes; idx++) {
		size_t op = bytes > PAGE_SIZE ? PAGE_SIZE : bytes;
		struct page *page = ntfs_map_page(inode->i_mapping, idx);

		if (IS_ERR(page))
			return PTR_ERR(page);

		lock_page(page);
		WARN_ON(!PageUptodate(page));
		ClearPageUptodate(page);

		memcpy(page_address(page), data, op);

		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);

		ntfs_unmap_page(page);

		bytes -= op;
		data = Add2Ptr(data, PAGE_SIZE);
	}
	return 0;
}

/*
 * ntfs_reparse_bytes
 *
 * Number of bytes for REPARSE_DATA_BUFFER(IO_REPARSE_TAG_SYMLINK)
 * for unicode string of @uni_len length.
 */
static inline u32 ntfs_reparse_bytes(u32 uni_len)
{
	/* Header + unicode string + decorated unicode string. */
	return sizeof(short) * (2 * uni_len + 4) +
	       offsetof(struct REPARSE_DATA_BUFFER,
			SymbolicLinkReparseBuffer.PathBuffer);
}

static struct REPARSE_DATA_BUFFER *
ntfs_create_reparse_buffer(struct ntfs_sb_info *sbi, const char *symname,
			   u32 size, u16 *nsize)
{
	int i, err;
	struct REPARSE_DATA_BUFFER *rp;
	__le16 *rp_name;
	typeof(rp->SymbolicLinkReparseBuffer) *rs;

	rp = kzalloc(ntfs_reparse_bytes(2 * size + 2), GFP_NOFS);
	if (!rp)
		return ERR_PTR(-ENOMEM);

	rs = &rp->SymbolicLinkReparseBuffer;
	rp_name = rs->PathBuffer;

	/* Convert link name to UTF-16. */
	err = ntfs_nls_to_utf16(sbi, symname, size,
				(struct cpu_str *)(rp_name - 1), 2 * size,
				UTF16_LITTLE_ENDIAN);
	if (err < 0)
		goto out;

	/* err = the length of unicode name of symlink. */
	*nsize = ntfs_reparse_bytes(err);

	if (*nsize > sbi->reparse.max_size) {
		err = -EFBIG;
		goto out;
	}

	/* Translate Linux '/' into Windows '\'. */
	for (i = 0; i < err; i++) {
		if (rp_name[i] == cpu_to_le16('/'))
			rp_name[i] = cpu_to_le16('\\');
	}

	rp->ReparseTag = IO_REPARSE_TAG_SYMLINK;
	rp->ReparseDataLength =
		cpu_to_le16(*nsize - offsetof(struct REPARSE_DATA_BUFFER,
					      SymbolicLinkReparseBuffer));

	/* PrintName + SubstituteName. */
	rs->SubstituteNameOffset = cpu_to_le16(sizeof(short) * err);
	rs->SubstituteNameLength = cpu_to_le16(sizeof(short) * err + 8);
	rs->PrintNameLength = rs->SubstituteNameOffset;

	/*
	 * TODO: Use relative path if possible to allow Windows to
	 * parse this path.
	 * 0-absolute path 1- relative path (SYMLINK_FLAG_RELATIVE).
	 */
	rs->Flags = 0;

	memmove(rp_name + err + 4, rp_name, sizeof(short) * err);

	/* Decorate SubstituteName. */
	rp_name += err;
	rp_name[0] = cpu_to_le16('\\');
	rp_name[1] = cpu_to_le16('?');
	rp_name[2] = cpu_to_le16('?');
	rp_name[3] = cpu_to_le16('\\');

	return rp;
out:
	kfree(rp);
	return ERR_PTR(err);
}

/*
 * ntfs_create_inode
 *
 * Helper function for:
 * - ntfs_create
 * - ntfs_mknod
 * - ntfs_symlink
 * - ntfs_mkdir
 * - ntfs_atomic_open
 * 
 * NOTE: if fnd != NULL (ntfs_atomic_open) then @dir is locked
 */
struct inode *ntfs_create_inode(struct mnt_idmap *idmap,
				struct inode *dir, struct dentry *dentry,
				const struct cpu_str *uni, umode_t mode,
				dev_t dev, const char *symname, u32 size,
				struct ntfs_fnd *fnd)
{
	int err;
	struct super_block *sb = dir->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	const struct qstr *name = &dentry->d_name;
	CLST ino = 0;
	struct ntfs_inode *dir_ni = ntfs_i(dir);
	struct ntfs_inode *ni = NULL;
	struct inode *inode = NULL;
	struct ATTRIB *attr;
	struct ATTR_STD_INFO5 *std5;
	struct ATTR_FILE_NAME *fname;
	struct MFT_REC *rec;
	u32 asize, dsize, sd_size;
	enum FILE_ATTRIBUTE fa;
	__le32 security_id = SECURITY_ID_INVALID;
	CLST vcn;
	const void *sd;
	u16 t16, nsize = 0, aid = 0;
	struct INDEX_ROOT *root, *dir_root;
	struct NTFS_DE *e, *new_de = NULL;
	struct REPARSE_DATA_BUFFER *rp = NULL;
	bool rp_inserted = false;

	if (!fnd)
		ni_lock_dir(dir_ni);

	dir_root = indx_get_root(&dir_ni->dir, dir_ni, NULL, NULL);
	if (!dir_root) {
		err = -EINVAL;
		goto out1;
	}

	if (S_ISDIR(mode)) {
		/* Use parent's directory attributes. */
		fa = dir_ni->std_fa | FILE_ATTRIBUTE_DIRECTORY |
		     FILE_ATTRIBUTE_ARCHIVE;
		/*
		 * By default child directory inherits parent attributes.
		 * Root directory is hidden + system.
		 * Make an exception for children in root.
		 */
		if (dir->i_ino == MFT_REC_ROOT)
			fa &= ~(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
	} else if (S_ISLNK(mode)) {
		/* It is good idea that link should be the same type (file/dir) as target */
		fa = FILE_ATTRIBUTE_REPARSE_POINT;

		/*
		 * Linux: there are dir/file/symlink and so on.
		 * NTFS: symlinks are "dir + reparse" or "file + reparse"
		 * It is good idea to create:
		 * dir + reparse if 'symname' points to directory
		 * or
		 * file + reparse if 'symname' points to file
		 * Unfortunately kern_path hangs if symname contains 'dir'.
		 */

		/*
		 *	struct path path;
		 *
		 *	if (!kern_path(symname, LOOKUP_FOLLOW, &path)){
		 *		struct inode *target = d_inode(path.dentry);
		 *
		 *		if (S_ISDIR(target->i_mode))
		 *			fa |= FILE_ATTRIBUTE_DIRECTORY;
		 *		// if ( target->i_sb == sb ){
		 *		//	use relative path?
		 *		// }
		 *		path_put(&path);
		 *	}
		 */
	} else if (S_ISREG(mode)) {
		if (sbi->options->sparse) {
			/* Sparsed regular file, cause option 'sparse'. */
			fa = FILE_ATTRIBUTE_SPARSE_FILE |
			     FILE_ATTRIBUTE_ARCHIVE;
		} else if (dir_ni->std_fa & FILE_ATTRIBUTE_COMPRESSED) {
			/* Compressed regular file, if parent is compressed. */
			fa = FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ARCHIVE;
		} else {
			/* Regular file, default attributes. */
			fa = FILE_ATTRIBUTE_ARCHIVE;
		}
	} else {
		fa = FILE_ATTRIBUTE_ARCHIVE;
	}

	/* If option "hide_dot_files" then set hidden attribute for dot files. */
	if (sbi->options->hide_dot_files && name->name[0] == '.')
		fa |= FILE_ATTRIBUTE_HIDDEN;

	if (!(mode & 0222))
		fa |= FILE_ATTRIBUTE_READONLY;

	/* Allocate PATH_MAX bytes. */
	new_de = __getname();
	if (!new_de) {
		err = -ENOMEM;
		goto out1;
	}

	/* Mark rw ntfs as dirty. it will be cleared at umount. */
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	/* Step 1: allocate and fill new mft record. */
	err = ntfs_look_free_mft(sbi, &ino, false, NULL, NULL);
	if (err)
		goto out2;

	ni = ntfs_new_inode(sbi, ino, fa & FILE_ATTRIBUTE_DIRECTORY);
	if (IS_ERR(ni)) {
		err = PTR_ERR(ni);
		ni = NULL;
		goto out3;
	}
	inode = &ni->vfs_inode;
	inode_init_owner(idmap, inode, dir, mode);
	mode = inode->i_mode;

	inode->i_atime = inode->i_mtime = inode->i_ctime = ni->i_crtime =
		current_time(inode);

	rec = ni->mi.mrec;
	rec->hard_links = cpu_to_le16(1);
	attr = Add2Ptr(rec, le16_to_cpu(rec->attr_off));

	/* Get default security id. */
	sd = s_default_security;
	sd_size = sizeof(s_default_security);

	if (is_ntfs3(sbi)) {
		security_id = dir_ni->std_security_id;
		if (le32_to_cpu(security_id) < SECURITY_ID_FIRST) {
			security_id = sbi->security.def_security_id;

			if (security_id == SECURITY_ID_INVALID &&
			    !ntfs_insert_security(sbi, sd, sd_size,
						  &security_id, NULL))
				sbi->security.def_security_id = security_id;
		}
	}

	/* Insert standard info. */
	std5 = Add2Ptr(attr, SIZEOF_RESIDENT);

	if (security_id == SECURITY_ID_INVALID) {
		dsize = sizeof(struct ATTR_STD_INFO);
	} else {
		dsize = sizeof(struct ATTR_STD_INFO5);
		std5->security_id = security_id;
		ni->std_security_id = security_id;
	}
	asize = SIZEOF_RESIDENT + dsize;

	attr->type = ATTR_STD;
	attr->size = cpu_to_le32(asize);
	attr->id = cpu_to_le16(aid++);
	attr->res.data_off = SIZEOF_RESIDENT_LE;
	attr->res.data_size = cpu_to_le32(dsize);

	std5->cr_time = std5->m_time = std5->c_time = std5->a_time =
		kernel2nt(&inode->i_atime);

	ni->std_fa = fa;
	std5->fa = fa;

	attr = Add2Ptr(attr, asize);

	/* Insert file name. */
	err = fill_name_de(sbi, new_de, name, uni);
	if (err)
		goto out4;

	mi_get_ref(&ni->mi, &new_de->ref);

	fname = (struct ATTR_FILE_NAME *)(new_de + 1);

	if (sbi->options->windows_names &&
	    !valid_windows_name(sbi, (struct le_str *)&fname->name_len)) {
		err = -EINVAL;
		goto out4;
	}

	mi_get_ref(&dir_ni->mi, &fname->home);
	fname->dup.cr_time = fname->dup.m_time = fname->dup.c_time =
		fname->dup.a_time = std5->cr_time;
	fname->dup.alloc_size = fname->dup.data_size = 0;
	fname->dup.fa = std5->fa;
	fname->dup.ea_size = fname->dup.reparse = 0;

	dsize = le16_to_cpu(new_de->key_size);
	asize = ALIGN(SIZEOF_RESIDENT + dsize, 8);

	attr->type = ATTR_NAME;
	attr->size = cpu_to_le32(asize);
	attr->res.data_off = SIZEOF_RESIDENT_LE;
	attr->res.flags = RESIDENT_FLAG_INDEXED;
	attr->id = cpu_to_le16(aid++);
	attr->res.data_size = cpu_to_le32(dsize);
	memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), fname, dsize);

	attr = Add2Ptr(attr, asize);

	if (security_id == SECURITY_ID_INVALID) {
		/* Insert security attribute. */
		asize = SIZEOF_RESIDENT + ALIGN(sd_size, 8);

		attr->type = ATTR_SECURE;
		attr->size = cpu_to_le32(asize);
		attr->id = cpu_to_le16(aid++);
		attr->res.data_off = SIZEOF_RESIDENT_LE;
		attr->res.data_size = cpu_to_le32(sd_size);
		memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), sd, sd_size);

		attr = Add2Ptr(attr, asize);
	}

	attr->id = cpu_to_le16(aid++);
	if (fa & FILE_ATTRIBUTE_DIRECTORY) {
		/*
		 * Regular directory or symlink to directory.
		 * Create root attribute.
		 */
		dsize = sizeof(struct INDEX_ROOT) + sizeof(struct NTFS_DE);
		asize = sizeof(I30_NAME) + SIZEOF_RESIDENT + dsize;

		attr->type = ATTR_ROOT;
		attr->size = cpu_to_le32(asize);

		attr->name_len = ARRAY_SIZE(I30_NAME);
		attr->name_off = SIZEOF_RESIDENT_LE;
		attr->res.data_off =
			cpu_to_le16(sizeof(I30_NAME) + SIZEOF_RESIDENT);
		attr->res.data_size = cpu_to_le32(dsize);
		memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), I30_NAME,
		       sizeof(I30_NAME));

		root = Add2Ptr(attr, sizeof(I30_NAME) + SIZEOF_RESIDENT);
		memcpy(root, dir_root, offsetof(struct INDEX_ROOT, ihdr));
		root->ihdr.de_off =
			cpu_to_le32(sizeof(struct INDEX_HDR)); // 0x10
		root->ihdr.used = cpu_to_le32(sizeof(struct INDEX_HDR) +
					      sizeof(struct NTFS_DE));
		root->ihdr.total = root->ihdr.used;

		e = Add2Ptr(root, sizeof(struct INDEX_ROOT));
		e->size = cpu_to_le16(sizeof(struct NTFS_DE));
		e->flags = NTFS_IE_LAST;
	} else if (S_ISLNK(mode)) {
		/*
		 * Symlink to file.
		 * Create empty resident data attribute.
		 */
		asize = SIZEOF_RESIDENT;

		/* Insert empty ATTR_DATA */
		attr->type = ATTR_DATA;
		attr->size = cpu_to_le32(SIZEOF_RESIDENT);
		attr->name_off = SIZEOF_RESIDENT_LE;
		attr->res.data_off = SIZEOF_RESIDENT_LE;
	} else if (S_ISREG(mode)) {
		/*
		 * Regular file. Create empty non resident data attribute.
		 */
		attr->type = ATTR_DATA;
		attr->non_res = 1;
		attr->nres.evcn = cpu_to_le64(-1ll);
		if (fa & FILE_ATTRIBUTE_SPARSE_FILE) {
			attr->size = cpu_to_le32(SIZEOF_NONRESIDENT_EX + 8);
			attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
			attr->flags = ATTR_FLAG_SPARSED;
			asize = SIZEOF_NONRESIDENT_EX + 8;
		} else if (fa & FILE_ATTRIBUTE_COMPRESSED) {
			attr->size = cpu_to_le32(SIZEOF_NONRESIDENT_EX + 8);
			attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
			attr->flags = ATTR_FLAG_COMPRESSED;
			attr->nres.c_unit = COMPRESSION_UNIT;
			asize = SIZEOF_NONRESIDENT_EX + 8;
		} else {
			attr->size = cpu_to_le32(SIZEOF_NONRESIDENT + 8);
			attr->name_off = SIZEOF_NONRESIDENT_LE;
			asize = SIZEOF_NONRESIDENT + 8;
		}
		attr->nres.run_off = attr->name_off;
	} else {
		/*
		 * Node. Create empty resident data attribute.
		 */
		attr->type = ATTR_DATA;
		attr->size = cpu_to_le32(SIZEOF_RESIDENT);
		attr->name_off = SIZEOF_RESIDENT_LE;
		if (fa & FILE_ATTRIBUTE_SPARSE_FILE)
			attr->flags = ATTR_FLAG_SPARSED;
		else if (fa & FILE_ATTRIBUTE_COMPRESSED)
			attr->flags = ATTR_FLAG_COMPRESSED;
		attr->res.data_off = SIZEOF_RESIDENT_LE;
		asize = SIZEOF_RESIDENT;
		ni->ni_flags |= NI_FLAG_RESIDENT;
	}

	if (S_ISDIR(mode)) {
		ni->ni_flags |= NI_FLAG_DIR;
		err = indx_init(&ni->dir, sbi, attr, INDEX_MUTEX_I30);
		if (err)
			goto out4;
	} else if (S_ISLNK(mode)) {
		rp = ntfs_create_reparse_buffer(sbi, symname, size, &nsize);

		if (IS_ERR(rp)) {
			err = PTR_ERR(rp);
			rp = NULL;
			goto out4;
		}

		/*
		 * Insert ATTR_REPARSE.
		 */
		attr = Add2Ptr(attr, asize);
		attr->type = ATTR_REPARSE;
		attr->id = cpu_to_le16(aid++);

		/* Resident or non resident? */
		asize = ALIGN(SIZEOF_RESIDENT + nsize, 8);
		t16 = PtrOffset(rec, attr);

		/*
		 * Below function 'ntfs_save_wsl_perm' requires 0x78 bytes.
		 * It is good idea to keep extened attributes resident.
		 */
		if (asize + t16 + 0x78 + 8 > sbi->record_size) {
			CLST alen;
			CLST clst = bytes_to_cluster(sbi, nsize);

			/* Bytes per runs. */
			t16 = sbi->record_size - t16 - SIZEOF_NONRESIDENT;

			attr->non_res = 1;
			attr->nres.evcn = cpu_to_le64(clst - 1);
			attr->name_off = SIZEOF_NONRESIDENT_LE;
			attr->nres.run_off = attr->name_off;
			attr->nres.data_size = cpu_to_le64(nsize);
			attr->nres.valid_size = attr->nres.data_size;
			attr->nres.alloc_size =
				cpu_to_le64(ntfs_up_cluster(sbi, nsize));

			err = attr_allocate_clusters(sbi, &ni->file.run, 0, 0,
						     clst, NULL, ALLOCATE_DEF,
						     &alen, 0, NULL, NULL);
			if (err)
				goto out5;

			err = run_pack(&ni->file.run, 0, clst,
				       Add2Ptr(attr, SIZEOF_NONRESIDENT), t16,
				       &vcn);
			if (err < 0)
				goto out5;

			if (vcn != clst) {
				err = -EINVAL;
				goto out5;
			}

			asize = SIZEOF_NONRESIDENT + ALIGN(err, 8);
		} else {
			attr->res.data_off = SIZEOF_RESIDENT_LE;
			attr->res.data_size = cpu_to_le32(nsize);
			memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), rp, nsize);
			nsize = 0;
		}
		/* Size of symlink equals the length of input string. */
		inode->i_size = size;

		attr->size = cpu_to_le32(asize);

		err = ntfs_insert_reparse(sbi, IO_REPARSE_TAG_SYMLINK,
					  &new_de->ref);
		if (err)
			goto out5;

		rp_inserted = true;
	}

	attr = Add2Ptr(attr, asize);
	attr->type = ATTR_END;

	rec->used = cpu_to_le32(PtrOffset(rec, attr) + 8);
	rec->next_attr_id = cpu_to_le16(aid);

	/* Step 2: Add new name in index. */
	err = indx_insert_entry(&dir_ni->dir, dir_ni, new_de, sbi, fnd, 0);
	if (err)
		goto out6;

	/* Unlock parent directory before ntfs_init_acl. */
	if (!fnd)
		ni_unlock(dir_ni);

	inode->i_generation = le16_to_cpu(rec->seq);

	dir->i_mtime = dir->i_ctime = inode->i_atime;

	if (S_ISDIR(mode)) {
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = &ntfs_dir_operations;
	} else if (S_ISLNK(mode)) {
		inode->i_op = &ntfs_link_inode_operations;
		inode->i_fop = NULL;
		inode->i_mapping->a_ops = &ntfs_aops;
		inode->i_size = size;
		inode_nohighmem(inode);
	} else if (S_ISREG(mode)) {
		inode->i_op = &ntfs_file_inode_operations;
		inode->i_fop = &ntfs_file_operations;
		inode->i_mapping->a_ops =
			is_compressed(ni) ? &ntfs_aops_cmpr : &ntfs_aops;
		init_rwsem(&ni->file.run_lock);
	} else {
		inode->i_op = &ntfs_special_inode_operations;
		init_special_inode(inode, mode, dev);
	}

#ifdef CONFIG_NTFS3_FS_POSIX_ACL
	if (!S_ISLNK(mode) && (sb->s_flags & SB_POSIXACL)) {
		err = ntfs_init_acl(idmap, inode, dir);
		if (err)
			goto out7;
	} else
#endif
	{
		inode->i_flags |= S_NOSEC;
	}

	/* Write non resident data. */
	if (nsize) {
		err = ntfs_sb_write_run(sbi, &ni->file.run, 0, rp, nsize, 0);
		if (err)
			goto out7;
	}

	/*
	 * Call 'd_instantiate' after inode->i_op is set
	 * but before finish_open.
	 */
	d_instantiate(dentry, inode);

	ntfs_save_wsl_perm(inode);
	mark_inode_dirty(dir);
	mark_inode_dirty(inode);

	/* Normal exit. */
	goto out2;

out7:

	/* Undo 'indx_insert_entry'. */
	if (!fnd)
		ni_lock_dir(dir_ni);
	indx_delete_entry(&dir_ni->dir, dir_ni, new_de + 1,
			  le16_to_cpu(new_de->key_size), sbi);
	/* ni_unlock(dir_ni); will be called later. */
out6:
	if (rp_inserted)
		ntfs_remove_reparse(sbi, IO_REPARSE_TAG_SYMLINK, &new_de->ref);

out5:
	if (!S_ISDIR(mode))
		run_deallocate(sbi, &ni->file.run, false);

out4:
	clear_rec_inuse(rec);
	clear_nlink(inode);
	ni->mi.dirty = false;
	discard_new_inode(inode);
out3:
	ntfs_mark_rec_free(sbi, ino, false);

out2:
	__putname(new_de);
	kfree(rp);

out1:
	if (err) {
		if (!fnd)
			ni_unlock(dir_ni);
		return ERR_PTR(err);
	}

	unlock_new_inode(inode);

	return inode;
}

int ntfs_link_inode(struct inode *inode, struct dentry *dentry)
{
	int err;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct NTFS_DE *de;

	/* Allocate PATH_MAX bytes. */
	de = __getname();
	if (!de)
		return -ENOMEM;

	/* Mark rw ntfs as dirty. It will be cleared at umount. */
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	/* Construct 'de'. */
	err = fill_name_de(sbi, de, &dentry->d_name, NULL);
	if (err)
		goto out;

	err = ni_add_name(ntfs_i(d_inode(dentry->d_parent)), ni, de);
out:
	__putname(de);
	return err;
}

/*
 * ntfs_unlink_inode
 *
 * inode_operations::unlink
 * inode_operations::rmdir
 */
int ntfs_unlink_inode(struct inode *dir, const struct dentry *dentry)
{
	int err;
	struct ntfs_sb_info *sbi = dir->i_sb->s_fs_info;
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = ntfs_i(inode);
	struct ntfs_inode *dir_ni = ntfs_i(dir);
	struct NTFS_DE *de, *de2 = NULL;
	int undo_remove;

	if (ntfs_is_meta_file(sbi, ni->mi.rno))
		return -EINVAL;

	/* Allocate PATH_MAX bytes. */
	de = __getname();
	if (!de)
		return -ENOMEM;

	ni_lock(ni);

	if (S_ISDIR(inode->i_mode) && !dir_is_empty(inode)) {
		err = -ENOTEMPTY;
		goto out;
	}

	err = fill_name_de(sbi, de, &dentry->d_name, NULL);
	if (err < 0)
		goto out;

	undo_remove = 0;
	err = ni_remove_name(dir_ni, ni, de, &de2, &undo_remove);

	if (!err) {
		drop_nlink(inode);
		dir->i_mtime = dir->i_ctime = current_time(dir);
		mark_inode_dirty(dir);
		inode->i_ctime = dir->i_ctime;
		if (inode->i_nlink)
			mark_inode_dirty(inode);
	} else if (!ni_remove_name_undo(dir_ni, ni, de, de2, undo_remove)) {
		_ntfs_bad_inode(inode);
	} else {
		if (ni_is_dirty(dir))
			mark_inode_dirty(dir);
		if (ni_is_dirty(inode))
			mark_inode_dirty(inode);
	}

out:
	ni_unlock(ni);
	__putname(de);
	return err;
}

void ntfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);

	if (inode->i_nlink)
		_ni_write_inode(inode, inode_needs_sync(inode));

	invalidate_inode_buffers(inode);
	clear_inode(inode);

	ni_clear(ntfs_i(inode));
}

/*
 * ntfs_translate_junction
 *
 * Translate a Windows junction target to the Linux equivalent.
 * On junctions, targets are always absolute (they include the drive
 * letter). We have no way of knowing if the target is for the current
 * mounted device or not so we just assume it is.
 */
static int ntfs_translate_junction(const struct super_block *sb,
				   const struct dentry *link_de, char *target,
				   int target_len, int target_max)
{
	int tl_len, err = target_len;
	char *link_path_buffer = NULL, *link_path;
	char *translated = NULL;
	char *target_start;
	int copy_len;

	link_path_buffer = kmalloc(PATH_MAX, GFP_NOFS);
	if (!link_path_buffer) {
		err = -ENOMEM;
		goto out;
	}
	/* Get link path, relative to mount point */
	link_path = dentry_path_raw(link_de, link_path_buffer, PATH_MAX);
	if (IS_ERR(link_path)) {
		ntfs_err(sb, "Error getting link path");
		err = -EINVAL;
		goto out;
	}

	translated = kmalloc(PATH_MAX, GFP_NOFS);
	if (!translated) {
		err = -ENOMEM;
		goto out;
	}

	/* Make translated path a relative path to mount point */
	strcpy(translated, "./");
	++link_path; /* Skip leading / */
	for (tl_len = sizeof("./") - 1; *link_path; ++link_path) {
		if (*link_path == '/') {
			if (PATH_MAX - tl_len < sizeof("../")) {
				ntfs_err(sb,
					 "Link path %s has too many components",
					 link_path);
				err = -EINVAL;
				goto out;
			}
			strcpy(translated + tl_len, "../");
			tl_len += sizeof("../") - 1;
		}
	}

	/* Skip drive letter */
	target_start = target;
	while (*target_start && *target_start != ':')
		++target_start;

	if (!*target_start) {
		ntfs_err(sb, "Link target (%s) missing drive separator",
			 target);
		err = -EINVAL;
		goto out;
	}

	/* Skip drive separator and leading /, if exists */
	target_start += 1 + (target_start[1] == '/');
	copy_len = target_len - (target_start - target);

	if (PATH_MAX - tl_len <= copy_len) {
		ntfs_err(sb, "Link target %s too large for buffer (%d <= %d)",
			 target_start, PATH_MAX - tl_len, copy_len);
		err = -EINVAL;
		goto out;
	}

	/* translated path has a trailing / and target_start does not */
	strcpy(translated + tl_len, target_start);
	tl_len += copy_len;
	if (target_max <= tl_len) {
		ntfs_err(sb, "Target path %s too large for buffer (%d <= %d)",
			 translated, target_max, tl_len);
		err = -EINVAL;
		goto out;
	}
	strcpy(target, translated);
	err = tl_len;

out:
	kfree(link_path_buffer);
	kfree(translated);
	return err;
}

static noinline int ntfs_readlink_hlp(const struct dentry *link_de,
				      struct inode *inode, char *buffer,
				      int buflen)
{
	int i, err = -EINVAL;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	u64 size;
	u16 ulen = 0;
	void *to_free = NULL;
	struct REPARSE_DATA_BUFFER *rp;
	const __le16 *uname;
	struct ATTRIB *attr;

	/* Reparse data present. Try to parse it. */
	static_assert(!offsetof(struct REPARSE_DATA_BUFFER, ReparseTag));
	static_assert(sizeof(u32) == sizeof(rp->ReparseTag));

	*buffer = 0;

	attr = ni_find_attr(ni, NULL, NULL, ATTR_REPARSE, NULL, 0, NULL, NULL);
	if (!attr)
		goto out;

	if (!attr->non_res) {
		rp = resident_data_ex(attr, sizeof(struct REPARSE_DATA_BUFFER));
		if (!rp)
			goto out;
		size = le32_to_cpu(attr->res.data_size);
	} else {
		size = le64_to_cpu(attr->nres.data_size);
		rp = NULL;
	}

	if (size > sbi->reparse.max_size || size <= sizeof(u32))
		goto out;

	if (!rp) {
		rp = kmalloc(size, GFP_NOFS);
		if (!rp) {
			err = -ENOMEM;
			goto out;
		}
		to_free = rp;
		/* Read into temporal buffer. */
		err = ntfs_read_run_nb(sbi, &ni->file.run, 0, rp, size, NULL);
		if (err)
			goto out;
	}

	/* Microsoft Tag. */
	switch (rp->ReparseTag) {
	case IO_REPARSE_TAG_MOUNT_POINT:
		/* Mount points and junctions. */
		/* Can we use 'Rp->MountPointReparseBuffer.PrintNameLength'? */
		if (size <= offsetof(struct REPARSE_DATA_BUFFER,
				     MountPointReparseBuffer.PathBuffer))
			goto out;
		uname = Add2Ptr(rp,
				offsetof(struct REPARSE_DATA_BUFFER,
					 MountPointReparseBuffer.PathBuffer) +
					le16_to_cpu(rp->MountPointReparseBuffer
							    .PrintNameOffset));
		ulen = le16_to_cpu(rp->MountPointReparseBuffer.PrintNameLength);
		break;

	case IO_REPARSE_TAG_SYMLINK:
		/* FolderSymbolicLink */
		/* Can we use 'Rp->SymbolicLinkReparseBuffer.PrintNameLength'? */
		if (size <= offsetof(struct REPARSE_DATA_BUFFER,
				     SymbolicLinkReparseBuffer.PathBuffer))
			goto out;
		uname = Add2Ptr(
			rp, offsetof(struct REPARSE_DATA_BUFFER,
				     SymbolicLinkReparseBuffer.PathBuffer) +
				    le16_to_cpu(rp->SymbolicLinkReparseBuffer
							.PrintNameOffset));
		ulen = le16_to_cpu(
			rp->SymbolicLinkReparseBuffer.PrintNameLength);
		break;

	case IO_REPARSE_TAG_CLOUD:
	case IO_REPARSE_TAG_CLOUD_1:
	case IO_REPARSE_TAG_CLOUD_2:
	case IO_REPARSE_TAG_CLOUD_3:
	case IO_REPARSE_TAG_CLOUD_4:
	case IO_REPARSE_TAG_CLOUD_5:
	case IO_REPARSE_TAG_CLOUD_6:
	case IO_REPARSE_TAG_CLOUD_7:
	case IO_REPARSE_TAG_CLOUD_8:
	case IO_REPARSE_TAG_CLOUD_9:
	case IO_REPARSE_TAG_CLOUD_A:
	case IO_REPARSE_TAG_CLOUD_B:
	case IO_REPARSE_TAG_CLOUD_C:
	case IO_REPARSE_TAG_CLOUD_D:
	case IO_REPARSE_TAG_CLOUD_E:
	case IO_REPARSE_TAG_CLOUD_F:
		err = sizeof("OneDrive") - 1;
		if (err > buflen)
			err = buflen;
		memcpy(buffer, "OneDrive", err);
		goto out;

	default:
		if (IsReparseTagMicrosoft(rp->ReparseTag)) {
			/* Unknown Microsoft Tag. */
			goto out;
		}
		if (!IsReparseTagNameSurrogate(rp->ReparseTag) ||
		    size <= sizeof(struct REPARSE_POINT)) {
			goto out;
		}

		/* Users tag. */
		uname = Add2Ptr(rp, sizeof(struct REPARSE_POINT));
		ulen = le16_to_cpu(rp->ReparseDataLength) -
		       sizeof(struct REPARSE_POINT);
	}

	/* Convert nlen from bytes to UNICODE chars. */
	ulen >>= 1;

	/* Check that name is available. */
	if (!ulen || uname + ulen > (__le16 *)Add2Ptr(rp, size))
		goto out;

	/* If name is already zero terminated then truncate it now. */
	if (!uname[ulen - 1])
		ulen -= 1;

	err = ntfs_utf16_to_nls(sbi, uname, ulen, buffer, buflen);

	if (err < 0)
		goto out;

	/* Translate Windows '\' into Linux '/'. */
	for (i = 0; i < err; i++) {
		if (buffer[i] == '\\')
			buffer[i] = '/';
	}

	/* Always set last zero. */
	buffer[err] = 0;

	/* If this is a junction, translate the link target. */
	if (rp->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
		err = ntfs_translate_junction(sb, link_de, buffer, err, buflen);

out:
	kfree(to_free);
	return err;
}

static const char *ntfs_get_link(struct dentry *de, struct inode *inode,
				 struct delayed_call *done)
{
	int err;
	char *ret;

	if (!de)
		return ERR_PTR(-ECHILD);

	ret = kmalloc(PAGE_SIZE, GFP_NOFS);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	err = ntfs_readlink_hlp(de, inode, ret, PAGE_SIZE);
	if (err < 0) {
		kfree(ret);
		return ERR_PTR(err);
	}

	set_delayed_call(done, kfree_link, ret);

	return ret;
}

// clang-format off
const struct inode_operations ntfs_link_inode_operations = {
	.get_link	= ntfs_get_link,
	.setattr	= ntfs3_setattr,
	.listxattr	= ntfs_listxattr,
	.permission	= ntfs_permission,
};

const struct address_space_operations ntfs_aops = {
	.read_folio	= ntfs_read_folio,
	.readahead	= ntfs_readahead,
	.writepages	= ntfs_writepages,
	.write_begin	= ntfs_write_begin,
	.write_end	= ntfs_write_end,
	.direct_IO	= ntfs_direct_IO,
	.bmap		= ntfs_bmap,
	.dirty_folio	= block_dirty_folio,
	.migrate_folio	= buffer_migrate_folio,
	.invalidate_folio = block_invalidate_folio,
};

const struct address_space_operations ntfs_aops_cmpr = {
	.read_folio	= ntfs_read_folio,
	.readahead	= ntfs_readahead,
};
// clang-format on
