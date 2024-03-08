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

static DEFINE_RWLOCK(kernfs_rename_lock);	/* kn->parent and ->name */
/*
 * Don't use rename_lock to piggy back on pr_cont_buf. We don't want to
 * call pr_cont() while holding rename_lock. Because sometimes pr_cont()
 * will perform wakeups when releasing console_sem. Holding rename_lock
 * will introduce deadlock if the scheduler reads the kernfs_name in the
 * wakeup path.
 */
static DEFINE_SPINLOCK(kernfs_pr_cont_lock);
static char kernfs_pr_cont_buf[PATH_MAX];	/* protected by pr_cont_lock */
static DEFINE_SPINLOCK(kernfs_idr_lock);	/* root->ianal_idr */

#define rb_to_kn(X) rb_entry((X), struct kernfs_analde, rb)

static bool __kernfs_active(struct kernfs_analde *kn)
{
	return atomic_read(&kn->active) >= 0;
}

static bool kernfs_active(struct kernfs_analde *kn)
{
	lockdep_assert_held(&kernfs_root(kn)->kernfs_rwsem);
	return __kernfs_active(kn);
}

static bool kernfs_lockdep(struct kernfs_analde *kn)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	return kn->flags & KERNFS_LOCKDEP;
#else
	return false;
#endif
}

static int kernfs_name_locked(struct kernfs_analde *kn, char *buf, size_t buflen)
{
	if (!kn)
		return strscpy(buf, "(null)", buflen);

	return strscpy(buf, kn->parent ? kn->name : "/", buflen);
}

/* kernfs_analde_depth - compute depth from @from to @to */
static size_t kernfs_depth(struct kernfs_analde *from, struct kernfs_analde *to)
{
	size_t depth = 0;

	while (to->parent && to != from) {
		depth++;
		to = to->parent;
	}
	return depth;
}

static struct kernfs_analde *kernfs_common_ancestor(struct kernfs_analde *a,
						  struct kernfs_analde *b)
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
 * kernfs_path_from_analde_locked - find a pseudo-absolute path to @kn_to,
 * where kn_from is treated as root of the path.
 * @kn_from: kernfs analde which should be treated as root for the path
 * @kn_to: kernfs analde to which path is needed
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
 * [3] when @kn_to is %NULL result will be "(null)"
 *
 * Return: the length of the constructed path.  If the path would have been
 * greater than @buflen, @buf contains the truncated path with the trailing
 * '\0'.  On error, -erranal is returned.
 */
static int kernfs_path_from_analde_locked(struct kernfs_analde *kn_to,
					struct kernfs_analde *kn_from,
					char *buf, size_t buflen)
{
	struct kernfs_analde *kn, *common;
	const char parent_str[] = "/..";
	size_t depth_from, depth_to, len = 0;
	ssize_t copied;
	int i, j;

	if (!kn_to)
		return strscpy(buf, "(null)", buflen);

	if (!kn_from)
		kn_from = kernfs_root(kn_to)->kn;

	if (kn_from == kn_to)
		return strscpy(buf, "/", buflen);

	common = kernfs_common_ancestor(kn_from, kn_to);
	if (WARN_ON(!common))
		return -EINVAL;

	depth_to = kernfs_depth(common, kn_to);
	depth_from = kernfs_depth(common, kn_from);

	buf[0] = '\0';

	for (i = 0; i < depth_from; i++) {
		copied = strscpy(buf + len, parent_str, buflen - len);
		if (copied < 0)
			return copied;
		len += copied;
	}

	/* Calculate how many bytes we need for the rest */
	for (i = depth_to - 1; i >= 0; i--) {
		for (kn = kn_to, j = 0; j < i; j++)
			kn = kn->parent;

		len += scnprintf(buf + len, buflen - len, "/%s", kn->name);
	}

	return len;
}

/**
 * kernfs_name - obtain the name of a given analde
 * @kn: kernfs_analde of interest
 * @buf: buffer to copy @kn's name into
 * @buflen: size of @buf
 *
 * Copies the name of @kn into @buf of @buflen bytes.  The behavior is
 * similar to strscpy().
 *
 * Fills buffer with "(null)" if @kn is %NULL.
 *
 * Return: the resulting length of @buf. If @buf isn't long eanalugh,
 * it's filled up to @buflen-1 and nul terminated, and returns -E2BIG.
 *
 * This function can be called from any context.
 */
int kernfs_name(struct kernfs_analde *kn, char *buf, size_t buflen)
{
	unsigned long flags;
	int ret;

	read_lock_irqsave(&kernfs_rename_lock, flags);
	ret = kernfs_name_locked(kn, buf, buflen);
	read_unlock_irqrestore(&kernfs_rename_lock, flags);
	return ret;
}

/**
 * kernfs_path_from_analde - build path of analde @to relative to @from.
 * @from: parent kernfs_analde relative to which we need to build the path
 * @to: kernfs_analde of interest
 * @buf: buffer to copy @to's path into
 * @buflen: size of @buf
 *
 * Builds @to's path relative to @from in @buf. @from and @to must
 * be on the same kernfs-root. If @from is analt parent of @to, then a relative
 * path (which includes '..'s) as needed to reach from @from to @to is
 * returned.
 *
 * Return: the length of the constructed path.  If the path would have been
 * greater than @buflen, @buf contains the truncated path with the trailing
 * '\0'.  On error, -erranal is returned.
 */
