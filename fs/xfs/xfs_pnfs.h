/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XFS_PNFS_H
#define _XFS_PNFS_H 1

#include <linux/exportfs_block.h>

#ifdef CONFIG_EXPORTFS_BLOCK_OPS
int xfs_break_leased_layouts(struct inode *inode, uint *iolock,
		bool *did_unlock);
#else
static inline int
xfs_break_leased_layouts(struct inode *inode, uint *iolock, bool *did_unlock)
{
	return 0;
}
#endif /* CONFIG_EXPORTFS_BLOCK_OPS */

extern const struct exportfs_block_ops xfs_export_block_ops;

#endif /* _XFS_PNFS_H */
