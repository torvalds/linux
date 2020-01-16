/*
 * ialloc.c
 *
 * PURPOSE
 *	Iyesde allocation handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
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

void udf_free_iyesde(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct logicalVolIntegrityDescImpUse *lvidiu = udf_sb_lvidiu(sb);

	if (lvidiu) {
		mutex_lock(&sbi->s_alloc_mutex);
		if (S_ISDIR(iyesde->i_mode))
			le32_add_cpu(&lvidiu->numDirs, -1);
		else
			le32_add_cpu(&lvidiu->numFiles, -1);
		udf_updated_lvid(sb);
		mutex_unlock(&sbi->s_alloc_mutex);
	}

	udf_free_blocks(sb, NULL, &UDF_I(iyesde)->i_location, 0, 1);
}

struct iyesde *udf_new_iyesde(struct iyesde *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct udf_sb_info *sbi = UDF_SB(sb);
	struct iyesde *iyesde;
	udf_pblk_t block;
	uint32_t start = UDF_I(dir)->i_location.logicalBlockNum;
	struct udf_iyesde_info *iinfo;
	struct udf_iyesde_info *dinfo = UDF_I(dir);
	struct logicalVolIntegrityDescImpUse *lvidiu;
	int err;

	iyesde = new_iyesde(sb);

	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	iinfo = UDF_I(iyesde);
	if (UDF_QUERY_FLAG(iyesde->i_sb, UDF_FLAG_USE_EXTENDED_FE)) {
		iinfo->i_efe = 1;
		if (UDF_VERS_USE_EXTENDED_FE > sbi->s_udfrev)
			sbi->s_udfrev = UDF_VERS_USE_EXTENDED_FE;
		iinfo->i_ext.i_data = kzalloc(iyesde->i_sb->s_blocksize -
					    sizeof(struct extendedFileEntry),
					    GFP_KERNEL);
	} else {
		iinfo->i_efe = 0;
		iinfo->i_ext.i_data = kzalloc(iyesde->i_sb->s_blocksize -
					    sizeof(struct fileEntry),
					    GFP_KERNEL);
	}
	if (!iinfo->i_ext.i_data) {
		iput(iyesde);
		return ERR_PTR(-ENOMEM);
	}

	err = -ENOSPC;
	block = udf_new_block(dir->i_sb, NULL,
			      dinfo->i_location.partitionReferenceNum,
			      start, &err);
	if (err) {
		iput(iyesde);
		return ERR_PTR(err);
	}

	lvidiu = udf_sb_lvidiu(sb);
	if (lvidiu) {
		iinfo->i_unique = lvid_get_unique_id(sb);
		iyesde->i_generation = iinfo->i_unique;
		mutex_lock(&sbi->s_alloc_mutex);
		if (S_ISDIR(mode))
			le32_add_cpu(&lvidiu->numDirs, 1);
		else
			le32_add_cpu(&lvidiu->numFiles, 1);
		udf_updated_lvid(sb);
		mutex_unlock(&sbi->s_alloc_mutex);
	}

	iyesde_init_owner(iyesde, dir, mode);
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UID_SET))
		iyesde->i_uid = sbi->s_uid;
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_GID_SET))
		iyesde->i_gid = sbi->s_gid;

	iinfo->i_location.logicalBlockNum = block;
	iinfo->i_location.partitionReferenceNum =
				dinfo->i_location.partitionReferenceNum;
	iyesde->i_iyes = udf_get_lb_pblock(sb, &iinfo->i_location, 0);
	iyesde->i_blocks = 0;
	iinfo->i_lenEAttr = 0;
	iinfo->i_lenAlloc = 0;
	iinfo->i_use = 0;
	iinfo->i_checkpoint = 1;
	iinfo->i_extraPerms = FE_PERM_U_CHATTR;
	udf_update_extra_perms(iyesde, mode);

	if (UDF_QUERY_FLAG(iyesde->i_sb, UDF_FLAG_USE_AD_IN_ICB))
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_IN_ICB;
	else if (UDF_QUERY_FLAG(iyesde->i_sb, UDF_FLAG_USE_SHORT_AD))
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_SHORT;
	else
		iinfo->i_alloc_type = ICBTAG_FLAG_AD_LONG;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	iinfo->i_crtime = iyesde->i_mtime;
	if (unlikely(insert_iyesde_locked(iyesde) < 0)) {
		make_bad_iyesde(iyesde);
		iput(iyesde);
		return ERR_PTR(-EIO);
	}
	mark_iyesde_dirty(iyesde);

	return iyesde;
}
