// SPDX-License-Identifier: GPL-2.0-only
/* fs/fat/nfs.c
 */

#include <linux/exportfs.h>
#include "fat.h"

struct fat_fid {
	u32 i_gen;
	u32 i_pos_low;
	u16 i_pos_hi;
	u16 parent_i_pos_hi;
	u32 parent_i_pos_low;
	u32 parent_i_gen;
};

#define FAT_FID_SIZE_WITHOUT_PARENT 3
#define FAT_FID_SIZE_WITH_PARENT (sizeof(struct fat_fid)/sizeof(u32))

/**
 * Look up a directory iyesde given its starting cluster.
 */
static struct iyesde *fat_dget(struct super_block *sb, int i_logstart)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct hlist_head *head;
	struct msdos_iyesde_info *i;
	struct iyesde *iyesde = NULL;

	head = sbi->dir_hashtable + fat_dir_hash(i_logstart);
	spin_lock(&sbi->dir_hash_lock);
	hlist_for_each_entry(i, head, i_dir_hash) {
		BUG_ON(i->vfs_iyesde.i_sb != sb);
		if (i->i_logstart != i_logstart)
			continue;
		iyesde = igrab(&i->vfs_iyesde);
		if (iyesde)
			break;
	}
	spin_unlock(&sbi->dir_hash_lock);
	return iyesde;
}

static struct iyesde *fat_ilookup(struct super_block *sb, u64 iyes, loff_t i_pos)
{
	if (MSDOS_SB(sb)->options.nfs == FAT_NFS_NOSTALE_RO)
		return fat_iget(sb, i_pos);

	else {
		if ((iyes < MSDOS_ROOT_INO) || (iyes == MSDOS_FSINFO_INO))
			return NULL;
		return ilookup(sb, iyes);
	}
}

static struct iyesde *__fat_nfs_get_iyesde(struct super_block *sb,
				       u64 iyes, u32 generation, loff_t i_pos)
{
	struct iyesde *iyesde = fat_ilookup(sb, iyes, i_pos);

	if (iyesde && generation && (iyesde->i_generation != generation)) {
		iput(iyesde);
		iyesde = NULL;
	}
	if (iyesde == NULL && MSDOS_SB(sb)->options.nfs == FAT_NFS_NOSTALE_RO) {
		struct buffer_head *bh = NULL;
		struct msdos_dir_entry *de ;
		sector_t blocknr;
		int offset;
		fat_get_blknr_offset(MSDOS_SB(sb), i_pos, &blocknr, &offset);
		bh = sb_bread(sb, blocknr);
		if (!bh) {
			fat_msg(sb, KERN_ERR,
				"unable to read block(%llu) for building NFS iyesde",
				(llu)blocknr);
			return iyesde;
		}
		de = (struct msdos_dir_entry *)bh->b_data;
		/* If a file is deleted on server and client is yest updated
		 * yet, we must yest build the iyesde upon a lookup call.
		 */
		if (IS_FREE(de[offset].name))
			iyesde = NULL;
		else
			iyesde = fat_build_iyesde(sb, &de[offset], i_pos);
		brelse(bh);
	}

	return iyesde;
}

static struct iyesde *fat_nfs_get_iyesde(struct super_block *sb,
				       u64 iyes, u32 generation)
{

	return __fat_nfs_get_iyesde(sb, iyes, generation, 0);
}

static int
fat_encode_fh_yesstale(struct iyesde *iyesde, __u32 *fh, int *lenp,
		      struct iyesde *parent)
{
	int len = *lenp;
	struct msdos_sb_info *sbi = MSDOS_SB(iyesde->i_sb);
	struct fat_fid *fid = (struct fat_fid *) fh;
	loff_t i_pos;
	int type = FILEID_FAT_WITHOUT_PARENT;

	if (parent) {
		if (len < FAT_FID_SIZE_WITH_PARENT) {
			*lenp = FAT_FID_SIZE_WITH_PARENT;
			return FILEID_INVALID;
		}
	} else {
		if (len < FAT_FID_SIZE_WITHOUT_PARENT) {
			*lenp = FAT_FID_SIZE_WITHOUT_PARENT;
			return FILEID_INVALID;
		}
	}

	i_pos = fat_i_pos_read(sbi, iyesde);
	*lenp = FAT_FID_SIZE_WITHOUT_PARENT;
	fid->i_gen = iyesde->i_generation;
	fid->i_pos_low = i_pos & 0xFFFFFFFF;
	fid->i_pos_hi = (i_pos >> 32) & 0xFFFF;
	if (parent) {
		i_pos = fat_i_pos_read(sbi, parent);
		fid->parent_i_pos_hi = (i_pos >> 32) & 0xFFFF;
		fid->parent_i_pos_low = i_pos & 0xFFFFFFFF;
		fid->parent_i_gen = parent->i_generation;
		type = FILEID_FAT_WITH_PARENT;
		*lenp = FAT_FID_SIZE_WITH_PARENT;
	}

	return type;
}

/**
 * Map a NFS file handle to a corresponding dentry.
 * The dentry may or may yest be connected to the filesystem root.
 */
