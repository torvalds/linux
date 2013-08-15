/*
 * fs/sysfs/dir.c - sysfs core and dir operation implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/idr.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/hash.h>
#include "sysfs.h"

DEFINE_MUTEX(sysfs_mutex);
DEFINE_SPINLOCK(sysfs_assoc_lock);

#define to_sysfs_dirent(X) rb_entry((X), struct sysfs_dirent, s_rb);

static DEFINE_SPINLOCK(sysfs_ino_lock);
static DEFINE_IDA(sysfs_ino_ida);

/**
 *	sysfs_name_hash
 *	@ns:   Namespace tag to hash
 *	@name: Null terminated string to hash
 *
 *	Returns 31 bit hash of ns + name (so it fits in an off_t )
 */
static unsigned int sysfs_name_hash(const void *ns, const char *name)
{
	unsigned long hash = init_name_hash();
	unsigned int len = strlen(name);
	while (len--)
		hash = partial_name_hash(*name++, hash);
	hash = ( end_name_hash(hash) ^ hash_ptr( (void *)ns, 31 ) );
	hash &= 0x7fffffffU;
	/* Reserve hash numbers 0, 1 and INT_MAX for magic directory entries */
	if (hash < 1)
		hash += 2;
	if (hash >= INT_MAX)
		hash = INT_MAX - 1;
	return hash;
}

static int sysfs_name_compare(unsigned int hash, const void *ns,
	const char *name, const struct sysfs_dirent *sd)
{
	if (hash != sd->s_hash)
		return hash - sd->s_hash;
	if (ns != sd->s_ns)
		return ns - sd->s_ns;
	return strcmp(name, sd->s_name);
}

static int sysfs_sd_compare(const struct sysfs_dirent *left,
			    const struct sysfs_dirent *right)
{
	return sysfs_name_compare(left->s_hash, left->s_ns, left->s_name,
				  right);
}

/**
 *	sysfs_link_sibling - link sysfs_dirent into sibling rbtree
 *	@sd: sysfs_dirent of interest
 *
 *	Link @sd into its sibling rbtree which starts from
 *	sd->s_parent->s_dir.children.
 *
 *	Locking:
 *	mutex_lock(sysfs_mutex)
 *
 *	RETURNS:
 *	0 on susccess -EEXIST on failure.
 */
static int sysfs_link_sibling(struct sysfs_dirent *sd)
{
	struct rb_node **node = &sd->s_parent->s_dir.children.rb_node;
	struct rb_node *parent = NULL;

	if (sysfs_type(sd) == SYSFS_DIR)
		sd->s_parent->s_dir.subdirs++;

	while (*node) {
		struct sysfs_dirent *pos;
		int result;

		pos = to_sysfs_dirent(*node);
		parent = *node;
		result = sysfs_sd_compare(sd, pos);
		if (result < 0)
			node = &pos->s_rb.rb_left;
		else if (result > 0)
			node = &pos->s_rb.rb_right;
		else
			return -EEXIST;
	}
	/* add new node and rebalance the tree */
	rb_link_node(&sd->s_rb, parent, node);
	rb_insert_color(&sd->s_rb, &sd->s_parent->s_dir.children);
	return 0;
}

/**
 *	sysfs_unlink_sibling - unlink sysfs_dirent from sibling rbtree
 *	@sd: sysfs_dirent of interest
 *
 *	Unlink @sd from its sibling rbtree which starts from
 *	sd->s_parent->s_dir.children.
 *
 *	Locking:
 *	mutex_lock(sysfs_mutex)
 */
