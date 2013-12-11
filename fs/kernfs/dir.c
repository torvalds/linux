/*
 * fs/kernfs/dir.c - kernfs directory implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/hash.h>

#include "kernfs-internal.h"

DEFINE_MUTEX(sysfs_mutex);

#define rb_to_kn(X) rb_entry((X), struct kernfs_node, rb)

/**
 *	sysfs_name_hash
 *	@name: Null terminated string to hash
 *	@ns:   Namespace tag to hash
 *
 *	Returns 31 bit hash of ns + name (so it fits in an off_t )
 */
static unsigned int sysfs_name_hash(const char *name, const void *ns)
{
	unsigned long hash = init_name_hash();
	unsigned int len = strlen(name);
	while (len--)
		hash = partial_name_hash(*name++, hash);
	hash = (end_name_hash(hash) ^ hash_ptr((void *)ns, 31));
	hash &= 0x7fffffffU;
	/* Reserve hash numbers 0, 1 and INT_MAX for magic directory entries */
	if (hash < 1)
		hash += 2;
	if (hash >= INT_MAX)
		hash = INT_MAX - 1;
	return hash;
}

static int sysfs_name_compare(unsigned int hash, const char *name,
			      const void *ns, const struct kernfs_node *kn)
{
	if (hash != kn->hash)
		return hash - kn->hash;
	if (ns != kn->ns)
		return ns - kn->ns;
	return strcmp(name, kn->name);
}

static int sysfs_sd_compare(const struct kernfs_node *left,
			    const struct kernfs_node *right)
{
	return sysfs_name_compare(left->hash, left->name, left->ns, right);
}

/**
 *	sysfs_link_sibling - link kernfs_node into sibling rbtree
 *	@kn: kernfs_node of interest
 *
 *	Link @kn into its sibling rbtree which starts from
 *	@kn->parent->dir.children.
 *
 *	Locking:
 *	mutex_lock(sysfs_mutex)
 *
 *	RETURNS:
 *	0 on susccess -EEXIST on failure.
 */
static int sysfs_link_sibling(struct kernfs_node *kn)
{
	struct rb_node **node = &kn->parent->dir.children.rb_node;
	struct rb_node *parent = NULL;

	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs++;

	while (*node) {
		struct kernfs_node *pos;
		int result;

		pos = rb_to_kn(*node);
		parent = *node;
		result = sysfs_sd_compare(kn, pos);
		if (result < 0)
			node = &pos->rb.rb_left;
		else if (result > 0)
			node = &pos->rb.rb_right;
		else
			return -EEXIST;
	}
	/* add new node and rebalance the tree */
	rb_link_node(&kn->rb, parent, node);
	rb_insert_color(&kn->rb, &kn->parent->dir.children);
	return 0;
}

/**
 *	sysfs_unlink_sibling - unlink kernfs_node from sibling rbtree
 *	@kn: kernfs_node of interest
 *
 *	Unlink @kn from its sibling rbtree which starts from
 *	kn->parent->dir.children.
 *
 *	Locking:
 *	mutex_lock(sysfs_mutex)
 */
static void sysfs_unlink_sibling(struct kernfs_node *kn)
{
	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs--;

	rb_erase(&kn->rb, &kn->parent->dir.children);
}

/**
 *	sysfs_get_active - get an active reference to kernfs_node
 *	@kn: kernfs_node to get an active reference to
 *
 *	Get an active reference of @kn.  This function is noop if @kn
 *	is NULL.
 *
 *	RETURNS:
 *	Pointer to @kn on success, NULL on failure.
 */
struct kernfs_node *sysfs_get_active(struct kernfs_node *kn)
{
	if (unlikely(!kn))
		return NULL;

	if (!atomic_inc_unless_negative(&kn->active))
		return NULL;

	if (kn->flags & KERNFS_LOCKDEP)
		rwsem_acquire_read(&kn->dep_map, 0, 1, _RET_IP_);
	return kn;
}

