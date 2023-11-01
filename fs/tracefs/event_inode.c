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
 * if ei->is_freed is not set. The ei->dentry is released under the
 * mutex at the same time ei->is_freed is set. If ei->is_freed is set
 * then the ei->dentry is invalid.
 */
static DEFINE_MUTEX(eventfs_mutex);

/*
 * The eventfs_inode (ei) itself is protected by SRCU. It is released from
 * its parent's list and will have is_freed set (under eventfs_mutex).
 * After the SRCU grace period is over, the ei may be freed.
 */
DEFINE_STATIC_SRCU(eventfs_srcu);

/* Mode is unsigned short, use the upper bits for flags */
enum {
	EVENTFS_SAVE_MODE	= BIT(16),
	EVENTFS_SAVE_UID	= BIT(17),
	EVENTFS_SAVE_GID	= BIT(18),
};

#define EVENTFS_MODE_MASK	(EVENTFS_SAVE_MODE - 1)

static struct dentry *eventfs_root_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags);
static int dcache_dir_open_wrapper(struct inode *inode, struct file *file);
static int dcache_readdir_wrapper(struct file *file, struct dir_context *ctx);
static int eventfs_release(struct inode *inode, struct file *file);

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
			ei->entry_attrs = kzalloc(sizeof(*ei->entry_attrs) * ei->nr_entries,
						  GFP_KERNEL);
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

static const struct inode_operations eventfs_root_dir_inode_operations = {
	.lookup		= eventfs_root_lookup,
	.setattr	= eventfs_set_attr,
};

static const struct inode_operations eventfs_file_inode_operations = {
	.setattr	= eventfs_set_attr,
};

static const struct file_operations eventfs_file_operations = {
	.open		= dcache_dir_open_wrapper,
	.read		= generic_read_dir,
	.iterate_shared	= dcache_readdir_wrapper,
	.llseek		= generic_file_llseek,
	.release	= eventfs_release,
};

static void update_inode_attr(struct inode *inode, struct eventfs_attr *attr, umode_t mode)
{
	if (!attr) {
		inode->i_mode = mode;
		return;
	}

	if (attr->mode & EVENTFS_SAVE_MODE)
		inode->i_mode = attr->mode & EVENTFS_MODE_MASK;
	else
		inode->i_mode = mode;

	if (attr->mode & EVENTFS_SAVE_UID)
		inode->i_uid = attr->uid;

	if (attr->mode & EVENTFS_SAVE_GID)
		inode->i_gid = attr->gid;
}

/**
 * create_file - create a file in the tracefs filesystem
 * @name: the name of the file to create.
 * @mode: the permission that the file should have.
 * @attr: saved attributes changed by user
 * @parent: parent dentry for this file.
 * @data: something that the caller will want to get to later on.
 * @fop: struct file_operations that should be used for this file.
 *
 * This function creates a dentry that represents a file in the eventsfs_inode
 * directory. The inode.i_private pointer will point to @data in the open()
 * call.
 */
static struct dentry *create_file(const char *name, umode_t mode,
				  struct eventfs_attr *attr,
				  struct dentry *parent, void *data,
				  const struct file_operations *fop)
{
	struct tracefs_inode *ti;
	struct dentry *dentry;
	struct inode *inode;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	if (WARN_ON_ONCE(!S_ISREG(mode)))
		return NULL;

	WARN_ON_ONCE(!parent);
	dentry = eventfs_start_creating(name, parent);

	if (IS_ERR(dentry))
		return dentry;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return eventfs_failed_creating(dentry);

	/* If the user updated the directory's attributes, use them */
	update_inode_attr(inode, attr, mode);

	inode->i_op = &eventfs_file_inode_operations;
	inode->i_fop = fop;
	inode->i_private = data;

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE;
	d_instantiate(dentry, inode);
	fsnotify_create(dentry->d_parent->d_inode, dentry);
	return eventfs_end_creating(dentry);
};

/**
 * create_dir - create a dir in the tracefs filesystem
 * @ei: the eventfs_inode that represents the directory to create
 * @parent: parent dentry for this file.
 *
 * This function will create a dentry for a directory represented by
 * a eventfs_inode.
 */
