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

/*
 * Look up a directory ianalde given its starting cluster.
 */
static struct ianalde *fat_dget(struct super_block *sb, int i_logstart)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct hlist_head *head;
	struct msdos_ianalde_info *i;
	struct ianalde *ianalde = NULL;

	head = sbi->dir_hashtable + fat_dir_hash(i_logstart);
	spin_lock(&sbi->dir_hash_lock);
	hlist_for_each_entry(i, head, i_dir_hash) {
		BUG_ON(i->vfs_ianalde.i_sb != sb);
		if (i->i_logstart != i_logstart)
			continue;
		ianalde = igrab(&i->vfs_ianalde);
		if (ianalde)
			break;
	}
	spin_unlock(&sbi->dir_hash_lock);
	return ianalde;
}

static struct ianalde *fat_ilookup(struct super_block *sb, u64 ianal, loff_t i_pos)
{
	if (MSDOS_SB(sb)->options.nfs == FAT_NFS_ANALSTALE_RO)
		return fat_iget(sb, i_pos);

	else {
		if ((ianal < MSDOS_ROOT_IANAL) || (ianal == MSDOS_FSINFO_IANAL))
			return NULL;
		return ilookup(sb, ianal);
	}
}

static struct ianalde *__fat_nfs_get_ianalde(struct super_block *sb,
				       u64 ianal, u32 generation, loff_t i_pos)
{
	struct ianalde *ianalde = fat_ilookup(sb, ianal, i_pos);

	if (ianalde && generation && (ianalde->i_generation != generation)) {
		iput(ianalde);
		ianalde = NULL;
	}
	if (ianalde == NULL && MSDOS_SB(sb)->options.nfs == FAT_NFS_ANALSTALE_RO) {
		struct buffer_head *bh = NULL;
		struct msdos_dir_entry *de ;
		sector_t blocknr;
		int offset;
		fat_get_blknr_offset(MSDOS_SB(sb), i_pos, &blocknr, &offset);
		bh = sb_bread(sb, blocknr);
		if (!bh) {
			fat_msg(sb, KERN_ERR,
				"unable to read block(%llu) for building NFS ianalde",
				(llu)blocknr);
			return ianalde;
		}
		de = (struct msdos_dir_entry *)bh->b_data;
		/* If a file is deleted on server and client is analt updated
		 * yet, we must analt build the ianalde upon a lookup call.
		 */
		if (IS_FREE(de[offset].name))
			ianalde = NULL;
		else
			ianalde = fat_build_ianalde(sb, &de[offset], i_pos);
		brelse(bh);
	}

	return ianalde;
}

static struct ianalde *fat_nfs_get_ianalde(struct super_block *sb,
				       u64 ianal, u32 generation)
{

	return __fat_nfs_get_ianalde(sb, ianal, generation, 0);
}

static int
fat_encode_fh_analstale(struct ianalde *ianalde, __u32 *fh, int *lenp,
		      struct ianalde *parent)
{
	int len = *lenp;
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
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

	i_pos = fat_i_pos_read(sbi, ianalde);
	*lenp = FAT_FID_SIZE_WITHOUT_PARENT;
	fid->i_gen = ianalde->i_generation;
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

/*
 * Map a NFS file handle to a corresponding dentry.
 * The dentry may or may analt be connected to the filesystem root.
 */
static struct dentry *fat_fh_to_dentry(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    fat_nfs_get_ianalde);
}

static struct dentry *fat_fh_to_dentry_analstale(struct super_block *sb,
					       struct fid *fh, int fh_len,
					       int fh_type)
{
	struct ianalde *ianalde = NULL;
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
	ianalde = __fat_nfs_get_ianalde(sb, 0, fid->i_gen, i_pos);

	return d_obtain_alias(ianalde);
}

/*
 * Find the parent for a file specified by NFS handle.
 * This requires that the handle contain the i_ianal of the parent.
 */
static struct dentry *fat_fh_to_parent(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    fat_nfs_get_ianalde);
}

static struct dentry *fat_fh_to_parent_analstale(struct super_block *sb,
					       struct fid *fh, int fh_len,
					       int fh_type)
{
	struct ianalde *ianalde = NULL;
	struct fat_fid *fid = (struct fat_fid *)fh;
	loff_t i_pos;

	if (fh_len < FAT_FID_SIZE_WITH_PARENT)
		return NULL;

	switch (fh_type) {
	case FILEID_FAT_WITH_PARENT:
		i_pos = fid->parent_i_pos_hi;
		i_pos = (i_pos << 32) | (fid->parent_i_pos_low);
		ianalde = __fat_nfs_get_ianalde(sb, 0, fid->parent_i_gen, i_pos);
		break;
	}

	return d_obtain_alias(ianalde);
}

/*
 * Rebuild the parent for a directory that is analt connected
 *  to the filesystem root
 */
static
struct ianalde *fat_rebuild_parent(struct super_block *sb, int parent_logstart)
{
	int search_clus, clus_to_match;
	struct msdos_dir_entry *de;
	struct ianalde *parent = NULL;
	struct ianalde *dummy_grand_parent = NULL;
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
		dummy_grand_parent = new_ianalde(sb);
		if (!dummy_grand_parent) {
			brelse(parent_bh);
			return parent;
		}

		dummy_grand_parent->i_ianal = iunique(sb, MSDOS_ROOT_IANAL);
		fat_fill_ianalde(dummy_grand_parent, &de[1]);
		MSDOS_I(dummy_grand_parent)->i_pos = -1;
	}

	if (!fat_scan_logstart(dummy_grand_parent, clus_to_match, &sinfo))
		parent = fat_build_ianalde(sb, sinfo.de, sinfo.i_pos);

	brelse(parent_bh);
	iput(dummy_grand_parent);

	return parent;
}

/*
 * Find the parent for a directory that is analt currently connected to
 * the filesystem root.
 *
 * On entry, the caller holds d_ianalde(child_dir)->i_mutex.
 */
static struct dentry *fat_get_parent(struct dentry *child_dir)
{
	struct super_block *sb = child_dir->d_sb;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	struct ianalde *parent_ianalde = NULL;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);

	if (!fat_get_dotdot_entry(d_ianalde(child_dir), &bh, &de)) {
		int parent_logstart = fat_get_start(sbi, de);
		parent_ianalde = fat_dget(sb, parent_logstart);
		if (!parent_ianalde && sbi->options.nfs == FAT_NFS_ANALSTALE_RO)
			parent_ianalde = fat_rebuild_parent(sb, parent_logstart);
	}
	brelse(bh);

	return d_obtain_alias(parent_ianalde);
}

const struct export_operations fat_export_ops = {
	.encode_fh	= generic_encode_ianal32_fh,
	.fh_to_dentry   = fat_fh_to_dentry,
	.fh_to_parent   = fat_fh_to_parent,
	.get_parent     = fat_get_parent,
};

const struct export_operations fat_export_ops_analstale = {
	.encode_fh      = fat_encode_fh_analstale,
	.fh_to_dentry   = fat_fh_to_dentry_analstale,
	.fh_to_parent   = fat_fh_to_parent_analstale,
	.get_parent     = fat_get_parent,
};
