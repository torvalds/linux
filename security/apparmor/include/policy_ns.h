/*
 * AppArmor security module
 *
 * This file contains AppArmor policy definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_NAMESPACE_H
#define __AA_NAMESPACE_H

#include <linux/kref.h>

#include "apparmor.h"
#include "apparmorfs.h"
#include "policy.h"


/* struct aa_ns_acct - accounting of profiles in namespace
 * @max_size: maximum space allowed for all profiles in namespace
 * @max_count: maximum number of profiles that can be in this namespace
 * @size: current size of profiles
 * @count: current count of profiles (includes null profiles)
 */
struct aa_ns_acct {
	int max_size;
	int max_count;
	int size;
	int count;
};

/* struct aa_ns - namespace for a set of profiles
 * @base: common policy
 * @parent: parent of namespace
 * @lock: lock for modifying the object
 * @acct: accounting for the namespace
 * @unconfined: special unconfined profile for the namespace
 * @sub_ns: list of namespaces under the current namespace.
 * @uniq_null: uniq value used for null learning profiles
 * @uniq_id: a unique id count for the profiles in the namespace
 * @level: level of ns within the tree hierarchy
 * @dents: dentries for the namespaces file entries in apparmorfs
 *
 * An aa_ns defines the set profiles that are searched to determine which
 * profile to attach to a task.  Profiles can not be shared between aa_ns
 * and profile names within a namespace are guaranteed to be unique.  When
 * profiles in separate namespaces have the same name they are NOT considered
 * to be equivalent.
 *
 * Namespaces are hierarchical and only namespaces and profiles below the
 * current namespace are visible.
 *
 * Namespace names must be unique and can not contain the characters :/\0
 */
struct aa_ns {
	struct aa_policy base;
	struct aa_ns *parent;
	struct mutex lock;
	struct aa_ns_acct acct;
	struct aa_profile *unconfined;
	struct list_head sub_ns;
	atomic_t uniq_null;
	long uniq_id;
	int level;

	struct dentry *dents[AAFS_NS_SIZEOF];
};

extern struct aa_ns *root_ns;

extern const char *aa_hidden_ns_name;

bool aa_ns_visible(struct aa_ns *curr, struct aa_ns *view, bool subns);
const char *aa_ns_name(struct aa_ns *parent, struct aa_ns *child, bool subns);
void aa_free_ns(struct aa_ns *ns);
int aa_alloc_root_ns(void);
void aa_free_root_ns(void);
void aa_free_ns_kref(struct kref *kref);

struct aa_ns *aa_find_ns(struct aa_ns *root, const char *name);
struct aa_ns *aa_findn_ns(struct aa_ns *root, const char *name, size_t n);
struct aa_ns *__aa_find_or_create_ns(struct aa_ns *parent, const char *name,
				     struct dentry *dir);
struct aa_ns *aa_prepare_ns(struct aa_ns *root, const char *name);
void __aa_remove_ns(struct aa_ns *ns);

static inline struct aa_profile *aa_deref_parent(struct aa_profile *p)
{
	return rcu_dereference_protected(p->parent,
					 mutex_is_locked(&p->ns->lock));
}

/**
 * aa_get_ns - increment references count on @ns
 * @ns: namespace to increment reference count of (MAYBE NULL)
 *
 * Returns: pointer to @ns, if @ns is NULL returns NULL
 * Requires: @ns must be held with valid refcount when called
 */
static inline struct aa_ns *aa_get_ns(struct aa_ns *ns)
{
	if (ns)
		aa_get_profile(ns->unconfined);

	return ns;
}

/**
 * aa_put_ns - decrement refcount on @ns
 * @ns: namespace to put reference of
 *
 * Decrement reference count of @ns and if no longer in use free it
 */
static inline void aa_put_ns(struct aa_ns *ns)
{
	if (ns)
		aa_put_profile(ns->unconfined);
}

/**
 * __aa_findn_ns - find a namespace on a list by @name
 * @head: list to search for namespace on  (NOT NULL)
 * @name: name of namespace to look for  (NOT NULL)
 * @n: length of @name
 * Returns: unrefcounted namespace
 *
 * Requires: rcu_read_lock be held
 */
static inline struct aa_ns *__aa_findn_ns(struct list_head *head,
					  const char *name, size_t n)
{
	return (struct aa_ns *)__policy_strn_find(head, name, n);
}

static inline struct aa_ns *__aa_find_ns(struct list_head *head,
					 const char *name)
{
	return __aa_findn_ns(head, name, strlen(name));
}

#endif /* AA_NAMESPACE_H */
