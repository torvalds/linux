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

/*
 * struct eventfs_inode - hold the properties of the eventfs directories.
 * @list:	link list into the parent directory
 * @entries:	the array of entries representing the files in the directory
 * @name:	the name of the directory to create
 * @children:	link list into the child eventfs_inode
 * @dentry:     the dentry of the directory
 * @d_parent:   pointer to the parent's dentry
 * @d_children: The array of dentries to represent the files when created
 * @data:	The private data to pass to the callbacks
 * @nr_entries: The number of items in @entries
 */
struct eventfs_inode {
	struct list_head		list;
	const struct eventfs_entry	*entries;
	const char			*name;
	struct list_head		children;
	struct dentry			*dentry;
	struct dentry			*d_parent;
	struct dentry			**d_children;
	void				*data;
	/*
	 * Union - used for deletion
	 * @del_list:	list of eventfs_inode to delete
	 * @rcu:	eventfs_indoe to delete in RCU
	 * @is_freed:	node is freed if one of the above is set
	 */
	union {
		struct list_head	del_list;
		struct rcu_head		rcu;
		unsigned long		is_freed;
	};
	int				nr_entries;
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
void eventfs_set_ei_status_free(struct tracefs_inode *ti, struct dentry *dentry);

#endif /* _TRACEFS_INTERNAL_H */
