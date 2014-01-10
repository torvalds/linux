/*
 * fs/kernfs/dir.c - kernfs directory implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/hash.h>

#include "kernfs-internal.h"

DEFINE_MUTEX(kernfs_mutex);

#define rb_to_kn(X) rb_entry((X), struct kernfs_node, rb)

static bool kernfs_lockdep(struct kernfs_node *kn)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	return kn->flags & KERNFS_LOCKDEP;
#else
	return false;
#endif
}

/**
 *	kernfs_name_hash
 *	@name: Null terminated string to hash
 *	@ns:   Namespace tag to hash
 *
 *	Returns 31 bit hash of ns + name (so it fits in an off_t )
 */
static unsigned int kernfs_name_hash(const char *name, const void *ns)
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

static int kernfs_name_compare(unsigned int hash, const char *name,
			       const void *ns, const struct kernfs_node *kn)
{
	if (hash != kn->hash)
		return hash - kn->hash;
	if (ns != kn->ns)
		return ns - kn->ns;
	return strcmp(name, kn->name);
}

static int kernfs_sd_compare(const struct kernfs_node *left,
			     const struct kernfs_node *right)
{
	return kernfs_name_compare(left->hash, left->name, left->ns, right);
}

/**
 *	kernfs_link_sibling - link kernfs_node into sibling rbtree
 *	@kn: kernfs_node of interest
 *
 *	Link @kn into its sibling rbtree which starts from
 *	@kn->parent->dir.children.
 *
 *	Locking:
 *	mutex_lock(kernfs_mutex)
 *
 *	RETURNS:
 *	0 on susccess -EEXIST on failure.
 */
static int kernfs_link_sibling(struct kernfs_node *kn)
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
		result = kernfs_sd_compare(kn, pos);
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
 *	kernfs_unlink_sibling - unlink kernfs_node from sibling rbtree
 *	@kn: kernfs_node of interest
 *
 *	Unlink @kn from its sibling rbtree which starts from
 *	kn->parent->dir.children.
 *
 *	Locking:
 *	mutex_lock(kernfs_mutex)
 */
static bool kernfs_unlink_sibling(struct kernfs_node *kn)
{
	if (RB_EMPTY_NODE(&kn->rb))
		return false;

	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs--;

	rb_erase(&kn->rb, &kn->parent->dir.children);
	RB_CLEAR_NODE(&kn->rb);
	return true;
}

/**
 *	kernfs_get_active - get an active reference to kernfs_node
 *	@kn: kernfs_node to get an active reference to
 *
 *	Get an active reference of @kn.  This function is noop if @kn
 *	is NULL.
 *
 *	RETURNS:
 *	Pointer to @kn on success, NULL on failure.
 */
struct kernfs_node *kernfs_get_active(struct kernfs_node *kn)
{
	if (unlikely(!kn))
		return NULL;

	if (kernfs_lockdep(kn))
		rwsem_acquire_read(&kn->dep_map, 0, 1, _RET_IP_);

	/*
	 * Try to obtain an active ref.  If @kn is deactivated, we block
	 * till either it's reactivated or killed.
	 */
	do {
		if (atomic_inc_unless_negative(&kn->active))
			return kn;

		wait_event(kernfs_root(kn)->deactivate_waitq,
			   atomic_read(&kn->active) >= 0 ||
			   RB_EMPTY_NODE(&kn->rb));
	} while (!RB_EMPTY_NODE(&kn->rb));

	if (kernfs_lockdep(kn))
		rwsem_release(&kn->dep_map, 1, _RET_IP_);
	return NULL;
}

/**
 *	kernfs_put_active - put an active reference to kernfs_node
 *	@kn: kernfs_node to put an active reference to
 *
 *	Put an active reference to @kn.  This function is noop if @kn
 *	is NULL.
 */
void kernfs_put_active(struct kernfs_node *kn)
{
	struct kernfs_root *root = kernfs_root(kn);
	int v;

	if (unlikely(!kn))
		return;

	if (kernfs_lockdep(kn))
		rwsem_release(&kn->dep_map, 1, _RET_IP_);
	v = atomic_dec_return(&kn->active);
	if (likely(v != KN_DEACTIVATED_BIAS))
		return;

	wake_up_all(&root->deactivate_waitq);
}

