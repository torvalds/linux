/*
 * dir.c - Operations for sysfs directories.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/idr.h>
#include <linux/completion.h>
#include <asm/semaphore.h>
#include "sysfs.h"

DECLARE_RWSEM(sysfs_rename_sem);
spinlock_t sysfs_lock = SPIN_LOCK_UNLOCKED;
spinlock_t kobj_sysfs_assoc_lock = SPIN_LOCK_UNLOCKED;

static spinlock_t sysfs_ino_lock = SPIN_LOCK_UNLOCKED;
static DEFINE_IDA(sysfs_ino_ida);

/**
 *	sysfs_link_sibling - link sysfs_dirent into sibling list
 *	@sd: sysfs_dirent of interest
 *
 *	Link @sd into its sibling list which starts from
 *	sd->s_parent->s_children.
 *
 *	Locking:
 *	mutex_lock(sd->s_parent->dentry->d_inode->i_mutex)
 */
static void sysfs_link_sibling(struct sysfs_dirent *sd)
{
	struct sysfs_dirent *parent_sd = sd->s_parent;

	BUG_ON(sd->s_sibling);
	sd->s_sibling = parent_sd->s_children;
	parent_sd->s_children = sd;
}

/**
 *	sysfs_unlink_sibling - unlink sysfs_dirent from sibling list
 *	@sd: sysfs_dirent of interest
 *
 *	Unlink @sd from its sibling list which starts from
 *	sd->s_parent->s_children.
 *
 *	Locking:
 *	mutex_lock(sd->s_parent->dentry->d_inode->i_mutex)
 */
static void sysfs_unlink_sibling(struct sysfs_dirent *sd)
{
	struct sysfs_dirent **pos;

	for (pos = &sd->s_parent->s_children; *pos; pos = &(*pos)->s_sibling) {
		if (*pos == sd) {
			*pos = sd->s_sibling;
			sd->s_sibling = NULL;
			break;
		}
	}
}

/**
 *	sysfs_get_active - get an active reference to sysfs_dirent
 *	@sd: sysfs_dirent to get an active reference to
 *
 *	Get an active reference of @sd.  This function is noop if @sd
 *	is NULL.
 *
 *	RETURNS:
 *	Pointer to @sd on success, NULL on failure.
 */
struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd)
{
	if (unlikely(!sd))
		return NULL;

	while (1) {
		int v, t;

		v = atomic_read(&sd->s_active);
		if (unlikely(v < 0))
			return NULL;

		t = atomic_cmpxchg(&sd->s_active, v, v + 1);
		if (likely(t == v))
			return sd;
		if (t < 0)
			return NULL;

		cpu_relax();
	}
}

/**
 *	sysfs_put_active - put an active reference to sysfs_dirent
 *	@sd: sysfs_dirent to put an active reference to
 *
 *	Put an active reference to @sd.  This function is noop if @sd
 *	is NULL.
 */
void sysfs_put_active(struct sysfs_dirent *sd)
{
	struct completion *cmpl;
	int v;

	if (unlikely(!sd))
		return;

	v = atomic_dec_return(&sd->s_active);
	if (likely(v != SD_DEACTIVATED_BIAS))
		return;

	/* atomic_dec_return() is a mb(), we'll always see the updated
	 * sd->s_sibling.
	 */
	cmpl = (void *)sd->s_sibling;
	complete(cmpl);
}

/**
 *	sysfs_get_active_two - get active references to sysfs_dirent and parent
 *	@sd: sysfs_dirent of interest
 *
 *	Get active reference to @sd and its parent.  Parent's active
 *	reference is grabbed first.  This function is noop if @sd is
 *	NULL.
 *
 *	RETURNS:
 *	Pointer to @sd on success, NULL on failure.
 */
struct sysfs_dirent *sysfs_get_active_two(struct sysfs_dirent *sd)
{
	if (sd) {
		if (sd->s_parent && unlikely(!sysfs_get_active(sd->s_parent)))
			return NULL;
		if (unlikely(!sysfs_get_active(sd))) {
			sysfs_put_active(sd->s_parent);
			return NULL;
		}
	}
	return sd;
}