/**
 *	sysfs_put_active - put an active reference to kernfs_node
 *	@kn: kernfs_node to put an active reference to
 *
 *	Put an active reference to @kn.  This function is noop if @kn
 *	is NULL.
 */
void sysfs_put_active(struct kernfs_node *kn)
{
	int v;

	if (unlikely(!kn))
		return;

	if (kn->flags & KERNFS_LOCKDEP)
		rwsem_release(&kn->dep_map, 1, _RET_IP_);
	v = atomic_dec_return(&kn->active);
	if (likely(v != KN_DEACTIVATED_BIAS))
		return;

	/*
	 * atomic_dec_return() is a mb(), we'll always see the updated
	 * kn->u.completion.
	 */
	complete(kn->u.completion);
}

/**
 *	sysfs_deactivate - deactivate kernfs_node
 *	@kn: kernfs_node to deactivate
 *
 *	Deny new active references and drain existing ones.
 */
static void sysfs_deactivate(struct kernfs_node *kn)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	int v;

	BUG_ON(!(kn->flags & KERNFS_REMOVED));

	if (!(kernfs_type(kn) & KERNFS_ACTIVE_REF))
		return;

	kn->u.completion = (void *)&wait;

	rwsem_acquire(&kn->dep_map, 0, 0, _RET_IP_);
	/* atomic_add_return() is a mb(), put_active() will always see
	 * the updated kn->u.completion.
	 */
	v = atomic_add_return(KN_DEACTIVATED_BIAS, &kn->active);

	if (v != KN_DEACTIVATED_BIAS) {
		lock_contended(&kn->dep_map, _RET_IP_);
		wait_for_completion(&wait);
	}

	lock_acquired(&kn->dep_map, _RET_IP_);
	rwsem_release(&kn->dep_map, 1, _RET_IP_);
}

/**
 * kernfs_get - get a reference count on a kernfs_node
 * @kn: the target kernfs_node
 */
void kernfs_get(struct kernfs_node *kn)
{
	if (kn) {
		WARN_ON(!atomic_read(&kn->count));
		atomic_inc(&kn->count);
	}
}
EXPORT_SYMBOL_GPL(kernfs_get);

/**
 * kernfs_put - put a reference count on a kernfs_node
 * @kn: the target kernfs_node
 *
 * Put a reference count of @kn and destroy it if it reached zero.
 */
void kernfs_put(struct kernfs_node *kn)
{
	struct kernfs_node *parent;
	struct kernfs_root *root;

	if (!kn || !atomic_dec_and_test(&kn->count))
		return;
	root = kernfs_root(kn);
 repeat:
	/* Moving/renaming is always done while holding reference.
	 * kn->parent won't change beneath us.
	 */
	parent = kn->parent;

	WARN(!(kn->flags & KERNFS_REMOVED),
		"sysfs: free using entry: %s/%s\n",
		parent ? parent->name : "", kn->name);

	if (kernfs_type(kn) == KERNFS_LINK)
		kernfs_put(kn->symlink.target_kn);
	if (kernfs_type(kn) & KERNFS_COPY_NAME)
		kfree(kn->name);
	if (kn->iattr) {
		if (kn->iattr->ia_secdata)
			security_release_secctx(kn->iattr->ia_secdata,
						kn->iattr->ia_secdata_len);
		simple_xattrs_free(&kn->iattr->xattrs);
	}
	kfree(kn->iattr);
	ida_simple_remove(&root->ino_ida, kn->ino);
	kmem_cache_free(sysfs_dir_cachep, kn);

	kn = parent;
	if (kn) {
		if (atomic_dec_and_test(&kn->count))
			goto repeat;
	} else {
		/* just released the root kn, free @root too */
		ida_destroy(&root->ino_ida);
		kfree(root);
	}
}
EXPORT_SYMBOL_GPL(kernfs_put);

