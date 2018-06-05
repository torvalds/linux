/*
 * AppArmor security module
 *
 * This file contains AppArmor label definitions
 *
 * Copyright 2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/audit.h>
#include <linux/seq_file.h>
#include <linux/sort.h>

#include "include/apparmor.h"
#include "include/cred.h"
#include "include/label.h"
#include "include/policy.h"
#include "include/secid.h"


/*
 * the aa_label represents the set of profiles confining an object
 *
 * Labels maintain a reference count to the set of pointers they reference
 * Labels are ref counted by
 *   tasks and object via the security field/security context off the field
 *   code - will take a ref count on a label if it needs the label
 *          beyond what is possible with an rcu_read_lock.
 *   profiles - each profile is a label
 *   secids - a pinned secid will keep a refcount of the label it is
 *          referencing
 *   objects - inode, files, sockets, ...
 *
 * Labels are not ref counted by the label set, so they maybe removed and
 * freed when no longer in use.
 *
 */

#define PROXY_POISON 97
#define LABEL_POISON 100

static void free_proxy(struct aa_proxy *proxy)
{
	if (proxy) {
		/* p->label will not updated any more as p is dead */
		aa_put_label(rcu_dereference_protected(proxy->label, true));
		memset(proxy, 0, sizeof(*proxy));
		RCU_INIT_POINTER(proxy->label, (struct aa_label *)PROXY_POISON);
		kfree(proxy);
	}
}

void aa_proxy_kref(struct kref *kref)
{
	struct aa_proxy *proxy = container_of(kref, struct aa_proxy, count);

	free_proxy(proxy);
}

struct aa_proxy *aa_alloc_proxy(struct aa_label *label, gfp_t gfp)
{
	struct aa_proxy *new;

	new = kzalloc(sizeof(struct aa_proxy), gfp);
	if (new) {
		kref_init(&new->count);
		rcu_assign_pointer(new->label, aa_get_label(label));
	}
	return new;
}

/* requires profile list write lock held */
void __aa_proxy_redirect(struct aa_label *orig, struct aa_label *new)
{
	struct aa_label *tmp;

	AA_BUG(!orig);
	AA_BUG(!new);
	lockdep_assert_held_exclusive(&labels_set(orig)->lock);

	tmp = rcu_dereference_protected(orig->proxy->label,
					&labels_ns(orig)->lock);
	rcu_assign_pointer(orig->proxy->label, aa_get_label(new));
	orig->flags |= FLAG_STALE;
	aa_put_label(tmp);
}

static void __proxy_share(struct aa_label *old, struct aa_label *new)
{
	struct aa_proxy *proxy = new->proxy;

	new->proxy = aa_get_proxy(old->proxy);
	__aa_proxy_redirect(old, new);
	aa_put_proxy(proxy);
}


/**
 * ns_cmp - compare ns for label set ordering
 * @a: ns to compare (NOT NULL)
 * @b: ns to compare (NOT NULL)
 *
 * Returns: <0 if a < b
 *          ==0 if a == b
 *          >0  if a > b
 */
static int ns_cmp(struct aa_ns *a, struct aa_ns *b)
{
	int res;

	AA_BUG(!a);
	AA_BUG(!b);
	AA_BUG(!a->base.hname);
	AA_BUG(!b->base.hname);

	if (a == b)
		return 0;

	res = a->level - b->level;
	if (res)
		return res;

	return strcmp(a->base.hname, b->base.hname);
}

/**
 * profile_cmp - profile comparison for set ordering
 * @a: profile to compare (NOT NULL)
 * @b: profile to compare (NOT NULL)
 *
 * Returns: <0  if a < b
 *          ==0 if a == b
 *          >0  if a > b
 */
static int profile_cmp(struct aa_profile *a, struct aa_profile *b)
{
	int res;

	AA_BUG(!a);
	AA_BUG(!b);
	AA_BUG(!a->ns);
	AA_BUG(!b->ns);
	AA_BUG(!a->base.hname);
	AA_BUG(!b->base.hname);

	if (a == b || a->base.hname == b->base.hname)
		return 0;
	res = ns_cmp(a->ns, b->ns);
	if (res)
		return res;

	return strcmp(a->base.hname, b->base.hname);
}

/**
 * vec_cmp - label comparison for set ordering
 * @a: label to compare (NOT NULL)
 * @vec: vector of profiles to compare (NOT NULL)
 * @n: length of @vec
 *
 * Returns: <0  if a < vec
 *          ==0 if a == vec
 *          >0  if a > vec
 */
static int vec_cmp(struct aa_profile **a, int an, struct aa_profile **b, int bn)
{
	int i;

	AA_BUG(!a);
	AA_BUG(!*a);
	AA_BUG(!b);
	AA_BUG(!*b);
	AA_BUG(an <= 0);
	AA_BUG(bn <= 0);

	for (i = 0; i < an && i < bn; i++) {
		int res = profile_cmp(a[i], b[i]);

		if (res != 0)
			return res;
	}

	return an - bn;
}

static bool vec_is_stale(struct aa_profile **vec, int n)
{
	int i;

	AA_BUG(!vec);

	for (i = 0; i < n; i++) {
		if (profile_is_stale(vec[i]))
			return true;
	}

	return false;
}

static bool vec_unconfined(struct aa_profile **vec, int n)
{
	int i;

	AA_BUG(!vec);

	for (i = 0; i < n; i++) {
		if (!profile_unconfined(vec[i]))
			return false;
	}

	return true;
}

static int sort_cmp(const void *a, const void *b)
{
	return profile_cmp(*(struct aa_profile **)a, *(struct aa_profile **)b);
}

/*
 * assumes vec is sorted
 * Assumes @vec has null terminator at vec[n], and will null terminate
 * vec[n - dups]
 */
static inline int unique(struct aa_profile **vec, int n)
{
	int i, pos, dups = 0;

	AA_BUG(n < 1);
	AA_BUG(!vec);

	pos = 0;
	for (i = 1; i < n; i++) {
		int res = profile_cmp(vec[pos], vec[i]);

		AA_BUG(res > 0, "vec not sorted");
		if (res == 0) {
			/* drop duplicate */
			aa_put_profile(vec[i]);
			dups++;
			continue;
		}
		pos++;
		if (dups)
			vec[pos] = vec[i];
	}

	AA_BUG(dups < 0);

	return dups;
}

/**
 * aa_vec_unique - canonical sort and unique a list of profiles
 * @n: number of refcounted profiles in the list (@n > 0)
 * @vec: list of profiles to sort and merge
 *
 * Returns: the number of duplicates eliminated == references put
 *
 * If @flags & VEC_FLAG_TERMINATE @vec has null terminator at vec[n], and will
 * null terminate vec[n - dups]
 */
int aa_vec_unique(struct aa_profile **vec, int n, int flags)
{
	int i, dups = 0;

	AA_BUG(n < 1);
	AA_BUG(!vec);

	/* vecs are usually small and inorder, have a fallback for larger */
	if (n > 8) {
		sort(vec, n, sizeof(struct aa_profile *), sort_cmp, NULL);
		dups = unique(vec, n);
		goto out;
	}

	/* insertion sort + unique in one */
	for (i = 1; i < n; i++) {
		struct aa_profile *tmp = vec[i];
		int pos, j;

		for (pos = i - 1 - dups; pos >= 0; pos--) {
			int res = profile_cmp(vec[pos], tmp);

			if (res == 0) {
				/* drop duplicate entry */
				aa_put_profile(tmp);
				dups++;
				goto continue_outer;
			} else if (res < 0)
				break;
		}
		/* pos is at entry < tmp, or index -1. Set to insert pos */
		pos++;

		for (j = i - dups; j > pos; j--)
			vec[j] = vec[j - 1];
		vec[pos] = tmp;
continue_outer:
		;
	}

	AA_BUG(dups < 0);

out:
	if (flags & VEC_FLAG_TERMINATE)
		vec[n - dups] = NULL;

	return dups;
}


