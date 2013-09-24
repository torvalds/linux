/*
 * AppArmor security module
 *
 * This file contains AppArmor policy definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_POLICY_H
#define __AA_POLICY_H

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/kref.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/socket.h>

#include "apparmor.h"
#include "audit.h"
#include "capability.h"
#include "domain.h"
#include "file.h"
#include "resource.h"

extern const char *const aa_profile_mode_names[];
#define APPARMOR_MODE_NAMES_MAX_INDEX 4

#define PROFILE_MODE(_profile, _mode)		\
	((aa_g_profile_mode == (_mode)) ||	\
	 ((_profile)->mode == (_mode)))

#define COMPLAIN_MODE(_profile)	PROFILE_MODE((_profile), APPARMOR_COMPLAIN)

#define KILL_MODE(_profile) PROFILE_MODE((_profile), APPARMOR_KILL)

#define PROFILE_IS_HAT(_profile) ((_profile)->flags & PFLAG_HAT)

#define PROFILE_INVALID(_profile) ((_profile)->flags & PFLAG_INVALID)

#define on_list_rcu(X) (!list_empty(X) && (X)->prev != LIST_POISON2)

/*
 * FIXME: currently need a clean way to replace and remove profiles as a
 * set.  It should be done at the namespace level.
 * Either, with a set of profiles loaded at the namespace level or via
 * a mark and remove marked interface.
 */
enum profile_mode {
	APPARMOR_ENFORCE,	/* enforce access rules */
	APPARMOR_COMPLAIN,	/* allow and log access violations */
	APPARMOR_KILL,		/* kill task on access violation */
	APPARMOR_UNCONFINED,	/* profile set to unconfined */
};

enum profile_flags {
	PFLAG_HAT = 1,			/* profile is a hat */
	PFLAG_NULL = 4,			/* profile is null learning profile */
	PFLAG_IX_ON_NAME_ERROR = 8,	/* fallback to ix on name lookup fail */
	PFLAG_IMMUTABLE = 0x10,		/* don't allow changes/replacement */
	PFLAG_USER_DEFINED = 0x20,	/* user based profile - lower privs */
	PFLAG_NO_LIST_REF = 0x40,	/* list doesn't keep profile ref */
	PFLAG_OLD_NULL_TRANS = 0x100,	/* use // as the null transition */
	PFLAG_INVALID = 0x200,		/* profile replaced/removed */
	PFLAG_NS_COUNT = 0x400,		/* carries NS ref count */

	/* These flags must correspond with PATH_flags */
	PFLAG_MEDIATE_DELETED = 0x10000, /* mediate instead delegate deleted */
};

struct aa_profile;

/* struct aa_policy - common part of both namespaces and profiles
 * @name: name of the object
 * @hname - The hierarchical name
 * @list: list policy object is on
 * @profiles: head of the profiles list contained in the object
 */
struct aa_policy {
	char *name;
	char *hname;
	struct list_head list;
	struct list_head profiles;
};

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

/* struct aa_namespace - namespace for a set of profiles
 * @base: common policy
 * @parent: parent of namespace
 * @lock: lock for modifying the object
 * @acct: accounting for the namespace
 * @unconfined: special unconfined profile for the namespace
 * @sub_ns: list of namespaces under the current namespace.
 * @uniq_null: uniq value used for null learning profiles
 * @uniq_id: a unique id count for the profiles in the namespace
 * @dents: dentries for the namespaces file entries in apparmorfs
 *
 * An aa_namespace defines the set profiles that are searched to determine
 * which profile to attach to a task.  Profiles can not be shared between
 * aa_namespaces and profile names within a namespace are guaranteed to be
 * unique.  When profiles in separate namespaces have the same name they
 * are NOT considered to be equivalent.
 *
 * Namespaces are hierarchical and only namespaces and profiles below the
 * current namespace are visible.
 *
 * Namespace names must be unique and can not contain the characters :/\0
 *
 * FIXME TODO: add vserver support of namespaces (can it all be done in
 *             userspace?)
 */
struct aa_namespace {
	struct aa_policy base;
	struct aa_namespace *parent;
	struct mutex lock;
	struct aa_ns_acct acct;
	struct aa_profile *unconfined;
	struct list_head sub_ns;
	atomic_t uniq_null;
	long uniq_id;

