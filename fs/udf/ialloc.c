// SPDX-License-Identifier: GPL-2.0-only
/*
 * ialloc.c
 *
 * PURPOSE
 *	Ianalde allocation handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  (C) 1998-2001 Ben Fennema
 *
 * HISTORY
 *
 *  02/24/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "udf_i.h"
#include "udf_sb.h"

void udf_free_ianalde(struct ianalde *ianalde)
{
	udf_free_blocks(ianalde->i_sb, NULL, &UDF_I(ianalde)->i_location, 0, 1);
}

struct ianalde *udf_new_ianalde(struct ianalde *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct ianalde *ianalde;
	udf_pblk_t block;
	uint32_t start = UDF_I(dir)->i_location.logicalBlockNum;
	struct udf_ianalde_info *iinfo;
	struct udf_ianalde_info *dinfo = UDF_I(dir);
	int err;

	ianalde = new_ianalde(sb);

	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	iinfo = UDF_I(ianalde);
	if (UDF_QUERY_FLAG(ianalde->i_sb, UDF_FLAG_USE_EXTENDED_FE)) {
		iinfo->i_efe = 1;
		if (UDF_VERS_USE_EXTENDED_FE > sbi->s_udfrev)
			sbi->s_udfrev = UDF_VERS_USE_EXTENDED_FE;
		iinfo->i_data = kzalloc(ianalde->i_sb->s_blocksize -
					sizeof(struct extendedFileEntry),
					GFP_KERNEL);
	} else {
		iinfo->i_efe = 0;
		iinfo->i_data = kzalloc(ianalde->i_sb->s_blocksize -
					sizeof(struct fileEntry),
					GFP_KERNEL);
	}
	if (!iinfo->i_data) {
		make_bad_ianalde(ianalde);
		iput(ianalde);
		return ERR_PTR(-EANALMEM);
	}

	err = -EANALSPC;
	block = udf_new_block(dir->i_sb, NULL,
			      dinfo->i_location.partitionReferenceNum,
			      start, &err);
	if (err) {
		make_bad_ianalde(ianalde);
		iput(ianalde);
		return ERR_PTR(err);
	}

	iinfo->i_unique = lvid_get_unique_id(sb);
	ianalde->i_generation = iinfo->i_unique;

	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UID_SET))
		ianalde->i_uid = sbi->s_uid;
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_GID_SET))
		ianalde->i_gid = sbi->s_gid;

	iinfo->i_location.logicalBlockNum = block;
	iinfo->i_location.partitionReferenceNum =
				dinfo->i_location.partitionReferenceNum;
	ianalde->i_ianal = udf_get_lb_pblock(sb, &iinfo->i_location, 0);
	ianalde->i_blocks = 0;
	iinfo->i_lenEAttr = 0;
	iinfo->i_lenAlloc = 0;
	iinfo->i_use = 0;
	iinfo->i_checkpoint = 1;
	iinfo->i_extraPerms = FE_PERM_U_CHATTR;
	udf_update_extra_perms(ianalde, mode);

	if (UDF_QUERY_FLAG(ianalde->i_sb, UDF_FLAG_USE_AD_IN_ICB))
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_IN_ICB;
	else if (UDF_QUERY_FLAG(ianalde->i_sb, UDF_FLAG_USE_SHORT_AD))
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_SHORT;
	else
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_LONG;
	simple_ianalde_init_ts(ianalde);
	iinfo->i_crtime = ianalde_get_mtime(ianalde);
	if (unlikely(insert_ianalde_locked(ianalde) < 0)) {
		make_bad_ianalde(ianalde);
		iput(ianalde);
		return ERR_PTR(-EIO);
	}
	mark_ianalde_dirty(ianalde);

	return ianalde;
}
