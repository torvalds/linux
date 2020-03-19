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
 * FS_EVENT_ON_CHILD mask on the parent inode.
 */
static inline void fsnotify_name(struct inode *dir, __u32 mask,
				 struct inode *child,
				 const struct qstr *name, u32 cookie)
{
	fsnotify(dir, mask, child, FSNOTIFY_EVENT_INODE, name, cookie);
	/*
	 * Send another flavor of the event without child inode data and
	 * without the specific event type (e.g. FS_CREATE|FS_IS_DIR).
	 * The name is relative to the dir inode the event is reported to.
	 */
	fsnotify(dir, FS_DIR_MODIFY, dir, FSNOTIFY_EVENT_INODE, name, 0);
}

static inline void fsnotify_dirent(struct inode *dir, struct dentry *dentry,
				   __u32 mask)
{
	fsnotify_name(dir, mask, d_inode(dentry), &dentry->d_name, 0);
}

/*
 * Simple wrappers to consolidate calls fsnotify_parent()/fsnotify() when
 * an event is on a file/dentry.
 */
static inline void fsnotify_dentry(struct dentry *dentry, __u32 mask)
{
	struct inode *inode = d_inode(dentry);

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify_parent(dentry, mask, inode, FSNOTIFY_EVENT_INODE);
	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
}

static inline int fsnotify_file(struct file *file, __u32 mask)
{
	const struct path *path = &file->f_path;
	struct inode *inode = file_inode(file);
	int ret;

	if (file->f_mode & FMODE_NONOTIFY)
		return 0;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	ret = fsnotify_parent(path->dentry, mask, path, FSNOTIFY_EVENT_PATH);
	if (ret)
		return ret;

	return fsnotify(inode, mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
}

/* Simple call site for access decisions */
static inline int fsnotify_perm(struct file *file, int mask)
{
	int ret;
	__u32 fsnotify_mask = 0;

	if (!(mask & (MAY_READ | MAY_OPEN)))
		return 0;

	if (mask & MAY_OPEN) {
		fsnotify_mask = FS_OPEN_PERM;

		if (file->f_flags & __FMODE_EXEC) {
			ret = fsnotify_file(file, FS_OPEN_EXEC_PERM);

			if (ret)
				return ret;
		}
	} else if (mask & MAY_READ) {
		fsnotify_mask = FS_ACCESS_PERM;
	}

	return fsnotify_file(file, fsnotify_mask);
}

/*
 * fsnotify_link_count - inode's link count changed
 */
static inline void fsnotify_link_count(struct inode *inode)
{
	__u32 mask = FS_ATTRIB;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
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
	__u32 mask = FS_MOVE_SELF;
	const struct qstr *new_name = &moved->d_name;

	if (old_dir == new_dir)
		old_dir_mask |= FS_DN_RENAME;

	if (isdir) {
		old_dir_mask |= FS_ISDIR;
		new_dir_mask |= FS_ISDIR;
		mask |= FS_ISDIR;
	}

	fsnotify_name(old_dir, old_dir_mask, source, old_name, fs_cookie);
	fsnotify_name(new_dir, new_dir_mask, source, new_name, fs_cookie);

	if (target)
		fsnotify_link_count(target);

	if (source)
		fsnotify(source, mask, source, FSNOTIFY_EVENT_INODE, NULL, 0);
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
	__u32 mask = FS_DELETE_SELF;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_ISDIR;

	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE, NULL, 0);
	__fsnotify_inode_delete(inode);
}

/*
 * fsnotify_create - 'name' was linked in
 */
static inline void fsnotify_create(struct inode *inode, struct dentry *dentry)
{
	audit_inode_child(inode, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_dirent(inode, dentry, FS_CREATE);
}

/*
 * fsnotify_link - new hardlink in 'inode' directory
 * Note: We have to pass also the linked inode ptr as some filesystems leave
 *   new_dentry->d_inode NULL and instantiate inode pointer later
 */
static inline void fsnotify_link(struct inode *dir, struct inode *inode,
				 struct dentry *new_dentry)
{
	fsnotify_link_count(inode);
	audit_inode_child(dir, new_dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_name(dir, FS_CREATE, inode, &new_dentry->d_name, 0);
}

/*
 * fsnotify_unlink - 'name' was unlinked
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsnotify_unlink(struct inode *dir, struct dentry *dentry)
{
	/* Expected to be called before d_delete() */
	WARN_ON_ONCE(d_is_negative(dentry));

	fsnotify_dirent(dir, dentry, FS_DELETE);
}

/*
 * fsnotify_mkdir - directory 'name' was created
 */
static inline void fsnotify_mkdir(struct inode *inode, struct dentry *dentry)
{
	audit_inode_child(inode, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsnotify_dirent(inode, dentry, FS_CREATE | FS_ISDIR);
}

/*
 * fsnotify_rmdir - directory 'name' was removed
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsnotify_rmdir(struct inode *dir, struct dentry *dentry)
{
	/* Expected to be called before d_delete() */
	WARN_ON_ONCE(d_is_negative(dentry));

	fsnotify_dirent(dir, dentry, FS_DELETE | FS_ISDIR);
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

#endif	/* _LINUX_FS_NOTIFY_H */
