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
#include <linux/iomap.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * ntfs_read_mft - Read record and parse MFT.
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
	u16 roff, rsize, names = 0, links = 0;
	const struct ATTR_FILE_NAME *fname = NULL;
	const struct INDEX_ROOT *root = NULL;
	struct REPARSE_DATA_BUFFER rp; // 0x18 bytes
	u64 t64;
	struct MFT_REC *rec;
	struct runs_tree *run;
	struct timespec64 ts;

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

	/*
	 * Really this check was done in 'ni_enum_attr_ex' -> ... 'mi_enum_attr'.
	 * There not critical to check this case again
	 */
	if (attr->name_len &&
	    sizeof(short) * attr->name_len + le16_to_cpu(attr->name_off) >
		    asize)
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

		nt2kernel(std5->cr_time, &ni->i_crtime);
		nt2kernel(std5->a_time, &ts);
		inode_set_atime_to_ts(inode, ts);
		nt2kernel(std5->c_time, &ts);
		inode_set_ctime_to_ts(inode, ts);
		nt2kernel(std5->m_time, &ts);
		inode_set_mtime_to_ts(inode, ts);

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

		names += 1;
		fname = Add2Ptr(attr, roff);
		if (fname->type == FILE_NAME_DOS)
			goto next_attr;

		links += 1;
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

		run = ino == MFT_REC_BITMAP ? &sbi->used.bitmap.run :
					      &ni->file.run;
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

		mode = sb->s_root ?
			       (S_IFDIR | (0777 & sbi->options->fs_dmask_inv)) :
			       (S_IFDIR | 0777);
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

	if (is_bad_inode(inode))
		goto out;

	if (!is_match && name) {
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
		ntfs_inode_warn(inode, "Correct links count -> %u.", names);
		rec->hard_links = cpu_to_le16(names);
		ni->mi.dirty = true;
	}

	set_nlink(inode, links);

	if (S_ISDIR(mode)) {
		ni->std_fa |= FILE_ATTRIBUTE_DIRECTORY;

		/*
		 * Dot and dot-dot should be included in count but was not
		 * included in enumeration.
		 * Usually a hard links to directories are disabled.
		 */
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = unlikely(is_legacy_ntfs(sb)) ?
				       &ntfs_legacy_dir_operations :
				       &ntfs_dir_operations;
		ni->i_valid = 0;
	} else if (S_ISLNK(mode)) {
		ni->std_fa &= ~FILE_ATTRIBUTE_DIRECTORY;
		inode->i_op = &ntfs_link_inode_operations;
		inode->i_fop = NULL;
		inode_nohighmem(inode);
	} else if (S_ISREG(mode)) {
		ni->std_fa &= ~FILE_ATTRIBUTE_DIRECTORY;
		inode->i_op = &ntfs_file_inode_operations;
		inode->i_fop = unlikely(is_legacy_ntfs(sb)) ?
				       &ntfs_legacy_file_operations :
				       &ntfs_file_operations;
		inode->i_mapping->a_ops = is_compressed(ni) ? &ntfs_aops_cmpr :
							      &ntfs_aops;
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
		mode = S_IFREG;
		init_rwsem(&ni->file.run_lock);
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
	if (inode_state_read_once(inode) & I_NEW)
		inode = ntfs_read_mft(inode, name, ref);
	else if (ref->seq != ntfs_i(inode)->mi.mrec->seq) {
		/*
		 * Sequence number is not expected.
		 * Looks like inode was reused but caller uses the old reference
		 */
		iput(inode);
		inode = ERR_PTR(-ESTALE);
	}

	if (IS_ERR(inode))
		ntfs_set_state(sb->s_fs_info, NTFS_DIRTY_ERROR);

	return inode;
}

static sector_t ntfs_bmap(struct address_space *mapping, sector_t block)
{
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);

	/*
	 * We can get here for an inline file via the FIBMAP ioctl
	 */
	if (is_resident(ni))
		return 0;

	if (mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) &&
	    !run_is_empty(&ni->file.run_da)) {
		/*
		 * With delalloc data we want to sync the file so
		 * that we can make sure we allocate blocks for file and data
		 * is in place for the user to see it
		 */
		ni_allocate_da_blocks(ni);
	}

	return iomap_bmap(mapping, block, &ntfs_iomap_ops);
}

