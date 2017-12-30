/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_BLKPG_H
#define _UAPI__LINUX_BLKPG_H

/*
 * Partition table and disk geometry handling
 *
 * A single ioctl with lots of subfunctions:
 *
 * Device number stuff:
 *    get_whole_disk()		(given the device number of a partition,
 *                               find the device number of the encompassing disk)
 *    get_all_partitions()	(given the device number of a disk, return the
 *				 device numbers of all its known partitions)
 *
 * Partition stuff:
 *    add_partition()
 *    delete_partition()
 *    test_partition_in_use()	(also for test_disk_in_use)
 *
 * Geometry stuff:
 *    get_geometry()
 *    set_geometry()
 *    get_bios_drivedata()
 *
 * For today, only the partition stuff - aeb, 990515
 */
#include <linux/compiler.h>
#include <linux/ioctl.h>

#define BLKPG      _IO(0x12,105)

/* The argument structure */
struct blkpg_ioctl_arg {
        int op;
        int flags;
        int datalen;
        void __user *data;
};

/* The subfunctions (for the op field) */
#define BLKPG_ADD_PARTITION	1
#define BLKPG_DEL_PARTITION	2
#define BLKPG_RESIZE_PARTITION	3

/* Sizes of name fields. Unused at present. */
#define BLKPG_DEVNAMELTH	64
#define BLKPG_VOLNAMELTH	64

/* The data structure for ADD_PARTITION and DEL_PARTITION */
struct blkpg_partition {
	long long start;		/* starting offset in bytes */
	long long length;		/* length in bytes */
	int pno;			/* partition number */
	char devname[BLKPG_DEVNAMELTH];	/* partition name, like sda5 or c0d1p2,
					   to be used in kernel messages */
	char volname[BLKPG_VOLNAMELTH];	/* volume label */
};

#endif /* _UAPI__LINUX_BLKPG_H */
