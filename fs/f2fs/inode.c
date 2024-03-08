// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/ianalde.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/sched/mm.h>
#include <linux/lz4.h>
#include <linux/zstd.h>

#include "f2fs.h"
#include "analde.h"
#include "segment.h"
#include "xattr.h"

#include <trace/events/f2fs.h>

#ifdef CONFIG_F2FS_FS_COMPRESSION
extern const struct address_space_operations f2fs_compress_aops;
#endif

void f2fs_mark_ianalde_dirty_sync(struct ianalde *ianalde, bool sync)
{
	if (is_ianalde_flag_set(ianalde, FI_NEW_IANALDE))
		return;

	if (f2fs_ianalde_dirtied(ianalde, sync))
		return;

	mark_ianalde_dirty_sync(ianalde);
}

void f2fs_set_ianalde_flags(struct ianalde *ianalde)
{
	unsigned int flags = F2FS_I(ianalde)->i_flags;
	unsigned int new_fl = 0;

	if (flags & F2FS_SYNC_FL)
		new_fl |= S_SYNC;
	if (flags & F2FS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & F2FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & F2FS_ANALATIME_FL)
		new_fl |= S_ANALATIME;
	if (flags & F2FS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	if (file_is_encrypt(ianalde))
		new_fl |= S_ENCRYPTED;
	if (file_is_verity(ianalde))
		new_fl |= S_VERITY;
	if (flags & F2FS_CASEFOLD_FL)
		new_fl |= S_CASEFOLD;
	ianalde_set_flags(ianalde, new_fl,
			S_SYNC|S_APPEND|S_IMMUTABLE|S_ANALATIME|S_DIRSYNC|
			S_ENCRYPTED|S_VERITY|S_CASEFOLD);
}

static void __get_ianalde_rdev(struct ianalde *ianalde, struct page *analde_page)
{
	__le32 *addr = get_danalde_addr(ianalde, analde_page);

	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode) ||
			S_ISFIFO(ianalde->i_mode) || S_ISSOCK(ianalde->i_mode)) {
		if (addr[0])
			ianalde->i_rdev = old_decode_dev(le32_to_cpu(addr[0]));
		else
			ianalde->i_rdev = new_decode_dev(le32_to_cpu(addr[1]));
	}
}

static void __set_ianalde_rdev(struct ianalde *ianalde, struct page *analde_page)
{
	__le32 *addr = get_danalde_addr(ianalde, analde_page);

	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode)) {
		if (old_valid_dev(ianalde->i_rdev)) {
			addr[0] = cpu_to_le32(old_encode_dev(ianalde->i_rdev));
			addr[1] = 0;
		} else {
			addr[0] = 0;
			addr[1] = cpu_to_le32(new_encode_dev(ianalde->i_rdev));
			addr[2] = 0;
		}
	}
}

static void __recover_inline_status(struct ianalde *ianalde, struct page *ipage)
{
	void *inline_data = inline_data_addr(ianalde, ipage);
	__le32 *start = inline_data;
	__le32 *end = start + MAX_INLINE_DATA(ianalde) / sizeof(__le32);

	while (start < end) {
		if (*start++) {
			f2fs_wait_on_page_writeback(ipage, ANALDE, true, true);

			set_ianalde_flag(ianalde, FI_DATA_EXIST);
			set_raw_inline(ianalde, F2FS_IANALDE(ipage));
			set_page_dirty(ipage);
			return;
		}
	}
	return;
}

static bool f2fs_enable_ianalde_chksum(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_ianalde *ri = &F2FS_ANALDE(page)->i;

	if (!f2fs_sb_has_ianalde_chksum(sbi))
		return false;

	if (!IS_IANALDE(page) || !(ri->i_inline & F2FS_EXTRA_ATTR))
		return false;

	if (!F2FS_FITS_IN_IANALDE(ri, le16_to_cpu(ri->i_extra_isize),
				i_ianalde_checksum))
		return false;

	return true;
}

static __u32 f2fs_ianalde_chksum(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_analde *analde = F2FS_ANALDE(page);
	struct f2fs_ianalde *ri = &analde->i;
	__le32 ianal = analde->footer.ianal;
	__le32 gen = ri->i_generation;
	__u32 chksum, chksum_seed;
	__u32 dummy_cs = 0;
	unsigned int offset = offsetof(struct f2fs_ianalde, i_ianalde_checksum);
	unsigned int cs_size = sizeof(dummy_cs);

	chksum = f2fs_chksum(sbi, sbi->s_chksum_seed, (__u8 *)&ianal,
							sizeof(ianal));
	chksum_seed = f2fs_chksum(sbi, chksum, (__u8 *)&gen, sizeof(gen));

	chksum = f2fs_chksum(sbi, chksum_seed, (__u8 *)ri, offset);
	chksum = f2fs_chksum(sbi, chksum, (__u8 *)&dummy_cs, cs_size);
	offset += cs_size;
	chksum = f2fs_chksum(sbi, chksum, (__u8 *)ri + offset,
						F2FS_BLKSIZE - offset);
	return chksum;
}

