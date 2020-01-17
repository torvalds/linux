// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/kernfs/dir.c - kernfs directory implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
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
static DEFINE_SPINLOCK(kernfs_idr_lock);	/* root->iyes_idr */

#define rb_to_kn(X) rb_entry((X), struct kernfs_yesde, rb)

static bool kernfs_active(struct kernfs_yesde *kn)
{
	lockdep_assert_held(&kernfs_mutex);
	return atomic_read(&kn->active) >= 0;
}

static bool kernfs_lockdep(struct kernfs_yesde *kn)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	return kn->flags & KERNFS_LOCKDEP;
#else
	return false;
#endif
}

static int kernfs_name_locked(struct kernfs_yesde *kn, char *buf, size_t buflen)
{
	if (!kn)
		return strlcpy(buf, "(null)", buflen);

	return strlcpy(buf, kn->parent ? kn->name : "/", buflen);
}

/* kernfs_yesde_depth - compute depth from @from to @to */
static size_t kernfs_depth(struct kernfs_yesde *from, struct kernfs_yesde *to)
{
	size_t depth = 0;

	while (to->parent && to != from) {
		depth++;
		to = to->parent;
	}
	return depth;
}

static struct kernfs_yesde *kernfs_common_ancestor(struct kernfs_yesde *a,
						  struct kernfs_yesde *b)
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
 * kernfs_path_from_yesde_locked - find a pseudo-absolute path to @kn_to,
 * where kn_from is treated as root of the path.
 * @kn_from: kernfs yesde which should be treated as root for the path
 * @kn_to: kernfs yesde to which path is needed
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
 * '\0'.  On error, -erryes is returned.
 */
static int kernfs_path_from_yesde_locked(struct kernfs_yesde *kn_to,
					struct kernfs_yesde *kn_from,
					char *buf, size_t buflen)
{
	struct kernfs_yesde *kn, *common;
	const char parent_str[] = "/..";
	size_t depth_from, depth_to, len = 0;
	int i, j;

	if (!kn_to)
		return strlcpy(buf, "(null)", buflen);

	if (!kn_from)
		kn_from = kernfs_root(kn_to)->kn;

	if (kn_from == kn_to)
		return strlcpy(buf, "/", buflen);

	if (!buf)
		return -EINVAL;

	common = kernfs_common_ancestor(kn_from, kn_to);
	if (WARN_ON(!common))
		return -EINVAL;

	depth_to = kernfs_depth(common, kn_to);
	depth_from = kernfs_depth(common, kn_from);

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
 * kernfs_name - obtain the name of a given yesde
 * @kn: kernfs_yesde of interest
 * @buf: buffer to copy @kn's name into
 * @buflen: size of @buf
 *
 * Copies the name of @kn into @buf of @buflen bytes.  The behavior is
 * similar to strlcpy().  It returns the length of @kn's name and if @buf
 * isn't long eyesugh, it's filled upto @buflen-1 and nul terminated.
 *
 * Fills buffer with "(null)" if @kn is NULL.
 *
 * This function can be called from any context.
 */
int kernfs_name(struct kernfs_yesde *kn, char *buf, size_t buflen)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&kernfs_rename_lock, flags);
	ret = kernfs_name_locked(kn, buf, buflen);
	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
	return ret;
}

/**
 * kernfs_path_from_yesde - build path of yesde @to relative to @from.
 * @from: parent kernfs_yesde relative to which we need to build the path
 * @to: kernfs_yesde of interest
 * @buf: buffer to copy @to's path into
 * @buflen: size of @buf
 *
 * Builds @to's path relative to @from in @buf. @from and @to must
 * be on the same kernfs-root. If @from is yest parent of @to, then a relative
 * path (which includes '..'s) as needed to reach from @from to @to is
 * returned.
 *
 * Returns the length of the full path.  If the full length is equal to or
 * greater than @buflen, @buf contains the truncated path with the trailing
 * '\0'.  On error, -erryes is returned.
 */
int kernfs_path_from_yesde(struct kernfs_yesde *to, struct kernfs_yesde *from,
			  char *buf, size_t buflen)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&kernfs_rename_lock, flags);
	ret = kernfs_path_from_yesde_locked(to, from, buf, buflen);
	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(kernfs_path_from_yesde);