	struct dentry *dents[AAFS_NS_SIZEOF];
};

/* struct aa_policydb - match engine for a policy
 * dfa: dfa pattern match
 * start: set of start states for the different classes of data
 */
struct aa_policydb {
	/* Generic policy DFA specific rule types will be subsections of it */
	struct aa_dfa *dfa;
	unsigned int start[AA_CLASS_LAST + 1];

};

struct aa_replacedby {
	struct kref count;
	struct aa_profile __rcu *profile;
};


/* struct aa_profile - basic confinement data
 * @base - base components of the profile (name, refcount, lists, lock ...)
 * @count: reference count of the obj
 * @rcu: rcu head used when removing from @list
 * @parent: parent of profile
 * @ns: namespace the profile is in
 * @replacedby: is set to the profile that replaced this profile
 * @rename: optional profile name that this profile renamed
 * @attach: human readable attachment string
 * @xmatch: optional extended matching for unconfined executables names
 * @xmatch_len: xmatch prefix len, used to determine xmatch priority
 * @audit: the auditing mode of the profile
 * @mode: the enforcement mode of the profile
 * @flags: flags controlling profile behavior
 * @path_flags: flags controlling path generation behavior
 * @size: the memory consumed by this profiles rules
 * @policy: general match rules governing policy
 * @file: The set of rules governing basic file access and domain transitions
 * @caps: capabilities for the profile
 * @rlimits: rlimits for the profile
 *
 * @dents: dentries for the profiles file entries in apparmorfs
 * @dirname: name of the profile dir in apparmorfs
 *
 * The AppArmor profile contains the basic confinement data.  Each profile
 * has a name, and exists in a namespace.  The @name and @exec_match are
 * used to determine profile attachment against unconfined tasks.  All other
 * attachments are determined by profile X transition rules.
 *
 * The @replacedby struct is write protected by the profile lock.
 *
 * Profiles have a hierarchy where hats and children profiles keep
 * a reference to their parent.
 *
 * Profile names can not begin with a : and can not contain the \0
 * character.  If a profile name begins with / it will be considered when
 * determining profile attachment on "unconfined" tasks.
 */
struct aa_profile {
	struct aa_policy base;
	struct kref count;
	struct rcu_head rcu;
	struct aa_profile __rcu *parent;

	struct aa_namespace *ns;
	struct aa_replacedby *replacedby;
	const char *rename;

	const char *attach;
	struct aa_dfa *xmatch;
	int xmatch_len;
	enum audit_mode audit;
	long mode;
	long flags;
	u32 path_flags;
	int size;

	struct aa_policydb policy;
	struct aa_file_rules file;
	struct aa_caps caps;
	struct aa_rlimit rlimits;

	unsigned char *hash;
	char *dirname;
	struct dentry *dents[AAFS_PROF_SIZEOF];
};

extern struct aa_namespace *root_ns;
extern enum profile_mode aa_g_profile_mode;

void aa_add_profile(struct aa_policy *common, struct aa_profile *profile);

bool aa_ns_visible(struct aa_namespace *curr, struct aa_namespace *view);
const char *aa_ns_name(struct aa_namespace *parent, struct aa_namespace *child);
int aa_alloc_root_ns(void);
void aa_free_root_ns(void);
void aa_free_namespace_kref(struct kref *kref);

struct aa_namespace *aa_find_namespace(struct aa_namespace *root,
				       const char *name);


void aa_free_replacedby_kref(struct kref *kref);
struct aa_profile *aa_alloc_profile(const char *name);
struct aa_profile *aa_new_null_profile(struct aa_profile *parent, int hat);
void aa_free_profile(struct aa_profile *profile);
void aa_free_profile_kref(struct kref *kref);
struct aa_profile *aa_find_child(struct aa_profile *parent, const char *name);
struct aa_profile *aa_lookup_profile(struct aa_namespace *ns, const char *name);
struct aa_profile *aa_match_profile(struct aa_namespace *ns, const char *name);

ssize_t aa_replace_profiles(void *udata, size_t size, bool noreplace);
ssize_t aa_remove_profiles(char *name, size_t size);

