/*
 * AppArmor security module
 *
 * This file contains AppArmor policy manipulation functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * AppArmor policy namespaces, allow for different sets of policies
 * to be loaded for tasks within the namespace.
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "include/apparmor.h"
#include "include/context.h"
#include "include/policy_ns.h"
#include "include/label.h"
#include "include/policy.h"

/* root profile namespace */
struct aa_ns *root_ns;
const char *aa_hidden_ns_name = "---";

/**
 * aa_ns_visible - test if @view is visible from @curr
 * @curr: namespace to treat as the parent (NOT NULL)
 * @view: namespace to test if visible from @curr (NOT NULL)
 * @subns: whether view of a subns is allowed
 *
 * Returns: true if @view is visible from @curr else false
 */
bool aa_ns_visible(struct aa_ns *curr, struct aa_ns *view, bool subns)
{
	if (curr == view)
		return true;

	if (!subns)
		return false;

	for ( ; view; view = view->parent) {
		if (view->parent == curr)
			return true;
	}

	return false;
}

/**
 * aa_na_name - Find the ns name to display for @view from @curr
 * @curr - current namespace (NOT NULL)
 * @view - namespace attempting to view (NOT NULL)
 * @subns - are subns visible
 *
 * Returns: name of @view visible from @curr
 */
const char *aa_ns_name(struct aa_ns *curr, struct aa_ns *view, bool subns)
{
	/* if view == curr then the namespace name isn't displayed */
	if (curr == view)
		return "";

	if (aa_ns_visible(curr, view, subns)) {
		/* at this point if a ns is visible it is in a view ns
		 * thus the curr ns.hname is a prefix of its name.
		 * Only output the virtualized portion of the name
		 * Add + 2 to skip over // separating curr hname prefix
		 * from the visible tail of the views hname
		 */
		return view->base.hname + strlen(curr->base.hname) + 2;
	}

	return aa_hidden_ns_name;
}

/**
 * alloc_ns - allocate, initialize and return a new namespace
 * @prefix: parent namespace name (MAYBE NULL)
 * @name: a preallocated name  (NOT NULL)
 *
 * Returns: refcounted namespace or NULL on failure.
 */
static struct aa_ns *alloc_ns(const char *prefix, const char *name)
{
	struct aa_ns *ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	AA_DEBUG("%s(%p)\n", __func__, ns);
	if (!ns)
		return NULL;
	if (!aa_policy_init(&ns->base, prefix, name, GFP_KERNEL))
		goto fail_ns;

	INIT_LIST_HEAD(&ns->sub_ns);
	INIT_LIST_HEAD(&ns->rawdata_list);
	mutex_init(&ns->lock);
	init_waitqueue_head(&ns->wait);

	/* released by aa_free_ns() */
	ns->unconfined = aa_alloc_profile("unconfined", NULL, GFP_KERNEL);
	if (!ns->unconfined)
		goto fail_unconfined;

	ns->unconfined->label.flags |= FLAG_IX_ON_NAME_ERROR |
		FLAG_IMMUTIBLE | FLAG_NS_COUNT | FLAG_UNCONFINED;
	ns->unconfined->mode = APPARMOR_UNCONFINED;
	ns->unconfined->file.dfa = aa_get_dfa(nulldfa);
	ns->unconfined->policy.dfa = aa_get_dfa(nulldfa);

	/* ns and ns->unconfined share ns->unconfined refcount */
	ns->unconfined->ns = ns;

	atomic_set(&ns->uniq_null, 0);

	aa_labelset_init(&ns->labels);

	return ns;

fail_unconfined:
	kzfree(ns->base.hname);
fail_ns:
	kzfree(ns);
	return NULL;
}

/**
 * aa_free_ns - free a profile namespace
 * @ns: the namespace to free  (MAYBE NULL)
 *
 * Requires: All references to the namespace must have been put, if the
 *           namespace was referenced by a profile confining a task,
 */
