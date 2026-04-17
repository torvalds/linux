// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iomap callack functions
 *
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/writeback.h>

#include "attrib.h"
#include "mft.h"
#include "ntfs.h"
#include "iomap.h"

static void ntfs_iomap_put_folio_non_resident(struct inode *inode, loff_t pos,
					      unsigned int len, struct folio *folio)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	unsigned long sector_size = 1UL << inode->i_blkbits;
	loff_t start_down, end_up, init;

	start_down = round_down(pos, sector_size);
	end_up = (pos + len - 1) | (sector_size - 1);
	init = ni->initialized_size;

	if (init >= start_down && init <= end_up) {
		if (init < pos) {
			loff_t offset = offset_in_folio(folio, pos + len);

			if (offset == 0)
				offset = folio_size(folio);
			folio_zero_segments(folio,
					    offset_in_folio(folio, init),
					    offset_in_folio(folio, pos),
					    offset,
					    folio_size(folio));

		} else  {
			loff_t offset = max_t(loff_t, pos + len, init);

			offset = offset_in_folio(folio, offset);
			if (offset == 0)
				offset = folio_size(folio);
			folio_zero_segment(folio,
					   offset,
					   folio_size(folio));
		}
	} else if (init <= pos) {
		loff_t offset = 0, offset2 = offset_in_folio(folio, pos + len);

		if ((init >> folio_shift(folio)) == (pos >> folio_shift(folio)))
			offset = offset_in_folio(folio, init);
		if (offset2 == 0)
			offset2 = folio_size(folio);
		folio_zero_segments(folio,
				    offset,
				    offset_in_folio(folio, pos),
				    offset2,
				    folio_size(folio));
	}
	folio_unlock(folio);
	folio_put(folio);
}

/*
 * iomap_zero_range is called for an area beyond the initialized size,
 * garbage values can be read, so zeroing out is needed.
 */
static void ntfs_iomap_put_folio(struct inode *inode, loff_t pos,
		unsigned int len, struct folio *folio)
{
	if (NInoNonResident(NTFS_I(inode)))
		return ntfs_iomap_put_folio_non_resident(inode, pos,
							 len, folio);
	folio_unlock(folio);
	folio_put(folio);
}

const struct iomap_write_ops ntfs_iomap_folio_ops = {
	.put_folio = ntfs_iomap_put_folio,
};

static int ntfs_read_iomap_begin_resident(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap)
{
	struct ntfs_inode *base_ni, *ni = NTFS_I(inode);
	struct ntfs_attr_search_ctx *ctx;
	loff_t i_size;
	u32 attr_len;
	int err = 0;
	char *kattr;

	if (NInoAttr(ni))
		base_ni = ni->ext.base_ntfs_ino;
	else
		base_ni = ni;

	ctx = ntfs_attr_get_search_ctx(base_ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto out;
	}

	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err))
		goto out;

	attr_len = le32_to_cpu(ctx->attr->data.resident.value_length);
	if (unlikely(attr_len > ni->initialized_size))
		attr_len = ni->initialized_size;
	i_size = i_size_read(inode);

	if (unlikely(attr_len > i_size)) {
		/* Race with shrinking truncate. */
		attr_len = i_size;
	}

	if (offset >= attr_len) {
		if (flags & IOMAP_REPORT)
			err = -ENOENT;
		else {
			iomap->type = IOMAP_HOLE;
			iomap->offset = offset;
			iomap->length = length;
		}
		goto out;
	}

	kattr = (u8 *)ctx->attr + le16_to_cpu(ctx->attr->data.resident.value_offset);

	iomap->inline_data = kmemdup(kattr, attr_len, GFP_KERNEL);
	if (!iomap->inline_data) {
		err = -ENOMEM;
		goto out;
	}

	iomap->type = IOMAP_INLINE;
	iomap->offset = 0;
	iomap->length = attr_len;

out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);

	return err;
}

