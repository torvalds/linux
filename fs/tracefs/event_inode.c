// SPDX-License-Identifier: GPL-2.0-only
/*
 *  event_inode.c - part of tracefs, a pseudo file system for activating tracing
 *
 *  Copyright (C) 2020-23 VMware Inc, author: Steven Rostedt <rostedt@goodmis.org>
 *  Copyright (C) 2020-23 VMware Inc, author: Ajay Kaher <akaher@vmware.com>
 *  Copyright (C) 2023 Google, author: Steven Rostedt <rostedt@goodmis.org>
 *
 *  eventfs is used to dynamically create inodes and dentries based on the
 *  meta data provided by the tracing system.
 *
 *  eventfs stores the meta-data of files/dirs and holds off on creating
 *  inodes/dentries of the files. When accessed, the eventfs will create the
 *  inodes/dentries in a just-in-time (JIT) manner. The eventfs will clean up
 *  and delete the inodes/dentries when they are no longer referenced.
 */
#include <linux/fsnotify.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/tracefs.h>
#include <linux/kref.h>
#include <linux/delay.h>
#include "internal.h"

/*
 * eventfs_mutex protects the eventfs_inode (ei) dentry. Any access
 * to the ei->dentry must be done under this mutex and after checking
 * if ei->is_freed is not set. When ei->is_freed is set, the dentry
 * is on its way to being freed after the last dput() is made on it.
 */
static DEFINE_MUTEX(eventfs_mutex);

/* Choose something "unique" ;-) */
#define EVENTFS_FILE_INODE_INO		0x12c4e37

struct eventfs_root_inode {
	struct eventfs_inode		ei;
	struct inode			*parent_inode;
	struct dentry			*events_dir;
};

static struct eventfs_root_inode *get_root_inode(struct eventfs_inode *ei)
{
	WARN_ON_ONCE(!ei->is_events);
	return container_of(ei, struct eventfs_root_inode, ei);
}

/* Just try to make something consistent and unique */
static int eventfs_dir_ino(struct eventfs_inode *ei)
{
	if (!ei->ino)
		ei->ino = get_next_ino();

	return ei->ino;
}

/*
 * The eventfs_inode (ei) itself is protected by SRCU. It is released from
 * its parent's list and will have is_freed set (under eventfs_mutex).
 * After the SRCU grace period is over and the last dput() is called
 * the ei is freed.
 */
DEFINE_STATIC_SRCU(eventfs_srcu);

/* Mode is unsigned short, use the upper bits for flags */
enum {
	EVENTFS_SAVE_MODE	= BIT(16),
	EVENTFS_SAVE_UID	= BIT(17),
	EVENTFS_SAVE_GID	= BIT(18),
};

#define EVENTFS_MODE_MASK	(EVENTFS_SAVE_MODE - 1)

static void free_ei_rcu(struct rcu_head *rcu)
{
	struct eventfs_inode *ei = container_of(rcu, struct eventfs_inode, rcu);
	struct eventfs_root_inode *rei;

	kfree(ei->entry_attrs);
	kfree_const(ei->name);
	if (ei->is_events) {
		rei = get_root_inode(ei);
		kfree(rei);
	} else {
		kfree(ei);
	}
}

/*
 * eventfs_inode reference count management.
 *
 * NOTE! We count only references from dentries, in the
 * form 'dentry->d_fsdata'. There are also references from
 * directory inodes ('ti->private'), but the dentry reference
 * count is always a superset of the inode reference count.
 */
static void release_ei(struct kref *ref)
{
	struct eventfs_inode *ei = container_of(ref, struct eventfs_inode, kref);
	const struct eventfs_entry *entry;

	WARN_ON_ONCE(!ei->is_freed);

	for (int i = 0; i < ei->nr_entries; i++) {
		entry = &ei->entries[i];
		if (entry->release)
			entry->release(entry->name, ei->data);
	}

	call_rcu(&ei->rcu, free_ei_rcu);
}

static inline void put_ei(struct eventfs_inode *ei)
{
	if (ei)
		kref_put(&ei->kref, release_ei);
}

