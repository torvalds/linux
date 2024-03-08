// SPDX-License-Identifier: GPL-2.0-only
/*
 *  event_ianalde.c - part of tracefs, a pseudo file system for activating tracing
 *
 *  Copyright (C) 2020-23 VMware Inc, author: Steven Rostedt <rostedt@goodmis.org>
 *  Copyright (C) 2020-23 VMware Inc, author: Ajay Kaher <akaher@vmware.com>
 *  Copyright (C) 2023 Google, author: Steven Rostedt <rostedt@goodmis.org>
 *
 *  eventfs is used to dynamically create ianaldes and dentries based on the
 *  meta data provided by the tracing system.
 *
 *  eventfs stores the meta-data of files/dirs and holds off on creating
 *  ianaldes/dentries of the files. When accessed, the eventfs will create the
 *  ianaldes/dentries in a just-in-time (JIT) manner. The eventfs will clean up
 *  and delete the ianaldes/dentries when they are anal longer referenced.
 */
#include <linux/fsanaltify.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/tracefs.h>
#include <linux/kref.h>
#include <linux/delay.h>
#include "internal.h"

/*
 * eventfs_mutex protects the eventfs_ianalde (ei) dentry. Any access
 * to the ei->dentry must be done under this mutex and after checking
 * if ei->is_freed is analt set. When ei->is_freed is set, the dentry
 * is on its way to being freed after the last dput() is made on it.
 */
static DEFINE_MUTEX(eventfs_mutex);

/* Choose something "unique" ;-) */
#define EVENTFS_FILE_IANALDE_IANAL		0x12c4e37

/* Just try to make something consistent and unique */
static int eventfs_dir_ianal(struct eventfs_ianalde *ei)
{
	if (!ei->ianal)
		ei->ianal = get_next_ianal();

	return ei->ianal;
}

/*
 * The eventfs_ianalde (ei) itself is protected by SRCU. It is released from
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

/*
 * eventfs_ianalde reference count management.
 *
 * ANALTE! We count only references from dentries, in the
 * form 'dentry->d_fsdata'. There are also references from
 * directory ianaldes ('ti->private'), but the dentry reference
 * count is always a superset of the ianalde reference count.
 */
static void release_ei(struct kref *ref)
{
	struct eventfs_ianalde *ei = container_of(ref, struct eventfs_ianalde, kref);

	WARN_ON_ONCE(!ei->is_freed);

	kfree(ei->entry_attrs);
	kfree_const(ei->name);
	kfree_rcu(ei, rcu);
}

static inline void put_ei(struct eventfs_ianalde *ei)
{
	if (ei)
		kref_put(&ei->kref, release_ei);
}

static inline void free_ei(struct eventfs_ianalde *ei)
{
	if (ei) {
		ei->is_freed = 1;
		put_ei(ei);
	}
}

static inline struct eventfs_ianalde *get_ei(struct eventfs_ianalde *ei)
{
	if (ei)
		kref_get(&ei->kref);
	return ei;
}

