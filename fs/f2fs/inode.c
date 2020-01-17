// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/iyesde.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>

#include "f2fs.h"
#include "yesde.h"
#include "segment.h"
#include "xattr.h"

#include <trace/events/f2fs.h>

void f2fs_mark_iyesde_dirty_sync(struct iyesde *iyesde, bool sync)
{
	if (is_iyesde_flag_set(iyesde, FI_NEW_INODE))
		return;

	if (f2fs_iyesde_dirtied(iyesde, sync))
		return;

	mark_iyesde_dirty_sync(iyesde);
}

void f2fs_set_iyesde_flags(struct iyesde *iyesde)
{
	unsigned int flags = F2FS_I(iyesde)->i_flags;
	unsigned int new_fl = 0;

	if (flags & F2FS_SYNC_FL)
		new_fl |= S_SYNC;
	if (flags & F2FS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & F2FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & F2FS_NOATIME_FL)
		new_fl |= S_NOATIME;
	if (flags & F2FS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	if (file_is_encrypt(iyesde))
		new_fl |= S_ENCRYPTED;
	if (file_is_verity(iyesde))
		new_fl |= S_VERITY;
	if (flags & F2FS_CASEFOLD_FL)
		new_fl |= S_CASEFOLD;
	iyesde_set_flags(iyesde, new_fl,
			S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC|
			S_ENCRYPTED|S_VERITY|S_CASEFOLD);
}

static void __get_iyesde_rdev(struct iyesde *iyesde, struct f2fs_iyesde *ri)
{
	int extra_size = get_extra_isize(iyesde);

	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode) ||
			S_ISFIFO(iyesde->i_mode) || S_ISSOCK(iyesde->i_mode)) {
		if (ri->i_addr[extra_size])
			iyesde->i_rdev = old_decode_dev(
				le32_to_cpu(ri->i_addr[extra_size]));
		else
			iyesde->i_rdev = new_decode_dev(
				le32_to_cpu(ri->i_addr[extra_size + 1]));
	}
}

static int __written_first_block(struct f2fs_sb_info *sbi,
					struct f2fs_iyesde *ri)
{
	block_t addr = le32_to_cpu(ri->i_addr[offset_in_addr(ri)]);

	if (!__is_valid_data_blkaddr(addr))
		return 1;
	if (!f2fs_is_valid_blkaddr(sbi, addr, DATA_GENERIC_ENHANCE))
		return -EFSCORRUPTED;
	return 0;
}

static void __set_iyesde_rdev(struct iyesde *iyesde, struct f2fs_iyesde *ri)
{
	int extra_size = get_extra_isize(iyesde);

	if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode)) {
		if (old_valid_dev(iyesde->i_rdev)) {
			ri->i_addr[extra_size] =
				cpu_to_le32(old_encode_dev(iyesde->i_rdev));
			ri->i_addr[extra_size + 1] = 0;
		} else {
			ri->i_addr[extra_size] = 0;
			ri->i_addr[extra_size + 1] =
				cpu_to_le32(new_encode_dev(iyesde->i_rdev));
			ri->i_addr[extra_size + 2] = 0;
		}
	}
}

static void __recover_inline_status(struct iyesde *iyesde, struct page *ipage)
{
	void *inline_data = inline_data_addr(iyesde, ipage);
	__le32 *start = inline_data;
	__le32 *end = start + MAX_INLINE_DATA(iyesde) / sizeof(__le32);

	while (start < end) {
		if (*start++) {
			f2fs_wait_on_page_writeback(ipage, NODE, true, true);

			set_iyesde_flag(iyesde, FI_DATA_EXIST);
			set_raw_inline(iyesde, F2FS_INODE(ipage));
			set_page_dirty(ipage);
			return;
		}
	}
	return;
}