static inline void free_ei(struct eventfs_inode *ei)
{
	if (ei) {
		ei->is_freed = 1;
		put_ei(ei);
	}
}

/*
 * Called when creation of an ei fails, do not call release() functions.
 */
static inline void cleanup_ei(struct eventfs_inode *ei)
{
	if (ei) {
		/* Set nr_entries to 0 to prevent release() function being called */
		ei->nr_entries = 0;
		free_ei(ei);
	}
}

static inline struct eventfs_inode *get_ei(struct eventfs_inode *ei)
{
	if (ei)
		kref_get(&ei->kref);
	return ei;
}

static struct dentry *eventfs_root_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags);
static int eventfs_iterate(struct file *file, struct dir_context *ctx);

static void update_attr(struct eventfs_attr *attr, struct iattr *iattr)
{
	unsigned int ia_valid = iattr->ia_valid;

	if (ia_valid & ATTR_MODE) {
		attr->mode = (attr->mode & ~EVENTFS_MODE_MASK) |
			(iattr->ia_mode & EVENTFS_MODE_MASK) |
			EVENTFS_SAVE_MODE;
	}
	if (ia_valid & ATTR_UID) {
		attr->mode |= EVENTFS_SAVE_UID;
		attr->uid = iattr->ia_uid;
	}
	if (ia_valid & ATTR_GID) {
		attr->mode |= EVENTFS_SAVE_GID;
		attr->gid = iattr->ia_gid;
	}
}

static int eventfs_set_attr(struct mnt_idmap *idmap, struct dentry *dentry,
			    struct iattr *iattr)
{
	const struct eventfs_entry *entry;
	struct eventfs_inode *ei;
	const char *name;
	int ret;

	mutex_lock(&eventfs_mutex);
	ei = dentry->d_fsdata;
	if (ei->is_freed) {
		/* Do not allow changes if the event is about to be removed. */
		mutex_unlock(&eventfs_mutex);
		return -ENODEV;
	}

	/* Preallocate the children mode array if necessary */
	if (!(dentry->d_inode->i_mode & S_IFDIR)) {
		if (!ei->entry_attrs) {
			ei->entry_attrs = kcalloc(ei->nr_entries, sizeof(*ei->entry_attrs),
						  GFP_NOFS);
			if (!ei->entry_attrs) {
				ret = -ENOMEM;
				goto out;
			}
		}
	}

	ret = simple_setattr(idmap, dentry, iattr);
	if (ret < 0)
		goto out;

	/*
	 * If this is a dir, then update the ei cache, only the file
	 * mode is saved in the ei->m_children, and the ownership is
	 * determined by the parent directory.
	 */
	if (dentry->d_inode->i_mode & S_IFDIR) {
		update_attr(&ei->attr, iattr);

	} else {
		name = dentry->d_name.name;

		for (int i = 0; i < ei->nr_entries; i++) {
			entry = &ei->entries[i];
			if (strcmp(name, entry->name) == 0) {
				update_attr(&ei->entry_attrs[i], iattr);
				break;
			}
		}
	}
 out:
	mutex_unlock(&eventfs_mutex);
	return ret;
}

static void update_events_attr(struct eventfs_inode *ei, struct super_block *sb)
{
	struct eventfs_root_inode *rei;
	struct inode *parent;

	rei = get_root_inode(ei);

	/* Use the parent inode permissions unless root set its permissions */
	parent = rei->parent_inode;

	if (rei->ei.attr.mode & EVENTFS_SAVE_UID)
		ei->attr.uid = rei->ei.attr.uid;
	else
		ei->attr.uid = parent->i_uid;

	if (rei->ei.attr.mode & EVENTFS_SAVE_GID)
		ei->attr.gid = rei->ei.attr.gid;
	else
		ei->attr.gid = parent->i_gid;
}

