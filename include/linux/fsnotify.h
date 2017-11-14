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

/* Notify this dentry's parent about a child's events. */
static inline int fsnotify_parent(const struct path *path, struct dentry *dentry, __u32 mask)
{
	if (!dentry)
		dentry = path->dentry;

	return __fsnotify_parent(path, dentry, mask);
}

/* simple call site for access decisions */
static inline int fsnotify_perm(struct file *file, int mask)
{
	const struct path *path = &file->f_path;
	/*
	 * Do not use file_inode() here or anywhere in this file to get the
	 * inode.  That would break *notity on overlayfs.
	 */
	struct inode *inode = path->dentry->d_inode;
	__u32 fsnotify_mask = 0;
	int ret;

	if (file->f_mode & FMODE_NONOTIFY)
		return 0;
	if (!(mask & (MAY_READ | MAY_OPEN)))
		return 0;
	if (mask & MAY_OPEN)
		fsnotify_mask = FS_OPEN_PERM;
	else if (mask & MAY_READ)
		fsnotify_mask = FS_ACCESS_PERM;
	else
		BUG();

	ret = fsnotify_parent(path, NULL, fsnotify_mask);
	if (ret)
		return ret;

	return fsnotify(inode, fsnotify_mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
}

/*
 * fsnotify_link_count - inode's link count changed
 */
static inline void fsnotify_link_count(struct inode *inode)
{
	fsnotify(inode, FS_ATTRIB, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
}

/*
 * fsnotify_move - file old_name at old_dir was moved to new_name at new_dir
 */
static inline void fsnotify_move(struct inode *old_dir, struct inode *new_dir,
				 const unsigned char *old_name,
				 int isdir, struct inode *target, struct dentry *moved)
{
	struct inode *source = moved->d_inode;
	u32 fs_cookie = fsnotify_get_cookie();
	__u32 old_dir_mask = (FS_EVENT_ON_CHILD | FS_MOVED_FROM);
	__u32 new_dir_mask = (FS_EVENT_ON_CHILD | FS_MOVED_TO);
	const unsigned char *new_name = moved->d_name.name;

	if (old_dir == new_dir)
		old_dir_mask |= FS_DN_RENAME;

	if (isdir) {
		old_dir_mask |= FS_ISDIR;
		new_dir_mask |= FS_ISDIR;
	}

	fsnotify(old_dir, old_dir_mask, source, FSNOTIFY_EVENT_INODE, old_name,
		 fs_cookie);
	fsnotify(new_dir, new_dir_mask, source, FSNOTIFY_EVENT_INODE, new_name,
		 fs_cookie);

	if (target)
		fsnotify_link_count(target);

	if (source)
		fsnotify(source, FS_MOVE_SELF, moved->d_inode, FSNOTIFY_EVENT_INODE, NULL, 0);
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
 * fsnotify_nameremove - a filename was removed from a directory
 */
static inline void fsnotify_nameremove(struct dentry *dentry, int isdir)
{
	__u32 mask = FS_DELETE;

	if (isdir)
		mask |= FS_ISDIR;

	fsnotify_parent(NULL, dentry, mask);
}

/*
 * fsnotify_inoderemove - an inode is going away
 */
static inline void fsnotify_inoderemove(struct inode *inode)
{
	fsnotify(inode, FS_DELETE_SELF, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
	__fsnotify_inode_delete(inode);
}

/*
 * fsnotify_create - 'name' was linked in
 */
static inline void fsnotify_create(struct inode *inode, struct dentry *dentry)
{
	audit_inode_child(inode, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify(inode, FS_CREATE, dentry->d_inode, FSNOTIFY_EVENT_INODE, dentry->d_name.name, 0);
}

/*
 * fsnotify_link - new hardlink in 'inode' directory
 * Note: We have to pass also the linked inode ptr as some filesystems leave
 *   new_dentry->d_inode NULL and instantiate inode pointer later
 */
static inline void fsnotify_link(struct inode *dir, struct inode *inode, struct dentry *new_dentry)
{
	fsnotify_link_count(inode);
	audit_inode_child(dir, new_dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify(dir, FS_CREATE, inode, FSNOTIFY_EVENT_INODE, new_dentry->d_name.name, 0);
}

/*
 * fsnotify_mkdir - directory 'name' was created
 */
static inline void fsnotify_mkdir(struct inode *inode, struct dentry *dentry)
{
	__u32 mask = (FS_CREATE | FS_ISDIR);
	struct inode *d_inode = dentry->d_inode;

	audit_inode_child(inode, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify(inode, mask, d_inode, FSNOTIFY_EVENT_INODE, dentry->d_name.name, 0);
}

/*
 * fsnotify_access - file was read
 */
static inline void fsnotify_access(struct file *file)
{
	const struct path *path = &file->f_path;
	struct inode *inode = path->dentry->d_inode;
	__u32 mask = FS_ACCESS;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	if (!(file->f_mode & FMODE_NONOTIFY)) {
		fsnotify_parent(path, NULL, mask);
		fsnotify(inode, mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
	}
}

/*
 * fsnotify_modify - file was modified
 */
static inline void fsnotify_modify(struct file *file)
{
	const struct path *path = &file->f_path;
	struct inode *inode = path->dentry->d_inode;
	__u32 mask = FS_MODIFY;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	if (!(file->f_mode & FMODE_NONOTIFY)) {
		fsnotify_parent(path, NULL, mask);
		fsnotify(inode, mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
	}
}

/*
 * fsnotify_open - file was opened
 */
static inline void fsnotify_open(struct file *file)
{
	const struct path *path = &file->f_path;
	struct inode *inode = path->dentry->d_inode;
	__u32 mask = FS_OPEN;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify_parent(path, NULL, mask);
	fsnotify(inode, mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
}

/*
 * fsnotify_close - file was closed
 */
static inline void fsnotify_close(struct file *file)
{
	const struct path *path = &file->f_path;
	struct inode *inode = path->dentry->d_inode;
	fmode_t mode = file->f_mode;
	__u32 mask = (mode & FMODE_WRITE) ? FS_CLOSE_WRITE : FS_CLOSE_NOWRITE;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	if (!(file->f_mode & FMODE_NONOTIFY)) {
		fsnotify_parent(path, NULL, mask);
		fsnotify(inode, mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
	}
}

/*
 * fsnotify_xattr - extended attributes were changed
 */
static inline void fsnotify_xattr(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	__u32 mask = FS_ATTRIB;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify_parent(NULL, dentry, mask);
	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
}

/*
 * fsnotify_change - notify_change event.  file was modified and/or metadata
 * was changed.
 */
static inline void fsnotify_change(struct dentry *dentry, unsigned int ia_valid)
{
	struct inode *inode = dentry->d_inode;
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

	if (mask) {
		if (S_ISDIR(inode->i_mode))
			mask |= FS_ISDIR;

		fsnotify_parent(NULL, dentry, mask);
		fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
	}
}

#endif	/* _LINUX_FS_NOTIFY_H */