/**
 * pr_cont_kernfs_name - pr_cont name of a kernfs_yesde
 * @kn: kernfs_yesde of interest
 *
 * This function can be called from any context.
 */
void pr_cont_kernfs_name(struct kernfs_yesde *kn)
{
	unsigned long flags;

	spin_lock_irqsave(&kernfs_rename_lock, flags);

	kernfs_name_locked(kn, kernfs_pr_cont_buf, sizeof(kernfs_pr_cont_buf));
	pr_cont("%s", kernfs_pr_cont_buf);

	spin_unlock_irqrestore(&kernfs_rename_lock, flags);
}

/**
 * pr_cont_kernfs_path - pr_cont path of a kernfs_yesde
 * @kn: kernfs_yesde of interest
 *
 * This function can be called from any context.
 */
void pr_cont_kernfs_path(struct kernfs_yesde *kn)
{
	unsigned long flags;
	int sz;

	spin_lock_irqsave(&kernfs_rename_lock, flags);

	sz = kernfs_path_from_yesde_locked(kn, NULL, kernfs_pr_cont_buf,
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
 * kernfs_get_parent - determine the parent yesde and pin it
 * @kn: kernfs_yesde of interest
 *
 * Determines @kn's parent, pins and returns it.  This function can be
 * called from any context.
 */
struct kernfs_yesde *kernfs_get_parent(struct kernfs_yesde *kn)
{
	struct kernfs_yesde *parent;
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
			       const void *ns, const struct kernfs_yesde *kn)
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

static int kernfs_sd_compare(const struct kernfs_yesde *left,
			     const struct kernfs_yesde *right)
{
	return kernfs_name_compare(left->hash, left->name, left->ns, right);
}

/**
 *	kernfs_link_sibling - link kernfs_yesde into sibling rbtree
 *	@kn: kernfs_yesde of interest
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
static int kernfs_link_sibling(struct kernfs_yesde *kn)
{
	struct rb_yesde **yesde = &kn->parent->dir.children.rb_yesde;
	struct rb_yesde *parent = NULL;

	while (*yesde) {
		struct kernfs_yesde *pos;
		int result;

		pos = rb_to_kn(*yesde);
		parent = *yesde;
		result = kernfs_sd_compare(kn, pos);
		if (result < 0)
			yesde = &pos->rb.rb_left;
		else if (result > 0)
			yesde = &pos->rb.rb_right;
		else
			return -EEXIST;
	}

	/* add new yesde and rebalance the tree */
	rb_link_yesde(&kn->rb, parent, yesde);
	rb_insert_color(&kn->rb, &kn->parent->dir.children);

	/* successfully added, account subdir number */
	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs++;

	return 0;
}

/**
 *	kernfs_unlink_sibling - unlink kernfs_yesde from sibling rbtree
 *	@kn: kernfs_yesde of interest
 *
 *	Try to unlink @kn from its sibling rbtree which starts from
 *	kn->parent->dir.children.  Returns %true if @kn was actually
 *	removed, %false if @kn wasn't on the rbtree.
 *
 *	Locking:
 *	mutex_lock(kernfs_mutex)
 */
static bool kernfs_unlink_sibling(struct kernfs_yesde *kn)
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
 *	kernfs_get_active - get an active reference to kernfs_yesde
 *	@kn: kernfs_yesde to get an active reference to
 *
 *	Get an active reference of @kn.  This function is yesop if @kn
 *	is NULL.
 *
 *	RETURNS:
 *	Pointer to @kn on success, NULL on failure.
 */
struct kernfs_yesde *kernfs_get_active(struct kernfs_yesde *kn)
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
 *	kernfs_put_active - put an active reference to kernfs_yesde
 *	@kn: kernfs_yesde to put an active reference to
 *
 *	Put an active reference to @kn.  This function is yesop if @kn
 *	is NULL.
 */
void kernfs_put_active(struct kernfs_yesde *kn)
{
	int v;

	if (unlikely(!kn))
		return;

	if (kernfs_lockdep(kn))
		rwsem_release(&kn->dep_map, _RET_IP_);
	v = atomic_dec_return(&kn->active);
	if (likely(v != KN_DEACTIVATED_BIAS))
		return;

	wake_up_all(&kernfs_root(kn)->deactivate_waitq);
}

/**
 * kernfs_drain - drain kernfs_yesde
 * @kn: kernfs_yesde to drain
 *
 * Drain existing usages and nuke all existing mmaps of @kn.  Mutiple
 * removers may invoke this function concurrently on @kn and all will
 * return after draining is complete.
 */
static void kernfs_drain(struct kernfs_yesde *kn)
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
		rwsem_release(&kn->dep_map, _RET_IP_);
	}

	kernfs_drain_open_files(kn);

	mutex_lock(&kernfs_mutex);
}