static void set_top_events_ownership(struct inode *inode)
{
	struct tracefs_inode *ti = get_tracefs(inode);
	struct eventfs_inode *ei = ti->private;

	/* The top events directory doesn't get automatically updated */
	if (!ei || !ei->is_events)
		return;

	update_events_attr(ei, inode->i_sb);

	if (!(ei->attr.mode & EVENTFS_SAVE_UID))
		inode->i_uid = ei->attr.uid;

	if (!(ei->attr.mode & EVENTFS_SAVE_GID))
		inode->i_gid = ei->attr.gid;
}

static int eventfs_get_attr(struct mnt_idmap *idmap,
			    const struct path *path, struct kstat *stat,
			    u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_backing_inode(dentry);

	set_top_events_ownership(inode);

	generic_fillattr(idmap, request_mask, inode, stat);
	return 0;
}

static int eventfs_permission(struct mnt_idmap *idmap,
			      struct inode *inode, int mask)
{
	set_top_events_ownership(inode);
	return generic_permission(idmap, inode, mask);
}

static const struct inode_operations eventfs_dir_inode_operations = {
	.lookup		= eventfs_root_lookup,
	.setattr	= eventfs_set_attr,
	.getattr	= eventfs_get_attr,
	.permission	= eventfs_permission,
};

static const struct inode_operations eventfs_file_inode_operations = {
	.setattr	= eventfs_set_attr,
};

static const struct file_operations eventfs_file_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= eventfs_iterate,
	.llseek		= generic_file_llseek,
};

/*
 * On a remount of tracefs, if UID or GID options are set, then
 * the mount point inode permissions should be used.
 * Reset the saved permission flags appropriately.
 */
void eventfs_remount(struct tracefs_inode *ti, bool update_uid, bool update_gid)
{
	struct eventfs_inode *ei = ti->private;

	if (!ei)
		return;

	if (update_uid)
		ei->attr.mode &= ~EVENTFS_SAVE_UID;

	if (update_gid)
		ei->attr.mode &= ~EVENTFS_SAVE_GID;

	if (!ei->entry_attrs)
		return;

	for (int i = 0; i < ei->nr_entries; i++) {
		if (update_uid)
			ei->entry_attrs[i].mode &= ~EVENTFS_SAVE_UID;
		if (update_gid)
			ei->entry_attrs[i].mode &= ~EVENTFS_SAVE_GID;
	}
}

/* Return the evenfs_inode of the "events" directory */
static struct eventfs_inode *eventfs_find_events(struct dentry *dentry)
{
	struct eventfs_inode *ei;

	do {
		// The parent is stable because we do not do renames
		dentry = dentry->d_parent;
		// ... and directories always have d_fsdata
		ei = dentry->d_fsdata;

		/*
		 * If the ei is being freed, the ownership of the children
		 * doesn't matter.
		 */
		if (ei->is_freed)
			return NULL;

		// Walk upwards until you find the events inode
	} while (!ei->is_events);

	update_events_attr(ei, dentry->d_sb);

	return ei;
}

static void update_inode_attr(struct dentry *dentry, struct inode *inode,
			      struct eventfs_attr *attr, umode_t mode)
{
	struct eventfs_inode *events_ei = eventfs_find_events(dentry);

	if (!events_ei)
		return;

	inode->i_mode = mode;
	inode->i_uid = events_ei->attr.uid;
	inode->i_gid = events_ei->attr.gid;

	if (!attr)
		return;

	if (attr->mode & EVENTFS_SAVE_MODE)
		inode->i_mode = attr->mode & EVENTFS_MODE_MASK;

	if (attr->mode & EVENTFS_SAVE_UID)
		inode->i_uid = attr->uid;

	if (attr->mode & EVENTFS_SAVE_GID)
		inode->i_gid = attr->gid;
}

/**
 * lookup_file - look up a file in the tracefs filesystem
 * @parent_ei: Pointer to the eventfs_inode that represents parent of the file
 * @dentry: the dentry to look up
 * @mode: the permission that the file should have.
 * @attr: saved attributes changed by user
 * @data: something that the caller will want to get to later on.
 * @fop: struct file_operations that should be used for this file.
 *
 * This function creates a dentry that represents a file in the eventsfs_inode
 * directory. The inode.i_private pointer will point to @data in the open()
 * call.
 */