/*
 * ntfs_read_iomap_begin_non_resident - map non-resident NTFS file data
 * @inode:		inode to map
 * @offset:		file offset to map
 * @length:		length of mapping
 * @flags:		IOMAP flags
 * @iomap:		iomap structure to fill
 * @need_unwritten:	true if UNWRITTEN extent type is needed
 *
 * Map a range of a non-resident NTFS file to an iomap extent.
 *
 * NTFS UNWRITTEN extent handling:
 * ================================
 * The concept of an unwritten extent in NTFS is slightly different from
 * that of other filesystems. NTFS conceptually manages only a single
 * continuous unwritten region, which is strictly defined based on
 * initialized_size.
 *
 * File offset layout:
 *   0                        initialized_size                   i_size(EOF)
 *   |----------#0----------|----------#1----------|----------#2----------|
 *   | Actual data          | Pre-allocated        | Pre-allocated        |
 *   | (user written)       | (within initialized) | (initialized ~ EOF)  |
 *   |----------------------|----------------------|----------------------|
 *   MAPPED                 MAPPED                 UNWRITTEN (conditionally)
 *
 * Region #0: User-written data, initialized and valid.
 * Region #1: Pre-allocated within initialized_size, must be zero-initialized
 *            by the filesystem before exposure to userspace.
 * Region #2: Pre-allocated beyond initialized_size, does not need initialization.
 *
 * The @need_unwritten parameter controls whether region #2 is mapped as
 * IOMAP_UNWRITTEN or IOMAP_MAPPED:
 * - For seek operations (SEEK_DATA/SEEK_HOLE): IOMAP_MAPPED is needed to
 *   prevent iomap_seek_data from incorrectly interpreting pre-allocated
 *   space as a hole. Since NTFS does not support multiple unwritten extents,
 *   all pre-allocated regions should be treated as data, not holes.
 * - For zero_range operations: IOMAP_MAPPED is needed to be zeroed out.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int ntfs_read_iomap_begin_non_resident(struct inode *inode, loff_t offset,
		loff_t length, unsigned int flags, struct iomap *iomap,
		bool need_unwritten)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	s64 vcn;
	s64 lcn;
	struct runlist_element *rl;
	struct ntfs_volume *vol = ni->vol;
	loff_t vcn_ofs;
	loff_t rl_length;

	vcn = ntfs_bytes_to_cluster(vol, offset);
	vcn_ofs = ntfs_bytes_to_cluster_off(vol, offset);

	down_write(&ni->runlist.lock);
	rl = ntfs_attr_vcn_to_rl(ni, vcn, &lcn);
	if (IS_ERR(rl)) {
		up_write(&ni->runlist.lock);
		return PTR_ERR(rl);
	}

	if (flags & IOMAP_REPORT) {
		if (lcn < LCN_HOLE) {
			up_write(&ni->runlist.lock);
			return -ENOENT;
		}
	} else if (lcn < LCN_ENOENT) {
		up_write(&ni->runlist.lock);
		return -EINVAL;
	}

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = offset;

	if (lcn <= LCN_DELALLOC) {
		if (lcn == LCN_DELALLOC)
			iomap->type = IOMAP_DELALLOC;
		else
			iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
	} else {
		if (need_unwritten && offset >= ni->initialized_size)
			iomap->type = IOMAP_UNWRITTEN;
		else
			iomap->type = IOMAP_MAPPED;
		iomap->addr = ntfs_cluster_to_bytes(vol, lcn) + vcn_ofs;
	}

	rl_length = ntfs_cluster_to_bytes(vol, rl->length - (vcn - rl->vcn));

	if (rl_length == 0 && rl->lcn > LCN_DELALLOC) {
		ntfs_error(inode->i_sb,
				"runlist(vcn : %lld, length : %lld, lcn : %lld) is corrupted\n",
				rl->vcn, rl->length, rl->lcn);
		up_write(&ni->runlist.lock);
		return -EIO;
	}

	if (rl_length && length > rl_length - vcn_ofs)
		iomap->length = rl_length - vcn_ofs;
	else
		iomap->length = length;
	up_write(&ni->runlist.lock);

	if (!(flags & IOMAP_ZERO) &&
			iomap->type == IOMAP_MAPPED &&
			iomap->offset < ni->initialized_size &&
			iomap->offset + iomap->length > ni->initialized_size) {
		iomap->length = round_up(ni->initialized_size, 1 << inode->i_blkbits) -
			iomap->offset;
	}
	iomap->flags |= IOMAP_F_MERGED;

	return 0;
}

static int __ntfs_read_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap,
		bool need_unwritten)
{
	if (NInoNonResident(NTFS_I(inode)))
		return ntfs_read_iomap_begin_non_resident(inode, offset, length,
				flags, iomap, need_unwritten);
	return ntfs_read_iomap_begin_resident(inode, offset, length,
					     flags, iomap);
}

static int ntfs_read_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	return __ntfs_read_iomap_begin(inode, offset, length, flags, iomap,
			srcmap, true);
}

static int ntfs_read_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	if (iomap->type == IOMAP_INLINE)
		kfree(iomap->inline_data);

	return written;
}

const struct iomap_ops ntfs_read_iomap_ops = {
	.iomap_begin = ntfs_read_iomap_begin,
	.iomap_end = ntfs_read_iomap_end,
};

/*
 * Check that the cached iomap still matches the NTFS runlist before
 * iomap_zero_range() is called. if the runlist changes while iomap is
 * iterating a cached iomap, iomap_zero_range() may overwrite folios
 * that have been already written with valid data.
 */