#define PROF_ADD 1
#define PROF_REPLACE 0

#define unconfined(X) ((X)->mode == APPARMOR_UNCONFINED)


static inline struct aa_profile *aa_deref_parent(struct aa_profile *p)
{
	return rcu_dereference_protected(p->parent,
					 mutex_is_locked(&p->ns->lock));
}

/**
 * aa_get_profile - increment refcount on profile @p
 * @p: profile  (MAYBE NULL)
 *
 * Returns: pointer to @p if @p is NULL will return NULL
 * Requires: @p must be held with valid refcount when called
 */
static inline struct aa_profile *aa_get_profile(struct aa_profile *p)
{
	if (p)
		kref_get(&(p->count));

	return p;
}

/**
 * aa_get_profile_not0 - increment refcount on profile @p found via lookup
 * @p: profile  (MAYBE NULL)
 *
 * Returns: pointer to @p if @p is NULL will return NULL
 * Requires: @p must be held with valid refcount when called
 */
static inline struct aa_profile *aa_get_profile_not0(struct aa_profile *p)
{
	if (p && kref_get_not0(&p->count))
		return p;

	return NULL;
}

/**
 * aa_get_profile_rcu - increment a refcount profile that can be replaced
 * @p: pointer to profile that can be replaced (NOT NULL)
 *
 * Returns: pointer to a refcounted profile.
 *     else NULL if no profile
 */
static inline struct aa_profile *aa_get_profile_rcu(struct aa_profile __rcu **p)
{
	struct aa_profile *c;

	rcu_read_lock();
	do {
		c = rcu_dereference(*p);
	} while (c && !kref_get_not0(&c->count));
	rcu_read_unlock();

	return c;
}

/**
 * aa_get_newest_profile - find the newest version of @profile
 * @profile: the profile to check for newer versions of
 *
 * Returns: refcounted newest version of @profile taking into account
 *          replacement, renames and removals
 *          return @profile.
 */
static inline struct aa_profile *aa_get_newest_profile(struct aa_profile *p)
{
	if (!p)
		return NULL;

	if (PROFILE_INVALID(p))
		return aa_get_profile_rcu(&p->replacedby->profile);

	return aa_get_profile(p);
}

/**
 * aa_put_profile - decrement refcount on profile @p
 * @p: profile  (MAYBE NULL)
 */
static inline void aa_put_profile(struct aa_profile *p)
{
	if (p)
		kref_put(&p->count, aa_free_profile_kref);
}

static inline struct aa_replacedby *aa_get_replacedby(struct aa_replacedby *p)
{
	if (p)
		kref_get(&(p->count));

	return p;
}

static inline void aa_put_replacedby(struct aa_replacedby *p)
{
	if (p)
		kref_put(&p->count, aa_free_replacedby_kref);
}

/* requires profile list write lock held */
static inline void __aa_update_replacedby(struct aa_profile *orig,
					  struct aa_profile *new)
{
	struct aa_profile *tmp = rcu_dereference(orig->replacedby->profile);
	rcu_assign_pointer(orig->replacedby->profile, aa_get_profile(new));
	orig->flags |= PFLAG_INVALID;
	aa_put_profile(tmp);
}

/**
 * aa_get_namespace - increment references count on @ns
 * @ns: namespace to increment reference count of (MAYBE NULL)
 *
 * Returns: pointer to @ns, if @ns is NULL returns NULL
 * Requires: @ns must be held with valid refcount when called
 */
static inline struct aa_namespace *aa_get_namespace(struct aa_namespace *ns)
{
	if (ns)
		aa_get_profile(ns->unconfined);

	return ns;
}

/**
 * aa_put_namespace - decrement refcount on @ns
 * @ns: namespace to put reference of
 *
 * Decrement reference count of @ns and if no longer in use free it
 */
static inline void aa_put_namespace(struct aa_namespace *ns)
{
	if (ns)
		aa_put_profile(ns->unconfined);
}

static inline int AUDIT_MODE(struct aa_profile *profile)
{
	if (aa_g_audit != AUDIT_NORMAL)
		return aa_g_audit;

	return profile->audit;
}

bool aa_may_manage_policy(int op);

#endif /* __AA_POLICY_H */