static struct dentry *lookup_file(struct eventfs_inode *parent_ei,
				  struct dentry *dentry,
				  umode_t mode,
				  struct eventfs_attr *attr,
				  void *data,
				  const struct file_operations *fop)
{
	struct tracefs_inode *ti;
	struct inode *inode;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	if (WARN_ON_ONCE(!S_ISREG(mode)))
		return ERR_PTR(-EIO);

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);

	/* If the user updated the directory's attributes, use them */
	update_inode_attr(dentry, inode, attr, mode);

	inode->i_op = &eventfs_file_inode_operations;
	inode->i_fop = fop;
	inode->i_private = data;

	/* All files will have the same inode number */
	inode->i_ino = EVENTFS_FILE_INODE_INO;

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE;

	// Files have their parent's ei as their fsdata
	dentry->d_fsdata = get_ei(parent_ei);

	d_add(dentry, inode);
	return NULL;
};

/**
 * lookup_dir_entry - look up a dir in the tracefs filesystem
 * @dentry: the directory to look up
 * @pei: Pointer to the parent eventfs_inode if available
 * @ei: the eventfs_inode that represents the directory to create
 *
 * This function will look up a dentry for a directory represented by
 * a eventfs_inode.
 */
static struct dentry *lookup_dir_entry(struct dentry *dentry,
	struct eventfs_inode *pei, struct eventfs_inode *ei)
{
	struct tracefs_inode *ti;
	struct inode *inode;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);

	/* If the user updated the directory's attributes, use them */
	update_inode_attr(dentry, inode, &ei->attr,
			  S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO);

	inode->i_op = &eventfs_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;

	/* All directories will have the same inode number */
	inode->i_ino = eventfs_dir_ino(ei);

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE;
	/* Only directories have ti->private set to an ei, not files */
	ti->private = ei;

	dentry->d_fsdata = get_ei(ei);

	d_add(dentry, inode);
	return NULL;
}

static inline struct eventfs_inode *init_ei(struct eventfs_inode *ei, const char *name)
{
	ei->name = kstrdup_const(name, GFP_KERNEL);
	if (!ei->name)
		return NULL;
	kref_init(&ei->kref);
	return ei;
}

static inline struct eventfs_inode *alloc_ei(const char *name)
{
	struct eventfs_inode *ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	struct eventfs_inode *result;

	if (!ei)
		return NULL;

	result = init_ei(ei, name);
	if (!result)
		kfree(ei);

	return result;
}

static inline struct eventfs_inode *alloc_root_ei(const char *name)
{
	struct eventfs_root_inode *rei = kzalloc(sizeof(*rei), GFP_KERNEL);
	struct eventfs_inode *ei;

	if (!rei)
		return NULL;

	rei->ei.is_events = 1;
	ei = init_ei(&rei->ei, name);
	if (!ei)
		kfree(rei);

	return ei;
}

/**
 * eventfs_d_release - dentry is going away
 * @dentry: dentry which has the reference to remove.
 *
 * Remove the association between a dentry from an eventfs_inode.
 */
void eventfs_d_release(struct dentry *dentry)
{
	put_ei(dentry->d_fsdata);
}

/**
 * lookup_file_dentry - create a dentry for a file of an eventfs_inode
 * @dentry: The parent dentry under which the new file's dentry will be created
 * @ei: the eventfs_inode that the file will be created under
 * @idx: the index into the entry_attrs[] of the @ei
 * @mode: The mode of the file.
 * @data: The data to use to set the inode of the file with on open()
 * @fops: The fops of the file to be created.
 *
 * This function creates a dentry for a file associated with an
 * eventfs_inode @ei. It uses the entry attributes specified by @idx,
 * if available. The file will have the specified @mode and its inode will be
 * set up with @data upon open. The file operations will be set to @fops.
 *
 * Return: Returns a pointer to the newly created file's dentry or an error
 * pointer.
 */