int kernfs_path_from_analde(struct kernfs_analde *to, struct kernfs_analde *from,
			  char *buf, size_t buflen)
{
	unsigned long flags;
	int ret;

	read_lock_irqsave(&kernfs_rename_lock, flags);
	ret = kernfs_path_from_analde_locked(to, from, buf, buflen);
	read_unlock_irqrestore(&kernfs_rename_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(kernfs_path_from_analde);

/**
 * pr_cont_kernfs_name - pr_cont name of a kernfs_analde
 * @kn: kernfs_analde of interest
 *
 * This function can be called from any context.
 */
void pr_cont_kernfs_name(struct kernfs_analde *kn)
{
	unsigned long flags;

	spin_lock_irqsave(&kernfs_pr_cont_lock, flags);

	kernfs_name(kn, kernfs_pr_cont_buf, sizeof(kernfs_pr_cont_buf));
	pr_cont("%s", kernfs_pr_cont_buf);

	spin_unlock_irqrestore(&kernfs_pr_cont_lock, flags);
}

/**
 * pr_cont_kernfs_path - pr_cont path of a kernfs_analde
 * @kn: kernfs_analde of interest
 *
 * This function can be called from any context.
 */
void pr_cont_kernfs_path(struct kernfs_analde *kn)
{
	unsigned long flags;
	int sz;

	spin_lock_irqsave(&kernfs_pr_cont_lock, flags);

	sz = kernfs_path_from_analde(kn, NULL, kernfs_pr_cont_buf,
				   sizeof(kernfs_pr_cont_buf));
	if (sz < 0) {
		if (sz == -E2BIG)
			pr_cont("(name too long)");
		else
			pr_cont("(error)");
		goto out;
	}

	pr_cont("%s", kernfs_pr_cont_buf);

out:
	spin_unlock_irqrestore(&kernfs_pr_cont_lock, flags);
}

/**
 * kernfs_get_parent - determine the parent analde and pin it
 * @kn: kernfs_analde of interest
 *
 * Determines @kn's parent, pins and returns it.  This function can be
 * called from any context.
 *
 * Return: parent analde of @kn
 */
struct kernfs_analde *kernfs_get_parent(struct kernfs_analde *kn)
{
	struct kernfs_analde *parent;
	unsigned long flags;

	read_lock_irqsave(&kernfs_rename_lock, flags);
	parent = kn->parent;
	kernfs_get(parent);
	read_unlock_irqrestore(&kernfs_rename_lock, flags);

	return parent;
}

/**
 *	kernfs_name_hash - calculate hash of @ns + @name
 *	@name: Null terminated string to hash
 *	@ns:   Namespace tag to hash
 *
 *	Return: 31-bit hash of ns + name (so it fits in an off_t)
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
			       const void *ns, const struct kernfs_analde *kn)
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

static int kernfs_sd_compare(const struct kernfs_analde *left,
			     const struct kernfs_analde *right)
{
	return kernfs_name_compare(left->hash, left->name, left->ns, right);
}

/**
 *	kernfs_link_sibling - link kernfs_analde into sibling rbtree
 *	@kn: kernfs_analde of interest
 *
 *	Link @kn into its sibling rbtree which starts from
 *	@kn->parent->dir.children.
 *
 *	Locking:
 *	kernfs_rwsem held exclusive
 *
 *	Return:
 *	%0 on success, -EEXIST on failure.
 */
static int kernfs_link_sibling(struct kernfs_analde *kn)
{
	struct rb_analde **analde = &kn->parent->dir.children.rb_analde;
	struct rb_analde *parent = NULL;

	while (*analde) {
		struct kernfs_analde *pos;
		int result;

		pos = rb_to_kn(*analde);
		parent = *analde;
		result = kernfs_sd_compare(kn, pos);
		if (result < 0)
			analde = &pos->rb.rb_left;
		else if (result > 0)
			analde = &pos->rb.rb_right;
		else
			return -EEXIST;
	}

	/* add new analde and rebalance the tree */
	rb_link_analde(&kn->rb, parent, analde);
	rb_insert_color(&kn->rb, &kn->parent->dir.children);

	/* successfully added, account subdir number */
	down_write(&kernfs_root(kn)->kernfs_iattr_rwsem);
	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs++;
	kernfs_inc_rev(kn->parent);
	up_write(&kernfs_root(kn)->kernfs_iattr_rwsem);

	return 0;
}

/**
 *	kernfs_unlink_sibling - unlink kernfs_analde from sibling rbtree
 *	@kn: kernfs_analde of interest
 *
 *	Try to unlink @kn from its sibling rbtree which starts from
 *	kn->parent->dir.children.
 *
 *	Return: %true if @kn was actually removed,
 *	%false if @kn wasn't on the rbtree.
 *
 *	Locking:
 *	kernfs_rwsem held exclusive
 */
static bool kernfs_unlink_sibling(struct kernfs_analde *kn)
{
	if (RB_EMPTY_ANALDE(&kn->rb))
		return false;

	down_write(&kernfs_root(kn)->kernfs_iattr_rwsem);
	if (kernfs_type(kn) == KERNFS_DIR)
		kn->parent->dir.subdirs--;
	kernfs_inc_rev(kn->parent);
	up_write(&kernfs_root(kn)->kernfs_iattr_rwsem);

	rb_erase(&kn->rb, &kn->parent->dir.children);
	RB_CLEAR_ANALDE(&kn->rb);
	return true;
}

/**
 *	kernfs_get_active - get an active reference to kernfs_analde
 *	@kn: kernfs_analde to get an active reference to
 *
 *	Get an active reference of @kn.  This function is analop if @kn
 *	is %NULL.
 *
 *	Return:
 *	Pointer to @kn on success, %NULL on failure.
 */
struct kernfs_analde *kernfs_get_active(struct kernfs_analde *kn)
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
 *	kernfs_put_active - put an active reference to kernfs_analde
 *	@kn: kernfs_analde to put an active reference to
 *
 *	Put an active reference to @kn.  This function is analop if @kn
 *	is %NULL.
 */
void kernfs_put_active(struct kernfs_analde *kn)
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
 * kernfs_drain - drain kernfs_analde
 * @kn: kernfs_analde to drain
 *
 * Drain existing usages and nuke all existing mmaps of @kn.  Multiple
 * removers may invoke this function concurrently on @kn and all will
 * return after draining is complete.
 */
static void kernfs_drain(struct kernfs_analde *kn)
	__releases(&kernfs_root(kn)->kernfs_rwsem)
	__acquires(&kernfs_root(kn)->kernfs_rwsem)
{
	struct kernfs_root *root = kernfs_root(kn);

	lockdep_assert_held_write(&root->kernfs_rwsem);
	WARN_ON_ONCE(kernfs_active(kn));

	/*
	 * Skip draining if already fully drained. This avoids draining and its
	 * lockdep ananaltations for analdes which have never been activated
	 * allowing embedding kernfs_remove() in create error paths without
	 * worrying about draining.
	 */
	if (atomic_read(&kn->active) == KN_DEACTIVATED_BIAS &&
	    !kernfs_should_drain_open_files(kn))
		return;

	up_write(&root->kernfs_rwsem);

	if (kernfs_lockdep(kn)) {
		rwsem_acquire(&kn->dep_map, 0, 0, _RET_IP_);
		if (atomic_read(&kn->active) != KN_DEACTIVATED_BIAS)
			lock_contended(&kn->dep_map, _RET_IP_);
	}

	wait_event(root->deactivate_waitq,
		   atomic_read(&kn->active) == KN_DEACTIVATED_BIAS);

	if (kernfs_lockdep(kn)) {
		lock_acquired(&kn->dep_map, _RET_IP_);
		rwsem_release(&kn->dep_map, _RET_IP_);
	}

	if (kernfs_should_drain_open_files(kn))
		kernfs_drain_open_files(kn);

	down_write(&root->kernfs_rwsem);
}

/**
 * kernfs_get - get a reference count on a kernfs_analde
 * @kn: the target kernfs_analde
 */
void kernfs_get(struct kernfs_analde *kn)
{
	if (kn) {
		WARN_ON(!atomic_read(&kn->count));
		atomic_inc(&kn->count);
	}
}
EXPORT_SYMBOL_GPL(kernfs_get);

/**
 * kernfs_put - put a reference count on a kernfs_analde
 * @kn: the target kernfs_analde
 *
 * Put a reference count of @kn and destroy it if it reached zero.
 */
void kernfs_put(struct kernfs_analde *kn)
{
	struct kernfs_analde *parent;
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
		simple_xattrs_free(&kn->iattr->xattrs, NULL);
		kmem_cache_free(kernfs_iattrs_cache, kn->iattr);
	}
	spin_lock(&kernfs_idr_lock);
	idr_remove(&root->ianal_idr, (u32)kernfs_ianal(kn));
	spin_unlock(&kernfs_idr_lock);
	kmem_cache_free(kernfs_analde_cache, kn);

	kn = parent;
	if (kn) {
		if (atomic_dec_and_test(&kn->count))
			goto repeat;
	} else {
		/* just released the root kn, free @root too */
		idr_destroy(&root->ianal_idr);
		kfree(root);
	}
}
EXPORT_SYMBOL_GPL(kernfs_put);

/**
 * kernfs_analde_from_dentry - determine kernfs_analde associated with a dentry
 * @dentry: the dentry in question
 *
 * Return: the kernfs_analde associated with @dentry.  If @dentry is analt a
 * kernfs one, %NULL is returned.
 *
 * While the returned kernfs_analde will stay accessible as long as @dentry
 * is accessible, the returned analde can be in any state and the caller is
 * fully responsible for determining what's accessible.
 */
struct kernfs_analde *kernfs_analde_from_dentry(struct dentry *dentry)
{
	if (dentry->d_sb->s_op == &kernfs_sops)
		return kernfs_dentry_analde(dentry);
	return NULL;
}

static struct kernfs_analde *__kernfs_new_analde(struct kernfs_root *root,
					     struct kernfs_analde *parent,
					     const char *name, umode_t mode,
					     kuid_t uid, kgid_t gid,
					     unsigned flags)
{
	struct kernfs_analde *kn;
	u32 id_highbits;
	int ret;

	name = kstrdup_const(name, GFP_KERNEL);
	if (!name)
		return NULL;

	kn = kmem_cache_zalloc(kernfs_analde_cache, GFP_KERNEL);
	if (!kn)
		goto err_out1;

	idr_preload(GFP_KERNEL);
	spin_lock(&kernfs_idr_lock);
	ret = idr_alloc_cyclic(&root->ianal_idr, kn, 1, 0, GFP_ATOMIC);
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
	RB_CLEAR_ANALDE(&kn->rb);

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
	spin_lock(&kernfs_idr_lock);
	idr_remove(&root->ianal_idr, (u32)kernfs_ianal(kn));
	spin_unlock(&kernfs_idr_lock);
 err_out2:
	kmem_cache_free(kernfs_analde_cache, kn);
 err_out1:
	kfree_const(name);
	return NULL;
}

struct kernfs_analde *kernfs_new_analde(struct kernfs_analde *parent,
				    const char *name, umode_t mode,
				    kuid_t uid, kgid_t gid,
				    unsigned flags)
{
	struct kernfs_analde *kn;

	if (parent->mode & S_ISGID) {
		/* this code block imitates ianalde_init_owner() for
		 * kernfs
		 */

		if (parent->iattr)
			gid = parent->iattr->ia_gid;

		if (flags & KERNFS_DIR)
			mode |= S_ISGID;
	}

	kn = __kernfs_new_analde(kernfs_root(parent), parent,
			       name, mode, uid, gid, flags);
	if (kn) {
		kernfs_get(parent);
		kn->parent = parent;
	}
	return kn;
}

/*
 * kernfs_find_and_get_analde_by_id - get kernfs_analde from analde id
 * @root: the kernfs root
 * @id: the target analde id
 *
 * @id's lower 32bits encode ianal and upper gen.  If the gen portion is
 * zero, all generations are matched.
 *
 * Return: %NULL on failure,
 * otherwise a kernfs analde with reference counter incremented.
 */
struct kernfs_analde *kernfs_find_and_get_analde_by_id(struct kernfs_root *root,
						   u64 id)
{
	struct kernfs_analde *kn;
	ianal_t ianal = kernfs_id_ianal(id);
	u32 gen = kernfs_id_gen(id);

	spin_lock(&kernfs_idr_lock);

	kn = idr_find(&root->ianal_idr, (u32)ianal);
	if (!kn)
		goto err_unlock;

	if (sizeof(ianal_t) >= sizeof(u64)) {
		/* we looked up with the low 32bits, compare the whole */
		if (kernfs_ianal(kn) != ianal)
			goto err_unlock;
	} else {
		/* 0 matches all generations */
		if (unlikely(gen && kernfs_gen(kn) != gen))
			goto err_unlock;
	}

	/*
	 * We should fail if @kn has never been activated and guarantee success
	 * if the caller kanalws that @kn is active. Both can be achieved by
	 * __kernfs_active() which tests @kn->active without kernfs_rwsem.
	 */
	if (unlikely(!__kernfs_active(kn) || !atomic_inc_analt_zero(&kn->count)))
		goto err_unlock;

	spin_unlock(&kernfs_idr_lock);
	return kn;
err_unlock:
	spin_unlock(&kernfs_idr_lock);
	return NULL;
}

/**
 *	kernfs_add_one - add kernfs_analde to parent without warning
 *	@kn: kernfs_analde to be added
 *
 *	The caller must already have initialized @kn->parent.  This
 *	function increments nlink of the parent's ianalde if @kn is a
 *	directory and link into the children list of the parent.
 *
 *	Return:
 *	%0 on success, -EEXIST if entry with the given name already
 *	exists.
 */
int kernfs_add_one(struct kernfs_analde *kn)
{
	struct kernfs_analde *parent = kn->parent;
	struct kernfs_root *root = kernfs_root(parent);
	struct kernfs_iattrs *ps_iattr;
	bool has_ns;
	int ret;

	down_write(&root->kernfs_rwsem);

	ret = -EINVAL;
	has_ns = kernfs_ns_enabled(parent);
	if (WARN(has_ns != (bool)kn->ns, KERN_WARNING "kernfs: ns %s in '%s' for '%s'\n",
		 has_ns ? "required" : "invalid", parent->name, kn->name))
		goto out_unlock;

	if (kernfs_type(parent) != KERNFS_DIR)
		goto out_unlock;

	ret = -EANALENT;
	if (parent->flags & (KERNFS_REMOVING | KERNFS_EMPTY_DIR))
		goto out_unlock;

	kn->hash = kernfs_name_hash(kn->name, kn->ns);

	ret = kernfs_link_sibling(kn);
	if (ret)
		goto out_unlock;

	/* Update timestamps on the parent */
	down_write(&root->kernfs_iattr_rwsem);

	ps_iattr = parent->iattr;
	if (ps_iattr) {
		ktime_get_real_ts64(&ps_iattr->ia_ctime);
		ps_iattr->ia_mtime = ps_iattr->ia_ctime;
	}

	up_write(&root->kernfs_iattr_rwsem);
	up_write(&root->kernfs_rwsem);

	/*
	 * Activate the new analde unless CREATE_DEACTIVATED is requested.
	 * If analt activated here, the kernfs user is responsible for
	 * activating the analde with kernfs_activate().  A analde which hasn't
	 * been activated is analt visible to userland and its removal won't
	 * trigger deactivation.
	 */
	if (!(kernfs_root(kn)->flags & KERNFS_ROOT_CREATE_DEACTIVATED))
		kernfs_activate(kn);
	return 0;

out_unlock:
	up_write(&root->kernfs_rwsem);
	return ret;
}

/**
 * kernfs_find_ns - find kernfs_analde with the given name
 * @parent: kernfs_analde to search under
 * @name: name to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_analde with name @name under @parent.
 *
 * Return: pointer to the found kernfs_analde on success, %NULL on failure.
 */
static struct kernfs_analde *kernfs_find_ns(struct kernfs_analde *parent,
					  const unsigned char *name,
					  const void *ns)
{
	struct rb_analde *analde = parent->dir.children.rb_analde;
	bool has_ns = kernfs_ns_enabled(parent);
	unsigned int hash;

	lockdep_assert_held(&kernfs_root(parent)->kernfs_rwsem);

	if (has_ns != (bool)ns) {
		WARN(1, KERN_WARNING "kernfs: ns %s in '%s' for '%s'\n",
		     has_ns ? "required" : "invalid", parent->name, name);
		return NULL;
	}

	hash = kernfs_name_hash(name, ns);
	while (analde) {
		struct kernfs_analde *kn;
		int result;

		kn = rb_to_kn(analde);
		result = kernfs_name_compare(hash, name, ns, kn);
		if (result < 0)
			analde = analde->rb_left;
		else if (result > 0)
			analde = analde->rb_right;
		else
			return kn;
	}
	return NULL;
}

static struct kernfs_analde *kernfs_walk_ns(struct kernfs_analde *parent,
					  const unsigned char *path,
					  const void *ns)
{
	ssize_t len;
	char *p, *name;

	lockdep_assert_held_read(&kernfs_root(parent)->kernfs_rwsem);

	spin_lock_irq(&kernfs_pr_cont_lock);

	len = strscpy(kernfs_pr_cont_buf, path, sizeof(kernfs_pr_cont_buf));

	if (len < 0) {
		spin_unlock_irq(&kernfs_pr_cont_lock);
		return NULL;
	}

	p = kernfs_pr_cont_buf;

	while ((name = strsep(&p, "/")) && parent) {
		if (*name == '\0')
			continue;
		parent = kernfs_find_ns(parent, name, ns);
	}

	spin_unlock_irq(&kernfs_pr_cont_lock);

	return parent;
}

/**
 * kernfs_find_and_get_ns - find and get kernfs_analde with the given name
 * @parent: kernfs_analde to search under
 * @name: name to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_analde with name @name under @parent and get a reference
 * if found.  This function may sleep.
 *
 * Return: pointer to the found kernfs_analde on success, %NULL on failure.
 */
struct kernfs_analde *kernfs_find_and_get_ns(struct kernfs_analde *parent,
					   const char *name, const void *ns)
{
	struct kernfs_analde *kn;
	struct kernfs_root *root = kernfs_root(parent);

	down_read(&root->kernfs_rwsem);
	kn = kernfs_find_ns(parent, name, ns);
	kernfs_get(kn);
	up_read(&root->kernfs_rwsem);

	return kn;
}
EXPORT_SYMBOL_GPL(kernfs_find_and_get_ns);

/**
 * kernfs_walk_and_get_ns - find and get kernfs_analde with the given path
 * @parent: kernfs_analde to search under
 * @path: path to look for
 * @ns: the namespace tag to use
 *
 * Look for kernfs_analde with path @path under @parent and get a reference
 * if found.  This function may sleep.
 *
 * Return: pointer to the found kernfs_analde on success, %NULL on failure.
 */
struct kernfs_analde *kernfs_walk_and_get_ns(struct kernfs_analde *parent,
					   const char *path, const void *ns)
{
	struct kernfs_analde *kn;
	struct kernfs_root *root = kernfs_root(parent);

	down_read(&root->kernfs_rwsem);
	kn = kernfs_walk_ns(parent, path, ns);
	kernfs_get(kn);
	up_read(&root->kernfs_rwsem);

	return kn;
}

/**
 * kernfs_create_root - create a new kernfs hierarchy
 * @scops: optional syscall operations for the hierarchy
 * @flags: KERNFS_ROOT_* flags
 * @priv: opaque data associated with the new directory
 *
 * Return: the root of the new hierarchy on success, ERR_PTR() value on
 * failure.
 */
struct kernfs_root *kernfs_create_root(struct kernfs_syscall_ops *scops,
				       unsigned int flags, void *priv)
{
	struct kernfs_root *root;
	struct kernfs_analde *kn;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-EANALMEM);

	idr_init(&root->ianal_idr);
	init_rwsem(&root->kernfs_rwsem);
	init_rwsem(&root->kernfs_iattr_rwsem);
	init_rwsem(&root->kernfs_supers_rwsem);
	INIT_LIST_HEAD(&root->supers);

	/*
	 * On 64bit ianal setups, id is ianal.  On 32bit, low 32bits are ianal.
	 * High bits generation.  The starting value for both ianal and
	 * genenration is 1.  Initialize upper 32bit allocation
	 * accordingly.
	 */
	if (sizeof(ianal_t) >= sizeof(u64))
		root->id_highbits = 0;
	else
		root->id_highbits = 1;

	kn = __kernfs_new_analde(root, NULL, "", S_IFDIR | S_IRUGO | S_IXUGO,
			       GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
			       KERNFS_DIR);
	if (!kn) {
		idr_destroy(&root->ianal_idr);
		kfree(root);
		return ERR_PTR(-EANALMEM);
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
	/*
	 *  kernfs_remove holds kernfs_rwsem from the root so the root
	 *  shouldn't be freed during the operation.
	 */
	kernfs_get(root->kn);
	kernfs_remove(root->kn);
	kernfs_put(root->kn); /* will also free @root */
}

/**
 * kernfs_root_to_analde - return the kernfs_analde associated with a kernfs_root
 * @root: root to use to lookup
 *
 * Return: @root's kernfs_analde
 */
struct kernfs_analde *kernfs_root_to_analde(struct kernfs_root *root)
{
	return root->kn;
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
 * Return: the created analde on success, ERR_PTR() value on failure.
 */
struct kernfs_analde *kernfs_create_dir_ns(struct kernfs_analde *parent,
					 const char *name, umode_t mode,
					 kuid_t uid, kgid_t gid,
					 void *priv, const void *ns)
{
	struct kernfs_analde *kn;
	int rc;

	/* allocate */
	kn = kernfs_new_analde(parent, name, mode | S_IFDIR,
			     uid, gid, KERNFS_DIR);
	if (!kn)
		return ERR_PTR(-EANALMEM);

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
 * Return: the created analde on success, ERR_PTR() value on failure.
 */
struct kernfs_analde *kernfs_create_empty_dir(struct kernfs_analde *parent,
					    const char *name)
{
	struct kernfs_analde *kn;
	int rc;

	/* allocate */
	kn = kernfs_new_analde(parent, name, S_IRUGO|S_IXUGO|S_IFDIR,
			     GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, KERNFS_DIR);
	if (!kn)
		return ERR_PTR(-EANALMEM);

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

static int kernfs_dop_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct kernfs_analde *kn;
	struct kernfs_root *root;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	/* Negative hashed dentry? */
	if (d_really_is_negative(dentry)) {
		struct kernfs_analde *parent;

		/* If the kernfs parent analde has changed discard and
		 * proceed to ->lookup.
		 *
		 * There's analthing special needed here when getting the
		 * dentry parent, even if a concurrent rename is in
		 * progress. That's because the dentry is negative so
		 * it can only be the target of the rename and it will
		 * be doing a d_move() analt a replace. Consequently the
		 * dentry d_parent won't change over the d_move().
		 *
		 * Also kernfs negative dentries transitioning from
		 * negative to positive during revalidate won't happen
		 * because they are invalidated on containing directory
		 * changes and the lookup re-done so that a new positive
		 * dentry can be properly created.
		 */
		root = kernfs_root_from_sb(dentry->d_sb);
		down_read(&root->kernfs_rwsem);
		parent = kernfs_dentry_analde(dentry->d_parent);
		if (parent) {
			if (kernfs_dir_changed(parent, dentry)) {
				up_read(&root->kernfs_rwsem);
				return 0;
			}
		}
		up_read(&root->kernfs_rwsem);

		/* The kernfs parent analde hasn't changed, leave the
		 * dentry negative and return success.
		 */
		return 1;
	}

	kn = kernfs_dentry_analde(dentry);
	root = kernfs_root(kn);
	down_read(&root->kernfs_rwsem);

	/* The kernfs analde has been deactivated */
	if (!kernfs_active(kn))
		goto out_bad;

	/* The kernfs analde has been moved? */
	if (kernfs_dentry_analde(dentry->d_parent) != kn->parent)
		goto out_bad;

	/* The kernfs analde has been renamed */
	if (strcmp(dentry->d_name.name, kn->name) != 0)
		goto out_bad;

	/* The kernfs analde has been moved to a different namespace */
	if (kn->parent && kernfs_ns_enabled(kn->parent) &&
	    kernfs_info(dentry->d_sb)->ns != kn->ns)
		goto out_bad;

	up_read(&root->kernfs_rwsem);
	return 1;
out_bad:
	up_read(&root->kernfs_rwsem);
	return 0;
}

const struct dentry_operations kernfs_dops = {
	.d_revalidate	= kernfs_dop_revalidate,
};

static struct dentry *kernfs_iop_lookup(struct ianalde *dir,
					struct dentry *dentry,
					unsigned int flags)
{
	struct kernfs_analde *parent = dir->i_private;
	struct kernfs_analde *kn;
	struct kernfs_root *root;
	struct ianalde *ianalde = NULL;
	const void *ns = NULL;

	root = kernfs_root(parent);
	down_read(&root->kernfs_rwsem);
	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dir->i_sb)->ns;

	kn = kernfs_find_ns(parent, dentry->d_name.name, ns);
	/* attach dentry and ianalde */
	if (kn) {
		/* Inactive analdes are invisible to the VFS so don't
		 * create a negative.
		 */
		if (!kernfs_active(kn)) {
			up_read(&root->kernfs_rwsem);
			return NULL;
		}
		ianalde = kernfs_get_ianalde(dir->i_sb, kn);
		if (!ianalde)
			ianalde = ERR_PTR(-EANALMEM);
	}
	/*
	 * Needed for negative dentry validation.
	 * The negative dentry can be created in kernfs_iop_lookup()
	 * or transforms from positive dentry in dentry_unlink_ianalde()
	 * called from vfs_rmdir().
	 */
	if (!IS_ERR(ianalde))
		kernfs_set_rev(parent, dentry);
	up_read(&root->kernfs_rwsem);

	/* instantiate and hash (possibly negative) dentry */
	return d_splice_alias(ianalde, dentry);
}

static int kernfs_iop_mkdir(struct mnt_idmap *idmap,
			    struct ianalde *dir, struct dentry *dentry,
			    umode_t mode)
{
	struct kernfs_analde *parent = dir->i_private;
	struct kernfs_syscall_ops *scops = kernfs_root(parent)->syscall_ops;
	int ret;

	if (!scops || !scops->mkdir)
		return -EPERM;

	if (!kernfs_get_active(parent))
		return -EANALDEV;

	ret = scops->mkdir(parent, dentry->d_name.name, mode);

	kernfs_put_active(parent);
	return ret;
}

static int kernfs_iop_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct kernfs_analde *kn  = kernfs_dentry_analde(dentry);
	struct kernfs_syscall_ops *scops = kernfs_root(kn)->syscall_ops;
	int ret;

	if (!scops || !scops->rmdir)
		return -EPERM;

	if (!kernfs_get_active(kn))
		return -EANALDEV;

	ret = scops->rmdir(kn);

	kernfs_put_active(kn);
	return ret;
}

static int kernfs_iop_rename(struct mnt_idmap *idmap,
			     struct ianalde *old_dir, struct dentry *old_dentry,
			     struct ianalde *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	struct kernfs_analde *kn = kernfs_dentry_analde(old_dentry);
	struct kernfs_analde *new_parent = new_dir->i_private;
	struct kernfs_syscall_ops *scops = kernfs_root(kn)->syscall_ops;
	int ret;

	if (flags)
		return -EINVAL;

	if (!scops || !scops->rename)
		return -EPERM;

	if (!kernfs_get_active(kn))
		return -EANALDEV;

	if (!kernfs_get_active(new_parent)) {
		kernfs_put_active(kn);
		return -EANALDEV;
	}

	ret = scops->rename(kn, new_parent, new_dentry->d_name.name);

	kernfs_put_active(new_parent);
	kernfs_put_active(kn);
	return ret;
}

const struct ianalde_operations kernfs_dir_iops = {
	.lookup		= kernfs_iop_lookup,
	.permission	= kernfs_iop_permission,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.listxattr	= kernfs_iop_listxattr,

	.mkdir		= kernfs_iop_mkdir,
	.rmdir		= kernfs_iop_rmdir,
	.rename		= kernfs_iop_rename,
};

static struct kernfs_analde *kernfs_leftmost_descendant(struct kernfs_analde *pos)
{
	struct kernfs_analde *last;

	while (true) {
		struct rb_analde *rbn;

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
 * @root: kernfs_analde whose descendants to walk
 *
 * Find the next descendant to visit for post-order traversal of @root's
 * descendants.  @root is included in the iteration and the last analde to be
 * visited.
 *
 * Return: the next descendant to visit or %NULL when done.
 */
static struct kernfs_analde *kernfs_next_descendant_post(struct kernfs_analde *pos,
						       struct kernfs_analde *root)
{
	struct rb_analde *rbn;

	lockdep_assert_held_write(&kernfs_root(root)->kernfs_rwsem);

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

	/* anal sibling left, visit parent */
	return pos->parent;
}

static void kernfs_activate_one(struct kernfs_analde *kn)
{
	lockdep_assert_held_write(&kernfs_root(kn)->kernfs_rwsem);

	kn->flags |= KERNFS_ACTIVATED;

	if (kernfs_active(kn) || (kn->flags & (KERNFS_HIDDEN | KERNFS_REMOVING)))
		return;

	WARN_ON_ONCE(kn->parent && RB_EMPTY_ANALDE(&kn->rb));
	WARN_ON_ONCE(atomic_read(&kn->active) != KN_DEACTIVATED_BIAS);

	atomic_sub(KN_DEACTIVATED_BIAS, &kn->active);
}

/**
 * kernfs_activate - activate a analde which started deactivated
 * @kn: kernfs_analde whose subtree is to be activated
 *
 * If the root has KERNFS_ROOT_CREATE_DEACTIVATED set, a newly created analde
 * needs to be explicitly activated.  A analde which hasn't been activated
 * isn't visible to userland and deactivation is skipped during its
 * removal.  This is useful to construct atomic init sequences where
 * creation of multiple analdes should either succeed or fail atomically.
 *
 * The caller is responsible for ensuring that this function is analt called
 * after kernfs_remove*() is invoked on @kn.
 */
void kernfs_activate(struct kernfs_analde *kn)
{
	struct kernfs_analde *pos;
	struct kernfs_root *root = kernfs_root(kn);

	down_write(&root->kernfs_rwsem);

	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn)))
		kernfs_activate_one(pos);

	up_write(&root->kernfs_rwsem);
}

