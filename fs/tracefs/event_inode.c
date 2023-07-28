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

/**
 * struct eventfs_file - hold the properties of the eventfs files and
 *                       directories.
 * @name:	the name of the file or directory to create
 * @list:	file or directory to be added to parent directory
 * @ei:		list of files and directories within directory
 * @fop:	file_operations for file or directory
 * @iop:	inode_operations for file or directory
 * @data:	something that the caller will want to get to later on
 * @mode:	the permission that the file or directory should have
 */
struct eventfs_file {
	const char			*name;
	struct list_head		list;
	struct eventfs_inode		*ei;
	const struct file_operations	*fop;
	const struct inode_operations	*iop;
	void				*data;
	umode_t				mode;
};

static DEFINE_MUTEX(eventfs_mutex);

static const struct inode_operations eventfs_root_dir_inode_operations = {
};

static const struct file_operations eventfs_file_operations = {
};

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
	ti->flags |= TRACEFS_EVENT_INODE;
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

	if (!parent)
		return ERR_PTR(-EINVAL);

	ti_parent = get_tracefs(parent->d_inode);
	ei_parent = ti_parent->private;

	ef = eventfs_prepare_ef(name, S_IFDIR, NULL, NULL, NULL);
	if (IS_ERR(ef))
		return ef;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ei_parent->e_top_files);
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

	if (!ef_parent)
		return ERR_PTR(-EINVAL);

	ef = eventfs_prepare_ef(name, S_IFDIR, NULL, NULL, NULL);
	if (IS_ERR(ef))
		return ef;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ef_parent->ei->e_top_files);
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

	if (!ef_parent)
		return -EINVAL;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	ef = eventfs_prepare_ef(name, mode, fop, NULL, data);
	if (IS_ERR(ef))
		return -ENOMEM;

	mutex_lock(&eventfs_mutex);
	list_add_tail(&ef->list, &ef_parent->ei->e_top_files);
	mutex_unlock(&eventfs_mutex);
	return 0;
}