static void label_destroy(struct aa_label *label)
{
	struct aa_label *tmp;

	AA_BUG(!label);

	if (!label_isprofile(label)) {
		struct aa_profile *profile;
		struct label_it i;

		aa_put_str(label->hname);

		label_for_each(i, label, profile) {
			aa_put_profile(profile);
			label->vec[i.i] = (struct aa_profile *)
					   (LABEL_POISON + (long) i.i);
		}
	}

	if (rcu_dereference_protected(label->proxy->label, true) == label)
		rcu_assign_pointer(label->proxy->label, NULL);

	aa_free_secid(label->secid);

	tmp = rcu_dereference_protected(label->proxy->label, true);
	if (tmp == label)
		rcu_assign_pointer(label->proxy->label, NULL);

	aa_put_proxy(label->proxy);
	label->proxy = (struct aa_proxy *) PROXY_POISON + 1;
}

void aa_label_free(struct aa_label *label)
{
	if (!label)
		return;

	label_destroy(label);
	kfree(label);
}

static void label_free_switch(struct aa_label *label)
{
	if (label->flags & FLAG_NS_COUNT)
		aa_free_ns(labels_ns(label));
	else if (label_isprofile(label))
		aa_free_profile(labels_profile(label));
	else
		aa_label_free(label);
}

static void label_free_rcu(struct rcu_head *head)
{
	struct aa_label *label = container_of(head, struct aa_label, rcu);

	if (label->flags & FLAG_IN_TREE)
		(void) aa_label_remove(label);
	label_free_switch(label);
}

void aa_label_kref(struct kref *kref)
{
	struct aa_label *label = container_of(kref, struct aa_label, count);
	struct aa_ns *ns = labels_ns(label);

	if (!ns) {
		/* never live, no rcu callback needed, just using the fn */
		label_free_switch(label);
		return;
	}
	/* TODO: update labels_profile macro so it works here */
	AA_BUG(label_isprofile(label) &&
	       on_list_rcu(&label->vec[0]->base.profiles));
	AA_BUG(label_isprofile(label) &&
	       on_list_rcu(&label->vec[0]->base.list));

	/* TODO: if compound label and not stale add to reclaim cache */
	call_rcu(&label->rcu, label_free_rcu);
}

static void label_free_or_put_new(struct aa_label *label, struct aa_label *new)
{
	if (label != new)
		/* need to free directly to break circular ref with proxy */
		aa_label_free(new);
	else
		aa_put_label(new);
}

bool aa_label_init(struct aa_label *label, int size, gfp_t gfp)
{
	AA_BUG(!label);
	AA_BUG(size < 1);

	if (aa_alloc_secid(label, gfp) < 0)
		return false;

	label->size = size;			/* doesn't include null */
	label->vec[size] = NULL;		/* null terminate */
	kref_init(&label->count);
	RB_CLEAR_NODE(&label->node);

	return true;
}

/**
 * aa_label_alloc - allocate a label with a profile vector of @size length
 * @size: size of profile vector in the label
 * @proxy: proxy to use OR null if to allocate a new one
 * @gfp: memory allocation type
 *
 * Returns: new label
 *     else NULL if failed
 */
struct aa_label *aa_label_alloc(int size, struct aa_proxy *proxy, gfp_t gfp)
{
	struct aa_label *new;

	AA_BUG(size < 1);

	/*  + 1 for null terminator entry on vec */
	new = kzalloc(sizeof(*new) + sizeof(struct aa_profile *) * (size + 1),
			gfp);
	AA_DEBUG("%s (%p)\n", __func__, new);
	if (!new)
		goto fail;

	if (!aa_label_init(new, size, gfp))
		goto fail;

	if (!proxy) {
		proxy = aa_alloc_proxy(new, gfp);
		if (!proxy)
			goto fail;
	} else
		aa_get_proxy(proxy);
	/* just set new's proxy, don't redirect proxy here if it was passed in*/
	new->proxy = proxy;

	return new;

fail:
	kfree(new);

	return NULL;
}


/**
 * label_cmp - label comparison for set ordering
 * @a: label to compare (NOT NULL)
 * @b: label to compare (NOT NULL)
 *
 * Returns: <0  if a < b
 *          ==0 if a == b
 *          >0  if a > b
 */
static int label_cmp(struct aa_label *a, struct aa_label *b)
{
	AA_BUG(!b);

	if (a == b)
		return 0;

	return vec_cmp(a->vec, a->size, b->vec, b->size);
}

/* helper fn for label_for_each_confined */
int aa_label_next_confined(struct aa_label *label, int i)
{
	AA_BUG(!label);
	AA_BUG(i < 0);

	for (; i < label->size; i++) {
		if (!profile_unconfined(label->vec[i]))
			return i;
	}

	return i;
}

/**
 * aa_label_next_not_in_set - return the next profile of @sub not in @set
 * @I: label iterator
 * @set: label to test against
 * @sub: label to if is subset of @set
 *
 * Returns: profile in @sub that is not in @set, with iterator set pos after
 *     else NULL if @sub is a subset of @set
 */
struct aa_profile *__aa_label_next_not_in_set(struct label_it *I,
					      struct aa_label *set,
					      struct aa_label *sub)
{
	AA_BUG(!set);
	AA_BUG(!I);
	AA_BUG(I->i < 0);
	AA_BUG(I->i > set->size);
	AA_BUG(!sub);
	AA_BUG(I->j < 0);
	AA_BUG(I->j > sub->size);

	while (I->j < sub->size && I->i < set->size) {
		int res = profile_cmp(sub->vec[I->j], set->vec[I->i]);

		if (res == 0) {
			(I->j)++;
			(I->i)++;
		} else if (res > 0)
			(I->i)++;
		else
			return sub->vec[(I->j)++];
	}

	if (I->j < sub->size)
		return sub->vec[(I->j)++];

	return NULL;
}

/**
 * aa_label_is_subset - test if @sub is a subset of @set
 * @set: label to test against
 * @sub: label to test if is subset of @set
 *
 * Returns: true if @sub is subset of @set
 *     else false
 */
bool aa_label_is_subset(struct aa_label *set, struct aa_label *sub)
{
	struct label_it i = { };

	AA_BUG(!set);
	AA_BUG(!sub);

	if (sub == set)
		return true;

	return __aa_label_next_not_in_set(&i, set, sub) == NULL;
}



/**
 * __label_remove - remove @label from the label set
 * @l: label to remove
 * @new: label to redirect to
 *
 * Requires: labels_set(@label)->lock write_lock
 * Returns:  true if the label was in the tree and removed
 */
static bool __label_remove(struct aa_label *label, struct aa_label *new)
{
	struct aa_labelset *ls = labels_set(label);

	AA_BUG(!ls);
	AA_BUG(!label);
	lockdep_assert_held_exclusive(&ls->lock);

	if (new)
		__aa_proxy_redirect(label, new);

	if (!label_is_stale(label))
		__label_make_stale(label);

	if (label->flags & FLAG_IN_TREE) {
		rb_erase(&label->node, &ls->root);
		label->flags &= ~FLAG_IN_TREE;
		return true;
	}

	return false;
}

