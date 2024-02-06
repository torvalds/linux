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
 * @mode:	the permission that the file or directory should have
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
	 * @del_list:	list of eventfs_file to delete
	 * @rcu:	eventfs_file to delete in RCU
	 * @is_freed:	node is freed if one of the above is set
	 */
	union {
		struct list_head	del_list;
		struct rcu_head		rcu;
		unsigned long		is_freed;
	};
	void				*data;
	umode_t				mode;
};

static DEFINE_MUTEX(eventfs_mutex);
DEFINE_STATIC_SRCU(eventfs_srcu);

static struct dentry *eventfs_root_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags);
static int dcache_dir_open_wrapper(struct inode *inode, struct file *file);
static int dcache_readdir_wrapper(struct file *file, struct dir_context *ctx);
static int eventfs_release(struct inode *inode, struct file *file);

static const struct inode_operations eventfs_root_dir_inode_operations = {
	.lookup		= eventfs_root_lookup,
};

static const struct file_operations eventfs_file_operations = {
	.open		= dcache_dir_open_wrapper,
	.read		= generic_read_dir,
	.iterate_shared	= dcache_readdir_wrapper,
	.llseek		= generic_file_llseek,
	.release	= eventfs_release,
};

/**
 * create_file - create a file in the tracefs filesystem
 * @name: the name of the file to create.
 * @mode: the permission that the file should have.
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
static struct dentry *create_file(const char *name, umode_t mode,
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

	dentry = eventfs_start_creating(name, parent);

	if (IS_ERR(dentry))
		return dentry;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return eventfs_failed_creating(dentry);

	inode->i_mode = mode;
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
 * @name: the name of the file to create.
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
static struct dentry *create_dir(const char *name, struct dentry *parent, void *data)
{
	struct tracefs_inode *ti;
	struct dentry *dentry;
	struct inode *inode;

	dentry = eventfs_start_creating(name, parent);
	if (IS_ERR(dentry))
		return dentry;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return eventfs_failed_creating(dentry);

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
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
	struct tracefs_inode *ti_parent;
	struct eventfs_inode *ei;
	struct eventfs_file *ef, *tmp;

	/* The top level events directory may be freed by this */
	if (unlikely(ti->flags & TRACEFS_EVENT_TOP_INODE)) {
		LIST_HEAD(ef_del_list);

		mutex_lock(&eventfs_mutex);

		ei = ti->private;

		/* Record all the top level files */
		list_for_each_entry_srcu(ef, &ei->e_top_files, list,
					 lockdep_is_held(&eventfs_mutex)) {
			list_add_tail(&ef->del_list, &ef_del_list);
		}

		/* Nothing should access this, but just in case! */
		ti->private = NULL;

		mutex_unlock(&eventfs_mutex);

		/* Now safely free the top level files and their children */
		list_for_each_entry_safe(ef, tmp, &ef_del_list, del_list) {
			list_del(&ef->del_list);
			eventfs_remove(ef);
		}

		kfree(ei);
		return;
	}

	mutex_lock(&eventfs_mutex);

	ti_parent = get_tracefs(dentry->d_parent->d_inode);
	if (!ti_parent || !(ti_parent->flags & TRACEFS_EVENT_INODE))
		goto out;

	ef = dentry->d_fsdata;
	if (!ef)
		goto out;

	/*
	 * If ef was freed, then the LSB bit is set for d_fsdata.
	 * But this should not happen, as it should still have a
	 * ref count that prevents it. Warn in case it does.
	 */
	if (WARN_ON_ONCE((unsigned long)ef & 1))
		goto out;

	dentry->d_fsdata = NULL;
	ef->dentry = NULL;
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
		dentry = create_dir(ef->name, parent, ef->data);
	else
		dentry = create_file(ef->name, ef->mode, parent,
				     ef->data, ef->fop);

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
	} else {
		ef->ei = NULL;
	}

	ef->iop = iop;
	ef->fop = fop;
	ef->mode = mode;
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

static void free_ef(struct rcu_head *head)
{
	struct eventfs_file *ef = container_of(head, struct eventfs_file, rcu);

	kfree(ef->name);
	kfree(ef->ei);
	kfree(ef);
}

/**
 * eventfs_remove_rec - remove eventfs dir or file from list
 * @ef: eventfs_file to be removed.
 * @head: to create list of eventfs_file to be deleted
 * @level: to check recursion depth
 *
 * The helper function eventfs_remove_rec() is used to clean up and free the
 * associated data from eventfs for both of the added functions.
 */
static void eventfs_remove_rec(struct eventfs_file *ef, struct list_head *head, int level)
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
			eventfs_remove_rec(ef_child, head, level + 1);
		}
	}

	list_del_rcu(&ef->list);
	list_add_tail(&ef->del_list, head);
}

/**
 * eventfs_remove - remove eventfs dir or file from list
 * @ef: eventfs_file to be removed.
 *
 * This function acquire the eventfs_mutex lock and call eventfs_remove_rec()
 */
void eventfs_remove(struct eventfs_file *ef)
{
	struct eventfs_file *tmp;
	LIST_HEAD(ef_del_list);
	struct dentry *dentry_list = NULL;
	struct dentry *dentry;

	if (!ef)
		return;

	mutex_lock(&eventfs_mutex);
	eventfs_remove_rec(ef, &ef_del_list, 0);
	list_for_each_entry_safe(ef, tmp, &ef_del_list, del_list) {
		if (ef->dentry) {
			unsigned long ptr = (unsigned long)dentry_list;

			/* Keep the dentry from being freed yet */
			dget(ef->dentry);

			/*
			 * Paranoid: The dget() above should prevent the dentry
			 * from being freed and calling eventfs_set_ef_status_free().
			 * But just in case, set the link list LSB pointer to 1
			 * and have eventfs_set_ef_status_free() check that to
			 * make sure that if it does happen, it will not think
			 * the d_fsdata is an event_file.
			 *
			 * For this to work, no event_file should be allocated
			 * on a odd space, as the ef should always be allocated
			 * to be at least word aligned. Check for that too.
			 */
			WARN_ON_ONCE(ptr & 1);

			ef->dentry->d_fsdata = (void *)(ptr | 1);
			dentry_list = ef->dentry;
			ef->dentry = NULL;
		}
		call_srcu(&eventfs_srcu, &ef->rcu, free_ef);
	}
	mutex_unlock(&eventfs_mutex);

	while (dentry_list) {
		unsigned long ptr;

		dentry = dentry_list;
		ptr = (unsigned long)dentry->d_fsdata & ~1UL;
		dentry_list = (struct dentry *)ptr;
		dentry->d_fsdata = NULL;
		d_invalidate(dentry);
		mutex_lock(&eventfs_mutex);
		/* dentry should now have at least a single reference */
		WARN_ONCE((int)d_count(dentry) < 1,
			  "dentry %p less than one reference (%d) after invalidate\n",
			  dentry, d_count(dentry));
		mutex_unlock(&eventfs_mutex);
		dput(dentry);
	}
}

/**
 * eventfs_remove_events_dir - remove eventfs dir or file from list
 * @dentry: events's dentry to be removed.
 *
 * This function remove events main directory
 */
void eventfs_remove_events_dir(struct dentry *dentry)
{
	struct tracefs_inode *ti;

	if (!dentry || !dentry->d_inode)
		return;

	ti = get_tracefs(dentry->d_inode);
	if (!ti || !(ti->flags & TRACEFS_EVENT_INODE))
		return;

	d_invalidate(dentry);
	dput(dentry);
}