static int sysfs_dentry_delete(const struct dentry *dentry)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	return !(kn && !(kn->flags & KERNFS_REMOVED));
}

static int sysfs_dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct kernfs_node *kn;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	kn = dentry->d_fsdata;
	mutex_lock(&sysfs_mutex);

	/* The sysfs dirent has been deleted */
	if (kn->flags & KERNFS_REMOVED)
		goto out_bad;

	/* The sysfs dirent has been moved? */
	if (dentry->d_parent->d_fsdata != kn->parent)
		goto out_bad;

	/* The sysfs dirent has been renamed */
	if (strcmp(dentry->d_name.name, kn->name) != 0)
		goto out_bad;

	/* The sysfs dirent has been moved to a different namespace */
	if (kn->parent && kernfs_ns_enabled(kn->parent) &&
	    kernfs_info(dentry->d_sb)->ns != kn->ns)
		goto out_bad;

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
	mutex_unlock(&sysfs_mutex);

	/* If we have submounts we must allow the vfs caches
	 * to lie about the state of the filesystem to prevent
	 * leaks and other nasty things.
	 */
	if (check_submounts_and_drop(dentry) != 0)
		goto out_valid;

	return 0;
}

static void sysfs_dentry_release(struct dentry *dentry)
{
	kernfs_put(dentry->d_fsdata);
}

const struct dentry_operations sysfs_dentry_ops = {
	.d_revalidate	= sysfs_dentry_revalidate,
	.d_delete	= sysfs_dentry_delete,
	.d_release	= sysfs_dentry_release,
};

struct kernfs_node *sysfs_new_dirent(struct kernfs_root *root,
				     const char *name, umode_t mode, int type)
{
	char *dup_name = NULL;
	struct kernfs_node *kn;
	int ret;

	if (type & KERNFS_COPY_NAME) {
		name = dup_name = kstrdup(name, GFP_KERNEL);
		if (!name)
			return NULL;
	}

	kn = kmem_cache_zalloc(sysfs_dir_cachep, GFP_KERNEL);
	if (!kn)
		goto err_out1;

	ret = ida_simple_get(&root->ino_ida, 1, 0, GFP_KERNEL);
	if (ret < 0)
		goto err_out2;
	kn->ino = ret;

	atomic_set(&kn->count, 1);
	atomic_set(&kn->active, 0);

	kn->name = name;
	kn->mode = mode;
	kn->flags = type | KERNFS_REMOVED;

	return kn;

 err_out2:
	kmem_cache_free(sysfs_dir_cachep, kn);
 err_out1:
	kfree(dup_name);
	return NULL;
}

/**
 *	sysfs_addrm_start - prepare for kernfs_node add/remove
 *	@acxt: pointer to kernfs_addrm_cxt to be used
 *
 *	This function is called when the caller is about to add or remove
 *	kernfs_node.  This function acquires sysfs_mutex.  @acxt is used to
 *	keep and pass context to other addrm functions.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).  sysfs_mutex is locked on
 *	return.
 */
void sysfs_addrm_start(struct kernfs_addrm_cxt *acxt)
	__acquires(sysfs_mutex)
{
	memset(acxt, 0, sizeof(*acxt));

	mutex_lock(&sysfs_mutex);
}