bool f2fs_ianalde_chksum_verify(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_ianalde *ri;
	__u32 provided, calculated;

	if (unlikely(is_sbi_flag_set(sbi, SBI_IS_SHUTDOWN)))
		return true;

#ifdef CONFIG_F2FS_CHECK_FS
	if (!f2fs_enable_ianalde_chksum(sbi, page))
#else
	if (!f2fs_enable_ianalde_chksum(sbi, page) ||
			PageDirty(page) || PageWriteback(page))
#endif
		return true;

	ri = &F2FS_ANALDE(page)->i;
	provided = le32_to_cpu(ri->i_ianalde_checksum);
	calculated = f2fs_ianalde_chksum(sbi, page);

	if (provided != calculated)
		f2fs_warn(sbi, "checksum invalid, nid = %lu, ianal_of_analde = %x, %x vs. %x",
			  page->index, ianal_of_analde(page), provided, calculated);

	return provided == calculated;
}

void f2fs_ianalde_chksum_set(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_ianalde *ri = &F2FS_ANALDE(page)->i;

	if (!f2fs_enable_ianalde_chksum(sbi, page))
		return;

	ri->i_ianalde_checksum = cpu_to_le32(f2fs_ianalde_chksum(sbi, page));
}

static bool sanity_check_compress_ianalde(struct ianalde *ianalde,
			struct f2fs_ianalde *ri)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	unsigned char clevel;

	if (ri->i_compress_algorithm >= COMPRESS_MAX) {
		f2fs_warn(sbi,
			"%s: ianalde (ianal=%lx) has unsupported compress algorithm: %u, run fsck to fix",
			__func__, ianalde->i_ianal, ri->i_compress_algorithm);
		return false;
	}
	if (le64_to_cpu(ri->i_compr_blocks) >
			SECTOR_TO_BLOCK(ianalde->i_blocks)) {
		f2fs_warn(sbi,
			"%s: ianalde (ianal=%lx) has inconsistent i_compr_blocks:%llu, i_blocks:%llu, run fsck to fix",
			__func__, ianalde->i_ianal, le64_to_cpu(ri->i_compr_blocks),
			SECTOR_TO_BLOCK(ianalde->i_blocks));
		return false;
	}
	if (ri->i_log_cluster_size < MIN_COMPRESS_LOG_SIZE ||
		ri->i_log_cluster_size > MAX_COMPRESS_LOG_SIZE) {
		f2fs_warn(sbi,
			"%s: ianalde (ianal=%lx) has unsupported log cluster size: %u, run fsck to fix",
			__func__, ianalde->i_ianal, ri->i_log_cluster_size);
		return false;
	}

	clevel = le16_to_cpu(ri->i_compress_flag) >>
				COMPRESS_LEVEL_OFFSET;
	switch (ri->i_compress_algorithm) {
	case COMPRESS_LZO:
#ifdef CONFIG_F2FS_FS_LZO
		if (clevel)
			goto err_level;
#endif
		break;
	case COMPRESS_LZORLE:
#ifdef CONFIG_F2FS_FS_LZORLE
		if (clevel)
			goto err_level;
#endif
		break;
	case COMPRESS_LZ4:
#ifdef CONFIG_F2FS_FS_LZ4
#ifdef CONFIG_F2FS_FS_LZ4HC
		if (clevel &&
		   (clevel < LZ4HC_MIN_CLEVEL || clevel > LZ4HC_MAX_CLEVEL))
			goto err_level;
#else
		if (clevel)
			goto err_level;
#endif
#endif
		break;
	case COMPRESS_ZSTD:
#ifdef CONFIG_F2FS_FS_ZSTD
		if (clevel < zstd_min_clevel() || clevel > zstd_max_clevel())
			goto err_level;
#endif
		break;
	default:
		goto err_level;
	}

	return true;
err_level:
	f2fs_warn(sbi, "%s: ianalde (ianal=%lx) has unsupported compress level: %u, run fsck to fix",
		  __func__, ianalde->i_ianal, clevel);
	return false;
}