static struct dentry *
lookup_file_dentry(struct dentry *dentry,
		   struct eventfs_inode *ei, int idx,
		   umode_t mode, void *data,
		   const struct file_operations *fops)
{
	struct eventfs_attr *attr = NULL;

	if (ei->entry_attrs)
		attr = &ei->entry_attrs[idx];

	return lookup_file(ei, dentry, mode, attr, data, fops);
}

/**
 * eventfs_root_lookup - lookup routine to create file/dir
 * @dir: in which a lookup is being done
 * @dentry: file/dir dentry
 * @flags: Just passed to simple_lookup()
 *
 * Used to create dynamic file/dir with-in @dir, search with-in @ei
 * list, if @dentry found go ahead and create the file/dir
 */

static struct dentry *eventfs_root_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags)
{
	struct eventfs_inode *ei_child;
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	const char *name = dentry->d_name.name;
	struct dentry *result = NULL;

	ti = get_tracefs(dir);
	if (WARN_ON_ONCE(!(ti->flags & TRACEFS_EVENT_INODE)))
		return ERR_PTR(-EIO);

	mutex_lock(&eventfs_mutex);

	ei = ti->private;
	if (!ei || ei->is_freed)
		goto out;

	list_for_each_entry(ei_child, &ei->children, list) {
		if (strcmp(ei_child->name, name) != 0)
			continue;
		/* A child is freed and removed from the list at the same time */
		if (WARN_ON_ONCE(ei_child->is_freed))
			goto out;
		result = lookup_dir_entry(dentry, ei, ei_child);
		goto out;
	}

	for (int i = 0; i < ei->nr_entries; i++) {
		void *data;
		umode_t mode;
		const struct file_operations *fops;
		const struct eventfs_entry *entry = &ei->entries[i];

		if (strcmp(name, entry->name) != 0)
			continue;

		data = ei->data;
		if (entry->callback(name, &mode, &data, &fops) <= 0)
			goto out;

		result = lookup_file_dentry(dentry, ei, i, mode, data, fops);
		goto out;
	}
 out:
	mutex_unlock(&eventfs_mutex);
	return result;
}

/*
 * Walk the children of a eventfs_inode to fill in getdents().
 */
static int eventfs_iterate(struct file *file, struct dir_context *ctx)
{
	const struct file_operations *fops;
	struct inode *f_inode = file_inode(file);
	const struct eventfs_entry *entry;
	struct eventfs_inode *ei_child;
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	const char *name;
	umode_t mode;
	int idx;
	int ret = -EINVAL;
	int ino;
	int i, r, c;

	if (!dir_emit_dots(file, ctx))
		return 0;

	ti = get_tracefs(f_inode);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return -EINVAL;

	c = ctx->pos - 2;

	idx = srcu_read_lock(&eventfs_srcu);

	mutex_lock(&eventfs_mutex);
	ei = READ_ONCE(ti->private);
	if (ei && ei->is_freed)
		ei = NULL;
	mutex_unlock(&eventfs_mutex);

	if (!ei)
		goto out;

	/*
	 * Need to create the dentries and inodes to have a consistent
	 * inode number.
	 */
	ret = 0;

	/* Start at 'c' to jump over already read entries */
	for (i = c; i < ei->nr_entries; i++, ctx->pos++) {
		void *cdata = ei->data;

		entry = &ei->entries[i];
		name = entry->name;

		mutex_lock(&eventfs_mutex);
		/* If ei->is_freed then just bail here, nothing more to do */
		if (ei->is_freed) {
			mutex_unlock(&eventfs_mutex);
			goto out;
		}
		r = entry->callback(name, &mode, &cdata, &fops);
		mutex_unlock(&eventfs_mutex);
		if (r <= 0)
			continue;

		ino = EVENTFS_FILE_INODE_INO;

		if (!dir_emit(ctx, name, strlen(name), ino, DT_REG))
			goto out;
	}

	/* Subtract the skipped entries above */
	c -= min((unsigned int)c, (unsigned int)ei->nr_entries);

	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 srcu_read_lock_held(&eventfs_srcu)) {

		if (c > 0) {
			c--;
			continue;
		}

		ctx->pos++;

		if (ei_child->is_freed)
			continue;

		name = ei_child->name;

		ino = eventfs_dir_ino(ei_child);

		if (!dir_emit(ctx, name, strlen(name), ino, DT_DIR))
			goto out_dec;
	}
	ret = 1;
 out:
	srcu_read_unlock(&eventfs_srcu, idx);

	return ret;

 out_dec:
	/* Incremented ctx->pos without adding something, reset it */
	ctx->pos--;
	goto out;
}