static bool ntfs_iomap_valid(struct inode *inode, const struct iomap *iomap)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct runlist_element *rl;
	s64 vcn, lcn;

	if (!NInoNonResident(ni))
		return false;

	vcn = iomap->offset >> ni->vol->cluster_size_bits;

	down_read(&ni->runlist.lock);
	rl = __ntfs_attr_find_vcn_nolock(&ni->runlist, vcn);
	if (IS_ERR(rl)) {
		up_read(&ni->runlist.lock);
		return false;
	}
	lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
	up_read(&ni->runlist.lock);
	return lcn == LCN_DELALLOC;
}

static const struct iomap_write_ops ntfs_zero_iomap_folio_ops = {
	.put_folio = ntfs_iomap_put_folio,
	.iomap_valid = ntfs_iomap_valid,
};

static int ntfs_seek_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	return __ntfs_read_iomap_begin(inode, offset, length, flags, iomap,
			srcmap, false);
}

static int ntfs_zero_read_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	if ((flags & IOMAP_ZERO) && (iomap->flags & IOMAP_F_STALE))
		return -EPERM;
	return written;
}

static const struct iomap_ops ntfs_zero_read_iomap_ops = {
	.iomap_begin = ntfs_seek_iomap_begin,
	.iomap_end = ntfs_zero_read_iomap_end,
};

const struct iomap_ops ntfs_seek_iomap_ops = {
	.iomap_begin = ntfs_seek_iomap_begin,
	.iomap_end = ntfs_read_iomap_end,
};

int ntfs_dio_zero_range(struct inode *inode, loff_t offset, loff_t length)
{
	if ((offset | length) & (SECTOR_SIZE - 1))
		return -EINVAL;

	return  blkdev_issue_zeroout(inode->i_sb->s_bdev,
				     offset >> SECTOR_SHIFT,
				     length >> SECTOR_SHIFT,
				     GFP_NOFS,
				     BLKDEV_ZERO_NOUNMAP);
}

static int ntfs_zero_range(struct inode *inode, loff_t offset, loff_t length)
{
	return iomap_zero_range(inode,
				offset, length,
				NULL,
				&ntfs_zero_read_iomap_ops,
				&ntfs_zero_iomap_folio_ops,
				NULL);
}

static int ntfs_write_simple_iomap_begin_non_resident(struct inode *inode, loff_t offset,
						      loff_t length, struct iomap *iomap)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ntfs_volume *vol = ni->vol;
	loff_t vcn_ofs, rl_length;
	struct runlist_element *rl, *rlc;
	bool is_retry = false;
	int err;
	s64 vcn, lcn;
	s64 max_clu_count =
		ntfs_bytes_to_cluster(vol, round_up(length, vol->cluster_size));

	vcn = ntfs_bytes_to_cluster(vol, offset);
	vcn_ofs = ntfs_bytes_to_cluster_off(vol, offset);

	down_read(&ni->runlist.lock);
	rl = ni->runlist.rl;
	if (!rl) {
		up_read(&ni->runlist.lock);
		err = ntfs_map_runlist(ni, vcn);
		if (err) {
			mutex_unlock(&ni->mrec_lock);
			return -ENOENT;
		}
		down_read(&ni->runlist.lock);
		rl = ni->runlist.rl;
	}
	up_read(&ni->runlist.lock);

	down_write(&ni->runlist.lock);