/**
 *	sysfs_add_one - add kernfs_node to parent without warning
 *	@acxt: addrm context to use
 *	@kn: kernfs_node to be added
 *	@parent: the parent kernfs_node to add @kn to
 *
 *	Get @parent and set @kn->parent to it and increment nlink of the
 *	parent inode if @kn is a directory and link into the children list
 *	of the parent.
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
int sysfs_add_one(struct kernfs_addrm_cxt *acxt, struct kernfs_node *kn,
		  struct kernfs_node *parent)
{
	bool has_ns = kernfs_ns_enabled(parent);
	struct kernfs_iattrs *ps_iattr;
	int ret;

	if (has_ns != (bool)kn->ns) {
		WARN(1, KERN_WARNING "sysfs: ns %s in '%s' for '%s'\n",
		     has_ns ? "required" : "invalid", parent->name, kn->name);
		return -EINVAL;
	}

	if (kernfs_type(parent) != KERNFS_DIR)
		return -EINVAL;

	kn->hash = sysfs_name_hash(kn->name, kn->ns);
	kn->parent = parent;
	kernfs_get(parent);

	ret = sysfs_link_sibling(kn);
	if (ret)
		return ret;

	/* Update timestamps on the parent */
	ps_iattr = parent->iattr;
	if (ps_iattr) {
		struct iattr *ps_iattrs = &ps_iattr->ia_iattr;
		ps_iattrs->ia_ctime = ps_iattrs->ia_mtime = CURRENT_TIME;
	}

	/* Mark the entry added into directory tree */
	kn->flags &= ~KERNFS_REMOVED;

	return 0;
}

/**
 *	sysfs_remove_one - remove kernfs_node from parent
 *	@acxt: addrm context to use
 *	@kn: kernfs_node to be removed
 *
 *	Mark @kn removed and drop nlink of parent inode if @kn is a
 *	directory.  @kn is unlinked from the children list.
 *
 *	This function should be called between calls to
 *	sysfs_addrm_start() and sysfs_addrm_finish() and should be
 *	passed the same @acxt as passed to sysfs_addrm_start().
 *
 *	LOCKING:
 *	Determined by sysfs_addrm_start().
 */
static void sysfs_remove_one(struct kernfs_addrm_cxt *acxt,
			     struct kernfs_node *kn)
{
	struct kernfs_iattrs *ps_iattr;

	/*
	 * Removal can be called multiple times on the same node.  Only the
	 * first invocation is effective and puts the base ref.
	 */
	if (kn->flags & KERNFS_REMOVED)
		return;

	if (kn->parent) {
		sysfs_unlink_sibling(kn);

		/* Update timestamps on the parent */
		ps_iattr = kn->parent->iattr;
		if (ps_iattr) {
			ps_iattr->ia_iattr.ia_ctime = CURRENT_TIME;
			ps_iattr->ia_iattr.ia_mtime = CURRENT_TIME;
		}
	}

	kn->flags |= KERNFS_REMOVED;
	kn->u.removed_list = acxt->removed;
	acxt->removed = kn;
}

/**
 *	sysfs_addrm_finish - finish up kernfs_node add/remove
 *	@acxt: addrm context to finish up
 *
 *	Finish up kernfs_node add/remove.  Resources acquired by
 *	sysfs_addrm_start() are released and removed kernfs_nodes are
 *	cleaned up.
 *
 *	LOCKING:
 *	sysfs_mutex is released.
 */
void sysfs_addrm_finish(struct kernfs_addrm_cxt *acxt)
	__releases(sysfs_mutex)
{
	/* release resources acquired by sysfs_addrm_start() */
	mutex_unlock(&sysfs_mutex);

	/* kill removed kernfs_nodes */
	while (acxt->removed) {
		struct kernfs_node *kn = acxt->removed;

		acxt->removed = kn->u.removed_list;

		sysfs_deactivate(kn);
		sysfs_unmap_bin_file(kn);
		kernfs_put(kn);
	}
}

/**
 * kernfs_find_ns - find kernfs_node with the given name
 * @parent: kernfs_node to search under
 * @name: name to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_node with name @name under @parent.  Returns pointer to
 * the found kernfs_node on success, %NULL on failure.
 */