/**
 * eventfs_create_dir - Create the eventfs_inode for this directory
 * @name: The name of the directory to create.
 * @parent: The eventfs_inode of the parent directory.
 * @entries: A list of entries that represent the files under this directory
 * @size: The number of @entries
 * @data: The default data to pass to the files (an entry may override it).
 *
 * This function creates the descriptor to represent a directory in the
 * eventfs. This descriptor is an eventfs_inode, and it is returned to be
 * used to create other children underneath.
 *
 * The @entries is an array of eventfs_entry structures which has:
 *	const char		 *name
 *	eventfs_callback	callback;
 *
 * The name is the name of the file, and the callback is a pointer to a function
 * that will be called when the file is reference (either by lookup or by
 * reading a directory). The callback is of the prototype:
 *
 *    int callback(const char *name, umode_t *mode, void **data,
 *		   const struct file_operations **fops);
 *
 * When a file needs to be created, this callback will be called with
 *   name = the name of the file being created (so that the same callback
 *          may be used for multiple files).
 *   mode = a place to set the file's mode
 *   data = A pointer to @data, and the callback may replace it, which will
 *         cause the file created to pass the new data to the open() call.
 *   fops = the fops to use for the created file.
 *
 * NB. @callback is called while holding internal locks of the eventfs
 *     system. The callback must not call any code that might also call into
 *     the tracefs or eventfs system or it will risk creating a deadlock.
 */
struct eventfs_inode *eventfs_create_dir(const char *name, struct eventfs_inode *parent,
					 const struct eventfs_entry *entries,
					 int size, void *data)
{
	struct eventfs_inode *ei;

	if (!parent)
		return ERR_PTR(-EINVAL);

	ei = alloc_ei(name);
	if (!ei)
		return ERR_PTR(-ENOMEM);

	ei->entries = entries;
	ei->nr_entries = size;
	ei->data = data;
	INIT_LIST_HEAD(&ei->children);
	INIT_LIST_HEAD(&ei->list);

	mutex_lock(&eventfs_mutex);
	if (!parent->is_freed)
		list_add_tail(&ei->list, &parent->children);
	mutex_unlock(&eventfs_mutex);

	/* Was the parent freed? */
	if (list_empty(&ei->list)) {
		cleanup_ei(ei);
		ei = NULL;
	}
	return ei;
}

/**
 * eventfs_create_events_dir - create the top level events directory
 * @name: The name of the top level directory to create.
 * @parent: Parent dentry for this file in the tracefs directory.
 * @entries: A list of entries that represent the files under this directory
 * @size: The number of @entries
 * @data: The default data to pass to the files (an entry may override it).
 *
 * This function creates the top of the trace event directory.
 *
 * See eventfs_create_dir() for use of @entries.
 */