remap_rl:
	/* Seek to element containing target vcn. */
	rl = __ntfs_attr_find_vcn_nolock(&ni->runlist, vcn);
	if (IS_ERR(rl)) {
		up_write(&ni->runlist.lock);
		mutex_unlock(&ni->mrec_lock);
		return -EIO;
	}
	lcn = ntfs_rl_vcn_to_lcn(rl, vcn);

	if (lcn <= LCN_RL_NOT_MAPPED && is_retry == false) {
		is_retry = true;
		if (!ntfs_map_runlist_nolock(ni, vcn, NULL)) {
			rl = ni->runlist.rl;
			goto remap_rl;
		}
	}

	max_clu_count = min(max_clu_count, rl->length - (vcn - rl->vcn));
	if (max_clu_count == 0) {
		ntfs_error(inode->i_sb,
				"runlist(vcn : %lld, length : %lld) is corrupted\n",
				rl->vcn, rl->length);
		up_write(&ni->runlist.lock);
		mutex_unlock(&ni->mrec_lock);
		return -EIO;
	}

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = offset;

	if (lcn <= LCN_DELALLOC) {
		if (lcn < LCN_DELALLOC) {
			max_clu_count =
				ntfs_available_clusters_count(vol, max_clu_count);
			if (max_clu_count < 0) {
				err = max_clu_count;
				up_write(&ni->runlist.lock);
				mutex_unlock(&ni->mrec_lock);
				return err;
			}
		}

		iomap->type = IOMAP_DELALLOC;
		iomap->addr = IOMAP_NULL_ADDR;

		if (lcn <= LCN_HOLE) {
			size_t new_rl_count;

			rlc = kmalloc(sizeof(struct runlist_element) * 2,
					GFP_NOFS);
			if (!rlc) {
				up_write(&ni->runlist.lock);
				mutex_unlock(&ni->mrec_lock);
				return -ENOMEM;
			}

			rlc->vcn = vcn;
			rlc->lcn = LCN_DELALLOC;
			rlc->length = max_clu_count;

			rlc[1].vcn = vcn + max_clu_count;
			rlc[1].lcn = LCN_RL_NOT_MAPPED;
			rlc[1].length = 0;

			rl = ntfs_runlists_merge(&ni->runlist, rlc, 0,
					&new_rl_count);
			if (IS_ERR(rl)) {
				ntfs_error(vol->sb, "Failed to merge runlists");
				up_write(&ni->runlist.lock);
				mutex_unlock(&ni->mrec_lock);
				kvfree(rlc);
				return PTR_ERR(rl);
			}

			ni->runlist.rl = rl;
			ni->runlist.count = new_rl_count;
			ni->i_dealloc_clusters += max_clu_count;
		}
		up_write(&ni->runlist.lock);
		mutex_unlock(&ni->mrec_lock);

		if (lcn < LCN_DELALLOC)
			ntfs_hold_dirty_clusters(vol, max_clu_count);

		rl_length = ntfs_cluster_to_bytes(vol, max_clu_count);
		if (length > rl_length - vcn_ofs)
			iomap->length = rl_length - vcn_ofs;
		else
			iomap->length = length;

		iomap->flags = IOMAP_F_NEW;
		if (lcn <= LCN_HOLE) {
			loff_t end = offset + length;

			if (vcn_ofs || ((vol->cluster_size > iomap->length) &&
					end < ni->initialized_size)) {
				loff_t z_start, z_end;

				z_start = vcn << vol->cluster_size_bits;
				z_end = min_t(loff_t, z_start + vol->cluster_size,
					      i_size_read(inode));
				if (z_end > z_start)
					err = ntfs_zero_range(inode,
							      z_start,
							      z_end - z_start);
			}
			if ((!err || err == -EPERM) &&
			    max_clu_count > 1 &&
			    (iomap->length & vol->cluster_size_mask &&
			     end < ni->initialized_size)) {
				loff_t z_start, z_end;

				z_start = (vcn + max_clu_count - 1) <<
					vol->cluster_size_bits;
				z_end = min_t(loff_t, z_start + vol->cluster_size,
					      i_size_read(inode));
				if (z_end > z_start)
					err = ntfs_zero_range(inode,
							      z_start,
							      z_end - z_start);
			}

			if (err == -EPERM)
				err = 0;
			if (err) {
				ntfs_release_dirty_clusters(vol, max_clu_count);
				return err;
			}
		}
	} else {
		up_write(&ni->runlist.lock);
		mutex_unlock(&ni->mrec_lock);

		iomap->type = IOMAP_MAPPED;
		iomap->addr = ntfs_cluster_to_bytes(vol, lcn) + vcn_ofs;

		rl_length = ntfs_cluster_to_bytes(vol, max_clu_count);
		if (length > rl_length - vcn_ofs)
			iomap->length = rl_length - vcn_ofs;
		else
			iomap->length = length;
	}

	return 0;
}