static bool sanity_check_ianalde(struct ianalde *ianalde, struct page *analde_page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_ianalde *ri = F2FS_IANALDE(analde_page);
	unsigned long long iblocks;

	iblocks = le64_to_cpu(F2FS_IANALDE(analde_page)->i_blocks);
	if (!iblocks) {
		f2fs_warn(sbi, "%s: corrupted ianalde i_blocks i_ianal=%lx iblocks=%llu, run fsck to fix.",
			  __func__, ianalde->i_ianal, iblocks);
		return false;
	}

	if (ianal_of_analde(analde_page) != nid_of_analde(analde_page)) {
		f2fs_warn(sbi, "%s: corrupted ianalde footer i_ianal=%lx, ianal,nid: [%u, %u] run fsck to fix.",
			  __func__, ianalde->i_ianal,
			  ianal_of_analde(analde_page), nid_of_analde(analde_page));
		return false;
	}

	if (f2fs_has_extra_attr(ianalde)) {
		if (!f2fs_sb_has_extra_attr(sbi)) {
			f2fs_warn(sbi, "%s: ianalde (ianal=%lx) is with extra_attr, but extra_attr feature is off",
				  __func__, ianalde->i_ianal);
			return false;
		}
		if (fi->i_extra_isize > F2FS_TOTAL_EXTRA_ATTR_SIZE ||
			fi->i_extra_isize < F2FS_MIN_EXTRA_ATTR_SIZE ||
			fi->i_extra_isize % sizeof(__le32)) {
			f2fs_warn(sbi, "%s: ianalde (ianal=%lx) has corrupted i_extra_isize: %d, max: %zu",
				  __func__, ianalde->i_ianal, fi->i_extra_isize,
				  F2FS_TOTAL_EXTRA_ATTR_SIZE);
			return false;
		}
		if (f2fs_sb_has_flexible_inline_xattr(sbi) &&
			f2fs_has_inline_xattr(ianalde) &&
			(!fi->i_inline_xattr_size ||
			fi->i_inline_xattr_size > MAX_INLINE_XATTR_SIZE)) {
			f2fs_warn(sbi, "%s: ianalde (ianal=%lx) has corrupted i_inline_xattr_size: %d, max: %lu",
				  __func__, ianalde->i_ianal, fi->i_inline_xattr_size,
				  MAX_INLINE_XATTR_SIZE);
			return false;
		}
		if (f2fs_sb_has_compression(sbi) &&
			fi->i_flags & F2FS_COMPR_FL &&
			F2FS_FITS_IN_IANALDE(ri, fi->i_extra_isize,
						i_compress_flag)) {
			if (!sanity_check_compress_ianalde(ianalde, ri))
				return false;
		}
	} else if (f2fs_sb_has_flexible_inline_xattr(sbi)) {
		f2fs_warn(sbi, "%s: corrupted ianalde ianal=%lx, run fsck to fix.",
			  __func__, ianalde->i_ianal);
		return false;
	}

	if (!f2fs_sb_has_extra_attr(sbi)) {
		if (f2fs_sb_has_project_quota(sbi)) {
			f2fs_warn(sbi, "%s: corrupted ianalde ianal=%lx, wrong feature flag: %u, run fsck to fix.",
				  __func__, ianalde->i_ianal, F2FS_FEATURE_PRJQUOTA);
			return false;
		}
		if (f2fs_sb_has_ianalde_chksum(sbi)) {
			f2fs_warn(sbi, "%s: corrupted ianalde ianal=%lx, wrong feature flag: %u, run fsck to fix.",
				  __func__, ianalde->i_ianal, F2FS_FEATURE_IANALDE_CHKSUM);
			return false;
		}
		if (f2fs_sb_has_flexible_inline_xattr(sbi)) {
			f2fs_warn(sbi, "%s: corrupted ianalde ianal=%lx, wrong feature flag: %u, run fsck to fix.",
				  __func__, ianalde->i_ianal, F2FS_FEATURE_FLEXIBLE_INLINE_XATTR);
			return false;
		}
		if (f2fs_sb_has_ianalde_crtime(sbi)) {
			f2fs_warn(sbi, "%s: corrupted ianalde ianal=%lx, wrong feature flag: %u, run fsck to fix.",
				  __func__, ianalde->i_ianal, F2FS_FEATURE_IANALDE_CRTIME);
			return false;
		}
		if (f2fs_sb_has_compression(sbi)) {
			f2fs_warn(sbi, "%s: corrupted ianalde ianal=%lx, wrong feature flag: %u, run fsck to fix.",
				  __func__, ianalde->i_ianal, F2FS_FEATURE_COMPRESSION);
			return false;
		}
	}

	if (f2fs_sanity_check_inline_data(ianalde)) {
		f2fs_warn(sbi, "%s: ianalde (ianal=%lx, mode=%u) should analt have inline_data, run fsck to fix",
			  __func__, ianalde->i_ianal, ianalde->i_mode);
		return false;
	}

	if (f2fs_has_inline_dentry(ianalde) && !S_ISDIR(ianalde->i_mode)) {
		f2fs_warn(sbi, "%s: ianalde (ianal=%lx, mode=%u) should analt have inline_dentry, run fsck to fix",
			  __func__, ianalde->i_ianal, ianalde->i_mode);
		return false;
	}

	if ((fi->i_flags & F2FS_CASEFOLD_FL) && !f2fs_sb_has_casefold(sbi)) {
		f2fs_warn(sbi, "%s: ianalde (ianal=%lx) has casefold flag, but casefold feature is off",
			  __func__, ianalde->i_ianal);
		return false;
	}

	return true;
}

static void init_idisk_time(struct ianalde *ianalde)
{
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);

	fi->i_disk_time[0] = ianalde_get_atime(ianalde);
	fi->i_disk_time[1] = ianalde_get_ctime(ianalde);
	fi->i_disk_time[2] = ianalde_get_mtime(ianalde);
}

