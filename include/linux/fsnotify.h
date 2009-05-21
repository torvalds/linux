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

#include <linux/dnotify.h>
#include <linux/inotify.h>
#include <linux/fsnotify_backend.h>
#include <linux/audit.h>

/*
 * fsnotify_d_instantiate - instantiate a dentry for inode
 * Called with dcache_lock held.
 */
static inline void fsnotify_d_instantiate(struct dentry *entry,
						struct inode *inode)
{
	inotify_d_instantiate(entry, inode);
}

/*
 * fsnotify_d_move - entry has been moved
 * Called with dcache_lock and entry->d_lock held.
 */
static inline void fsnotify_d_move(struct dentry *entry)
{
	inotify_d_move(entry);
}

/*
 * fsnotify_link_count - inode's link count changed
 */
static inline void fsnotify_link_count(struct inode *inode)
{
	inotify_inode_queue_event(inode, IN_ATTRIB, 0, NULL, NULL);

	fsnotify(inode, FS_ATTRIB, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_move - file old_name at old_dir was moved to new_name at new_dir
 */
static inline void fsnotify_move(struct inode *old_dir, struct inode *new_dir,
				 const char *old_name, const char *new_name,
				 int isdir, struct inode *target, struct dentry *moved)
{
	struct inode *source = moved->d_inode;
	u32 cookie = inotify_get_cookie();
	__u32 old_dir_mask = 0;
	__u32 new_dir_mask = 0;

	if (old_dir == new_dir) {
		inode_dir_notify(old_dir, DN_RENAME);
		old_dir_mask = FS_DN_RENAME;
	} else {
		inode_dir_notify(old_dir, DN_DELETE);
		old_dir_mask = FS_DELETE;
		inode_dir_notify(new_dir, DN_CREATE);
		new_dir_mask = FS_CREATE;
	}

	if (isdir) {
		isdir = IN_ISDIR;
		old_dir_mask |= FS_IN_ISDIR;
		new_dir_mask |= FS_IN_ISDIR;
	}

	old_dir_mask |= FS_MOVED_FROM;
	new_dir_mask |= FS_MOVED_TO;

	inotify_inode_queue_event(old_dir, IN_MOVED_FROM|isdir,cookie,old_name,
				  source);
	inotify_inode_queue_event(new_dir, IN_MOVED_TO|isdir, cookie, new_name,
				  source);

	fsnotify(old_dir, old_dir_mask, old_dir, FSNOTIFY_EVENT_INODE);
	fsnotify(new_dir, new_dir_mask, new_dir, FSNOTIFY_EVENT_INODE);

	if (target) {
		inotify_inode_queue_event(target, IN_DELETE_SELF, 0, NULL, NULL);
		inotify_inode_is_dead(target);

		/* this is really a link_count change not a removal */
		fsnotify_link_count(target);
	}

	if (source) {
		inotify_inode_queue_event(source, IN_MOVE_SELF, 0, NULL, NULL);
		fsnotify(source, FS_MOVE_SELF, moved->d_inode, FSNOTIFY_EVENT_INODE);
	}
	audit_inode_child(new_name, moved, new_dir);
}

/*
 * fsnotify_nameremove - a filename was removed from a directory
 */
static inline void fsnotify_nameremove(struct dentry *dentry, int isdir)
{
	__u32 mask = FS_DELETE;

	if (isdir)
		mask |= FS_IN_ISDIR;
	dnotify_parent(dentry, DN_DELETE);
	inotify_dentry_parent_queue_event(dentry, mask, 0, dentry->d_name.name);
}

/*
 * fsnotify_inoderemove - an inode is going away
 */
static inline void fsnotify_inoderemove(struct inode *inode)
{
	inotify_inode_queue_event(inode, IN_DELETE_SELF, 0, NULL, NULL);
	inotify_inode_is_dead(inode);

	fsnotify(inode, FS_DELETE_SELF, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_create - 'name' was linked in
 */
static inline void fsnotify_create(struct inode *inode, struct dentry *dentry)
{
	inode_dir_notify(inode, DN_CREATE);
	inotify_inode_queue_event(inode, IN_CREATE, 0, dentry->d_name.name,
				  dentry->d_inode);
	audit_inode_child(dentry->d_name.name, dentry, inode);

	fsnotify(inode, FS_CREATE, dentry->d_inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_link - new hardlink in 'inode' directory
 * Note: We have to pass also the linked inode ptr as some filesystems leave
 *   new_dentry->d_inode NULL and instantiate inode pointer later
 */
static inline void fsnotify_link(struct inode *dir, struct inode *inode, struct dentry *new_dentry)
{
	inode_dir_notify(dir, DN_CREATE);
	inotify_inode_queue_event(dir, IN_CREATE, 0, new_dentry->d_name.name,
				  inode);
	fsnotify_link_count(inode);
	audit_inode_child(new_dentry->d_name.name, new_dentry, dir);

	fsnotify(dir, FS_CREATE, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_mkdir - directory 'name' was created
 */
static inline void fsnotify_mkdir(struct inode *inode, struct dentry *dentry)
{
	__u32 mask = (FS_CREATE | FS_IN_ISDIR);
	struct inode *d_inode = dentry->d_inode;

	inode_dir_notify(inode, DN_CREATE);
	inotify_inode_queue_event(inode, mask, 0, dentry->d_name.name, d_inode);
	audit_inode_child(dentry->d_name.name, dentry, inode);

	fsnotify(inode, mask, d_inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_access - file was read
 */
static inline void fsnotify_access(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	__u32 mask = FS_ACCESS;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_IN_ISDIR;

	dnotify_parent(dentry, DN_ACCESS);
	inotify_dentry_parent_queue_event(dentry, mask, 0, dentry->d_name.name);
	inotify_inode_queue_event(inode, mask, 0, NULL, NULL);

	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_modify - file was modified
 */
static inline void fsnotify_modify(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	__u32 mask = FS_MODIFY;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_IN_ISDIR;

	dnotify_parent(dentry, DN_MODIFY);
	inotify_dentry_parent_queue_event(dentry, mask, 0, dentry->d_name.name);
	inotify_inode_queue_event(inode, mask, 0, NULL, NULL);

	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_open - file was opened
 */
static inline void fsnotify_open(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	__u32 mask = FS_OPEN;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_IN_ISDIR;

	inotify_dentry_parent_queue_event(dentry, mask, 0, dentry->d_name.name);
	inotify_inode_queue_event(inode, mask, 0, NULL, NULL);

	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_close - file was closed
 */
static inline void fsnotify_close(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	const char *name = dentry->d_name.name;
	fmode_t mode = file->f_mode;
	__u32 mask = (mode & FMODE_WRITE) ? FS_CLOSE_WRITE : FS_CLOSE_NOWRITE;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_IN_ISDIR;

	inotify_dentry_parent_queue_event(dentry, mask, 0, name);
	inotify_inode_queue_event(inode, mask, 0, NULL, NULL);

	fsnotify(inode, mask, file, FSNOTIFY_EVENT_FILE);
}

/*
 * fsnotify_xattr - extended attributes were changed
 */
static inline void fsnotify_xattr(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	__u32 mask = FS_ATTRIB;

	if (S_ISDIR(inode->i_mode))
		mask |= FS_IN_ISDIR;

	inotify_dentry_parent_queue_event(dentry, mask, 0, dentry->d_name.name);
	inotify_inode_queue_event(inode, mask, 0, NULL, NULL);

	fsnotify(inode, mask, inode, FSNOTIFY_EVENT_INODE);
}

/*
 * fsnotify_change - notify_change event.  file was modified and/or metadata
 * was changed.
 */
static inline void fsnotify_change(struct dentry *dentry, unsigned int ia_valid)
{
	struct inode *inode = dentry->d_inode;
	int dn_mask = 0;
	__u32 in_mask = 0;

	if (ia_valid & ATTR_UID) {
		in_mask |= FS_ATTRIB;
		dn_mask |= DN_ATTRIB;
	}
	if (ia_valid & ATTR_GID) {
		in_mask |= FS_ATTRIB;
		dn_mask |= DN_ATTRIB;
	}
	if (ia_valid & ATTR_SIZE) {
		in_mask |= FS_MODIFY;
		dn_mask |= DN_MODIFY;
	}
	/* both times implies a utime(s) call */
	if ((ia_valid & (ATTR_ATIME | ATTR_MTIME)) == (ATTR_ATIME | ATTR_MTIME))
	{
		in_mask |= FS_ATTRIB;
		dn_mask |= DN_ATTRIB;
	} else if (ia_valid & ATTR_ATIME) {
		in_mask |= FS_ACCESS;
		dn_mask |= DN_ACCESS;
	} else if (ia_valid & ATTR_MTIME) {
		in_mask |= FS_MODIFY;
		dn_mask |= DN_MODIFY;
	}
	if (ia_valid & ATTR_MODE) {
		in_mask |= FS_ATTRIB;
		dn_mask |= DN_ATTRIB;
	}

	if (dn_mask)
		dnotify_parent(dentry, dn_mask);
	if (in_mask) {
		if (S_ISDIR(inode->i_mode))
			in_mask |= FS_IN_ISDIR;
		inotify_inode_queue_event(inode, in_mask, 0, NULL, NULL);
		inotify_dentry_parent_queue_event(dentry, in_mask, 0,
						  dentry->d_name.name);
		fsnotify(inode, in_mask, inode, FSNOTIFY_EVENT_INODE);
	}
}

#if defined(CONFIG_INOTIFY) || defined(CONFIG_FSNOTIFY)	/* notify helpers */

/*
 * fsnotify_oldname_init - save off the old filename before we change it
 */
static inline const char *fsnotify_oldname_init(const char *name)
{
	return kstrdup(name, GFP_KERNEL);
}

/*
 * fsnotify_oldname_free - free the name we got from fsnotify_oldname_init
 */
static inline void fsnotify_oldname_free(const char *old_name)
{
	kfree(old_name);
}

#else	/* CONFIG_INOTIFY || CONFIG_FSNOTIFY */

static inline const char *fsnotify_oldname_init(const char *name)
{
	return NULL;
}

static inline void fsnotify_oldname_free(const char *old_name)
{
}

#endif	/* ! CONFIG_INOTIFY */

#endif	/* _LINUX_FS_NOTIFY_H */