/**
 * __label_replace - replace @old with @new in label set
 * @old: label to remove from label set
 * @new: label to replace @old with
 *
 * Requires: labels_set(@old)->lock write_lock
 *           valid ref count be held on @new
 * Returns: true if @old was in set and replaced by @new
 *
 * Note: current implementation requires label set be order in such a way
 *       that @new directly replaces @old position in the set (ie.
 *       using pointer comparison of the label address would not work)
 */
static bool __label_replace(struct aa_label *old, struct aa_label *new)
{
	struct aa_labelset *ls = labels_set(old);

	AA_BUG(!ls);
	AA_BUG(!old);
	AA_BUG(!new);
	lockdep_assert_held_exclusive(&ls->lock);
	AA_BUG(new->flags & FLAG_IN_TREE);

	if (!label_is_stale(old))
		__label_make_stale(old);

	if (old->flags & FLAG_IN_TREE) {
		rb_replace_node(&old->node, &new->node, &ls->root);
		old->flags &= ~FLAG_IN_TREE;
		new->flags |= FLAG_IN_TREE;
		return true;
	}

	return false;
}

/**
 * __label_insert - attempt to insert @l into a label set
 * @ls: set of labels to insert @l into (NOT NULL)
 * @label: new label to insert (NOT NULL)
 * @replace: whether insertion should replace existing entry that is not stale
 *
 * Requires: @ls->lock
 *           caller to hold a valid ref on l
 *           if @replace is true l has a preallocated proxy associated
 * Returns: @l if successful in inserting @l - with additional refcount
 *          else ref counted equivalent label that is already in the set,
 *          the else condition only happens if @replace is false
 */
static struct aa_label *__label_insert(struct aa_labelset *ls,
				       struct aa_label *label, bool replace)
{
	struct rb_node **new, *parent = NULL;

	AA_BUG(!ls);
	AA_BUG(!label);
	AA_BUG(labels_set(label) != ls);
	lockdep_assert_held_exclusive(&ls->lock);
	AA_BUG(label->flags & FLAG_IN_TREE);

	/* Figure out where to put new node */
	new = &ls->root.rb_node;
	while (*new) {
		struct aa_label *this = rb_entry(*new, struct aa_label, node);
		int result = label_cmp(label, this);

		parent = *new;
		if (result == 0) {
			/* !__aa_get_label means queued for destruction,
			 * so replace in place, however the label has
			 * died before the replacement so do not share
			 * the proxy
			 */
			if (!replace && !label_is_stale(this)) {
				if (__aa_get_label(this))
					return this;
			} else
				__proxy_share(this, label);
			AA_BUG(!__label_replace(this, label));
			return aa_get_label(label);
		} else if (result < 0)
			new = &((*new)->rb_left);
		else /* (result > 0) */
			new = &((*new)->rb_right);
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&label->node, parent, new);
	rb_insert_color(&label->node, &ls->root);
	label->flags |= FLAG_IN_TREE;

	return aa_get_label(label);
}

/**
 * __vec_find - find label that matches @vec in label set
 * @vec: vec of profiles to find matching label for (NOT NULL)
 * @n: length of @vec
 *
 * Requires: @vec_labelset(vec) lock held
 *           caller to hold a valid ref on l
 *
 * Returns: ref counted @label if matching label is in tree
 *          ref counted label that is equiv to @l in tree
 *     else NULL if @vec equiv is not in tree
 */
static struct aa_label *__vec_find(struct aa_profile **vec, int n)
{
	struct rb_node *node;

	AA_BUG(!vec);
	AA_BUG(!*vec);
	AA_BUG(n <= 0);

	node = vec_labelset(vec, n)->root.rb_node;
	while (node) {
		struct aa_label *this = rb_entry(node, struct aa_label, node);
		int result = vec_cmp(this->vec, this->size, vec, n);

		if (result > 0)
			node = node->rb_left;
		else if (result < 0)
			node = node->rb_right;
		else
			return __aa_get_label(this);
	}

	return NULL;
}

/**
 * __label_find - find label @label in label set
 * @label: label to find (NOT NULL)
 *
 * Requires: labels_set(@label)->lock held
 *           caller to hold a valid ref on l
 *
 * Returns: ref counted @label if @label is in tree OR
 *          ref counted label that is equiv to @label in tree
 *     else NULL if @label or equiv is not in tree
 */
static struct aa_label *__label_find(struct aa_label *label)
{
	AA_BUG(!label);

	return __vec_find(label->vec, label->size);
}


/**
 * aa_label_remove - remove a label from the labelset
 * @label: label to remove
 *
 * Returns: true if @label was removed from the tree
 *     else @label was not in tree so it could not be removed
 */
bool aa_label_remove(struct aa_label *label)
{
	struct aa_labelset *ls = labels_set(label);
	unsigned long flags;
	bool res;

	AA_BUG(!ls);

	write_lock_irqsave(&ls->lock, flags);
	res = __label_remove(label, ns_unconfined(labels_ns(label)));
	write_unlock_irqrestore(&ls->lock, flags);

	return res;
}

/**
 * aa_label_replace - replace a label @old with a new version @new
 * @old: label to replace
 * @new: label replacing @old
 *
 * Returns: true if @old was in tree and replaced
 *     else @old was not in tree, and @new was not inserted
 */
bool aa_label_replace(struct aa_label *old, struct aa_label *new)
{
	unsigned long flags;
	bool res;

	if (name_is_shared(old, new) && labels_ns(old) == labels_ns(new)) {
		write_lock_irqsave(&labels_set(old)->lock, flags);
		if (old->proxy != new->proxy)
			__proxy_share(old, new);
		else
			__aa_proxy_redirect(old, new);
		res = __label_replace(old, new);
		write_unlock_irqrestore(&labels_set(old)->lock, flags);
	} else {
		struct aa_label *l;
		struct aa_labelset *ls = labels_set(old);

		write_lock_irqsave(&ls->lock, flags);
		res = __label_remove(old, new);
		if (labels_ns(old) != labels_ns(new)) {
			write_unlock_irqrestore(&ls->lock, flags);
			ls = labels_set(new);
			write_lock_irqsave(&ls->lock, flags);
		}
		l = __label_insert(ls, new, true);
		res = (l == new);
		write_unlock_irqrestore(&ls->lock, flags);
		aa_put_label(l);
	}

	return res;
}

/**
 * vec_find - find label @l in label set
 * @vec: array of profiles to find equiv label for (NOT NULL)
 * @n: length of @vec
 *
 * Returns: refcounted label if @vec equiv is in tree
 *     else NULL if @vec equiv is not in tree
 */
static struct aa_label *vec_find(struct aa_profile **vec, int n)
{
	struct aa_labelset *ls;
	struct aa_label *label;
	unsigned long flags;

	AA_BUG(!vec);
	AA_BUG(!*vec);
	AA_BUG(n <= 0);

	ls = vec_labelset(vec, n);
	read_lock_irqsave(&ls->lock, flags);
	label = __vec_find(vec, n);
	read_unlock_irqrestore(&ls->lock, flags);

	return label;
}