/**
 *	sysfs_put_active_two - put active references to sysfs_dirent and parent
 *	@sd: sysfs_dirent of interest
 *
 *	Put active references to @sd and its parent.  This function is
 *	noop if @sd is NULL.
 */
void sysfs_put_active_two(struct sysfs_dirent *sd)
{
	if (sd) {
		sysfs_put_active(sd);
		sysfs_put_active(sd->s_parent);
	}
}

/**
 *	sysfs_deactivate - deactivate sysfs_dirent
 *	@sd: sysfs_dirent to deactivate
 *
 *	Deny new active references and drain existing ones.
 */
void sysfs_deactivate(struct sysfs_dirent *sd)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	int v;

	BUG_ON(sd->s_sibling || !(sd->s_flags & SYSFS_FLAG_REMOVED));
	sd->s_sibling = (void *)&wait;

	/* atomic_add_return() is a mb(), put_active() will always see
	 * the updated sd->s_sibling.
	 */
	v = atomic_add_return(SD_DEACTIVATED_BIAS, &sd->s_active);

	if (v != SD_DEACTIVATED_BIAS)
		wait_for_completion(&wait);

	sd->s_sibling = NULL;
}

static int sysfs_alloc_ino(ino_t *pino)
{
	int ino, rc;

 retry:
	spin_lock(&sysfs_ino_lock);
	rc = ida_get_new_above(&sysfs_ino_ida, 2, &ino);
	spin_unlock(&sysfs_ino_lock);

	if (rc == -EAGAIN) {
		if (ida_pre_get(&sysfs_ino_ida, GFP_KERNEL))
			goto retry;
		rc = -ENOMEM;
	}

	*pino = ino;
	return rc;
}

static void sysfs_free_ino(ino_t ino)
{
	spin_lock(&sysfs_ino_lock);
	ida_remove(&sysfs_ino_ida, ino);
	spin_unlock(&sysfs_ino_lock);
}

void release_sysfs_dirent(struct sysfs_dirent * sd)
{
	struct sysfs_dirent *parent_sd;

 repeat:
	parent_sd = sd->s_parent;

	if (sysfs_type(sd) == SYSFS_KOBJ_LINK)
		sysfs_put(sd->s_elem.symlink.target_sd);
	if (sysfs_type(sd) & SYSFS_COPY_NAME)
		kfree(sd->s_name);
	kfree(sd->s_iattr);
	sysfs_free_ino(sd->s_ino);
	kmem_cache_free(sysfs_dir_cachep, sd);

	sd = parent_sd;
	if (sd && atomic_dec_and_test(&sd->s_count))
		goto repeat;
}

static void sysfs_d_iput(struct dentry * dentry, struct inode * inode)
{
	struct sysfs_dirent * sd = dentry->d_fsdata;

	if (sd) {
		/* sd->s_dentry is protected with sysfs_lock.  This
		 * allows sysfs_drop_dentry() to dereference it.
		 */
		spin_lock(&sysfs_lock);

		/* The dentry might have been deleted or another
		 * lookup could have happened updating sd->s_dentry to
		 * point the new dentry.  Ignore if it isn't pointing
		 * to this dentry.
		 */
		if (sd->s_dentry == dentry)
			sd->s_dentry = NULL;
		spin_unlock(&sysfs_lock);
		sysfs_put(sd);
	}
	iput(inode);
}

static struct dentry_operations sysfs_dentry_ops = {
	.d_iput		= sysfs_d_iput,
};

struct sysfs_dirent *sysfs_new_dirent(const char *name, umode_t mode, int type)
{
	char *dup_name = NULL;
	struct sysfs_dirent *sd = NULL;

	if (type & SYSFS_COPY_NAME) {
		name = dup_name = kstrdup(name, GFP_KERNEL);
		if (!name)
			goto err_out;
	}

	sd = kmem_cache_zalloc(sysfs_dir_cachep, GFP_KERNEL);
	if (!sd)
		goto err_out;

	if (sysfs_alloc_ino(&sd->s_ino))
		goto err_out;