static struct kernfs_node *kernfs_find_ns(struct kernfs_node *parent,
					  const unsigned char *name,
					  const void *ns)
{
	struct rb_node *node = parent->dir.children.rb_node;
	bool has_ns = kernfs_ns_enabled(parent);
	unsigned int hash;

	lockdep_assert_held(&sysfs_mutex);

	if (has_ns != (bool)ns) {
		WARN(1, KERN_WARNING "sysfs: ns %s in '%s' for '%s'\n",
		     has_ns ? "required" : "invalid", parent->name, name);
		return NULL;
	}

	hash = sysfs_name_hash(name, ns);
	while (node) {
		struct kernfs_node *kn;
		int result;

		kn = rb_to_kn(node);
		result = sysfs_name_compare(hash, name, ns, kn);
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return kn;
	}
	return NULL;
}

/**
 * kernfs_find_and_get_ns - find and get kernfs_node with the given name
 * @parent: kernfs_node to search under
 * @name: name to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_node with name @name under @parent and get a reference
 * if found.  This function may sleep and returns pointer to the found
 * kernfs_node on success, %NULL on failure.
 */
struct kernfs_node *kernfs_find_and_get_ns(struct kernfs_node *parent,
					   const char *name, const void *ns)
{
	struct kernfs_node *kn;

	mutex_lock(&sysfs_mutex);
	kn = kernfs_find_ns(parent, name, ns);
	kernfs_get(kn);
	mutex_unlock(&sysfs_mutex);

	return kn;
}
EXPORT_SYMBOL_GPL(kernfs_find_and_get_ns);

/**
 * kernfs_create_root - create a new kernfs hierarchy
 * @priv: opaque data associated with the new directory
 *
 * Returns the root of the new hierarchy on success, ERR_PTR() value on
 * failure.
 */
struct kernfs_root *kernfs_create_root(void *priv)
{
	struct kernfs_root *root;
	struct kernfs_node *kn;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	ida_init(&root->ino_ida);

	kn = sysfs_new_dirent(root, "", S_IFDIR | S_IRUGO | S_IXUGO, KERNFS_DIR);
	if (!kn) {
		ida_destroy(&root->ino_ida);
		kfree(root);
		return ERR_PTR(-ENOMEM);
	}

	kn->flags &= ~KERNFS_REMOVED;
	kn->priv = priv;
	kn->dir.root = root;

	root->kn = kn;

	return root;
}

/**
 * kernfs_destroy_root - destroy a kernfs hierarchy
 * @root: root of the hierarchy to destroy
 *
 * Destroy the hierarchy anchored at @root by removing all existing
 * directories and destroying @root.
 */
void kernfs_destroy_root(struct kernfs_root *root)
{
	kernfs_remove(root->kn);	/* will also free @root */
}

/**
 * kernfs_create_dir_ns - create a directory
 * @parent: parent in which to create a new directory
 * @name: name of the new directory
 * @priv: opaque data associated with the new directory
 * @ns: optional namespace tag of the directory
 *
 * Returns the created node on success, ERR_PTR() value on failure.
 */
struct kernfs_node *kernfs_create_dir_ns(struct kernfs_node *parent,
					 const char *name, void *priv,
					 const void *ns)
{
	umode_t mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	struct kernfs_addrm_cxt acxt;
	struct kernfs_node *kn;
	int rc;

	/* allocate */
	kn = sysfs_new_dirent(kernfs_root(parent), name, mode, KERNFS_DIR);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	kn->dir.root = parent->dir.root;
	kn->ns = ns;
	kn->priv = priv;

	/* link in */
	sysfs_addrm_start(&acxt);
	rc = sysfs_add_one(&acxt, kn, parent);
	sysfs_addrm_finish(&acxt);

	if (!rc)
		return kn;

	kernfs_put(kn);
	return ERR_PTR(rc);
}

