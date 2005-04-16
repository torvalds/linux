/*
 *  fs/partitions/ultrix.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Re-organised Jul 1999 Russell King
 */

#include "check.h"

int ultrix_partition(struct parsed_partitions *state, struct block_device *bdev)
{
	int i;
	Sector sect;
	unsigned char *data;
	struct ultrix_disklabel {
		s32	pt_magic;	/* magic no. indicating part. info exits */
		s32	pt_valid;	/* set by driver if pt is current */
		struct  pt_info {
			s32		pi_nblocks; /* no. of sectors */
			u32		pi_blkoff;  /* block offset for start */
		} pt_part[8];
	} *label;

#define PT_MAGIC	0x032957	/* Partition magic number */
#define PT_VALID	1		/* Indicates if struct is valid */

	data = read_dev_sector(bdev, (16384 - sizeof(*label))/512, &sect);
	if (!data)
		return -1;
	
	label = (struct ultrix_disklabel *)(data + 512 - sizeof(*label));

	if (label->pt_magic == PT_MAGIC && label->pt_valid == PT_VALID) {
		for (i=0; i<8; i++)
			if (label->pt_part[i].pi_nblocks)
				put_partition(state, i+1, 
					      label->pt_part[i].pi_blkoff,
					      label->pt_part[i].pi_nblocks);
		put_dev_sector(sect);
		printk ("\n");
		return 1;
	} else {
		put_dev_sector(sect);
		return 0;
	}
}
