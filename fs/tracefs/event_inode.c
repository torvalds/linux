// SPDX-License-Identifier: GPL-2.0-only
/*
 *  event_inode.c - part of tracefs, a pseudo file system for activating tracing
 *
 *  Copyright (C) 2020-23 VMware Inc, author: Steven Rostedt (VMware) <rostedt@goodmis.org>
 *  Copyright (C) 2020-23 VMware Inc, author: Ajay Kaher <akaher@vmware.com>
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

struct eventfs_inode {
	struct list_head	e_top_files;
};

/*
 * struct eventfs_file - hold the properties of the eventfs files and
 *                       directories.
 * @name:	the name of the file or directory to create
 * @d_parent:   holds parent's dentry
 * @dentry:     once accessed holds dentry
 * @list:	file or directory to be added to parent directory
 * @ei:		list of files and directories within directory
 * @fop:	file_operations for file or directory
 * @iop:	inode_operations for file or directory
 * @data:	something that the caller will want to get to later on
 * @is_freed:	Flag set if the eventfs is on its way to be freed
 * @mode:	the permission that the file or directory should have
 * @uid:	saved uid if changed
 * @gid:	saved gid if changed
 */
struct eventfs_file {
	const char			*name;
	struct dentry			*d_parent;
	struct dentry			*dentry;
	struct list_head		list;
	struct eventfs_inode		*ei;
	const struct file_operations	*fop;
	const struct inode_operations	*iop;
	/*
	 * Union - used for deletion
	 * @llist:	for calling dput() if needed after RCU
	 * @rcu:	eventfs_file to delete in RCU
	 */
	union {
		struct llist_node	llist;
		struct rcu_head		rcu;
	};
	void				*data;
	unsigned int			is_freed:1;
	unsigned int			mode:31;
	kuid_t				uid;
	kgid_t				gid;
};

static DEFINE_MUTEX(eventfs_mutex);
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

static void update_attr(struct eventfs_file *ef, struct iattr *iattr)
{
	unsigned int ia_valid = iattr->ia_valid;

	if (ia_valid & ATTR_MODE) {
		ef->mode = (ef->mode & ~EVENTFS_MODE_MASK) |
			(iattr->ia_mode & EVENTFS_MODE_MASK) |
			EVENTFS_SAVE_MODE;
	}
	if (ia_valid & ATTR_UID) {
		ef->mode |= EVENTFS_SAVE_UID;
		ef->uid = iattr->ia_uid;
	}
	if (ia_valid & ATTR_GID) {
		ef->mode |= EVENTFS_SAVE_GID;
		ef->gid = iattr->ia_gid;
	}
}

static int eventfs_set_attr(struct mnt_idmap *idmap, struct dentry *dentry,
			     struct iattr *iattr)
{
	struct eventfs_file *ef;
	int ret;

	mutex_lock(&eventfs_mutex);
	ef = dentry->d_fsdata;
	if (ef->is_freed) {
		/* Do not allow changes if the event is about to be removed. */
		mutex_unlock(&eventfs_mutex);
		return -ENODEV;
	}

	ret = simple_setattr(idmap, dentry, iattr);
	if (!ret)
		update_attr(ef, iattr);
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

static void update_inode_attr(struct inode *inode, struct eventfs_file *ef)
{
	inode->i_mode = ef->mode & EVENTFS_MODE_MASK;

	if (ef->mode & EVENTFS_SAVE_UID)
		inode->i_uid = ef->uid;

	if (ef->mode & EVENTFS_SAVE_GID)
		inode->i_gid = ef->gid;
}

/**
 * create_file - create a file in the tracefs filesystem
 * @ef: the eventfs_file
 * @parent: parent dentry for this file.
 * @data: something that the caller will want to get to later on.
 * @fop: struct file_operations that should be used for this file.
 *
 * This is the basic "create a file" function for tracefs.  It allows for a
 * wide range of flexibility in creating a file.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the tracefs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If tracefs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
static struct dentry *create_file(struct eventfs_file *ef,
				  struct dentry *parent, void *data,
				  const struct file_operations *fop)
{
	struct tracefs_inode *ti;
	struct dentry *dentry;
	struct inode *inode;

	if (!(ef->mode & S_IFMT))
		ef->mode |= S_IFREG;

	if (WARN_ON_ONCE(!S_ISREG(ef->mode)))
		return NULL;

	dentry = eventfs_start_creating(ef->name, parent);

	if (IS_ERR(dentry))
		return dentry;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return eventfs_failed_creating(dentry);

	/* If the user updated the directory's attributes, use them */
	update_inode_attr(inode, ef);

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
 * @data: something that the caller will want to get to later on.
 *
 * This is the basic "create a dir" function for eventfs.  It allows for a
 * wide range of flexibility in creating a dir.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the tracefs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If tracefs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
static struct dentry *create_dir(struct eventfs_file *ef,
				 struct dentry *parent, void *data)
{
	struct tracefs_inode *ti;
	struct dentry *dentry;
	struct inode *inode;

