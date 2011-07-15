/*
 *  fs/partitions/osf.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include "check.h"
#include "osf.h"

int osf_partition(struct parsed_partitions *state, struct block_device *bdev)
{
	int i;
	int slot = 1;
	Sector sect;
	unsigned char *data;
	struct disklabel {
		__le32 d_magic;
		__le16 d_type,d_subtype;
		u8 d_typename[16];
		u8 d_packname[16];
		__le32 d_secsize;
		__le32 d_nsectors;
		__le32 d_ntracks;
		__le32 d_ncylinders;
		__le32 d_secpercyl;
		__le32 d_secprtunit;
		__le16 d_sparespertrack;
		__le16 d_sparespercyl;
		__le32 d_acylinders;
		__le16 d_rpm, d_interleave, d_trackskew, d_cylskew;
		__le32 d_headswitch, d_trkseek, d_flags;
		__le32 d_drivedata[5];
		__le32 d_spare[5];
		__le32 d_magic2;
		__le16 d_checksum;
		__le16 d_npartitions;
		__le32 d_bbsize, d_sbsize;
		struct d_partition {
			__le32 p_size;
			__le32 p_offset;
			__le32 p_fsize;
			u8  p_fstype;
			u8  p_frag;
			__le16 p_cpg;
		} d_partitions[8];
	} * label;
	struct d_partition * partition;

	data = read_dev_sector(bdev, 0, &sect);
	if (!data)
		return -1;

	label = (struct disklabel *) (data+64);
	partition = label->d_partitions;
	if (le32_to_cpu(label->d_magic) != DISKLABELMAGIC) {
		put_dev_sector(sect);
		return 0;
	}
	if (le32_to_cpu(label->d_magic2) != DISKLABELMAGIC) {
		put_dev_sector(sect);
		return 0;
	}
	for (i = 0 ; i < le16_to_cpu(label->d_npartitions); i++, partition++) {
		if (slot == state->limit)
		        break;
		if (le32_to_cpu(partition->p_size))
			put_partition(state, slot,
				le32_to_cpu(partition->p_offset),
				le32_to_cpu(partition->p_size));
		slot++;
	}
	printk("\n");
	put_dev_sector(sect);
	return 1;
}
