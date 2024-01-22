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
 * if ei->is_freed is not set. When ei->is_freed is set, the dentry
 * is on its way to being freed after the last dput() is made on it.
 */
static DEFINE_MUTEX(eventfs_mutex);

/* Choose something "unique" ;-) */
#define EVENTFS_FILE_INODE_INO		0x12c4e37
#define EVENTFS_DIR_INODE_INO		0x134b2f5

/*
 * The eventfs_inode (ei) itself is protected by SRCU. It is released from
 * its parent's list and will have is_freed set (under eventfs_mutex).
 * After the SRCU grace period is over and the last dput() is called
 * the ei is freed.
 */
DEFINE_STATIC_SRCU(eventfs_srcu);

/* Mode is unsigned short, use the upper bits for flags */
enum {
	EVENTFS_SAVE_MODE	= BIT(16),
	EVENTFS_SAVE_UID	= BIT(17),
	EVENTFS_SAVE_GID	= BIT(18),
	EVENTFS_TOPLEVEL	= BIT(19),
};

#define EVENTFS_MODE_MASK	(EVENTFS_SAVE_MODE - 1)

static struct dentry *eventfs_root_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags);
static int eventfs_iterate(struct file *file, struct dir_context *ctx);

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
			ei->entry_attrs = kcalloc(ei->nr_entries, sizeof(*ei->entry_attrs),
						  GFP_NOFS);
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
		/*
		 * The events directory dentry is never freed, unless its
		 * part of an instance that is deleted. It's attr is the
		 * default for its child files and directories.
		 * Do not update it. It's not used for its own mode or ownership.
		 */
		if (ei->is_events) {
			/* But it still needs to know if it was modified */
			if (iattr->ia_valid & ATTR_UID)
				ei->attr.mode |= EVENTFS_SAVE_UID;
			if (iattr->ia_valid & ATTR_GID)
				ei->attr.mode |= EVENTFS_SAVE_GID;
		} else {
			update_attr(&ei->attr, iattr);
		}

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

static void update_top_events_attr(struct eventfs_inode *ei, struct dentry *dentry)
{
	struct inode *inode;

	/* Only update if the "events" was on the top level */
	if (!ei || !(ei->attr.mode & EVENTFS_TOPLEVEL))
		return;

	/* Get the tracefs root inode. */
	inode = d_inode(dentry->d_sb->s_root);
	ei->attr.uid = inode->i_uid;
	ei->attr.gid = inode->i_gid;
}

static void set_top_events_ownership(struct inode *inode)
{
	struct tracefs_inode *ti = get_tracefs(inode);
	struct eventfs_inode *ei = ti->private;
	struct dentry *dentry;

	/* The top events directory doesn't get automatically updated */
	if (!ei || !ei->is_events || !(ei->attr.mode & EVENTFS_TOPLEVEL))
		return;

	dentry = ei->dentry;

	update_top_events_attr(ei, dentry);

	if (!(ei->attr.mode & EVENTFS_SAVE_UID))
		inode->i_uid = ei->attr.uid;

	if (!(ei->attr.mode & EVENTFS_SAVE_GID))
		inode->i_gid = ei->attr.gid;
}

static int eventfs_get_attr(struct mnt_idmap *idmap,
			    const struct path *path, struct kstat *stat,
			    u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_backing_inode(dentry);

	set_top_events_ownership(inode);

	generic_fillattr(idmap, request_mask, inode, stat);
	return 0;
}

static int eventfs_permission(struct mnt_idmap *idmap,
			      struct inode *inode, int mask)
{
	set_top_events_ownership(inode);
	return generic_permission(idmap, inode, mask);
}

static const struct inode_operations eventfs_root_dir_inode_operations = {
	.lookup		= eventfs_root_lookup,
	.setattr	= eventfs_set_attr,
	.getattr	= eventfs_get_attr,
	.permission	= eventfs_permission,
};

static const struct inode_operations eventfs_file_inode_operations = {
	.setattr	= eventfs_set_attr,
};

static const struct file_operations eventfs_file_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= eventfs_iterate,
	.llseek		= generic_file_llseek,
};

/* Return the evenfs_inode of the "events" directory */
static struct eventfs_inode *eventfs_find_events(struct dentry *dentry)
{
	struct eventfs_inode *ei;

	mutex_lock(&eventfs_mutex);
	do {
		/* The parent always has an ei, except for events itself */
		ei = dentry->d_parent->d_fsdata;

		/*
		 * If the ei is being freed, the ownership of the children
		 * doesn't matter.
		 */
		if (ei->is_freed) {
			ei = NULL;
			break;
		}

		dentry = ei->dentry;
	} while (!ei->is_events);
	mutex_unlock(&eventfs_mutex);

	update_top_events_attr(ei, dentry);

	return ei;
}

static void update_inode_attr(struct dentry *dentry, struct inode *inode,
			      struct eventfs_attr *attr, umode_t mode)
{
	struct eventfs_inode *events_ei = eventfs_find_events(dentry);

	if (!events_ei)
		return;

