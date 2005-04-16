/*
 * File...........: linux/fs/partitions/ibm.c      
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Volker Sameske <sameske@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000

 * History of changes (starts July 2000)
 * 07/10/00 Fixed detection of CMS formatted disks     
 * 02/13/00 VTOC partition support added
 * 12/27/01 fixed PL030593 (CMS reserved minidisk not detected on 64 bit)
 * 07/24/03 no longer using contents of freed page for CMS label recognition (BZ3611)
 */

#include <linux/config.h>
#include <linux/buffer_head.h>
#include <linux/hdreg.h>
#include <linux/slab.h>
#include <asm/dasd.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>
#include <asm/vtoc.h>

#include "check.h"
#include "ibm.h"

/*
 * compute the block number from a 
 * cyl-cyl-head-head structure
 */
static inline int
cchh2blk (cchh_t *ptr, struct hd_geometry *geo) {
        return ptr->cc * geo->heads * geo->sectors +
	       ptr->hh * geo->sectors;
}


/*
 * compute the block number from a 
 * cyl-cyl-head-head-block structure
 */
static inline int
cchhb2blk (cchhb_t *ptr, struct hd_geometry *geo) {
        return ptr->cc * geo->heads * geo->sectors +
		ptr->hh * geo->sectors +
		ptr->b;
}

/*
 */
int 
ibm_partition(struct parsed_partitions *state, struct block_device *bdev)
{
	int blocksize, offset, size;
	dasd_information_t *info;
	struct hd_geometry *geo;
	char type[5] = {0,};
	char name[7] = {0,};
	volume_label_t *vlabel;
	unsigned char *data;
	Sector sect;

	if ((info = kmalloc(sizeof(dasd_information_t), GFP_KERNEL)) == NULL)
		goto out_noinfo;
	if ((geo = kmalloc(sizeof(struct hd_geometry), GFP_KERNEL)) == NULL)
		goto out_nogeo;
	if ((vlabel = kmalloc(sizeof(volume_label_t), GFP_KERNEL)) == NULL)
		goto out_novlab;
	
	if (ioctl_by_bdev(bdev, BIODASDINFO, (unsigned long)info) != 0 ||
	    ioctl_by_bdev(bdev, HDIO_GETGEO, (unsigned long)geo) != 0)
		goto out_noioctl;
	
	if ((blocksize = bdev_hardsect_size(bdev)) <= 0)
		goto out_badsect;

	/*
	 * Get volume label, extract name and type.
	 */
	data = read_dev_sector(bdev, info->label_block*(blocksize/512), &sect);
	if (data == NULL)
		goto out_readerr;

	strncpy (type, data, 4);
	if ((!info->FBA_layout) && (!strcmp(info->type, "ECKD")))
		strncpy(name, data + 8, 6);
	else
		strncpy(name, data + 4, 6);
	memcpy (vlabel, data, sizeof(volume_label_t));
	put_dev_sector(sect);

	EBCASC(type, 4);
	EBCASC(name, 6);

	/*
	 * Three different types: CMS1, VOL1 and LNX1/unlabeled
	 */
	if (strncmp(type, "CMS1", 4) == 0) {
		/*
		 * VM style CMS1 labeled disk
		 */
		int *label = (int *) vlabel;

		if (label[13] != 0) {
			printk("CMS1/%8s(MDSK):", name);
			/* disk is reserved minidisk */
			blocksize = label[3];
			offset = label[13];
			size = (label[7] - 1)*(blocksize >> 9);
		} else {
			printk("CMS1/%8s:", name);
			offset = (info->label_block + 1);
			size = bdev->bd_inode->i_size >> 9;
		}
		put_partition(state, 1, offset*(blocksize >> 9),
				 size-offset*(blocksize >> 9));
	} else if ((strncmp(type, "VOL1", 4) == 0) &&
		(!info->FBA_layout) && (!strcmp(info->type, "ECKD"))) {
		/*
		 * New style VOL1 labeled disk
		 */
		unsigned int blk;
		int counter;

		printk("VOL1/%8s:", name);

		/* get block number and read then go through format1 labels */
		blk = cchhb2blk(&vlabel->vtoc, geo) + 1;
		counter = 0;
		while ((data = read_dev_sector(bdev, blk*(blocksize/512),
					       &sect)) != NULL) {
			format1_label_t f1;

			memcpy(&f1, data, sizeof(format1_label_t));
			put_dev_sector(sect);

			/* skip FMT4 / FMT5 / FMT7 labels */
			if (f1.DS1FMTID == _ascebc['4']
			    || f1.DS1FMTID == _ascebc['5']
			    || f1.DS1FMTID == _ascebc['7']) {
			        blk++;
				continue;
			}

			/* only FMT1 valid at this point */
			if (f1.DS1FMTID != _ascebc['1'])
				break;

			/* OK, we got valid partition data */
		        offset = cchh2blk(&f1.DS1EXT1.llimit, geo);
			size  = cchh2blk(&f1.DS1EXT1.ulimit, geo) - 
				offset + geo->sectors;
			if (counter >= state->limit)
				break;
			put_partition(state, counter + 1, 
					 offset * (blocksize >> 9),
					 size * (blocksize >> 9));
			counter++;
			blk++;
		}
	} else {
		/*
		 * Old style LNX1 or unlabeled disk
		 */
		if (strncmp(type, "LNX1", 4) == 0)
			printk ("LNX1/%8s:", name);
		else
			printk("(nonl)/%8s:", name);
		offset = (info->label_block + 1);
		size = (bdev->bd_inode->i_size >> 9);
		put_partition(state, 1, offset*(blocksize >> 9),
				 size-offset*(blocksize >> 9));
	}

	printk("\n");
	kfree(vlabel);
	kfree(geo);
	kfree(info);
	return 1;
	
out_readerr:
out_badsect:
out_noioctl:
	kfree(vlabel);
out_novlab:
	kfree(geo);
out_nogeo:
	kfree(info);
out_noinfo:
	return 0;
}