	atomic_set(&sd->s_count, 1);
	atomic_set(&sd->s_active, 0);
	atomic_set(&sd->s_event, 1);

	sd->s_name = name;
	sd->s_mode = mode;
	sd->s_flags = type;

	return sd;

 err_out:
	kfree(dup_name);
	kmem_cache_free(sysfs_dir_cachep, sd);
	return NULL;
}

static void sysfs_attach_dentry(struct sysfs_dirent *sd, struct dentry *dentry)
{
	dentry->d_op = &sysfs_dentry_ops;
	dentry->d_fsdata = sysfs_get(sd);

	/* protect sd->s_dentry against sysfs_d_iput */
	spin_lock(&sysfs_lock);
	sd->s_dentry = dentry;
	spin_unlock(&sysfs_lock);

	d_rehash(dentry);
}

void sysfs_attach_dirent(struct sysfs_dirent *sd,
			 struct sysfs_dirent *parent_sd, struct dentry *dentry)
{
	if (dentry)
		sysfs_attach_dentry(sd, dentry);

	if (parent_sd) {
		sd->s_parent = sysfs_get(parent_sd);
		sysfs_link_sibling(sd);
	}
}

/**
 *	sysfs_find_dirent - find sysfs_dirent with the given name
 *	@parent_sd: sysfs_dirent to search under
 *	@name: name to look for
 *
 *	Look for sysfs_dirent with name @name under @parent_sd.
 *
 *	LOCKING:
 *	mutex_lock(parent->i_mutex)
 *
 *	RETURNS:
 *	Pointer to sysfs_dirent if found, NULL if not.
 */
struct sysfs_dirent *sysfs_find_dirent(struct sysfs_dirent *parent_sd,
				       const unsigned char *name)
{
	struct sysfs_dirent *sd;

	for (sd = parent_sd->s_children; sd; sd = sd->s_sibling)
		if (sysfs_type(sd) && !strcmp(sd->s_name, name))
			return sd;
	return NULL;
}

/**
 *	sysfs_get_dirent - find and get sysfs_dirent with the given name
 *	@parent_sd: sysfs_dirent to search under
 *	@name: name to look for
 *
 *	Look for sysfs_dirent with name @name under @parent_sd and get
 *	it if found.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	Pointer to sysfs_dirent if found, NULL if not.
 */
struct sysfs_dirent *sysfs_get_dirent(struct sysfs_dirent *parent_sd,
				      const unsigned char *name)
{
	struct sysfs_dirent *sd;

	mutex_lock(&parent_sd->s_dentry->d_inode->i_mutex);
	sd = sysfs_find_dirent(parent_sd, name);
	sysfs_get(sd);
	mutex_unlock(&parent_sd->s_dentry->d_inode->i_mutex);

	return sd;
}

static int create_dir(struct kobject *kobj, struct sysfs_dirent *parent_sd,
		      const char *name, struct sysfs_dirent **p_sd)
{
	struct dentry *parent = parent_sd->s_dentry;
	int error;
	umode_t mode = S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO;
	struct dentry *dentry;
	struct inode *inode;
	struct sysfs_dirent *sd;

	mutex_lock(&parent->d_inode->i_mutex);

	/* allocate */
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		goto out_unlock;
	}

	error = -EEXIST;
	if (dentry->d_inode)
		goto out_dput;

	error = -ENOMEM;
	sd = sysfs_new_dirent(name, mode, SYSFS_DIR);
	if (!sd)
		goto out_drop;
	sd->s_elem.dir.kobj = kobj;

	inode = sysfs_get_inode(sd);
	if (!inode)
		goto out_sput;

	if (inode->i_state & I_NEW) {
		inode->i_op = &sysfs_dir_inode_operations;
		inode->i_fop = &sysfs_dir_operations;
		/* directory inodes start off with i_nlink == 2 (for ".") */
		inc_nlink(inode);
	}

	/* link in */
	error = -EEXIST;
	if (sysfs_find_dirent(parent_sd, name))
		goto out_iput;

	sysfs_instantiate(dentry, inode);
	inc_nlink(parent->d_inode);
	sysfs_attach_dirent(sd, parent_sd, dentry);

	*p_sd = sd;
	error = 0;
	goto out_unlock;	/* pin directory dentry in core */

 out_iput:
	iput(inode);
 out_sput:
	sysfs_put(sd);
 out_drop:
	d_drop(dentry);
 out_dput:
	dput(dentry);
 out_unlock:
	mutex_unlock(&parent->d_inode->i_mutex);
	return error;
}

