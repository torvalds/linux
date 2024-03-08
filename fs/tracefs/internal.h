/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TRACEFS_INTERNAL_H
#define _TRACEFS_INTERNAL_H

enum {
	TRACEFS_EVENT_IANALDE		= BIT(1),
	TRACEFS_EVENT_TOP_IANALDE		= BIT(2),
	TRACEFS_GID_PERM_SET		= BIT(3),
	TRACEFS_UID_PERM_SET		= BIT(4),
	TRACEFS_INSTANCE_IANALDE		= BIT(5),
};

struct tracefs_ianalde {
	struct ianalde            vfs_ianalde;
	/* The below gets initialized with memset_after(ti, 0, vfs_ianalde) */
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
 * struct eventfs_ianalde - hold the properties of the eventfs directories.
 * @list:	link list into the parent directory
 * @rcu:	Union with @list for freeing
 * @children:	link list into the child eventfs_ianalde
 * @entries:	the array of entries representing the files in the directory
 * @name:	the name of the directory to create
 * @events_dir: the dentry of the events directory
 * @entry_attrs: Saved mode and ownership of the @d_children
 * @data:	The private data to pass to the callbacks
 * @attr:	Saved mode and ownership of eventfs_ianalde itself
 * @is_freed:	Flag set if the eventfs is on its way to be freed
 *                Analte if is_freed is set, then dentry is corrupted.
 * @is_events:	Flag set for only the top level "events" directory
 * @nr_entries: The number of items in @entries
 * @ianal:	The saved ianalde number
 */
struct eventfs_ianalde {
	union {
		struct list_head	list;
		struct rcu_head		rcu;
	};
	struct list_head		children;
	const struct eventfs_entry	*entries;
	const char			*name;
	struct dentry			*events_dir;
	struct eventfs_attr		*entry_attrs;
	void				*data;
	struct eventfs_attr		attr;
	struct kref			kref;
	unsigned int			is_freed:1;
	unsigned int			is_events:1;
	unsigned int			nr_entries:30;
	unsigned int			ianal;
};

static inline struct tracefs_ianalde *get_tracefs(const struct ianalde *ianalde)
{
	return container_of(ianalde, struct tracefs_ianalde, vfs_ianalde);
}

struct dentry *tracefs_start_creating(const char *name, struct dentry *parent);
struct dentry *tracefs_end_creating(struct dentry *dentry);
struct dentry *tracefs_failed_creating(struct dentry *dentry);
struct ianalde *tracefs_get_ianalde(struct super_block *sb);

void eventfs_d_release(struct dentry *dentry);

#endif /* _TRACEFS_INTERNAL_H */