static struct dentry *create_dir(struct eventfs_inode *ei, struct dentry *parent)
{
	struct tracefs_inode *ti;
	struct dentry *dentry;
	struct inode *inode;

	dentry = eventfs_start_creating(ei->name, parent);
	if (IS_ERR(dentry))
		return dentry;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return eventfs_failed_creating(dentry);

	/* If the user updated the directory's attributes, use them */
	update_inode_attr(inode, &ei->attr, S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO);

	inode->i_op = &eventfs_root_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE;

	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(dentry->d_parent->d_inode);
	fsnotify_mkdir(dentry->d_parent->d_inode, dentry);
	return eventfs_end_creating(dentry);
}

static void free_ei(struct eventfs_inode *ei)
{
	kfree_const(ei->name);
	kfree(ei->d_children);
	kfree(ei->entry_attrs);
	kfree(ei);
}

/**
 * eventfs_set_ei_status_free - remove the dentry reference from an eventfs_inode
 * @ti: the tracefs_inode of the dentry
 * @dentry: dentry which has the reference to remove.
 *
 * Remove the association between a dentry from an eventfs_inode.
 */
void eventfs_set_ei_status_free(struct tracefs_inode *ti, struct dentry *dentry)
{
	struct eventfs_inode *ei;
	int i;

	mutex_lock(&eventfs_mutex);

	ei = dentry->d_fsdata;
	if (!ei)
		goto out;

	/* This could belong to one of the files of the ei */
	if (ei->dentry != dentry) {
		for (i = 0; i < ei->nr_entries; i++) {
			if (ei->d_children[i] == dentry)
				break;
		}
		if (WARN_ON_ONCE(i == ei->nr_entries))
			goto out;
		ei->d_children[i] = NULL;
	} else if (ei->is_freed) {
		free_ei(ei);
	} else {
		ei->dentry = NULL;
	}

	dentry->d_fsdata = NULL;
 out:
	mutex_unlock(&eventfs_mutex);
}

/**
 * create_file_dentry - create a dentry for a file of an eventfs_inode
 * @ei: the eventfs_inode that the file will be created under
 * @idx: the index into the d_children[] of the @ei
 * @parent: The parent dentry of the created file.
 * @name: The name of the file to create
 * @mode: The mode of the file.
 * @data: The data to use to set the inode of the file with on open()
 * @fops: The fops of the file to be created.
 * @lookup: If called by the lookup routine, in which case, dput() the created dentry.
 *
 * Create a dentry for a file of an eventfs_inode @ei and place it into the
 * address located at @e_dentry. If the @e_dentry already has a dentry, then
 * just do a dget() on it and return. Otherwise create the dentry and attach it.
 */
static struct dentry *
create_file_dentry(struct eventfs_inode *ei, int idx,
		   struct dentry *parent, const char *name, umode_t mode, void *data,
		   const struct file_operations *fops, bool lookup)
{
	struct eventfs_attr *attr = NULL;
	struct dentry **e_dentry = &ei->d_children[idx];
	struct dentry *dentry;
	bool invalidate = false;

	mutex_lock(&eventfs_mutex);
	if (ei->is_freed) {
		mutex_unlock(&eventfs_mutex);
		return NULL;
	}
	/* If the e_dentry already has a dentry, use it */
	if (*e_dentry) {
		/* lookup does not need to up the ref count */
		if (!lookup)
			dget(*e_dentry);
		mutex_unlock(&eventfs_mutex);
		return *e_dentry;
	}

	/* ei->entry_attrs are protected by SRCU */
	if (ei->entry_attrs)
		attr = &ei->entry_attrs[idx];

	mutex_unlock(&eventfs_mutex);

	/* The lookup already has the parent->d_inode locked */
	if (!lookup)
		inode_lock(parent->d_inode);

	dentry = create_file(name, mode, attr, parent, data, fops);

	if (!lookup)
		inode_unlock(parent->d_inode);

	mutex_lock(&eventfs_mutex);

	if (IS_ERR_OR_NULL(dentry)) {
		/*
		 * When the mutex was released, something else could have
		 * created the dentry for this e_dentry. In which case
		 * use that one.
		 *
		 * Note, with the mutex held, the e_dentry cannot have content
		 * and the ei->is_freed be true at the same time.
		 */
		dentry = *e_dentry;
		if (WARN_ON_ONCE(dentry && ei->is_freed))
			dentry = NULL;
		/* The lookup does not need to up the dentry refcount */
		if (dentry && !lookup)
			dget(dentry);
		mutex_unlock(&eventfs_mutex);
		return dentry;
	}

	if (!*e_dentry && !ei->is_freed) {
		*e_dentry = dentry;
		dentry->d_fsdata = ei;
	} else {
		/*
		 * Should never happen unless we get here due to being freed.
		 * Otherwise it means two dentries exist with the same name.
		 */
		WARN_ON_ONCE(!ei->is_freed);
		invalidate = true;
	}
	mutex_unlock(&eventfs_mutex);

	if (invalidate)
		d_invalidate(dentry);

	if (lookup || invalidate)
		dput(dentry);

	return invalidate ? NULL : dentry;
}

