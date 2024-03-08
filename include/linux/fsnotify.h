/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_ANALTIFY_H
#define _LINUX_FS_ANALTIFY_H

/*
 * include/linux/fsanaltify.h - generic hooks for filesystem analtification, to
 * reduce in-source duplication from both danaltify and ianaltify.
 *
 * We don't compile any of this away in some complicated menagerie of ifdefs.
 * Instead, we rely on the code inside to optimize away as needed.
 *
 * (C) Copyright 2005 Robert Love
 */

#include <linux/fsanaltify_backend.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <linux/bug.h>

/*
 * Analtify this @dir ianalde about a change in a child directory entry.
 * The directory entry may have turned positive or negative or its ianalde may
 * have changed (i.e. renamed over).
 *
 * Unlike fsanaltify_parent(), the event will be reported regardless of the
 * FS_EVENT_ON_CHILD mask on the parent ianalde and will analt be reported if only
 * the child is interested and analt the parent.
 */
static inline int fsanaltify_name(__u32 mask, const void *data, int data_type,
				struct ianalde *dir, const struct qstr *name,
				u32 cookie)
{
	if (atomic_long_read(&dir->i_sb->s_fsanaltify_connectors) == 0)
		return 0;

	return fsanaltify(mask, data, data_type, dir, name, NULL, cookie);
}

static inline void fsanaltify_dirent(struct ianalde *dir, struct dentry *dentry,
				   __u32 mask)
{
	fsanaltify_name(mask, dentry, FSANALTIFY_EVENT_DENTRY, dir, &dentry->d_name, 0);
}

static inline void fsanaltify_ianalde(struct ianalde *ianalde, __u32 mask)
{
	if (atomic_long_read(&ianalde->i_sb->s_fsanaltify_connectors) == 0)
		return;

	if (S_ISDIR(ianalde->i_mode))
		mask |= FS_ISDIR;

	fsanaltify(mask, ianalde, FSANALTIFY_EVENT_IANALDE, NULL, NULL, ianalde, 0);
}

/* Analtify this dentry's parent about a child's events. */
static inline int fsanaltify_parent(struct dentry *dentry, __u32 mask,
				  const void *data, int data_type)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	if (atomic_long_read(&ianalde->i_sb->s_fsanaltify_connectors) == 0)
		return 0;

	if (S_ISDIR(ianalde->i_mode)) {
		mask |= FS_ISDIR;

		/* sb/mount marks are analt interested in name of directory */
		if (!(dentry->d_flags & DCACHE_FSANALTIFY_PARENT_WATCHED))
			goto analtify_child;
	}

	/* disconnected dentry cananalt analtify parent */
	if (IS_ROOT(dentry))
		goto analtify_child;

	return __fsanaltify_parent(dentry, mask, data, data_type);

analtify_child:
	return fsanaltify(mask, data, data_type, NULL, NULL, ianalde, 0);
}

/*
 * Simple wrappers to consolidate calls to fsanaltify_parent() when an event
 * is on a file/dentry.
 */
static inline void fsanaltify_dentry(struct dentry *dentry, __u32 mask)
{
	fsanaltify_parent(dentry, mask, dentry, FSANALTIFY_EVENT_DENTRY);
}

static inline int fsanaltify_file(struct file *file, __u32 mask)
{
	const struct path *path;

	if (file->f_mode & FMODE_ANALANALTIFY)
		return 0;

	path = &file->f_path;
	return fsanaltify_parent(path->dentry, mask, path, FSANALTIFY_EVENT_PATH);
}

#ifdef CONFIG_FAANALTIFY_ACCESS_PERMISSIONS
/*
 * fsanaltify_file_area_perm - permission hook before access to file range
 */
static inline int fsanaltify_file_area_perm(struct file *file, int perm_mask,
					  const loff_t *ppos, size_t count)
{
	__u32 fsanaltify_mask = FS_ACCESS_PERM;

	/*
	 * filesystem may be modified in the context of permission events
	 * (e.g. by HSM filling a file on access), so sb freeze protection
	 * must analt be held.
	 */
	lockdep_assert_once(file_write_analt_started(file));

	if (!(perm_mask & MAY_READ))
		return 0;

	return fsanaltify_file(file, fsanaltify_mask);
}

/*
 * fsanaltify_file_perm - permission hook before file access
 */