/* requires sort and merge done first */
static struct aa_label *vec_create_and_insert_label(struct aa_profile **vec,
						    int len, gfp_t gfp)
{
	struct aa_label *label = NULL;
	struct aa_labelset *ls;
	unsigned long flags;
	struct aa_label *new;
	int i;

	AA_BUG(!vec);

	if (len == 1)
		return aa_get_label(&vec[0]->label);

	ls = labels_set(&vec[len - 1]->label);

	/* TODO: enable when read side is lockless
	 * check if label exists before taking locks
	 */
	new = aa_label_alloc(len, NULL, gfp);
	if (!new)
		return NULL;

	for (i = 0; i < len; i++)
		new->vec[i] = aa_get_profile(vec[i]);

	write_lock_irqsave(&ls->lock, flags);
	label = __label_insert(ls, new, false);
	write_unlock_irqrestore(&ls->lock, flags);
	label_free_or_put_new(label, new);

	return label;
}

struct aa_label *aa_vec_find_or_create_label(struct aa_profile **vec, int len,
					     gfp_t gfp)
{
	struct aa_label *label = vec_find(vec, len);

	if (label)
		return label;

	return vec_create_and_insert_label(vec, len, gfp);
}

/**
 * aa_label_find - find label @label in label set
 * @label: label to find (NOT NULL)
 *
 * Requires: caller to hold a valid ref on l
 *
 * Returns: refcounted @label if @label is in tree
 *          refcounted label that is equiv to @label in tree
 *     else NULL if @label or equiv is not in tree
 */
struct aa_label *aa_label_find(struct aa_label *label)
{
	AA_BUG(!label);

	return vec_find(label->vec, label->size);
}


/**
 * aa_label_insert - insert label @label into @ls or return existing label
 * @ls - labelset to insert @label into
 * @label - label to insert
 *
 * Requires: caller to hold a valid ref on @label
 *
 * Returns: ref counted @label if successful in inserting @label
 *     else ref counted equivalent label that is already in the set
 */
struct aa_label *aa_label_insert(struct aa_labelset *ls, struct aa_label *label)
{
	struct aa_label *l;
	unsigned long flags;

	AA_BUG(!ls);
	AA_BUG(!label);

	/* check if label exists before taking lock */
	if (!label_is_stale(label)) {
		read_lock_irqsave(&ls->lock, flags);
		l = __label_find(label);
		read_unlock_irqrestore(&ls->lock, flags);
		if (l)
			return l;
	}

	write_lock_irqsave(&ls->lock, flags);
	l = __label_insert(ls, label, false);
	write_unlock_irqrestore(&ls->lock, flags);

	return l;
}


/**
 * aa_label_next_in_merge - find the next profile when merging @a and @b
 * @I: label iterator
 * @a: label to merge
 * @b: label to merge
 *
 * Returns: next profile
 *     else null if no more profiles
 */
struct aa_profile *aa_label_next_in_merge(struct label_it *I,
					  struct aa_label *a,
					  struct aa_label *b)
{
	AA_BUG(!a);
	AA_BUG(!b);
	AA_BUG(!I);
	AA_BUG(I->i < 0);
	AA_BUG(I->i > a->size);
	AA_BUG(I->j < 0);
	AA_BUG(I->j > b->size);

	if (I->i < a->size) {
		if (I->j < b->size) {
			int res = profile_cmp(a->vec[I->i], b->vec[I->j]);

			if (res > 0)
				return b->vec[(I->j)++];
			if (res == 0)
				(I->j)++;
		}

		return a->vec[(I->i)++];
	}

	if (I->j < b->size)
		return b->vec[(I->j)++];

	return NULL;
}

/**
 * label_merge_cmp - cmp of @a merging with @b against @z for set ordering
 * @a: label to merge then compare (NOT NULL)
 * @b: label to merge then compare (NOT NULL)
 * @z: label to compare merge against (NOT NULL)
 *
 * Assumes: using the most recent versions of @a, @b, and @z
 *
 * Returns: <0  if a < b
 *          ==0 if a == b
 *          >0  if a > b
 */
static int label_merge_cmp(struct aa_label *a, struct aa_label *b,
			   struct aa_label *z)
{
	struct aa_profile *p = NULL;
	struct label_it i = { };
	int k;

	AA_BUG(!a);
	AA_BUG(!b);
	AA_BUG(!z);

	for (k = 0;
	     k < z->size && (p = aa_label_next_in_merge(&i, a, b));
	     k++) {
		int res = profile_cmp(p, z->vec[k]);

		if (res != 0)
			return res;
	}

	if (p)
		return 1;
	else if (k < z->size)
		return -1;
	return 0;
}

/**
 * label_merge_insert - create a new label by merging @a and @b
 * @new: preallocated label to merge into (NOT NULL)
 * @a: label to merge with @b  (NOT NULL)
 * @b: label to merge with @a  (NOT NULL)
 *
 * Requires: preallocated proxy
 *
 * Returns: ref counted label either @new if merge is unique
 *          @a if @b is a subset of @a
 *          @b if @a is a subset of @b
 *
 * NOTE: will not use @new if the merge results in @new == @a or @b
 *
 *       Must be used within labelset write lock to avoid racing with
 *       setting labels stale.
 */
static struct aa_label *label_merge_insert(struct aa_label *new,
					   struct aa_label *a,
					   struct aa_label *b)
{
	struct aa_label *label;
	struct aa_labelset *ls;
	struct aa_profile *next;
	struct label_it i;
	unsigned long flags;
	int k = 0, invcount = 0;
	bool stale = false;

	AA_BUG(!a);
	AA_BUG(a->size < 0);
	AA_BUG(!b);
	AA_BUG(b->size < 0);
	AA_BUG(!new);
	AA_BUG(new->size < a->size + b->size);

	label_for_each_in_merge(i, a, b, next) {
		AA_BUG(!next);
		if (profile_is_stale(next)) {
			new->vec[k] = aa_get_newest_profile(next);
			AA_BUG(!new->vec[k]->label.proxy);
			AA_BUG(!new->vec[k]->label.proxy->label);
			if (next->label.proxy != new->vec[k]->label.proxy)
				invcount++;
			k++;
			stale = true;
		} else
			new->vec[k++] = aa_get_profile(next);
	}
	/* set to actual size which is <= allocated len */
	new->size = k;
	new->vec[k] = NULL;

	if (invcount) {
		new->size -= aa_vec_unique(&new->vec[0], new->size,
					   VEC_FLAG_TERMINATE);
		/* TODO: deal with reference labels */
		if (new->size == 1) {
			label = aa_get_label(&new->vec[0]->label);
			return label;
		}
	} else if (!stale) {
		/*
		 * merge could be same as a || b, note: it is not possible
		 * for new->size == a->size == b->size unless a == b
		 */
		if (k == a->size)
			return aa_get_label(a);
		else if (k == b->size)
			return aa_get_label(b);
	}
	if (vec_unconfined(new->vec, new->size))
		new->flags |= FLAG_UNCONFINED;
	ls = labels_set(new);
	write_lock_irqsave(&ls->lock, flags);
	label = __label_insert(labels_set(new), new, false);
	write_unlock_irqrestore(&ls->lock, flags);

	return label;
}

/**
 * labelset_of_merge - find which labelset a merged label should be inserted
 * @a: label to merge and insert
 * @b: label to merge and insert
 *
 * Returns: labelset that the merged label should be inserted into
 */
static struct aa_labelset *labelset_of_merge(struct aa_label *a,
					     struct aa_label *b)
{
	struct aa_ns *nsa = labels_ns(a);
	struct aa_ns *nsb = labels_ns(b);

	if (ns_cmp(nsa, nsb) <= 0)
		return &nsa->labels;
	return &nsb->labels;
}