struct eventfs_inode *eventfs_create_events_dir(const char *name, struct dentry *parent,
						const struct eventfs_entry *entries,
						int size, void *data)
{
	struct dentry *dentry = tracefs_start_creating(name, parent);
	struct eventfs_root_inode *rei;
	struct eventfs_inode *ei;
	struct tracefs_inode *ti;
	struct inode *inode;
	kuid_t uid;
	kgid_t gid;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (IS_ERR(dentry))
		return ERR_CAST(dentry);

	ei = alloc_root_ei(name);
	if (!ei)
		goto fail;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		goto fail;

	// Note: we have a ref to the dentry from tracefs_start_creating()
	rei = get_root_inode(ei);
	rei->events_dir = dentry;
	rei->parent_inode = d_inode(dentry->d_sb->s_root);

	ei->entries = entries;
	ei->nr_entries = size;
	ei->data = data;

	/* Save the ownership of this directory */
	uid = d_inode(dentry->d_parent)->i_uid;
	gid = d_inode(dentry->d_parent)->i_gid;

	ei->attr.uid = uid;
	ei->attr.gid = gid;

	/*
	 * When the "events" directory is created, it takes on the
	 * permissions of its parent. But can be reset on remount.
	 */
	ei->attr.mode |= EVENTFS_SAVE_UID | EVENTFS_SAVE_GID;

	INIT_LIST_HEAD(&ei->children);
	INIT_LIST_HEAD(&ei->list);

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE;
	ti->private = ei;

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	inode->i_uid = uid;
	inode->i_gid = gid;
	inode->i_op = &eventfs_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;

	dentry->d_fsdata = get_ei(ei);

	/*
	 * Keep all eventfs directories with i_nlink == 1.
	 * Due to the dynamic nature of the dentry creations and not
	 * wanting to add a pointer to the parent eventfs_inode in the
	 * eventfs_inode structure, keeping the i_nlink in sync with the
	 * number of directories would cause too much complexity for
	 * something not worth much. Keeping directory links at 1
	 * tells userspace not to trust the link number.
	 */
	d_instantiate(dentry, inode);
	/* The dentry of the "events" parent does keep track though */
	inc_nlink(dentry->d_parent->d_inode);
	fsnotify_mkdir(dentry->d_parent->d_inode, dentry);
	tracefs_end_creating(dentry);

	return ei;

 fail:
	cleanup_ei(ei);
	tracefs_failed_creating(dentry);
	return ERR_PTR(-ENOMEM);
}

/**
 * eventfs_remove_rec - remove eventfs dir or file from list
 * @ei: eventfs_inode to be removed.
 * @level: prevent recursion from going more than 3 levels deep.
 *
 * This function recursively removes eventfs_inodes which
 * contains info of files and/or directories.
 */
static void eventfs_remove_rec(struct eventfs_inode *ei, int level)
{
	struct eventfs_inode *ei_child;

	/*
	 * Check recursion depth. It should never be greater than 3:
	 * 0 - events/
	 * 1 - events/group/
	 * 2 - events/group/event/
	 * 3 - events/group/event/file
	 */
	if (WARN_ON_ONCE(level > 3))
		return;

	/* search for nested folders or files */
	list_for_each_entry(ei_child, &ei->children, list)
		eventfs_remove_rec(ei_child, level + 1);

	list_del(&ei->list);
	free_ei(ei);
}

/**
 * eventfs_remove_dir - remove eventfs dir or file from list
 * @ei: eventfs_inode to be removed.
 *
 * This function acquire the eventfs_mutex lock and call eventfs_remove_rec()
 */
void eventfs_remove_dir(struct eventfs_inode *ei)
{
	if (!ei)
		return;

	mutex_lock(&eventfs_mutex);
	eventfs_remove_rec(ei, 0);
	mutex_unlock(&eventfs_mutex);
}

/**
 * eventfs_remove_events_dir - remove the top level eventfs directory
 * @ei: the event_inode returned by eventfs_create_events_dir().
 *
 * This function removes the events main directory
 */
void eventfs_remove_events_dir(struct eventfs_inode *ei)
{
	struct eventfs_root_inode *rei;
	struct dentry *dentry;

	rei = get_root_inode(ei);
	dentry = rei->events_dir;
	if (!dentry)
		return;

	rei->events_dir = NULL;
	eventfs_remove_dir(ei);

	/*
	 * Matches the dget() done by tracefs_start_creating()
	 * in eventfs_create_events_dir() when it the dentry was
	 * created. In other words, it's a normal dentry that
	 * sticks around while the other ei->dentry are created
	 * and destroyed dynamically.
	 */
	d_invalidate(dentry);
	dput(dentry);
}