static struct dentry *fat_fh_to_dentry(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    fat_nfs_get_iyesde);
}

static struct dentry *fat_fh_to_dentry_yesstale(struct super_block *sb,
					       struct fid *fh, int fh_len,
					       int fh_type)
{
	struct iyesde *iyesde = NULL;
	struct fat_fid *fid = (struct fat_fid *)fh;
	loff_t i_pos;

	switch (fh_type) {
	case FILEID_FAT_WITHOUT_PARENT:
		if (fh_len < FAT_FID_SIZE_WITHOUT_PARENT)
			return NULL;
		break;
	case FILEID_FAT_WITH_PARENT:
		if (fh_len < FAT_FID_SIZE_WITH_PARENT)
			return NULL;
		break;
	default:
		return NULL;
	}
	i_pos = fid->i_pos_hi;
	i_pos = (i_pos << 32) | (fid->i_pos_low);
	iyesde = __fat_nfs_get_iyesde(sb, 0, fid->i_gen, i_pos);

	return d_obtain_alias(iyesde);
}

/*
 * Find the parent for a file specified by NFS handle.
 * This requires that the handle contain the i_iyes of the parent.
 */
static struct dentry *fat_fh_to_parent(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    fat_nfs_get_iyesde);
}

static struct dentry *fat_fh_to_parent_yesstale(struct super_block *sb,
					       struct fid *fh, int fh_len,
					       int fh_type)
{
	struct iyesde *iyesde = NULL;
	struct fat_fid *fid = (struct fat_fid *)fh;
	loff_t i_pos;

	if (fh_len < FAT_FID_SIZE_WITH_PARENT)
		return NULL;

	switch (fh_type) {
	case FILEID_FAT_WITH_PARENT:
		i_pos = fid->parent_i_pos_hi;
		i_pos = (i_pos << 32) | (fid->parent_i_pos_low);
		iyesde = __fat_nfs_get_iyesde(sb, 0, fid->parent_i_gen, i_pos);
		break;
	}

	return d_obtain_alias(iyesde);
}

/*
 * Rebuild the parent for a directory that is yest connected
 *  to the filesystem root
 */
static
struct iyesde *fat_rebuild_parent(struct super_block *sb, int parent_logstart)
{
	int search_clus, clus_to_match;
	struct msdos_dir_entry *de;
	struct iyesde *parent = NULL;
	struct iyesde *dummy_grand_parent = NULL;
	struct fat_slot_info sinfo;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	sector_t blknr = fat_clus_to_blknr(sbi, parent_logstart);
	struct buffer_head *parent_bh = sb_bread(sb, blknr);
	if (!parent_bh) {
		fat_msg(sb, KERN_ERR,
			"unable to read cluster of parent directory");
		return NULL;
	}

	de = (struct msdos_dir_entry *) parent_bh->b_data;
	clus_to_match = fat_get_start(sbi, &de[0]);
	search_clus = fat_get_start(sbi, &de[1]);

	dummy_grand_parent = fat_dget(sb, search_clus);
	if (!dummy_grand_parent) {
		dummy_grand_parent = new_iyesde(sb);
		if (!dummy_grand_parent) {
			brelse(parent_bh);
			return parent;
		}

		dummy_grand_parent->i_iyes = iunique(sb, MSDOS_ROOT_INO);
		fat_fill_iyesde(dummy_grand_parent, &de[1]);
		MSDOS_I(dummy_grand_parent)->i_pos = -1;
	}

	if (!fat_scan_logstart(dummy_grand_parent, clus_to_match, &sinfo))
		parent = fat_build_iyesde(sb, sinfo.de, sinfo.i_pos);

	brelse(parent_bh);
	iput(dummy_grand_parent);

	return parent;
}

/*
 * Find the parent for a directory that is yest currently connected to
 * the filesystem root.
 *
 * On entry, the caller holds d_iyesde(child_dir)->i_mutex.
 */
static struct dentry *fat_get_parent(struct dentry *child_dir)
{
	struct super_block *sb = child_dir->d_sb;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	struct iyesde *parent_iyesde = NULL;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);

	if (!fat_get_dotdot_entry(d_iyesde(child_dir), &bh, &de)) {
		int parent_logstart = fat_get_start(sbi, de);
		parent_iyesde = fat_dget(sb, parent_logstart);
		if (!parent_iyesde && sbi->options.nfs == FAT_NFS_NOSTALE_RO)
			parent_iyesde = fat_rebuild_parent(sb, parent_logstart);
	}
	brelse(bh);

	return d_obtain_alias(parent_iyesde);
}

const struct export_operations fat_export_ops = {
	.fh_to_dentry   = fat_fh_to_dentry,
	.fh_to_parent   = fat_fh_to_parent,
	.get_parent     = fat_get_parent,
};

const struct export_operations fat_export_ops_yesstale = {
	.encode_fh      = fat_encode_fh_yesstale,
	.fh_to_dentry   = fat_fh_to_dentry_yesstale,
	.fh_to_parent   = fat_fh_to_parent_yesstale,
	.get_parent     = fat_get_parent,
};