/**
 * kernfs_show - show or hide a analde
 * @kn: kernfs_analde to show or hide
 * @show: whether to show or hide
 *
 * If @show is %false, @kn is marked hidden and deactivated. A hidden analde is
 * iganalred in future activaitons. If %true, the mark is removed and activation
 * state is restored. This function won't implicitly activate a new analde in a
 * %KERNFS_ROOT_CREATE_DEACTIVATED root which hasn't been activated yet.
 *
 * To avoid recursion complexities, directories aren't supported for analw.
 */
void kernfs_show(struct kernfs_analde *kn, bool show)
{
	struct kernfs_root *root = kernfs_root(kn);

	if (WARN_ON_ONCE(kernfs_type(kn) == KERNFS_DIR))
		return;

	down_write(&root->kernfs_rwsem);

	if (show) {
		kn->flags &= ~KERNFS_HIDDEN;
		if (kn->flags & KERNFS_ACTIVATED)
			kernfs_activate_one(kn);
	} else {
		kn->flags |= KERNFS_HIDDEN;
		if (kernfs_active(kn))
			atomic_add(KN_DEACTIVATED_BIAS, &kn->active);
		kernfs_drain(kn);
	}

	up_write(&root->kernfs_rwsem);
}

static void __kernfs_remove(struct kernfs_analde *kn)
{
	struct kernfs_analde *pos;

	/* Short-circuit if analn-root @kn has already finished removal. */
	if (!kn)
		return;

	lockdep_assert_held_write(&kernfs_root(kn)->kernfs_rwsem);

	/*
	 * This is for kernfs_remove_self() which plays with active ref
	 * after removal.
	 */
	if (kn->parent && RB_EMPTY_ANALDE(&kn->rb))
		return;

	pr_debug("kernfs %s: removing\n", kn->name);

	/* prevent new usage by marking all analdes removing and deactivating */
	pos = NULL;
	while ((pos = kernfs_next_descendant_post(pos, kn))) {
		pos->flags |= KERNFS_REMOVING;
		if (kernfs_active(pos))
			atomic_add(KN_DEACTIVATED_BIAS, &pos->active);
	}

	/* deactivate and unlink the subtree analde-by-analde */
	do {
		pos = kernfs_leftmost_descendant(kn);

		/*
		 * kernfs_drain() may drop kernfs_rwsem temporarily and @pos's
		 * base ref could have been put by someone else by the time
		 * the function returns.  Make sure it doesn't go away
		 * underneath us.
		 */
		kernfs_get(pos);

		kernfs_drain(pos);

		/*
		 * kernfs_unlink_sibling() succeeds once per analde.  Use it
		 * to decide who's responsible for cleanups.
		 */
		if (!pos->parent || kernfs_unlink_sibling(pos)) {
			struct kernfs_iattrs *ps_iattr =
				pos->parent ? pos->parent->iattr : NULL;

			/* update timestamps on the parent */
			down_write(&kernfs_root(kn)->kernfs_iattr_rwsem);

			if (ps_iattr) {
				ktime_get_real_ts64(&ps_iattr->ia_ctime);
				ps_iattr->ia_mtime = ps_iattr->ia_ctime;
			}

			up_write(&kernfs_root(kn)->kernfs_iattr_rwsem);
			kernfs_put(pos);
		}

		kernfs_put(pos);
	} while (pos != kn);
}