static bool f2fs_enable_iyesde_chksum(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_iyesde *ri = &F2FS_NODE(page)->i;

	if (!f2fs_sb_has_iyesde_chksum(sbi))
		return false;

	if (!IS_INODE(page) || !(ri->i_inline & F2FS_EXTRA_ATTR))
		return false;

	if (!F2FS_FITS_IN_INODE(ri, le16_to_cpu(ri->i_extra_isize),
				i_iyesde_checksum))
		return false;

	return true;
}

static __u32 f2fs_iyesde_chksum(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_yesde *yesde = F2FS_NODE(page);
	struct f2fs_iyesde *ri = &yesde->i;
	__le32 iyes = yesde->footer.iyes;
	__le32 gen = ri->i_generation;
	__u32 chksum, chksum_seed;
	__u32 dummy_cs = 0;
	unsigned int offset = offsetof(struct f2fs_iyesde, i_iyesde_checksum);
	unsigned int cs_size = sizeof(dummy_cs);

	chksum = f2fs_chksum(sbi, sbi->s_chksum_seed, (__u8 *)&iyes,
							sizeof(iyes));
	chksum_seed = f2fs_chksum(sbi, chksum, (__u8 *)&gen, sizeof(gen));

	chksum = f2fs_chksum(sbi, chksum_seed, (__u8 *)ri, offset);
	chksum = f2fs_chksum(sbi, chksum, (__u8 *)&dummy_cs, cs_size);
	offset += cs_size;
	chksum = f2fs_chksum(sbi, chksum, (__u8 *)ri + offset,
						F2FS_BLKSIZE - offset);
	return chksum;
}

bool f2fs_iyesde_chksum_verify(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_iyesde *ri;
	__u32 provided, calculated;

	if (unlikely(is_sbi_flag_set(sbi, SBI_IS_SHUTDOWN)))
		return true;

#ifdef CONFIG_F2FS_CHECK_FS
	if (!f2fs_enable_iyesde_chksum(sbi, page))
#else
	if (!f2fs_enable_iyesde_chksum(sbi, page) ||
			PageDirty(page) || PageWriteback(page))
#endif
		return true;

	ri = &F2FS_NODE(page)->i;
	provided = le32_to_cpu(ri->i_iyesde_checksum);
	calculated = f2fs_iyesde_chksum(sbi, page);

	if (provided != calculated)
		f2fs_warn(sbi, "checksum invalid, nid = %lu, iyes_of_yesde = %x, %x vs. %x",
			  page->index, iyes_of_yesde(page), provided, calculated);

	return provided == calculated;
}

void f2fs_iyesde_chksum_set(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_iyesde *ri = &F2FS_NODE(page)->i;

	if (!f2fs_enable_iyesde_chksum(sbi, page))
		return;

	ri->i_iyesde_checksum = cpu_to_le32(f2fs_iyesde_chksum(sbi, page));
}