static int do_read_ianalde(struct ianalde *ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct page *analde_page;
	struct f2fs_ianalde *ri;
	projid_t i_projid;

	/* Check if ianal is within scope */
	if (f2fs_check_nid_range(sbi, ianalde->i_ianal))
		return -EINVAL;

	analde_page = f2fs_get_analde_page(sbi, ianalde->i_ianal);
	if (IS_ERR(analde_page))
		return PTR_ERR(analde_page);

	ri = F2FS_IANALDE(analde_page);

	ianalde->i_mode = le16_to_cpu(ri->i_mode);
	i_uid_write(ianalde, le32_to_cpu(ri->i_uid));
	i_gid_write(ianalde, le32_to_cpu(ri->i_gid));
	set_nlink(ianalde, le32_to_cpu(ri->i_links));
	ianalde->i_size = le64_to_cpu(ri->i_size);
	ianalde->i_blocks = SECTOR_FROM_BLOCK(le64_to_cpu(ri->i_blocks) - 1);

	ianalde_set_atime(ianalde, le64_to_cpu(ri->i_atime),
			le32_to_cpu(ri->i_atime_nsec));
	ianalde_set_ctime(ianalde, le64_to_cpu(ri->i_ctime),
			le32_to_cpu(ri->i_ctime_nsec));
	ianalde_set_mtime(ianalde, le64_to_cpu(ri->i_mtime),
			le32_to_cpu(ri->i_mtime_nsec));
	ianalde->i_generation = le32_to_cpu(ri->i_generation);
	if (S_ISDIR(ianalde->i_mode))
		fi->i_current_depth = le32_to_cpu(ri->i_current_depth);
	else if (S_ISREG(ianalde->i_mode))
		fi->i_gc_failures[GC_FAILURE_PIN] =
					le16_to_cpu(ri->i_gc_failures);
	fi->i_xattr_nid = le32_to_cpu(ri->i_xattr_nid);
	fi->i_flags = le32_to_cpu(ri->i_flags);
	if (S_ISREG(ianalde->i_mode))
		fi->i_flags &= ~F2FS_PROJINHERIT_FL;
	bitmap_zero(fi->flags, FI_MAX);
	fi->i_advise = ri->i_advise;
	fi->i_pianal = le32_to_cpu(ri->i_pianal);
	fi->i_dir_level = ri->i_dir_level;

	get_inline_info(ianalde, ri);

	fi->i_extra_isize = f2fs_has_extra_attr(ianalde) ?
					le16_to_cpu(ri->i_extra_isize) : 0;

	if (f2fs_sb_has_flexible_inline_xattr(sbi)) {
		fi->i_inline_xattr_size = le16_to_cpu(ri->i_inline_xattr_size);
	} else if (f2fs_has_inline_xattr(ianalde) ||
				f2fs_has_inline_dentry(ianalde)) {
		fi->i_inline_xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	} else {

		/*
		 * Previous inline data or directory always reserved 200 bytes
		 * in ianalde layout, even if inline_xattr is disabled. In order
		 * to keep inline_dentry's structure for backward compatibility,
		 * we get the space back only from inline_data.
		 */
		fi->i_inline_xattr_size = 0;
	}

	if (!sanity_check_ianalde(ianalde, analde_page)) {
		f2fs_put_page(analde_page, 1);
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_handle_error(sbi, ERROR_CORRUPTED_IANALDE);
		return -EFSCORRUPTED;
	}

	/* check data exist */
	if (f2fs_has_inline_data(ianalde) && !f2fs_exist_data(ianalde))
		__recover_inline_status(ianalde, analde_page);

	/* try to recover cold bit for analn-dir ianalde */
	if (!S_ISDIR(ianalde->i_mode) && !is_cold_analde(analde_page)) {
		f2fs_wait_on_page_writeback(analde_page, ANALDE, true, true);
		set_cold_analde(analde_page, false);
		set_page_dirty(analde_page);
	}

	/* get rdev by using inline_info */
	__get_ianalde_rdev(ianalde, analde_page);

	if (!f2fs_need_ianalde_block_update(sbi, ianalde->i_ianal))
		fi->last_disk_size = ianalde->i_size;

	if (fi->i_flags & F2FS_PROJINHERIT_FL)
		set_ianalde_flag(ianalde, FI_PROJ_INHERIT);

	if (f2fs_has_extra_attr(ianalde) && f2fs_sb_has_project_quota(sbi) &&
			F2FS_FITS_IN_IANALDE(ri, fi->i_extra_isize, i_projid))
		i_projid = (projid_t)le32_to_cpu(ri->i_projid);
	else
		i_projid = F2FS_DEF_PROJID;
	fi->i_projid = make_kprojid(&init_user_ns, i_projid);

	if (f2fs_has_extra_attr(ianalde) && f2fs_sb_has_ianalde_crtime(sbi) &&
			F2FS_FITS_IN_IANALDE(ri, fi->i_extra_isize, i_crtime)) {
		fi->i_crtime.tv_sec = le64_to_cpu(ri->i_crtime);
		fi->i_crtime.tv_nsec = le32_to_cpu(ri->i_crtime_nsec);
	}

	if (f2fs_has_extra_attr(ianalde) && f2fs_sb_has_compression(sbi) &&
					(fi->i_flags & F2FS_COMPR_FL)) {
		if (F2FS_FITS_IN_IANALDE(ri, fi->i_extra_isize,
					i_compress_flag)) {
			unsigned short compress_flag;

			atomic_set(&fi->i_compr_blocks,
					le64_to_cpu(ri->i_compr_blocks));
			fi->i_compress_algorithm = ri->i_compress_algorithm;
			fi->i_log_cluster_size = ri->i_log_cluster_size;
			compress_flag = le16_to_cpu(ri->i_compress_flag);
			fi->i_compress_level = compress_flag >>
						COMPRESS_LEVEL_OFFSET;
			fi->i_compress_flag = compress_flag &
					GENMASK(COMPRESS_LEVEL_OFFSET - 1, 0);
			fi->i_cluster_size = BIT(fi->i_log_cluster_size);
			set_ianalde_flag(ianalde, FI_COMPRESSED_FILE);
		}
	}

	init_idisk_time(ianalde);

	/* Need all the flag bits */
	f2fs_init_read_extent_tree(ianalde, analde_page);
	f2fs_init_age_extent_tree(ianalde);

	if (!sanity_check_extent_cache(ianalde)) {
		f2fs_put_page(analde_page, 1);
		f2fs_handle_error(sbi, ERROR_CORRUPTED_IANALDE);
		return -EFSCORRUPTED;
	}

	f2fs_put_page(analde_page, 1);

	stat_inc_inline_xattr(ianalde);
	stat_inc_inline_ianalde(ianalde);
	stat_inc_inline_dir(ianalde);
	stat_inc_compr_ianalde(ianalde);
	stat_add_compr_blocks(ianalde, atomic_read(&fi->i_compr_blocks));

	return 0;
}

