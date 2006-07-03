/*
 *  linux/fs/partitions/acorn.c
 *
 *  Copyright (c) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Scan ADFS partitions on hard disk drives.  Unfortunately, there
 *  isn't a standard for partitioning drives on Acorn machines, so
 *  every single manufacturer of SCSI and IDE cards created their own
 *  method.
 */
#include <linux/buffer_head.h>
#include <linux/adfs_fs.h>

#include "check.h"
#include "acorn.h"

/*
 * Partition types. (Oh for reusability)
 */
#define PARTITION_RISCIX_MFM	1
#define PARTITION_RISCIX_SCSI	2
#define PARTITION_LINUX		9

static struct adfs_discrecord *
adfs_partition(struct parsed_partitions *state, char *name, char *data,
	       unsigned long first_sector, int slot)
{
	struct adfs_discrecord *dr;
	unsigned int nr_sects;

	if (adfs_checkbblk(data))
		return NULL;

	dr = (struct adfs_discrecord *)(data + 0x1c0);

	if (dr->disc_size == 0 && dr->disc_size_high == 0)
		return NULL;

	nr_sects = (le32_to_cpu(dr->disc_size_high) << 23) |
		   (le32_to_cpu(dr->disc_size) >> 9);

	if (name)
		printk(" [%s]", name);
	put_partition(state, slot, first_sector, nr_sects);
	return dr;
}

#ifdef CONFIG_ACORN_PARTITION_RISCIX

struct riscix_part {
	__le32	start;
	__le32	length;
	__le32	one;
	char	name[16];
};

struct riscix_record {
	__le32	magic;
#define RISCIX_MAGIC	cpu_to_le32(0x4a657320)
	__le32	date;
	struct riscix_part part[8];
};

static int
riscix_partition(struct parsed_partitions *state, struct block_device *bdev,
		unsigned long first_sect, int slot, unsigned long nr_sects)
{
	Sector sect;
	struct riscix_record *rr;
	
	rr = (struct riscix_record *)read_dev_sector(bdev, first_sect, &sect);
	if (!rr)
		return -1;

	printk(" [RISCiX]");


	if (rr->magic == RISCIX_MAGIC) {
		unsigned long size = nr_sects > 2 ? 2 : nr_sects;
		int part;

		printk(" <");

		put_partition(state, slot++, first_sect, size);
		for (part = 0; part < 8; part++) {
			if (rr->part[part].one &&
			    memcmp(rr->part[part].name, "All\0", 4)) {
				put_partition(state, slot++,
					le32_to_cpu(rr->part[part].start),
					le32_to_cpu(rr->part[part].length));
				printk("(%s)", rr->part[part].name);
			}
		}

		printk(" >\n");
	} else {
		put_partition(state, slot++, first_sect, nr_sects);
	}

	put_dev_sector(sect);
	return slot;
}
#endif

#define LINUX_NATIVE_MAGIC 0xdeafa1de
#define LINUX_SWAP_MAGIC   0xdeafab1e

struct linux_part {
	__le32 magic;
	__le32 start_sect;
	__le32 nr_sects;
};

static int
linux_partition(struct parsed_partitions *state, struct block_device *bdev,
		unsigned long first_sect, int slot, unsigned long nr_sects)
{
	Sector sect;
	struct linux_part *linuxp;
	unsigned long size = nr_sects > 2 ? 2 : nr_sects;

	printk(" [Linux]");

	put_partition(state, slot++, first_sect, size);

	linuxp = (struct linux_part *)read_dev_sector(bdev, first_sect, &sect);
	if (!linuxp)
		return -1;

	printk(" <");
	while (linuxp->magic == cpu_to_le32(LINUX_NATIVE_MAGIC) ||
	       linuxp->magic == cpu_to_le32(LINUX_SWAP_MAGIC)) {
		if (slot == state->limit)
			break;
		put_partition(state, slot++, first_sect +
				 le32_to_cpu(linuxp->start_sect),
				 le32_to_cpu(linuxp->nr_sects));
		linuxp ++;
	}
	printk(" >");

	put_dev_sector(sect);
	return slot;
}