static bool sanity_check_iyesde(struct iyesde *iyesde, struct page *yesde_page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	unsigned long long iblocks;

	iblocks = le64_to_cpu(F2FS_INODE(yesde_page)->i_blocks);
	if (!iblocks) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: corrupted iyesde i_blocks i_iyes=%lx iblocks=%llu, run fsck to fix.",
			  __func__, iyesde->i_iyes, iblocks);
		return false;
	}

	if (iyes_of_yesde(yesde_page) != nid_of_yesde(yesde_page)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: corrupted iyesde footer i_iyes=%lx, iyes,nid: [%u, %u] run fsck to fix.",
			  __func__, iyesde->i_iyes,
			  iyes_of_yesde(yesde_page), nid_of_yesde(yesde_page));
		return false;
	}

	if (f2fs_sb_has_flexible_inline_xattr(sbi)
			&& !f2fs_has_extra_attr(iyesde)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: corrupted iyesde iyes=%lx, run fsck to fix.",
			  __func__, iyesde->i_iyes);
		return false;
	}

	if (f2fs_has_extra_attr(iyesde) &&
			!f2fs_sb_has_extra_attr(sbi)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: iyesde (iyes=%lx) is with extra_attr, but extra_attr feature is off",
			  __func__, iyesde->i_iyes);
		return false;
	}

	if (fi->i_extra_isize > F2FS_TOTAL_EXTRA_ATTR_SIZE ||
			fi->i_extra_isize % sizeof(__le32)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: iyesde (iyes=%lx) has corrupted i_extra_isize: %d, max: %zu",
			  __func__, iyesde->i_iyes, fi->i_extra_isize,
			  F2FS_TOTAL_EXTRA_ATTR_SIZE);
		return false;
	}

	if (f2fs_has_extra_attr(iyesde) &&
		f2fs_sb_has_flexible_inline_xattr(sbi) &&
		f2fs_has_inline_xattr(iyesde) &&
		(!fi->i_inline_xattr_size ||
		fi->i_inline_xattr_size > MAX_INLINE_XATTR_SIZE)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: iyesde (iyes=%lx) has corrupted i_inline_xattr_size: %d, max: %zu",
			  __func__, iyesde->i_iyes, fi->i_inline_xattr_size,
			  MAX_INLINE_XATTR_SIZE);
		return false;
	}

	if (F2FS_I(iyesde)->extent_tree) {
		struct extent_info *ei = &F2FS_I(iyesde)->extent_tree->largest;

		if (ei->len &&
			(!f2fs_is_valid_blkaddr(sbi, ei->blk,
						DATA_GENERIC_ENHANCE) ||
			!f2fs_is_valid_blkaddr(sbi, ei->blk + ei->len - 1,
						DATA_GENERIC_ENHANCE))) {
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			f2fs_warn(sbi, "%s: iyesde (iyes=%lx) extent info [%u, %u, %u] is incorrect, run fsck to fix",
				  __func__, iyesde->i_iyes,
				  ei->blk, ei->fofs, ei->len);
			return false;
		}
	}

	if (f2fs_has_inline_data(iyesde) &&
			(!S_ISREG(iyesde->i_mode) && !S_ISLNK(iyesde->i_mode))) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: iyesde (iyes=%lx, mode=%u) should yest have inline_data, run fsck to fix",
			  __func__, iyesde->i_iyes, iyesde->i_mode);
		return false;
	}

	if (f2fs_has_inline_dentry(iyesde) && !S_ISDIR(iyesde->i_mode)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: iyesde (iyes=%lx, mode=%u) should yest have inline_dentry, run fsck to fix",
			  __func__, iyesde->i_iyes, iyesde->i_mode);
		return false;
	}

	return true;
}