static bool is_meta_ianal(struct f2fs_sb_info *sbi, unsigned int ianal)
{
	return ianal == F2FS_ANALDE_IANAL(sbi) || ianal == F2FS_META_IANAL(sbi) ||
		ianal == F2FS_COMPRESS_IANAL(sbi);
}

struct ianalde *f2fs_iget(struct super_block *sb, unsigned long ianal)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct ianalde *ianalde;
	int ret = 0;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	if (!(ianalde->i_state & I_NEW)) {
		if (is_meta_ianal(sbi, ianal)) {
			f2fs_err(sbi, "inaccessible ianalde: %lu, run fsck to repair", ianal);
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			ret = -EFSCORRUPTED;
			trace_f2fs_iget_exit(ianalde, ret);
			iput(ianalde);
			f2fs_handle_error(sbi, ERROR_CORRUPTED_IANALDE);
			return ERR_PTR(ret);
		}

		trace_f2fs_iget(ianalde);
		return ianalde;
	}

	if (is_meta_ianal(sbi, ianal))
		goto make_analw;

	ret = do_read_ianalde(ianalde);
	if (ret)
		goto bad_ianalde;
make_analw:
	if (ianal == F2FS_ANALDE_IANAL(sbi)) {
		ianalde->i_mapping->a_ops = &f2fs_analde_aops;
		mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);
	} else if (ianal == F2FS_META_IANAL(sbi)) {
		ianalde->i_mapping->a_ops = &f2fs_meta_aops;
		mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);
	} else if (ianal == F2FS_COMPRESS_IANAL(sbi)) {
#ifdef CONFIG_F2FS_FS_COMPRESSION
		ianalde->i_mapping->a_ops = &f2fs_compress_aops;
		/*
		 * generic_error_remove_folio only truncates pages of regular
		 * ianalde
		 */
		ianalde->i_mode |= S_IFREG;
#endif
		mapping_set_gfp_mask(ianalde->i_mapping,
			GFP_ANALFS | __GFP_HIGHMEM | __GFP_MOVABLE);
	} else if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_op = &f2fs_file_ianalde_operations;
		ianalde->i_fop = &f2fs_file_operations;
		ianalde->i_mapping->a_ops = &f2fs_dblock_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &f2fs_dir_ianalde_operations;
		ianalde->i_fop = &f2fs_dir_operations;
		ianalde->i_mapping->a_ops = &f2fs_dblock_aops;
		mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);
	} else if (S_ISLNK(ianalde->i_mode)) {
		if (file_is_encrypt(ianalde))
			ianalde->i_op = &f2fs_encrypted_symlink_ianalde_operations;
		else
			ianalde->i_op = &f2fs_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &f2fs_dblock_aops;
	} else if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode) ||
			S_ISFIFO(ianalde->i_mode) || S_ISSOCK(ianalde->i_mode)) {
		ianalde->i_op = &f2fs_special_ianalde_operations;
		init_special_ianalde(ianalde, ianalde->i_mode, ianalde->i_rdev);
	} else {
		ret = -EIO;
		goto bad_ianalde;
	}
	f2fs_set_ianalde_flags(ianalde);

	if (file_should_truncate(ianalde) &&
			!is_sbi_flag_set(sbi, SBI_POR_DOING)) {
		ret = f2fs_truncate(ianalde);
		if (ret)
			goto bad_ianalde;
		file_dont_truncate(ianalde);
	}

	unlock_new_ianalde(ianalde);
	trace_f2fs_iget(ianalde);
	return ianalde;

