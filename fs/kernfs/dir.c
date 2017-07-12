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
static DEFINE_SPINLOCK(kernfs_rename_lock);	/* kn->parent and ->name */
static char kernfs_pr_cont_buf[PATH_MAX];	/* protected by rename_lock */
static DEFINE_SPINLOCK(kernfs_idr_lock);	/* root->ino_idr */

#define rb_to_kn(X) rb_entry((X), struct kernfs_node, rb)

static bool kernfs_active(struct kernfs_node *kn)
{
	lockdep_assert_held(&kernfs_mutex);
	return atomic_read(&kn->active) >= 0;
}

static bool kernfs_lockdep(struct kernfs_node *kn)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	return kn->flags & KERNFS_LOCKDEP;
#else
	return false;
#endif
}

static int kernfs_name_locked(struct kernfs_node *kn, char *buf, size_t buflen)
{
	if (!kn)
		return strlcpy(buf, "(null)", buflen);

	return strlcpy(buf, kn->parent ? kn->name : "/", buflen);
}

/* kernfs_node_depth - compute depth from @from to @to */
static size_t kernfs_depth(struct kernfs_node *from, struct kernfs_node *to)
{
	size_t depth = 0;

	while (to->parent && to != from) {
		depth++;
		to = to->parent;
	}
	return depth;
}

static struct kernfs_node *kernfs_common_ancestor(struct kernfs_node *a,
						  struct kernfs_node *b)
{
	size_t da, db;
	struct kernfs_root *ra = kernfs_root(a), *rb = kernfs_root(b);

	if (ra != rb)
		return NULL;

	da = kernfs_depth(ra->kn, a);
	db = kernfs_depth(rb->kn, b);

	while (da > db) {
		a = a->parent;
		da--;
	}
	while (db > da) {
		b = b->parent;
		db--;
	}

	/* worst case b and a will be the same at root */
	while (b != a) {
		b = b->parent;
		a = a->parent;
	}

	return a;
}

/**
 * kernfs_path_from_node_locked - find a pseudo-absolute path to @kn_to,
 * where kn_from is treated as root of the path.
 * @kn_from: kernfs node which should be treated as root for the path
 * @kn_to: kernfs node to which path is needed
 * @buf: buffer to copy the path into
 * @buflen: size of @buf
 *
 * We need to handle couple of scenarios here:
 * [1] when @kn_from is an ancestor of @kn_to at some level
 * kn_from: /n1/n2/n3
 * kn_to:   /n1/n2/n3/n4/n5
 * result:  /n4/n5
 *
 * [2] when @kn_from is on a different hierarchy and we need to find common
 * ancestor between @kn_from and @kn_to.
 * kn_from: /n1/n2/n3/n4
 * kn_to:   /n1/n2/n5
 * result:  /../../n5
 * OR
 * kn_from: /n1/n2/n3/n4/n5   [depth=5]
 * kn_to:   /n1/n2/n3         [depth=3]
 * result:  /../..
 *
 * [3] when @kn_to is NULL result will be "(null)"
 *
 * Returns the length of the full path.  If the full length is equal to or
 * greater than @buflen, @buf contains the truncated path with the trailing
 * '\0'.  On error, -errno is returned.
 */
static int kernfs_path_from_node_locked(struct kernfs_node *kn_to,
					struct kernfs_node *kn_from,
					char *buf, size_t buflen)
{
	struct kernfs_node *kn, *common;
	const char parent_str[] = "/..";
	size_t depth_from, depth_to, len = 0;
	int i, j;

	if (!kn_to)
		return strlcpy(buf, "(null)", buflen);

	if (!kn_from)
		kn_from = kernfs_root(kn_to)->kn;

	if (kn_from == kn_to)
		return strlcpy(buf, "/", buflen);

	common = kernfs_common_ancestor(kn_from, kn_to);
	if (WARN_ON(!common))
		return -EINVAL;

	depth_to = kernfs_depth(common, kn_to);
	depth_from = kernfs_depth(common, kn_from);

	if (buf)
		buf[0] = '\0';

	for (i = 0; i < depth_from; i++)
		len += strlcpy(buf + len, parent_str,
			       len < buflen ? buflen - len : 0);

	/* Calculate how many bytes we need for the rest */
	for (i = depth_to - 1; i >= 0; i--) {
		for (kn = kn_to, j = 0; j < i; j++)
			kn = kn->parent;
		len += strlcpy(buf + len, "/",
			       len < buflen ? buflen - len : 0);
		len += strlcpy(buf + len, kn->name,
			       len < buflen ? buflen - len : 0);
	}

	return len;
}