	dentry = eventfs_start_creating(ef->name, parent);
	if (IS_ERR(dentry))
		return dentry;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return eventfs_failed_creating(dentry);

	update_inode_attr(inode, ef);

	inode->i_op = &eventfs_root_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;
	inode->i_private = data;

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE;

	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(dentry->d_parent->d_inode);
	fsnotify_mkdir(dentry->d_parent->d_inode, dentry);
	return eventfs_end_creating(dentry);
}

static void free_ef(struct eventfs_file *ef)
{
	kfree(ef->name);
	kfree(ef->ei);
	kfree(ef);
}

/**
 * eventfs_set_ef_status_free - set the ef->status to free
 * @ti: the tracefs_inode of the dentry
 * @dentry: dentry who's status to be freed
 *
 * eventfs_set_ef_status_free will be called if no more
 * references remain
 */
void eventfs_set_ef_status_free(struct tracefs_inode *ti, struct dentry *dentry)
{
	struct eventfs_inode *ei;
	struct eventfs_file *ef;

	/* The top level events directory may be freed by this */
	if (unlikely(ti->flags & TRACEFS_EVENT_TOP_INODE)) {
		mutex_lock(&eventfs_mutex);
		ei = ti->private;

		/* Nothing should access this, but just in case! */
		ti->private = NULL;
		mutex_unlock(&eventfs_mutex);

		ef = dentry->d_fsdata;
		if (ef)
			free_ef(ef);
		return;
	}

	mutex_lock(&eventfs_mutex);

	ef = dentry->d_fsdata;
	if (!ef)
		goto out;

	if (ef->is_freed) {
		free_ef(ef);
	} else {
		ef->dentry = NULL;
	}

	dentry->d_fsdata = NULL;
out:
	mutex_unlock(&eventfs_mutex);
}

/**
 * eventfs_post_create_dir - post create dir routine
 * @ef: eventfs_file of recently created dir
 *
 * Map the meta-data of files within an eventfs dir to their parent dentry
 */
static void eventfs_post_create_dir(struct eventfs_file *ef)
{
	struct eventfs_file *ef_child;
	struct tracefs_inode *ti;

	/* srcu lock already held */
	/* fill parent-child relation */
	list_for_each_entry_srcu(ef_child, &ef->ei->e_top_files, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		ef_child->d_parent = ef->dentry;
	}

	ti = get_tracefs(ef->dentry->d_inode);
	ti->private = ef->ei;
}

/**
 * create_dentry - helper function to create dentry
 * @ef: eventfs_file of file or directory to create
 * @parent: parent dentry
 * @lookup: true if called from lookup routine
 *
 * Used to create a dentry for file/dir, executes post dentry creation routine
 */
static struct dentry *
create_dentry(struct eventfs_file *ef, struct dentry *parent, bool lookup)
{
	bool invalidate = false;
	struct dentry *dentry;

	mutex_lock(&eventfs_mutex);
	if (ef->is_freed) {
		mutex_unlock(&eventfs_mutex);
		return NULL;
	}
	if (ef->dentry) {
		dentry = ef->dentry;
		/* On dir open, up the ref count */
		if (!lookup)
			dget(dentry);
		mutex_unlock(&eventfs_mutex);
		return dentry;
	}
	mutex_unlock(&eventfs_mutex);

	if (!lookup)
		inode_lock(parent->d_inode);

	if (ef->ei)
		dentry = create_dir(ef, parent, ef->data);
	else
		dentry = create_file(ef, parent, ef->data, ef->fop);

	if (!lookup)
		inode_unlock(parent->d_inode);

	mutex_lock(&eventfs_mutex);
	if (IS_ERR_OR_NULL(dentry)) {
		/* If the ef was already updated get it */
		dentry = ef->dentry;
		if (dentry && !lookup)
			dget(dentry);
		mutex_unlock(&eventfs_mutex);
		return dentry;
	}

	if (!ef->dentry && !ef->is_freed) {
		ef->dentry = dentry;
		if (ef->ei)
			eventfs_post_create_dir(ef);
		dentry->d_fsdata = ef;
	} else {
		/* A race here, should try again (unless freed) */
		invalidate = true;

		/*
		 * Should never happen unless we get here due to being freed.
		 * Otherwise it means two dentries exist with the same name.
		 */
		WARN_ON_ONCE(!ef->is_freed);
	}
	mutex_unlock(&eventfs_mutex);
	if (invalidate)
		d_invalidate(dentry);

	if (lookup || invalidate)
		dput(dentry);

	return invalidate ? NULL : dentry;
}

static bool match_event_file(struct eventfs_file *ef, const char *name)
{
	bool ret;

	mutex_lock(&eventfs_mutex);
	ret = !ef->is_freed && strcmp(ef->name, name) == 0;
	mutex_unlock(&eventfs_mutex);

	return ret;
}

/**
 * eventfs_root_lookup - lookup routine to create file/dir
 * @dir: in which a lookup is being done
 * @dentry: file/dir dentry
 * @flags: to pass as flags parameter to simple lookup
 *
 * Used to create a dynamic file/dir within @dir. Use the eventfs_inode
 * list of meta data to find the information needed to create the file/dir.
 */
static struct dentry *eventfs_root_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags)
{
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	struct eventfs_file *ef;
	struct dentry *ret = NULL;
	int idx;