static void sysfs_unlink_sibling(struct sysfs_dirent *sd)
{
	if (sysfs_type(sd) == SYSFS_DIR)
		sd->s_parent->s_dir.subdirs--;

	rb_erase(&sd->s_rb, &sd->s_parent->s_dir.children);
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

/* Test for attributes that want to ignore lockdep for read-locking */
static bool ignore_lockdep(struct sysfs_dirent *sd)
{
	return sysfs_type(sd) == SYSFS_KOBJ_ATTR &&
			sd->s_attr.attr->ignore_lockdep;
}

#else

static inline bool ignore_lockdep(struct sysfs_dirent *sd)
{
	return true;
}

#endif

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

	if (!atomic_inc_unless_negative(&sd->s_active))
		return NULL;

	if (likely(!ignore_lockdep(sd)))
		rwsem_acquire_read(&sd->dep_map, 0, 1, _RET_IP_);
	return sd;
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
	int v;

	if (unlikely(!sd))
		return;

	if (likely(!ignore_lockdep(sd)))
		rwsem_release(&sd->dep_map, 1, _RET_IP_);
	v = atomic_dec_return(&sd->s_active);
	if (likely(v != SD_DEACTIVATED_BIAS))
		return;

	/* atomic_dec_return() is a mb(), we'll always see the updated
	 * sd->u.completion.
	 */
	complete(sd->u.completion);
}

/**
 *	sysfs_deactivate - deactivate sysfs_dirent
 *	@sd: sysfs_dirent to deactivate
 *
 *	Deny new active references and drain existing ones.
 */
static void sysfs_deactivate(struct sysfs_dirent *sd)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	int v;

	BUG_ON(!(sd->s_flags & SYSFS_FLAG_REMOVED));

	if (!(sysfs_type(sd) & SYSFS_ACTIVE_REF))
		return;

	sd->u.completion = (void *)&wait;

	rwsem_acquire(&sd->dep_map, 0, 0, _RET_IP_);
	/* atomic_add_return() is a mb(), put_active() will always see
	 * the updated sd->u.completion.
	 */
	v = atomic_add_return(SD_DEACTIVATED_BIAS, &sd->s_active);

	if (v != SD_DEACTIVATED_BIAS) {
		lock_contended(&sd->dep_map, _RET_IP_);
		wait_for_completion(&wait);
	}

	lock_acquired(&sd->dep_map, _RET_IP_);
	rwsem_release(&sd->dep_map, 1, _RET_IP_);
}

static int sysfs_alloc_ino(unsigned int *pino)
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

static void sysfs_free_ino(unsigned int ino)
{
	spin_lock(&sysfs_ino_lock);
	ida_remove(&sysfs_ino_ida, ino);
	spin_unlock(&sysfs_ino_lock);
}

void release_sysfs_dirent(struct sysfs_dirent * sd)
{
	struct sysfs_dirent *parent_sd;

 repeat:
	/* Moving/renaming is always done while holding reference.
	 * sd->s_parent won't change beneath us.
	 */
	parent_sd = sd->s_parent;

	WARN(!(sd->s_flags & SYSFS_FLAG_REMOVED),
		"sysfs: free using entry: %s/%s\n",
		parent_sd ? parent_sd->s_name : "", sd->s_name);

	if (sysfs_type(sd) == SYSFS_KOBJ_LINK)
		sysfs_put(sd->s_symlink.target_sd);
	if (sysfs_type(sd) & SYSFS_COPY_NAME)
		kfree(sd->s_name);
	if (sd->s_iattr && sd->s_iattr->ia_secdata)
		security_release_secctx(sd->s_iattr->ia_secdata,
					sd->s_iattr->ia_secdata_len);
	kfree(sd->s_iattr);
	sysfs_free_ino(sd->s_ino);
	kmem_cache_free(sysfs_dir_cachep, sd);

	sd = parent_sd;
	if (sd && atomic_dec_and_test(&sd->s_count))
		goto repeat;
}

static int sysfs_dentry_delete(const struct dentry *dentry)
{
	struct sysfs_dirent *sd = dentry->d_fsdata;
	return !(sd && !(sd->s_flags & SYSFS_FLAG_REMOVED));
}