/**
 * kernfs_name - obtain the name of a given node
 * @kn: kernfs_node of interest
 * @buf: buffer to copy @kn's name into
 * @buflen: size of @buf
 *
 * Copies the name of @kn into @buf of @buflen bytes.  The behavior is
 * similar to strlcpy().  It returns the length of @kn's name and if @buf
 * isn't long enough, it's filled upto @buflen-1 and nul terminated.
 *
 * Fills buffer with "(null)" if @kn is NULL.
 *
 * This function can be called from any context.
 */
int kernfs_name(struct kernfs_node *kn, char *buf, size_t buflen)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&kernfs_rename_lock, flags);
	ret = kernfs_name_locked(kn, buf, buflen);
	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
	return ret;
}

/**
 * kernfs_path_from_node - build path of node @to relative to @from.
 * @from: parent kernfs_node relative to which we need to build the path
 * @to: kernfs_node of interest
 * @buf: buffer to copy @to's path into
 * @buflen: size of @buf
 *
 * Builds @to's path relative to @from in @buf. @from and @to must
 * be on the same kernfs-root. If @from is not parent of @to, then a relative
 * path (which includes '..'s) as needed to reach from @from to @to is
 * returned.
 *
 * Returns the length of the full path.  If the full length is equal to or
 * greater than @buflen, @buf contains the truncated path with the trailing
 * '\0'.  On error, -errno is returned.
 */
int kernfs_path_from_node(struct kernfs_node *to, struct kernfs_node *from,
			  char *buf, size_t buflen)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&kernfs_rename_lock, flags);
	ret = kernfs_path_from_node_locked(to, from, buf, buflen);
	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(kernfs_path_from_node);

/**
 * pr_cont_kernfs_name - pr_cont name of a kernfs_node
 * @kn: kernfs_node of interest
 *
 * This function can be called from any context.
 */
void pr_cont_kernfs_name(struct kernfs_node *kn)
{
	unsigned long flags;

	spin_lock_irqsave(&kernfs_rename_lock, flags);

	kernfs_name_locked(kn, kernfs_pr_cont_buf, sizeof(kernfs_pr_cont_buf));
	pr_cont("%s", kernfs_pr_cont_buf);

	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
}

/**
 * pr_cont_kernfs_path - pr_cont path of a kernfs_node
 * @kn: kernfs_node of interest
 *
 * This function can be called from any context.
 */
void pr_cont_kernfs_path(struct kernfs_node *kn)
{
	unsigned long flags;
	int sz;

	spin_lock_irqsave(&kernfs_rename_lock, flags);

	sz = kernfs_path_from_node_locked(kn, NULL, kernfs_pr_cont_buf,
					  sizeof(kernfs_pr_cont_buf));
	if (sz < 0) {
		pr_cont("(error)");
		goto out;
	}

	if (sz >= sizeof(kernfs_pr_cont_buf)) {
		pr_cont("(name too long)");
		goto out;
	}

	pr_cont("%s", kernfs_pr_cont_buf);

out:
	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
}

/**
 * kernfs_get_parent - determine the parent node and pin it
 * @kn: kernfs_node of interest
 *
 * Determines @kn's parent, pins and returns it.  This function can be
 * called from any context.
 */
struct kernfs_node *kernfs_get_parent(struct kernfs_node *kn)
{
	struct kernfs_node *parent;
	unsigned long flags;

	spin_lock_irqsave(&kernfs_rename_lock, flags);
	parent = kn->parent;
	kernfs_get(parent);
	spin_unlock_irqrestore(&kernfs_rename_lock, flags);

	return parent;
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
	unsigned long hash = init_name_hash(ns);
	unsigned int len = strlen(name);
	while (len--)
		hash = partial_name_hash(*name++, hash);
	hash = end_name_hash(hash);
	hash &= 0x7fffffffU;
	/* Reserve hash numbers 0, 1 and INT_MAX for magic directory entries */
	if (hash < 2)
		hash += 2;
	if (hash >= INT_MAX)
		hash = INT_MAX - 1;
	return hash;
}

