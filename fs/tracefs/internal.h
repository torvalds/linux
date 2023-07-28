/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TRACEFS_INTERNAL_H
#define _TRACEFS_INTERNAL_H

enum {
	TRACEFS_EVENT_INODE     = BIT(1),
};

struct tracefs_inode {
	unsigned long           flags;
	void                    *private;
	struct inode            vfs_inode;
};

static inline struct tracefs_inode *get_tracefs(const struct inode *inode)
{
	return container_of(inode, struct tracefs_inode, vfs_inode);
}

struct dentry *tracefs_start_creating(const char *name, struct dentry *parent);
struct dentry *tracefs_end_creating(struct dentry *dentry);
struct dentry *tracefs_failed_creating(struct dentry *dentry);
struct inode *tracefs_get_inode(struct super_block *sb);
#endif /* _TRACEFS_INTERNAL_H */