int sysfs_create_subdir(struct kobject *kobj, const char *name,
			struct sysfs_dirent **p_sd)
{
	return create_dir(kobj, kobj->sd, name, p_sd);
}

/**
 *	sysfs_create_dir - create a directory for an object.
 *	@kobj:		object we're creating directory for. 
 *	@shadow_parent:	parent object.
 */
int sysfs_create_dir(struct kobject *kobj,
		     struct sysfs_dirent *shadow_parent_sd)
{
	struct sysfs_dirent *parent_sd, *sd;
	int error = 0;

	BUG_ON(!kobj);

	if (shadow_parent_sd)
		parent_sd = shadow_parent_sd;
	else if (kobj->parent)
		parent_sd = kobj->parent->sd;
	else if (sysfs_mount && sysfs_mount->mnt_sb)
		parent_sd = sysfs_mount->mnt_sb->s_root->d_fsdata;
	else
		return -EFAULT;

	error = create_dir(kobj, parent_sd, kobject_name(kobj), &sd);
	if (!error)
		kobj->sd = sd;
	return error;
}

static struct dentry * sysfs_lookup(struct inode *dir, struct dentry *dentry,
				struct nameidata *nd)
{
	struct sysfs_dirent * parent_sd = dentry->d_parent->d_fsdata;
	struct sysfs_dirent * sd;
	struct bin_attribute *bin_attr;
	struct inode *inode;
	int found = 0;

	for (sd = parent_sd->s_children; sd; sd = sd->s_sibling) {
		if ((sysfs_type(sd) & SYSFS_NOT_PINNED) &&
		    !strcmp(sd->s_name, dentry->d_name.name)) {
			found = 1;
			break;
		}
	}

	/* no such entry */
	if (!found)
		return NULL;

	/* attach dentry and inode */
	inode = sysfs_get_inode(sd);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		/* initialize inode according to type */
		switch (sysfs_type(sd)) {
		case SYSFS_KOBJ_ATTR:
			inode->i_size = PAGE_SIZE;
			inode->i_fop = &sysfs_file_operations;
			break;
		case SYSFS_KOBJ_BIN_ATTR:
			bin_attr = sd->s_elem.bin_attr.bin_attr;
			inode->i_size = bin_attr->size;
			inode->i_fop = &bin_fops;
			break;
		case SYSFS_KOBJ_LINK:
			inode->i_op = &sysfs_symlink_inode_operations;
			break;
		default:
			BUG();
		}
	}

	sysfs_instantiate(dentry, inode);
	sysfs_attach_dentry(sd, dentry);

	return NULL;
}

const struct inode_operations sysfs_dir_inode_operations = {
	.lookup		= sysfs_lookup,
	.setattr	= sysfs_setattr,
};

static void remove_dir(struct sysfs_dirent *sd)
{
	struct dentry *parent = sd->s_parent->s_dentry;

	mutex_lock(&parent->d_inode->i_mutex);

	sysfs_unlink_sibling(sd);
	sd->s_flags |= SYSFS_FLAG_REMOVED;

	pr_debug(" o %s removing done\n", sd->s_name);

	mutex_unlock(&parent->d_inode->i_mutex);

	sysfs_drop_dentry(sd);
	sysfs_deactivate(sd);
	sysfs_put(sd);
}

void sysfs_remove_subdir(struct sysfs_dirent *sd)
{
	remove_dir(sd);
}