#ifdef CONFIG_ACORN_PARTITION_CUMANA
int
adfspart_check_CUMANA(struct parsed_partitions *state, struct block_device *bdev)
{
	unsigned long first_sector = 0;
	unsigned int start_blk = 0;
	Sector sect;
	unsigned char *data;
	char *name = "CUMANA/ADFS";
	int first = 1;
	int slot = 1;

	/*
	 * Try Cumana style partitions - sector 6 contains ADFS boot block
	 * with pointer to next 'drive'.
	 *
	 * There are unknowns in this code - is the 'cylinder number' of the
	 * next partition relative to the start of this one - I'm assuming
	 * it is.
	 *
	 * Also, which ID did Cumana use?
	 *
	 * This is totally unfinished, and will require more work to get it
	 * going. Hence it is totally untested.
	 */
	do {
		struct adfs_discrecord *dr;
		unsigned int nr_sects;

		data = read_dev_sector(bdev, start_blk * 2 + 6, &sect);
		if (!data)
			return -1;

		if (slot == state->limit)
			break;

		dr = adfs_partition(state, name, data, first_sector, slot++);
		if (!dr)
			break;

		name = NULL;

		nr_sects = (data[0x1fd] + (data[0x1fe] << 8)) *
			   (dr->heads + (dr->lowsector & 0x40 ? 1 : 0)) *
			   dr->secspertrack;

		if (!nr_sects)
			break;

		first = 0;
		first_sector += nr_sects;
		start_blk += nr_sects >> (BLOCK_SIZE_BITS - 9);
		nr_sects = 0; /* hmm - should be partition size */

		switch (data[0x1fc] & 15) {
		case 0: /* No partition / ADFS? */
			break;

#ifdef CONFIG_ACORN_PARTITION_RISCIX
		case PARTITION_RISCIX_SCSI:
			/* RISCiX - we don't know how to find the next one. */
			slot = riscix_partition(state, bdev, first_sector,
						 slot, nr_sects);
			break;
#endif

		case PARTITION_LINUX:
			slot = linux_partition(state, bdev, first_sector,
						slot, nr_sects);
			break;
		}
		put_dev_sector(sect);
		if (slot == -1)
			return -1;
	} while (1);
	put_dev_sector(sect);
	return first ? 0 : 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_ADFS
/*
 * Purpose: allocate ADFS partitions.
 *
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 *
 * Returns: -1 on error, 0 for no ADFS boot sector, 1 for ok.
 *
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition on first drive.
 *	    hda2 = non-ADFS partition.
 */
int
adfspart_check_ADFS(struct parsed_partitions *state, struct block_device *bdev)
{
	unsigned long start_sect, nr_sects, sectscyl, heads;
	Sector sect;
	unsigned char *data;
	struct adfs_discrecord *dr;
	unsigned char id;
	int slot = 1;

	data = read_dev_sector(bdev, 6, &sect);
	if (!data)
		return -1;

	dr = adfs_partition(state, "ADFS", data, 0, slot++);
	if (!dr) {
		put_dev_sector(sect);
    		return 0;
	}

	heads = dr->heads + ((dr->lowsector >> 6) & 1);
	sectscyl = dr->secspertrack * heads;
	start_sect = ((data[0x1fe] << 8) + data[0x1fd]) * sectscyl;
	id = data[0x1fc] & 15;
	put_dev_sector(sect);

#ifdef CONFIG_BLK_DEV_MFM
	if (MAJOR(bdev->bd_dev) == MFM_ACORN_MAJOR) {
		extern void xd_set_geometry(struct block_device *,
			unsigned char, unsigned char, unsigned int);
		xd_set_geometry(bdev, dr->secspertrack, heads, 1);
		invalidate_bdev(bdev, 1);
		truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
	}
#endif

	/*
	 * Work out start of non-adfs partition.
	 */
	nr_sects = (bdev->bd_inode->i_size >> 9) - start_sect;

	if (start_sect) {
		switch (id) {
#ifdef CONFIG_ACORN_PARTITION_RISCIX
		case PARTITION_RISCIX_SCSI:
		case PARTITION_RISCIX_MFM:
			slot = riscix_partition(state, bdev, start_sect,
						 slot, nr_sects);
			break;
#endif

		case PARTITION_LINUX:
			slot = linux_partition(state, bdev, start_sect,
						slot, nr_sects);
			break;
		}
	}
	printk("\n");
	return 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_ICS

struct ics_part {
	__le32 start;
	__le32 size;
};

static int adfspart_check_ICSLinux(struct block_device *bdev, unsigned long block)
{
	Sector sect;
	unsigned char *data = read_dev_sector(bdev, block, &sect);
	int result = 0;

	if (data) {
		if (memcmp(data, "LinuxPart", 9) == 0)
			result = 1;
		put_dev_sector(sect);
	}

	return result;
}

/*
 * Check for a valid ICS partition using the checksum.
 */
static inline int valid_ics_sector(const unsigned char *data)
{
	unsigned long sum;
	int i;

	for (i = 0, sum = 0x50617274; i < 508; i++)
		sum += data[i];

	sum -= le32_to_cpu(*(__le32 *)(&data[508]));

	return sum == 0;
}

/*
 * Purpose: allocate ICS partitions.
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 * Returns: -1 on error, 0 for no ICS table, 1 for partitions ok.
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition 0 on first drive.
 *	    hda2 = ADFS partition 1 on first drive.
 *		..etc..
 */
int
adfspart_check_ICS(struct parsed_partitions *state, struct block_device *bdev)
{
	const unsigned char *data;
	const struct ics_part *p;
	int slot;
	Sector sect;

	/*
	 * Try ICS style partitions - sector 0 contains partition info.
	 */
	data = read_dev_sector(bdev, 0, &sect);
	if (!data)
	    	return -1;

	if (!valid_ics_sector(data)) {
	    	put_dev_sector(sect);
		return 0;
	}

	printk(" [ICS]");

	for (slot = 1, p = (const struct ics_part *)data; p->size; p++) {
		u32 start = le32_to_cpu(p->start);
		s32 size = le32_to_cpu(p->size); /* yes, it's signed. */

		if (slot == state->limit)
			break;

		/*
		 * Negative sizes tell the RISC OS ICS driver to ignore
		 * this partition - in effect it says that this does not
		 * contain an ADFS filesystem.
		 */
		if (size < 0) {
			size = -size;

			/*
			 * Our own extension - We use the first sector
			 * of the partition to identify what type this
			 * partition is.  We must not make this visible
			 * to the filesystem.
			 */
			if (size > 1 && adfspart_check_ICSLinux(bdev, start)) {
				start += 1;
				size -= 1;
			}
		}

		if (size)
			put_partition(state, slot++, start, size);
	}

	put_dev_sector(sect);
	printk("\n");
	return 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_POWERTEC
struct ptec_part {
	__le32 unused1;
	__le32 unused2;
	__le32 start;
	__le32 size;
	__le32 unused5;
	char type[8];
};

static inline int valid_ptec_sector(const unsigned char *data)
{
	unsigned char checksum = 0x2a;
	int i;

	/*
	 * If it looks like a PC/BIOS partition, then it
	 * probably isn't PowerTec.
	 */
	if (data[510] == 0x55 && data[511] == 0xaa)
		return 0;

	for (i = 0; i < 511; i++)
		checksum += data[i];

	return checksum == data[511];
}

/*
 * Purpose: allocate ICS partitions.
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 * Returns: -1 on error, 0 for no ICS table, 1 for partitions ok.
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition 0 on first drive.
 *	    hda2 = ADFS partition 1 on first drive.
 *		..etc..
 */
int
adfspart_check_POWERTEC(struct parsed_partitions *state, struct block_device *bdev)
{
	Sector sect;
	const unsigned char *data;
	const struct ptec_part *p;
	int slot = 1;
	int i;

	data = read_dev_sector(bdev, 0, &sect);
	if (!data)
		return -1;

	if (!valid_ptec_sector(data)) {
		put_dev_sector(sect);
		return 0;
	}

	printk(" [POWERTEC]");

	for (i = 0, p = (const struct ptec_part *)data; i < 12; i++, p++) {
		u32 start = le32_to_cpu(p->start);
		u32 size = le32_to_cpu(p->size);

		if (size)
			put_partition(state, slot++, start, size);
	}

	put_dev_sector(sect);
	printk("\n");
	return 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_EESOX
struct eesox_part {
	char	magic[6];
	char	name[10];
	__le32	start;
	__le32	unused6;
	__le32	unused7;
	__le32	unused8;
};

/*
 * Guess who created this format?
 */
static const char eesox_name[] = {
	'N', 'e', 'i', 'l', ' ',
	'C', 'r', 'i', 't', 'c', 'h', 'e', 'l', 'l', ' ', ' '
};

/*
 * EESOX SCSI partition format.
 *
 * This is a goddamned awful partition format.  We don't seem to store
 * the size of the partition in this table, only the start addresses.
 *
 * There are two possibilities where the size comes from:
 *  1. The individual ADFS boot block entries that are placed on the disk.
 *  2. The start address of the next entry.
 */
int
adfspart_check_EESOX(struct parsed_partitions *state, struct block_device *bdev)
{
	Sector sect;
	const unsigned char *data;
	unsigned char buffer[256];
	struct eesox_part *p;
	sector_t start = 0;
	int i, slot = 1;

	data = read_dev_sector(bdev, 7, &sect);
	if (!data)
		return -1;

	/*
	 * "Decrypt" the partition table.  God knows why...
	 */
	for (i = 0; i < 256; i++)
		buffer[i] = data[i] ^ eesox_name[i & 15];

	put_dev_sector(sect);

	for (i = 0, p = (struct eesox_part *)buffer; i < 8; i++, p++) {
		sector_t next;

		if (memcmp(p->magic, "Eesox", 6))
			break;

		next = le32_to_cpu(p->start);
		if (i)
			put_partition(state, slot++, start, next - start);
		start = next;
	}

	if (i != 0) {
		sector_t size;

		size = get_capacity(bdev->bd_disk);
		put_partition(state, slot++, start, size - start);
		printk("\n");
	}

	return i ? 1 : 0;
}
#endif
