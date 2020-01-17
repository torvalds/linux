/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_NOTIFY_H
#define _LINUX_FS_NOTIFY_H

/*
 * include/linux/fsyestify.h - generic hooks for filesystem yestification, to
 * reduce in-source duplication from both dyestify and iyestify.
 *
 * We don't compile any of this away in some complicated menagerie of ifdefs.
 * Instead, we rely on the code inside to optimize away as needed.
 *
 * (C) Copyright 2005 Robert Love
 */

#include <linux/fsyestify_backend.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <linux/bug.h>

/*
 * Notify this @dir iyesde about a change in the directory entry @dentry.
 *
 * Unlike fsyestify_parent(), the event will be reported regardless of the
 * FS_EVENT_ON_CHILD mask on the parent iyesde.
 */
static inline int fsyestify_dirent(struct iyesde *dir, struct dentry *dentry,
				  __u32 mask)
{
	return fsyestify(dir, mask, d_iyesde(dentry), FSNOTIFY_EVENT_INODE,
			&dentry->d_name, 0);
}

/* Notify this dentry's parent about a child's events. */
static inline int fsyestify_parent(const struct path *path,
				  struct dentry *dentry, __u32 mask)
{
	if (!dentry)
		dentry = path->dentry;

	return __fsyestify_parent(path, dentry, mask);
}

/*
 * Simple wrapper to consolidate calls fsyestify_parent()/fsyestify() when
 * an event is on a path.
 */
static inline int fsyestify_path(struct iyesde *iyesde, const struct path *path,
				__u32 mask)
{
	int ret = fsyestify_parent(path, NULL, mask);

	if (ret)
		return ret;
	return fsyestify(iyesde, mask, path, FSNOTIFY_EVENT_PATH, NULL, 0);
}

/* Simple call site for access decisions */
static inline int fsyestify_perm(struct file *file, int mask)
{
	int ret;
	const struct path *path = &file->f_path;
	struct iyesde *iyesde = file_iyesde(file);
	__u32 fsyestify_mask = 0;

	if (file->f_mode & FMODE_NONOTIFY)
		return 0;
	if (!(mask & (MAY_READ | MAY_OPEN)))
		return 0;
	if (mask & MAY_OPEN) {
		fsyestify_mask = FS_OPEN_PERM;

		if (file->f_flags & __FMODE_EXEC) {
			ret = fsyestify_path(iyesde, path, FS_OPEN_EXEC_PERM);

			if (ret)
				return ret;
		}
	} else if (mask & MAY_READ) {
		fsyestify_mask = FS_ACCESS_PERM;
	}

	if (S_ISDIR(iyesde->i_mode))
		fsyestify_mask |= FS_ISDIR;

	return fsyestify_path(iyesde, path, fsyestify_mask);
}

/*
 * fsyestify_link_count - iyesde's link count changed
 */
static inline void fsyestify_link_count(struct iyesde *iyesde)
{
	__u32 mask = FS_ATTRIB;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;

	fsyestify(iyesde, mask, iyesde, FSNOTIFY_EVENT_INODE, NULL, 0);
}

/*
 * fsyestify_move - file old_name at old_dir was moved to new_name at new_dir
 */
static inline void fsyestify_move(struct iyesde *old_dir, struct iyesde *new_dir,
				 const struct qstr *old_name,
				 int isdir, struct iyesde *target,
				 struct dentry *moved)
{
	struct iyesde *source = moved->d_iyesde;
	u32 fs_cookie = fsyestify_get_cookie();
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

	fsyestify(old_dir, old_dir_mask, source, FSNOTIFY_EVENT_INODE, old_name,
		 fs_cookie);
	fsyestify(new_dir, new_dir_mask, source, FSNOTIFY_EVENT_INODE, new_name,
		 fs_cookie);

	if (target)
		fsyestify_link_count(target);

	if (source)
		fsyestify(source, mask, source, FSNOTIFY_EVENT_INODE, NULL, 0);
	audit_iyesde_child(new_dir, moved, AUDIT_TYPE_CHILD_CREATE);
}

/*
 * fsyestify_iyesde_delete - and iyesde is being evicted from cache, clean up is needed
 */
static inline void fsyestify_iyesde_delete(struct iyesde *iyesde)
{
	__fsyestify_iyesde_delete(iyesde);
}

/*
 * fsyestify_vfsmount_delete - a vfsmount is being destroyed, clean up is needed
 */
static inline void fsyestify_vfsmount_delete(struct vfsmount *mnt)
{
	__fsyestify_vfsmount_delete(mnt);
}

/*
 * fsyestify_iyesderemove - an iyesde is going away
 */
