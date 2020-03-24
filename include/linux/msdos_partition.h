/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MSDOS_PARTITION_H
#define _LINUX_MSDOS_PARTITION_H

#define MSDOS_LABEL_MAGIC		0xAA55

struct msdos_partition {
	u8 boot_ind;		/* 0x80 - active */
	u8 head;		/* starting head */
	u8 sector;		/* starting sector */
	u8 cyl;			/* starting cylinder */
	u8 sys_ind;		/* What partition type */
	u8 end_head;		/* end head */
	u8 end_sector;		/* end sector */
	u8 end_cyl;		/* end cylinder */
	__le32 start_sect;	/* starting sector counting from 0 */
	__le32 nr_sects;	/* nr of sectors in partition */
} __packed;

#endif /* LINUX_MSDOS_PARTITION_H */