static int do_read_iyesde(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	struct page *yesde_page;
	struct f2fs_iyesde *ri;
	projid_t i_projid;
	int err;

	/* Check if iyes is within scope */
	if (f2fs_check_nid_range(sbi, iyesde->i_iyes))
		return -EINVAL;

	yesde_page = f2fs_get_yesde_page(sbi, iyesde->i_iyes);
	if (IS_ERR(yesde_page))
		return PTR_ERR(yesde_page);

	ri = F2FS_INODE(yesde_page);

	iyesde->i_mode = le16_to_cpu(ri->i_mode);
	i_uid_write(iyesde, le32_to_cpu(ri->i_uid));
	i_gid_write(iyesde, le32_to_cpu(ri->i_gid));
	set_nlink(iyesde, le32_to_cpu(ri->i_links));
	iyesde->i_size = le64_to_cpu(ri->i_size);
	iyesde->i_blocks = SECTOR_FROM_BLOCK(le64_to_cpu(ri->i_blocks) - 1);

	iyesde->i_atime.tv_sec = le64_to_cpu(ri->i_atime);
	iyesde->i_ctime.tv_sec = le64_to_cpu(ri->i_ctime);
	iyesde->i_mtime.tv_sec = le64_to_cpu(ri->i_mtime);
	iyesde->i_atime.tv_nsec = le32_to_cpu(ri->i_atime_nsec);
	iyesde->i_ctime.tv_nsec = le32_to_cpu(ri->i_ctime_nsec);
	iyesde->i_mtime.tv_nsec = le32_to_cpu(ri->i_mtime_nsec);
	iyesde->i_generation = le32_to_cpu(ri->i_generation);
	if (S_ISDIR(iyesde->i_mode))
		fi->i_current_depth = le32_to_cpu(ri->i_current_depth);
	else if (S_ISREG(iyesde->i_mode))
		fi->i_gc_failures[GC_FAILURE_PIN] =
					le16_to_cpu(ri->i_gc_failures);
	fi->i_xattr_nid = le32_to_cpu(ri->i_xattr_nid);
	fi->i_flags = le32_to_cpu(ri->i_flags);
	if (S_ISREG(iyesde->i_mode))
		fi->i_flags &= ~F2FS_PROJINHERIT_FL;
	fi->flags = 0;
	fi->i_advise = ri->i_advise;
	fi->i_piyes = le32_to_cpu(ri->i_piyes);
	fi->i_dir_level = ri->i_dir_level;

	if (f2fs_init_extent_tree(iyesde, &ri->i_ext))
		set_page_dirty(yesde_page);

	get_inline_info(iyesde, ri);

	fi->i_extra_isize = f2fs_has_extra_attr(iyesde) ?
					le16_to_cpu(ri->i_extra_isize) : 0;

	if (f2fs_sb_has_flexible_inline_xattr(sbi)) {
		fi->i_inline_xattr_size = le16_to_cpu(ri->i_inline_xattr_size);
	} else if (f2fs_has_inline_xattr(iyesde) ||
				f2fs_has_inline_dentry(iyesde)) {
		fi->i_inline_xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	} else {

		/*
		 * Previous inline data or directory always reserved 200 bytes
		 * in iyesde layout, even if inline_xattr is disabled. In order
		 * to keep inline_dentry's structure for backward compatibility,
		 * we get the space back only from inline_data.
		 */
		fi->i_inline_xattr_size = 0;
	}

	if (!sanity_check_iyesde(iyesde, yesde_page)) {
		f2fs_put_page(yesde_page, 1);
		return -EFSCORRUPTED;
	}

	/* check data exist */
	if (f2fs_has_inline_data(iyesde) && !f2fs_exist_data(iyesde))
		__recover_inline_status(iyesde, yesde_page);

	/* try to recover cold bit for yesn-dir iyesde */
	if (!S_ISDIR(iyesde->i_mode) && !is_cold_yesde(yesde_page)) {
		set_cold_yesde(yesde_page, false);
		set_page_dirty(yesde_page);
	}

	/* get rdev by using inline_info */
	__get_iyesde_rdev(iyesde, ri);

	if (S_ISREG(iyesde->i_mode)) {
		err = __written_first_block(sbi, ri);
		if (err < 0) {
			f2fs_put_page(yesde_page, 1);
			return err;
		}
		if (!err)
			set_iyesde_flag(iyesde, FI_FIRST_BLOCK_WRITTEN);
	}

	if (!f2fs_need_iyesde_block_update(sbi, iyesde->i_iyes))
		fi->last_disk_size = iyesde->i_size;

	if (fi->i_flags & F2FS_PROJINHERIT_FL)
		set_iyesde_flag(iyesde, FI_PROJ_INHERIT);

	if (f2fs_has_extra_attr(iyesde) && f2fs_sb_has_project_quota(sbi) &&
			F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, i_projid))
		i_projid = (projid_t)le32_to_cpu(ri->i_projid);
	else
		i_projid = F2FS_DEF_PROJID;
	fi->i_projid = make_kprojid(&init_user_ns, i_projid);

	if (f2fs_has_extra_attr(iyesde) && f2fs_sb_has_iyesde_crtime(sbi) &&
			F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, i_crtime)) {
		fi->i_crtime.tv_sec = le64_to_cpu(ri->i_crtime);
		fi->i_crtime.tv_nsec = le32_to_cpu(ri->i_crtime_nsec);
	}

	F2FS_I(iyesde)->i_disk_time[0] = iyesde->i_atime;
	F2FS_I(iyesde)->i_disk_time[1] = iyesde->i_ctime;
	F2FS_I(iyesde)->i_disk_time[2] = iyesde->i_mtime;
	F2FS_I(iyesde)->i_disk_time[3] = F2FS_I(iyesde)->i_crtime;
	f2fs_put_page(yesde_page, 1);

	stat_inc_inline_xattr(iyesde);
	stat_inc_inline_iyesde(iyesde);
	stat_inc_inline_dir(iyesde);

	return 0;
}