/**
 * kernfs_drain - drain kernfs_node
 * @kn: kernfs_node to drain
 *
 * Drain existing usages of @kn.  Mutiple removers may invoke this function
 * concurrently on @kn and all will return after draining is complete.
 * Returns %true if drain is performed and kernfs_mutex was temporarily
 * released.  %false if @kn was already drained and no operation was
 * necessary.
 *
 * The caller is responsible for ensuring @kn stays pinned while this
 * function is in progress even if it gets removed by someone else.
 */
static bool kernfs_drain(struct kernfs_node *kn)
	__releases(&kernfs_mutex) __acquires(&kernfs_mutex)
{
	struct kernfs_root *root = kernfs_root(kn);

	lockdep_assert_held(&kernfs_mutex);
	WARN_ON_ONCE(atomic_read(&kn->active) >= 0);

	/*
	 * We want to go through the active ref lockdep annotation at least
	 * once for all node removals, but the lockdep annotation can't be
	 * nested inside kernfs_mutex and deactivation can't make forward
	 * progress if we keep dropping the mutex.  Use JUST_ACTIVATED to
	 * force the slow path once for each deactivation if lockdep is
	 * enabled.
	 */
	if ((!kernfs_lockdep(kn) || !(kn->flags & KERNFS_JUST_DEACTIVATED)) &&
	    atomic_read(&kn->active) == KN_DEACTIVATED_BIAS)
		return false;

	kn->flags &= ~KERNFS_JUST_DEACTIVATED;
	mutex_unlock(&kernfs_mutex);

	if (kernfs_lockdep(kn)) {
		rwsem_acquire(&kn->dep_map, 0, 0, _RET_IP_);
		if (atomic_read(&kn->active) != KN_DEACTIVATED_BIAS)
			lock_contended(&kn->dep_map, _RET_IP_);
	}

	wait_event(root->deactivate_waitq,
		   atomic_read(&kn->active) == KN_DEACTIVATED_BIAS);

	if (kernfs_lockdep(kn)) {
		lock_acquired(&kn->dep_map, _RET_IP_);
		rwsem_release(&kn->dep_map, 1, _RET_IP_);
	}

	mutex_lock(&kernfs_mutex);
	return true;
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
	/*
	 * Moving/renaming is always done while holding reference.
	 * kn->parent won't change beneath us.
	 */
	parent = kn->parent;

	WARN_ONCE(atomic_read(&kn->active) != KN_DEACTIVATED_BIAS,
		  "kernfs_put: %s/%s: released with incorrect active_ref %d\n",
		  parent ? parent->name : "", kn->name, atomic_read(&kn->active));

	if (kernfs_type(kn) == KERNFS_LINK)
		kernfs_put(kn->symlink.target_kn);
	if (!(kn->flags & KERNFS_STATIC_NAME))
		kfree(kn->name);
	if (kn->iattr) {
		if (kn->iattr->ia_secdata)
			security_release_secctx(kn->iattr->ia_secdata,
						kn->iattr->ia_secdata_len);
		simple_xattrs_free(&kn->iattr->xattrs);
	}
	kfree(kn->iattr);
	ida_simple_remove(&root->ino_ida, kn->ino);
	kmem_cache_free(kernfs_node_cache, kn);

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

static int kernfs_dop_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct kernfs_node *kn;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	/* Always perform fresh lookup for negatives */
	if (!dentry->d_inode)
		goto out_bad_unlocked;

	kn = dentry->d_fsdata;
	mutex_lock(&kernfs_mutex);

	/* Force fresh lookup if removed */
	if (kn->parent && RB_EMPTY_NODE(&kn->rb))
		goto out_bad;

	/* The kernfs node has been moved? */
	if (dentry->d_parent->d_fsdata != kn->parent)
		goto out_bad;

	/* The kernfs node has been renamed */
	if (strcmp(dentry->d_name.name, kn->name) != 0)
		goto out_bad;

	/* The kernfs node has been moved to a different namespace */
	if (kn->parent && kernfs_ns_enabled(kn->parent) &&
	    kernfs_info(dentry->d_sb)->ns != kn->ns)
		goto out_bad;

	mutex_unlock(&kernfs_mutex);
out_valid:
	return 1;
out_bad:
	mutex_unlock(&kernfs_mutex);
out_bad_unlocked:
	/*
	 * @dentry doesn't match the underlying kernfs node, drop the
	 * dentry and force lookup.  If we have submounts we must allow the
	 * vfs caches to lie about the state of the filesystem to prevent
	 * leaks and other nasty things, so use check_submounts_and_drop()
	 * instead of d_drop().
	 */
	if (check_submounts_and_drop(dentry) != 0)
		goto out_valid;

	return 0;
}