static int sysfs_dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct sysfs_dirent *sd;
	int is_dir;
	int type;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	sd = dentry->d_fsdata;
	mutex_lock(&sysfs_mutex);

	/* The sysfs dirent has been deleted */
	if (sd->s_flags & SYSFS_FLAG_REMOVED)
		goto out_bad;

	/* The sysfs dirent has been moved? */
	if (dentry->d_parent->d_fsdata != sd->s_parent)
		goto out_bad;

	/* The sysfs dirent has been renamed */
	if (strcmp(dentry->d_name.name, sd->s_name) != 0)
		goto out_bad;

	/* The sysfs dirent has been moved to a different namespace */
	type = KOBJ_NS_TYPE_NONE;
	if (sd->s_parent) {
		type = sysfs_ns_type(sd->s_parent);
		if (type != KOBJ_NS_TYPE_NONE &&
				sysfs_info(dentry->d_sb)->ns[type] != sd->s_ns)
			goto out_bad;
	}

	mutex_unlock(&sysfs_mutex);
out_valid:
	return 1;
out_bad:
	/* Remove the dentry from the dcache hashes.
	 * If this is a deleted dentry we use d_drop instead of d_delete
	 * so sysfs doesn't need to cope with negative dentries.
	 *
	 * If this is a dentry that has simply been renamed we
	 * use d_drop to remove it from the dcache lookup on its
	 * old parent.  If this dentry persists later when a lookup
	 * is performed at its new name the dentry will be readded
	 * to the dcache hashes.
	 */
	is_dir = (sysfs_type(sd) == SYSFS_DIR);
	mutex_unlock(&sysfs_mutex);
	if (is_dir) {
		/* If we have submounts we must allow the vfs caches
		 * to lie about the state of the filesystem to prevent
		 * leaks and other nasty things.
		 */
		if (have_submounts(dentry))
			goto out_valid;
		shrink_dcache_parent(dentry);
	}
	d_drop(dentry);
	return 0;
}

static void sysfs_dentry_release(struct dentry *dentry)
{
	sysfs_put(dentry->d_fsdata);
}

const struct dentry_operations sysfs_dentry_ops = {
	.d_revalidate	= sysfs_dentry_revalidate,
	.d_delete	= sysfs_dentry_delete,
	.d_release	= sysfs_dentry_release,
};

struct sysfs_dirent *sysfs_new_dirent(const char *name, umode_t mode, int type)
{
	char *dup_name = NULL;
	struct sysfs_dirent *sd;

	if (type & SYSFS_COPY_NAME) {
		name = dup_name = kstrdup(name, GFP_KERNEL);
		if (!name)
			return NULL;
	}

	sd = kmem_cache_zalloc(sysfs_dir_cachep, GFP_KERNEL);
	if (!sd)
		goto err_out1;

	if (sysfs_alloc_ino(&sd->s_ino))
		goto err_out2;

	atomic_set(&sd->s_count, 1);
	atomic_set(&sd->s_active, 0);

	sd->s_name = name;
	sd->s_mode = mode;
	sd->s_flags = type | SYSFS_FLAG_REMOVED;

	return sd;

 err_out2:
	kmem_cache_free(sysfs_dir_cachep, sd);
 err_out1:
	kfree(dup_name);
	return NULL;
}

/**
 *	sysfs_addrm_start - prepare for sysfs_dirent add/remove
 *	@acxt: pointer to sysfs_addrm_cxt to be used
 *	@parent_sd: parent sysfs_dirent
 *
 *	This function is called when the caller is about to add or
 *	remove sysfs_dirent under @parent_sd.  This function acquires
 *	sysfs_mutex.  @acxt is used to keep and pass context to
 *	other addrm functions.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).  sysfs_mutex is locked on
 *	return.
 */
void sysfs_addrm_start(struct sysfs_addrm_cxt *acxt,
		       struct sysfs_dirent *parent_sd)
{
	memset(acxt, 0, sizeof(*acxt));
	acxt->parent_sd = parent_sd;