static void ntfs_iomap_read_end_io(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		struct folio *folio = fi.folio;
		struct inode *inode = folio->mapping->host;
		struct ntfs_inode *ni = ntfs_i(inode);
		u64 valid = ni->i_valid;
		u32 f_size = folio_size(folio);
		loff_t f_pos = folio_pos(folio);


		if (valid < f_pos + f_size) {
			u32 z_from = valid <= f_pos ?
					     0 :
					     offset_in_folio(folio, valid);
			/* The only thing ntfs_iomap_read_end_io used for. */
			folio_zero_segment(folio, z_from, f_size);
		}

		iomap_finish_folio_read(folio, fi.offset, fi.length, error);
	}
	bio_put(bio);
}

/*
 * Copied from iomap/bio.c.
 */
static int ntfs_iomap_bio_read_folio_range(const struct iomap_iter *iter,
					   struct iomap_read_folio_ctx *ctx,
					   size_t plen)
{
	struct folio *folio = ctx->cur_folio;
	const struct iomap *iomap = &iter->iomap;
	loff_t pos = iter->pos;
	size_t poff = offset_in_folio(folio, pos);
	loff_t length = iomap_length(iter);
	sector_t sector;
	struct bio *bio = ctx->read_ctx;

	sector = iomap_sector(iomap, pos);
	if (!bio || bio_end_sector(bio) != sector ||
	    !bio_add_folio(bio, folio, plen, poff)) {
		gfp_t gfp = mapping_gfp_constraint(folio->mapping, GFP_KERNEL);
		gfp_t orig_gfp = gfp;
		unsigned int nr_vecs = DIV_ROUND_UP(length, PAGE_SIZE);

		if (bio)
			submit_bio(bio);

		if (ctx->rac) /* same as readahead_gfp_mask */
			gfp |= __GFP_NORETRY | __GFP_NOWARN;
		bio = bio_alloc(iomap->bdev, bio_max_segs(nr_vecs), REQ_OP_READ,
				gfp);
		/*
		 * If the bio_alloc fails, try it again for a single page to
		 * avoid having to deal with partial page reads.  This emulates
		 * what do_mpage_read_folio does.
		 */
		if (!bio)
			bio = bio_alloc(iomap->bdev, 1, REQ_OP_READ, orig_gfp);
		if (ctx->rac)
			bio->bi_opf |= REQ_RAHEAD;
		bio->bi_iter.bi_sector = sector;
		bio->bi_end_io = ntfs_iomap_read_end_io;
		bio_add_folio_nofail(bio, folio, plen, poff);
		ctx->read_ctx = bio;
	}
	return 0;
}

static void ntfs_iomap_bio_submit_read(struct iomap_read_folio_ctx *ctx)
{
	struct bio *bio = ctx->read_ctx;

	if (bio)
		submit_bio(bio);
}

static const struct iomap_read_ops ntfs_iomap_bio_read_ops = {
	.read_folio_range = ntfs_iomap_bio_read_folio_range,
	.submit_read = ntfs_iomap_bio_submit_read,
};

static int ntfs_read_folio(struct file *file, struct folio *folio)
{
	int err;
	struct address_space *mapping = folio->mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t vbo = folio_pos(folio);
	struct iomap_read_folio_ctx ctx = {
		.cur_folio = folio,
		.ops = &ntfs_iomap_bio_read_ops,
	};

	if (unlikely(is_bad_ni(ni))) {
		folio_unlock(folio);
		return -EIO;
	}

	if (ni->i_valid <= vbo) {
		folio_zero_range(folio, 0, folio_size(folio));
		folio_mark_uptodate(folio);
		folio_unlock(folio);
		return 0;
	}

	if (is_compressed(ni)) {
		/* ni_lock is taken inside ni_read_folio_cmpr after page locks */
		err = ni_read_folio_cmpr(ni, folio);
		return err;
	}

	iomap_read_folio(&ntfs_iomap_ops, &ctx, NULL);
	return 0;
}