void aa_free_ns(struct aa_ns *ns)
{
	if (!ns)
		return;

	aa_policy_destroy(&ns->base);
	aa_labelset_destroy(&ns->labels);
	aa_put_ns(ns->parent);

	ns->unconfined->ns = NULL;
	aa_free_profile(ns->unconfined);
	kzfree(ns);
}

/**
 * aa_findn_ns  -  look up a profile namespace on the namespace list
 * @root: namespace to search in  (NOT NULL)
 * @name: name of namespace to find  (NOT NULL)
 * @n: length of @name
 *
 * Returns: a refcounted namespace on the list, or NULL if no namespace
 *          called @name exists.
 *
 * refcount released by caller
 */
struct aa_ns *aa_findn_ns(struct aa_ns *root, const char *name, size_t n)
{
	struct aa_ns *ns = NULL;

	rcu_read_lock();
	ns = aa_get_ns(__aa_findn_ns(&root->sub_ns, name, n));
	rcu_read_unlock();

	return ns;
}

/**
 * aa_find_ns  -  look up a profile namespace on the namespace list
 * @root: namespace to search in  (NOT NULL)
 * @name: name of namespace to find  (NOT NULL)
 *
 * Returns: a refcounted namespace on the list, or NULL if no namespace
 *          called @name exists.
 *
 * refcount released by caller
 */
struct aa_ns *aa_find_ns(struct aa_ns *root, const char *name)
{
	return aa_findn_ns(root, name, strlen(name));
}

/**
 * __aa_lookupn_ns - lookup the namespace matching @hname
 * @base: base list to start looking up profile name from  (NOT NULL)
 * @hname: hierarchical ns name  (NOT NULL)
 * @n: length of @hname
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted ns pointer or NULL if not found
 *
 * Do a relative name lookup, recursing through profile tree.
 */
struct aa_ns *__aa_lookupn_ns(struct aa_ns *view, const char *hname, size_t n)
{
	struct aa_ns *ns = view;
	const char *split;

	for (split = strnstr(hname, "//", n); split;
	     split = strnstr(hname, "//", n)) {
		ns = __aa_findn_ns(&ns->sub_ns, hname, split - hname);
		if (!ns)
			return NULL;

		n -= split + 2 - hname;
		hname = split + 2;
	}

	if (n)
		return __aa_findn_ns(&ns->sub_ns, hname, n);
	return NULL;
}

/**
 * aa_lookupn_ns  -  look up a policy namespace relative to @view
 * @view: namespace to search in  (NOT NULL)
 * @name: name of namespace to find  (NOT NULL)
 * @n: length of @name
 *
 * Returns: a refcounted namespace on the list, or NULL if no namespace
 *          called @name exists.
 *
 * refcount released by caller
 */
struct aa_ns *aa_lookupn_ns(struct aa_ns *view, const char *name, size_t n)
{
	struct aa_ns *ns = NULL;

	rcu_read_lock();
	ns = aa_get_ns(__aa_lookupn_ns(view, name, n));
	rcu_read_unlock();

	return ns;
}

static struct aa_ns *__aa_create_ns(struct aa_ns *parent, const char *name,
				    struct dentry *dir)
{
	struct aa_ns *ns;
	int error;

	AA_BUG(!parent);
	AA_BUG(!name);
	AA_BUG(!mutex_is_locked(&parent->lock));

	ns = alloc_ns(parent->base.hname, name);
	if (!ns)
		return NULL;
	ns->level = parent->level + 1;
	mutex_lock_nested(&ns->lock, ns->level);
	error = __aafs_ns_mkdir(ns, ns_subns_dir(parent), name, dir);
	if (error) {
		AA_ERROR("Failed to create interface for ns %s\n",
			 ns->base.name);
		mutex_unlock(&ns->lock);
		aa_free_ns(ns);
		return ERR_PTR(error);
	}
	ns->parent = aa_get_ns(parent);
	list_add_rcu(&ns->base.list, &parent->sub_ns);
	/* add list ref */
	aa_get_ns(ns);
	mutex_unlock(&ns->lock);