/**
 * eventfs_post_create_dir - post create dir routine
 * @ei: eventfs_inode of recently created dir
 *
 * Map the meta-data of files within an eventfs dir to their parent dentry
 */
static void eventfs_post_create_dir(struct eventfs_inode *ei)
{
	struct eventfs_inode *ei_child;
	struct tracefs_inode *ti;

	lockdep_assert_held(&eventfs_mutex);

	/* srcu lock already held */
	/* fill parent-child relation */
	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		ei_child->d_parent = ei->dentry;
	}

	ti = get_tracefs(ei->dentry->d_inode);
	ti->private = ei;
}

/**
 * create_dir_dentry - Create a directory dentry for the eventfs_inode
 * @pei: The eventfs_inode parent of ei.
 * @ei: The eventfs_inode to create the directory for
 * @parent: The dentry of the parent of this directory
 * @lookup: True if this is called by the lookup code
 *
 * This creates and attaches a directory dentry to the eventfs_inode @ei.
 */
static struct dentry *
create_dir_dentry(struct eventfs_inode *pei, struct eventfs_inode *ei,
		  struct dentry *parent, bool lookup)
{
	bool invalidate = false;
	struct dentry *dentry = NULL;

	mutex_lock(&eventfs_mutex);
	if (pei->is_freed || ei->is_freed) {
		mutex_unlock(&eventfs_mutex);
		return NULL;
	}
	if (ei->dentry) {
		/* If the dentry already has a dentry, use it */
		dentry = ei->dentry;
		/* lookup does not need to up the ref count */
		if (!lookup)
			dget(dentry);
		mutex_unlock(&eventfs_mutex);
		return dentry;
	}
	mutex_unlock(&eventfs_mutex);

	/* The lookup already has the parent->d_inode locked */
	if (!lookup)
		inode_lock(parent->d_inode);

	dentry = create_dir(ei, parent);

	if (!lookup)
		inode_unlock(parent->d_inode);

	mutex_lock(&eventfs_mutex);

	if (IS_ERR_OR_NULL(dentry) && !ei->is_freed) {
		/*
		 * When the mutex was released, something else could have
		 * created the dentry for this e_dentry. In which case
		 * use that one.
		 *
		 * Note, with the mutex held, the e_dentry cannot have content
		 * and the ei->is_freed be true at the same time.
		 */
		dentry = ei->dentry;
		if (dentry && !lookup)
			dget(dentry);
		mutex_unlock(&eventfs_mutex);
		return dentry;
	}

	if (!ei->dentry && !ei->is_freed) {
		ei->dentry = dentry;
		eventfs_post_create_dir(ei);
		dentry->d_fsdata = ei;
	} else {
		/*
		 * Should never happen unless we get here due to being freed.
		 * Otherwise it means two dentries exist with the same name.
		 */
		WARN_ON_ONCE(!ei->is_freed);
		invalidate = true;
	}
	mutex_unlock(&eventfs_mutex);
	if (invalidate)
		d_invalidate(dentry);

	if (lookup || invalidate)
		dput(dentry);