static void kernfs_dop_release(struct dentry *dentry)
{
	kernfs_put(dentry->d_fsdata);
}

const struct dentry_operations kernfs_dops = {
	.d_revalidate	= kernfs_dop_revalidate,
	.d_release	= kernfs_dop_release,
};

struct kernfs_node *kernfs_new_node(struct kernfs_root *root, const char *name,
				    umode_t mode, unsigned flags)
{
	char *dup_name = NULL;
	struct kernfs_node *kn;
	int ret;

	if (!(flags & KERNFS_STATIC_NAME)) {
		name = dup_name = kstrdup(name, GFP_KERNEL);
		if (!name)
			return NULL;
	}

	kn = kmem_cache_zalloc(kernfs_node_cache, GFP_KERNEL);
	if (!kn)
		goto err_out1;

	ret = ida_simple_get(&root->ino_ida, 1, 0, GFP_KERNEL);
	if (ret < 0)
		goto err_out2;
	kn->ino = ret;

	atomic_set(&kn->count, 1);
	atomic_set(&kn->active, KN_DEACTIVATED_BIAS);
	kn->deact_depth = 1;
	RB_CLEAR_NODE(&kn->rb);

	kn->name = name;
	kn->mode = mode;
	kn->flags = flags;

	return kn;

 err_out2:
	kmem_cache_free(kernfs_node_cache, kn);
 err_out1:
	kfree(dup_name);
	return NULL;
}

/**
 *	kernfs_add_one - add kernfs_node to parent without warning
 *	@kn: kernfs_node to be added
 *	@parent: the parent kernfs_node to add @kn to
 *
 *	Get @parent and set @kn->parent to it and increment nlink of the
 *	parent inode if @kn is a directory and link into the children list
 *	of the parent.
 *
 *	RETURNS:
 *	0 on success, -EEXIST if entry with the given name already
 *	exists.
 */