static void __sysfs_remove_dir(struct sysfs_dirent *dir_sd)
{
	struct sysfs_dirent *removed = NULL;
	struct sysfs_dirent **pos;
	struct dentry *dir;

	if (!dir_sd)
		return;

	dir = dir_sd->s_dentry;

	pr_debug("sysfs %s: removing dir\n", dir_sd->s_name);
	mutex_lock(&dir->d_inode->i_mutex);
	pos = &dir_sd->s_children;
	while (*pos) {
		struct sysfs_dirent *sd = *pos;

		if (sysfs_type(sd) && (sysfs_type(sd) & SYSFS_NOT_PINNED)) {
			sd->s_flags |= SYSFS_FLAG_REMOVED;
			*pos = sd->s_sibling;
			sd->s_sibling = removed;
			removed = sd;
		} else
			pos = &(*pos)->s_sibling;
	}
	mutex_unlock(&dir->d_inode->i_mutex);

	while (removed) {
		struct sysfs_dirent *sd = removed;

		removed = sd->s_sibling;
		sd->s_sibling = NULL;

		sysfs_drop_dentry(sd);
		sysfs_deactivate(sd);
		sysfs_put(sd);
	}

	remove_dir(dir_sd);
}

/**
 *	sysfs_remove_dir - remove an object's directory.
 *	@kobj:	object.
 *
 *	The only thing special about this is that we remove any files in
 *	the directory before we remove the directory, and we've inlined
 *	what used to be sysfs_rmdir() below, instead of calling separately.
 */

void sysfs_remove_dir(struct kobject * kobj)
{
	struct sysfs_dirent *sd = kobj->sd;

	spin_lock(&kobj_sysfs_assoc_lock);
	kobj->sd = NULL;
	spin_unlock(&kobj_sysfs_assoc_lock);

	__sysfs_remove_dir(sd);
}

int sysfs_rename_dir(struct kobject *kobj, struct sysfs_dirent *new_parent_sd,
		     const char *new_name)
{
	struct sysfs_dirent *sd = kobj->sd;
	struct dentry *new_parent = new_parent_sd->s_dentry;
	struct dentry *new_dentry;
	char *dup_name;
	int error;

	if (!new_parent_sd)
		return -EFAULT;

	down_write(&sysfs_rename_sem);
	mutex_lock(&new_parent->d_inode->i_mutex);

	new_dentry = lookup_one_len(new_name, new_parent, strlen(new_name));
	if (IS_ERR(new_dentry)) {
		error = PTR_ERR(new_dentry);
		goto out_unlock;
	}

	/* By allowing two different directories with the same
	 * d_parent we allow this routine to move between different
	 * shadows of the same directory
	 */
	error = -EINVAL;
	if (sd->s_parent->s_dentry->d_inode != new_parent->d_inode ||
	    new_dentry->d_parent->d_inode != new_parent->d_inode ||
	    new_dentry == sd->s_dentry)
		goto out_dput;

	error = -EEXIST;
	if (new_dentry->d_inode)
		goto out_dput;

	/* rename kobject and sysfs_dirent */
	error = -ENOMEM;
	new_name = dup_name = kstrdup(new_name, GFP_KERNEL);
	if (!new_name)
		goto out_drop;

	error = kobject_set_name(kobj, "%s", new_name);
	if (error)
		goto out_free;

	kfree(sd->s_name);
	sd->s_name = new_name;

	/* move under the new parent */
	d_add(new_dentry, NULL);
	d_move(sd->s_dentry, new_dentry);

	sysfs_unlink_sibling(sd);
	sysfs_get(new_parent_sd);
	sysfs_put(sd->s_parent);
	sd->s_parent = new_parent_sd;
	sysfs_link_sibling(sd);

	error = 0;
	goto out_unlock;

 out_free:
	kfree(dup_name);
 out_drop:
	d_drop(new_dentry);
 out_dput:
	dput(new_dentry);
 out_unlock:
	mutex_unlock(&new_parent->d_inode->i_mutex);
	up_write(&sysfs_rename_sem);
	return error;
}

