/*
 *	Definitions of structures for vfsv0 quota format
 */

#ifndef _LINUX_DQBLK_V2_H
#define _LINUX_DQBLK_V2_H

#include <linux/types.h>

/* id numbers of quota format */
#define QFMT_VFS_V0 2

/* Inmemory copy of version specific information */
struct v2_mem_dqinfo {
	unsigned int dqi_blocks;
	unsigned int dqi_free_blk;
	unsigned int dqi_free_entry;
};

#endif /* _LINUX_DQBLK_V2_H */
