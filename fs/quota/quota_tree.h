/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Definitions of structures for vfsv0 quota format
 */

#ifndef _LINUX_QUOTA_TREE_H
#define _LINUX_QUOTA_TREE_H

#include <linux/types.h>
#include <linux/quota.h>

/*
 *  Structure of header of block with quota structures. It is padded to 16 bytes so
 *  there will be space for exactly 21 quota-entries in a block
 */
struct qt_disk_dqdbheader {
	__le32 dqdh_next_free;	/* Number of next block with free entry */
	__le32 dqdh_prev_free;	/* Number of previous block with free entry */
	__le16 dqdh_entries;	/* Number of valid entries in block */
	__le16 dqdh_pad1;
	__le32 dqdh_pad2;
};

#define QT_TREEOFF	1		/* Offset of tree in file in blocks */

#endif /* _LINUX_QUOTAIO_TREE_H */