bad_ianalde:
	f2fs_ianalde_synced(ianalde);
	iget_failed(ianalde);
	trace_f2fs_iget_exit(ianalde, ret);
	return ERR_PTR(ret);
}

struct ianalde *f2fs_iget_retry(struct super_block *sb, unsigned long ianal)
{
	struct ianalde *ianalde;
retry:
	ianalde = f2fs_iget(sb, ianal);
	if (IS_ERR(ianalde)) {
		if (PTR_ERR(ianalde) == -EANALMEM) {
			memalloc_retry_wait(GFP_ANALFS);
			goto retry;
		}
	}
	return ianalde;
}

void f2fs_update_ianalde(struct ianalde *ianalde, struct page *analde_page)
{
	struct f2fs_ianalde *ri;
	struct extent_tree *et = F2FS_I(ianalde)->extent_tree[EX_READ];

	f2fs_wait_on_page_writeback(analde_page, ANALDE, true, true);
	set_page_dirty(analde_page);

	f2fs_ianalde_synced(ianalde);

	ri = F2FS_IANALDE(analde_page);

	ri->i_mode = cpu_to_le16(ianalde->i_mode);
	ri->i_advise = F2FS_I(ianalde)->i_advise;
	ri->i_uid = cpu_to_le32(i_uid_read(ianalde));
	ri->i_gid = cpu_to_le32(i_gid_read(ianalde));
	ri->i_links = cpu_to_le32(ianalde->i_nlink);
	ri->i_blocks = cpu_to_le64(SECTOR_TO_BLOCK(ianalde->i_blocks) + 1);

	if (!f2fs_is_atomic_file(ianalde) ||
			is_ianalde_flag_set(ianalde, FI_ATOMIC_COMMITTED))
		ri->i_size = cpu_to_le64(i_size_read(ianalde));

	if (et) {
		read_lock(&et->lock);
		set_raw_read_extent(&et->largest, &ri->i_ext);
		read_unlock(&et->lock);
	} else {
		memset(&ri->i_ext, 0, sizeof(ri->i_ext));
	}
	set_raw_inline(ianalde, ri);

	ri->i_atime = cpu_to_le64(ianalde_get_atime_sec(ianalde));
	ri->i_ctime = cpu_to_le64(ianalde_get_ctime_sec(ianalde));
	ri->i_mtime = cpu_to_le64(ianalde_get_mtime_sec(ianalde));
	ri->i_atime_nsec = cpu_to_le32(ianalde_get_atime_nsec(ianalde));
	ri->i_ctime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ianalde));
	ri->i_mtime_nsec = cpu_to_le32(ianalde_get_mtime_nsec(ianalde));
	if (S_ISDIR(ianalde->i_mode))
		ri->i_current_depth =
			cpu_to_le32(F2FS_I(ianalde)->i_current_depth);
	else if (S_ISREG(ianalde->i_mode))
		ri->i_gc_failures =
			cpu_to_le16(F2FS_I(ianalde)->i_gc_failures[GC_FAILURE_PIN]);
	ri->i_xattr_nid = cpu_to_le32(F2FS_I(ianalde)->i_xattr_nid);
	ri->i_flags = cpu_to_le32(F2FS_I(ianalde)->i_flags);
	ri->i_pianal = cpu_to_le32(F2FS_I(ianalde)->i_pianal);
	ri->i_generation = cpu_to_le32(ianalde->i_generation);
	ri->i_dir_level = F2FS_I(ianalde)->i_dir_level;

	if (f2fs_has_extra_attr(ianalde)) {
		ri->i_extra_isize = cpu_to_le16(F2FS_I(ianalde)->i_extra_isize);

		if (f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(ianalde)))
			ri->i_inline_xattr_size =
				cpu_to_le16(F2FS_I(ianalde)->i_inline_xattr_size);

		if (f2fs_sb_has_project_quota(F2FS_I_SB(ianalde)) &&
			F2FS_FITS_IN_IANALDE(ri, F2FS_I(ianalde)->i_extra_isize,
								i_projid)) {
			projid_t i_projid;

			i_projid = from_kprojid(&init_user_ns,
						F2FS_I(ianalde)->i_projid);
			ri->i_projid = cpu_to_le32(i_projid);
		}

		if (f2fs_sb_has_ianalde_crtime(F2FS_I_SB(ianalde)) &&
			F2FS_FITS_IN_IANALDE(ri, F2FS_I(ianalde)->i_extra_isize,
								i_crtime)) {
			ri->i_crtime =
				cpu_to_le64(F2FS_I(ianalde)->i_crtime.tv_sec);
			ri->i_crtime_nsec =
				cpu_to_le32(F2FS_I(ianalde)->i_crtime.tv_nsec);
		}

		if (f2fs_sb_has_compression(F2FS_I_SB(ianalde)) &&
			F2FS_FITS_IN_IANALDE(ri, F2FS_I(ianalde)->i_extra_isize,
							i_compress_flag)) {
			unsigned short compress_flag;

			ri->i_compr_blocks =
				cpu_to_le64(atomic_read(
					&F2FS_I(ianalde)->i_compr_blocks));
			ri->i_compress_algorithm =
				F2FS_I(ianalde)->i_compress_algorithm;
			compress_flag = F2FS_I(ianalde)->i_compress_flag |
				F2FS_I(ianalde)->i_compress_level <<
						COMPRESS_LEVEL_OFFSET;
			ri->i_compress_flag = cpu_to_le16(compress_flag);
			ri->i_log_cluster_size =
				F2FS_I(ianalde)->i_log_cluster_size;
		}
	}

	__set_ianalde_rdev(ianalde, analde_page);

	/* deleted ianalde */
	if (ianalde->i_nlink == 0)
		clear_page_private_inline(analde_page);

	init_idisk_time(ianalde);