	return invalidate ? NULL : dentry;
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
	const struct file_operations *fops;
	const struct eventfs_entry *entry;
	struct eventfs_inode *ei_child;
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	struct dentry *ei_dentry = NULL;
	struct dentry *ret = NULL;
	const char *name = dentry->d_name.name;
	bool created = false;
	umode_t mode;
	void *data;
	int idx;
	int i;
	int r;

	ti = get_tracefs(dir);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return NULL;

	/* Grab srcu to prevent the ei from going away */
	idx = srcu_read_lock(&eventfs_srcu);

	/*
	 * Grab the eventfs_mutex to consistent value from ti->private.
	 * This s
	 */
	mutex_lock(&eventfs_mutex);
	ei = READ_ONCE(ti->private);
	if (ei && !ei->is_freed)
		ei_dentry = READ_ONCE(ei->dentry);
	mutex_unlock(&eventfs_mutex);

	if (!ei || !ei_dentry)
		goto out;

	data = ei->data;

	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		if (strcmp(ei_child->name, name) != 0)
			continue;
		ret = simple_lookup(dir, dentry, flags);
		create_dir_dentry(ei, ei_child, ei_dentry, true);
		created = true;
		break;
	}

	if (created)
		goto out;

	for (i = 0; i < ei->nr_entries; i++) {
		entry = &ei->entries[i];
		if (strcmp(name, entry->name) == 0) {
			void *cdata = data;
			mutex_lock(&eventfs_mutex);
			/* If ei->is_freed, then the event itself may be too */
			if (!ei->is_freed)
				r = entry->callback(name, &mode, &cdata, &fops);
			else
				r = -1;
			mutex_unlock(&eventfs_mutex);
			if (r <= 0)
				continue;
			ret = simple_lookup(dir, dentry, flags);
			create_file_dentry(ei, i, ei_dentry, name, mode, cdata,
					   fops, true);
			break;
		}
	}
 out:
	srcu_read_unlock(&eventfs_srcu, idx);
	return ret;
}

struct dentry_list {
	void			*cursor;
	struct dentry		**dentries;
};

/**
 * eventfs_release - called to release eventfs file/dir
 * @inode: inode to be released
 * @file: file to be released (not used)
 */
static int eventfs_release(struct inode *inode, struct file *file)
{
	struct tracefs_inode *ti;
	struct dentry_list *dlist = file->private_data;
	void *cursor;
	int i;

	ti = get_tracefs(inode);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return -EINVAL;

	if (WARN_ON_ONCE(!dlist))
		return -EINVAL;

	for (i = 0; dlist->dentries && dlist->dentries[i]; i++) {
		dput(dlist->dentries[i]);
	}

	cursor = dlist->cursor;
	kfree(dlist->dentries);
	kfree(dlist);
	file->private_data = cursor;
	return dcache_dir_close(inode, file);
}

static int add_dentries(struct dentry ***dentries, struct dentry *d, int cnt)
{
	struct dentry **tmp;

	tmp = krealloc(*dentries, sizeof(d) * (cnt + 2), GFP_KERNEL);
	if (!tmp)
		return -1;
	tmp[cnt] = d;
	tmp[cnt + 1] = NULL;
	*dentries = tmp;
	return 0;
}

/**
 * dcache_dir_open_wrapper - eventfs open wrapper
 * @inode: not used
 * @file: dir to be opened (to create it's children)
 *
 * Used to dynamic create file/dir with-in @file, all the
 * file/dir will be created. If already created then references
 * will be increased
 */