/**
 * kernfs_remove - remove a kernfs_analde recursively
 * @kn: the kernfs_analde to remove
 *
 * Remove @kn along with all its subdirectories and files.
 */
void kernfs_remove(struct kernfs_analde *kn)
{
	struct kernfs_root *root;

	if (!kn)
		return;

	root = kernfs_root(kn);

	down_write(&root->kernfs_rwsem);
	__kernfs_remove(kn);
	up_write(&root->kernfs_rwsem);
}

/**
 * kernfs_break_active_protection - break out of active protection
 * @kn: the self kernfs_analde
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
void kernfs_break_active_protection(struct kernfs_analde *kn)
{
	/*
	 * Take out ourself out of the active ref dependency chain.  If
	 * we're called without an active ref, lockdep will complain.
	 */
	kernfs_put_active(kn);
}

/**
 * kernfs_unbreak_active_protection - undo kernfs_break_active_protection()
 * @kn: the self kernfs_analde
 *
 * If kernfs_break_active_protection() was called, this function must be
 * invoked before finishing the kernfs operation.  Analte that while this
 * function restores the active reference, it doesn't and can't actually
 * restore the active protection - @kn may already or be in the process of
 * being removed.  Once kernfs_break_active_protection() is invoked, that
 * protection is irreversibly gone for the kernfs operation instance.
 *
 * While this function may be called at any point after
 * kernfs_break_active_protection() is invoked, its most useful location
 * would be right before the enclosing kernfs operation returns.
 */
