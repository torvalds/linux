/*
 *  fs/partitions/sgi.c
 *
 *  Code extracted from drivers/block/genhd.c
 */

#include "check.h"
#include "sgi.h"

struct sgi_disklabel {
	__be32 magic_mushroom;		/* Big fat spliff... */
	__be16 root_part_num;		/* Root partition number */
	__be16 swap_part_num;		/* Swap partition number */
	s8 boot_file[16];		/* Name of boot file for ARCS */
	u8 _unused0[48];		/* Device parameter useless crapola.. */
	struct sgi_volume {
		s8 name[8];		/* Name of volume */
		__be32 block_num;		/* Logical block number */
		__be32 num_bytes;		/* How big, in bytes */
	} volume[15];
	struct sgi_partition {
		__be32 num_blocks;		/* Size in logical blocks */
		__be32 first_block;	/* First logical block */
		__be32 type;		/* Type of this partition */
	} partitions[16];
	__be32 csum;			/* Disk label checksum */
	__be32 _unused1;			/* Padding */
};

int sgi_partition(struct parsed_partitions *state)
{
	int i, csum;
	__be32 magic;
	int slot = 1;
	unsigned int start, blocks;
	__be32 *ui, cs;
	Sector sect;
	struct sgi_disklabel *label;
	struct sgi_partition *p;
	char b[BDEVNAME_SIZE];

	label = read_part_sector(state, 0, &sect);
	if (!label)
		return -1;
	p = &label->partitions[0];
	magic = label->magic_mushroom;
	if(be32_to_cpu(magic) != SGI_LABEL_MAGIC) {
		/*printk("Dev %s SGI disklabel: bad magic %08x\n",
		       bdevname(bdev, b), be32_to_cpu(magic));*/
		put_dev_sector(sect);
		return 0;
	}
	ui = ((__be32 *) (label + 1)) - 1;
	for(csum = 0; ui >= ((__be32 *) label);) {
		cs = *ui--;
		csum += be32_to_cpu(cs);
	}
	if(csum) {
		printk(KERN_WARNING "Dev %s SGI disklabel: csum bad, label corrupted\n",
		       bdevname(state->bdev, b));
		put_dev_sector(sect);
		return 0;
	}
	/* All SGI disk labels have 16 partitions, disks under Linux only
	 * have 15 minor's.  Luckily there are always a few zero length
	 * partitions which we don't care about so we never overflow the
	 * current_minor.
	 */
	for(i = 0; i < 16; i++, p++) {
		blocks = be32_to_cpu(p->num_blocks);
		start  = be32_to_cpu(p->first_block);
		if (blocks) {
			put_partition(state, slot, start, blocks);
			if (be32_to_cpu(p->type) == LINUX_RAID_PARTITION)
				state->parts[slot].flags = ADDPART_FLAG_RAID;
		}
		slot++;
	}
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	put_dev_sector(sect);
	return 1;
}
