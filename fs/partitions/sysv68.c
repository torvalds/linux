/*
 *  fs/partitions/sysv68.c
 *
 *  Copyright (C) 2007 Philippe De Muyter <phdm@macqel.be>
 */

#include "check.h"
#include "sysv68.h"

/*
 *	Volume ID structure: on first 256-bytes sector of disk
 */

struct volumeid {
	u8	vid_unused[248];
	u8	vid_mac[8];	/* ASCII string "MOTOROLA" */
};

/*
 *	config block: second 256-bytes sector on disk
 */

struct dkconfig {
	u8	ios_unused0[128];
	__be32	ios_slcblk;	/* Slice table block number */
	__be16	ios_slccnt;	/* Number of entries in slice table */
	u8	ios_unused1[122];
};

/*
 *	combined volumeid and dkconfig block
 */

struct dkblk0 {
	struct volumeid dk_vid;
	struct dkconfig dk_ios;
};

/*
 *	Slice Table Structure
 */

struct slice {
	__be32	nblocks;		/* slice size (in blocks) */
	__be32	blkoff;			/* block offset of slice */
};


int sysv68_partition(struct parsed_partitions *state)
{
	int i, slices;
	int slot = 1;
	Sector sect;
	unsigned char *data;
	struct dkblk0 *b;
	struct slice *slice;

	data = read_part_sector(state, 0, &sect);
	if (!data)
		return -1;

	b = (struct dkblk0 *)data;
	if (memcmp(b->dk_vid.vid_mac, "MOTOROLA", sizeof(b->dk_vid.vid_mac))) {
		put_dev_sector(sect);
		return 0;
	}
	slices = be16_to_cpu(b->dk_ios.ios_slccnt);
	i = be32_to_cpu(b->dk_ios.ios_slcblk);
	put_dev_sector(sect);

	data = read_part_sector(state, i, &sect);
	if (!data)
		return -1;

	slices -= 1; /* last slice is the whole disk */
	printk("sysV68: %s(s%u)", state->name, slices);
	slice = (struct slice *)data;
	for (i = 0; i < slices; i++, slice++) {
		if (slot == state->limit)
			break;
		if (be32_to_cpu(slice->nblocks)) {
			put_partition(state, slot,
				be32_to_cpu(slice->blkoff),
				be32_to_cpu(slice->nblocks));
			printk("(s%u)", i);
		}
		slot++;
	}
	printk("\n");
	put_dev_sector(sect);
	return 1;
}