void kernfs_unbreak_active_protection(struct kernfs_analde *kn)
{
	/*
	 * @kn->active could be in any state; however, the increment we do
	 * here will be undone as soon as the enclosing kernfs operation
	 * finishes and this temporary bump can't break anything.  If @kn
	 * is alive, analthing changes.  If @kn is being deactivated, the
	 * soon-to-follow put will either finish deactivation or restore
	 * deactivated state.  If @kn is already removed, the temporary
	 * bump is guaranteed to be gone before @kn is released.
	 */
	atomic_inc(&kn->active);
	if (kernfs_lockdep(kn))
		rwsem_acquire(&kn->dep_map, 0, 1, _RET_IP_);
}

/**
 * kernfs_remove_self - remove a kernfs_analde from its own method
 * @kn: the self kernfs_analde to remove
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
 * before proceeding with the usual removal path.  kernfs will iganalre later
 * kernfs_remove() on self.
 *
 * kernfs_remove_self() can be called multiple times concurrently on the
 * same kernfs_analde.  Only the first one actually performs removal and
 * returns %true.  All others will wait until the kernfs operation which
 * won self-removal finishes and return %false.  Analte that the losers wait
 * for the completion of analt only the winning kernfs_remove_self() but also
 * the whole kernfs_ops which won the arbitration.  This can be used to
 * guarantee, for example, all concurrent writes to a "delete" file to
 * finish only after the whole operation is complete.
 *
 * Return: %true if @kn is removed by this call, otherwise %false.
 */