static int dcache_dir_open_wrapper(struct inode *inode, struct file *file)
{
	const struct file_operations *fops;
	const struct eventfs_entry *entry;
	struct eventfs_inode *ei_child;
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	struct dentry_list *dlist;
	struct dentry **dentries = NULL;
	struct dentry *parent = file_dentry(file);
	struct dentry *d;
	struct inode *f_inode = file_inode(file);
	const char *name = parent->d_name.name;
	umode_t mode;
	void *data;
	int cnt = 0;
	int idx;
	int ret;
	int i;
	int r;

	ti = get_tracefs(f_inode);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return -EINVAL;

	if (WARN_ON_ONCE(file->private_data))
		return -EINVAL;

	idx = srcu_read_lock(&eventfs_srcu);

	mutex_lock(&eventfs_mutex);
	ei = READ_ONCE(ti->private);
	mutex_unlock(&eventfs_mutex);

	if (!ei) {
		srcu_read_unlock(&eventfs_srcu, idx);
		return -EINVAL;
	}


	data = ei->data;

	dlist = kmalloc(sizeof(*dlist), GFP_KERNEL);
	if (!dlist) {
		srcu_read_unlock(&eventfs_srcu, idx);
		return -ENOMEM;
	}

	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		d = create_dir_dentry(ei, ei_child, parent, false);
		if (d) {
			ret = add_dentries(&dentries, d, cnt);
			if (ret < 0)
				break;
			cnt++;
		}
	}

	for (i = 0; i < ei->nr_entries; i++) {
		void *cdata = data;
		entry = &ei->entries[i];
		name = entry->name;
		mutex_lock(&eventfs_mutex);
		/* If ei->is_freed, then the event itself may be too */
		if (!ei->is_freed)
			r = entry->callback(name, &mode, &cdata, &fops);
		else
			r = -1;
		mutex_unlock(&eventfs_mutex);
		if (r <= 0)
			continue;
		d = create_file_dentry(ei, i, parent, name, mode, cdata, fops, false);
		if (d) {
			ret = add_dentries(&dentries, d, cnt);
			if (ret < 0)
				break;
			cnt++;
		}
	}
	srcu_read_unlock(&eventfs_srcu, idx);
	ret = dcache_dir_open(inode, file);

	/*
	 * dcache_dir_open() sets file->private_data to a dentry cursor.
	 * Need to save that but also save all the dentries that were
	 * opened by this function.
	 */
	dlist->cursor = file->private_data;
	dlist->dentries = dentries;
	file->private_data = dlist;
	return ret;
}

/*
 * This just sets the file->private_data back to the cursor and back.
 */