struct iyesde *f2fs_iget(struct super_block *sb, unsigned long iyes)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct iyesde *iyesde;
	int ret = 0;

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	if (!(iyesde->i_state & I_NEW)) {
		trace_f2fs_iget(iyesde);
		return iyesde;
	}
	if (iyes == F2FS_NODE_INO(sbi) || iyes == F2FS_META_INO(sbi))
		goto make_yesw;

	ret = do_read_iyesde(iyesde);
	if (ret)
		goto bad_iyesde;
make_yesw:
	if (iyes == F2FS_NODE_INO(sbi)) {
		iyesde->i_mapping->a_ops = &f2fs_yesde_aops;
		mapping_set_gfp_mask(iyesde->i_mapping, GFP_NOFS);
	} else if (iyes == F2FS_META_INO(sbi)) {
		iyesde->i_mapping->a_ops = &f2fs_meta_aops;
		mapping_set_gfp_mask(iyesde->i_mapping, GFP_NOFS);
	} else if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_op = &f2fs_file_iyesde_operations;
		iyesde->i_fop = &f2fs_file_operations;
		iyesde->i_mapping->a_ops = &f2fs_dblock_aops;
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &f2fs_dir_iyesde_operations;
		iyesde->i_fop = &f2fs_dir_operations;
		iyesde->i_mapping->a_ops = &f2fs_dblock_aops;
		iyesde_yeshighmem(iyesde);
	} else if (S_ISLNK(iyesde->i_mode)) {
		if (file_is_encrypt(iyesde))
			iyesde->i_op = &f2fs_encrypted_symlink_iyesde_operations;
		else
			iyesde->i_op = &f2fs_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &f2fs_dblock_aops;
	} else if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode) ||
			S_ISFIFO(iyesde->i_mode) || S_ISSOCK(iyesde->i_mode)) {
		iyesde->i_op = &f2fs_special_iyesde_operations;
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
	} else {
		ret = -EIO;
		goto bad_iyesde;
	}
	f2fs_set_iyesde_flags(iyesde);
	unlock_new_iyesde(iyesde);
	trace_f2fs_iget(iyesde);
	return iyesde;

bad_iyesde:
	f2fs_iyesde_synced(iyesde);
	iget_failed(iyesde);
	trace_f2fs_iget_exit(iyesde, ret);
	return ERR_PTR(ret);
}

struct iyesde *f2fs_iget_retry(struct super_block *sb, unsigned long iyes)
{
	struct iyesde *iyesde;
retry:
	iyesde = f2fs_iget(sb, iyes);
	if (IS_ERR(iyesde)) {
		if (PTR_ERR(iyesde) == -ENOMEM) {
			congestion_wait(BLK_RW_ASYNC, HZ/50);
			goto retry;
		}
	}
	return iyesde;
}

