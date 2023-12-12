/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_NOTIFY_H
#define _LINUX_FS_NOTIFY_H

/*
 * include/linux/fsnotify.h - generic hooks for filesystem notification, to
 * reduce in-source duplication from both dnotify and inotify.
 *
 * We don't compile any of this away in some complicated menagerie of ifdefs.
 * Instead, we rely on the code inside to optimize away as needed.
 *
 * (C) Copyright 2005 Robert Love
 */

#include <linux/fsnotify_backend.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <linux/bug.h>

/*
 * Notify this @dir inode about a change in a child directory entry.
 * The directory entry may have turned positive or negative or its inode may
 * have changed (i.e. renamed over).
 *
 * Unlike fsnotify_parent(), the event will be reported regardless of the
 * FS_EVENT_ON_CHILD mask on the parent inode and will not be reported if only
 * the child is interested and not the parent.
 */
static inline int fsnotify_name(__u32 mask, const void *data, int data_type,
				struct inode *dir, const struct qstr *name,
				u32 cookie)
{
	if (atomic_long_read(&dir->i_sb->s_fsnotify_connectors) == 0)
		return 0;

	return fsnotify(mask, data, data_type, dir, name, NULL, cookie);
}

static inline void fsnotify_dirent(struct inode *dir, struct dentry *dentry,
				   __u32 mask)
{
	fsnotify_name(mask, dentry, FSNOTIFY_EVENT_DENTRY, dir, &dentry->d_name, 0);
}

static inline void fsnotify_inode(struct inode *inode, __u32 mask)
{
	if (atomic_long_read(&inode->i_sb->s_fsnotify_connectors) == 0)
		return;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify(mask, inode, FSNOTIFY_EVENT_INODE, NULL, NULL, inode, 0);
}

/* Notify this dentry's parent about a child's events. */
static inline int fsnotify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	struct inode *inode = d_inode(dentry);

	if (atomic_long_read(&inode->i_sb->s_fsnotify_connectors) == 0)
		return 0;

	if (S_ISDIR(inode->i_mode)) {
		mask |= FS_ISDIR;

		/* sb/mount marks are not interested in name of directory */
		if (!(dentry->d_flags & DCACHE_FSNOTIFY_PARENT_WATCHED))
			goto notify_child;
	}

	/* disconnected dentry cannot notify parent */
	if (IS_ROOT(dentry))
		goto notify_child;

	return __fsnotify_parent(dentry, mask, data, data_type);

notify_child:
	return fsnotify(mask, data, data_type, NULL, NULL, inode, 0);
}

/*
 * Simple wrappers to consolidate calls to fsnotify_parent() when an event
 * is on a file/dentry.
 */
static inline void fsnotify_dentry(struct dentry *dentry, __u32 mask)
{
	fsnotify_parent(dentry, mask, dentry, FSNOTIFY_EVENT_DENTRY);
}

static inline int fsnotify_file(struct file *file, __u32 mask)
{
	const struct path *path;

	if (file->f_mode & FMODE_NONOTIFY)
		return 0;

	path = &file->f_path;
	return fsnotify_parent(path->dentry, mask, path, FSNOTIFY_EVENT_PATH);
}

/*
 * fsnotify_file_area_perm - permission hook before access to file range
 */
static inline int fsnotify_file_area_perm(struct file *file, int perm_mask,
					  const loff_t *ppos, size_t count)
{
	__u32 fsnotify_mask = FS_ACCESS_PERM;

	/*
	 * filesystem may be modified in the context of permission events
	 * (e.g. by HSM filling a file on access), so sb freeze protection
	 * must not be held.
	 */
	lockdep_assert_once(file_write_not_started(file));

	if (!(perm_mask & MAY_READ))
		return 0;

	return fsnotify_file(file, fsnotify_mask);
}

/*
 * fsnotify_file_perm - permission hook before file access
 */
static inline int fsnotify_file_perm(struct file *file, int perm_mask)
{
	return fsnotify_file_area_perm(file, perm_mask, NULL, 0);
}

/*
 * fsnotify_open_perm - permission hook before file open
 */
