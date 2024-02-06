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
 * struct eventfs_attr - cache the mode and ownership of a eventfs entry
 * @mode:	saved mode plus flags of what is saved
 * @uid:	saved uid if changed
 * @gid:	saved gid if changed
 */
struct eventfs_attr {
	int				mode;
	kuid_t				uid;
	kgid_t				gid;
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
 * @entry_attrs: Saved mode and ownership of the @d_children
 * @attr:	Saved mode and ownership of eventfs_inode itself
 * @data:	The private data to pass to the callbacks
 * @is_freed:	Flag set if the eventfs is on its way to be freed
 *                Note if is_freed is set, then dentry is corrupted.
 * @nr_entries: The number of items in @entries
 */
struct eventfs_inode {
	struct list_head		list;
	const struct eventfs_entry	*entries;
	const char			*name;
	struct list_head		children;
	struct dentry			*dentry; /* Check is_freed to access */
	struct dentry			*d_parent;
	struct dentry			**d_children;
	struct eventfs_attr		*entry_attrs;
	struct eventfs_attr		attr;
	void				*data;
	/*
	 * Union - used for deletion
	 * @llist:	for calling dput() if needed after RCU
	 * @rcu:	eventfs_inode to delete in RCU
	 */
	union {
		struct llist_node	llist;
		struct rcu_head		rcu;
	};
	unsigned int			is_freed:1;
	unsigned int			nr_entries:31;
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