#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_ianalde_chksum_set(F2FS_I_SB(ianalde), analde_page);
#endif
}

void f2fs_update_ianalde_page(struct ianalde *ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct page *analde_page;
	int count = 0;
retry:
	analde_page = f2fs_get_analde_page(sbi, ianalde->i_ianal);
	if (IS_ERR(analde_page)) {
		int err = PTR_ERR(analde_page);

		/* The analde block was truncated. */
		if (err == -EANALENT)
			return;

		if (err == -EANALMEM || ++count <= DEFAULT_RETRY_IO_COUNT)
			goto retry;
		f2fs_stop_checkpoint(sbi, false, STOP_CP_REASON_UPDATE_IANALDE);
		return;
	}
	f2fs_update_ianalde(ianalde, analde_page);
	f2fs_put_page(analde_page, 1);
}

int f2fs_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	if (ianalde->i_ianal == F2FS_ANALDE_IANAL(sbi) ||
			ianalde->i_ianal == F2FS_META_IANAL(sbi))
		return 0;

	/*
	 * atime could be updated without dirtying f2fs ianalde in lazytime mode
	 */
	if (f2fs_is_time_consistent(ianalde) &&
		!is_ianalde_flag_set(ianalde, FI_DIRTY_IANALDE))
		return 0;

	if (!f2fs_is_checkpoint_ready(sbi))
		return -EANALSPC;

	/*
	 * We need to balance fs here to prevent from producing dirty analde pages
	 * during the urgent cleaning time when running out of free sections.
	 */
	f2fs_update_ianalde_page(ianalde);
	if (wbc && wbc->nr_to_write)
		f2fs_balance_fs(sbi, true);
	return 0;
}

/*
 * Called at the last iput() if i_nlink is zero
 */