int kernfs_add_one(struct kernfs_node *kn, struct kernfs_node *parent)
{
	struct kernfs_iattrs *ps_iattr;
	bool has_ns;
	int ret;

	if (!kernfs_get_active(parent))
		return -ENOENT;

	mutex_lock(&kernfs_mutex);

	ret = -EINVAL;
	has_ns = kernfs_ns_enabled(parent);
	if (WARN(has_ns != (bool)kn->ns, KERN_WARNING "kernfs: ns %s in '%s' for '%s'\n",
		 has_ns ? "required" : "invalid", parent->name, kn->name))
		goto out_unlock;

	if (kernfs_type(parent) != KERNFS_DIR)
		goto out_unlock;

	kn->hash = kernfs_name_hash(kn->name, kn->ns);
	kn->parent = parent;
	kernfs_get(parent);

	ret = kernfs_link_sibling(kn);
	if (ret)
		goto out_unlock;

	/* Update timestamps on the parent */
	ps_iattr = parent->iattr;
	if (ps_iattr) {
		struct iattr *ps_iattrs = &ps_iattr->ia_iattr;
		ps_iattrs->ia_ctime = ps_iattrs->ia_mtime = CURRENT_TIME;
	}

	/* Mark the entry added into directory tree */
	atomic_sub(KN_DEACTIVATED_BIAS, &kn->active);
	kn->deact_depth--;
	ret = 0;
out_unlock:
	mutex_unlock(&kernfs_mutex);
	kernfs_put_active(parent);
	return ret;
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

	lockdep_assert_held(&kernfs_mutex);

	if (has_ns != (bool)ns) {
		WARN(1, KERN_WARNING "kernfs: ns %s in '%s' for '%s'\n",
		     has_ns ? "required" : "invalid", parent->name, name);
		return NULL;
	}

	hash = kernfs_name_hash(name, ns);
	while (node) {
		struct kernfs_node *kn;
		int result;

		kn = rb_to_kn(node);
		result = kernfs_name_compare(hash, name, ns, kn);
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

	mutex_lock(&kernfs_mutex);
	kn = kernfs_find_ns(parent, name, ns);
	kernfs_get(kn);
	mutex_unlock(&kernfs_mutex);

	return kn;
}
EXPORT_SYMBOL_GPL(kernfs_find_and_get_ns);

/**
 * kernfs_create_root - create a new kernfs hierarchy
 * @kdops: optional directory syscall operations for the hierarchy
 * @priv: opaque data associated with the new directory
 *
 * Returns the root of the new hierarchy on success, ERR_PTR() value on
 * failure.
 */
struct kernfs_root *kernfs_create_root(struct kernfs_dir_ops *kdops, void *priv)
{
	struct kernfs_root *root;
	struct kernfs_node *kn;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	ida_init(&root->ino_ida);

	kn = kernfs_new_node(root, "", S_IFDIR | S_IRUGO | S_IXUGO, KERNFS_DIR);
	if (!kn) {
		ida_destroy(&root->ino_ida);
		kfree(root);
		return ERR_PTR(-ENOMEM);
	}

	atomic_sub(KN_DEACTIVATED_BIAS, &kn->active);
	kn->deact_depth--;
	kn->priv = priv;
	kn->dir.root = root;

	root->dir_ops = kdops;
	root->kn = kn;
	init_waitqueue_head(&root->deactivate_waitq);

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
 * @mode: mode of the new directory
 * @priv: opaque data associated with the new directory
 * @ns: optional namespace tag of the directory
 *
 * Returns the created node on success, ERR_PTR() value on failure.
 */
struct kernfs_node *kernfs_create_dir_ns(struct kernfs_node *parent,
					 const char *name, umode_t mode,
					 void *priv, const void *ns)
{
	struct kernfs_node *kn;
	int rc;

	/* allocate */
	kn = kernfs_new_node(kernfs_root(parent), name, mode | S_IFDIR,
			     KERNFS_DIR);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	kn->dir.root = parent->dir.root;
	kn->ns = ns;
	kn->priv = priv;

	/* link in */
	rc = kernfs_add_one(kn, parent);
	if (!rc)
		return kn;

	kernfs_put(kn);
	return ERR_PTR(rc);
}

static struct dentry *kernfs_iop_lookup(struct inode *dir,
					struct dentry *dentry,
					unsigned int flags)
{
	struct dentry *ret;
	struct kernfs_node *parent = dentry->d_parent->d_fsdata;
	struct kernfs_node *kn;
	struct inode *inode;
	const void *ns = NULL;

	mutex_lock(&kernfs_mutex);

	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dir->i_sb)->ns;

	kn = kernfs_find_ns(parent, dentry->d_name.name, ns);

	/* no such entry */
	if (!kn) {
		ret = NULL;
		goto out_unlock;
	}
	kernfs_get(kn);
	dentry->d_fsdata = kn;

	/* attach dentry and inode */
	inode = kernfs_get_inode(dir->i_sb, kn);
	if (!inode) {
		ret = ERR_PTR(-ENOMEM);
		goto out_unlock;
	}

	/* instantiate and hash dentry */
	ret = d_materialise_unique(dentry, inode);
 out_unlock:
	mutex_unlock(&kernfs_mutex);
	return ret;
}

static int kernfs_iop_mkdir(struct inode *dir, struct dentry *dentry,
			    umode_t mode)
{
	struct kernfs_node *parent = dir->i_private;
	struct kernfs_dir_ops *kdops = kernfs_root(parent)->dir_ops;

	if (!kdops || !kdops->mkdir)
		return -EPERM;

	return kdops->mkdir(parent, dentry->d_name.name, mode);
}

static int kernfs_iop_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct kernfs_node *kn  = dentry->d_fsdata;
	struct kernfs_dir_ops *kdops = kernfs_root(kn)->dir_ops;

	if (!kdops || !kdops->rmdir)
		return -EPERM;

	return kdops->rmdir(kn);
}