	ti = get_tracefs(dir);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return NULL;

	ei = ti->private;
	idx = srcu_read_lock(&eventfs_srcu);
	list_for_each_entry_srcu(ef, &ei->e_top_files, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		if (!match_event_file(ef, dentry->d_name.name))
			continue;
		ret = simple_lookup(dir, dentry, flags);
		create_dentry(ef, ef->d_parent, true);
		break;
	}
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

/**
 * dcache_dir_open_wrapper - eventfs open wrapper
 * @inode: not used
 * @file: dir to be opened (to create its child)
 *
 * Used to dynamically create the file/dir within @file. @file is really a
 * directory and all the files/dirs of the children within @file will be
 * created. If any of the files/dirs have already been created, their
 * reference count will be incremented.
 */
static int dcache_dir_open_wrapper(struct inode *inode, struct file *file)
{
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	struct eventfs_file *ef;
	struct dentry_list *dlist;
	struct dentry **dentries = NULL;
	struct dentry *dentry = file_dentry(file);
	struct dentry *d;
	struct inode *f_inode = file_inode(file);
	int cnt = 0;
	int idx;
	int ret;

	ti = get_tracefs(f_inode);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return -EINVAL;

	if (WARN_ON_ONCE(file->private_data))
		return -EINVAL;

	dlist = kmalloc(sizeof(*dlist), GFP_KERNEL);
	if (!dlist)
		return -ENOMEM;

	ei = ti->private;
	idx = srcu_read_lock(&eventfs_srcu);
	list_for_each_entry_srcu(ef, &ei->e_top_files, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		d = create_dentry(ef, dentry, false);
		if (d) {
			struct dentry **tmp;


			tmp = krealloc(dentries, sizeof(d) * (cnt + 2), GFP_KERNEL);
			if (!tmp)
				break;
			tmp[cnt] = d;
			tmp[cnt + 1] = NULL;
			cnt++;
			dentries = tmp;
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
 * eventfs_prepare_ef - helper function to prepare eventfs_file
 * @name: the name of the file/directory to create.
 * @mode: the permission that the file should have.
 * @fop: struct file_operations that should be used for this file/directory.
 * @iop: struct inode_operations that should be used for this file/directory.
 * @data: something that the caller will want to get to later on. The
 *        inode.i_private pointer will point to this value on the open() call.
 *
 * This function allocates and fills the eventfs_file structure.
 */
static struct eventfs_file *eventfs_prepare_ef(const char *name, umode_t mode,
					const struct file_operations *fop,
					const struct inode_operations *iop,
					void *data)
{
	struct eventfs_file *ef;

	ef = kzalloc(sizeof(*ef), GFP_KERNEL);
	if (!ef)
		return ERR_PTR(-ENOMEM);

	ef->name = kstrdup(name, GFP_KERNEL);
	if (!ef->name) {
		kfree(ef);
		return ERR_PTR(-ENOMEM);
	}

	if (S_ISDIR(mode)) {
		ef->ei = kzalloc(sizeof(*ef->ei), GFP_KERNEL);
		if (!ef->ei) {
			kfree(ef->name);
			kfree(ef);
			return ERR_PTR(-ENOMEM);
		}
		INIT_LIST_HEAD(&ef->ei->e_top_files);
		ef->mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	} else {
		ef->ei = NULL;
		ef->mode = mode;
	}

	ef->iop = iop;
	ef->fop = fop;
	ef->data = data;
	return ef;
}

/**
 * eventfs_create_events_dir - create the trace event structure
 * @name: the name of the directory to create.
 * @parent: parent dentry for this file.  This should be a directory dentry
 *          if set.  If this parameter is NULL, then the directory will be
 *          created in the root of the tracefs filesystem.
 *
 * This function creates the top of the trace event directory.
 */
struct dentry *eventfs_create_events_dir(const char *name,
					 struct dentry *parent)
{
	struct dentry *dentry = tracefs_start_creating(name, parent);
	struct eventfs_inode *ei;
	struct tracefs_inode *ti;
	struct inode *inode;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (IS_ERR(dentry))
		return dentry;

	ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	if (!ei)
		return ERR_PTR(-ENOMEM);
	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode)) {
		kfree(ei);
		tracefs_failed_creating(dentry);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&ei->e_top_files);

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE | TRACEFS_EVENT_TOP_INODE;
	ti->private = ei;

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	inode->i_op = &eventfs_root_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(dentry->d_parent->d_inode);
	fsnotify_mkdir(dentry->d_parent->d_inode, dentry);
	return tracefs_end_creating(dentry);
}

/**
 * eventfs_add_subsystem_dir - add eventfs subsystem_dir to list to create later
 * @name: the name of the file to create.
 * @parent: parent dentry for this dir.
 *
 * This function adds eventfs subsystem dir to list.
 * And all these dirs are created on the fly when they are looked up,
 * and the dentry and inodes will be removed when they are done.
 */
struct eventfs_file *eventfs_add_subsystem_dir(const char *name,
					       struct dentry *parent)
{
	struct tracefs_inode *ti_parent;
	struct eventfs_inode *ei_parent;
	struct eventfs_file *ef;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (!parent)
		return ERR_PTR(-EINVAL);

	ti_parent = get_tracefs(parent->d_inode);
	ei_parent = ti_parent->private;

	ef = eventfs_prepare_ef(name, S_IFDIR, NULL, NULL, NULL);
	if (IS_ERR(ef))
		return ef;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ei_parent->e_top_files);
	ef->d_parent = parent;
	mutex_unlock(&eventfs_mutex);
	return ef;
}

/**
 * eventfs_add_dir - add eventfs dir to list to create later
 * @name: the name of the file to create.
 * @ef_parent: parent eventfs_file for this dir.
 *
 * This function adds eventfs dir to list.
 * And all these dirs are created on the fly when they are looked up,
 * and the dentry and inodes will be removed when they are done.
 */
struct eventfs_file *eventfs_add_dir(const char *name,
				     struct eventfs_file *ef_parent)
{
	struct eventfs_file *ef;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (!ef_parent)
		return ERR_PTR(-EINVAL);