/**
 * kernfs_get - get a reference count on a kernfs_yesde
 * @kn: the target kernfs_yesde
 */
void kernfs_get(struct kernfs_yesde *kn)
{
	if (kn) {
		WARN_ON(!atomic_read(&kn->count));
		atomic_inc(&kn->count);
	}
}
EXPORT_SYMBOL_GPL(kernfs_get);

/**
 * kernfs_put - put a reference count on a kernfs_yesde
 * @kn: the target kernfs_yesde
 *
 * Put a reference count of @kn and destroy it if it reached zero.
 */
void kernfs_put(struct kernfs_yesde *kn)
{
	struct kernfs_yesde *parent;
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
		simple_xattrs_free(&kn->iattr->xattrs);
		kmem_cache_free(kernfs_iattrs_cache, kn->iattr);
	}
	spin_lock(&kernfs_idr_lock);
	idr_remove(&root->iyes_idr, (u32)kernfs_iyes(kn));
	spin_unlock(&kernfs_idr_lock);
	kmem_cache_free(kernfs_yesde_cache, kn);

	kn = parent;
	if (kn) {
		if (atomic_dec_and_test(&kn->count))
			goto repeat;
	} else {
		/* just released the root kn, free @root too */
		idr_destroy(&root->iyes_idr);
		kfree(root);
	}
}
EXPORT_SYMBOL_GPL(kernfs_put);

static int kernfs_dop_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct kernfs_yesde *kn;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	/* Always perform fresh lookup for negatives */
	if (d_really_is_negative(dentry))
		goto out_bad_unlocked;

	kn = kernfs_dentry_yesde(dentry);
	mutex_lock(&kernfs_mutex);

	/* The kernfs yesde has been deactivated */
	if (!kernfs_active(kn))
		goto out_bad;

	/* The kernfs yesde has been moved? */
	if (kernfs_dentry_yesde(dentry->d_parent) != kn->parent)
		goto out_bad;

	/* The kernfs yesde has been renamed */
	if (strcmp(dentry->d_name.name, kn->name) != 0)
		goto out_bad;

	/* The kernfs yesde has been moved to a different namespace */
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

const struct dentry_operations kernfs_dops = {
	.d_revalidate	= kernfs_dop_revalidate,
};

/**
 * kernfs_yesde_from_dentry - determine kernfs_yesde associated with a dentry
 * @dentry: the dentry in question
 *
 * Return the kernfs_yesde associated with @dentry.  If @dentry is yest a
 * kernfs one, %NULL is returned.
 *
 * While the returned kernfs_yesde will stay accessible as long as @dentry
 * is accessible, the returned yesde can be in any state and the caller is
 * fully responsible for determining what's accessible.
 */
struct kernfs_yesde *kernfs_yesde_from_dentry(struct dentry *dentry)
{
	if (dentry->d_sb->s_op == &kernfs_sops &&
	    !d_really_is_negative(dentry))
		return kernfs_dentry_yesde(dentry);
	return NULL;
}