static inline int fsnotify_open_perm(struct file *file)
{
	int ret;

	if (file->f_flags & __FMODE_EXEC) {
		ret = fsnotify_file(file, FS_OPEN_EXEC_PERM);
		if (ret)
			return ret;
	}

	return fsnotify_file(file, FS_OPEN_PERM);
}

/*
 * fsnotify_link_count - inode's link count changed
 */
static inline void fsnotify_link_count(struct inode *inode)
{
	fsnotify_inode(inode, FS_ATTRIB);
}

/*
 * fsnotify_move - file old_name at old_dir was moved to new_name at new_dir
 */
static inline void fsnotify_move(struct inode *old_dir, struct inode *new_dir,
				 const struct qstr *old_name,
				 int isdir, struct inode *target,
				 struct dentry *moved)
{
	struct inode *source = moved->d_inode;
	u32 fs_cookie = fsnotify_get_cookie();
	__u32 old_dir_mask = FS_MOVED_FROM;
	__u32 new_dir_mask = FS_MOVED_TO;
	__u32 rename_mask = FS_RENAME;
	const struct qstr *new_name = &moved->d_name;

	if (isdir) {
		old_dir_mask |= FS_ISDIR;
		new_dir_mask |= FS_ISDIR;
		rename_mask |= FS_ISDIR;
	}

	/* Event with information about both old and new parent+name */
	fsnotify_name(rename_mask, moved, FSNOTIFY_EVENT_DENTRY,
		      old_dir, old_name, 0);

	fsnotify_name(old_dir_mask, source, FSNOTIFY_EVENT_INODE,
		      old_dir, old_name, fs_cookie);
	fsnotify_name(new_dir_mask, source, FSNOTIFY_EVENT_INODE,
		      new_dir, new_name, fs_cookie);

	if (target)
		fsnotify_link_count(target);
	fsnotify_inode(source, FS_MOVE_SELF);
	audit_inode_child(new_dir, moved, AUDIT_TYPE_CHILD_CREATE);
}

/*
 * fsnotify_inode_delete - and inode is being evicted from cache, clean up is needed
 */
static inline void fsnotify_inode_delete(struct inode *inode)
{
	__fsnotify_inode_delete(inode);
}

/*
 * fsnotify_vfsmount_delete - a vfsmount is being destroyed, clean up is needed
 */
static inline void fsnotify_vfsmount_delete(struct vfsmount *mnt)
{
	__fsnotify_vfsmount_delete(mnt);
}

/*
 * fsnotify_inoderemove - an inode is going away
 */
static inline void fsnotify_inoderemove(struct inode *inode)
{
	fsnotify_inode(inode, FS_DELETE_SELF);
	__fsnotify_inode_delete(inode);
}

/*
 * fsnotify_create - 'name' was linked in
 *
 * Caller must make sure that dentry->d_name is stable.
 * Note: some filesystems (e.g. kernfs) leave @dentry negative and instantiate
 * ->d_inode later
 */
