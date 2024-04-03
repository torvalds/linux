/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor policy definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
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
extern int aa_unprivileged_unconfined_restricted;

extern const char *const aa_profile_mode_names[];
#define APPARMOR_MODE_NAMES_MAX_INDEX 4

#define PROFILE_MODE(_profile, _mode)		\
	((aa_g_profile_mode == (_mode)) ||	\
	 ((_profile)->mode == (_mode)))

#define COMPLAIN_MODE(_profile)	PROFILE_MODE((_profile), APPARMOR_COMPLAIN)

#define USER_MODE(_profile)	PROFILE_MODE((_profile), APPARMOR_USER)

#define KILL_MODE(_profile) PROFILE_MODE((_profile), APPARMOR_KILL)

#define PROFILE_IS_HAT(_profile) ((_profile)->label.flags & FLAG_HAT)

#define CHECK_DEBUG1(_profile) ((_profile)->label.flags & FLAG_DEBUG1)

#define CHECK_DEBUG2(_profile) ((_profile)->label.flags & FLAG_DEBUG2)

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
	APPARMOR_USER,		/* modified complain mode to userspace */
};


/* struct aa_policydb - match engine for a policy
 * count: refcount for the pdb
 * dfa: dfa pattern match
 * perms: table of permissions
 * strs: table of strings, index by x
 * start: set of start states for the different classes of data
 */
struct aa_policydb {
	struct kref count;
	struct aa_dfa *dfa;
	struct {
		struct aa_perms *perms;
		u32 size;
	};
	struct aa_str_table trans;
	aa_state_t start[AA_CLASS_LAST + 1];
};

extern struct aa_policydb *nullpdb;

struct aa_policydb *aa_alloc_pdb(gfp_t gfp);
void aa_pdb_free_kref(struct kref *kref);

/**
 * aa_get_pdb - increment refcount on @pdb
 * @pdb: policydb  (MAYBE NULL)
 *
 * Returns: pointer to @pdb if @pdb is NULL will return NULL
 * Requires: @pdb must be held with valid refcount when called
 */
static inline struct aa_policydb *aa_get_pdb(struct aa_policydb *pdb)
{
	if (pdb)
		kref_get(&(pdb->count));

	return pdb;
}

/**
 * aa_put_pdb - put a pdb refcount
 * @pdb: pdb to put refcount   (MAYBE NULL)
 *
 * Requires: if @pdb != NULL that a valid refcount be held
 */
static inline void aa_put_pdb(struct aa_policydb *pdb)
{
	if (pdb)
		kref_put(&pdb->count, aa_pdb_free_kref);
}

static inline struct aa_perms *aa_lookup_perms(struct aa_policydb *policy,
					       aa_state_t state)
{
	unsigned int index = ACCEPT_TABLE(policy->dfa)[state];

	if (!(policy->perms))
		return &default_perms;

	return &(policy->perms[index]);
}


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

/* struct aa_ruleset - data covering mediation rules
 * @list: list the rule is on
 * @size: the memory consumed by this ruleset
 * @policy: general match rules governing policy
 * @file: The set of rules governing basic file access and domain transitions
 * @caps: capabilities for the profile
 * @rlimits: rlimits for the profile
 * @secmark_count: number of secmark entries
 * @secmark: secmark label match info
 */
struct aa_ruleset {
	struct list_head list;

	int size;

	/* TODO: merge policy and file */
	struct aa_policydb *policy;
	struct aa_policydb *file;
	struct aa_caps caps;

	struct aa_rlimit rlimits;

	int secmark_count;
	struct aa_secmark *secmark;
};

/* struct aa_attachment - data and rules for a profiles attachment
 * @list:
 * @xmatch_str: human readable attachment string
 * @xmatch: optional extended matching for unconfined executables names
 * @xmatch_len: xmatch prefix len, used to determine xmatch priority
 * @xattr_count: number of xattrs in table
 * @xattrs: table of xattrs
 */
struct aa_attachment {
	const char *xmatch_str;
	struct aa_policydb *xmatch;
	unsigned int xmatch_len;
	int xattr_count;
	char **xattrs;
};

/* struct aa_profile - basic confinement data
 * @base - base components of the profile (name, refcount, lists, lock ...)
 * @label - label this profile is an extension of
 * @parent: parent of profile
 * @ns: namespace the profile is in
 * @rename: optional profile name that this profile renamed
 *
 * @audit: the auditing mode of the profile
 * @mode: the enforcement mode of the profile
 * @path_flags: flags controlling path generation behavior
 * @disconnected: what to prepend if attach_disconnected is specified
 * @attach: attachment rules for the profile
 * @rules: rules to be enforced
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

	enum audit_mode audit;
	long mode;
	u32 path_flags;
	const char *disconnected;

	struct aa_attachment attach;
	struct list_head rules;

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

struct aa_ruleset *aa_alloc_ruleset(gfp_t gfp);
struct aa_profile *aa_alloc_profile(const char *name, struct aa_proxy *proxy,
				    gfp_t gfp);
struct aa_profile *aa_alloc_null(struct aa_profile *parent, const char *name,
				 gfp_t gfp);
struct aa_profile *aa_new_learning_profile(struct aa_profile *parent, bool hat,
					   const char *base, gfp_t gfp);
void aa_free_profile(struct aa_profile *profile);
struct aa_profile *aa_find_child(struct aa_profile *parent, const char *name);
struct aa_profile *aa_lookupn_profile(struct aa_ns *ns, const char *hname,
				      size_t n);
struct aa_profile *aa_lookup_profile(struct aa_ns *ns, const char *name);
struct aa_profile *aa_fqlookupn_profile(struct aa_label *base,
					const char *fqname, size_t n);

ssize_t aa_replace_profiles(struct aa_ns *view, struct aa_label *label,
			    u32 mask, struct aa_loaddata *udata);
ssize_t aa_remove_profiles(struct aa_ns *view, struct aa_label *label,
			   char *name, size_t size);
void __aa_profile_list_release(struct list_head *head);

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

static inline aa_state_t RULE_MEDIATES(struct aa_ruleset *rules,
				       unsigned char class)
{
	if (class <= AA_CLASS_LAST)
		return rules->policy->start[class];
	else
		return aa_dfa_match_len(rules->policy->dfa,
					rules->policy->start[0], &class, 1);
}

static inline aa_state_t RULE_MEDIATES_AF(struct aa_ruleset *rules, u16 AF)
{
	aa_state_t state = RULE_MEDIATES(rules, AA_CLASS_NET);
	__be16 be_af = cpu_to_be16(AF);

	if (!state)
		return DFA_NOMATCH;
	return aa_dfa_match_len(rules->policy->dfa, state, (char *) &be_af, 2);
}

static inline aa_state_t ANY_RULE_MEDIATES(struct list_head *head,
					   unsigned char class)
{
	struct aa_ruleset *rule;

	/* TODO: change to list walk */
	rule = list_first_entry(head, typeof(*rule), list);
	return RULE_MEDIATES(rule, class);
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

bool aa_policy_view_capable(const struct cred *subj_cred,
			    struct aa_label *label, struct aa_ns *ns);
bool aa_policy_admin_capable(const struct cred *subj_cred,
			     struct aa_label *label, struct aa_ns *ns);
int aa_may_manage_policy(const struct cred *subj_cred,
			 struct aa_label *label, struct aa_ns *ns,
			 u32 mask);
bool aa_current_policy_view_capable(struct aa_ns *ns);
bool aa_current_policy_admin_capable(struct aa_ns *ns);

#endif /* __AA_POLICY_H */
