/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TRACEFS_INTERNAL_H
#define _TRACEFS_INTERNAL_H

enum {
	TRACEFS_EVENT_INODE		= BIT(1),
	TRACEFS_GID_PERM_SET		= BIT(2),
	TRACEFS_UID_PERM_SET		= BIT(3),
	TRACEFS_INSTANCE_INODE		= BIT(4),
};

struct tracefs_inode {
	union {
		struct inode            vfs_inode;
		struct rcu_head		rcu;
	};
	/* The below gets initialized with memset_after(ti, 0, vfs_inode) */
	struct list_head	list;
	unsigned long           flags;
	void                    *private;
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
 * @rcu:	Union with @list for freeing
 * @children:	link list into the child eventfs_inode
 * @entries:	the array of entries representing the files in the directory
 * @name:	the name of the directory to create
 * @entry_attrs: Saved mode and ownership of the @d_children
 * @data:	The private data to pass to the callbacks
 * @attr:	Saved mode and ownership of eventfs_inode itself
 * @is_freed:	Flag set if the eventfs is on its way to be freed
 *                Note if is_freed is set, then dentry is corrupted.
 * @is_events:	Flag set for only the top level "events" directory
 * @nr_entries: The number of items in @entries
 * @ino:	The saved inode number
 */
struct eventfs_inode {
	union {
		struct list_head	list;
		struct rcu_head		rcu;
	};
	struct list_head		children;
	const struct eventfs_entry	*entries;
	const char			*name;
	struct eventfs_attr		*entry_attrs;
	void				*data;
	struct eventfs_attr		attr;
	struct kref			kref;
	unsigned int			is_freed:1;
	unsigned int			is_events:1;
	unsigned int			nr_entries:30;
	unsigned int			ino;
};

static inline struct tracefs_inode *get_tracefs(const struct inode *inode)
{
	return container_of(inode, struct tracefs_inode, vfs_inode);
}

struct dentry *tracefs_start_creating(const char *name, struct dentry *parent);
struct dentry *tracefs_end_creating(struct dentry *dentry);
struct dentry *tracefs_failed_creating(struct dentry *dentry);
struct inode *tracefs_get_inode(struct super_block *sb);

void eventfs_remount(struct tracefs_inode *ti, bool update_uid, bool update_gid);
void eventfs_d_release(struct dentry *dentry);

#endif /* _TRACEFS_INTERNAL_H */