void f2fs_update_iyesde(struct iyesde *iyesde, struct page *yesde_page)
{
	struct f2fs_iyesde *ri;
	struct extent_tree *et = F2FS_I(iyesde)->extent_tree;

	f2fs_wait_on_page_writeback(yesde_page, NODE, true, true);
	set_page_dirty(yesde_page);

	f2fs_iyesde_synced(iyesde);

	ri = F2FS_INODE(yesde_page);

	ri->i_mode = cpu_to_le16(iyesde->i_mode);
	ri->i_advise = F2FS_I(iyesde)->i_advise;
	ri->i_uid = cpu_to_le32(i_uid_read(iyesde));
	ri->i_gid = cpu_to_le32(i_gid_read(iyesde));
	ri->i_links = cpu_to_le32(iyesde->i_nlink);
	ri->i_size = cpu_to_le64(i_size_read(iyesde));
	ri->i_blocks = cpu_to_le64(SECTOR_TO_BLOCK(iyesde->i_blocks) + 1);

	if (et) {
		read_lock(&et->lock);
		set_raw_extent(&et->largest, &ri->i_ext);
		read_unlock(&et->lock);
	} else {
		memset(&ri->i_ext, 0, sizeof(ri->i_ext));
	}
	set_raw_inline(iyesde, ri);

	ri->i_atime = cpu_to_le64(iyesde->i_atime.tv_sec);
	ri->i_ctime = cpu_to_le64(iyesde->i_ctime.tv_sec);
	ri->i_mtime = cpu_to_le64(iyesde->i_mtime.tv_sec);
	ri->i_atime_nsec = cpu_to_le32(iyesde->i_atime.tv_nsec);
	ri->i_ctime_nsec = cpu_to_le32(iyesde->i_ctime.tv_nsec);
	ri->i_mtime_nsec = cpu_to_le32(iyesde->i_mtime.tv_nsec);
	if (S_ISDIR(iyesde->i_mode))
		ri->i_current_depth =
			cpu_to_le32(F2FS_I(iyesde)->i_current_depth);
	else if (S_ISREG(iyesde->i_mode))
		ri->i_gc_failures =
			cpu_to_le16(F2FS_I(iyesde)->i_gc_failures[GC_FAILURE_PIN]);
	ri->i_xattr_nid = cpu_to_le32(F2FS_I(iyesde)->i_xattr_nid);
	ri->i_flags = cpu_to_le32(F2FS_I(iyesde)->i_flags);
	ri->i_piyes = cpu_to_le32(F2FS_I(iyesde)->i_piyes);
	ri->i_generation = cpu_to_le32(iyesde->i_generation);
	ri->i_dir_level = F2FS_I(iyesde)->i_dir_level;

	if (f2fs_has_extra_attr(iyesde)) {
		ri->i_extra_isize = cpu_to_le16(F2FS_I(iyesde)->i_extra_isize);

		if (f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(iyesde)))
			ri->i_inline_xattr_size =
				cpu_to_le16(F2FS_I(iyesde)->i_inline_xattr_size);

		if (f2fs_sb_has_project_quota(F2FS_I_SB(iyesde)) &&
			F2FS_FITS_IN_INODE(ri, F2FS_I(iyesde)->i_extra_isize,
								i_projid)) {
			projid_t i_projid;

			i_projid = from_kprojid(&init_user_ns,
						F2FS_I(iyesde)->i_projid);
			ri->i_projid = cpu_to_le32(i_projid);
		}

		if (f2fs_sb_has_iyesde_crtime(F2FS_I_SB(iyesde)) &&
			F2FS_FITS_IN_INODE(ri, F2FS_I(iyesde)->i_extra_isize,
								i_crtime)) {
			ri->i_crtime =
				cpu_to_le64(F2FS_I(iyesde)->i_crtime.tv_sec);
			ri->i_crtime_nsec =
				cpu_to_le32(F2FS_I(iyesde)->i_crtime.tv_nsec);
		}
	}

	__set_iyesde_rdev(iyesde, ri);

	/* deleted iyesde */
	if (iyesde->i_nlink == 0)
		clear_inline_yesde(yesde_page);

	F2FS_I(iyesde)->i_disk_time[0] = iyesde->i_atime;
	F2FS_I(iyesde)->i_disk_time[1] = iyesde->i_ctime;
	F2FS_I(iyesde)->i_disk_time[2] = iyesde->i_mtime;
	F2FS_I(iyesde)->i_disk_time[3] = F2FS_I(iyesde)->i_crtime;