#define NTFS_IOMAP_FLAGS_BEGIN		BIT(1)
#define NTFS_IOMAP_FLAGS_DIO		BIT(2)
#define	NTFS_IOMAP_FLAGS_MKWRITE	BIT(3)
#define	NTFS_IOMAP_FLAGS_WRITEBACK	BIT(4)

static int ntfs_write_da_iomap_begin_non_resident(struct inode *inode,
		loff_t offset, loff_t length, unsigned int flags,
		struct iomap *iomap, int ntfs_iomap_flags)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ntfs_volume *vol = ni->vol;
	loff_t vcn_ofs, rl_length;
	s64 vcn, start_lcn, lcn_count;
	bool balloc = false, update_mp;
	int err;
	s64 max_clu_count =
		ntfs_bytes_to_cluster(vol, round_up(length, vol->cluster_size));

	vcn = ntfs_bytes_to_cluster(vol, offset);
	vcn_ofs = ntfs_bytes_to_cluster_off(vol, offset);

	update_mp = ntfs_iomap_flags & (NTFS_IOMAP_FLAGS_DIO | NTFS_IOMAP_FLAGS_MKWRITE) ||
			NInoAttr(ni) || ni->mft_no < FILE_first_user;
	down_write(&ni->runlist.lock);
	err = ntfs_attr_map_cluster(ni, vcn, &start_lcn, &lcn_count,
			max_clu_count, &balloc, update_mp,
			ntfs_iomap_flags & NTFS_IOMAP_FLAGS_WRITEBACK);
	up_write(&ni->runlist.lock);
	mutex_unlock(&ni->mrec_lock);
	if (err) {
		ni->i_dealloc_clusters = 0;
		return err;
	}

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = offset;

	rl_length = ntfs_cluster_to_bytes(vol, lcn_count);
	if (length > rl_length - vcn_ofs)
		iomap->length = rl_length - vcn_ofs;
	else
		iomap->length = length;

	if (start_lcn == LCN_HOLE)
		iomap->type = IOMAP_HOLE;
	else
		iomap->type = IOMAP_MAPPED;
	if (balloc == true)
		iomap->flags = IOMAP_F_NEW;

	iomap->addr = ntfs_cluster_to_bytes(vol, start_lcn) + vcn_ofs;

	if (balloc == true) {
		if (flags & IOMAP_DIRECT ||
		    ntfs_iomap_flags & NTFS_IOMAP_FLAGS_MKWRITE) {
			loff_t end = offset + length;

			if (vcn_ofs || ((vol->cluster_size > iomap->length) &&
					end < ni->initialized_size))
				err = ntfs_dio_zero_range(inode,
							  start_lcn <<
							  vol->cluster_size_bits,
							  vol->cluster_size);
			if (!err && lcn_count > 1 &&
			    (iomap->length & vol->cluster_size_mask &&
			     end < ni->initialized_size))
				err = ntfs_dio_zero_range(inode,
							  (start_lcn + lcn_count - 1) <<
							  vol->cluster_size_bits,
							  vol->cluster_size);
		} else {
			if (lcn_count > ni->i_dealloc_clusters)
				ni->i_dealloc_clusters = 0;
			else
				ni->i_dealloc_clusters -= lcn_count;
		}
		if (err < 0)
			return err;
	}

	if (ntfs_iomap_flags & NTFS_IOMAP_FLAGS_MKWRITE &&
	    iomap->offset + iomap->length > ni->initialized_size) {
		err = ntfs_attr_set_initialized_size(ni, iomap->offset +
				iomap->length);
	}

	return err;
}