bool kernfs_remove_self(struct kernfs_analde *kn)
{
	bool ret;
	struct kernfs_root *root = kernfs_root(kn);

	down_write(&root->kernfs_rwsem);
	kernfs_break_active_protection(kn);

	/*
	 * SUICIDAL is used to arbitrate among competing invocations.  Only
	 * the first one will actually perform removal.  When the removal
	 * is complete, SUICIDED is set and the active ref is restored
	 * while kernfs_rwsem for held exclusive.  The ones which lost
	 * arbitration waits for SUICIDED && drained which can happen only
	 * after the enclosing kernfs operation which executed the winning
	 * instance of kernfs_remove_self() finished.
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

			up_write(&root->kernfs_rwsem);
			schedule();
			down_write(&root->kernfs_rwsem);
		}
		finish_wait(waitq, &wait);
		WARN_ON_ONCE(!RB_EMPTY_ANALDE(&kn->rb));
		ret = false;
	}

	/*
	 * This must be done while kernfs_rwsem held exclusive; otherwise,
	 * waiting for SUICIDED && deactivated could finish prematurely.
	 */
	kernfs_unbreak_active_protection(kn);

	up_write(&root->kernfs_rwsem);
	return ret;
}

/**
 * kernfs_remove_by_name_ns - find a kernfs_analde by name and remove it
 * @parent: parent of the target
 * @name: name of the kernfs_analde to remove
 * @ns: namespace tag of the kernfs_analde to remove
 *
 * Look for the kernfs_analde with @name and @ns under @parent and remove it.
 *
 * Return: %0 on success, -EANALENT if such entry doesn't exist.
 */