static inline void fsnotify_create(struct inode *dir, struct dentry *dentry)
{
	audit_inode_child(dir, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_dirent(dir, dentry, FS_CREATE);
}

/*
 * fsnotify_link - new hardlink in 'inode' directory
 *
 * Caller must make sure that new_dentry->d_name is stable.
 * Note: We have to pass also the linked inode ptr as some filesystems leave
 *   new_dentry->d_inode NULL and instantiate inode pointer later
 */
static inline void fsnotify_link(struct inode *dir, struct inode *inode,
				 struct dentry *new_dentry)
{
	fsnotify_link_count(inode);
	audit_inode_child(dir, new_dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_name(FS_CREATE, inode, FSNOTIFY_EVENT_INODE,
		      dir, &new_dentry->d_name, 0);
}

/*
 * fsnotify_delete - @dentry was unlinked and unhashed
 *
 * Caller must make sure that dentry->d_name is stable.
 *
 * Note: unlike fsnotify_unlink(), we have to pass also the unlinked inode
 * as this may be called after d_delete() and old_dentry may be negative.
 */
static inline void fsnotify_delete(struct inode *dir, struct inode *inode,
				   struct dentry *dentry)
{
	__u32 mask = FS_DELETE;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify_name(mask, inode, FSNOTIFY_EVENT_INODE, dir, &dentry->d_name,
		      0);
}

/**
 * d_delete_notify - delete a dentry and call fsnotify_delete()
 * @dentry: The dentry to delete
 *
 * This helper is used to guaranty that the unlinked inode cannot be found
 * by lookup of this name after fsnotify_delete() event has been delivered.
 */
static inline void d_delete_notify(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	ihold(inode);
	d_delete(dentry);
	fsnotify_delete(dir, inode, dentry);
	iput(inode);
}

/*
 * fsnotify_unlink - 'name' was unlinked
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsnotify_unlink(struct inode *dir, struct dentry *dentry)
{
	if (WARN_ON_ONCE(d_is_negative(dentry)))
		return;

	fsnotify_delete(dir, d_inode(dentry), dentry);
}

/*
 * fsnotify_mkdir - directory 'name' was created
 *
 * Caller must make sure that dentry->d_name is stable.
 * Note: some filesystems (e.g. kernfs) leave @dentry negative and instantiate
 * ->d_inode later
 */
static inline void fsnotify_mkdir(struct inode *dir, struct dentry *dentry)
{
	audit_inode_child(dir, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_dirent(dir, dentry, FS_CREATE | FS_ISDIR);
}

/*
 * fsnotify_rmdir - directory 'name' was removed
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsnotify_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (WARN_ON_ONCE(d_is_negative(dentry)))
		return;

	fsnotify_delete(dir, d_inode(dentry), dentry);
}

/*
 * fsnotify_access - file was read
 */
static inline void fsnotify_access(struct file *file)
{
	fsnotify_file(file, FS_ACCESS);
}

/*
 * fsnotify_modify - file was modified
 */
static inline void fsnotify_modify(struct file *file)
{
	fsnotify_file(file, FS_MODIFY);
}

/*
 * fsnotify_open - file was opened
 */
static inline void fsnotify_open(struct file *file)
{
	__u32 mask = FS_OPEN;

	if (file->f_flags & __FMODE_EXEC)
		mask |= FS_OPEN_EXEC;

	fsnotify_file(file, mask);
}

/*
 * fsnotify_close - file was closed
 */
static inline void fsnotify_close(struct file *file)
{
	__u32 mask = (file->f_mode & FMODE_WRITE) ? FS_CLOSE_WRITE :
						    FS_CLOSE_NOWRITE;

	fsnotify_file(file, mask);
}

/*
 * fsnotify_xattr - extended attributes were changed
 */
static inline void fsnotify_xattr(struct dentry *dentry)
{
	fsnotify_dentry(dentry, FS_ATTRIB);
}

/*
 * fsnotify_change - notify_change event.  file was modified and/or metadata
 * was changed.
 */
static inline void fsnotify_change(struct dentry *dentry, unsigned int ia_valid)
{
	__u32 mask = 0;

	if (ia_valid & ATTR_UID)
		mask |= FS_ATTRIB;
	if (ia_valid & ATTR_GID)
		mask |= FS_ATTRIB;
	if (ia_valid & ATTR_SIZE)
		mask |= FS_MODIFY;

	/* both times implies a utime(s) call */
	if ((ia_valid & (ATTR_ATIME | ATTR_MTIME)) == (ATTR_ATIME | ATTR_MTIME))
		mask |= FS_ATTRIB;
	else if (ia_valid & ATTR_ATIME)
		mask |= FS_ACCESS;
	else if (ia_valid & ATTR_MTIME)
		mask |= FS_MODIFY;

	if (ia_valid & ATTR_MODE)
		mask |= FS_ATTRIB;

	if (mask)
		fsnotify_dentry(dentry, mask);
}

static inline int fsnotify_sb_error(struct super_block *sb, struct inode *inode,
				    int error)
{
	struct fs_error_report report = {
		.error = error,
		.inode = inode,
		.sb = sb,
	};

	return fsnotify(FS_ERROR, &report, FSNOTIFY_EVENT_ERROR,
			NULL, NULL, NULL, 0);
}

#endif	/* _LINUX_FS_NOTIFY_H */