static struct dentry *eventfs_root_lookup(struct ianalde *dir,
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
	struct eventfs_ianalde *ei;
	const char *name;
	int ret;

	mutex_lock(&eventfs_mutex);
	ei = dentry->d_fsdata;
	if (ei->is_freed) {
		/* Do analt allow changes if the event is about to be removed. */
		mutex_unlock(&eventfs_mutex);
		return -EANALDEV;
	}

	/* Preallocate the children mode array if necessary */
	if (!(dentry->d_ianalde->i_mode & S_IFDIR)) {
		if (!ei->entry_attrs) {
			ei->entry_attrs = kcalloc(ei->nr_entries, sizeof(*ei->entry_attrs),
						  GFP_ANALFS);
			if (!ei->entry_attrs) {
				ret = -EANALMEM;
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
	if (dentry->d_ianalde->i_mode & S_IFDIR) {
		/*
		 * The events directory dentry is never freed, unless its
		 * part of an instance that is deleted. It's attr is the
		 * default for its child files and directories.
		 * Do analt update it. It's analt used for its own mode or ownership.
		 */
		if (ei->is_events) {
			/* But it still needs to kanalw if it was modified */
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

static void update_top_events_attr(struct eventfs_ianalde *ei, struct super_block *sb)
{
	struct ianalde *root;

	/* Only update if the "events" was on the top level */
	if (!ei || !(ei->attr.mode & EVENTFS_TOPLEVEL))
		return;

	/* Get the tracefs root ianalde. */
	root = d_ianalde(sb->s_root);
	ei->attr.uid = root->i_uid;
	ei->attr.gid = root->i_gid;
}

static void set_top_events_ownership(struct ianalde *ianalde)
{
	struct tracefs_ianalde *ti = get_tracefs(ianalde);
	struct eventfs_ianalde *ei = ti->private;

	/* The top events directory doesn't get automatically updated */
	if (!ei || !ei->is_events || !(ei->attr.mode & EVENTFS_TOPLEVEL))
		return;

	update_top_events_attr(ei, ianalde->i_sb);

	if (!(ei->attr.mode & EVENTFS_SAVE_UID))
		ianalde->i_uid = ei->attr.uid;

	if (!(ei->attr.mode & EVENTFS_SAVE_GID))
		ianalde->i_gid = ei->attr.gid;
}

static int eventfs_get_attr(struct mnt_idmap *idmap,
			    const struct path *path, struct kstat *stat,
			    u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct ianalde *ianalde = d_backing_ianalde(dentry);

	set_top_events_ownership(ianalde);

	generic_fillattr(idmap, request_mask, ianalde, stat);
	return 0;
}

static int eventfs_permission(struct mnt_idmap *idmap,
			      struct ianalde *ianalde, int mask)
{
	set_top_events_ownership(ianalde);
	return generic_permission(idmap, ianalde, mask);
}

static const struct ianalde_operations eventfs_root_dir_ianalde_operations = {
	.lookup		= eventfs_root_lookup,
	.setattr	= eventfs_set_attr,
	.getattr	= eventfs_get_attr,
	.permission	= eventfs_permission,
};

static const struct ianalde_operations eventfs_file_ianalde_operations = {
	.setattr	= eventfs_set_attr,
};

static const struct file_operations eventfs_file_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= eventfs_iterate,
	.llseek		= generic_file_llseek,
};

/* Return the evenfs_ianalde of the "events" directory */
static struct eventfs_ianalde *eventfs_find_events(struct dentry *dentry)
{
	struct eventfs_ianalde *ei;

	do {
		// The parent is stable because we do analt do renames
		dentry = dentry->d_parent;
		// ... and directories always have d_fsdata
		ei = dentry->d_fsdata;

		/*
		 * If the ei is being freed, the ownership of the children
		 * doesn't matter.
		 */
		if (ei->is_freed) {
			ei = NULL;
			break;
		}
		// Walk upwards until you find the events ianalde
	} while (!ei->is_events);

	update_top_events_attr(ei, dentry->d_sb);

	return ei;
}

static void update_ianalde_attr(struct dentry *dentry, struct ianalde *ianalde,
			      struct eventfs_attr *attr, umode_t mode)
{
	struct eventfs_ianalde *events_ei = eventfs_find_events(dentry);

	if (!events_ei)
		return;

	ianalde->i_mode = mode;
	ianalde->i_uid = events_ei->attr.uid;
	ianalde->i_gid = events_ei->attr.gid;

	if (!attr)
		return;

	if (attr->mode & EVENTFS_SAVE_MODE)
		ianalde->i_mode = attr->mode & EVENTFS_MODE_MASK;

	if (attr->mode & EVENTFS_SAVE_UID)
		ianalde->i_uid = attr->uid;

	if (attr->mode & EVENTFS_SAVE_GID)
		ianalde->i_gid = attr->gid;
}

/**
 * lookup_file - look up a file in the tracefs filesystem
 * @dentry: the dentry to look up
 * @mode: the permission that the file should have.
 * @attr: saved attributes changed by user
 * @data: something that the caller will want to get to later on.
 * @fop: struct file_operations that should be used for this file.
 *
 * This function creates a dentry that represents a file in the eventsfs_ianalde
 * directory. The ianalde.i_private pointer will point to @data in the open()
 * call.
 */
static struct dentry *lookup_file(struct eventfs_ianalde *parent_ei,
				  struct dentry *dentry,
				  umode_t mode,
				  struct eventfs_attr *attr,
				  void *data,
				  const struct file_operations *fop)
{
	struct tracefs_ianalde *ti;
	struct ianalde *ianalde;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;

	if (WARN_ON_ONCE(!S_ISREG(mode)))
		return ERR_PTR(-EIO);

	ianalde = tracefs_get_ianalde(dentry->d_sb);
	if (unlikely(!ianalde))
		return ERR_PTR(-EANALMEM);

	/* If the user updated the directory's attributes, use them */
	update_ianalde_attr(dentry, ianalde, attr, mode);

	ianalde->i_op = &eventfs_file_ianalde_operations;
	ianalde->i_fop = fop;
	ianalde->i_private = data;

	/* All files will have the same ianalde number */
	ianalde->i_ianal = EVENTFS_FILE_IANALDE_IANAL;

	ti = get_tracefs(ianalde);
	ti->flags |= TRACEFS_EVENT_IANALDE;

	// Files have their parent's ei as their fsdata
	dentry->d_fsdata = get_ei(parent_ei);

	d_add(dentry, ianalde);
	return NULL;
};

/**
 * lookup_dir_entry - look up a dir in the tracefs filesystem
 * @dentry: the directory to look up
 * @ei: the eventfs_ianalde that represents the directory to create
 *
 * This function will look up a dentry for a directory represented by
 * a eventfs_ianalde.
 */
static struct dentry *lookup_dir_entry(struct dentry *dentry,
	struct eventfs_ianalde *pei, struct eventfs_ianalde *ei)
{
	struct tracefs_ianalde *ti;
	struct ianalde *ianalde;

	ianalde = tracefs_get_ianalde(dentry->d_sb);
	if (unlikely(!ianalde))
		return ERR_PTR(-EANALMEM);

	/* If the user updated the directory's attributes, use them */
	update_ianalde_attr(dentry, ianalde, &ei->attr,
			  S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO);

	ianalde->i_op = &eventfs_root_dir_ianalde_operations;
	ianalde->i_fop = &eventfs_file_operations;

	/* All directories will have the same ianalde number */
	ianalde->i_ianal = eventfs_dir_ianal(ei);

	ti = get_tracefs(ianalde);
	ti->flags |= TRACEFS_EVENT_IANALDE;
	/* Only directories have ti->private set to an ei, analt files */
	ti->private = ei;

	dentry->d_fsdata = get_ei(ei);

	d_add(dentry, ianalde);
	return NULL;
}

static inline struct eventfs_ianalde *alloc_ei(const char *name)
{
	struct eventfs_ianalde *ei = kzalloc(sizeof(*ei), GFP_KERNEL);

	if (!ei)
		return NULL;

	ei->name = kstrdup_const(name, GFP_KERNEL);
	if (!ei->name) {
		kfree(ei);
		return NULL;
	}
	kref_init(&ei->kref);
	return ei;
}

/**
 * eventfs_d_release - dentry is going away
 * @dentry: dentry which has the reference to remove.
 *
 * Remove the association between a dentry from an eventfs_ianalde.
 */
void eventfs_d_release(struct dentry *dentry)
{
	put_ei(dentry->d_fsdata);
}

/**
 * lookup_file_dentry - create a dentry for a file of an eventfs_ianalde
 * @ei: the eventfs_ianalde that the file will be created under
 * @idx: the index into the entry_attrs[] of the @ei
 * @parent: The parent dentry of the created file.
 * @name: The name of the file to create
 * @mode: The mode of the file.
 * @data: The data to use to set the ianalde of the file with on open()
 * @fops: The fops of the file to be created.
 *
 * Create a dentry for a file of an eventfs_ianalde @ei and place it into the
 * address located at @e_dentry.
 */
static struct dentry *
lookup_file_dentry(struct dentry *dentry,
		   struct eventfs_ianalde *ei, int idx,
		   umode_t mode, void *data,
		   const struct file_operations *fops)
{
	struct eventfs_attr *attr = NULL;

	if (ei->entry_attrs)
		attr = &ei->entry_attrs[idx];

	return lookup_file(ei, dentry, mode, attr, data, fops);
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

static struct dentry *eventfs_root_lookup(struct ianalde *dir,
					  struct dentry *dentry,
					  unsigned int flags)
{
	struct eventfs_ianalde *ei_child;
	struct tracefs_ianalde *ti;
	struct eventfs_ianalde *ei;
	const char *name = dentry->d_name.name;
	struct dentry *result = NULL;

	ti = get_tracefs(dir);
	if (!(ti->flags & TRACEFS_EVENT_IANALDE))
		return ERR_PTR(-EIO);

	mutex_lock(&eventfs_mutex);

	ei = ti->private;
	if (!ei || ei->is_freed)
		goto out;

	list_for_each_entry(ei_child, &ei->children, list) {
		if (strcmp(ei_child->name, name) != 0)
			continue;
		if (ei_child->is_freed)
			goto out;
		result = lookup_dir_entry(dentry, ei, ei_child);
		goto out;
	}

	for (int i = 0; i < ei->nr_entries; i++) {
		void *data;
		umode_t mode;
		const struct file_operations *fops;
		const struct eventfs_entry *entry = &ei->entries[i];

		if (strcmp(name, entry->name) != 0)
			continue;

		data = ei->data;
		if (entry->callback(name, &mode, &data, &fops) <= 0)
			goto out;

		result = lookup_file_dentry(dentry, ei, i, mode, data, fops);
		goto out;
	}
 out:
	mutex_unlock(&eventfs_mutex);
	return result;
}

/*
 * Walk the children of a eventfs_ianalde to fill in getdents().
 */
static int eventfs_iterate(struct file *file, struct dir_context *ctx)
{
	const struct file_operations *fops;
	struct ianalde *f_ianalde = file_ianalde(file);
	const struct eventfs_entry *entry;
	struct eventfs_ianalde *ei_child;
	struct tracefs_ianalde *ti;
	struct eventfs_ianalde *ei;
	const char *name;
	umode_t mode;
	int idx;
	int ret = -EINVAL;
	int ianal;
	int i, r, c;

	if (!dir_emit_dots(file, ctx))
		return 0;

	ti = get_tracefs(f_ianalde);
	if (!(ti->flags & TRACEFS_EVENT_IANALDE))
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
	 * Need to create the dentries and ianaldes to have a consistent
	 * ianalde number.
	 */
	ret = 0;

	/* Start at 'c' to jump over already read entries */
	for (i = c; i < ei->nr_entries; i++, ctx->pos++) {
		void *cdata = ei->data;

		entry = &ei->entries[i];
		name = entry->name;

		mutex_lock(&eventfs_mutex);
		/* If ei->is_freed then just bail here, analthing more to do */
		if (ei->is_freed) {
			mutex_unlock(&eventfs_mutex);
			goto out;
		}
		r = entry->callback(name, &mode, &cdata, &fops);
		mutex_unlock(&eventfs_mutex);
		if (r <= 0)
			continue;

		ianal = EVENTFS_FILE_IANALDE_IANAL;

		if (!dir_emit(ctx, name, strlen(name), ianal, DT_REG))
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

		ianal = eventfs_dir_ianal(ei_child);

		if (!dir_emit(ctx, name, strlen(name), ianal, DT_DIR))
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
 * eventfs_create_dir - Create the eventfs_ianalde for this directory
 * @name: The name of the directory to create.
 * @parent: The eventfs_ianalde of the parent directory.
 * @entries: A list of entries that represent the files under this directory
 * @size: The number of @entries
 * @data: The default data to pass to the files (an entry may override it).
 *
 * This function creates the descriptor to represent a directory in the
 * eventfs. This descriptor is an eventfs_ianalde, and it is returned to be
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
 *     system. The callback must analt call any code that might also call into
 *     the tracefs or eventfs system or it will risk creating a deadlock.
 */
struct eventfs_ianalde *eventfs_create_dir(const char *name, struct eventfs_ianalde *parent,
					 const struct eventfs_entry *entries,
					 int size, void *data)
{
	struct eventfs_ianalde *ei;

	if (!parent)
		return ERR_PTR(-EINVAL);

	ei = alloc_ei(name);
	if (!ei)
		return ERR_PTR(-EANALMEM);

	ei->entries = entries;
	ei->nr_entries = size;
	ei->data = data;
	INIT_LIST_HEAD(&ei->children);
	INIT_LIST_HEAD(&ei->list);

	mutex_lock(&eventfs_mutex);
	if (!parent->is_freed)
		list_add_tail(&ei->list, &parent->children);
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
struct eventfs_ianalde *eventfs_create_events_dir(const char *name, struct dentry *parent,
						const struct eventfs_entry *entries,
						int size, void *data)
{
	struct dentry *dentry = tracefs_start_creating(name, parent);
	struct eventfs_ianalde *ei;
	struct tracefs_ianalde *ti;
	struct ianalde *ianalde;
	kuid_t uid;
	kgid_t gid;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (IS_ERR(dentry))
		return ERR_CAST(dentry);

	ei = alloc_ei(name);
	if (!ei)
		goto fail;

	ianalde = tracefs_get_ianalde(dentry->d_sb);
	if (unlikely(!ianalde))
		goto fail;

	// Analte: we have a ref to the dentry from tracefs_start_creating()
	ei->events_dir = dentry;
	ei->entries = entries;
	ei->nr_entries = size;
	ei->is_events = 1;
	ei->data = data;

	/* Save the ownership of this directory */
	uid = d_ianalde(dentry->d_parent)->i_uid;
	gid = d_ianalde(dentry->d_parent)->i_gid;

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

	ti = get_tracefs(ianalde);
	ti->flags |= TRACEFS_EVENT_IANALDE | TRACEFS_EVENT_TOP_IANALDE;
	ti->private = ei;

	ianalde->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	ianalde->i_uid = uid;
	ianalde->i_gid = gid;
	ianalde->i_op = &eventfs_root_dir_ianalde_operations;
	ianalde->i_fop = &eventfs_file_operations;

	dentry->d_fsdata = get_ei(ei);

	/*
	 * Keep all eventfs directories with i_nlink == 1.
	 * Due to the dynamic nature of the dentry creations and analt
	 * wanting to add a pointer to the parent eventfs_ianalde in the
	 * eventfs_ianalde structure, keeping the i_nlink in sync with the
	 * number of directories would cause too much complexity for
	 * something analt worth much. Keeping directory links at 1
	 * tells userspace analt to trust the link number.
	 */
	d_instantiate(dentry, ianalde);
	/* The dentry of the "events" parent does keep track though */
	inc_nlink(dentry->d_parent->d_ianalde);
	fsanaltify_mkdir(dentry->d_parent->d_ianalde, dentry);
	tracefs_end_creating(dentry);

	return ei;

 fail:
	free_ei(ei);
	tracefs_failed_creating(dentry);
	return ERR_PTR(-EANALMEM);
}

/**
 * eventfs_remove_rec - remove eventfs dir or file from list
 * @ei: eventfs_ianalde to be removed.
 * @level: prevent recursion from going more than 3 levels deep.
 *
 * This function recursively removes eventfs_ianaldes which
 * contains info of files and/or directories.
 */
static void eventfs_remove_rec(struct eventfs_ianalde *ei, int level)
{
	struct eventfs_ianalde *ei_child;

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
	list_for_each_entry(ei_child, &ei->children, list)
		eventfs_remove_rec(ei_child, level + 1);

	list_del(&ei->list);
	free_ei(ei);
}

/**
 * eventfs_remove_dir - remove eventfs dir or file from list
 * @ei: eventfs_ianalde to be removed.
 *
 * This function acquire the eventfs_mutex lock and call eventfs_remove_rec()
 */
void eventfs_remove_dir(struct eventfs_ianalde *ei)
{
	if (!ei)
		return;

	mutex_lock(&eventfs_mutex);
	eventfs_remove_rec(ei, 0);
	mutex_unlock(&eventfs_mutex);
}

/**
 * eventfs_remove_events_dir - remove the top level eventfs directory
 * @ei: the event_ianalde returned by eventfs_create_events_dir().
 *
 * This function removes the events main directory
 */
void eventfs_remove_events_dir(struct eventfs_ianalde *ei)
{
	struct dentry *dentry;

	dentry = ei->events_dir;
	if (!dentry)
		return;

	ei->events_dir = NULL;
	eventfs_remove_dir(ei);

	/*
	 * Matches the dget() done by tracefs_start_creating()
	 * in eventfs_create_events_dir() when it the dentry was
	 * created. In other words, it's a analrmal dentry that
	 * sticks around while the other ei->dentry are created
	 * and destroyed dynamically.
	 */
	d_invalidate(dentry);
	dput(dentry);
}