static int ntfs_write_iomap_begin_resident(struct inode *inode, loff_t offset,
		struct iomap *iomap)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct attr_record *a;
	struct ntfs_attr_search_ctx *ctx;
	u32 attr_len;
	int err = 0;
	char *kattr;

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto out;
	}

	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (err) {
		if (err == -ENOENT)
			err = -EIO;
		goto out;
	}

	a = ctx->attr;
	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(a->data.resident.value_length);
	kattr = (u8 *)a + le16_to_cpu(a->data.resident.value_offset);

	iomap->inline_data = kmemdup(kattr, attr_len, GFP_KERNEL);
	if (!iomap->inline_data) {
		err = -ENOMEM;
		goto out;
	}

	iomap->type = IOMAP_INLINE;
	iomap->offset = 0;
	/* iomap requires there is only one INLINE_DATA extent */
	iomap->length = attr_len;

out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	mutex_unlock(&ni->mrec_lock);
	return err;
}

static int ntfs_write_iomap_begin_non_resident(struct inode *inode, loff_t offset,
					       loff_t length, unsigned int flags,
					       struct iomap *iomap, int ntfs_iomap_flags)
{
	struct ntfs_inode *ni = NTFS_I(inode);

	if (ntfs_iomap_flags & (NTFS_IOMAP_FLAGS_BEGIN | NTFS_IOMAP_FLAGS_DIO) &&
	    offset + length > ni->initialized_size) {
		int ret;

		ret = ntfs_extend_initialized_size(inode, offset,
						   offset + length,
						   ntfs_iomap_flags &
						   NTFS_IOMAP_FLAGS_DIO);
		if (ret < 0)
			return ret;
	}

	mutex_lock(&ni->mrec_lock);
	if (ntfs_iomap_flags & NTFS_IOMAP_FLAGS_BEGIN)
		return  ntfs_write_simple_iomap_begin_non_resident(inode, offset,
								   length, iomap);
	else
		return ntfs_write_da_iomap_begin_non_resident(inode,
							      offset, length,
							      flags, iomap,
							      ntfs_iomap_flags);
}

static int __ntfs_write_iomap_begin(struct inode *inode, loff_t offset,
				    loff_t length, unsigned int flags,
				    struct iomap *iomap, int ntfs_iomap_flags)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	loff_t end = offset + length;

	if (NVolShutdown(ni->vol))
		return -EIO;

	if (ntfs_iomap_flags & (NTFS_IOMAP_FLAGS_BEGIN | NTFS_IOMAP_FLAGS_DIO) &&
	    end > ni->data_size) {
		struct ntfs_volume *vol = ni->vol;
		int ret;

		mutex_lock(&ni->mrec_lock);
		if (end > ni->allocated_size &&
		    end < ni->allocated_size + vol->preallocated_size)
			ret = ntfs_attr_expand(ni, end,
					ni->allocated_size + vol->preallocated_size);
		else
			ret = ntfs_attr_expand(ni, end, 0);
		mutex_unlock(&ni->mrec_lock);
		if (ret)
			return ret;
	}

	if (!NInoNonResident(ni)) {
		mutex_lock(&ni->mrec_lock);
		return ntfs_write_iomap_begin_resident(inode, offset, iomap);
	}
	return  ntfs_write_iomap_begin_non_resident(inode, offset, length, flags,
						    iomap, ntfs_iomap_flags);
}