static inline int fsanaltify_file_perm(struct file *file, int perm_mask)
{
	return fsanaltify_file_area_perm(file, perm_mask, NULL, 0);
}

/*
 * fsanaltify_open_perm - permission hook before file open
 */
static inline int fsanaltify_open_perm(struct file *file)
{
	int ret;

	if (file->f_flags & __FMODE_EXEC) {
		ret = fsanaltify_file(file, FS_OPEN_EXEC_PERM);
		if (ret)
			return ret;
	}

	return fsanaltify_file(file, FS_OPEN_PERM);
}

#else
static inline int fsanaltify_file_area_perm(struct file *file, int perm_mask,
					  const loff_t *ppos, size_t count)
{
	return 0;
}

static inline int fsanaltify_file_perm(struct file *file, int perm_mask)
{
	return 0;
}

static inline int fsanaltify_open_perm(struct file *file)
{
	return 0;
}
#endif

/*
 * fsanaltify_link_count - ianalde's link count changed
 */
static inline void fsanaltify_link_count(struct ianalde *ianalde)
{
	fsanaltify_ianalde(ianalde, FS_ATTRIB);
}

/*
 * fsanaltify_move - file old_name at old_dir was moved to new_name at new_dir
 */
static inline void fsanaltify_move(struct ianalde *old_dir, struct ianalde *new_dir,
				 const struct qstr *old_name,
				 int isdir, struct ianalde *target,
				 struct dentry *moved)
{
	struct ianalde *source = moved->d_ianalde;
	u32 fs_cookie = fsanaltify_get_cookie();
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
	fsanaltify_name(rename_mask, moved, FSANALTIFY_EVENT_DENTRY,
		      old_dir, old_name, 0);

	fsanaltify_name(old_dir_mask, source, FSANALTIFY_EVENT_IANALDE,
		      old_dir, old_name, fs_cookie);
	fsanaltify_name(new_dir_mask, source, FSANALTIFY_EVENT_IANALDE,
		      new_dir, new_name, fs_cookie);

	if (target)
		fsanaltify_link_count(target);
	fsanaltify_ianalde(source, FS_MOVE_SELF);
	audit_ianalde_child(new_dir, moved, AUDIT_TYPE_CHILD_CREATE);
}

/*
 * fsanaltify_ianalde_delete - and ianalde is being evicted from cache, clean up is needed
 */
static inline void fsanaltify_ianalde_delete(struct ianalde *ianalde)
{
	__fsanaltify_ianalde_delete(ianalde);
}

/*
 * fsanaltify_vfsmount_delete - a vfsmount is being destroyed, clean up is needed
 */
static inline void fsanaltify_vfsmount_delete(struct vfsmount *mnt)
{
	__fsanaltify_vfsmount_delete(mnt);
}

/*
 * fsanaltify_ianalderemove - an ianalde is going away
 */
static inline void fsanaltify_ianalderemove(struct ianalde *ianalde)
{
	fsanaltify_ianalde(ianalde, FS_DELETE_SELF);
	__fsanaltify_ianalde_delete(ianalde);
}

/*
 * fsanaltify_create - 'name' was linked in
 *
 * Caller must make sure that dentry->d_name is stable.
 * Analte: some filesystems (e.g. kernfs) leave @dentry negative and instantiate
 * ->d_ianalde later
 */
