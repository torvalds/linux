/*
 *  fs/partitions/sun.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include "check.h"
#include "sun.h"

int sun_partition(struct parsed_partitions *state, struct block_device *bdev)
{
	int i;
	__be16 csum;
	int slot = 1;
	__be16 *ush;
	Sector sect;
	struct sun_disklabel {
		unsigned char info[128];   /* Informative text string */
		unsigned char spare0[14];
		struct sun_info {
			unsigned char spare1;
			unsigned char id;
			unsigned char spare2;
			unsigned char flags;
		} infos[8];
		unsigned char spare[246];  /* Boot information etc. */
		__be16 rspeed;     /* Disk rotational speed */
		__be16 pcylcount;  /* Physical cylinder count */
		__be16 sparecyl;   /* extra sects per cylinder */
		unsigned char spare2[4];   /* More magic... */
		__be16 ilfact;     /* Interleave factor */
		__be16 ncyl;       /* Data cylinder count */
		__be16 nacyl;      /* Alt. cylinder count */
		__be16 ntrks;      /* Tracks per cylinder */
		__be16 nsect;      /* Sectors per track */
		unsigned char spare3[4];   /* Even more magic... */
		struct sun_partition {
			__be32 start_cylinder;
			__be32 num_sectors;
		} partitions[8];
		__be16 magic;      /* Magic number */
		__be16 csum;       /* Label xor'd checksum */
	} * label;		
	struct sun_partition *p;
	unsigned long spc;
	char b[BDEVNAME_SIZE];

	label = (struct sun_disklabel *)read_dev_sector(bdev, 0, &sect);
	if (!label)
		return -1;

	p = label->partitions;
	if (be16_to_cpu(label->magic) != SUN_LABEL_MAGIC) {
/*		printk(KERN_INFO "Dev %s Sun disklabel: bad magic %04x\n",
		       bdevname(bdev, b), be16_to_cpu(label->magic)); */
		put_dev_sector(sect);
		return 0;
	}
	/* Look at the checksum */
	ush = ((__be16 *) (label+1)) - 1;
	for (csum = 0; ush >= ((__be16 *) label);)
		csum ^= *ush--;
	if (csum) {
		printk("Dev %s Sun disklabel: Csum bad, label corrupted\n",
		       bdevname(bdev, b));
		put_dev_sector(sect);
		return 0;
	}

	/* All Sun disks have 8 partition entries */
	spc = be16_to_cpu(label->ntrks) * be16_to_cpu(label->nsect);
	for (i = 0; i < 8; i++, p++) {
		unsigned long st_sector;
		unsigned int num_sectors;

		st_sector = be32_to_cpu(p->start_cylinder) * spc;
		num_sectors = be32_to_cpu(p->num_sectors);
		if (num_sectors) {
			put_partition(state, slot, st_sector, num_sectors);
			state->parts[slot].flags = 0;
			if (label->infos[i].id == LINUX_RAID_PARTITION)
				state->parts[slot].flags |= ADDPART_FLAG_RAID;
			if (label->infos[i].id == SUN_WHOLE_DISK)
				state->parts[slot].flags |= ADDPART_FLAG_WHOLEDISK;
		}
		slot++;
	}
	printk("\n");
	put_dev_sector(sect);
	return 1;
}
