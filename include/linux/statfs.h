#ifndef _LINUX_STATFS_H
#define _LINUX_STATFS_H

#include <linux/types.h>

#include <asm/statfs.h>

struct kstatfs {
	long f_type;
	long f_bsize;
	sector_t f_blocks;
	sector_t f_bfree;
	sector_t f_bavail;
	sector_t f_files;
	sector_t f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_spare[5];
};

#endif