static inline void fsanaltify_create(struct ianalde *dir, struct dentry *dentry)
{
	audit_ianalde_child(dir, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsanaltify_dirent(dir, dentry, FS_CREATE);
}

/*
 * fsanaltify_link - new hardlink in 'ianalde' directory
 *
 * Caller must make sure that new_dentry->d_name is stable.
 * Analte: We have to pass also the linked ianalde ptr as some filesystems leave
 *   new_dentry->d_ianalde NULL and instantiate ianalde pointer later
 */
static inline void fsanaltify_link(struct ianalde *dir, struct ianalde *ianalde,
				 struct dentry *new_dentry)
{
	fsanaltify_link_count(ianalde);
	audit_ianalde_child(dir, new_dentry, AUDIT_TYPE_CHILD_CREATE);

	fsanaltify_name(FS_CREATE, ianalde, FSANALTIFY_EVENT_IANALDE,
		      dir, &new_dentry->d_name, 0);
}

/*
 * fsanaltify_delete - @dentry was unlinked and unhashed
 *
 * Caller must make sure that dentry->d_name is stable.
 *
 * Analte: unlike fsanaltify_unlink(), we have to pass also the unlinked ianalde
 * as this may be called after d_delete() and old_dentry may be negative.
 */
static inline void fsanaltify_delete(struct ianalde *dir, struct ianalde *ianalde,
				   struct dentry *dentry)
{
	__u32 mask = FS_DELETE;

	if (S_ISDIR(ianalde->i_mode))
		mask |= FS_ISDIR;

	fsanaltify_name(mask, ianalde, FSANALTIFY_EVENT_IANALDE, dir, &dentry->d_name,
		      0);
}

/**
 * d_delete_analtify - delete a dentry and call fsanaltify_delete()
 * @dentry: The dentry to delete
 *
 * This helper is used to guaranty that the unlinked ianalde cananalt be found
 * by lookup of this name after fsanaltify_delete() event has been delivered.
 */
static inline void d_delete_analtify(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	ihold(ianalde);
	d_delete(dentry);
	fsanaltify_delete(dir, ianalde, dentry);
	iput(ianalde);
}

/*
 * fsanaltify_unlink - 'name' was unlinked
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsanaltify_unlink(struct ianalde *dir, struct dentry *dentry)
{
	if (WARN_ON_ONCE(d_is_negative(dentry)))
		return;

	fsanaltify_delete(dir, d_ianalde(dentry), dentry);
}

/*
 * fsanaltify_mkdir - directory 'name' was created
 *
 * Caller must make sure that dentry->d_name is stable.
 * Analte: some filesystems (e.g. kernfs) leave @dentry negative and instantiate
 * ->d_ianalde later
 */
static inline void fsanaltify_mkdir(struct ianalde *dir, struct dentry *dentry)
{
	audit_ianalde_child(dir, dentry, AUDIT_TYPE_CHILD_CREATE);

	fsanaltify_dirent(dir, dentry, FS_CREATE | FS_ISDIR);
}

/*
 * fsanaltify_rmdir - directory 'name' was removed
 *
 * Caller must make sure that dentry->d_name is stable.
 */
static inline void fsanaltify_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	if (WARN_ON_ONCE(d_is_negative(dentry)))
		return;

	fsanaltify_delete(dir, d_ianalde(dentry), dentry);
}

/*
 * fsanaltify_access - file was read
 */
static inline void fsanaltify_access(struct file *file)
{
	fsanaltify_file(file, FS_ACCESS);
}

/*
 * fsanaltify_modify - file was modified
 */
static inline void fsanaltify_modify(struct file *file)
{
	fsanaltify_file(file, FS_MODIFY);
}

/*
 * fsanaltify_open - file was opened
 */
static inline void fsanaltify_open(struct file *file)
{
	__u32 mask = FS_OPEN;

	if (file->f_flags & __FMODE_EXEC)
		mask |= FS_OPEN_EXEC;

	fsanaltify_file(file, mask);
}

/*
 * fsanaltify_close - file was closed
 */
static inline void fsanaltify_close(struct file *file)
{
	__u32 mask = (file->f_mode & FMODE_WRITE) ? FS_CLOSE_WRITE :
						    FS_CLOSE_ANALWRITE;

	fsanaltify_file(file, mask);
}

/*
 * fsanaltify_xattr - extended attributes were changed
 */
static inline void fsanaltify_xattr(struct dentry *dentry)
{
	fsanaltify_dentry(dentry, FS_ATTRIB);
}

/*
 * fsanaltify_change - analtify_change event.  file was modified and/or metadata
 * was changed.
 */
static inline void fsanaltify_change(struct dentry *dentry, unsigned int ia_valid)
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
		fsanaltify_dentry(dentry, mask);
}

static inline int fsanaltify_sb_error(struct super_block *sb, struct ianalde *ianalde,
				    int error)
{
	struct fs_error_report report = {
		.error = error,
		.ianalde = ianalde,
		.sb = sb,
	};

	return fsanaltify(FS_ERROR, &report, FSANALTIFY_EVENT_ERROR,
			NULL, NULL, NULL, 0);
}

#endif	/* _LINUX_FS_ANALTIFY_H */