static int kernfs_iop_rename(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry)
{
	struct kernfs_node *kn  = old_dentry->d_fsdata;
	struct kernfs_node *new_parent = new_dir->i_private;
	struct kernfs_dir_ops *kdops = kernfs_root(kn)->dir_ops;

	if (!kdops || !kdops->rename)
		return -EPERM;

	return kdops->rename(kn, new_parent, new_dentry->d_name.name);
}

const struct inode_operations kernfs_dir_iops = {
	.lookup		= kernfs_iop_lookup,
	.permission	= kernfs_iop_permission,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.setxattr	= kernfs_iop_setxattr,
	.removexattr	= kernfs_iop_removexattr,
	.getxattr	= kernfs_iop_getxattr,
	.listxattr	= kernfs_iop_listxattr,

	.mkdir		= kernfs_iop_mkdir,
	.rmdir		= kernfs_iop_rmdir,
	.rename		= kernfs_iop_rename,
};

static struct kernfs_node *kernfs_leftmost_descendant(struct kernfs_node *pos)
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
 * kernfs_next_descendant_post - find the next descendant for post-order walk
 * @pos: the current position (%NULL to initiate traversal)
 * @root: kernfs_node whose descendants to walk
 *
 * Find the next descendant to visit for post-order traversal of @root's
 * descendants.  @root is included in the iteration and the last node to be
 * visited.
 */
static struct kernfs_node *kernfs_next_descendant_post(struct kernfs_node *pos,
						       struct kernfs_node *root)
{
	struct rb_node *rbn;

	lockdep_assert_held(&kernfs_mutex);

	/* if first iteration, visit leftmost descendant which may be root */
	if (!pos)
		return kernfs_leftmost_descendant(root);

	/* if we visited @root, we're done */
	if (pos == root)
		return NULL;

	/* if there's an unvisited sibling, visit its leftmost descendant */
	rbn = rb_next(&pos->rb);
	if (rbn)
		return kernfs_leftmost_descendant(rb_to_kn(rbn));

	/* no sibling left, visit parent */
	return pos->parent;
}

static void __kernfs_deactivate(struct kernfs_node *kn)
{
	struct kernfs_node *pos;

	lockdep_assert_held(&kernfs_mutex);

	/* prevent any new usage under @kn by deactivating all nodes */
	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn))) {
		if (!pos->deact_depth++) {
			WARN_ON_ONCE(atomic_read(&pos->active) < 0);
			atomic_add(KN_DEACTIVATED_BIAS, &pos->active);
			pos->flags |= KERNFS_JUST_DEACTIVATED;
		}
	}

	/*
	 * Drain the subtree.  If kernfs_drain() blocked to drain, which is
	 * indicated by %true return, it temporarily released kernfs_mutex
	 * and the rbtree might have been modified inbetween breaking our
	 * future walk.  Restart the walk after each %true return.
	 */
	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn))) {
		bool drained;

		kernfs_get(pos);
		drained = kernfs_drain(pos);
		kernfs_put(pos);
		if (drained)
			pos = NULL;
	}
}

static void __kernfs_reactivate(struct kernfs_node *kn)
{
	struct kernfs_node *pos;

	lockdep_assert_held(&kernfs_mutex);

	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn))) {
		if (!--pos->deact_depth) {
			WARN_ON_ONCE(atomic_read(&pos->active) >= 0);
			atomic_sub(KN_DEACTIVATED_BIAS, &pos->active);
		}
		WARN_ON_ONCE(pos->deact_depth < 0);
	}

	/* some nodes reactivated, kick get_active waiters */
	wake_up_all(&kernfs_root(kn)->deactivate_waitq);
}

static void __kernfs_deactivate_self(struct kernfs_node *kn)
{
	/*
	 * Take out ourself out of the active ref dependency chain and
	 * deactivate.  If we're called without an active ref, lockdep will
	 * complain.
	 */
	kernfs_put_active(kn);
	__kernfs_deactivate(kn);
}