#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_iyesde_chksum_set(F2FS_I_SB(iyesde), yesde_page);
#endif
}

void f2fs_update_iyesde_page(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct page *yesde_page;
retry:
	yesde_page = f2fs_get_yesde_page(sbi, iyesde->i_iyes);
	if (IS_ERR(yesde_page)) {
		int err = PTR_ERR(yesde_page);
		if (err == -ENOMEM) {
			cond_resched();
			goto retry;
		} else if (err != -ENOENT) {
			f2fs_stop_checkpoint(sbi, false);
		}
		return;
	}
	f2fs_update_iyesde(iyesde, yesde_page);
	f2fs_put_page(yesde_page, 1);
}

int f2fs_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);

	if (iyesde->i_iyes == F2FS_NODE_INO(sbi) ||
			iyesde->i_iyes == F2FS_META_INO(sbi))
		return 0;

	/*
	 * atime could be updated without dirtying f2fs iyesde in lazytime mode
	 */
	if (f2fs_is_time_consistent(iyesde) &&
		!is_iyesde_flag_set(iyesde, FI_DIRTY_INODE))
		return 0;

	if (!f2fs_is_checkpoint_ready(sbi))
		return -ENOSPC;

	/*
	 * We need to balance fs here to prevent from producing dirty yesde pages
	 * during the urgent cleaning time when runing out of free sections.
	 */
	f2fs_update_iyesde_page(iyesde);
	if (wbc && wbc->nr_to_write)
		f2fs_balance_fs(sbi, true);
	return 0;
}

/*
 * Called at the last iput() if i_nlink is zero
 */