static void ntfs_readahead(struct readahead_control *rac)
{
	struct address_space *mapping = rac->mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct iomap_read_folio_ctx ctx = {
		.ops = &ntfs_iomap_bio_read_ops,
		.rac = rac,
	};

	if (is_resident(ni)) {
		/* No readahead for resident. */
		return;
	}

	if (is_compressed(ni)) {
		/* No readahead for compressed. */
		return;
	}

	iomap_readahead(&ntfs_iomap_ops, &ctx, NULL);
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
			return -EFBIG;
		}
	} else if (new_size > sbi->maxbytes) {
		return -EFBIG;
	}

	ni_lock(ni);
	down_write(&ni->file.run_lock);

	err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run, new_size,
			    &ni->i_valid, true);

	if (!err) {
		i_size_write(inode, new_size);
		mark_inode_dirty(inode);
	}

	up_write(&ni->file.run_lock);
	ni_unlock(ni);

	return err;
}

/*
 * Special value to detect ntfs_writeback_range call
 */
#define WB_NO_DA (struct iomap *)1
/*
 * Function to get mapping vbo -> lbo.
 * used with:
 * - iomap_zero_range
 * - iomap_truncate_page
 * - iomap_dio_rw
 * - iomap_file_buffered_write
 * - iomap_bmap
 * - iomap_fiemap
 * - iomap_bio_read_folio
 * - iomap_bio_readahead
 */
static int ntfs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
			    unsigned int flags, struct iomap *iomap,
			    struct iomap *srcmap)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	u8 cluster_bits = sbi->cluster_bits;
	CLST vcn = offset >> cluster_bits;
	u32 off = offset & sbi->cluster_mask;
	bool rw = flags & IOMAP_WRITE;
	loff_t endbyte = offset + length;
	void *res = NULL;
	int err;
	CLST lcn, clen, clen_max = 1;
	bool new_clst = false;
	bool no_da;
	bool zero = false;
	if (unlikely(ntfs3_forced_shutdown(sbi->sb)))
		return -EIO;

	if (flags & IOMAP_REPORT) {
		if (offset > ntfs_get_maxbytes(ni)) {
			/* called from fiemap/bmap. */
			return -EINVAL;
		}

		if (offset >= inode->i_size) {
			/* special code for report. */
			return -ENOENT;
		}
	}

	if (IOMAP_ZERO == flags && (endbyte & sbi->cluster_mask)) {
		rw = true;
	} else if (rw) {
		clen_max = bytes_to_cluster(sbi, endbyte) - vcn;
	}

	/* 
	 * Force to allocate clusters if directIO(write) or writeback_range.
	 * NOTE: attr_data_get_block allocates clusters only for sparse file.
	 * Normal file allocates clusters in attr_set_size.
	*/
	no_da = flags == (IOMAP_DIRECT | IOMAP_WRITE) || srcmap == WB_NO_DA;

	err = attr_data_get_block(ni, vcn, clen_max, &lcn, &clen,
				  rw ? &new_clst : NULL, zero, &res, no_da);

	if (err) {
		return err;
	}

	if (lcn == EOF_LCN) {
		/* request out of file. */
		if (flags & IOMAP_REPORT) {
			/* special code for report. */
			return -ENOENT;
		}

		if (rw) {
			/* should never be here. */
			return -EINVAL;
		}
		lcn = SPARSE_LCN;
	}

	iomap->flags = new_clst ? IOMAP_F_NEW : 0;

	if (lcn == RESIDENT_LCN) {
		if (offset >= clen) {
			kfree(res);
			if (flags & IOMAP_REPORT) {
				/* special code for report. */
				return -ENOENT;
			}
			return -EFAULT;
		}

		iomap->private = iomap->inline_data = res;
		iomap->type = IOMAP_INLINE;
		iomap->offset = 0;
		iomap->length = clen; /* resident size in bytes. */
		return 0;
	}

	if (!clen) {
		/* broken file? */
		return -EINVAL;
	}

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = offset;
	iomap->length = ((loff_t)clen << cluster_bits) - off;

	if (lcn == COMPRESSED_LCN) {
		/* should never be here. */
		return -EOPNOTSUPP;
	}

	if (lcn == DELALLOC_LCN) {
		iomap->type = IOMAP_DELALLOC;
		iomap->addr = IOMAP_NULL_ADDR;
	} else {

		/* Translate clusters into bytes. */
		iomap->addr = ((loff_t)lcn << cluster_bits) + off;
		if (length && iomap->length > length)
			iomap->length = length;
		else
			endbyte = offset + iomap->length;

		if (lcn == SPARSE_LCN) {
			iomap->addr = IOMAP_NULL_ADDR;
			iomap->type = IOMAP_HOLE;
			//			if (IOMAP_ZERO == flags && !off) {
			//				iomap->length = (endbyte - offset) &
			//						sbi->cluster_mask_inv;
			//			}
		} else if (endbyte <= ni->i_valid) {
			iomap->type = IOMAP_MAPPED;
		} else if (offset < ni->i_valid) {
			iomap->type = IOMAP_MAPPED;
			if (flags & IOMAP_REPORT)
				iomap->length = ni->i_valid - offset;
		} else if (rw || (flags & IOMAP_ZERO)) {
			iomap->type = IOMAP_MAPPED;
		} else {
			iomap->type = IOMAP_UNWRITTEN;
		}
	}

	if ((flags & IOMAP_ZERO) &&
	    (iomap->type == IOMAP_MAPPED || iomap->type == IOMAP_DELALLOC)) {
		/* Avoid too large requests. */
		u32 tail;
		u32 off_a = offset & (PAGE_SIZE - 1);
		if (off_a)
			tail = PAGE_SIZE - off_a;
		else
			tail = PAGE_SIZE;

		if (iomap->length > tail)
			iomap->length = tail;
	}

	return 0;
}