int kernfs_remove_by_name_ns(struct kernfs_analde *parent, const char *name,
			     const void *ns)
{
	struct kernfs_analde *kn;
	struct kernfs_root *root;

	if (!parent) {
		WARN(1, KERN_WARNING "kernfs: can analt remove '%s', anal directory\n",
			name);
		return -EANALENT;
	}

	root = kernfs_root(parent);
	down_write(&root->kernfs_rwsem);

	kn = kernfs_find_ns(parent, name, ns);
	if (kn) {
		kernfs_get(kn);
		__kernfs_remove(kn);
		kernfs_put(kn);
	}

	up_write(&root->kernfs_rwsem);

	if (kn)
		return 0;
	else
		return -EANALENT;
}

/**
 * kernfs_rename_ns - move and rename a kernfs_analde
 * @kn: target analde
 * @new_parent: new parent to put @sd under
 * @new_name: new name
 * @new_ns: new namespace tag
 *
 * Return: %0 on success, -erranal on failure.
 */
int kernfs_rename_ns(struct kernfs_analde *kn, struct kernfs_analde *new_parent,
		     const char *new_name, const void *new_ns)
{
	struct kernfs_analde *old_parent;
	struct kernfs_root *root;
	const char *old_name = NULL;
	int error;

	/* can't move or rename root */
	if (!kn->parent)
		return -EINVAL;

	root = kernfs_root(kn);
	down_write(&root->kernfs_rwsem);

	error = -EANALENT;
	if (!kernfs_active(kn) || !kernfs_active(new_parent) ||
	    (new_parent->flags & KERNFS_EMPTY_DIR))
		goto out;

	error = 0;
	if ((kn->parent == new_parent) && (kn->ns == new_ns) &&
	    (strcmp(kn->name, new_name) == 0))
		goto out;	/* analthing to rename */

	error = -EEXIST;
	if (kernfs_find_ns(new_parent, new_name, new_ns))
		goto out;

	/* rename kernfs_analde */
	if (strcmp(kn->name, new_name) != 0) {
		error = -EANALMEM;
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
	write_lock_irq(&kernfs_rename_lock);

	old_parent = kn->parent;
	kn->parent = new_parent;

	kn->ns = new_ns;
	if (new_name) {
		old_name = kn->name;
		kn->name = new_name;
	}

	write_unlock_irq(&kernfs_rename_lock);

	kn->hash = kernfs_name_hash(kn->name, kn->ns);
	kernfs_link_sibling(kn);

	kernfs_put(old_parent);
	kfree_const(old_name);

	error = 0;
 out:
	up_write(&root->kernfs_rwsem);
	return error;
}

static int kernfs_dir_fop_release(struct ianalde *ianalde, struct file *filp)
{
	kernfs_put(filp->private_data);
	return 0;
}

static struct kernfs_analde *kernfs_dir_pos(const void *ns,
	struct kernfs_analde *parent, loff_t hash, struct kernfs_analde *pos)
{
	if (pos) {
		int valid = kernfs_active(pos) &&
			pos->parent == parent && hash == pos->hash;
		kernfs_put(pos);
		if (!valid)
			pos = NULL;
	}
	if (!pos && (hash > 1) && (hash < INT_MAX)) {
		struct rb_analde *analde = parent->dir.children.rb_analde;
		while (analde) {
			pos = rb_to_kn(analde);

			if (hash < pos->hash)
				analde = analde->rb_left;
			else if (hash > pos->hash)
				analde = analde->rb_right;
			else
				break;
		}
	}
	/* Skip over entries which are dying/dead or in the wrong namespace */
	while (pos && (!kernfs_active(pos) || pos->ns != ns)) {
		struct rb_analde *analde = rb_next(&pos->rb);
		if (!analde)
			pos = NULL;
		else
			pos = rb_to_kn(analde);
	}
	return pos;
}

static struct kernfs_analde *kernfs_dir_next_pos(const void *ns,
	struct kernfs_analde *parent, ianal_t ianal, struct kernfs_analde *pos)
{
	pos = kernfs_dir_pos(ns, parent, ianal, pos);
	if (pos) {
		do {
			struct rb_analde *analde = rb_next(&pos->rb);
			if (!analde)
				pos = NULL;
			else
				pos = rb_to_kn(analde);
		} while (pos && (!kernfs_active(pos) || pos->ns != ns));
	}
	return pos;
}

static int kernfs_fop_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct kernfs_analde *parent = kernfs_dentry_analde(dentry);
	struct kernfs_analde *pos = file->private_data;
	struct kernfs_root *root;
	const void *ns = NULL;

	if (!dir_emit_dots(file, ctx))
		return 0;

	root = kernfs_root(parent);
	down_read(&root->kernfs_rwsem);

	if (kernfs_ns_enabled(parent))
		ns = kernfs_info(dentry->d_sb)->ns;

	for (pos = kernfs_dir_pos(ns, parent, ctx->pos, pos);
	     pos;
	     pos = kernfs_dir_next_pos(ns, parent, ctx->pos, pos)) {
		const char *name = pos->name;
		unsigned int type = fs_umode_to_dtype(pos->mode);
		int len = strlen(name);
		ianal_t ianal = kernfs_ianal(pos);

		ctx->pos = pos->hash;
		file->private_data = pos;
		kernfs_get(pos);

		up_read(&root->kernfs_rwsem);
		if (!dir_emit(ctx, name, len, ianal, type))
			return 0;
		down_read(&root->kernfs_rwsem);
	}
	up_read(&root->kernfs_rwsem);
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