	mutex_lock(&sysfs_mutex);
}

/**
 *	__sysfs_add_one - add sysfs_dirent to parent without warning
 *	@acxt: addrm context to use
 *	@sd: sysfs_dirent to be added
 *
 *	Get @acxt->parent_sd and set sd->s_parent to it and increment
 *	nlink of parent inode if @sd is a directory and link into the
 *	children list of the parent.
 *
 *	This function should be called between calls to
 *	sysfs_addrm_start() and sysfs_addrm_finish() and should be
 *	passed the same @acxt as passed to sysfs_addrm_start().
 *
 *	LOCKING:
 *	Determined by sysfs_addrm_start().
 *
 *	RETURNS:
 *	0 on success, -EEXIST if entry with the given name already
 *	exists.
 */
int __sysfs_add_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd)
{
	struct sysfs_inode_attrs *ps_iattr;
	int ret;

	if (!!sysfs_ns_type(acxt->parent_sd) != !!sd->s_ns) {
		WARN(1, KERN_WARNING "sysfs: ns %s in '%s' for '%s'\n",
			sysfs_ns_type(acxt->parent_sd)? "required": "invalid",
			acxt->parent_sd->s_name, sd->s_name);
		return -EINVAL;
	}

	sd->s_hash = sysfs_name_hash(sd->s_ns, sd->s_name);
	sd->s_parent = sysfs_get(acxt->parent_sd);

	ret = sysfs_link_sibling(sd);
	if (ret)
		return ret;

	/* Update timestamps on the parent */
	ps_iattr = acxt->parent_sd->s_iattr;
	if (ps_iattr) {
		struct iattr *ps_iattrs = &ps_iattr->ia_iattr;
		ps_iattrs->ia_ctime = ps_iattrs->ia_mtime = CURRENT_TIME;
	}

	/* Mark the entry added into directory tree */
	sd->s_flags &= ~SYSFS_FLAG_REMOVED;

	return 0;
}

/**
 *	sysfs_pathname - return full path to sysfs dirent
 *	@sd: sysfs_dirent whose path we want
 *	@path: caller allocated buffer of size PATH_MAX
 *
 *	Gives the name "/" to the sysfs_root entry; any path returned
 *	is relative to wherever sysfs is mounted.
 */
static char *sysfs_pathname(struct sysfs_dirent *sd, char *path)
{
	if (sd->s_parent) {
		sysfs_pathname(sd->s_parent, path);
		strlcat(path, "/", PATH_MAX);
	}
	strlcat(path, sd->s_name, PATH_MAX);
	return path;
}

/**
 *	sysfs_add_one - add sysfs_dirent to parent
 *	@acxt: addrm context to use
 *	@sd: sysfs_dirent to be added
 *
 *	Get @acxt->parent_sd and set sd->s_parent to it and increment
 *	nlink of parent inode if @sd is a directory and link into the
 *	children list of the parent.
 *
 *	This function should be called between calls to
 *	sysfs_addrm_start() and sysfs_addrm_finish() and should be
 *	passed the same @acxt as passed to sysfs_addrm_start().
 *
 *	LOCKING:
 *	Determined by sysfs_addrm_start().
 *
 *	RETURNS:
 *	0 on success, -EEXIST if entry with the given name already
 *	exists.
 */
int sysfs_add_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd)
{
	int ret;

	ret = __sysfs_add_one(acxt, sd);
	if (ret == -EEXIST) {
		char *path = kzalloc(PATH_MAX, GFP_KERNEL);
		WARN(1, KERN_WARNING
		     "sysfs: cannot create duplicate filename '%s'\n",
		     (path == NULL) ? sd->s_name
				    : (sysfs_pathname(acxt->parent_sd, path),
				       strlcat(path, "/", PATH_MAX),
				       strlcat(path, sd->s_name, PATH_MAX),
				       path));
		kfree(path);
	}

	return ret;
}