static struct dentry *sysfs_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct dentry *ret = NULL;
	struct kernfs_node *parent = dentry->d_parent->d_fsdata;
	struct kernfs_node *kn;
	struct inode *inode;
	const void *ns = NULL;

	mutex_lock(&sysfs_mutex);

	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dir->i_sb)->ns;

	kn = kernfs_find_ns(parent, dentry->d_name.name, ns);

	/* no such entry */
	if (!kn) {
		ret = ERR_PTR(-ENOENT);
		goto out_unlock;
	}
	kernfs_get(kn);
	dentry->d_fsdata = kn;

	/* attach dentry and inode */
	inode = sysfs_get_inode(dir->i_sb, kn);
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
	.removexattr	= sysfs_removexattr,
	.getxattr	= sysfs_getxattr,
	.listxattr	= sysfs_listxattr,
};

static struct kernfs_node *sysfs_leftmost_descendant(struct kernfs_node *pos)
{
	struct kernfs_node *last;

	while (true) {
		struct rb_node *rbn;

		last = pos;

		if (kernfs_type(pos) != KERNFS_DIR)
			break;

		rbn = rb_first(&pos->dir.children);
		if (!rbn)
			break;

		pos = rb_to_kn(rbn);
	}

	return last;
}

/**
 * sysfs_next_descendant_post - find the next descendant for post-order walk
 * @pos: the current position (%NULL to initiate traversal)
 * @root: kernfs_node whose descendants to walk
 *
 * Find the next descendant to visit for post-order traversal of @root's
 * descendants.  @root is included in the iteration and the last node to be
 * visited.
 */
static struct kernfs_node *sysfs_next_descendant_post(struct kernfs_node *pos,
						      struct kernfs_node *root)
{
	struct rb_node *rbn;

	lockdep_assert_held(&sysfs_mutex);

	/* if first iteration, visit leftmost descendant which may be root */
	if (!pos)
		return sysfs_leftmost_descendant(root);

	/* if we visited @root, we're done */
	if (pos == root)
		return NULL;

	/* if there's an unvisited sibling, visit its leftmost descendant */
	rbn = rb_next(&pos->rb);
	if (rbn)
		return sysfs_leftmost_descendant(rb_to_kn(rbn));

	/* no sibling left, visit parent */
	return pos->parent;
}

static void __kernfs_remove(struct kernfs_addrm_cxt *acxt,
			    struct kernfs_node *kn)
{
	struct kernfs_node *pos, *next;

	if (!kn)
		return;

	pr_debug("sysfs %s: removing\n", kn->name);

	next = NULL;
	do {
		pos = next;
		next = sysfs_next_descendant_post(pos, kn);
		if (pos)
			sysfs_remove_one(acxt, pos);
	} while (next);
}

/**
 * kernfs_remove - remove a kernfs_node recursively
 * @kn: the kernfs_node to remove
 *
 * Remove @kn along with all its subdirectories and files.
 */
void kernfs_remove(struct kernfs_node *kn)
{
	struct kernfs_addrm_cxt acxt;

	sysfs_addrm_start(&acxt);
	__kernfs_remove(&acxt, kn);
	sysfs_addrm_finish(&acxt);
}

/**
 * kernfs_remove_by_name_ns - find a kernfs_node by name and remove it
 * @parent: parent of the target
 * @name: name of the kernfs_node to remove
 * @ns: namespace tag of the kernfs_node to remove
 *
 * Look for the kernfs_node with @name and @ns under @parent and remove it.
 * Returns 0 on success, -ENOENT if such entry doesn't exist.
 */
int kernfs_remove_by_name_ns(struct kernfs_node *parent, const char *name,
			     const void *ns)
{
	struct kernfs_addrm_cxt acxt;
	struct kernfs_node *kn;

	if (!parent) {
		WARN(1, KERN_WARNING "sysfs: can not remove '%s', no directory\n",
			name);
		return -ENOENT;
	}

	sysfs_addrm_start(&acxt);

	kn = kernfs_find_ns(parent, name, ns);
	if (kn)
		__kernfs_remove(&acxt, kn);

	sysfs_addrm_finish(&acxt);

	if (kn)
		return 0;
	else
		return -ENOENT;
}