static inline void fsyestify_iyesderemove(struct iyesde *iyesde)
{
	__u32 mask = FS_DELETE_SELF;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;

	fsyestify(iyesde, mask, iyesde, FSNOTIFY_EVENT_INODE, NULL, 0);
	__fsyestify_iyesde_delete(iyesde);
}

/*
 * fsyestify_create - 'name' was linked in
 */
static inline void fsyestify_create(struct iyesde *iyesde, struct dentry *dentry)
{
	audit_iyesde_child(iyesde, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsyestify_dirent(iyesde, dentry, FS_CREATE);
}

/*
 * fsyestify_link - new hardlink in 'iyesde' directory
 * Note: We have to pass also the linked iyesde ptr as some filesystems leave
 *   new_dentry->d_iyesde NULL and instantiate iyesde pointer later
 */
static inline void fsyestify_link(struct iyesde *dir, struct iyesde *iyesde, struct dentry *new_dentry)
{
	fsyestify_link_count(iyesde);
	audit_iyesde_child(dir, new_dentry, AUDIT_TYPE_CHILD_CREATE);

	fsyestify(dir, FS_CREATE, iyesde, FSNOTIFY_EVENT_INODE, &new_dentry->d_name, 0);
}

/*
 * fsyestify_unlink - 'name' was unlinked
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsyestify_unlink(struct iyesde *dir, struct dentry *dentry)
{
	/* Expected to be called before d_delete() */
	WARN_ON_ONCE(d_is_negative(dentry));

	fsyestify_dirent(dir, dentry, FS_DELETE);
}

/*
 * fsyestify_mkdir - directory 'name' was created
 */
static inline void fsyestify_mkdir(struct iyesde *iyesde, struct dentry *dentry)
{
	audit_iyesde_child(iyesde, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsyestify_dirent(iyesde, dentry, FS_CREATE | FS_ISDIR);
}

/*
 * fsyestify_rmdir - directory 'name' was removed
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsyestify_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	/* Expected to be called before d_delete() */
	WARN_ON_ONCE(d_is_negative(dentry));

	fsyestify_dirent(dir, dentry, FS_DELETE | FS_ISDIR);
}

/*
 * fsyestify_access - file was read
 */
static inline void fsyestify_access(struct file *file)
{
	const struct path *path = &file->f_path;
	struct iyesde *iyesde = file_iyesde(file);
	__u32 mask = FS_ACCESS;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;

	if (!(file->f_mode & FMODE_NONOTIFY))
		fsyestify_path(iyesde, path, mask);
}

/*
 * fsyestify_modify - file was modified
 */
static inline void fsyestify_modify(struct file *file)
{
	const struct path *path = &file->f_path;
	struct iyesde *iyesde = file_iyesde(file);
	__u32 mask = FS_MODIFY;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;

	if (!(file->f_mode & FMODE_NONOTIFY))
		fsyestify_path(iyesde, path, mask);
}

/*
 * fsyestify_open - file was opened
 */
static inline void fsyestify_open(struct file *file)
{
	const struct path *path = &file->f_path;
	struct iyesde *iyesde = file_iyesde(file);
	__u32 mask = FS_OPEN;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;
	if (file->f_flags & __FMODE_EXEC)
		mask |= FS_OPEN_EXEC;

	fsyestify_path(iyesde, path, mask);
}

/*
 * fsyestify_close - file was closed
 */
static inline void fsyestify_close(struct file *file)
{
	const struct path *path = &file->f_path;
	struct iyesde *iyesde = file_iyesde(file);
	fmode_t mode = file->f_mode;
	__u32 mask = (mode & FMODE_WRITE) ? FS_CLOSE_WRITE : FS_CLOSE_NOWRITE;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;

	if (!(file->f_mode & FMODE_NONOTIFY))
		fsyestify_path(iyesde, path, mask);
}

/*
 * fsyestify_xattr - extended attributes were changed
 */
static inline void fsyestify_xattr(struct dentry *dentry)
{
	struct iyesde *iyesde = dentry->d_iyesde;
	__u32 mask = FS_ATTRIB;

	if (S_ISDIR(iyesde->i_mode))
		mask |= FS_ISDIR;

	fsyestify_parent(NULL, dentry, mask);
	fsyestify(iyesde, mask, iyesde, FSNOTIFY_EVENT_INODE, NULL, 0);
}

/*
 * fsyestify_change - yestify_change event.  file was modified and/or metadata
 * was changed.
 */
static inline void fsyestify_change(struct dentry *dentry, unsigned int ia_valid)
{
	struct iyesde *iyesde = dentry->d_iyesde;
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
		if (S_ISDIR(iyesde->i_mode))
			mask |= FS_ISDIR;

		fsyestify_parent(NULL, dentry, mask);
		fsyestify(iyesde, mask, iyesde, FSNOTIFY_EVENT_INODE, NULL, 0);
	}
}

#endif	/* _LINUX_FS_NOTIFY_H */