static struct kernfs_yesde *__kernfs_new_yesde(struct kernfs_root *root,
					     struct kernfs_yesde *parent,
					     const char *name, umode_t mode,
					     kuid_t uid, kgid_t gid,
					     unsigned flags)
{
	struct kernfs_yesde *kn;
	u32 id_highbits;
	int ret;

	name = kstrdup_const(name, GFP_KERNEL);
	if (!name)
		return NULL;

	kn = kmem_cache_zalloc(kernfs_yesde_cache, GFP_KERNEL);
	if (!kn)
		goto err_out1;

	idr_preload(GFP_KERNEL);
	spin_lock(&kernfs_idr_lock);
	ret = idr_alloc_cyclic(&root->iyes_idr, kn, 1, 0, GFP_ATOMIC);
	if (ret >= 0 && ret < root->last_id_lowbits)
		root->id_highbits++;
	id_highbits = root->id_highbits;
	root->last_id_lowbits = ret;
	spin_unlock(&kernfs_idr_lock);
	idr_preload_end();
	if (ret < 0)
		goto err_out2;

	kn->id = (u64)id_highbits << 32 | ret;

	atomic_set(&kn->count, 1);
	atomic_set(&kn->active, KN_DEACTIVATED_BIAS);
	RB_CLEAR_NODE(&kn->rb);

	kn->name = name;
	kn->mode = mode;
	kn->flags = flags;

	if (!uid_eq(uid, GLOBAL_ROOT_UID) || !gid_eq(gid, GLOBAL_ROOT_GID)) {
		struct iattr iattr = {
			.ia_valid = ATTR_UID | ATTR_GID,
			.ia_uid = uid,
			.ia_gid = gid,
		};

		ret = __kernfs_setattr(kn, &iattr);
		if (ret < 0)
			goto err_out3;
	}

	if (parent) {
		ret = security_kernfs_init_security(parent, kn);
		if (ret)
			goto err_out3;
	}

	return kn;

 err_out3:
	idr_remove(&root->iyes_idr, (u32)kernfs_iyes(kn));
 err_out2:
	kmem_cache_free(kernfs_yesde_cache, kn);
 err_out1:
	kfree_const(name);
	return NULL;
}

struct kernfs_yesde *kernfs_new_yesde(struct kernfs_yesde *parent,
				    const char *name, umode_t mode,
				    kuid_t uid, kgid_t gid,
				    unsigned flags)
{
	struct kernfs_yesde *kn;

	kn = __kernfs_new_yesde(kernfs_root(parent), parent,
			       name, mode, uid, gid, flags);
	if (kn) {
		kernfs_get(parent);
		kn->parent = parent;
	}
	return kn;
}

/*
 * kernfs_find_and_get_yesde_by_id - get kernfs_yesde from yesde id
 * @root: the kernfs root
 * @id: the target yesde id
 *
 * @id's lower 32bits encode iyes and upper gen.  If the gen portion is
 * zero, all generations are matched.
 *
 * RETURNS:
 * NULL on failure. Return a kernfs yesde with reference counter incremented
 */
struct kernfs_yesde *kernfs_find_and_get_yesde_by_id(struct kernfs_root *root,
						   u64 id)
{
	struct kernfs_yesde *kn;
	iyes_t iyes = kernfs_id_iyes(id);
	u32 gen = kernfs_id_gen(id);

	spin_lock(&kernfs_idr_lock);

	kn = idr_find(&root->iyes_idr, (u32)iyes);
	if (!kn)
		goto err_unlock;

	if (sizeof(iyes_t) >= sizeof(u64)) {
		/* we looked up with the low 32bits, compare the whole */
		if (kernfs_iyes(kn) != iyes)
			goto err_unlock;
	} else {
		/* 0 matches all generations */
		if (unlikely(gen && kernfs_gen(kn) != gen))
			goto err_unlock;
	}

	/*
	 * ACTIVATED is protected with kernfs_mutex but it was clear when
	 * @kn was added to idr and we just wanna see it set.  No need to
	 * grab kernfs_mutex.
	 */
	if (unlikely(!(kn->flags & KERNFS_ACTIVATED) ||
		     !atomic_inc_yest_zero(&kn->count)))
		goto err_unlock;

	spin_unlock(&kernfs_idr_lock);
	return kn;
err_unlock:
	spin_unlock(&kernfs_idr_lock);
	return NULL;
}