	return ns;
}

/**
 * aa_create_ns - create an ns, fail if it already exists
 * @parent: the parent of the namespace being created
 * @name: the name of the namespace
 * @dir: if not null the dir to put the ns entries in
 *
 * Returns: the a refcounted ns that has been add or an ERR_PTR
 */
struct aa_ns *__aa_find_or_create_ns(struct aa_ns *parent, const char *name,
				     struct dentry *dir)
{
	struct aa_ns *ns;

	AA_BUG(!mutex_is_locked(&parent->lock));

	/* try and find the specified ns */
	/* released by caller */
	ns = aa_get_ns(__aa_find_ns(&parent->sub_ns, name));
	if (!ns)
		ns = __aa_create_ns(parent, name, dir);
	else
		ns = ERR_PTR(-EEXIST);

	/* return ref */
	return ns;
}

/**
 * aa_prepare_ns - find an existing or create a new namespace of @name
 * @parent: ns to treat as parent
 * @name: the namespace to find or add  (NOT NULL)
 *
 * Returns: refcounted namespace or PTR_ERR if failed to create one
 */
struct aa_ns *aa_prepare_ns(struct aa_ns *parent, const char *name)
{
	struct aa_ns *ns;

	mutex_lock_nested(&parent->lock, parent->level);
	/* try and find the specified ns and if it doesn't exist create it */
	/* released by caller */
	ns = aa_get_ns(__aa_find_ns(&parent->sub_ns, name));
	if (!ns)
		ns = __aa_create_ns(parent, name, NULL);
	mutex_unlock(&parent->lock);

	/* return ref */
	return ns;
}

static void __ns_list_release(struct list_head *head);

/**
 * destroy_ns - remove everything contained by @ns
 * @ns: namespace to have it contents removed  (NOT NULL)
 */
static void destroy_ns(struct aa_ns *ns)
{
	if (!ns)
		return;

	mutex_lock_nested(&ns->lock, ns->level);
	/* release all profiles in this namespace */
	__aa_profile_list_release(&ns->base.profiles);

	/* release all sub namespaces */
	__ns_list_release(&ns->sub_ns);

	if (ns->parent) {
		unsigned long flags;

		write_lock_irqsave(&ns->labels.lock, flags);
		__aa_proxy_redirect(ns_unconfined(ns),
				    ns_unconfined(ns->parent));
		write_unlock_irqrestore(&ns->labels.lock, flags);
	}
	__aafs_ns_rmdir(ns);
	mutex_unlock(&ns->lock);
}

/**
 * __aa_remove_ns - remove a namespace and all its children
 * @ns: namespace to be removed  (NOT NULL)
 *
 * Requires: ns->parent->lock be held and ns removed from parent.
 */
void __aa_remove_ns(struct aa_ns *ns)
{
	/* remove ns from namespace list */
	list_del_rcu(&ns->base.list);
	destroy_ns(ns);
	aa_put_ns(ns);
}

/**
 * __ns_list_release - remove all profile namespaces on the list put refs
 * @head: list of profile namespaces  (NOT NULL)
 *
 * Requires: namespace lock be held
 */
static void __ns_list_release(struct list_head *head)
{
	struct aa_ns *ns, *tmp;

	list_for_each_entry_safe(ns, tmp, head, base.list)
		__aa_remove_ns(ns);

}

/**
 * aa_alloc_root_ns - allocate the root profile namespace
 *
 * Returns: %0 on success else error
 *
 */
int __init aa_alloc_root_ns(void)
{
	/* released by aa_free_root_ns - used as list ref*/
	root_ns = alloc_ns(NULL, "root");
	if (!root_ns)
		return -ENOMEM;

	return 0;
}

 /**
  * aa_free_root_ns - free the root profile namespace
  */
void __init aa_free_root_ns(void)
{
	 struct aa_ns *ns = root_ns;

	 root_ns = NULL;

	 destroy_ns(ns);
	 aa_put_ns(ns);
}