/**
 * __label_find_merge - find label that is equiv to merge of @a and @b
 * @ls: set of labels to search (NOT NULL)
 * @a: label to merge with @b  (NOT NULL)
 * @b: label to merge with @a  (NOT NULL)
 *
 * Requires: ls->lock read_lock held
 *
 * Returns: ref counted label that is equiv to merge of @a and @b
 *     else NULL if merge of @a and @b is not in set
 */
static struct aa_label *__label_find_merge(struct aa_labelset *ls,
					   struct aa_label *a,
					   struct aa_label *b)
{
	struct rb_node *node;

	AA_BUG(!ls);
	AA_BUG(!a);
	AA_BUG(!b);

	if (a == b)
		return __label_find(a);

	node  = ls->root.rb_node;
	while (node) {
		struct aa_label *this = container_of(node, struct aa_label,
						     node);
		int result = label_merge_cmp(a, b, this);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return __aa_get_label(this);
	}

	return NULL;
}


/**
 * aa_label_find_merge - find label that is equiv to merge of @a and @b
 * @a: label to merge with @b  (NOT NULL)
 * @b: label to merge with @a  (NOT NULL)
 *
 * Requires: labels be fully constructed with a valid ns
 *
 * Returns: ref counted label that is equiv to merge of @a and @b
 *     else NULL if merge of @a and @b is not in set
 */
struct aa_label *aa_label_find_merge(struct aa_label *a, struct aa_label *b)
{
	struct aa_labelset *ls;
	struct aa_label *label, *ar = NULL, *br = NULL;
	unsigned long flags;

	AA_BUG(!a);
	AA_BUG(!b);

	if (label_is_stale(a))
		a = ar = aa_get_newest_label(a);
	if (label_is_stale(b))
		b = br = aa_get_newest_label(b);
	ls = labelset_of_merge(a, b);
	read_lock_irqsave(&ls->lock, flags);
	label = __label_find_merge(ls, a, b);
	read_unlock_irqrestore(&ls->lock, flags);
	aa_put_label(ar);
	aa_put_label(br);

	return label;
}

/**
 * aa_label_merge - attempt to insert new merged label of @a and @b
 * @ls: set of labels to insert label into (NOT NULL)
 * @a: label to merge with @b  (NOT NULL)
 * @b: label to merge with @a  (NOT NULL)
 * @gfp: memory allocation type
 *
 * Requires: caller to hold valid refs on @a and @b
 *           labels be fully constructed with a valid ns
 *
 * Returns: ref counted new label if successful in inserting merge of a & b
 *     else ref counted equivalent label that is already in the set.
 *     else NULL if could not create label (-ENOMEM)
 */
struct aa_label *aa_label_merge(struct aa_label *a, struct aa_label *b,
				gfp_t gfp)
{
	struct aa_label *label = NULL;

	AA_BUG(!a);
	AA_BUG(!b);

	if (a == b)
		return aa_get_newest_label(a);

	/* TODO: enable when read side is lockless
	 * check if label exists before taking locks
	if (!label_is_stale(a) && !label_is_stale(b))
		label = aa_label_find_merge(a, b);
	*/

	if (!label) {
		struct aa_label *new;

		a = aa_get_newest_label(a);
		b = aa_get_newest_label(b);

		/* could use label_merge_len(a, b), but requires double
		 * comparison for small savings
		 */
		new = aa_label_alloc(a->size + b->size, NULL, gfp);
		if (!new)
			goto out;

		label = label_merge_insert(new, a, b);
		label_free_or_put_new(label, new);
out:
		aa_put_label(a);
		aa_put_label(b);
	}

	return label;
}

static inline bool label_is_visible(struct aa_profile *profile,
				    struct aa_label *label)
{
	return aa_ns_visible(profile->ns, labels_ns(label), true);
}

/* match a profile and its associated ns component if needed
 * Assumes visibility test has already been done.
 * If a subns profile is not to be matched should be prescreened with
 * visibility test.
 */
static inline unsigned int match_component(struct aa_profile *profile,
					   struct aa_profile *tp,
					   unsigned int state)
{
	const char *ns_name;

	if (profile->ns == tp->ns)
		return aa_dfa_match(profile->policy.dfa, state, tp->base.hname);

	/* try matching with namespace name and then profile */
	ns_name = aa_ns_name(profile->ns, tp->ns, true);
	state = aa_dfa_match_len(profile->policy.dfa, state, ":", 1);
	state = aa_dfa_match(profile->policy.dfa, state, ns_name);
	state = aa_dfa_match_len(profile->policy.dfa, state, ":", 1);
	return aa_dfa_match(profile->policy.dfa, state, tp->base.hname);
}

/**
 * label_compound_match - find perms for full compound label
 * @profile: profile to find perms for
 * @label: label to check access permissions for
 * @start: state to start match in
 * @subns: whether to do permission checks on components in a subns
 * @request: permissions to request
 * @perms: perms struct to set
 *
 * Returns: 0 on success else ERROR
 *
 * For the label A//&B//&C this does the perm match for A//&B//&C
 * @perms should be preinitialized with allperms OR a previous permission
 *        check to be stacked.
 */
static int label_compound_match(struct aa_profile *profile,
				struct aa_label *label,
				unsigned int state, bool subns, u32 request,
				struct aa_perms *perms)
{
	struct aa_profile *tp;
	struct label_it i;

	/* find first subcomponent that is visible */
	label_for_each(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = match_component(profile, tp, state);
		if (!state)
			goto fail;
		goto next;
	}

	/* no component visible */
	*perms = allperms;
	return 0;

next:
	label_for_each_cont(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = aa_dfa_match(profile->policy.dfa, state, "//&");
		state = match_component(profile, tp, state);
		if (!state)
			goto fail;
	}
	aa_compute_perms(profile->policy.dfa, state, perms);
	aa_apply_modes_to_perms(profile, perms);
	if ((perms->allow & request) != request)
		return -EACCES;

	return 0;

fail:
	*perms = nullperms;
	return state;
}

/**
 * label_components_match - find perms for all subcomponents of a label
 * @profile: profile to find perms for
 * @label: label to check access permissions for
 * @start: state to start match in
 * @subns: whether to do permission checks on components in a subns
 * @request: permissions to request
 * @perms: an initialized perms struct to add accumulation to
 *
 * Returns: 0 on success else ERROR
 *
 * For the label A//&B//&C this does the perm match for each of A and B and C
 * @perms should be preinitialized with allperms OR a previous permission
 *        check to be stacked.
 */
static int label_components_match(struct aa_profile *profile,
				  struct aa_label *label, unsigned int start,
				  bool subns, u32 request,
				  struct aa_perms *perms)
{
	struct aa_profile *tp;
	struct label_it i;
	struct aa_perms tmp;
	unsigned int state = 0;

	/* find first subcomponent to test */
	label_for_each(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = match_component(profile, tp, start);
		if (!state)
			goto fail;
		goto next;
	}

	/* no subcomponents visible - no change in perms */
	return 0;

next:
	aa_compute_perms(profile->policy.dfa, state, &tmp);
	aa_apply_modes_to_perms(profile, &tmp);
	aa_perms_accum(perms, &tmp);
	label_for_each_cont(i, label, tp) {
		if (!aa_ns_visible(profile->ns, tp->ns, subns))
			continue;
		state = match_component(profile, tp, start);
		if (!state)
			goto fail;
		aa_compute_perms(profile->policy.dfa, state, &tmp);
		aa_apply_modes_to_perms(profile, &tmp);
		aa_perms_accum(perms, &tmp);
	}

