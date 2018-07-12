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
#include <linux/rhashtable.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/socket.h>

#include "apparmor.h"
#include "audit.h"
#include "capability.h"
#include "domain.h"
#include "file.h"
#include "lib.h"
#include "label.h"
#include "net.h"
#include "perms.h"
#include "resource.h"


struct aa_ns;

extern int unprivileged_userns_apparmor_policy;

extern const char *const aa_profile_mode_names[];
#define APPARMOR_MODE_NAMES_MAX_INDEX 4

#define PROFILE_MODE(_profile, _mode)		\
	((aa_g_profile_mode == (_mode)) ||	\
	 ((_profile)->mode == (_mode)))

#define COMPLAIN_MODE(_profile)	PROFILE_MODE((_profile), APPARMOR_COMPLAIN)

#define KILL_MODE(_profile) PROFILE_MODE((_profile), APPARMOR_KILL)

#define PROFILE_IS_HAT(_profile) ((_profile)->label.flags & FLAG_HAT)

#define profile_is_stale(_profile) (label_is_stale(&(_profile)->label))

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


/* struct aa_policydb - match engine for a policy
 * dfa: dfa pattern match
 * start: set of start states for the different classes of data
 */
struct aa_policydb {
	/* Generic policy DFA specific rule types will be subsections of it */
	struct aa_dfa *dfa;
	unsigned int start[AA_CLASS_LAST + 1];

};

/* struct aa_data - generic data structure
 * key: name for retrieving this data
 * size: size of data in bytes
 * data: binary data
 * head: reserved for rhashtable
 */
struct aa_data {
	char *key;
	u32 size;
	char *data;
	struct rhash_head head;
};


/* struct aa_profile - basic confinement data
 * @base - base components of the profile (name, refcount, lists, lock ...)
 * @label - label this profile is an extension of
 * @parent: parent of profile
 * @ns: namespace the profile is in
 * @rename: optional profile name that this profile renamed
 * @attach: human readable attachment string
 * @xmatch: optional extended matching for unconfined executables names
 * @xmatch_len: xmatch prefix len, used to determine xmatch priority
 * @audit: the auditing mode of the profile
 * @mode: the enforcement mode of the profile
 * @path_flags: flags controlling path generation behavior
 * @disconnected: what to prepend if attach_disconnected is specified
 * @size: the memory consumed by this profiles rules
 * @policy: general match rules governing policy
 * @file: The set of rules governing basic file access and domain transitions
 * @caps: capabilities for the profile
 * @rlimits: rlimits for the profile
 *
 * @dents: dentries for the profiles file entries in apparmorfs
 * @dirname: name of the profile dir in apparmorfs
 * @data: hashtable for free-form policy aa_data
 *
 * The AppArmor profile contains the basic confinement data.  Each profile
 * has a name, and exists in a namespace.  The @name and @exec_match are
 * used to determine profile attachment against unconfined tasks.  All other
 * attachments are determined by profile X transition rules.
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
	struct aa_profile __rcu *parent;

	struct aa_ns *ns;
	const char *rename;

	const char *attach;
	struct aa_dfa *xmatch;
	int xmatch_len;
	enum audit_mode audit;
	long mode;
	u32 path_flags;
	const char *disconnected;
	int size;

	struct aa_policydb policy;
	struct aa_file_rules file;
	struct aa_caps caps;

	int xattr_count;
	char **xattrs;

	struct aa_rlimit rlimits;

	struct aa_loaddata *rawdata;
	unsigned char *hash;
	char *dirname;
	struct dentry *dents[AAFS_PROF_SIZEOF];
	struct rhashtable *data;
	struct aa_label label;
};

extern enum profile_mode aa_g_profile_mode;

#define AA_MAY_LOAD_POLICY	AA_MAY_APPEND
#define AA_MAY_REPLACE_POLICY	AA_MAY_WRITE
#define AA_MAY_REMOVE_POLICY	AA_MAY_DELETE

#define profiles_ns(P) ((P)->ns)
#define name_is_shared(A, B) ((A)->hname && (A)->hname == (B)->hname)

void aa_add_profile(struct aa_policy *common, struct aa_profile *profile);


void aa_free_proxy_kref(struct kref *kref);
struct aa_profile *aa_alloc_profile(const char *name, struct aa_proxy *proxy,
				    gfp_t gfp);
struct aa_profile *aa_new_null_profile(struct aa_profile *parent, bool hat,
				       const char *base, gfp_t gfp);
void aa_free_profile(struct aa_profile *profile);
void aa_free_profile_kref(struct kref *kref);
struct aa_profile *aa_find_child(struct aa_profile *parent, const char *name);
struct aa_profile *aa_lookupn_profile(struct aa_ns *ns, const char *hname,
				      size_t n);
struct aa_profile *aa_lookup_profile(struct aa_ns *ns, const char *name);
struct aa_profile *aa_fqlookupn_profile(struct aa_label *base,
					const char *fqname, size_t n);
struct aa_profile *aa_match_profile(struct aa_ns *ns, const char *name);

ssize_t aa_replace_profiles(struct aa_ns *view, struct aa_label *label,
			    u32 mask, struct aa_loaddata *udata);
ssize_t aa_remove_profiles(struct aa_ns *view, struct aa_label *label,
			   char *name, size_t size);
void __aa_profile_list_release(struct list_head *head);

#define PROF_ADD 1
#define PROF_REPLACE 0

#define profile_unconfined(X) ((X)->mode == APPARMOR_UNCONFINED)

/**
 * aa_get_newest_profile - simple wrapper fn to wrap the label version
 * @p: profile (NOT NULL)
 *
 * Returns refcount to newest version of the profile (maybe @p)
 *
 * Requires: @p must be held with a valid refcount
 */
static inline struct aa_profile *aa_get_newest_profile(struct aa_profile *p)
{
	return labels_profile(aa_get_newest_label(&p->label));
}

#define PROFILE_MEDIATES(P, T)  ((P)->policy.start[(unsigned char) (T)])
static inline unsigned int PROFILE_MEDIATES_AF(struct aa_profile *profile,
					       u16 AF) {
	unsigned int state = PROFILE_MEDIATES(profile, AA_CLASS_NET);
	__be16 be_af = cpu_to_be16(AF);

	if (!state)
		return 0;
	return aa_dfa_match_len(profile->policy.dfa, state, (char *) &be_af, 2);
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
		kref_get(&(p->label.count));

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
	if (p && kref_get_unless_zero(&p->label.count))
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
	} while (c && !kref_get_unless_zero(&c->label.count));
	rcu_read_unlock();

	return c;
}

/**
 * aa_put_profile - decrement refcount on profile @p
 * @p: profile  (MAYBE NULL)
 */
static inline void aa_put_profile(struct aa_profile *p)
{
	if (p)
		kref_put(&p->label.count, aa_label_kref);
}

static inline int AUDIT_MODE(struct aa_profile *profile)
{
	if (aa_g_audit != AUDIT_NORMAL)
		return aa_g_audit;

	return profile->audit;
}

bool policy_view_capable(struct aa_ns *ns);
bool policy_admin_capable(struct aa_ns *ns);
int aa_may_manage_policy(struct aa_label *label, struct aa_ns *ns,
			 u32 mask);

#endif /* __AA_POLICY_H */