static void __kernfs_reactivate_self(struct kernfs_node *kn)
{
	__kernfs_reactivate(kn);
	/*
	 * Restore active ref dropped by deactivate_self() so that it's
	 * balanced on return.  put_active() will soon be called on @kn, so
	 * this can't break anything regardless of @kn's state.
	 */
	atomic_inc(&kn->active);
	if (kernfs_lockdep(kn))
		rwsem_acquire(&kn->dep_map, 0, 1, _RET_IP_);
}

/**
 * kernfs_deactivate - deactivate subtree of a node
 * @kn: kernfs_node to deactivate subtree of
 *
 * Deactivate the subtree of @kn.  On return, there's no active operation
 * going on under @kn and creation or renaming of a node under @kn is
 * blocked until @kn is reactivated or removed.  This function can be
 * called multiple times and nests properly.  Each invocation should be
 * paired with kernfs_reactivate().
 *
 * For a kernfs user which uses simple locking, the subsystem lock would
 * nest inside active reference.  This becomes problematic if the user
 * tries to remove nodes while holding the subystem lock as it would create
 * a reverse locking dependency from the subsystem lock to active ref.
 * This function can be used to break such reverse dependency.  The user
 * can call this function outside the subsystem lock and then proceed to
 * invoke kernfs_remove() while holding the subsystem lock without
 * introducing such reverse dependency.
 */
void kernfs_deactivate(struct kernfs_node *kn)
{
	mutex_lock(&kernfs_mutex);
	__kernfs_deactivate(kn);
	mutex_unlock(&kernfs_mutex);
}

/**
 * kernfs_reactivate - reactivate subtree of a node
 * @kn: kernfs_node to reactivate subtree of
 *
 * Undo kernfs_deactivate().
 */
void kernfs_reactivate(struct kernfs_node *kn)
{
	mutex_lock(&kernfs_mutex);
	__kernfs_reactivate(kn);
	mutex_unlock(&kernfs_mutex);
}

/**
 * kernfs_deactivate_self - deactivate subtree of a node from its own method
 * @kn: the self kernfs_node to deactivate subtree of
 *
 * The caller must be running off of a kernfs operation which is invoked
 * with an active reference - e.g. one of kernfs_ops.  Once this function
 * is called, @kn may be removed by someone else while the enclosing method
 * is in progress.  Other than that, this function is equivalent to
 * kernfs_deactivate() and should be paired with kernfs_reactivate_self().
 */
void kernfs_deactivate_self(struct kernfs_node *kn)
{
	mutex_lock(&kernfs_mutex);
	__kernfs_deactivate_self(kn);
	mutex_unlock(&kernfs_mutex);
}

/**
 * kernfs_reactivate_self - reactivate subtree of a node from its own method
 * @kn: the self kernfs_node to reactivate subtree of
 *
 * Undo kernfs_deactivate_self().
 */
void kernfs_reactivate_self(struct kernfs_node *kn)
{
	mutex_lock(&kernfs_mutex);
	__kernfs_reactivate_self(kn);
	mutex_unlock(&kernfs_mutex);
}

static void __kernfs_remove(struct kernfs_node *kn)
{
	struct kernfs_root *root = kernfs_root(kn);
	struct kernfs_node *pos;

	lockdep_assert_held(&kernfs_mutex);

	if (!kn)
		return;

	pr_debug("kernfs %s: removing\n", kn->name);

	__kernfs_deactivate(kn);

	/* unlink the subtree node-by-node */
	do {
		pos = kernfs_leftmost_descendant(kn);

		/*
		 * We're gonna release kernfs_mutex to unmap bin files,
		 * Make sure @pos doesn't go away inbetween.
		 */
		kernfs_get(pos);

		/*
		 * This must be come before unlinking; otherwise, when
		 * there are multiple removers, some may finish before
		 * unmapping is complete.
		 */
		if (pos->flags & KERNFS_HAS_MMAP) {
			mutex_unlock(&kernfs_mutex);
			kernfs_unmap_file(pos);
			mutex_lock(&kernfs_mutex);
		}

		/*
		 * kernfs_unlink_sibling() succeeds once per node.  Use it
		 * to decide who's responsible for cleanups.
		 */
		if (!pos->parent || kernfs_unlink_sibling(pos)) {
			struct kernfs_iattrs *ps_iattr =
				pos->parent ? pos->parent->iattr : NULL;

			/* update timestamps on the parent */
			if (ps_iattr) {
				ps_iattr->ia_iattr.ia_ctime = CURRENT_TIME;
				ps_iattr->ia_iattr.ia_mtime = CURRENT_TIME;
			}

			kernfs_put(pos);
		}

		kernfs_put(pos);
	} while (pos != kn);

	/* some nodes killed, kick get_active waiters */
	wake_up_all(&root->deactivate_waitq);
}