/**
 *	sysfs_remove_one - remove sysfs_dirent from parent
 *	@acxt: addrm context to use
 *	@sd: sysfs_dirent to be removed
 *
 *	Mark @sd removed and drop nlink of parent inode if @sd is a
 *	directory.  @sd is unlinked from the children list.
 *
 *	This function should be called between calls to
 *	sysfs_addrm_start() and sysfs_addrm_finish() and should be
 *	passed the same @acxt as passed to sysfs_addrm_start().
 *
 *	LOCKING:
 *	Determined by sysfs_addrm_start().
 */
void sysfs_remove_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd)
{
	struct sysfs_inode_attrs *ps_iattr;

	BUG_ON(sd->s_flags & SYSFS_FLAG_REMOVED);

	sysfs_unlink_sibling(sd);

	/* Update timestamps on the parent */
	ps_iattr = acxt->parent_sd->s_iattr;
	if (ps_iattr) {
		struct iattr *ps_iattrs = &ps_iattr->ia_iattr;
		ps_iattrs->ia_ctime = ps_iattrs->ia_mtime = CURRENT_TIME;
	}

	sd->s_flags |= SYSFS_FLAG_REMOVED;
	sd->u.removed_list = acxt->removed;
	acxt->removed = sd;
}

/**
 *	sysfs_addrm_finish - finish up sysfs_dirent add/remove
 *	@acxt: addrm context to finish up
 *
 *	Finish up sysfs_dirent add/remove.  Resources acquired by
 *	sysfs_addrm_start() are released and removed sysfs_dirents are
 *	cleaned up.
 *
 *	LOCKING:
 *	sysfs_mutex is released.
 */