static int dcache_readdir_wrapper(struct file *file, struct dir_context *ctx)
{
	struct dentry_list *dlist = file->private_data;
	int ret;

	file->private_data = dlist->cursor;
	ret = dcache_readdir(file, ctx);
	dlist->cursor = file->private_data;
	file->private_data = dlist;
	return ret;
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

	ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	if (!ei)
		return ERR_PTR(-ENOMEM);

	ei->name = kstrdup_const(name, GFP_KERNEL);
	if (!ei->name) {
		kfree(ei);
		return ERR_PTR(-ENOMEM);
	}

	if (size) {
		ei->d_children = kzalloc(sizeof(*ei->d_children) * size, GFP_KERNEL);
		if (!ei->d_children) {
			kfree_const(ei->name);
			kfree(ei);
			return ERR_PTR(-ENOMEM);
		}
	}

	ei->entries = entries;
	ei->nr_entries = size;
	ei->data = data;
	INIT_LIST_HEAD(&ei->children);
	INIT_LIST_HEAD(&ei->list);

	mutex_lock(&eventfs_mutex);
	if (!parent->is_freed) {
		list_add_tail(&ei->list, &parent->children);
		ei->d_parent = parent->dentry;
	}
	mutex_unlock(&eventfs_mutex);

	/* Was the parent freed? */
	if (list_empty(&ei->list)) {
		free_ei(ei);
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
	struct eventfs_inode *ei;
	struct tracefs_inode *ti;
	struct inode *inode;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (IS_ERR(dentry))
		return ERR_CAST(dentry);

	ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	if (!ei)
		goto fail_ei;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		goto fail;

	if (size) {
		ei->d_children = kzalloc(sizeof(*ei->d_children) * size, GFP_KERNEL);
		if (!ei->d_children)
			goto fail;
	}

	ei->dentry = dentry;
	ei->entries = entries;
	ei->nr_entries = size;
	ei->data = data;
	ei->name = kstrdup_const(name, GFP_KERNEL);
	if (!ei->name)
		goto fail;

	INIT_LIST_HEAD(&ei->children);
	INIT_LIST_HEAD(&ei->list);

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE | TRACEFS_EVENT_TOP_INODE;
	ti->private = ei;

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	inode->i_op = &eventfs_root_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;

	dentry->d_fsdata = ei;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(dentry->d_parent->d_inode);
	fsnotify_mkdir(dentry->d_parent->d_inode, dentry);
	tracefs_end_creating(dentry);

	return ei;

 fail:
	kfree(ei->d_children);
	kfree(ei);
 fail_ei:
	tracefs_failed_creating(dentry);
	return ERR_PTR(-ENOMEM);
}

static LLIST_HEAD(free_list);

static void eventfs_workfn(struct work_struct *work)
{
        struct eventfs_inode *ei, *tmp;
        struct llist_node *llnode;

	llnode = llist_del_all(&free_list);
        llist_for_each_entry_safe(ei, tmp, llnode, llist) {
		/* This dput() matches the dget() from unhook_dentry() */
		for (int i = 0; i < ei->nr_entries; i++) {
			if (ei->d_children[i])
				dput(ei->d_children[i]);
		}
		/* This should only get here if it had a dentry */
		if (!WARN_ON_ONCE(!ei->dentry))
			dput(ei->dentry);
        }
}

static DECLARE_WORK(eventfs_work, eventfs_workfn);

static void free_rcu_ei(struct rcu_head *head)
{
	struct eventfs_inode *ei = container_of(head, struct eventfs_inode, rcu);

	if (ei->dentry) {
		/* Do not free the ei until all references of dentry are gone */
		if (llist_add(&ei->llist, &free_list))
			queue_work(system_unbound_wq, &eventfs_work);
		return;
	}

	/* If the ei doesn't have a dentry, neither should its children */
	for (int i = 0; i < ei->nr_entries; i++) {
		WARN_ON_ONCE(ei->d_children[i]);
	}

	free_ei(ei);
}

static void unhook_dentry(struct dentry *dentry)
{
	if (!dentry)
		return;
	/*
	 * Need to add a reference to the dentry that is expected by
	 * simple_recursive_removal(), which will include a dput().
	 */
	dget(dentry);

	/*
	 * Also add a reference for the dput() in eventfs_workfn().
	 * That is required as that dput() will free the ei after
	 * the SRCU grace period is over.
	 */
	dget(dentry);
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

	if (!ei)
		return;
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
	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 lockdep_is_held(&eventfs_mutex)) {
		/* Children only have dentry if parent does */
		WARN_ON_ONCE(ei_child->dentry && !ei->dentry);
		eventfs_remove_rec(ei_child, level + 1);
	}


	ei->is_freed = 1;

	for (int i = 0; i < ei->nr_entries; i++) {
		if (ei->d_children[i]) {
			/* Children only have dentry if parent does */
			WARN_ON_ONCE(!ei->dentry);
			unhook_dentry(ei->d_children[i]);
		}
	}

	unhook_dentry(ei->dentry);

	list_del_rcu(&ei->list);
	call_srcu(&eventfs_srcu, &ei->rcu, free_rcu_ei);
}

/**
 * eventfs_remove_dir - remove eventfs dir or file from list
 * @ei: eventfs_inode to be removed.
 *
 * This function acquire the eventfs_mutex lock and call eventfs_remove_rec()
 */
void eventfs_remove_dir(struct eventfs_inode *ei)
{
	struct dentry *dentry;

	if (!ei)
		return;

	mutex_lock(&eventfs_mutex);
	dentry = ei->dentry;
	eventfs_remove_rec(ei, 0);
	mutex_unlock(&eventfs_mutex);

	/*
	 * If any of the ei children has a dentry, then the ei itself
	 * must have a dentry.
	 */
	if (dentry)
		simple_recursive_removal(dentry, NULL);
}

/**
 * eventfs_remove_events_dir - remove the top level eventfs directory
 * @ei: the event_inode returned by eventfs_create_events_dir().
 *
 * This function removes the events main directory
 */
void eventfs_remove_events_dir(struct eventfs_inode *ei)
{
	struct dentry *dentry;

	dentry = ei->dentry;
	eventfs_remove_dir(ei);

	/*
	 * Matches the dget() done by tracefs_start_creating()
	 * in eventfs_create_events_dir() when it the dentry was
	 * created. In other words, it's a normal dentry that
	 * sticks around while the other ei->dentry are created
	 * and destroyed dynamically.
	 */
	dput(dentry);
}