	if ((perms->allow & request) != request)
		return -EACCES;

	return 0;

fail:
	*perms = nullperms;
	return -EACCES;
}

/**
 * aa_label_match - do a multi-component label match
 * @profile: profile to match against (NOT NULL)
 * @label: label to match (NOT NULL)
 * @state: state to start in
 * @subns: whether to match subns components
 * @request: permission request
 * @perms: Returns computed perms (NOT NULL)
 *
 * Returns: the state the match finished in, may be the none matching state
 */
int aa_label_match(struct aa_profile *profile, struct aa_label *label,
		   unsigned int state, bool subns, u32 request,
		   struct aa_perms *perms)
{
	int error = label_compound_match(profile, label, state, subns, request,
					 perms);
	if (!error)
		return error;

	*perms = allperms;
	return label_components_match(profile, label, state, subns, request,
				      perms);
}


/**
 * aa_update_label_name - update a label to have a stored name
 * @ns: ns being viewed from (NOT NULL)
 * @label: label to update (NOT NULL)
 * @gfp: type of memory allocation
 *
 * Requires: labels_set(label) not locked in caller
 *
 * note: only updates the label name if it does not have a name already
 *       and if it is in the labelset
 */
bool aa_update_label_name(struct aa_ns *ns, struct aa_label *label, gfp_t gfp)
{
	struct aa_labelset *ls;
	unsigned long flags;
	char __counted *name;
	bool res = false;

	AA_BUG(!ns);
	AA_BUG(!label);

	if (label->hname || labels_ns(label) != ns)
		return res;

	if (aa_label_acntsxprint(&name, ns, label, FLAGS_NONE, gfp) == -1)
		return res;

	ls = labels_set(label);
	write_lock_irqsave(&ls->lock, flags);
	if (!label->hname && label->flags & FLAG_IN_TREE) {
		label->hname = name;
		res = true;
	} else
		aa_put_str(name);
	write_unlock_irqrestore(&ls->lock, flags);

	return res;
}

/*
 * cached label name is present and visible
 * @label->hname only exists if label is namespace hierachical
 */
static inline bool use_label_hname(struct aa_ns *ns, struct aa_label *label,
				   int flags)
{
	if (label->hname && (!ns || labels_ns(label) == ns) &&
	    !(flags & ~FLAG_SHOW_MODE))
		return true;

	return false;
}

/* helper macro for snprint routines */
#define update_for_len(total, len, size, str)	\
do {					\
	AA_BUG(len < 0);		\
	total += len;			\
	len = min(len, size);		\
	size -= len;			\
	str += len;			\
} while (0)

/**
 * aa_profile_snxprint - print a profile name to a buffer
 * @str: buffer to write to. (MAY BE NULL if @size == 0)
 * @size: size of buffer
 * @view: namespace profile is being viewed from
 * @profile: profile to view (NOT NULL)
 * @flags: whether to include the mode string
 * @prev_ns: last ns printed when used in compound print
 *
 * Returns: size of name written or would be written if larger than
 *          available buffer
 *
 * Note: will not print anything if the profile is not visible
 */
static int aa_profile_snxprint(char *str, size_t size, struct aa_ns *view,
			       struct aa_profile *profile, int flags,
			       struct aa_ns **prev_ns)
{
	const char *ns_name = NULL;

	AA_BUG(!str && size != 0);
	AA_BUG(!profile);

	if (!view)
		view = profiles_ns(profile);

	if (view != profile->ns &&
	    (!prev_ns || (*prev_ns != profile->ns))) {
		if (prev_ns)
			*prev_ns = profile->ns;
		ns_name = aa_ns_name(view, profile->ns,
				     flags & FLAG_VIEW_SUBNS);
		if (ns_name == aa_hidden_ns_name) {
			if (flags & FLAG_HIDDEN_UNCONFINED)
				return snprintf(str, size, "%s", "unconfined");
			return snprintf(str, size, "%s", ns_name);
		}
	}

	if ((flags & FLAG_SHOW_MODE) && profile != profile->ns->unconfined) {
		const char *modestr = aa_profile_mode_names[profile->mode];

		if (ns_name)
			return snprintf(str, size, ":%s:%s (%s)", ns_name,
					profile->base.hname, modestr);
		return snprintf(str, size, "%s (%s)", profile->base.hname,
				modestr);
	}

	if (ns_name)
		return snprintf(str, size, ":%s:%s", ns_name,
				profile->base.hname);
	return snprintf(str, size, "%s", profile->base.hname);
}

static const char *label_modename(struct aa_ns *ns, struct aa_label *label,
				  int flags)
{
	struct aa_profile *profile;
	struct label_it i;
	int mode = -1, count = 0;

	label_for_each(i, label, profile) {
		if (aa_ns_visible(ns, profile->ns, flags & FLAG_VIEW_SUBNS)) {
			if (profile->mode == APPARMOR_UNCONFINED)
				/* special case unconfined so stacks with
				 * unconfined don't report as mixed. ie.
				 * profile_foo//&:ns1:unconfined (mixed)
				 */
				continue;
			count++;
			if (mode == -1)
				mode = profile->mode;
			else if (mode != profile->mode)
				return "mixed";
		}
	}

	if (count == 0)
		return "-";
	if (mode == -1)
		/* everything was unconfined */
		mode = APPARMOR_UNCONFINED;

	return aa_profile_mode_names[mode];
}

/* if any visible label is not unconfined the display_mode returns true */
static inline bool display_mode(struct aa_ns *ns, struct aa_label *label,
				int flags)
{
	if ((flags & FLAG_SHOW_MODE)) {
		struct aa_profile *profile;
		struct label_it i;

		label_for_each(i, label, profile) {
			if (aa_ns_visible(ns, profile->ns,
					  flags & FLAG_VIEW_SUBNS) &&
			    profile != profile->ns->unconfined)
				return true;
		}
		/* only ns->unconfined in set of profiles in ns */
		return false;
	}

	return false;
}

/**
 * aa_label_snxprint - print a label name to a string buffer
 * @str: buffer to write to. (MAY BE NULL if @size == 0)
 * @size: size of buffer
 * @ns: namespace profile is being viewed from
 * @label: label to view (NOT NULL)
 * @flags: whether to include the mode string
 *
 * Returns: size of name written or would be written if larger than
 *          available buffer
 *
 * Note: labels do not have to be strictly hierarchical to the ns as
 *       objects may be shared across different namespaces and thus
 *       pickup labeling from each ns.  If a particular part of the
 *       label is not visible it will just be excluded.  And if none
 *       of the label is visible "---" will be used.
 */
int aa_label_snxprint(char *str, size_t size, struct aa_ns *ns,
		      struct aa_label *label, int flags)
{
	struct aa_profile *profile;
	struct aa_ns *prev_ns = NULL;
	struct label_it i;
	int count = 0, total = 0;
	size_t len;

	AA_BUG(!str && size != 0);
	AA_BUG(!label);

	if (flags & FLAG_ABS_ROOT) {
		ns = root_ns;
		len = snprintf(str, size, "=");
		update_for_len(total, len, size, str);
	} else if (!ns) {
		ns = labels_ns(label);
	}

	label_for_each(i, label, profile) {
		if (aa_ns_visible(ns, profile->ns, flags & FLAG_VIEW_SUBNS)) {
			if (count > 0) {
				len = snprintf(str, size, "//&");
				update_for_len(total, len, size, str);
			}
			len = aa_profile_snxprint(str, size, ns, profile,
						  flags & FLAG_VIEW_SUBNS,
						  &prev_ns);
			update_for_len(total, len, size, str);
			count++;
		}
	}