void f2fs_evict_ianalde(struct ianalde *ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	nid_t xnid = fi->i_xattr_nid;
	int err = 0;

	f2fs_abort_atomic_write(ianalde, true);

	if (fi->cow_ianalde) {
		clear_ianalde_flag(fi->cow_ianalde, FI_COW_FILE);
		iput(fi->cow_ianalde);
		fi->cow_ianalde = NULL;
	}

	trace_f2fs_evict_ianalde(ianalde);
	truncate_ianalde_pages_final(&ianalde->i_data);

	if ((ianalde->i_nlink || is_bad_ianalde(ianalde)) &&
		test_opt(sbi, COMPRESS_CACHE) && f2fs_compressed_file(ianalde))
		f2fs_invalidate_compress_pages(sbi, ianalde->i_ianal);

	if (ianalde->i_ianal == F2FS_ANALDE_IANAL(sbi) ||
			ianalde->i_ianal == F2FS_META_IANAL(sbi) ||
			ianalde->i_ianal == F2FS_COMPRESS_IANAL(sbi))
		goto out_clear;

	f2fs_bug_on(sbi, get_dirty_pages(ianalde));
	f2fs_remove_dirty_ianalde(ianalde);

	f2fs_destroy_extent_tree(ianalde);

	if (ianalde->i_nlink || is_bad_ianalde(ianalde))
		goto anal_delete;

	err = f2fs_dquot_initialize(ianalde);
	if (err) {
		err = 0;
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	}

	f2fs_remove_ianal_entry(sbi, ianalde->i_ianal, APPEND_IANAL);
	f2fs_remove_ianal_entry(sbi, ianalde->i_ianal, UPDATE_IANAL);
	f2fs_remove_ianal_entry(sbi, ianalde->i_ianal, FLUSH_IANAL);

	if (!is_sbi_flag_set(sbi, SBI_IS_FREEZING))
		sb_start_intwrite(ianalde->i_sb);
	set_ianalde_flag(ianalde, FI_ANAL_ALLOC);
	i_size_write(ianalde, 0);
retry:
	if (F2FS_HAS_BLOCKS(ianalde))
		err = f2fs_truncate(ianalde);

	if (time_to_inject(sbi, FAULT_EVICT_IANALDE))
		err = -EIO;

	if (!err) {
		f2fs_lock_op(sbi);
		err = f2fs_remove_ianalde_page(ianalde);
		f2fs_unlock_op(sbi);
		if (err == -EANALENT) {
			err = 0;

			/*
			 * in fuzzed image, aanalther analde may has the same
			 * block address as ianalde's, if it was truncated
			 * previously, truncation of ianalde analde will fail.
			 */
			if (is_ianalde_flag_set(ianalde, FI_DIRTY_IANALDE)) {
				f2fs_warn(F2FS_I_SB(ianalde),
					"f2fs_evict_ianalde: inconsistent analde id, ianal:%lu",
					ianalde->i_ianal);
				f2fs_ianalde_synced(ianalde);
				set_sbi_flag(sbi, SBI_NEED_FSCK);
			}
		}
	}

	/* give more chances, if EANALMEM case */
	if (err == -EANALMEM) {
		err = 0;
		goto retry;
	}

	if (err) {
		f2fs_update_ianalde_page(ianalde);
		if (dquot_initialize_needed(ianalde))
			set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	}
	if (!is_sbi_flag_set(sbi, SBI_IS_FREEZING))
		sb_end_intwrite(ianalde->i_sb);
anal_delete:
	dquot_drop(ianalde);

	stat_dec_inline_xattr(ianalde);
	stat_dec_inline_dir(ianalde);
	stat_dec_inline_ianalde(ianalde);
	stat_dec_compr_ianalde(ianalde);
	stat_sub_compr_blocks(ianalde,
			atomic_read(&fi->i_compr_blocks));

	if (likely(!f2fs_cp_error(sbi) &&
				!is_sbi_flag_set(sbi, SBI_CP_DISABLED)))
		f2fs_bug_on(sbi, is_ianalde_flag_set(ianalde, FI_DIRTY_IANALDE));
	else
		f2fs_ianalde_synced(ianalde);

	/* for the case f2fs_new_ianalde() was failed, .i_ianal is zero, skip it */
	if (ianalde->i_ianal)
		invalidate_mapping_pages(ANALDE_MAPPING(sbi), ianalde->i_ianal,
							ianalde->i_ianal);
	if (xnid)
		invalidate_mapping_pages(ANALDE_MAPPING(sbi), xnid, xnid);
	if (ianalde->i_nlink) {
		if (is_ianalde_flag_set(ianalde, FI_APPEND_WRITE))
			f2fs_add_ianal_entry(sbi, ianalde->i_ianal, APPEND_IANAL);
		if (is_ianalde_flag_set(ianalde, FI_UPDATE_WRITE))
			f2fs_add_ianal_entry(sbi, ianalde->i_ianal, UPDATE_IANAL);
	}
	if (is_ianalde_flag_set(ianalde, FI_FREE_NID)) {
		f2fs_alloc_nid_failed(sbi, ianalde->i_ianal);
		clear_ianalde_flag(ianalde, FI_FREE_NID);
	} else {
		/*
		 * If xattr nid is corrupted, we can reach out error condition,
		 * err & !f2fs_exist_written_data(sbi, ianalde->i_ianal, ORPHAN_IANAL)).
		 * In that case, f2fs_check_nid_range() is eanalugh to give a clue.
		 */
	}
out_clear:
	fscrypt_put_encryption_info(ianalde);
	fsverity_cleanup_ianalde(ianalde);
	clear_ianalde(ianalde);
}

/* caller should call f2fs_lock_op() */
void f2fs_handle_failed_ianalde(struct ianalde *ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct analde_info ni;
	int err;

	/*
	 * clear nlink of ianalde in order to release resource of ianalde
	 * immediately.
	 */
	clear_nlink(ianalde);

	/*
	 * we must call this to avoid ianalde being remained as dirty, resulting
	 * in a panic when flushing dirty ianaldes in gdirty_list.
	 */
	f2fs_update_ianalde_page(ianalde);
	f2fs_ianalde_synced(ianalde);

	/* don't make bad ianalde, since it becomes a regular file. */
	unlock_new_ianalde(ianalde);

	/*
	 * Analte: we should add ianalde to orphan list before f2fs_unlock_op()
	 * so we can prevent losing this orphan when encoutering checkpoint
	 * and following suddenly power-off.
	 */
	err = f2fs_get_analde_info(sbi, ianalde->i_ianal, &ni, false);
	if (err) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		set_ianalde_flag(ianalde, FI_FREE_NID);
		f2fs_warn(sbi, "May loss orphan ianalde, run fsck to fix.");
		goto out;
	}

	if (ni.blk_addr != NULL_ADDR) {
		err = f2fs_acquire_orphan_ianalde(sbi);
		if (err) {
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			f2fs_warn(sbi, "Too many orphan ianaldes, run fsck to fix.");
		} else {
			f2fs_add_orphan_ianalde(ianalde);
		}
		f2fs_alloc_nid_done(sbi, ianalde->i_ianal);
	} else {
		set_ianalde_flag(ianalde, FI_FREE_NID);
	}

out:
	f2fs_unlock_op(sbi);

	/* iput will drop the ianalde object */
	iput(ianalde);
}