/**
 * kernfs_remove - remove a kernfs_node recursively
 * @kn: the kernfs_node to remove
 *
 * Remove @kn along with all its subdirectories and files.
 */
void kernfs_remove(struct kernfs_node *kn)
{
	mutex_lock(&kernfs_mutex);
	__kernfs_remove(kn);
	mutex_unlock(&kernfs_mutex);
}

/**
 * kernfs_remove_self - remove a kernfs_node from its own method
 * @kn: the self kernfs_node to remove
 *
 * The caller must be running off of a kernfs operation which is invoked
 * with an active reference - e.g. one of kernfs_ops.  This can be used to
 * implement a file operation which deletes itself.
 *
 * For example, the "delete" file for a sysfs device directory can be
 * implemented by invoking kernfs_remove_self() on the "delete" file
 * itself.  This function breaks the circular dependency of trying to
 * deactivate self while holding an active ref itself.  It isn't necessary
 * to modify the usual removal path to use kernfs_remove_self().  The
 * "delete" implementation can simply invoke kernfs_remove_self() on self
 * before proceeding with the usual removal path.  kernfs will ignore later
 * kernfs_remove() on self.
 *
 * kernfs_remove_self() can be called multiple times concurrently on the
 * same kernfs_node.  Only the first one actually performs removal and
 * returns %true.  All others will wait until the kernfs operation which
 * won self-removal finishes and return %false.  Note that the losers wait
 * for the completion of not only the winning kernfs_remove_self() but also
 * the whole kernfs_ops which won the arbitration.  This can be used to
 * guarantee, for example, all concurrent writes to a "delete" file to
 * finish only after the whole operation is complete.
 */
bool kernfs_remove_self(struct kernfs_node *kn)
{
	bool ret;

	mutex_lock(&kernfs_mutex);
	__kernfs_deactivate_self(kn);

	/*
	 * SUICIDAL is used to arbitrate among competing invocations.  Only
	 * the first one will actually perform removal.  When the removal
	 * is complete, SUICIDED is set and the active ref is restored
	 * while holding kernfs_mutex.  The ones which lost arbitration
	 * waits for SUICDED && drained which can happen only after the
	 * enclosing kernfs operation which executed the winning instance
	 * of kernfs_remove_self() finished.
	 */
	if (!(kn->flags & KERNFS_SUICIDAL)) {
		kn->flags |= KERNFS_SUICIDAL;
		__kernfs_remove(kn);
		kn->flags |= KERNFS_SUICIDED;
		ret = true;
	} else {
		wait_queue_head_t *waitq = &kernfs_root(kn)->deactivate_waitq;
		DEFINE_WAIT(wait);

		while (true) {
			prepare_to_wait(waitq, &wait, TASK_UNINTERRUPTIBLE);

			if ((kn->flags & KERNFS_SUICIDED) &&
			    atomic_read(&kn->active) == KN_DEACTIVATED_BIAS)
				break;

			mutex_unlock(&kernfs_mutex);
			schedule();
			mutex_lock(&kernfs_mutex);
		}
		finish_wait(waitq, &wait);
		WARN_ON_ONCE(!RB_EMPTY_NODE(&kn->rb));
		ret = false;
	}

	__kernfs_reactivate_self(kn);
	mutex_unlock(&kernfs_mutex);
	return ret;
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
	struct kernfs_node *kn;

	if (!parent) {
		WARN(1, KERN_WARNING "kernfs: can not remove '%s', no directory\n",
			name);
		return -ENOENT;
	}

	mutex_lock(&kernfs_mutex);

	kn = kernfs_find_ns(parent, name, ns);
	if (kn)
		__kernfs_remove(kn);

	mutex_unlock(&kernfs_mutex);

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

	error = -ENOENT;
	if (!kernfs_get_active(new_parent))
		goto out;
	if (!kernfs_get_active(kn))
		goto out_put_new_parent;

	mutex_lock(&kernfs_mutex);

	error = 0;
	if ((kn->parent == new_parent) && (kn->ns == new_ns) &&
	    (strcmp(kn->name, new_name) == 0))
		goto out_unlock;	/* nothing to rename */

	error = -EEXIST;
	if (kernfs_find_ns(new_parent, new_name, new_ns))
		goto out_unlock;

	/* rename kernfs_node */
	if (strcmp(kn->name, new_name) != 0) {
		error = -ENOMEM;
		new_name = kstrdup(new_name, GFP_KERNEL);
		if (!new_name)
			goto out_unlock;

		if (kn->flags & KERNFS_STATIC_NAME)
			kn->flags &= ~KERNFS_STATIC_NAME;
		else
			kfree(kn->name);

		kn->name = new_name;
	}

	/*
	 * Move to the appropriate place in the appropriate directories rbtree.
	 */
	kernfs_unlink_sibling(kn);
	kernfs_get(new_parent);
	kernfs_put(kn->parent);
	kn->ns = new_ns;
	kn->hash = kernfs_name_hash(kn->name, kn->ns);
	kn->parent = new_parent;
	kernfs_link_sibling(kn);

	error = 0;
out_unlock:
	mutex_unlock(&kernfs_mutex);
	kernfs_put_active(kn);
out_put_new_parent:
	kernfs_put_active(new_parent);
out:
	return error;
}