int sysfs_move_dir(struct kobject *kobj, struct kobject *new_parent)
{
	struct dentry *old_parent_dentry, *new_parent_dentry, *new_dentry;
	struct sysfs_dirent *new_parent_sd, *sd;
	int error;

	old_parent_dentry = kobj->parent ?
		kobj->parent->sd->s_dentry : sysfs_mount->mnt_sb->s_root;
	new_parent_dentry = new_parent ?
		new_parent->sd->s_dentry : sysfs_mount->mnt_sb->s_root;

	if (old_parent_dentry->d_inode == new_parent_dentry->d_inode)
		return 0;	/* nothing to move */
again:
	mutex_lock(&old_parent_dentry->d_inode->i_mutex);
	if (!mutex_trylock(&new_parent_dentry->d_inode->i_mutex)) {
		mutex_unlock(&old_parent_dentry->d_inode->i_mutex);
		goto again;
	}

	new_parent_sd = new_parent_dentry->d_fsdata;
	sd = kobj->sd;

	new_dentry = lookup_one_len(kobj->name, new_parent_dentry,
				    strlen(kobj->name));
	if (IS_ERR(new_dentry)) {
		error = PTR_ERR(new_dentry);
		goto out;
	} else
		error = 0;
	d_add(new_dentry, NULL);
	d_move(sd->s_dentry, new_dentry);
	dput(new_dentry);

	/* Remove from old parent's list and insert into new parent's list. */
	sysfs_unlink_sibling(sd);
	sysfs_get(new_parent_sd);
	sysfs_put(sd->s_parent);
	sd->s_parent = new_parent_sd;
	sysfs_link_sibling(sd);

out:
	mutex_unlock(&new_parent_dentry->d_inode->i_mutex);
	mutex_unlock(&old_parent_dentry->d_inode->i_mutex);

	return error;
}

static int sysfs_dir_open(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_path.dentry;
	struct sysfs_dirent * parent_sd = dentry->d_fsdata;
	struct sysfs_dirent * sd;

	mutex_lock(&dentry->d_inode->i_mutex);
	sd = sysfs_new_dirent("_DIR_", 0, 0);
	if (sd)
		sysfs_attach_dirent(sd, parent_sd, NULL);
	mutex_unlock(&dentry->d_inode->i_mutex);

	file->private_data = sd;
	return sd ? 0 : -ENOMEM;
}

static int sysfs_dir_close(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_path.dentry;
	struct sysfs_dirent * cursor = file->private_data;

	mutex_lock(&dentry->d_inode->i_mutex);
	sysfs_unlink_sibling(cursor);
	mutex_unlock(&dentry->d_inode->i_mutex);

	release_sysfs_dirent(cursor);

	return 0;
}

/* Relationship between s_mode and the DT_xxx types */
static inline unsigned char dt_type(struct sysfs_dirent *sd)
{
	return (sd->s_mode >> 12) & 15;
}

static int sysfs_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct sysfs_dirent * parent_sd = dentry->d_fsdata;
	struct sysfs_dirent *cursor = filp->private_data;
	struct sysfs_dirent **pos;
	ino_t ino;
	int i = filp->f_pos;

	switch (i) {
		case 0:
			ino = parent_sd->s_ino;
			if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		case 1:
			if (parent_sd->s_parent)
				ino = parent_sd->s_parent->s_ino;
			else
				ino = parent_sd->s_ino;
			if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		default:
			pos = &parent_sd->s_children;
			while (*pos != cursor)
				pos = &(*pos)->s_sibling;

			/* unlink cursor */
			*pos = cursor->s_sibling;

			if (filp->f_pos == 2)
				pos = &parent_sd->s_children;

			for ( ; *pos; pos = &(*pos)->s_sibling) {
				struct sysfs_dirent *next = *pos;
				const char * name;
				int len;

				if (!sysfs_type(next))
					continue;

				name = next->s_name;
				len = strlen(name);
				ino = next->s_ino;

				if (filldir(dirent, name, len, filp->f_pos, ino,
						 dt_type(next)) < 0)
					break;

				filp->f_pos++;
			}

			/* put cursor back in */
			cursor->s_sibling = *pos;
			*pos = cursor;
	}
	return 0;
}

