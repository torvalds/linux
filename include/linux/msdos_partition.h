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

enum msdos_sys_ind {
	/*
	 * These three have identical behaviour; use the second one if DOS FDISK
	 * gets confused about extended/logical partitions starting past
	 * cylinder 1023.
	 */
	DOS_EXTENDED_PARTITION = 5,
	LINUX_EXTENDED_PARTITION = 0x85,
	WIN98_EXTENDED_PARTITION = 0x0f,

	LINUX_DATA_PARTITION = 0x83,
	LINUX_LVM_PARTITION = 0x8e,
	LINUX_RAID_PARTITION = 0xfd,	/* autodetect RAID partition */

	SOLARIS_X86_PARTITION =	0x82,	/* also Linux swap partitions */
	NEW_SOLARIS_X86_PARTITION = 0xbf,

	DM6_AUX1PARTITION = 0x51,	/* no DDO:  use xlated geom */
	DM6_AUX3PARTITION = 0x53,	/* no DDO:  use xlated geom */
	DM6_PARTITION =	0x54,		/* has DDO: use xlated geom & offset */
	EZD_PARTITION =	0x55,		/* EZ-DRIVE */

	FREEBSD_PARTITION = 0xa5,	/* FreeBSD Partition ID */
	OPENBSD_PARTITION = 0xa6,	/* OpenBSD Partition ID */
	NETBSD_PARTITION = 0xa9,	/* NetBSD Partition ID */
	BSDI_PARTITION = 0xb7,		/* BSDI Partition ID */
	MINIX_PARTITION = 0x81,		/* Minix Partition ID */
	UNIXWARE_PARTITION = 0x63,	/* Same as GNU_HURD and SCO Unix */
};

#endif /* LINUX_MSDOS_PARTITION_H */