static int kernfs_name_compare(unsigned int hash, const char *name,
			       const void *ns, const struct kernfs_node *kn)
{
	if (hash < kn->hash)
		return -1;
	if (hash > kn->hash)
		return 1;
	if (ns < kn->ns)
		return -1;
	if (ns > kn->ns)
		return 1;
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

	/* successfully added, account subdir number */
	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs++;

	return 0;
}

/**
 *	kernfs_unlink_sibling - unlink kernfs_node from sibling rbtree
 *	@kn: kernfs_node of interest
 *
 *	Try to unlink @kn from its sibling rbtree which starts from
 *	kn->parent->dir.children.  Returns %true if @kn was actually
 *	removed, %false if @kn wasn't on the rbtree.
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

	if (!atomic_inc_unless_negative(&kn->active))
		return NULL;

	if (kernfs_lockdep(kn))
		rwsem_acquire_read(&kn->dep_map, 0, 1, _RET_IP_);
	return kn;
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
 * Drain existing usages and nuke all existing mmaps of @kn.  Mutiple
 * removers may invoke this function concurrently on @kn and all will
 * return after draining is complete.
 */
static void kernfs_drain(struct kernfs_node *kn)
	__releases(&kernfs_mutex) __acquires(&kernfs_mutex)
{
	struct kernfs_root *root = kernfs_root(kn);

	lockdep_assert_held(&kernfs_mutex);
	WARN_ON_ONCE(kernfs_active(kn));

	mutex_unlock(&kernfs_mutex);

	if (kernfs_lockdep(kn)) {
		rwsem_acquire(&kn->dep_map, 0, 0, _RET_IP_);
		if (atomic_read(&kn->active) != KN_DEACTIVATED_BIAS)
			lock_contended(&kn->dep_map, _RET_IP_);
	}

	/* but everyone should wait for draining */
	wait_event(root->deactivate_waitq,
		   atomic_read(&kn->active) == KN_DEACTIVATED_BIAS);

	if (kernfs_lockdep(kn)) {
		lock_acquired(&kn->dep_map, _RET_IP_);
		rwsem_release(&kn->dep_map, 1, _RET_IP_);
	}

	kernfs_drain_open_files(kn);

	mutex_lock(&kernfs_mutex);
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

	kfree_const(kn->name);

	if (kn->iattr) {
		if (kn->iattr->ia_secdata)
			security_release_secctx(kn->iattr->ia_secdata,
						kn->iattr->ia_secdata_len);
		simple_xattrs_free(&kn->iattr->xattrs);
	}
	kfree(kn->iattr);
	spin_lock(&kernfs_idr_lock);
	idr_remove(&root->ino_idr, kn->ino);
	spin_unlock(&kernfs_idr_lock);
	kmem_cache_free(kernfs_node_cache, kn);

	kn = parent;
	if (kn) {
		if (atomic_dec_and_test(&kn->count))
			goto repeat;
	} else {
		/* just released the root kn, free @root too */
		idr_destroy(&root->ino_idr);
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
	if (d_really_is_negative(dentry))
		goto out_bad_unlocked;

	kn = dentry->d_fsdata;
	mutex_lock(&kernfs_mutex);

	/* The kernfs node has been deactivated */
	if (!kernfs_active(kn))
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
	return 1;
out_bad:
	mutex_unlock(&kernfs_mutex);
out_bad_unlocked:
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

/**
 * kernfs_node_from_dentry - determine kernfs_node associated with a dentry
 * @dentry: the dentry in question
 *
 * Return the kernfs_node associated with @dentry.  If @dentry is not a
 * kernfs one, %NULL is returned.
 *
 * While the returned kernfs_node will stay accessible as long as @dentry
 * is accessible, the returned node can be in any state and the caller is
 * fully responsible for determining what's accessible.
 */
struct kernfs_node *kernfs_node_from_dentry(struct dentry *dentry)
{
	if (dentry->d_sb->s_op == &kernfs_sops)
		return dentry->d_fsdata;
	return NULL;
}

static struct kernfs_node *__kernfs_new_node(struct kernfs_root *root,
					     const char *name, umode_t mode,
					     unsigned flags)
{
	struct kernfs_node *kn;
	u32 gen;
	int cursor;
	int ret;

	name = kstrdup_const(name, GFP_KERNEL);
	if (!name)
		return NULL;

	kn = kmem_cache_zalloc(kernfs_node_cache, GFP_KERNEL);
	if (!kn)
		goto err_out1;

	idr_preload(GFP_KERNEL);
	spin_lock(&kernfs_idr_lock);
	cursor = idr_get_cursor(&root->ino_idr);
	ret = idr_alloc_cyclic(&root->ino_idr, kn, 1, 0, GFP_ATOMIC);
	if (ret >= 0 && ret < cursor)
		root->next_generation++;
	gen = root->next_generation;
	spin_unlock(&kernfs_idr_lock);
	idr_preload_end();
	if (ret < 0)
		goto err_out2;
	kn->ino = ret;
	kn->generation = gen;

	atomic_set(&kn->count, 1);
	atomic_set(&kn->active, KN_DEACTIVATED_BIAS);
	RB_CLEAR_NODE(&kn->rb);

	kn->name = name;
	kn->mode = mode;
	kn->flags = flags;

	return kn;

 err_out2:
	kmem_cache_free(kernfs_node_cache, kn);
 err_out1:
	kfree_const(name);
	return NULL;
}

struct kernfs_node *kernfs_new_node(struct kernfs_node *parent,
				    const char *name, umode_t mode,
				    unsigned flags)
{
	struct kernfs_node *kn;

	kn = __kernfs_new_node(kernfs_root(parent), name, mode, flags);
	if (kn) {
		kernfs_get(parent);
		kn->parent = parent;
	}
	return kn;
}

/**
 *	kernfs_add_one - add kernfs_node to parent without warning
 *	@kn: kernfs_node to be added
 *
 *	The caller must already have initialized @kn->parent.  This
 *	function increments nlink of the parent's inode if @kn is a
 *	directory and link into the children list of the parent.
 *
 *	RETURNS:
 *	0 on success, -EEXIST if entry with the given name already
 *	exists.
 */
int kernfs_add_one(struct kernfs_node *kn)
{
	struct kernfs_node *parent = kn->parent;
	struct kernfs_iattrs *ps_iattr;
	bool has_ns;
	int ret;

	mutex_lock(&kernfs_mutex);

	ret = -EINVAL;
	has_ns = kernfs_ns_enabled(parent);
	if (WARN(has_ns != (bool)kn->ns, KERN_WARNING "kernfs: ns %s in '%s' for '%s'\n",
		 has_ns ? "required" : "invalid", parent->name, kn->name))
		goto out_unlock;

	if (kernfs_type(parent) != KERNFS_DIR)
		goto out_unlock;

	ret = -ENOENT;
	if (parent->flags & KERNFS_EMPTY_DIR)
		goto out_unlock;

	if ((parent->flags & KERNFS_ACTIVATED) && !kernfs_active(parent))
		goto out_unlock;

	kn->hash = kernfs_name_hash(kn->name, kn->ns);

	ret = kernfs_link_sibling(kn);
	if (ret)
		goto out_unlock;

	/* Update timestamps on the parent */
	ps_iattr = parent->iattr;
	if (ps_iattr) {
		struct iattr *ps_iattrs = &ps_iattr->ia_iattr;
		ktime_get_real_ts(&ps_iattrs->ia_ctime);
		ps_iattrs->ia_mtime = ps_iattrs->ia_ctime;
	}

	mutex_unlock(&kernfs_mutex);

	/*
	 * Activate the new node unless CREATE_DEACTIVATED is requested.
	 * If not activated here, the kernfs user is responsible for
	 * activating the node with kernfs_activate().  A node which hasn't
	 * been activated is not visible to userland and its removal won't
	 * trigger deactivation.
	 */
	if (!(kernfs_root(kn)->flags & KERNFS_ROOT_CREATE_DEACTIVATED))
		kernfs_activate(kn);
	return 0;

out_unlock:
	mutex_unlock(&kernfs_mutex);
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

static struct kernfs_node *kernfs_walk_ns(struct kernfs_node *parent,
					  const unsigned char *path,
					  const void *ns)
{
	size_t len;
	char *p, *name;

	lockdep_assert_held(&kernfs_mutex);

	/* grab kernfs_rename_lock to piggy back on kernfs_pr_cont_buf */
	spin_lock_irq(&kernfs_rename_lock);

	len = strlcpy(kernfs_pr_cont_buf, path, sizeof(kernfs_pr_cont_buf));

	if (len >= sizeof(kernfs_pr_cont_buf)) {
		spin_unlock_irq(&kernfs_rename_lock);
		return NULL;
	}

	p = kernfs_pr_cont_buf;

	while ((name = strsep(&p, "/")) && parent) {
		if (*name == '\0')
			continue;
		parent = kernfs_find_ns(parent, name, ns);
	}

	spin_unlock_irq(&kernfs_rename_lock);

	return parent;
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
 * kernfs_walk_and_get_ns - find and get kernfs_node with the given path
 * @parent: kernfs_node to search under
 * @path: path to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_node with path @path under @parent and get a reference
 * if found.  This function may sleep and returns pointer to the found
 * kernfs_node on success, %NULL on failure.
 */
struct kernfs_node *kernfs_walk_and_get_ns(struct kernfs_node *parent,
					   const char *path, const void *ns)
{
	struct kernfs_node *kn;

	mutex_lock(&kernfs_mutex);
	kn = kernfs_walk_ns(parent, path, ns);
	kernfs_get(kn);
	mutex_unlock(&kernfs_mutex);

	return kn;
}

/**
 * kernfs_create_root - create a new kernfs hierarchy
 * @scops: optional syscall operations for the hierarchy
 * @flags: KERNFS_ROOT_* flags
 * @priv: opaque data associated with the new directory
 *
 * Returns the root of the new hierarchy on success, ERR_PTR() value on
 * failure.
 */
struct kernfs_root *kernfs_create_root(struct kernfs_syscall_ops *scops,
				       unsigned int flags, void *priv)
{
	struct kernfs_root *root;
	struct kernfs_node *kn;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	idr_init(&root->ino_idr);
	INIT_LIST_HEAD(&root->supers);
	root->next_generation = 1;

	kn = __kernfs_new_node(root, "", S_IFDIR | S_IRUGO | S_IXUGO,
			       KERNFS_DIR);
	if (!kn) {
		idr_destroy(&root->ino_idr);
		kfree(root);
		return ERR_PTR(-ENOMEM);
	}

	kn->priv = priv;
	kn->dir.root = root;

	root->syscall_ops = scops;
	root->flags = flags;
	root->kn = kn;
	init_waitqueue_head(&root->deactivate_waitq);

	if (!(root->flags & KERNFS_ROOT_CREATE_DEACTIVATED))
		kernfs_activate(kn);

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
	kn = kernfs_new_node(parent, name, mode | S_IFDIR, KERNFS_DIR);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	kn->dir.root = parent->dir.root;
	kn->ns = ns;
	kn->priv = priv;

	/* link in */
	rc = kernfs_add_one(kn);
	if (!rc)
		return kn;

	kernfs_put(kn);
	return ERR_PTR(rc);
}

/**
 * kernfs_create_empty_dir - create an always empty directory
 * @parent: parent in which to create a new directory
 * @name: name of the new directory
 *
 * Returns the created node on success, ERR_PTR() value on failure.
 */
struct kernfs_node *kernfs_create_empty_dir(struct kernfs_node *parent,
					    const char *name)
{
	struct kernfs_node *kn;
	int rc;

	/* allocate */
	kn = kernfs_new_node(parent, name, S_IRUGO|S_IXUGO|S_IFDIR, KERNFS_DIR);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	kn->flags |= KERNFS_EMPTY_DIR;
	kn->dir.root = parent->dir.root;
	kn->ns = NULL;
	kn->priv = NULL;

	/* link in */
	rc = kernfs_add_one(kn);
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
	if (!kn || !kernfs_active(kn)) {
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
	ret = d_splice_alias(inode, dentry);
 out_unlock:
	mutex_unlock(&kernfs_mutex);
	return ret;
}

static int kernfs_iop_mkdir(struct inode *dir, struct dentry *dentry,
			    umode_t mode)
{
	struct kernfs_node *parent = dir->i_private;
	struct kernfs_syscall_ops *scops = kernfs_root(parent)->syscall_ops;
	int ret;

	if (!scops || !scops->mkdir)
		return -EPERM;

	if (!kernfs_get_active(parent))
		return -ENODEV;

	ret = scops->mkdir(parent, dentry->d_name.name, mode);

	kernfs_put_active(parent);
	return ret;
}

static int kernfs_iop_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct kernfs_node *kn  = dentry->d_fsdata;
	struct kernfs_syscall_ops *scops = kernfs_root(kn)->syscall_ops;
	int ret;

	if (!scops || !scops->rmdir)
		return -EPERM;

	if (!kernfs_get_active(kn))
		return -ENODEV;

	ret = scops->rmdir(kn);

	kernfs_put_active(kn);
	return ret;
}

static int kernfs_iop_rename(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	struct kernfs_node *kn  = old_dentry->d_fsdata;
	struct kernfs_node *new_parent = new_dir->i_private;
	struct kernfs_syscall_ops *scops = kernfs_root(kn)->syscall_ops;
	int ret;

	if (flags)
		return -EINVAL;

	if (!scops || !scops->rename)
		return -EPERM;

	if (!kernfs_get_active(kn))
		return -ENODEV;

	if (!kernfs_get_active(new_parent)) {
		kernfs_put_active(kn);
		return -ENODEV;
	}

	ret = scops->rename(kn, new_parent, new_dentry->d_name.name);

	kernfs_put_active(new_parent);
	kernfs_put_active(kn);
	return ret;
}

const struct inode_operations kernfs_dir_iops = {
	.lookup		= kernfs_iop_lookup,
	.permission	= kernfs_iop_permission,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
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

/**
 * kernfs_activate - activate a node which started deactivated
 * @kn: kernfs_node whose subtree is to be activated
 *
 * If the root has KERNFS_ROOT_CREATE_DEACTIVATED set, a newly created node
 * needs to be explicitly activated.  A node which hasn't been activated
 * isn't visible to userland and deactivation is skipped during its
 * removal.  This is useful to construct atomic init sequences where
 * creation of multiple nodes should either succeed or fail atomically.
 *
 * The caller is responsible for ensuring that this function is not called
 * after kernfs_remove*() is invoked on @kn.
 */
void kernfs_activate(struct kernfs_node *kn)
{
	struct kernfs_node *pos;

	mutex_lock(&kernfs_mutex);

	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn))) {
		if (!pos || (pos->flags & KERNFS_ACTIVATED))
			continue;

		WARN_ON_ONCE(pos->parent && RB_EMPTY_NODE(&pos->rb));
		WARN_ON_ONCE(atomic_read(&pos->active) != KN_DEACTIVATED_BIAS);

		atomic_sub(KN_DEACTIVATED_BIAS, &pos->active);
		pos->flags |= KERNFS_ACTIVATED;
	}

	mutex_unlock(&kernfs_mutex);
}

static void __kernfs_remove(struct kernfs_node *kn)
{
	struct kernfs_node *pos;

	lockdep_assert_held(&kernfs_mutex);

	/*
	 * Short-circuit if non-root @kn has already finished removal.
	 * This is for kernfs_remove_self() which plays with active ref
	 * after removal.
	 */
	if (!kn || (kn->parent && RB_EMPTY_NODE(&kn->rb)))
		return;

	pr_debug("kernfs %s: removing\n", kn->name);

	/* prevent any new usage under @kn by deactivating all nodes */
	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn)))
		if (kernfs_active(pos))
			atomic_add(KN_DEACTIVATED_BIAS, &pos->active);

	/* deactivate and unlink the subtree node-by-node */
	do {
		pos = kernfs_leftmost_descendant(kn);

		/*
		 * kernfs_drain() drops kernfs_mutex temporarily and @pos's
		 * base ref could have been put by someone else by the time
		 * the function returns.  Make sure it doesn't go away
		 * underneath us.
		 */
		kernfs_get(pos);

		/*
		 * Drain iff @kn was activated.  This avoids draining and
		 * its lockdep annotations for nodes which have never been
		 * activated and allows embedding kernfs_remove() in create
		 * error paths without worrying about draining.
		 */
		if (kn->flags & KERNFS_ACTIVATED)
			kernfs_drain(pos);
		else
			WARN_ON_ONCE(atomic_read(&kn->active) != KN_DEACTIVATED_BIAS);

		/*
		 * kernfs_unlink_sibling() succeeds once per node.  Use it
		 * to decide who's responsible for cleanups.
		 */
		if (!pos->parent || kernfs_unlink_sibling(pos)) {
			struct kernfs_iattrs *ps_iattr =
				pos->parent ? pos->parent->iattr : NULL;

			/* update timestamps on the parent */
			if (ps_iattr) {
				ktime_get_real_ts(&ps_iattr->ia_iattr.ia_ctime);
				ps_iattr->ia_iattr.ia_mtime =
					ps_iattr->ia_iattr.ia_ctime;
			}

			kernfs_put(pos);
		}

		kernfs_put(pos);
	} while (pos != kn);
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
 * kernfs_break_active_protection - break out of active protection
 * @kn: the self kernfs_node
 *
 * The caller must be running off of a kernfs operation which is invoked
 * with an active reference - e.g. one of kernfs_ops.  Each invocation of
 * this function must also be matched with an invocation of
 * kernfs_unbreak_active_protection().
 *
 * This function releases the active reference of @kn the caller is
 * holding.  Once this function is called, @kn may be removed at any point
 * and the caller is solely responsible for ensuring that the objects it
 * dereferences are accessible.
 */