	inode->i_mode = mode;
	inode->i_uid = events_ei->attr.uid;
	inode->i_gid = events_ei->attr.gid;

	if (!attr)
		return;

	if (attr->mode & EVENTFS_SAVE_MODE)
		inode->i_mode = attr->mode & EVENTFS_MODE_MASK;

	if (attr->mode & EVENTFS_SAVE_UID)
		inode->i_uid = attr->uid;

	if (attr->mode & EVENTFS_SAVE_GID)
		inode->i_gid = attr->gid;
}

static void update_gid(struct eventfs_inode *ei, kgid_t gid, int level)
{
	struct eventfs_inode *ei_child;

	/* at most we have events/system/event */
	if (WARN_ON_ONCE(level > 3))
		return;

	ei->attr.gid = gid;

	if (ei->entry_attrs) {
		for (int i = 0; i < ei->nr_entries; i++) {
			ei->entry_attrs[i].gid = gid;
		}
	}

	/*
	 * Only eventfs_inode with dentries are updated, make sure
	 * all eventfs_inodes are updated. If one of the children
	 * do not have a dentry, this function must traverse it.
	 */
	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 srcu_read_lock_held(&eventfs_srcu)) {
		if (!ei_child->dentry)
			update_gid(ei_child, gid, level + 1);
	}
}

void eventfs_update_gid(struct dentry *dentry, kgid_t gid)
{
	struct eventfs_inode *ei = dentry->d_fsdata;
	int idx;

	idx = srcu_read_lock(&eventfs_srcu);
	update_gid(ei, gid, 0);
	srcu_read_unlock(&eventfs_srcu, idx);
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
	update_inode_attr(dentry, inode, attr, mode);

	inode->i_op = &eventfs_file_inode_operations;
	inode->i_fop = fop;
	inode->i_private = data;

	/* All files will have the same inode number */
	inode->i_ino = EVENTFS_FILE_INODE_INO;

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
	update_inode_attr(dentry, inode, &ei->attr,
			  S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO);

	inode->i_op = &eventfs_root_dir_inode_operations;
	inode->i_fop = &eventfs_file_operations;

	/* All directories will have the same inode number */
	inode->i_ino = EVENTFS_DIR_INODE_INO;

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
 *
 * Create a dentry for a file of an eventfs_inode @ei and place it into the
 * address located at @e_dentry.
 */
static struct dentry *
create_file_dentry(struct eventfs_inode *ei, int idx,
		   struct dentry *parent, const char *name, umode_t mode, void *data,
		   const struct file_operations *fops)
{
	struct eventfs_attr *attr = NULL;
	struct dentry **e_dentry = &ei->d_children[idx];
	struct dentry *dentry;

	WARN_ON_ONCE(!inode_is_locked(parent->d_inode));

	mutex_lock(&eventfs_mutex);
	if (ei->is_freed) {
		mutex_unlock(&eventfs_mutex);
		return NULL;
	}
	/* If the e_dentry already has a dentry, use it */
	if (*e_dentry) {
		dget(*e_dentry);
		mutex_unlock(&eventfs_mutex);
		return *e_dentry;
	}

	/* ei->entry_attrs are protected by SRCU */
	if (ei->entry_attrs)
		attr = &ei->entry_attrs[idx];

	mutex_unlock(&eventfs_mutex);

	dentry = create_file(name, mode, attr, parent, data, fops);

	mutex_lock(&eventfs_mutex);

	if (IS_ERR_OR_NULL(dentry)) {
		/*
		 * When the mutex was released, something else could have
		 * created the dentry for this e_dentry. In which case
		 * use that one.
		 *
		 * If ei->is_freed is set, the e_dentry is currently on its
		 * way to being freed, don't return it. If e_dentry is NULL
		 * it means it was already freed.
		 */
		if (ei->is_freed) {
			dentry = NULL;
		} else {
			dentry = *e_dentry;
			dget(dentry);
		}
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
		dentry = NULL;
	}
	mutex_unlock(&eventfs_mutex);

	return dentry;
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
 *
 * This creates and attaches a directory dentry to the eventfs_inode @ei.
 */
static struct dentry *
create_dir_dentry(struct eventfs_inode *pei, struct eventfs_inode *ei,
		  struct dentry *parent)
{
	struct dentry *dentry = NULL;

	WARN_ON_ONCE(!inode_is_locked(parent->d_inode));

	mutex_lock(&eventfs_mutex);
	if (pei->is_freed || ei->is_freed) {
		mutex_unlock(&eventfs_mutex);
		return NULL;
	}
	if (ei->dentry) {
		/* If the eventfs_inode already has a dentry, use it */
		dentry = ei->dentry;
		dget(dentry);
		mutex_unlock(&eventfs_mutex);
		return dentry;
	}
	mutex_unlock(&eventfs_mutex);

	dentry = create_dir(ei, parent);

	mutex_lock(&eventfs_mutex);

	if (IS_ERR_OR_NULL(dentry) && !ei->is_freed) {
		/*
		 * When the mutex was released, something else could have
		 * created the dentry for this e_dentry. In which case
		 * use that one.
		 *
		 * If ei->is_freed is set, the e_dentry is currently on its
		 * way to being freed.
		 */
		dentry = ei->dentry;
		if (dentry)
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
		dentry = NULL;
	}
	mutex_unlock(&eventfs_mutex);

	return dentry;
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
	struct dentry *d;
	const char *name = dentry->d_name.name;
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
		if (IS_ERR(ret))
			goto out;
		d = create_dir_dentry(ei, ei_child, ei_dentry);
		dput(d);
		goto out;
	}

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
			if (IS_ERR(ret))
				goto out;
			d = create_file_dentry(ei, i, ei_dentry, name, mode, cdata, fops);
			dput(d);
			break;
		}
	}
 out:
	srcu_read_unlock(&eventfs_srcu, idx);
	return ret;
}