static int ntfs_iomap_end(struct inode *inode, loff_t pos, loff_t length,
			  ssize_t written, unsigned int flags,
			  struct iomap *iomap)
{
	int err = 0;
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t endbyte = pos + written;

	if ((flags & IOMAP_WRITE) || (flags & IOMAP_ZERO)) {
		if (iomap->type == IOMAP_INLINE) {
			u32 data_size;
			struct ATTRIB *attr;
			struct mft_inode *mi;

			attr = ni_find_attr(ni, NULL, NULL, ATTR_DATA, NULL, 0,
					    NULL, &mi);
			if (!attr || attr->non_res) {
				err = -EINVAL;
				goto out;
			}

			data_size = le32_to_cpu(attr->res.data_size);
			if (!(pos < data_size && endbyte <= data_size)) {
				err = -EINVAL;
				goto out;
			}

			/* Update resident data. */
			memcpy(resident_data(attr) + pos,
			       iomap_inline_data(iomap, pos), written);
			mi->dirty = true;
			ni->i_valid = data_size;
		} else if (ni->i_valid < endbyte) {
			ni->i_valid = endbyte;
			mark_inode_dirty(inode);
		}
	}

	if ((flags & IOMAP_ZERO) &&
	    (iomap->type == IOMAP_MAPPED || iomap->type == IOMAP_DELALLOC)) {
		/* Pair for code in ntfs_iomap_begin. */
		balance_dirty_pages_ratelimited(inode->i_mapping);
		cond_resched();
	}

out:
	if (iomap->type == IOMAP_INLINE) {
		kfree(iomap->private);
		iomap->private = NULL;
	}

	return err;
}

/*
 * write_begin + put_folio + write_end.
 * iomap_zero_range
 * iomap_truncate_page
 * iomap_file_buffered_write
 */
static void ntfs_iomap_put_folio(struct inode *inode, loff_t pos,
				 unsigned int len, struct folio *folio)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t end = pos + len;
	u32 f_size = folio_size(folio);
	loff_t f_pos = folio_pos(folio);
	loff_t f_end = f_pos + f_size;

	if (ni->i_valid <= end && end < f_end) {
		/* zero range [end - f_end). */
		/* The only thing ntfs_iomap_put_folio used for. */
		folio_zero_segment(folio, offset_in_folio(folio, end), f_size);
	}
	folio_unlock(folio);
	folio_put(folio);
}