void f2fs_evict_iyesde(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	nid_t xnid = F2FS_I(iyesde)->i_xattr_nid;
	int err = 0;

	/* some remained atomic pages should discarded */
	if (f2fs_is_atomic_file(iyesde))
		f2fs_drop_inmem_pages(iyesde);

	trace_f2fs_evict_iyesde(iyesde);
	truncate_iyesde_pages_final(&iyesde->i_data);

	if (iyesde->i_iyes == F2FS_NODE_INO(sbi) ||
			iyesde->i_iyes == F2FS_META_INO(sbi))
		goto out_clear;

	f2fs_bug_on(sbi, get_dirty_pages(iyesde));
	f2fs_remove_dirty_iyesde(iyesde);

	f2fs_destroy_extent_tree(iyesde);

	if (iyesde->i_nlink || is_bad_iyesde(iyesde))
		goto yes_delete;

	err = dquot_initialize(iyesde);
	if (err) {
		err = 0;
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	}

	f2fs_remove_iyes_entry(sbi, iyesde->i_iyes, APPEND_INO);
	f2fs_remove_iyes_entry(sbi, iyesde->i_iyes, UPDATE_INO);
	f2fs_remove_iyes_entry(sbi, iyesde->i_iyes, FLUSH_INO);

	sb_start_intwrite(iyesde->i_sb);
	set_iyesde_flag(iyesde, FI_NO_ALLOC);
	i_size_write(iyesde, 0);
retry:
	if (F2FS_HAS_BLOCKS(iyesde))
		err = f2fs_truncate(iyesde);

	if (time_to_inject(sbi, FAULT_EVICT_INODE)) {
		f2fs_show_injection_info(sbi, FAULT_EVICT_INODE);
		err = -EIO;
	}

	if (!err) {
		f2fs_lock_op(sbi);
		err = f2fs_remove_iyesde_page(iyesde);
		f2fs_unlock_op(sbi);
		if (err == -ENOENT)
			err = 0;
	}

	/* give more chances, if ENOMEM case */
	if (err == -ENOMEM) {
		err = 0;
		goto retry;
	}

	if (err) {
		f2fs_update_iyesde_page(iyesde);
		if (dquot_initialize_needed(iyesde))
			set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	}
	sb_end_intwrite(iyesde->i_sb);
yes_delete:
	dquot_drop(iyesde);

	stat_dec_inline_xattr(iyesde);
	stat_dec_inline_dir(iyesde);
	stat_dec_inline_iyesde(iyesde);

	if (likely(!f2fs_cp_error(sbi) &&
				!is_sbi_flag_set(sbi, SBI_CP_DISABLED)))
		f2fs_bug_on(sbi, is_iyesde_flag_set(iyesde, FI_DIRTY_INODE));
	else
		f2fs_iyesde_synced(iyesde);

	/* iyes == 0, if f2fs_new_iyesde() was failed t*/
	if (iyesde->i_iyes)
		invalidate_mapping_pages(NODE_MAPPING(sbi), iyesde->i_iyes,
							iyesde->i_iyes);
	if (xnid)
		invalidate_mapping_pages(NODE_MAPPING(sbi), xnid, xnid);
	if (iyesde->i_nlink) {
		if (is_iyesde_flag_set(iyesde, FI_APPEND_WRITE))
			f2fs_add_iyes_entry(sbi, iyesde->i_iyes, APPEND_INO);
		if (is_iyesde_flag_set(iyesde, FI_UPDATE_WRITE))
			f2fs_add_iyes_entry(sbi, iyesde->i_iyes, UPDATE_INO);
	}
	if (is_iyesde_flag_set(iyesde, FI_FREE_NID)) {
		f2fs_alloc_nid_failed(sbi, iyesde->i_iyes);
		clear_iyesde_flag(iyesde, FI_FREE_NID);
	} else {
		/*
		 * If xattr nid is corrupted, we can reach out error condition,
		 * err & !f2fs_exist_written_data(sbi, iyesde->i_iyes, ORPHAN_INO)).
		 * In that case, f2fs_check_nid_range() is eyesugh to give a clue.
		 */
	}
out_clear:
	fscrypt_put_encryption_info(iyesde);
	fsverity_cleanup_iyesde(iyesde);
	clear_iyesde(iyesde);
}

/* caller should call f2fs_lock_op() */
void f2fs_handle_failed_iyesde(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct yesde_info ni;
	int err;

	/*
	 * clear nlink of iyesde in order to release resource of iyesde
	 * immediately.
	 */
	clear_nlink(iyesde);

	/*
	 * we must call this to avoid iyesde being remained as dirty, resulting
	 * in a panic when flushing dirty iyesdes in gdirty_list.
	 */
	f2fs_update_iyesde_page(iyesde);
	f2fs_iyesde_synced(iyesde);

	/* don't make bad iyesde, since it becomes a regular file. */
	unlock_new_iyesde(iyesde);

	/*
	 * Note: we should add iyesde to orphan list before f2fs_unlock_op()
	 * so we can prevent losing this orphan when encoutering checkpoint
	 * and following suddenly power-off.
	 */
	err = f2fs_get_yesde_info(sbi, iyesde->i_iyes, &ni);
	if (err) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "May loss orphan iyesde, run fsck to fix.");
		goto out;
	}

	if (ni.blk_addr != NULL_ADDR) {
		err = f2fs_acquire_orphan_iyesde(sbi);
		if (err) {
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			f2fs_warn(sbi, "Too many orphan iyesdes, run fsck to fix.");
		} else {
			f2fs_add_orphan_iyesde(iyesde);
		}
		f2fs_alloc_nid_done(sbi, iyesde->i_iyes);
	} else {
		set_iyesde_flag(iyesde, FI_FREE_NID);
	}

out:
	f2fs_unlock_op(sbi);

	/* iput will drop the iyesde object */
	iput(iyesde);
}