/*
 * Walk the children of a eventfs_inode to fill in getdents().
 */
static int eventfs_iterate(struct file *file, struct dir_context *ctx)
{
	const struct file_operations *fops;
	struct inode *f_inode = file_inode(file);
	const struct eventfs_entry *entry;
	struct eventfs_inode *ei_child;
	struct tracefs_inode *ti;
	struct eventfs_inode *ei;
	const char *name;
	umode_t mode;
	int idx;
	int ret = -EINVAL;
	int ino;
	int i, r, c;

	if (!dir_emit_dots(file, ctx))
		return 0;

	ti = get_tracefs(f_inode);
	if (!(ti->flags & TRACEFS_EVENT_INODE))
		return -EINVAL;

	c = ctx->pos - 2;

	idx = srcu_read_lock(&eventfs_srcu);

	mutex_lock(&eventfs_mutex);
	ei = READ_ONCE(ti->private);
	if (ei && ei->is_freed)
		ei = NULL;
	mutex_unlock(&eventfs_mutex);

	if (!ei)
		goto out;

	/*
	 * Need to create the dentries and inodes to have a consistent
	 * inode number.
	 */
	ret = 0;

	/* Start at 'c' to jump over already read entries */
	for (i = c; i < ei->nr_entries; i++, ctx->pos++) {
		void *cdata = ei->data;

		entry = &ei->entries[i];
		name = entry->name;

		mutex_lock(&eventfs_mutex);
		/* If ei->is_freed then just bail here, nothing more to do */
		if (ei->is_freed) {
			mutex_unlock(&eventfs_mutex);
			goto out;
		}
		r = entry->callback(name, &mode, &cdata, &fops);
		mutex_unlock(&eventfs_mutex);
		if (r <= 0)
			continue;

		ino = EVENTFS_FILE_INODE_INO;

		if (!dir_emit(ctx, name, strlen(name), ino, DT_REG))
			goto out;
	}

	/* Subtract the skipped entries above */
	c -= min((unsigned int)c, (unsigned int)ei->nr_entries);

	list_for_each_entry_srcu(ei_child, &ei->children, list,
				 srcu_read_lock_held(&eventfs_srcu)) {

		if (c > 0) {
			c--;
			continue;
		}

		ctx->pos++;

		if (ei_child->is_freed)
			continue;

		name = ei_child->name;

		ino = EVENTFS_DIR_INODE_INO;

		if (!dir_emit(ctx, name, strlen(name), ino, DT_DIR))
			goto out_dec;
	}
	ret = 1;
 out:
	srcu_read_unlock(&eventfs_srcu, idx);

	return ret;

 out_dec:
	/* Incremented ctx->pos without adding something, reset it */
	ctx->pos--;
	goto out;
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
		ei->d_children = kcalloc(size, sizeof(*ei->d_children), GFP_KERNEL);
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
	kuid_t uid;
	kgid_t gid;

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
		ei->d_children = kcalloc(size, sizeof(*ei->d_children), GFP_KERNEL);
		if (!ei->d_children)
			goto fail;
	}

	ei->dentry = dentry;
	ei->entries = entries;
	ei->nr_entries = size;
	ei->is_events = 1;
	ei->data = data;
	ei->name = kstrdup_const(name, GFP_KERNEL);
	if (!ei->name)
		goto fail;

	/* Save the ownership of this directory */
	uid = d_inode(dentry->d_parent)->i_uid;
	gid = d_inode(dentry->d_parent)->i_gid;

	/*
	 * If the events directory is of the top instance, then parent
	 * is NULL. Set the attr.mode to reflect this and its permissions will
	 * default to the tracefs root dentry.
	 */
	if (!parent)
		ei->attr.mode = EVENTFS_TOPLEVEL;

	/* This is used as the default ownership of the files and directories */
	ei->attr.uid = uid;
	ei->attr.gid = gid;

	INIT_LIST_HEAD(&ei->children);
	INIT_LIST_HEAD(&ei->list);

	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_EVENT_INODE | TRACEFS_EVENT_TOP_INODE;
	ti->private = ei;

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	inode->i_uid = uid;
	inode->i_gid = gid;
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