/* Relationship between s_mode and the DT_xxx types */
static inline unsigned char dt_type(struct kernfs_node *kn)
{
	return (kn->mode >> 12) & 15;
}

static int kernfs_dir_fop_release(struct inode *inode, struct file *filp)
{
	kernfs_put(filp->private_data);
	return 0;
}

static struct kernfs_node *kernfs_dir_pos(const void *ns,
	struct kernfs_node *parent, loff_t hash, struct kernfs_node *pos)
{
	if (pos) {
		int valid = pos->parent == parent && hash == pos->hash;
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

static struct kernfs_node *kernfs_dir_next_pos(const void *ns,
	struct kernfs_node *parent, ino_t ino, struct kernfs_node *pos)
{
	pos = kernfs_dir_pos(ns, parent, ino, pos);
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

static int kernfs_fop_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct kernfs_node *parent = dentry->d_fsdata;
	struct kernfs_node *pos = file->private_data;
	const void *ns = NULL;

	if (!dir_emit_dots(file, ctx))
		return 0;
	mutex_lock(&kernfs_mutex);

	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dentry->d_sb)->ns;

	for (pos = kernfs_dir_pos(ns, parent, ctx->pos, pos);
	     pos;
	     pos = kernfs_dir_next_pos(ns, parent, ctx->pos, pos)) {
		const char *name = pos->name;
		unsigned int type = dt_type(pos);
		int len = strlen(name);
		ino_t ino = pos->ino;

		ctx->pos = pos->hash;
		file->private_data = pos;
		kernfs_get(pos);

		mutex_unlock(&kernfs_mutex);
		if (!dir_emit(ctx, name, len, ino, type))
			return 0;
		mutex_lock(&kernfs_mutex);
	}
	mutex_unlock(&kernfs_mutex);
	file->private_data = NULL;
	ctx->pos = INT_MAX;
	return 0;
}

static loff_t kernfs_dir_fop_llseek(struct file *file, loff_t offset,
				    int whence)
{
	struct inode *inode = file_inode(file);
	loff_t ret;

	mutex_lock(&inode->i_mutex);
	ret = generic_file_llseek(file, offset, whence);
	mutex_unlock(&inode->i_mutex);

	return ret;
}

const struct file_operations kernfs_dir_fops = {
	.read		= generic_read_dir,
	.iterate	= kernfs_fop_readdir,
	.release	= kernfs_dir_fop_release,
	.llseek		= kernfs_dir_fop_llseek,
};