/*
 * iomap_writeback_ops::writeback_range
 */
static ssize_t ntfs_writeback_range(struct iomap_writepage_ctx *wpc,
				    struct folio *folio, u64 offset,
				    unsigned int len, u64 end_pos)
{
	struct iomap *iomap = &wpc->iomap;
	/* Check iomap position. */
	if (iomap->offset + iomap->length <= offset || offset < iomap->offset) {
		int err;
		struct inode *inode = wpc->inode;
		struct ntfs_inode *ni = ntfs_i(inode);
		struct ntfs_sb_info *sbi = ntfs_sb(inode->i_sb);
		loff_t i_size_up = ntfs_up_cluster(sbi, inode->i_size);
		loff_t len_max = i_size_up - offset;

		err = ni->file.run_da.count ? ni_allocate_da_blocks(ni) : 0;

		if (!err) {
			/* Use local special value 'WB_NO_DA' to disable delalloc. */
			err = ntfs_iomap_begin(inode, offset, len_max,
					       IOMAP_WRITE, iomap, WB_NO_DA);
		}

		if (err) {
			ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);
			return err;
		}
	}

	return iomap_add_to_ioend(wpc, folio, offset, end_pos, len);
}


static const struct iomap_writeback_ops ntfs_writeback_ops = {
	.writeback_range = ntfs_writeback_range,
	.writeback_submit = iomap_ioend_writeback_submit,
};

static int ntfs_resident_writepage(struct folio *folio,
				   struct writeback_control *wbc)
{
	struct address_space *mapping = folio->mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	int ret;

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	ni_lock(ni);
	ret = attr_data_write_resident(ni, folio);
	ni_unlock(ni);

	if (ret != E_NTFS_NONRESIDENT)
		folio_unlock(folio);
	mapping_set_error(mapping, ret);
	return ret;
}

static int ntfs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	int err;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct iomap_writepage_ctx wpc = {
		.inode = mapping->host,
		.wbc = wbc,
		.ops = &ntfs_writeback_ops,
	};

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	if (is_resident(ni)) {
		struct folio *folio = NULL;

		while ((folio = writeback_iter(mapping, wbc, folio, &err)))
			err = ntfs_resident_writepage(folio, wbc);

		return err;
	}

	return iomap_writepages(&wpc);
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
 * Helper function to read file.
 * Used to read $AttrDef and $UpCase
 */