static int ntfs_write_iomap_begin(struct inode *inode, loff_t offset,
				  loff_t length, unsigned int flags,
				  struct iomap *iomap, struct iomap *srcmap)
{
	return __ntfs_write_iomap_begin(inode, offset, length, flags, iomap,
			NTFS_IOMAP_FLAGS_BEGIN);
}

static int ntfs_write_iomap_end_resident(struct inode *inode, loff_t pos,
					 loff_t length, ssize_t written,
					 unsigned int flags, struct iomap *iomap)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ntfs_attr_search_ctx *ctx;
	u32 attr_len;
	int err;
	char *kattr;

	mutex_lock(&ni->mrec_lock);
	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx) {
		written = -ENOMEM;
		mutex_unlock(&ni->mrec_lock);
		return written;
	}

	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			       CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (err) {
		if (err == -ENOENT)
			err = -EIO;
		written = err;
		goto err_out;
	}

	/* The total length of the attribute value. */
	attr_len = le32_to_cpu(ctx->attr->data.resident.value_length);
	if (pos >= attr_len || pos + written > attr_len)
		goto err_out;

	kattr = (u8 *)ctx->attr + le16_to_cpu(ctx->attr->data.resident.value_offset);
	memcpy(kattr + pos, iomap_inline_data(iomap, pos), written);
	mark_mft_record_dirty(ctx->ntfs_ino);
err_out:
	ntfs_attr_put_search_ctx(ctx);
	kfree(iomap->inline_data);
	mutex_unlock(&ni->mrec_lock);
	return written;

}

static int ntfs_write_iomap_end(struct inode *inode, loff_t pos, loff_t length,
				ssize_t written, unsigned int flags,
				struct iomap *iomap)
{
	if (iomap->type == IOMAP_INLINE)
		return ntfs_write_iomap_end_resident(inode, pos, length,
						     written, flags, iomap);
	return written;
}

const struct iomap_ops ntfs_write_iomap_ops = {
	.iomap_begin		= ntfs_write_iomap_begin,
	.iomap_end		= ntfs_write_iomap_end,
};

static int ntfs_page_mkwrite_iomap_begin(struct inode *inode, loff_t offset,
				  loff_t length, unsigned int flags,
				  struct iomap *iomap, struct iomap *srcmap)
{
	return __ntfs_write_iomap_begin(inode, offset, length, flags, iomap,
			NTFS_IOMAP_FLAGS_MKWRITE);
}

const struct iomap_ops ntfs_page_mkwrite_iomap_ops = {
	.iomap_begin		= ntfs_page_mkwrite_iomap_begin,
	.iomap_end		= ntfs_write_iomap_end,
};

static int ntfs_dio_iomap_begin(struct inode *inode, loff_t offset,
				  loff_t length, unsigned int flags,
				  struct iomap *iomap, struct iomap *srcmap)
{
	return __ntfs_write_iomap_begin(inode, offset, length, flags, iomap,
			NTFS_IOMAP_FLAGS_DIO);
}

const struct iomap_ops ntfs_dio_iomap_ops = {
	.iomap_begin		= ntfs_dio_iomap_begin,
	.iomap_end		= ntfs_write_iomap_end,
};

static ssize_t ntfs_writeback_range(struct iomap_writepage_ctx *wpc,
		struct folio *folio, u64 offset, unsigned int len, u64 end_pos)
{
	if (offset < wpc->iomap.offset ||
	    offset >= wpc->iomap.offset + wpc->iomap.length) {
		int error;

		error = __ntfs_write_iomap_begin(wpc->inode, offset,
				NTFS_I(wpc->inode)->allocated_size - offset,
				IOMAP_WRITE, &wpc->iomap,
				NTFS_IOMAP_FLAGS_WRITEBACK);
		if (error)
			return error;
	}

	return iomap_add_to_ioend(wpc, folio, offset, end_pos, len);
}

const struct iomap_writeback_ops ntfs_writeback_ops = {
	.writeback_range	= ntfs_writeback_range,
	.writeback_submit	= iomap_ioend_writeback_submit,
};
