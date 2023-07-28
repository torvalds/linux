/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TRACEFS_INTERNAL_H
#define _TRACEFS_INTERNAL_H

struct tracefs_inode {
	unsigned long           flags;
	void                    *private;
	struct inode            vfs_inode;
};

static inline struct tracefs_inode *get_tracefs(const struct inode *inode)
{
	return container_of(inode, struct tracefs_inode, vfs_inode);
}
#endif /* _TRACEFS_INTERNAL_H */