int inode_read_data(struct inode *inode, void *data, size_t bytes)
{
	pgoff_t idx;
	struct address_space *mapping = inode->i_mapping;

	for (idx = 0; bytes; idx++) {
		size_t op = bytes > PAGE_SIZE ? PAGE_SIZE : bytes;
		struct page *page = read_mapping_page(mapping, idx, NULL);
		void *kaddr;

		if (IS_ERR(page))
			return PTR_ERR(page);

		kaddr = kmap_atomic(page);
		memcpy(data, kaddr, op);
		kunmap_atomic(kaddr);

		put_page(page);

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
static inline u32 ntfs_reparse_bytes(u32 uni_len, bool is_absolute)
{
	/* Header + unicode string + decorated unicode string. */
	return sizeof(short) * (2 * uni_len + (is_absolute ? 4 : 0)) +
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
	bool is_absolute;

	is_absolute = symname[0] && symname[1] == ':';

	rp = kzalloc(ntfs_reparse_bytes(2 * size + 2, is_absolute), GFP_NOFS);
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
	*nsize = ntfs_reparse_bytes(err, is_absolute);

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
	rs->SubstituteNameLength =
		cpu_to_le16(sizeof(short) * err + (is_absolute ? 8 : 0));
	rs->PrintNameLength = rs->SubstituteNameOffset;

	/*
	 * TODO: Use relative path if possible to allow Windows to
	 * parse this path.
	 * 0-absolute path, 1- relative path (SYMLINK_FLAG_RELATIVE).
	 */
	rs->Flags = cpu_to_le32(is_absolute ? 0 : SYMLINK_FLAG_RELATIVE);

	memmove(rp_name + err + (is_absolute ? 4 : 0), rp_name,
		sizeof(short) * err);

	if (is_absolute) {
		/* Decorate SubstituteName. */
		rp_name += err;
		rp_name[0] = cpu_to_le16('\\');
		rp_name[1] = cpu_to_le16('?');
		rp_name[2] = cpu_to_le16('?');
		rp_name[3] = cpu_to_le16('\\');
	}

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
int ntfs_create_inode(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, const struct cpu_str *uni,
		      umode_t mode, dev_t dev, const char *symname, u32 size,
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

	/* New file will be resident or non resident. */
	const bool new_file_resident = 1;

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
	new_de = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!new_de) {
		err = -ENOMEM;
		goto out1;
	}

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(dir_ni))) {
		err = -EINVAL;
		goto out2;
	}

	if (unlikely(ntfs3_forced_shutdown(sb))) {
		err = -EIO;
		goto out2;
	}

	/* Mark rw ntfs as dirty. it will be cleared at umount. */
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	/* Step 1: allocate and fill new mft record. */
	err = ntfs_look_free_mft(sbi, &ino, false, NULL, NULL);
	if (err)
		goto out2;

	ni = ntfs_new_inode(sbi, ino, S_ISDIR(mode) ? RECORD_FLAG_DIR : 0);
	if (IS_ERR(ni)) {
		err = PTR_ERR(ni);
		ni = NULL;
		goto out3;
	}
	inode = &ni->vfs_inode;
	inode_init_owner(idmap, inode, dir, mode);
	mode = inode->i_mode;

	ni->i_crtime = current_time(inode);

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
		kernel2nt(&ni->i_crtime);

	std5->fa = ni->std_fa = fa;

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
	fname->dup.extend_data = S_ISLNK(mode) ? IO_REPARSE_TAG_SYMLINK : 0;

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
		root->ihdr.de_off = cpu_to_le32(sizeof(struct INDEX_HDR));
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
	} else if (!new_file_resident && S_ISREG(mode)) {
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
			attr->nres.c_unit = NTFS_LZNT_CUNIT;
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
		 * It is good idea to keep extended attributes resident.
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

			err = attr_allocate_clusters(sbi, &ni->file.run, NULL,
						     0, 0, clst, NULL,
						     ALLOCATE_DEF, &alen, 0,
						     NULL, NULL);
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
			/* Write non resident data. */
			err = ntfs_sb_write_run(sbi, &ni->file.run, 0, rp,
						nsize, 0);
			if (err)
				goto out5;
		} else {
			attr->res.data_off = SIZEOF_RESIDENT_LE;
			attr->res.data_size = cpu_to_le32(nsize);
			memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), rp, nsize);
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

	inode->i_generation = le16_to_cpu(rec->seq);

	if (S_ISDIR(mode)) {
		inode->i_op = &ntfs_dir_inode_operations;
		inode->i_fop = unlikely(is_legacy_ntfs(sb)) ?
				       &ntfs_legacy_dir_operations :
				       &ntfs_dir_operations;
	} else if (S_ISLNK(mode)) {
		inode->i_op = &ntfs_link_inode_operations;
		inode->i_fop = NULL;
		inode->i_mapping->a_ops = &ntfs_aops;
		inode->i_size = size;
		inode_nohighmem(inode);
	} else if (S_ISREG(mode)) {
		inode->i_op = &ntfs_file_inode_operations;
		inode->i_fop = unlikely(is_legacy_ntfs(sb)) ?
				       &ntfs_legacy_file_operations :
				       &ntfs_file_operations;
		inode->i_mapping->a_ops = is_compressed(ni) ? &ntfs_aops_cmpr :
							      &ntfs_aops;
		init_rwsem(&ni->file.run_lock);
	} else {
		inode->i_op = &ntfs_special_inode_operations;
		init_special_inode(inode, mode, dev);
	}

#ifdef CONFIG_NTFS3_FS_POSIX_ACL
	if (!S_ISLNK(mode) && (sb->s_flags & SB_POSIXACL)) {
		err = ntfs_init_acl(idmap, inode, dir);
		if (err)
			goto out5;
	} else