/**
 *	kernfs_add_one - add kernfs_yesde to parent without warning
 *	@kn: kernfs_yesde to be added
 *
 *	The caller must already have initialized @kn->parent.  This
 *	function increments nlink of the parent's iyesde if @kn is a
 *	directory and link into the children list of the parent.
 *
 *	RETURNS:
 *	0 on success, -EEXIST if entry with the given name already
 *	exists.
 */
int kernfs_add_one(struct kernfs_yesde *kn)
{
	struct kernfs_yesde *parent = kn->parent;
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
		ktime_get_real_ts64(&ps_iattr->ia_ctime);
		ps_iattr->ia_mtime = ps_iattr->ia_ctime;
	}

	mutex_unlock(&kernfs_mutex);

	/*
	 * Activate the new yesde unless CREATE_DEACTIVATED is requested.
	 * If yest activated here, the kernfs user is responsible for
	 * activating the yesde with kernfs_activate().  A yesde which hasn't
	 * been activated is yest visible to userland and its removal won't
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
 * kernfs_find_ns - find kernfs_yesde with the given name
 * @parent: kernfs_yesde to search under
 * @name: name to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_yesde with name @name under @parent.  Returns pointer to
 * the found kernfs_yesde on success, %NULL on failure.
 */
static struct kernfs_yesde *kernfs_find_ns(struct kernfs_yesde *parent,
					  const unsigned char *name,
					  const void *ns)
{
	struct rb_yesde *yesde = parent->dir.children.rb_yesde;
	bool has_ns = kernfs_ns_enabled(parent);
	unsigned int hash;

	lockdep_assert_held(&kernfs_mutex);

	if (has_ns != (bool)ns) {
		WARN(1, KERN_WARNING "kernfs: ns %s in '%s' for '%s'\n",
		     has_ns ? "required" : "invalid", parent->name, name);
		return NULL;
	}

	hash = kernfs_name_hash(name, ns);
	while (yesde) {
		struct kernfs_yesde *kn;
		int result;

		kn = rb_to_kn(yesde);
		result = kernfs_name_compare(hash, name, ns, kn);
		if (result < 0)
			yesde = yesde->rb_left;
		else if (result > 0)
			yesde = yesde->rb_right;
		else
			return kn;
	}
	return NULL;
}

