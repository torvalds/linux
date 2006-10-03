/*
 * linux/fs/hfsplus/part_tbl.c
 *
 * Copyright (C) 1996-1997  Paul H. Hargrove
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * Original code to handle the new style Mac partition table based on
 * a patch contributed by Holger Schemel (aeglos@valinor.owl.de).
 *
 * In function preconditions the term "valid" applied to a pointer to
 * a structure means that the pointer is non-NULL and the structure it
 * points to has all fields initialized to consistent values.
 *
 */

#include "hfsplus_fs.h"

/* offsets to various blocks */
#define HFS_DD_BLK		0 /* Driver Descriptor block */
#define HFS_PMAP_BLK		1 /* First block of partition map */
#define HFS_MDB_BLK		2 /* Block (w/i partition) of MDB */

/* magic numbers for various disk blocks */
#define HFS_DRVR_DESC_MAGIC	0x4552 /* "ER": driver descriptor map */
#define HFS_OLD_PMAP_MAGIC	0x5453 /* "TS": old-type partition map */
#define HFS_NEW_PMAP_MAGIC	0x504D /* "PM": new-type partition map */
#define HFS_SUPER_MAGIC		0x4244 /* "BD": HFS MDB (super block) */
#define HFS_MFS_SUPER_MAGIC	0xD2D7 /* MFS MDB (super block) */

/*
 * The new style Mac partition map
 *
 * For each partition on the media there is a physical block (512-byte
 * block) containing one of these structures.  These blocks are
 * contiguous starting at block 1.
 */
struct new_pmap {
	__be16	pmSig;		/* signature */
	__be16	reSigPad;	/* padding */
	__be32	pmMapBlkCnt;	/* partition blocks count */
	__be32	pmPyPartStart;	/* physical block start of partition */
	__be32	pmPartBlkCnt;	/* physical block count of partition */
	u8	pmPartName[32];	/* (null terminated?) string
				   giving the name of this
				   partition */
	u8	pmPartType[32];	/* (null terminated?) string
				   giving the type of this
				   partition */
	/* a bunch more stuff we don't need */
} __packed;

/*
 * The old style Mac partition map
 *
 * The partition map consists for a 2-byte signature followed by an
 * array of these structures.  The map is terminated with an all-zero
 * one of these.
 */
struct old_pmap {
	__be16		pdSig;	/* Signature bytes */
	struct 	old_pmap_entry {
		__be32	pdStart;
		__be32	pdSize;
		__be32	pdFSID;
	}	pdEntry[42];
} __packed;

/*
 * hfs_part_find()
 *
 * Parse the partition map looking for the
 * start and length of the 'part'th HFS partition.
 */
int hfs_part_find(struct super_block *sb,
		  sector_t *part_start, sector_t *part_size)
{
	struct buffer_head *bh;
	__be16 *data;
	int i, size, res;

	res = -ENOENT;
	bh = sb_bread512(sb, *part_start + HFS_PMAP_BLK, data);
	if (!bh)
		return -EIO;

	switch (be16_to_cpu(*data)) {
	case HFS_OLD_PMAP_MAGIC:
	  {
		struct old_pmap *pm;
		struct old_pmap_entry *p;

		pm = (struct old_pmap *)bh->b_data;
		p = pm->pdEntry;
		size = 42;
		for (i = 0; i < size; p++, i++) {
			if (p->pdStart && p->pdSize &&
			    p->pdFSID == cpu_to_be32(0x54465331)/*"TFS1"*/ &&
			    (HFSPLUS_SB(sb).part < 0 || HFSPLUS_SB(sb).part == i)) {
				*part_start += be32_to_cpu(p->pdStart);
				*part_size = be32_to_cpu(p->pdSize);
				res = 0;
			}
		}
		break;
	  }
	case HFS_NEW_PMAP_MAGIC:
	  {
		struct new_pmap *pm;

		pm = (struct new_pmap *)bh->b_data;
		size = be32_to_cpu(pm->pmMapBlkCnt);
		for (i = 0; i < size;) {
			if (!memcmp(pm->pmPartType,"Apple_HFS", 9) &&
			    (HFSPLUS_SB(sb).part < 0 || HFSPLUS_SB(sb).part == i)) {
				*part_start += be32_to_cpu(pm->pmPyPartStart);
				*part_size = be32_to_cpu(pm->pmPartBlkCnt);
				res = 0;
				break;
			}
			brelse(bh);
			bh = sb_bread512(sb, *part_start + HFS_PMAP_BLK + ++i, pm);
			if (!bh)
				return -EIO;
			if (pm->pmSig != cpu_to_be16(HFS_NEW_PMAP_MAGIC))
				break;
		}
		break;
	  }
	}
	brelse(bh);

	return res;
}