#endif
	{
		inode->i_flags |= S_NOSEC;
	}

	if (!S_ISLNK(mode)) {
		/*
		 * ntfs_init_acl and ntfs_save_wsl_perm update extended attribute.
		 * The packed size of extended attribute is stored in direntry too.
		 * 'fname' here points to inside new_de.
		 */
		err = ntfs_save_wsl_perm(inode, &fname->dup.extend_data);
		if (err)
			goto out6;

		/*
		 * update ea_size in file_name attribute too.
		 * Use ni_find_attr cause layout of MFT record may be changed
		 * in ntfs_init_acl and ntfs_save_wsl_perm.
		 */
		attr = ni_find_attr(ni, NULL, NULL, ATTR_NAME, NULL, 0, NULL,
				    NULL);
		if (attr) {
			struct ATTR_FILE_NAME *fn;

			fn = resident_data_ex(attr, SIZEOF_ATTRIBUTE_FILENAME);
			if (fn)
				fn->dup.extend_data = fname->dup.extend_data;
		}
	}

	/* We do not need to update parent directory later */
	ni->ni_flags &= ~NI_FLAG_UPDATE_PARENT;

	/* Step 2: Add new name in index. */
	err = indx_insert_entry(&dir_ni->dir, dir_ni, new_de, sbi, fnd, 0);
	if (err)
		goto out6;

	/*
	 * Call 'd_instantiate' after inode->i_op is set
	 * but before finish_open.
	 */
	d_instantiate(dentry, inode);

	/* Set original time. inode times (i_ctime) may be changed in ntfs_init_acl. */
	inode_set_atime_to_ts(inode, ni->i_crtime);
	inode_set_ctime_to_ts(inode, ni->i_crtime);
	inode_set_mtime_to_ts(inode, ni->i_crtime);
	inode_set_mtime_to_ts(dir, ni->i_crtime);
	inode_set_ctime_to_ts(dir, ni->i_crtime);

	mark_inode_dirty(dir);
	mark_inode_dirty(inode);

	/* Normal exit. */
	goto out2;

out6:
	attr = ni_find_attr(ni, NULL, NULL, ATTR_EA, NULL, 0, NULL, NULL);
	if (attr && attr->non_res) {
		/* Delete ATTR_EA, if non-resident. */
		struct runs_tree run;
		run_init(&run);
		attr_set_size(ni, ATTR_EA, NULL, 0, &run, 0, NULL, false);
		run_close(&run);
	}

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
	kfree(new_de);
	kfree(rp);

out1:
	if (!fnd)
		ni_unlock(dir_ni);

	if (!err)
		unlock_new_inode(inode);

	return err;
}

int ntfs_link_inode(struct inode *inode, struct dentry *dentry)
{
	int err;
	struct ntfs_inode *ni = ntfs_i(inode);
	struct ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct NTFS_DE *de;

	/* Allocate PATH_MAX bytes. */
	de = kzalloc(PATH_MAX, GFP_KERNEL);
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
	kfree(de);
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

	de = kzalloc(PATH_MAX, GFP_KERNEL);
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
		inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
		mark_inode_dirty(dir);
		inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
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
	kfree(de);
	return err;
}

void ntfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);

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
	.setattr	= ntfs_setattr,
	.listxattr	= ntfs_listxattr,
};

const struct address_space_operations ntfs_aops = {
	.read_folio	= ntfs_read_folio,
	.readahead	= ntfs_readahead,
	.writepages	= ntfs_writepages,
	.bmap		= ntfs_bmap,
	.dirty_folio	= iomap_dirty_folio,
	.migrate_folio	= filemap_migrate_folio,
	.release_folio	= iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
};

const struct address_space_operations ntfs_aops_cmpr = {
	.read_folio	= ntfs_read_folio,
	.dirty_folio	= iomap_dirty_folio,
	.release_folio	= iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
};

const struct iomap_ops ntfs_iomap_ops = {
	.iomap_begin	= ntfs_iomap_begin,
	.iomap_end	= ntfs_iomap_end,
};

const struct iomap_write_ops ntfs_iomap_folio_ops = {
	.put_folio = ntfs_iomap_put_folio,
};
// clang-format on