static loff_t sysfs_dir_lseek(struct file * file, loff_t offset, int origin)
{
	struct dentry * dentry = file->f_path.dentry;

	mutex_lock(&dentry->d_inode->i_mutex);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (file->f_pos >= 2) {
			struct sysfs_dirent *sd = dentry->d_fsdata;
			struct sysfs_dirent *cursor = file->private_data;
			struct sysfs_dirent **pos;
			loff_t n = file->f_pos - 2;

			sysfs_unlink_sibling(cursor);

			pos = &sd->s_children;
			while (n && *pos) {
				struct sysfs_dirent *next = *pos;
				if (sysfs_type(next))
					n--;
				pos = &(*pos)->s_sibling;
			}

			cursor->s_sibling = *pos;
			*pos = cursor;
		}
	}
	mutex_unlock(&dentry->d_inode->i_mutex);
	return offset;
}


/**
 *	sysfs_make_shadowed_dir - Setup so a directory can be shadowed
 *	@kobj:	object we're creating shadow of.
 */

int sysfs_make_shadowed_dir(struct kobject *kobj,
	void * (*follow_link)(struct dentry *, struct nameidata *))
{
	struct inode *inode;
	struct inode_operations *i_op;

	inode = kobj->sd->s_dentry->d_inode;
	if (inode->i_op != &sysfs_dir_inode_operations)
		return -EINVAL;

	i_op = kmalloc(sizeof(*i_op), GFP_KERNEL);
	if (!i_op)
		return -ENOMEM;

	memcpy(i_op, &sysfs_dir_inode_operations, sizeof(*i_op));
	i_op->follow_link = follow_link;

	/* Locking of inode->i_op?
	 * Since setting i_op is a single word write and they
	 * are atomic we should be ok here.
	 */
	inode->i_op = i_op;
	return 0;
}

/**
 *	sysfs_create_shadow_dir - create a shadow directory for an object.
 *	@kobj:	object we're creating directory for.
 *
 *	sysfs_make_shadowed_dir must already have been called on this
 *	directory.
 */

struct sysfs_dirent *sysfs_create_shadow_dir(struct kobject *kobj)
{
	struct dentry *dir = kobj->sd->s_dentry;
	struct inode *inode = dir->d_inode;
	struct dentry *parent = dir->d_parent;
	struct sysfs_dirent *parent_sd = parent->d_fsdata;
	struct dentry *shadow;
	struct sysfs_dirent *sd;

	sd = ERR_PTR(-EINVAL);
	if (!sysfs_is_shadowed_inode(inode))
		goto out;

	shadow = d_alloc(parent, &dir->d_name);
	if (!shadow)
		goto nomem;

	sd = sysfs_new_dirent("_SHADOW_", inode->i_mode, SYSFS_DIR);
	if (!sd)
		goto nomem;
	sd->s_elem.dir.kobj = kobj;
	/* point to parent_sd but don't attach to it */
	sd->s_parent = sysfs_get(parent_sd);
	sysfs_attach_dirent(sd, NULL, shadow);

	d_instantiate(shadow, igrab(inode));
	inc_nlink(inode);
	inc_nlink(parent->d_inode);

	dget(shadow);		/* Extra count - pin the dentry in core */

out:
	return sd;
nomem:
	dput(shadow);
	sd = ERR_PTR(-ENOMEM);
	goto out;
}

/**
 *	sysfs_remove_shadow_dir - remove an object's directory.
 *	@shadow_sd: sysfs_dirent of shadow directory
 *
 *	The only thing special about this is that we remove any files in
 *	the directory before we remove the directory, and we've inlined
 *	what used to be sysfs_rmdir() below, instead of calling separately.
 */

void sysfs_remove_shadow_dir(struct sysfs_dirent *shadow_sd)
{
	__sysfs_remove_dir(shadow_sd);
}

const struct file_operations sysfs_dir_operations = {
	.open		= sysfs_dir_open,
	.release	= sysfs_dir_close,
	.llseek		= sysfs_dir_lseek,
	.read		= generic_read_dir,
	.readdir	= sysfs_readdir,
};