void sysfs_addrm_finish(struct sysfs_addrm_cxt *acxt)
{
	/* release resources acquired by sysfs_addrm_start() */
	mutex_unlock(&sysfs_mutex);

	/* kill removed sysfs_dirents */
	while (acxt->removed) {
		struct sysfs_dirent *sd = acxt->removed;

		acxt->removed = sd->u.removed_list;

		sysfs_deactivate(sd);
		unmap_bin_file(sd);
		sysfs_put(sd);
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
 *	mutex_lock(sysfs_mutex)
 *
 *	RETURNS:
 *	Pointer to sysfs_dirent if found, NULL if not.
 */
struct sysfs_dirent *sysfs_find_dirent(struct sysfs_dirent *parent_sd,
				       const void *ns,
				       const unsigned char *name)
{
	struct rb_node *node = parent_sd->s_dir.children.rb_node;
	unsigned int hash;

	if (!!sysfs_ns_type(parent_sd) != !!ns) {
		WARN(1, KERN_WARNING "sysfs: ns %s in '%s' for '%s'\n",
			sysfs_ns_type(parent_sd)? "required": "invalid",
			parent_sd->s_name, name);
		return NULL;
	}

	hash = sysfs_name_hash(ns, name);
	while (node) {
		struct sysfs_dirent *sd;
		int result;

		sd = to_sysfs_dirent(node);
		result = sysfs_name_compare(hash, ns, name, sd);
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return sd;
	}
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
 *	Kernel thread context (may sleep).  Grabs sysfs_mutex.
 *
 *	RETURNS:
 *	Pointer to sysfs_dirent if found, NULL if not.
 */
struct sysfs_dirent *sysfs_get_dirent(struct sysfs_dirent *parent_sd,
				      const void *ns,
				      const unsigned char *name)
{
	struct sysfs_dirent *sd;

	mutex_lock(&sysfs_mutex);
	sd = sysfs_find_dirent(parent_sd, ns, name);
	sysfs_get(sd);
	mutex_unlock(&sysfs_mutex);

	return sd;
}
EXPORT_SYMBOL_GPL(sysfs_get_dirent);

static int create_dir(struct kobject *kobj, struct sysfs_dirent *parent_sd,
	enum kobj_ns_type type, const void *ns, const char *name,
	struct sysfs_dirent **p_sd)
{
	umode_t mode = S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO;
	struct sysfs_addrm_cxt acxt;
	struct sysfs_dirent *sd;
	int rc;

	/* allocate */
	sd = sysfs_new_dirent(name, mode, SYSFS_DIR);
	if (!sd)
		return -ENOMEM;

	sd->s_flags |= (type << SYSFS_NS_TYPE_SHIFT);
	sd->s_ns = ns;
	sd->s_dir.kobj = kobj;

	/* link in */
	sysfs_addrm_start(&acxt, parent_sd);
	rc = sysfs_add_one(&acxt, sd);
	sysfs_addrm_finish(&acxt);

	if (rc == 0)
		*p_sd = sd;
	else
		sysfs_put(sd);

	return rc;
}

int sysfs_create_subdir(struct kobject *kobj, const char *name,
			struct sysfs_dirent **p_sd)
{
	return create_dir(kobj, kobj->sd,
			  KOBJ_NS_TYPE_NONE, NULL, name, p_sd);
}

/**
 *	sysfs_read_ns_type: return associated ns_type
 *	@kobj: the kobject being queried
 *
 *	Each kobject can be tagged with exactly one namespace type
 *	(i.e. network or user).  Return the ns_type associated with
 *	this object if any
 */
static enum kobj_ns_type sysfs_read_ns_type(struct kobject *kobj)
{
	const struct kobj_ns_type_operations *ops;
	enum kobj_ns_type type;

	ops = kobj_child_ns_ops(kobj);
	if (!ops)
		return KOBJ_NS_TYPE_NONE;

	type = ops->type;
	BUG_ON(type <= KOBJ_NS_TYPE_NONE);
	BUG_ON(type >= KOBJ_NS_TYPES);
	BUG_ON(!kobj_ns_type_registered(type));

	return type;
}

/**
 *	sysfs_create_dir - create a directory for an object.
 *	@kobj:		object we're creating directory for. 
 */
int sysfs_create_dir(struct kobject * kobj)
{
	enum kobj_ns_type type;
	struct sysfs_dirent *parent_sd, *sd;
	const void *ns = NULL;
	int error = 0;

	BUG_ON(!kobj);

	if (kobj->parent)
		parent_sd = kobj->parent->sd;
	else
		parent_sd = &sysfs_root;

	if (!parent_sd)
		return -ENOENT;

	if (sysfs_ns_type(parent_sd))
		ns = kobj->ktype->namespace(kobj);
	type = sysfs_read_ns_type(kobj);

	error = create_dir(kobj, parent_sd, type, ns, kobject_name(kobj), &sd);
	if (!error)
		kobj->sd = sd;
	return error;
}

static struct dentry * sysfs_lookup(struct inode *dir, struct dentry *dentry,
				unsigned int flags)
{
	struct dentry *ret = NULL;
	struct dentry *parent = dentry->d_parent;
	struct sysfs_dirent *parent_sd = parent->d_fsdata;
	struct sysfs_dirent *sd;
	struct inode *inode;
	enum kobj_ns_type type;
	const void *ns;

	mutex_lock(&sysfs_mutex);

	type = sysfs_ns_type(parent_sd);
	ns = sysfs_info(dir->i_sb)->ns[type];

	sd = sysfs_find_dirent(parent_sd, ns, dentry->d_name.name);

	/* no such entry */
	if (!sd) {
		ret = ERR_PTR(-ENOENT);
		goto out_unlock;
	}
	dentry->d_fsdata = sysfs_get(sd);

	/* attach dentry and inode */
	inode = sysfs_get_inode(dir->i_sb, sd);
	if (!inode) {
		ret = ERR_PTR(-ENOMEM);
		goto out_unlock;
	}

	/* instantiate and hash dentry */
	ret = d_materialise_unique(dentry, inode);
 out_unlock:
	mutex_unlock(&sysfs_mutex);
	return ret;
}

const struct inode_operations sysfs_dir_inode_operations = {
	.lookup		= sysfs_lookup,
	.permission	= sysfs_permission,
	.setattr	= sysfs_setattr,
	.getattr	= sysfs_getattr,
	.setxattr	= sysfs_setxattr,
};

static void remove_dir(struct sysfs_dirent *sd)
{
	struct sysfs_addrm_cxt acxt;

	sysfs_addrm_start(&acxt, sd->s_parent);
	sysfs_remove_one(&acxt, sd);
	sysfs_addrm_finish(&acxt);
}

void sysfs_remove_subdir(struct sysfs_dirent *sd)
{
	remove_dir(sd);
}


static void __sysfs_remove_dir(struct sysfs_dirent *dir_sd)
{
	struct sysfs_addrm_cxt acxt;
	struct rb_node *pos;

	if (!dir_sd)
		return;

	pr_debug("sysfs %s: removing dir\n", dir_sd->s_name);
	sysfs_addrm_start(&acxt, dir_sd);
	pos = rb_first(&dir_sd->s_dir.children);
	while (pos) {
		struct sysfs_dirent *sd = to_sysfs_dirent(pos);
		pos = rb_next(pos);
		if (sysfs_type(sd) != SYSFS_DIR)
			sysfs_remove_one(&acxt, sd);
	}
	sysfs_addrm_finish(&acxt);

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

	spin_lock(&sysfs_assoc_lock);
	kobj->sd = NULL;
	spin_unlock(&sysfs_assoc_lock);

	__sysfs_remove_dir(sd);
}

int sysfs_rename(struct sysfs_dirent *sd,
	struct sysfs_dirent *new_parent_sd, const void *new_ns,
	const char *new_name)
{
	int error;

	mutex_lock(&sysfs_mutex);

	error = 0;
	if ((sd->s_parent == new_parent_sd) && (sd->s_ns == new_ns) &&
	    (strcmp(sd->s_name, new_name) == 0))
		goto out;	/* nothing to rename */

	error = -EEXIST;
	if (sysfs_find_dirent(new_parent_sd, new_ns, new_name))
		goto out;

	/* rename sysfs_dirent */
	if (strcmp(sd->s_name, new_name) != 0) {
		error = -ENOMEM;
		new_name = kstrdup(new_name, GFP_KERNEL);
		if (!new_name)
			goto out;

		kfree(sd->s_name);
		sd->s_name = new_name;
	}

	/* Move to the appropriate place in the appropriate directories rbtree. */
	sysfs_unlink_sibling(sd);
	sysfs_get(new_parent_sd);
	sysfs_put(sd->s_parent);
	sd->s_ns = new_ns;
	sd->s_hash = sysfs_name_hash(sd->s_ns, sd->s_name);
	sd->s_parent = new_parent_sd;
	sysfs_link_sibling(sd);

	error = 0;
 out:
	mutex_unlock(&sysfs_mutex);
	return error;
}

int sysfs_rename_dir(struct kobject *kobj, const char *new_name)
{
	struct sysfs_dirent *parent_sd = kobj->sd->s_parent;
	const void *new_ns = NULL;

	if (sysfs_ns_type(parent_sd))
		new_ns = kobj->ktype->namespace(kobj);

	return sysfs_rename(kobj->sd, parent_sd, new_ns, new_name);
}

int sysfs_move_dir(struct kobject *kobj, struct kobject *new_parent_kobj)
{
	struct sysfs_dirent *sd = kobj->sd;
	struct sysfs_dirent *new_parent_sd;
	const void *new_ns = NULL;

	BUG_ON(!sd->s_parent);
	if (sysfs_ns_type(sd->s_parent))
		new_ns = kobj->ktype->namespace(kobj);
	new_parent_sd = new_parent_kobj && new_parent_kobj->sd ?
		new_parent_kobj->sd : &sysfs_root;

	return sysfs_rename(sd, new_parent_sd, new_ns, sd->s_name);
}

/* Relationship between s_mode and the DT_xxx types */
static inline unsigned char dt_type(struct sysfs_dirent *sd)
{
	return (sd->s_mode >> 12) & 15;
}

static int sysfs_dir_release(struct inode *inode, struct file *filp)
{
	sysfs_put(filp->private_data);
	return 0;
}

static struct sysfs_dirent *sysfs_dir_pos(const void *ns,
	struct sysfs_dirent *parent_sd,	loff_t hash, struct sysfs_dirent *pos)
{
	if (pos) {
		int valid = !(pos->s_flags & SYSFS_FLAG_REMOVED) &&
			pos->s_parent == parent_sd &&
			hash == pos->s_hash;
		sysfs_put(pos);
		if (!valid)
			pos = NULL;
	}
	if (!pos && (hash > 1) && (hash < INT_MAX)) {
		struct rb_node *node = parent_sd->s_dir.children.rb_node;
		while (node) {
			pos = to_sysfs_dirent(node);

			if (hash < pos->s_hash)
				node = node->rb_left;
			else if (hash > pos->s_hash)
				node = node->rb_right;
			else
				break;
		}
	}
	/* Skip over entries in the wrong namespace */
	while (pos && pos->s_ns != ns) {
		struct rb_node *node = rb_next(&pos->s_rb);
		if (!node)
			pos = NULL;
		else
			pos = to_sysfs_dirent(node);
	}
	return pos;
}

static struct sysfs_dirent *sysfs_dir_next_pos(const void *ns,
	struct sysfs_dirent *parent_sd,	ino_t ino, struct sysfs_dirent *pos)
{
	pos = sysfs_dir_pos(ns, parent_sd, ino, pos);
	if (pos) do {
		struct rb_node *node = rb_next(&pos->s_rb);
		if (!node)
			pos = NULL;
		else
			pos = to_sysfs_dirent(node);
	} while (pos && pos->s_ns != ns);
	return pos;
}

static int sysfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct sysfs_dirent * parent_sd = dentry->d_fsdata;
	struct sysfs_dirent *pos = file->private_data;
	enum kobj_ns_type type;
	const void *ns;

	type = sysfs_ns_type(parent_sd);
	ns = sysfs_info(dentry->d_sb)->ns[type];

	if (!dir_emit_dots(file, ctx))
		return 0;
	mutex_lock(&sysfs_mutex);
	for (pos = sysfs_dir_pos(ns, parent_sd, ctx->pos, pos);
	     pos;
	     pos = sysfs_dir_next_pos(ns, parent_sd, ctx->pos, pos)) {
		const char *name = pos->s_name;
		unsigned int type = dt_type(pos);
		int len = strlen(name);
		ino_t ino = pos->s_ino;
		ctx->pos = pos->s_hash;
		file->private_data = sysfs_get(pos);

		mutex_unlock(&sysfs_mutex);
		if (!dir_emit(ctx, name, len, ino, type))
			return 0;
		mutex_lock(&sysfs_mutex);
	}
	mutex_unlock(&sysfs_mutex);
	file->private_data = NULL;
	ctx->pos = INT_MAX;
	return 0;
}

static loff_t sysfs_dir_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file_inode(file);
	loff_t ret;

	mutex_lock(&inode->i_mutex);
	ret = generic_file_llseek(file, offset, whence);
	mutex_unlock(&inode->i_mutex);

	return ret;
}

const struct file_operations sysfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate	= sysfs_readdir,
	.release	= sysfs_dir_release,
	.llseek		= sysfs_dir_llseek,
};