	if (count == 0) {
		if (flags & FLAG_HIDDEN_UNCONFINED)
			return snprintf(str, size, "%s", "unconfined");
		return snprintf(str, size, "%s", aa_hidden_ns_name);
	}

	/* count == 1 && ... is for backwards compat where the mode
	 * is not displayed for 'unconfined' in the current ns
	 */
	if (display_mode(ns, label, flags)) {
		len = snprintf(str, size, " (%s)",
			       label_modename(ns, label, flags));
		update_for_len(total, len, size, str);
	}

	return total;
}
#undef update_for_len

/**
 * aa_label_asxprint - allocate a string buffer and print label into it
 * @strp: Returns - the allocated buffer with the label name. (NOT NULL)
 * @ns: namespace profile is being viewed from
 * @label: label to view (NOT NULL)
 * @flags: flags controlling what label info is printed
 * @gfp: kernel memory allocation type
 *
 * Returns: size of name written or would be written if larger than
 *          available buffer
 */
int aa_label_asxprint(char **strp, struct aa_ns *ns, struct aa_label *label,
		      int flags, gfp_t gfp)
{
	int size;

	AA_BUG(!strp);
	AA_BUG(!label);

	size = aa_label_snxprint(NULL, 0, ns, label, flags);
	if (size < 0)
		return size;

	*strp = kmalloc(size + 1, gfp);
	if (!*strp)
		return -ENOMEM;
	return aa_label_snxprint(*strp, size + 1, ns, label, flags);
}

/**
 * aa_label_acntsxprint - allocate a __counted string buffer and print label
 * @strp: buffer to write to. (MAY BE NULL if @size == 0)
 * @ns: namespace profile is being viewed from
 * @label: label to view (NOT NULL)
 * @flags: flags controlling what label info is printed
 * @gfp: kernel memory allocation type
 *
 * Returns: size of name written or would be written if larger than
 *          available buffer
 */
int aa_label_acntsxprint(char __counted **strp, struct aa_ns *ns,
			 struct aa_label *label, int flags, gfp_t gfp)
{
	int size;

	AA_BUG(!strp);
	AA_BUG(!label);

	size = aa_label_snxprint(NULL, 0, ns, label, flags);
	if (size < 0)
		return size;

	*strp = aa_str_alloc(size + 1, gfp);
	if (!*strp)
		return -ENOMEM;
	return aa_label_snxprint(*strp, size + 1, ns, label, flags);
}


void aa_label_xaudit(struct audit_buffer *ab, struct aa_ns *ns,
		     struct aa_label *label, int flags, gfp_t gfp)
{
	const char *str;
	char *name = NULL;
	int len;

	AA_BUG(!ab);
	AA_BUG(!label);

	if (!use_label_hname(ns, label, flags) ||
	    display_mode(ns, label, flags)) {
		len  = aa_label_asxprint(&name, ns, label, flags, gfp);
		if (len == -1) {
			AA_DEBUG("label print error");
			return;
		}
		str = name;
	} else {
		str = (char *) label->hname;
		len = strlen(str);
	}
	if (audit_string_contains_control(str, len))
		audit_log_n_hex(ab, str, len);
	else
		audit_log_n_string(ab, str, len);

	kfree(name);
}

void aa_label_seq_xprint(struct seq_file *f, struct aa_ns *ns,
			 struct aa_label *label, int flags, gfp_t gfp)
{
	AA_BUG(!f);
	AA_BUG(!label);

	if (!use_label_hname(ns, label, flags)) {
		char *str;
		int len;

		len = aa_label_asxprint(&str, ns, label, flags, gfp);
		if (len == -1) {
			AA_DEBUG("label print error");
			return;
		}
		seq_printf(f, "%s", str);
		kfree(str);
	} else if (display_mode(ns, label, flags))
		seq_printf(f, "%s (%s)", label->hname,
			   label_modename(ns, label, flags));
	else
		seq_printf(f, "%s", label->hname);
}

void aa_label_xprintk(struct aa_ns *ns, struct aa_label *label, int flags,
		      gfp_t gfp)
{
	AA_BUG(!label);

	if (!use_label_hname(ns, label, flags)) {
		char *str;
		int len;

		len = aa_label_asxprint(&str, ns, label, flags, gfp);
		if (len == -1) {
			AA_DEBUG("label print error");
			return;
		}
		pr_info("%s", str);
		kfree(str);
	} else if (display_mode(ns, label, flags))
		pr_info("%s (%s)", label->hname,
		       label_modename(ns, label, flags));
	else
		pr_info("%s", label->hname);
}

void aa_label_audit(struct audit_buffer *ab, struct aa_label *label, gfp_t gfp)
{
	struct aa_ns *ns = aa_get_current_ns();

	aa_label_xaudit(ab, ns, label, FLAG_VIEW_SUBNS, gfp);
	aa_put_ns(ns);
}

void aa_label_seq_print(struct seq_file *f, struct aa_label *label, gfp_t gfp)
{
	struct aa_ns *ns = aa_get_current_ns();

	aa_label_seq_xprint(f, ns, label, FLAG_VIEW_SUBNS, gfp);
	aa_put_ns(ns);
}

void aa_label_printk(struct aa_label *label, gfp_t gfp)
{
	struct aa_ns *ns = aa_get_current_ns();

	aa_label_xprintk(ns, label, FLAG_VIEW_SUBNS, gfp);
	aa_put_ns(ns);
}

static int label_count_strn_entries(const char *str, size_t n)
{
	const char *end = str + n;
	const char *split;
	int count = 1;

	AA_BUG(!str);

	for (split = aa_label_strn_split(str, end - str);
	     split;
	     split = aa_label_strn_split(str, end - str)) {
		count++;
		str = split + 3;
	}

	return count;
}

/*
 * ensure stacks with components like
 *   :ns:A//&B
 * have :ns: applied to both 'A' and 'B' by making the lookup relative
 * to the base if the lookup specifies an ns, else making the stacked lookup
 * relative to the last embedded ns in the string.
 */
static struct aa_profile *fqlookupn_profile(struct aa_label *base,
					    struct aa_label *currentbase,
					    const char *str, size_t n)
{
	const char *first = skipn_spaces(str, n);

	if (first && *first == ':')
		return aa_fqlookupn_profile(base, str, n);

	return aa_fqlookupn_profile(currentbase, str, n);
}

/**
 * aa_label_strn_parse - parse, validate and convert a text string to a label
 * @base: base label to use for lookups (NOT NULL)
 * @str: null terminated text string (NOT NULL)
 * @n: length of str to parse, will stop at \0 if encountered before n
 * @gfp: allocation type
 * @create: true if should create compound labels if they don't exist
 * @force_stack: true if should stack even if no leading &
 *
 * Returns: the matching refcounted label if present
 *     else ERRPTR
 */
