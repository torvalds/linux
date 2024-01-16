/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_QUOTAIO_V1_H
#define _LINUX_QUOTAIO_V1_H

#include <linux/types.h>

/*
 * The following constants define the amount of time given a user
 * before the soft limits are treated as hard limits (usually resulting
 * in an allocation failure). The timer is started when the user crosses
 * their soft limit, it is reset when they go below their soft limit.
 */
#define MAX_IQ_TIME  604800	/* (7*24*60*60) 1 week */
#define MAX_DQ_TIME  604800	/* (7*24*60*60) 1 week */

/*
 * The following structure defines the format of the disk quota file
 * (as it appears on disk) - the file is an array of these structures
 * indexed by user or group number.
 */
struct v1_disk_dqblk {
	__u32 dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	__u32 dqb_bsoftlimit;	/* preferred limit on disk blks */
	__u32 dqb_curblocks;	/* current block count */
	__u32 dqb_ihardlimit;	/* absolute limit on allocated inodes */
	__u32 dqb_isoftlimit;	/* preferred inode limit */
	__u32 dqb_curinodes;	/* current # allocated inodes */

	/* below fields differ in length on 32-bit vs 64-bit architectures */
	unsigned long dqb_btime; /* time limit for excessive disk use */
	unsigned long dqb_itime; /* time limit for excessive inode use */
};

#define v1_dqoff(UID)      ((loff_t)((UID) * sizeof (struct v1_disk_dqblk)))

#endif	/* _LINUX_QUOTAIO_V1_H */