	ef = eventfs_prepare_ef(name, S_IFDIR, NULL, NULL, NULL);
	if (IS_ERR(ef))
		return ef;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ef_parent->ei->e_top_files);
	ef->d_parent = ef_parent->dentry;
	mutex_unlock(&eventfs_mutex);
	return ef;
}

/**
 * eventfs_add_events_file - add the data needed to create a file for later reference
 * @name: the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: parent dentry for this file.
 * @data: something that the caller will want to get to later on.
 * @fop: struct file_operations that should be used for this file.
 *
 * This function is used to add the information needed to create a
 * dentry/inode within the top level events directory. The file created
 * will have the @mode permissions. The @data will be used to fill the
 * inode.i_private when the open() call is done. The dentry and inodes are
 * all created when they are referenced, and removed when they are no
 * longer referenced.
 */
int eventfs_add_events_file(const char *name, umode_t mode,
			 struct dentry *parent, void *data,
			 const struct file_operations *fop)
{
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	struct eventfs_file *ef;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return -ENODEV;

	if (!parent)
		return -EINVAL;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	if (!parent->d_inode)
		return -EINVAL;

	ti = get_tracefs(parent->d_inode);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return -EINVAL;

	ei = ti->private;
	ef = eventfs_prepare_ef(name, mode, fop, NULL, data);

	if (IS_ERR(ef))
		return -ENOMEM;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ei->e_top_files);
	ef->d_parent = parent;
	mutex_unlock(&eventfs_mutex);
	return 0;
}

