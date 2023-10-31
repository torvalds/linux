/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TRACEFS_INTERNAL_H
#define _TRACEFS_INTERNAL_H

enum {
	TRACEFS_EVENT_INODE		= BIT(1),
	TRACEFS_EVENT_TOP_INODE		= BIT(2),
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
struct dentry *eventfs_start_creating(const char *name, struct dentry *parent);
struct dentry *eventfs_failed_creating(struct dentry *dentry);
struct dentry *eventfs_end_creating(struct dentry *dentry);
void eventfs_set_ef_status_free(struct tracefs_inode *ti, struct dentry *dentry);

#endif /* _TRACEFS_INTERNAL_H */