static struct kernfs_yesde *kernfs_walk_ns(struct kernfs_yesde *parent,
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
 * kernfs_find_and_get_ns - find and get kernfs_yesde with the given name
 * @parent: kernfs_yesde to search under
 * @name: name to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_yesde with name @name under @parent and get a reference
 * if found.  This function may sleep and returns pointer to the found
 * kernfs_yesde on success, %NULL on failure.
 */
struct kernfs_yesde *kernfs_find_and_get_ns(struct kernfs_yesde *parent,
					   const char *name, const void *ns)
{
	struct kernfs_yesde *kn;

	mutex_lock(&kernfs_mutex);
	kn = kernfs_find_ns(parent, name, ns);
	kernfs_get(kn);
	mutex_unlock(&kernfs_mutex);

	return kn;
}
EXPORT_SYMBOL_GPL(kernfs_find_and_get_ns);

/**
 * kernfs_walk_and_get_ns - find and get kernfs_yesde with the given path
 * @parent: kernfs_yesde to search under
 * @path: path to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_yesde with path @path under @parent and get a reference
 * if found.  This function may sleep and returns pointer to the found
 * kernfs_yesde on success, %NULL on failure.
 */
struct kernfs_yesde *kernfs_walk_and_get_ns(struct kernfs_yesde *parent,
					   const char *path, const void *ns)
{
	struct kernfs_yesde *kn;

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
	struct kernfs_yesde *kn;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	idr_init(&root->iyes_idr);
	INIT_LIST_HEAD(&root->supers);

	/*
	 * On 64bit iyes setups, id is iyes.  On 32bit, low 32bits are iyes.
	 * High bits generation.  The starting value for both iyes and
	 * genenration is 1.  Initialize upper 32bit allocation
	 * accordingly.
	 */
	if (sizeof(iyes_t) >= sizeof(u64))
		root->id_highbits = 0;
	else
		root->id_highbits = 1;

	kn = __kernfs_new_yesde(root, NULL, "", S_IFDIR | S_IRUGO | S_IXUGO,
			       GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
			       KERNFS_DIR);
	if (!kn) {
		idr_destroy(&root->iyes_idr);
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
 * @uid: uid of the new directory
 * @gid: gid of the new directory
 * @priv: opaque data associated with the new directory
 * @ns: optional namespace tag of the directory
 *
 * Returns the created yesde on success, ERR_PTR() value on failure.
 */
struct kernfs_yesde *kernfs_create_dir_ns(struct kernfs_yesde *parent,
					 const char *name, umode_t mode,
					 kuid_t uid, kgid_t gid,
					 void *priv, const void *ns)
{
	struct kernfs_yesde *kn;
	int rc;

	/* allocate */
	kn = kernfs_new_yesde(parent, name, mode | S_IFDIR,
			     uid, gid, KERNFS_DIR);
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
 * Returns the created yesde on success, ERR_PTR() value on failure.
 */
struct kernfs_yesde *kernfs_create_empty_dir(struct kernfs_yesde *parent,
					    const char *name)
{
	struct kernfs_yesde *kn;
	int rc;

	/* allocate */
	kn = kernfs_new_yesde(parent, name, S_IRUGO|S_IXUGO|S_IFDIR,
			     GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, KERNFS_DIR);
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

static struct dentry *kernfs_iop_lookup(struct iyesde *dir,
					struct dentry *dentry,
					unsigned int flags)
{
	struct dentry *ret;
	struct kernfs_yesde *parent = dir->i_private;
	struct kernfs_yesde *kn;
	struct iyesde *iyesde;
	const void *ns = NULL;

	mutex_lock(&kernfs_mutex);

	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dir->i_sb)->ns;

	kn = kernfs_find_ns(parent, dentry->d_name.name, ns);

	/* yes such entry */
	if (!kn || !kernfs_active(kn)) {
		ret = NULL;
		goto out_unlock;
	}

	/* attach dentry and iyesde */
	iyesde = kernfs_get_iyesde(dir->i_sb, kn);
	if (!iyesde) {
		ret = ERR_PTR(-ENOMEM);
		goto out_unlock;
	}

	/* instantiate and hash dentry */
	ret = d_splice_alias(iyesde, dentry);
 out_unlock:
	mutex_unlock(&kernfs_mutex);
	return ret;
}

static int kernfs_iop_mkdir(struct iyesde *dir, struct dentry *dentry,
			    umode_t mode)
{
	struct kernfs_yesde *parent = dir->i_private;
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

static int kernfs_iop_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	struct kernfs_yesde *kn  = kernfs_dentry_yesde(dentry);
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

static int kernfs_iop_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			     struct iyesde *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	struct kernfs_yesde *kn = kernfs_dentry_yesde(old_dentry);
	struct kernfs_yesde *new_parent = new_dir->i_private;
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

const struct iyesde_operations kernfs_dir_iops = {
	.lookup		= kernfs_iop_lookup,
	.permission	= kernfs_iop_permission,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.listxattr	= kernfs_iop_listxattr,

	.mkdir		= kernfs_iop_mkdir,
	.rmdir		= kernfs_iop_rmdir,
	.rename		= kernfs_iop_rename,
};

static struct kernfs_yesde *kernfs_leftmost_descendant(struct kernfs_yesde *pos)
{
	struct kernfs_yesde *last;

	while (true) {
		struct rb_yesde *rbn;

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
 * @root: kernfs_yesde whose descendants to walk
 *
 * Find the next descendant to visit for post-order traversal of @root's
 * descendants.  @root is included in the iteration and the last yesde to be
 * visited.
 */
static struct kernfs_yesde *kernfs_next_descendant_post(struct kernfs_yesde *pos,
						       struct kernfs_yesde *root)
{
	struct rb_yesde *rbn;

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

	/* yes sibling left, visit parent */
	return pos->parent;
}

/**
 * kernfs_activate - activate a yesde which started deactivated
 * @kn: kernfs_yesde whose subtree is to be activated
 *
 * If the root has KERNFS_ROOT_CREATE_DEACTIVATED set, a newly created yesde
 * needs to be explicitly activated.  A yesde which hasn't been activated
 * isn't visible to userland and deactivation is skipped during its
 * removal.  This is useful to construct atomic init sequences where
 * creation of multiple yesdes should either succeed or fail atomically.
 *
 * The caller is responsible for ensuring that this function is yest called
 * after kernfs_remove*() is invoked on @kn.
 */
void kernfs_activate(struct kernfs_yesde *kn)
{
	struct kernfs_yesde *pos;

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

static void __kernfs_remove(struct kernfs_yesde *kn)
{
	struct kernfs_yesde *pos;

	lockdep_assert_held(&kernfs_mutex);

	/*
	 * Short-circuit if yesn-root @kn has already finished removal.
	 * This is for kernfs_remove_self() which plays with active ref
	 * after removal.
	 */
	if (!kn || (kn->parent && RB_EMPTY_NODE(&kn->rb)))
		return;

	pr_debug("kernfs %s: removing\n", kn->name);

	/* prevent any new usage under @kn by deactivating all yesdes */
	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn)))
		if (kernfs_active(pos))
			atomic_add(KN_DEACTIVATED_BIAS, &pos->active);

	/* deactivate and unlink the subtree yesde-by-yesde */
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
		 * its lockdep anyestations for yesdes which have never been
		 * activated and allows embedding kernfs_remove() in create
		 * error paths without worrying about draining.
		 */
		if (kn->flags & KERNFS_ACTIVATED)
			kernfs_drain(pos);
		else
			WARN_ON_ONCE(atomic_read(&kn->active) != KN_DEACTIVATED_BIAS);

		/*
		 * kernfs_unlink_sibling() succeeds once per yesde.  Use it
		 * to decide who's responsible for cleanups.
		 */
		if (!pos->parent || kernfs_unlink_sibling(pos)) {
			struct kernfs_iattrs *ps_iattr =
				pos->parent ? pos->parent->iattr : NULL;

			/* update timestamps on the parent */
			if (ps_iattr) {
				ktime_get_real_ts64(&ps_iattr->ia_ctime);
				ps_iattr->ia_mtime = ps_iattr->ia_ctime;
			}

			kernfs_put(pos);
		}

		kernfs_put(pos);
	} while (pos != kn);
}

/**
 * kernfs_remove - remove a kernfs_yesde recursively
 * @kn: the kernfs_yesde to remove
 *
 * Remove @kn along with all its subdirectories and files.
 */
void kernfs_remove(struct kernfs_yesde *kn)
{
	mutex_lock(&kernfs_mutex);
	__kernfs_remove(kn);
	mutex_unlock(&kernfs_mutex);
}

/**
 * kernfs_break_active_protection - break out of active protection
 * @kn: the self kernfs_yesde
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
void kernfs_break_active_protection(struct kernfs_yesde *kn)
{
	/*
	 * Take out ourself out of the active ref dependency chain.  If
	 * we're called without an active ref, lockdep will complain.
	 */
	kernfs_put_active(kn);
}

/**
 * kernfs_unbreak_active_protection - undo kernfs_break_active_protection()
 * @kn: the self kernfs_yesde
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
void kernfs_unbreak_active_protection(struct kernfs_yesde *kn)
{
	/*
	 * @kn->active could be in any state; however, the increment we do
	 * here will be undone as soon as the enclosing kernfs operation
	 * finishes and this temporary bump can't break anything.  If @kn
	 * is alive, yesthing changes.  If @kn is being deactivated, the
	 * soon-to-follow put will either finish deactivation or restore
	 * deactivated state.  If @kn is already removed, the temporary
	 * bump is guaranteed to be gone before @kn is released.
	 */
	atomic_inc(&kn->active);
	if (kernfs_lockdep(kn))
		rwsem_acquire(&kn->dep_map, 0, 1, _RET_IP_);
}

/**
 * kernfs_remove_self - remove a kernfs_yesde from its own method
 * @kn: the self kernfs_yesde to remove
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
 * before proceeding with the usual removal path.  kernfs will igyesre later
 * kernfs_remove() on self.
 *
 * kernfs_remove_self() can be called multiple times concurrently on the
 * same kernfs_yesde.  Only the first one actually performs removal and
 * returns %true.  All others will wait until the kernfs operation which
 * won self-removal finishes and return %false.  Note that the losers wait
 * for the completion of yest only the winning kernfs_remove_self() but also
 * the whole kernfs_ops which won the arbitration.  This can be used to
 * guarantee, for example, all concurrent writes to a "delete" file to
 * finish only after the whole operation is complete.
 */
bool kernfs_remove_self(struct kernfs_yesde *kn)
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
 * kernfs_remove_by_name_ns - find a kernfs_yesde by name and remove it
 * @parent: parent of the target
 * @name: name of the kernfs_yesde to remove
 * @ns: namespace tag of the kernfs_yesde to remove
 *
 * Look for the kernfs_yesde with @name and @ns under @parent and remove it.
 * Returns 0 on success, -ENOENT if such entry doesn't exist.
 */
int kernfs_remove_by_name_ns(struct kernfs_yesde *parent, const char *name,
			     const void *ns)
{
	struct kernfs_yesde *kn;

	if (!parent) {
		WARN(1, KERN_WARNING "kernfs: can yest remove '%s', yes directory\n",
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
 * kernfs_rename_ns - move and rename a kernfs_yesde
 * @kn: target yesde
 * @new_parent: new parent to put @sd under
 * @new_name: new name
 * @new_ns: new namespace tag
 */
int kernfs_rename_ns(struct kernfs_yesde *kn, struct kernfs_yesde *new_parent,
		     const char *new_name, const void *new_ns)
{
	struct kernfs_yesde *old_parent;
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
		goto out;	/* yesthing to rename */

	error = -EEXIST;
	if (kernfs_find_ns(new_parent, new_name, new_ns))
		goto out;

	/* rename kernfs_yesde */
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
static inline unsigned char dt_type(struct kernfs_yesde *kn)
{
	return (kn->mode >> 12) & 15;
}

static int kernfs_dir_fop_release(struct iyesde *iyesde, struct file *filp)
{
	kernfs_put(filp->private_data);
	return 0;
}

static struct kernfs_yesde *kernfs_dir_pos(const void *ns,
	struct kernfs_yesde *parent, loff_t hash, struct kernfs_yesde *pos)
{
	if (pos) {
		int valid = kernfs_active(pos) &&
			pos->parent == parent && hash == pos->hash;
		kernfs_put(pos);
		if (!valid)
			pos = NULL;
	}
	if (!pos && (hash > 1) && (hash < INT_MAX)) {
		struct rb_yesde *yesde = parent->dir.children.rb_yesde;
		while (yesde) {
			pos = rb_to_kn(yesde);

			if (hash < pos->hash)
				yesde = yesde->rb_left;
			else if (hash > pos->hash)
				yesde = yesde->rb_right;
			else
				break;
		}
	}
	/* Skip over entries which are dying/dead or in the wrong namespace */
	while (pos && (!kernfs_active(pos) || pos->ns != ns)) {
		struct rb_yesde *yesde = rb_next(&pos->rb);
		if (!yesde)
			pos = NULL;
		else
			pos = rb_to_kn(yesde);
	}
	return pos;
}

static struct kernfs_yesde *kernfs_dir_next_pos(const void *ns,
	struct kernfs_yesde *parent, iyes_t iyes, struct kernfs_yesde *pos)
{
	pos = kernfs_dir_pos(ns, parent, iyes, pos);
	if (pos) {
		do {
			struct rb_yesde *yesde = rb_next(&pos->rb);
			if (!yesde)
				pos = NULL;
			else
				pos = rb_to_kn(yesde);
		} while (pos && (!kernfs_active(pos) || pos->ns != ns));
	}
	return pos;
}

static int kernfs_fop_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct kernfs_yesde *parent = kernfs_dentry_yesde(dentry);
	struct kernfs_yesde *pos = file->private_data;
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
		iyes_t iyes = kernfs_iyes(pos);

		ctx->pos = pos->hash;
		file->private_data = pos;
		kernfs_get(pos);

		mutex_unlock(&kernfs_mutex);
		if (!dir_emit(ctx, name, len, iyes, type))
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