/**
 * eventfs_add_file - add eventfs file to list to create later
 * @name: the name of the file to create.
 * @mode: the permission that the file should have.
 * @ef_parent: parent eventfs_file for this file.
 * @data: something that the caller will want to get to later on.
 * @fop: struct file_operations that should be used for this file.
 *
 * This function is used to add the information needed to create a
 * file within a subdirectory of the events directory. The file created
 * will have the @mode permissions. The @data will be used to fill the
 * inode.i_private when the open() call is done. The dentry and inodes are
 * all created when they are referenced, and removed when they are no
 * longer referenced.
 */
int eventfs_add_file(const char *name, umode_t mode,
		     struct eventfs_file *ef_parent,
		     void *data,
		     const struct file_operations *fop)
{
	struct eventfs_file *ef;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return -ENODEV;

	if (!ef_parent)
		return -EINVAL;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	ef = eventfs_prepare_ef(name, mode, fop, NULL, data);
	if (IS_ERR(ef))
		return -ENOMEM;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ef_parent->ei->e_top_files);
	ef->d_parent = ef_parent->dentry;
	mutex_unlock(&eventfs_mutex);
	return 0;
}

static LLIST_HEAD(free_list);

static void eventfs_workfn(struct work_struct *work)
{
        struct eventfs_file *ef, *tmp;
        struct llist_node *llnode;

	llnode = llist_del_all(&free_list);
        llist_for_each_entry_safe(ef, tmp, llnode, llist) {
		/* This should only get here if it had a dentry */
		if (!WARN_ON_ONCE(!ef->dentry))
			dput(ef->dentry);
        }
}

static DECLARE_WORK(eventfs_work, eventfs_workfn);

static void free_rcu_ef(struct rcu_head *head)
{
	struct eventfs_file *ef = container_of(head, struct eventfs_file, rcu);

	if (ef->dentry) {
		/* Do not free the ef until all references of dentry are gone */
		if (llist_add(&ef->llist, &free_list))
			queue_work(system_unbound_wq, &eventfs_work);
		return;
	}

	free_ef(ef);
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
 * @ef: eventfs_file to be removed.
 * @level: to check recursion depth
 *
 * The helper function eventfs_remove_rec() is used to clean up and free the
 * associated data from eventfs for both of the added functions.
 */
static void eventfs_remove_rec(struct eventfs_file *ef, int level)
{
	struct eventfs_file *ef_child;

	if (!ef)
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

	if (ef->ei) {
		/* search for nested folders or files */
		list_for_each_entry_srcu(ef_child, &ef->ei->e_top_files, list,
					 lockdep_is_held(&eventfs_mutex)) {
			eventfs_remove_rec(ef_child, level + 1);
		}
	}

	ef->is_freed = 1;

	unhook_dentry(ef->dentry);

	list_del_rcu(&ef->list);
	call_srcu(&eventfs_srcu, &ef->rcu, free_rcu_ef);
}

/**
 * eventfs_remove - remove eventfs dir or file from list
 * @ef: eventfs_file to be removed.
 *
 * This function acquire the eventfs_mutex lock and call eventfs_remove_rec()
 */
void eventfs_remove(struct eventfs_file *ef)
{
	struct dentry *dentry;

	if (!ef)
		return;

	mutex_lock(&eventfs_mutex);
	dentry = ef->dentry;
	eventfs_remove_rec(ef, 0);
	mutex_unlock(&eventfs_mutex);

	/*
	 * If any of the ei children has a dentry, then the ei itself
	 * must have a dentry.
	 */
	if (dentry)
		simple_recursive_removal(dentry, NULL);
}

/**
 * eventfs_remove_events_dir - remove eventfs dir or file from list
 * @dentry: events's dentry to be removed.
 *
 * This function remove events main directory
 */
void eventfs_remove_events_dir(struct dentry *dentry)
{
	struct eventfs_file *ef_child;
	struct eventfs_inode *ei;
	struct tracefs_inode *ti;

	if (!dentry || !dentry->d_inode)
		return;

	ti = get_tracefs(dentry->d_inode);
	if (!ti || !(ti->flags & TRACEFS_EVENT_INODE))
		return;

	mutex_lock(&eventfs_mutex);
	ei = ti->private;
	list_for_each_entry_srcu(ef_child, &ei->e_top_files, list,
				 lockdep_is_held(&eventfs_mutex)) {
		eventfs_remove_rec(ef_child, 0);
	}
	mutex_unlock(&eventfs_mutex);
}