void kernfs_break_active_protection(struct kernfs_node *kn)
{
	/*
	 * Take out ourself out of the active ref dependency chain.  If
	 * we're called without an active ref, lockdep will complain.
	 */
	kernfs_put_active(kn);
}

/**
 * kernfs_unbreak_active_protection - undo kernfs_break_active_protection()
 * @kn: the self kernfs_node
 *
 * If kernfs_break_active_protection() was called, this function must be
 * invoked before finishing the kernfs operation.  Note that while this
 * function restores the active reference, it doesn't and can't actually
 * restore the active protection - @kn may already or be in the process of
 * being removed.  Once kernfs_break_active_protection() is invoked, that
 * protection is irreversibly gone for the kernfs operation instance.
 *
 * While this function may be called at any point after
 * kernfs_break_active_protection() is invoked, its most useful location
 * would be right before the enclosing kernfs operation returns.
 */
void kernfs_unbreak_active_protection(struct kernfs_node *kn)
{
	/*
	 * @kn->active could be in any state; however, the increment we do
	 * here will be undone as soon as the enclosing kernfs operation
	 * finishes and this temporary bump can't break anything.  If @kn
	 * is alive, nothing changes.  If @kn is being deactivated, the
	 * soon-to-follow put will either finish deactivation or restore
	 * deactivated state.  If @kn is already removed, the temporary
	 * bump is guaranteed to be gone before @kn is released.
	 */
	atomic_inc(&kn->active);
	if (kernfs_lockdep(kn))
		rwsem_acquire(&kn->dep_map, 0, 1, _RET_IP_);
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
	kernfs_break_active_protection(kn);

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

	/*
	 * This must be done while holding kernfs_mutex; otherwise, waiting
	 * for SUICIDED && deactivated could finish prematurely.
	 */
	kernfs_unbreak_active_protection(kn);

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
	struct kernfs_node *old_parent;
	const char *old_name = NULL;
	int error;

	/* can't move or rename root */
	if (!kn->parent)
		return -EINVAL;

	mutex_lock(&kernfs_mutex);

	error = -ENOENT;
	if (!kernfs_active(kn) || !kernfs_active(new_parent) ||
	    (new_parent->flags & KERNFS_EMPTY_DIR))
		goto out;

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
		new_name = kstrdup_const(new_name, GFP_KERNEL);
		if (!new_name)
			goto out;
	} else {
		new_name = NULL;
	}

	/*
	 * Move to the appropriate place in the appropriate directories rbtree.
	 */
	kernfs_unlink_sibling(kn);
	kernfs_get(new_parent);

	/* rename_lock protects ->parent and ->name accessors */
	spin_lock_irq(&kernfs_rename_lock);

	old_parent = kn->parent;
	kn->parent = new_parent;

	kn->ns = new_ns;
	if (new_name) {
		old_name = kn->name;
		kn->name = new_name;
	}

	spin_unlock_irq(&kernfs_rename_lock);

	kn->hash = kernfs_name_hash(kn->name, kn->ns);
	kernfs_link_sibling(kn);

	kernfs_put(old_parent);
	kfree_const(old_name);

	error = 0;
 out:
	mutex_unlock(&kernfs_mutex);
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
		int valid = kernfs_active(pos) &&
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
	/* Skip over entries which are dying/dead or in the wrong namespace */
	while (pos && (!kernfs_active(pos) || pos->ns != ns)) {
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
	if (pos) {
		do {
			struct rb_node *node = rb_next(&pos->rb);
			if (!node)
				pos = NULL;
			else
				pos = rb_to_kn(node);
		} while (pos && (!kernfs_active(pos) || pos->ns != ns));
	}
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

const struct file_operations kernfs_dir_fops = {
	.read		= generic_read_dir,
	.iterate_shared	= kernfs_fop_readdir,
	.release	= kernfs_dir_fop_release,
	.llseek		= generic_file_llseek,
};