struct aa_label *aa_label_strn_parse(struct aa_label *base, const char *str,
				     size_t n, gfp_t gfp, bool create,
				     bool force_stack)
{
	DEFINE_VEC(profile, vec);
	struct aa_label *label, *currbase = base;
	int i, len, stack = 0, error;
	const char *end = str + n;
	const char *split;

	AA_BUG(!base);
	AA_BUG(!str);

	str = skipn_spaces(str, n);
	if (str == NULL || (*str == '=' && base != &root_ns->unconfined->label))
		return ERR_PTR(-EINVAL);

	len = label_count_strn_entries(str, end - str);
	if (*str == '&' || force_stack) {
		/* stack on top of base */
		stack = base->size;
		len += stack;
		if (*str == '&')
			str++;
	}

	error = vec_setup(profile, vec, len, gfp);
	if (error)
		return ERR_PTR(error);

	for (i = 0; i < stack; i++)
		vec[i] = aa_get_profile(base->vec[i]);

	for (split = aa_label_strn_split(str, end - str), i = stack;
	     split && i < len; i++) {
		vec[i] = fqlookupn_profile(base, currbase, str, split - str);
		if (!vec[i])
			goto fail;
		/*
		 * if component specified a new ns it becomes the new base
		 * so that subsequent lookups are relative to it
		 */
		if (vec[i]->ns != labels_ns(currbase))
			currbase = &vec[i]->label;
		str = split + 3;
		split = aa_label_strn_split(str, end - str);
	}
	/* last element doesn't have a split */
	if (i < len) {
		vec[i] = fqlookupn_profile(base, currbase, str, end - str);
		if (!vec[i])
			goto fail;
	}
	if (len == 1)
		/* no need to free vec as len < LOCAL_VEC_ENTRIES */
		return &vec[0]->label;

	len -= aa_vec_unique(vec, len, VEC_FLAG_TERMINATE);
	/* TODO: deal with reference labels */
	if (len == 1) {
		label = aa_get_label(&vec[0]->label);
		goto out;
	}

	if (create)
		label = aa_vec_find_or_create_label(vec, len, gfp);
	else
		label = vec_find(vec, len);
	if (!label)
		goto fail;

out:
	/* use adjusted len from after vec_unique, not original */
	vec_cleanup(profile, vec, len);
	return label;

fail:
	label = ERR_PTR(-ENOENT);
	goto out;
}

struct aa_label *aa_label_parse(struct aa_label *base, const char *str,
				gfp_t gfp, bool create, bool force_stack)
{
	return aa_label_strn_parse(base, str, strlen(str), gfp, create,
				   force_stack);
}

/**
 * aa_labelset_destroy - remove all labels from the label set
 * @ls: label set to cleanup (NOT NULL)
 *
 * Labels that are removed from the set may still exist beyond the set
 * being destroyed depending on their reference counting
 */
void aa_labelset_destroy(struct aa_labelset *ls)
{
	struct rb_node *node;
	unsigned long flags;

	AA_BUG(!ls);

	write_lock_irqsave(&ls->lock, flags);
	for (node = rb_first(&ls->root); node; node = rb_first(&ls->root)) {
		struct aa_label *this = rb_entry(node, struct aa_label, node);

		if (labels_ns(this) != root_ns)
			__label_remove(this,
				       ns_unconfined(labels_ns(this)->parent));
		else
			__label_remove(this, NULL);
	}
	write_unlock_irqrestore(&ls->lock, flags);
}

/*
 * @ls: labelset to init (NOT NULL)
 */
void aa_labelset_init(struct aa_labelset *ls)
{
	AA_BUG(!ls);

	rwlock_init(&ls->lock);
	ls->root = RB_ROOT;
}

static struct aa_label *labelset_next_stale(struct aa_labelset *ls)
{
	struct aa_label *label;
	struct rb_node *node;
	unsigned long flags;

	AA_BUG(!ls);

	read_lock_irqsave(&ls->lock, flags);

	__labelset_for_each(ls, node) {
		label = rb_entry(node, struct aa_label, node);
		if ((label_is_stale(label) ||
		     vec_is_stale(label->vec, label->size)) &&
		    __aa_get_label(label))
			goto out;

	}
	label = NULL;

out:
	read_unlock_irqrestore(&ls->lock, flags);

	return label;
}

/**
 * __label_update - insert updated version of @label into labelset
 * @label - the label to update/replace
 *
 * Returns: new label that is up to date
 *     else NULL on failure
 *
 * Requires: @ns lock be held
 *
 * Note: worst case is the stale @label does not get updated and has
 *       to be updated at a later time.
 */
static struct aa_label *__label_update(struct aa_label *label)
{
	struct aa_label *new, *tmp;
	struct aa_labelset *ls;
	unsigned long flags;
	int i, invcount = 0;

	AA_BUG(!label);
	AA_BUG(!mutex_is_locked(&labels_ns(label)->lock));

	new = aa_label_alloc(label->size, label->proxy, GFP_KERNEL);
	if (!new)
		return NULL;

	/*
	 * while holding the ns_lock will stop profile replacement, removal,
	 * and label updates, label merging and removal can be occurring
	 */
	ls = labels_set(label);
	write_lock_irqsave(&ls->lock, flags);
	for (i = 0; i < label->size; i++) {
		AA_BUG(!label->vec[i]);
		new->vec[i] = aa_get_newest_profile(label->vec[i]);
		AA_BUG(!new->vec[i]);
		AA_BUG(!new->vec[i]->label.proxy);
		AA_BUG(!new->vec[i]->label.proxy->label);
		if (new->vec[i]->label.proxy != label->vec[i]->label.proxy)
			invcount++;
	}

	/* updated stale label by being removed/renamed from labelset */
	if (invcount) {
		new->size -= aa_vec_unique(&new->vec[0], new->size,
					   VEC_FLAG_TERMINATE);
		/* TODO: deal with reference labels */
		if (new->size == 1) {
			tmp = aa_get_label(&new->vec[0]->label);
			AA_BUG(tmp == label);
			goto remove;
		}
		if (labels_set(label) != labels_set(new)) {
			write_unlock_irqrestore(&ls->lock, flags);
			tmp = aa_label_insert(labels_set(new), new);
			write_lock_irqsave(&ls->lock, flags);
			goto remove;
		}
	} else
		AA_BUG(labels_ns(label) != labels_ns(new));

	tmp = __label_insert(labels_set(label), new, true);
remove:
	/* ensure label is removed, and redirected correctly */
	__label_remove(label, tmp);
	write_unlock_irqrestore(&ls->lock, flags);
	label_free_or_put_new(tmp, new);

	return tmp;
}

/**
 * __labelset_update - update labels in @ns
 * @ns: namespace to update labels in  (NOT NULL)
 *
 * Requires: @ns lock be held
 *
 * Walk the labelset ensuring that all labels are up to date and valid
 * Any label that has a stale component is marked stale and replaced and
 * by an updated version.
 *
 * If failures happen due to memory pressures then stale labels will
 * be left in place until the next pass.
 */
static void __labelset_update(struct aa_ns *ns)
{
	struct aa_label *label;

	AA_BUG(!ns);
	AA_BUG(!mutex_is_locked(&ns->lock));

	do {
		label = labelset_next_stale(&ns->labels);
		if (label) {
			struct aa_label *l = __label_update(label);

			aa_put_label(l);
			aa_put_label(label);
		}
	} while (label);
}

/**
 * __aa_labelset_udate_subtree - update all labels with a stale component
 * @ns: ns to start update at (NOT NULL)
 *
 * Requires: @ns lock be held
 *
 * Invalidates labels based on @p in @ns and any children namespaces.
 */
void __aa_labelset_update_subtree(struct aa_ns *ns)
{
	struct aa_ns *child;

	AA_BUG(!ns);
	AA_BUG(!mutex_is_locked(&ns->lock));

	__labelset_update(ns);

	list_for_each_entry(child, &ns->sub_ns, base.list) {
		mutex_lock_nested(&child->lock, child->level);
		__aa_labelset_update_subtree(child);
		mutex_unlock(&child->lock);
	}
}