/**
 * kernfs_rename_ns - move and rename a kernfs_node
 * @kn: target node
 * @new_parent: new parent to put @sd under
 * @new_name: new name
 * @new_ns: new namespace tag
 */
int kernfs_rename_ns(struct kernfs_node *kn, struct kernfs_node *new_parent,
		     const char *new_name, const void *new_ns)
{
	int error;

	mutex_lock(&sysfs_mutex);

	error = 0;
	if ((kn->parent == new_parent) && (kn->ns == new_ns) &&
	    (strcmp(kn->name, new_name) == 0))
		goto out;	/* nothing to rename */

	error = -EEXIST;
	if (kernfs_find_ns(new_parent, new_name, new_ns))
		goto out;

	/* rename kernfs_node */
	if (strcmp(kn->name, new_name) != 0) {
		error = -ENOMEM;
		new_name = kstrdup(new_name, GFP_KERNEL);
		if (!new_name)
			goto out;

		kfree(kn->name);
		kn->name = new_name;
	}

	/*
	 * Move to the appropriate place in the appropriate directories rbtree.
	 */
	sysfs_unlink_sibling(kn);
	kernfs_get(new_parent);
	kernfs_put(kn->parent);
	kn->ns = new_ns;
	kn->hash = sysfs_name_hash(kn->name, kn->ns);
	kn->parent = new_parent;
	sysfs_link_sibling(kn);

	error = 0;
 out:
	mutex_unlock(&sysfs_mutex);
	return error;
}

/* Relationship between s_mode and the DT_xxx types */
static inline unsigned char dt_type(struct kernfs_node *kn)
{
	return (kn->mode >> 12) & 15;
}

static int sysfs_dir_release(struct inode *inode, struct file *filp)
{
	kernfs_put(filp->private_data);
	return 0;
}

static struct kernfs_node *sysfs_dir_pos(const void *ns,
	struct kernfs_node *parent, loff_t hash, struct kernfs_node *pos)
{
	if (pos) {
		int valid = !(pos->flags & KERNFS_REMOVED) &&
			pos->parent == parent && hash == pos->hash;
		kernfs_put(pos);
		if (!valid)
			pos = NULL;
	}
	if (!pos && (hash > 1) && (hash < INT_MAX)) {
		struct rb_node *node = parent->dir.children.rb_node;
		while (node) {
			pos = rb_to_kn(node);

			if (hash < pos->hash)
				node = node->rb_left;
			else if (hash > pos->hash)
				node = node->rb_right;
			else
				break;
		}
	}
	/* Skip over entries in the wrong namespace */
	while (pos && pos->ns != ns) {
		struct rb_node *node = rb_next(&pos->rb);
		if (!node)
			pos = NULL;
		else
			pos = rb_to_kn(node);
	}
	return pos;
}

static struct kernfs_node *sysfs_dir_next_pos(const void *ns,
	struct kernfs_node *parent, ino_t ino, struct kernfs_node *pos)
{
	pos = sysfs_dir_pos(ns, parent, ino, pos);
	if (pos)
		do {
			struct rb_node *node = rb_next(&pos->rb);
			if (!node)
				pos = NULL;
			else
				pos = rb_to_kn(node);
		} while (pos && pos->ns != ns);
	return pos;
}

static int sysfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct kernfs_node *parent = dentry->d_fsdata;
	struct kernfs_node *pos = file->private_data;
	const void *ns = NULL;

	if (!dir_emit_dots(file, ctx))
		return 0;
	mutex_lock(&sysfs_mutex);

	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dentry->d_sb)->ns;

	for (pos = sysfs_dir_pos(ns, parent, ctx->pos, pos);
	     pos;
	     pos = sysfs_dir_next_pos(ns, parent, ctx->pos, pos)) {
		const char *name = pos->name;
		unsigned int type = dt_type(pos);
		int len = strlen(name);
		ino_t ino = pos->ino;

		ctx->pos = pos->hash;
		file->private_data = pos;
		kernfs_get(pos);

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
