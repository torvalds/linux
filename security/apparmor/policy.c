/*
 * AppArmor security module
 *
 * This file contains AppArmor policy manipulation functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * AppArmor policy is based around profiles, which contain the rules a
 * task is confined by.  Every task in the system has a profile attached
 * to it determined either by matching "unconfined" tasks against the
 * visible set of profiles or by following a profiles attachment rules.
 *
 * Each profile exists in a profile namespace which is a container of
 * visible profiles.  Each namespace contains a special "unconfined" profile,
 * which doesn't enforce any confinement on a task beyond DAC.
 *
 * Namespace and profile names can be written together in either
 * of two syntaxes.
 *	:namespace:profile - used by kernel interfaces for easy detection
 *	namespace://profile - used by policy
 *
 * Profile names can not start with : or @ or ^ and may not contain \0
 *
 * Reserved profile names
 *	unconfined - special automatically generated unconfined profile
 *	inherit - special name to indicate profile inheritance
 *	null-XXXX-YYYY - special automatically generated learning profiles
 *
 * Namespace names may not start with / or @ and may not contain \0 or :
 * Reserved namespace names
 *	user-XXXX - user defined profiles
 *
 * a // in a profile or namespace name indicates a hierarchical name with the
 * name before the // being the parent and the name after the child.
 *
 * Profile and namespace hierarchies serve two different but similar purposes.
 * The namespace contains the set of visible profiles that are considered
 * for attachment.  The hierarchy of namespaces allows for virtualizing
 * the namespace so that for example a chroot can have its own set of profiles
 * which may define some local user namespaces.
 * The profile hierarchy severs two distinct purposes,
 * -  it allows for sub profiles or hats, which allows an application to run
 *    subprograms under its own profile with different restriction than it
 *    self, and not have it use the system profile.
 *    eg. if a mail program starts an editor, the policy might make the
 *        restrictions tighter on the editor tighter than the mail program,
 *        and definitely different than general editor restrictions
 * - it allows for binary hierarchy of profiles, so that execution history
 *   is preserved.  This feature isn't exploited by AppArmor reference policy
 *   but is allowed.  NOTE: this is currently suboptimal because profile
 *   aliasing is not currently implemented so that a profile for each
 *   level must be defined.
 *   eg. /bin/bash///bin/ls as a name would indicate /bin/ls was started
 *       from /bin/bash
 *
 *   A profile or namespace name that can contain one or more // separators
 *   is referred to as an hname (hierarchical).
 *   eg.  /bin/bash//bin/ls
 *
 *   An fqname is a name that may contain both namespace and profile hnames.
 *   eg. :ns:/bin/bash//bin/ls
 *
 * NOTES:
 *   - locking of profile lists is currently fairly coarse.  All profile
 *     lists within a namespace use the namespace lock.
 * FIXME: move profile lists to using rcu_lists
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/cred.h>
#include <linux/rculist.h>
#include <linux/user_namespace.h>

#include "include/apparmor.h"
#include "include/capability.h"
#include "include/context.h"
#include "include/file.h"
#include "include/ipc.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/policy_ns.h"
#include "include/policy_unpack.h"
#include "include/resource.h"

int unprivileged_userns_apparmor_policy = 1;

const char *const aa_profile_mode_names[] = {
	"enforce",
	"complain",
	"kill",
	"unconfined",
};

/* requires profile list write lock held */
void __aa_update_proxy(struct aa_profile *orig, struct aa_profile *new)
{
	struct aa_profile *tmp;

	tmp = rcu_dereference_protected(orig->proxy->profile,
					mutex_is_locked(&orig->ns->lock));
	rcu_assign_pointer(orig->proxy->profile, aa_get_profile(new));
	orig->flags |= PFLAG_STALE;
	aa_put_profile(tmp);
}

/**
 * __list_add_profile - add a profile to a list
 * @list: list to add it to  (NOT NULL)
 * @profile: the profile to add  (NOT NULL)
 *
 * refcount @profile, should be put by __list_remove_profile
 *
 * Requires: namespace lock be held, or list not be shared
 */
static void __list_add_profile(struct list_head *list,
			       struct aa_profile *profile)
{
	list_add_rcu(&profile->base.list, list);
	/* get list reference */
	aa_get_profile(profile);
}

/**
 * __list_remove_profile - remove a profile from the list it is on
 * @profile: the profile to remove  (NOT NULL)
 *
 * remove a profile from the list, warning generally removal should
 * be done with __replace_profile as most profile removals are
 * replacements to the unconfined profile.
 *
 * put @profile list refcount
 *
 * Requires: namespace lock be held, or list not have been live
 */
static void __list_remove_profile(struct aa_profile *profile)
{
	list_del_rcu(&profile->base.list);
	aa_put_profile(profile);
}

/**
 * __remove_profile - remove old profile, and children
 * @profile: profile to be replaced  (NOT NULL)
 *
 * Requires: namespace list lock be held, or list not be shared
 */
static void __remove_profile(struct aa_profile *profile)
{
	/* release any children lists first */
	__aa_profile_list_release(&profile->base.profiles);
	/* released by free_profile */
	__aa_update_proxy(profile, profile->ns->unconfined);
	__aa_fs_profile_rmdir(profile);
	__list_remove_profile(profile);
}

/**
 * __aa_profile_list_release - remove all profiles on the list and put refs
 * @head: list of profiles  (NOT NULL)
 *
 * Requires: namespace lock be held
 */
void __aa_profile_list_release(struct list_head *head)
{
	struct aa_profile *profile, *tmp;
	list_for_each_entry_safe(profile, tmp, head, base.list)
		__remove_profile(profile);
}


static void free_proxy(struct aa_proxy *p)
{
	if (p) {
		/* r->profile will not be updated any more as r is dead */
		aa_put_profile(rcu_dereference_protected(p->profile, true));
		kzfree(p);
	}
}


void aa_free_proxy_kref(struct kref *kref)
{
	struct aa_proxy *p = container_of(kref, struct aa_proxy, count);

	free_proxy(p);
}

/**
 * aa_free_data - free a data blob
 * @ptr: data to free
 * @arg: unused
 */
static void aa_free_data(void *ptr, void *arg)
{
	struct aa_data *data = ptr;

	kzfree(data->data);
	kzfree(data->key);
	kzfree(data);
}

/**
 * aa_free_profile - free a profile
 * @profile: the profile to free  (MAYBE NULL)
 *
 * Free a profile, its hats and null_profile. All references to the profile,
 * its hats and null_profile must have been put.
 *
 * If the profile was referenced from a task context, free_profile() will
 * be called from an rcu callback routine, so we must not sleep here.
 */
void aa_free_profile(struct aa_profile *profile)
{
	struct rhashtable *rht;

	AA_DEBUG("%s(%p)\n", __func__, profile);

	if (!profile)
		return;

	/* free children profiles */
	aa_policy_destroy(&profile->base);
	aa_put_profile(rcu_access_pointer(profile->parent));

	aa_put_ns(profile->ns);
	kzfree(profile->rename);

	aa_free_file_rules(&profile->file);
	aa_free_cap_rules(&profile->caps);
	aa_free_rlimit_rules(&profile->rlimits);

	kzfree(profile->dirname);
	aa_put_dfa(profile->xmatch);
	aa_put_dfa(profile->policy.dfa);
	aa_put_proxy(profile->proxy);

	if (profile->data) {
		rht = profile->data;
		profile->data = NULL;
		rhashtable_free_and_destroy(rht, aa_free_data, NULL);
		kzfree(rht);
	}

	kzfree(profile->hash);
	aa_put_loaddata(profile->rawdata);
	kzfree(profile);
}

/**
 * aa_free_profile_rcu - free aa_profile by rcu (called by aa_free_profile_kref)
 * @head: rcu_head callback for freeing of a profile  (NOT NULL)
 */
static void aa_free_profile_rcu(struct rcu_head *head)
{
	struct aa_profile *p = container_of(head, struct aa_profile, rcu);
	if (p->flags & PFLAG_NS_COUNT)
		aa_free_ns(p->ns);
	else
		aa_free_profile(p);
}

/**
 * aa_free_profile_kref - free aa_profile by kref (called by aa_put_profile)
 * @kr: kref callback for freeing of a profile  (NOT NULL)
 */
void aa_free_profile_kref(struct kref *kref)
{
	struct aa_profile *p = container_of(kref, struct aa_profile, count);
	call_rcu(&p->rcu, aa_free_profile_rcu);
}

/**
 * aa_alloc_profile - allocate, initialize and return a new profile
 * @hname: name of the profile  (NOT NULL)
 * @gfp: allocation type
 *
 * Returns: refcount profile or NULL on failure
 */
struct aa_profile *aa_alloc_profile(const char *hname, gfp_t gfp)
{
	struct aa_profile *profile;

	/* freed by free_profile - usually through aa_put_profile */
	profile = kzalloc(sizeof(*profile), gfp);
	if (!profile)
		return NULL;

	profile->proxy = kzalloc(sizeof(struct aa_proxy), gfp);
	if (!profile->proxy)
		goto fail;
	kref_init(&profile->proxy->count);

	if (!aa_policy_init(&profile->base, NULL, hname, gfp))
		goto fail;
	kref_init(&profile->count);

	/* refcount released by caller */
	return profile;

fail:
	kzfree(profile->proxy);
	kzfree(profile);

	return NULL;
}

/**
 * aa_new_null_profile - create or find a null-X learning profile
 * @parent: profile that caused this profile to be created (NOT NULL)
 * @hat: true if the null- learning profile is a hat
 * @base: name to base the null profile off of
 * @gfp: type of allocation
 *
 * Find/Create a null- complain mode profile used in learning mode.  The
 * name of the profile is unique and follows the format of parent//null-XXX.
 * where XXX is based on the @name or if that fails or is not supplied
 * a unique number
 *
 * null profiles are added to the profile list but the list does not
 * hold a count on them so that they are automatically released when
 * not in use.
 *
 * Returns: new refcounted profile else NULL on failure
 */
struct aa_profile *aa_new_null_profile(struct aa_profile *parent, bool hat,
				       const char *base, gfp_t gfp)
{
	struct aa_profile *profile;
	char *name;

	AA_BUG(!parent);

	if (base) {
		name = kmalloc(strlen(parent->base.hname) + 8 + strlen(base),
			       gfp);
		if (name) {
			sprintf(name, "%s//null-%s", parent->base.hname, base);
			goto name;
		}
		/* fall through to try shorter uniq */
	}

	name = kmalloc(strlen(parent->base.hname) + 2 + 7 + 8, gfp);
	if (!name)
		return NULL;
	sprintf(name, "%s//null-%x", parent->base.hname,
		atomic_inc_return(&parent->ns->uniq_null));

name:
	/* lookup to see if this is a dup creation */
	profile = aa_find_child(parent, basename(name));
	if (profile)
		goto out;

	profile = aa_alloc_profile(name, gfp);
	if (!profile)
		goto fail;

	profile->mode = APPARMOR_COMPLAIN;
	profile->flags |= PFLAG_NULL;
	if (hat)
		profile->flags |= PFLAG_HAT;
	profile->path_flags = parent->path_flags;

	/* released on free_profile */
	rcu_assign_pointer(profile->parent, aa_get_profile(parent));
	profile->ns = aa_get_ns(parent->ns);
	profile->file.dfa = aa_get_dfa(nulldfa);
	profile->policy.dfa = aa_get_dfa(nulldfa);

	mutex_lock(&profile->ns->lock);
	__list_add_profile(&parent->base.profiles, profile);
	mutex_unlock(&profile->ns->lock);

	/* refcount released by caller */
out:
	kfree(name);

	return profile;

fail:
	kfree(name);
	aa_free_profile(profile);
	return NULL;
}

/* TODO: profile accounting - setup in remove */

/**
 * __find_child - find a profile on @head list with a name matching @name
 * @head: list to search  (NOT NULL)
 * @name: name of profile (NOT NULL)
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted profile ptr, or NULL if not found
 */
static struct aa_profile *__find_child(struct list_head *head, const char *name)
{
	return (struct aa_profile *)__policy_find(head, name);
}

/**
 * __strn_find_child - find a profile on @head list using substring of @name
 * @head: list to search  (NOT NULL)
 * @name: name of profile (NOT NULL)
 * @len: length of @name substring to match
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted profile ptr, or NULL if not found
 */
static struct aa_profile *__strn_find_child(struct list_head *head,
					    const char *name, int len)
{
	return (struct aa_profile *)__policy_strn_find(head, name, len);
}

/**
 * aa_find_child - find a profile by @name in @parent
 * @parent: profile to search  (NOT NULL)
 * @name: profile name to search for  (NOT NULL)
 *
 * Returns: a refcounted profile or NULL if not found
 */
struct aa_profile *aa_find_child(struct aa_profile *parent, const char *name)
{
	struct aa_profile *profile;

	rcu_read_lock();
	do {
		profile = __find_child(&parent->base.profiles, name);
	} while (profile && !aa_get_profile_not0(profile));
	rcu_read_unlock();

	/* refcount released by caller */
	return profile;
}

/**
 * __lookup_parent - lookup the parent of a profile of name @hname
 * @ns: namespace to lookup profile in  (NOT NULL)
 * @hname: hierarchical profile name to find parent of  (NOT NULL)
 *
 * Lookups up the parent of a fully qualified profile name, the profile
 * that matches hname does not need to exist, in general this
 * is used to load a new profile.
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted policy or NULL if not found
 */
static struct aa_policy *__lookup_parent(struct aa_ns *ns,
					 const char *hname)
{
	struct aa_policy *policy;
	struct aa_profile *profile = NULL;
	char *split;

	policy = &ns->base;

	for (split = strstr(hname, "//"); split;) {
		profile = __strn_find_child(&policy->profiles, hname,
					    split - hname);
		if (!profile)
			return NULL;
		policy = &profile->base;
		hname = split + 2;
		split = strstr(hname, "//");
	}
	if (!profile)
		return &ns->base;
	return &profile->base;
}

/**
 * __lookupn_profile - lookup the profile matching @hname
 * @base: base list to start looking up profile name from  (NOT NULL)
 * @hname: hierarchical profile name  (NOT NULL)
 * @n: length of @hname
 *
 * Requires: rcu_read_lock be held
 *
 * Returns: unrefcounted profile pointer or NULL if not found
 *
 * Do a relative name lookup, recursing through profile tree.
 */
static struct aa_profile *__lookupn_profile(struct aa_policy *base,
					    const char *hname, size_t n)
{
	struct aa_profile *profile = NULL;
	const char *split;

	for (split = strnstr(hname, "//", n); split;
	     split = strnstr(hname, "//", n)) {
		profile = __strn_find_child(&base->profiles, hname,
					    split - hname);
		if (!profile)
			return NULL;

		base = &profile->base;
		n -= split + 2 - hname;
		hname = split + 2;
	}

	if (n)
		return __strn_find_child(&base->profiles, hname, n);
	return NULL;
}

static struct aa_profile *__lookup_profile(struct aa_policy *base,
					   const char *hname)
{
	return __lookupn_profile(base, hname, strlen(hname));
}

/**
 * aa_lookup_profile - find a profile by its full or partial name
 * @ns: the namespace to start from (NOT NULL)
 * @hname: name to do lookup on.  Does not contain namespace prefix (NOT NULL)
 * @n: size of @hname
 *
 * Returns: refcounted profile or NULL if not found
 */
struct aa_profile *aa_lookupn_profile(struct aa_ns *ns, const char *hname,
				      size_t n)
{
	struct aa_profile *profile;

	rcu_read_lock();
	do {
		profile = __lookupn_profile(&ns->base, hname, n);
	} while (profile && !aa_get_profile_not0(profile));
	rcu_read_unlock();

	/* the unconfined profile is not in the regular profile list */
	if (!profile && strncmp(hname, "unconfined", n) == 0)
		profile = aa_get_newest_profile(ns->unconfined);

	/* refcount released by caller */
	return profile;
}

struct aa_profile *aa_lookup_profile(struct aa_ns *ns, const char *hname)
{
	return aa_lookupn_profile(ns, hname, strlen(hname));
}

struct aa_profile *aa_fqlookupn_profile(struct aa_profile *base,
					const char *fqname, size_t n)
{
	struct aa_profile *profile;
	struct aa_ns *ns;
	const char *name, *ns_name;
	size_t ns_len;

	name = aa_splitn_fqname(fqname, n, &ns_name, &ns_len);
	if (ns_name) {
		ns = aa_findn_ns(base->ns, ns_name, ns_len);
		if (!ns)
			return NULL;
	} else
		ns = aa_get_ns(base->ns);

	if (name)
		profile = aa_lookupn_profile(ns, name, n - (name - fqname));
	else if (ns)
		/* default profile for ns, currently unconfined */
		profile = aa_get_newest_profile(ns->unconfined);
	else
		profile = NULL;
	aa_put_ns(ns);

	return profile;
}

/**
 * replacement_allowed - test to see if replacement is allowed
 * @profile: profile to test if it can be replaced  (MAYBE NULL)
 * @noreplace: true if replacement shouldn't be allowed but addition is okay
 * @info: Returns - info about why replacement failed (NOT NULL)
 *
 * Returns: %0 if replacement allowed else error code
 */
static int replacement_allowed(struct aa_profile *profile, int noreplace,
			       const char **info)
{
	if (profile) {
		if (profile->flags & PFLAG_IMMUTABLE) {
			*info = "cannot replace immutible profile";
			return -EPERM;
		} else if (noreplace) {
			*info = "profile already exists";
			return -EEXIST;
		}
	}
	return 0;
}

/* audit callback for net specific fields */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	if (aad(sa)->iface.ns) {
		audit_log_format(ab, " ns=");
		audit_log_untrustedstring(ab, aad(sa)->iface.ns);
	}
}

/**
 * aa_audit_policy - Do auditing of policy changes
 * @profile: profile to check if it can manage policy
 * @op: policy operation being performed
 * @gfp: memory allocation flags
 * @nsname: name of the ns being manipulated (MAY BE NULL)
 * @name: name of profile being manipulated (NOT NULL)
 * @info: any extra information to be audited (MAYBE NULL)
 * @error: error code
 *
 * Returns: the error to be returned after audit is done
 */
static int audit_policy(struct aa_profile *profile, const char *op,
			const char *nsname, const char *name,
			const char *info, int error)
{
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, op);

	aad(&sa)->iface.ns = nsname;
	aad(&sa)->name = name;
	aad(&sa)->info = info;
	aad(&sa)->error = error;

	return aa_audit(AUDIT_APPARMOR_STATUS, profile, &sa, audit_cb);
}

/**
 * policy_view_capable - check if viewing policy in at @ns is allowed
 * ns: namespace being viewed by current task (may be NULL)
 * Returns: true if viewing policy is allowed
 *
 * If @ns is NULL then the namespace being viewed is assumed to be the
 * tasks current namespace.
 */
bool policy_view_capable(struct aa_ns *ns)
{
	struct user_namespace *user_ns = current_user_ns();
	struct aa_ns *view_ns = aa_get_current_ns();
	bool root_in_user_ns = uid_eq(current_euid(), make_kuid(user_ns, 0)) ||
			       in_egroup_p(make_kgid(user_ns, 0));
	bool response = false;
	if (!ns)
		ns = view_ns;

	if (root_in_user_ns && aa_ns_visible(view_ns, ns, true) &&
	    (user_ns == &init_user_ns ||
	     (unprivileged_userns_apparmor_policy != 0 &&
	      user_ns->level == view_ns->level)))
		response = true;
	aa_put_ns(view_ns);

	return response;
}

bool policy_admin_capable(struct aa_ns *ns)
{
	struct user_namespace *user_ns = current_user_ns();
	bool capable = ns_capable(user_ns, CAP_MAC_ADMIN);

	AA_DEBUG("cap_mac_admin? %d\n", capable);
	AA_DEBUG("policy locked? %d\n", aa_g_lock_policy);

	return policy_view_capable(ns) && capable && !aa_g_lock_policy;
}

/**
 * aa_may_manage_policy - can the current task manage policy
 * @profile: profile to check if it can manage policy
 * @op: the policy manipulation operation being done
 *
 * Returns: 0 if the task is allowed to manipulate policy else error
 */
int aa_may_manage_policy(struct aa_profile *profile, struct aa_ns *ns,
			 const char *op)
{
	/* check if loading policy is locked out */
	if (aa_g_lock_policy)
		return audit_policy(profile, op, NULL, NULL,
			     "policy_locked", -EACCES);

	if (!policy_admin_capable(ns))
		return audit_policy(profile, op, NULL, NULL,
				    "not policy admin", -EACCES);

	/* TODO: add fine grained mediation of policy loads */
	return 0;
}

static struct aa_profile *__list_lookup_parent(struct list_head *lh,
					       struct aa_profile *profile)
{
	const char *base = basename(profile->base.hname);
	long len = base - profile->base.hname;
	struct aa_load_ent *ent;

	/* parent won't have trailing // so remove from len */
	if (len <= 2)
		return NULL;
	len -= 2;

	list_for_each_entry(ent, lh, list) {
		if (ent->new == profile)
			continue;
		if (strncmp(ent->new->base.hname, profile->base.hname, len) ==
		    0 && ent->new->base.hname[len] == 0)
			return ent->new;
	}

	return NULL;
}

/**
 * __replace_profile - replace @old with @new on a list
 * @old: profile to be replaced  (NOT NULL)
 * @new: profile to replace @old with  (NOT NULL)
 * @share_proxy: transfer @old->proxy to @new
 *
 * Will duplicate and refcount elements that @new inherits from @old
 * and will inherit @old children.
 *
 * refcount @new for list, put @old list refcount
 *
 * Requires: namespace list lock be held, or list not be shared
 */
static void __replace_profile(struct aa_profile *old, struct aa_profile *new,
			      bool share_proxy)
{
	struct aa_profile *child, *tmp;

	if (!list_empty(&old->base.profiles)) {
		LIST_HEAD(lh);
		list_splice_init_rcu(&old->base.profiles, &lh, synchronize_rcu);

		list_for_each_entry_safe(child, tmp, &lh, base.list) {
			struct aa_profile *p;

			list_del_init(&child->base.list);
			p = __find_child(&new->base.profiles, child->base.name);
			if (p) {
				/* @p replaces @child  */
				__replace_profile(child, p, share_proxy);
				continue;
			}

			/* inherit @child and its children */
			/* TODO: update hname of inherited children */
			/* list refcount transferred to @new */
			p = aa_deref_parent(child);
			rcu_assign_pointer(child->parent, aa_get_profile(new));
			list_add_rcu(&child->base.list, &new->base.profiles);
			aa_put_profile(p);
		}
	}

	if (!rcu_access_pointer(new->parent)) {
		struct aa_profile *parent = aa_deref_parent(old);
		rcu_assign_pointer(new->parent, aa_get_profile(parent));
	}
	__aa_update_proxy(old, new);
	if (share_proxy) {
		aa_put_proxy(new->proxy);
		new->proxy = aa_get_proxy(old->proxy);
	} else if (!rcu_access_pointer(new->proxy->profile))
		/* aafs interface uses proxy */
		rcu_assign_pointer(new->proxy->profile,
				   aa_get_profile(new));
	__aa_fs_profile_migrate_dents(old, new);

	if (list_empty(&new->base.list)) {
		/* new is not on a list already */
		list_replace_rcu(&old->base.list, &new->base.list);
		aa_get_profile(new);
		aa_put_profile(old);
	} else
		__list_remove_profile(old);
}

/**
 * __lookup_replace - lookup replacement information for a profile
 * @ns - namespace the lookup occurs in
 * @hname - name of profile to lookup
 * @noreplace - true if not replacing an existing profile
 * @p - Returns: profile to be replaced
 * @info - Returns: info string on why lookup failed
 *
 * Returns: profile to replace (no ref) on success else ptr error
 */
static int __lookup_replace(struct aa_ns *ns, const char *hname,
			    bool noreplace, struct aa_profile **p,
			    const char **info)
{
	*p = aa_get_profile(__lookup_profile(&ns->base, hname));
	if (*p) {
		int error = replacement_allowed(*p, noreplace, info);
		if (error) {
			*info = "profile can not be replaced";
			return error;
		}
	}

	return 0;
}

/**
 * aa_replace_profiles - replace profile(s) on the profile list
 * @view: namespace load is viewed from
 * @label: label that is attempting to load/replace policy
 * @noreplace: true if only doing addition, no replacement allowed
 * @udata: serialized data stream  (NOT NULL)
 *
 * unpack and replace a profile on the profile list and uses of that profile
 * by any aa_task_ctx.  If the profile does not exist on the profile list
 * it is added.
 *
 * Returns: size of data consumed else error code on failure.
 */
ssize_t aa_replace_profiles(struct aa_ns *view, struct aa_profile *profile,
			    bool noreplace, struct aa_loaddata *udata)
{
	const char *ns_name, *info = NULL;
	struct aa_ns *ns = NULL;
	struct aa_load_ent *ent, *tmp;
	const char *op = OP_PROF_REPL;
	ssize_t count, error;
	LIST_HEAD(lh);

	/* released below */
	error = aa_unpack(udata, &lh, &ns_name);
	if (error)
		goto out;

	/* ensure that profiles are all for the same ns
	 * TODO: update locking to remove this constaint. All profiles in
	 *       the load set must succeed as a set or the load will
	 *       fail. Sort ent list and take ns locks in hierarchy order
	 */
	count = 0;
	list_for_each_entry(ent, &lh, list) {
		if (ns_name) {
			if (ent->ns_name &&
			    strcmp(ent->ns_name, ns_name) != 0) {
				info = "policy load has mixed namespaces";
				error = -EACCES;
				goto fail;
			}
		} else if (ent->ns_name) {
			if (count) {
				info = "policy load has mixed namespaces";
				error = -EACCES;
				goto fail;
			}
			ns_name = ent->ns_name;
		} else
			count++;
	}
	if (ns_name) {
		ns = aa_prepare_ns(view, ns_name);
		if (IS_ERR(ns)) {
			op = OP_PROF_LOAD;
			info = "failed to prepare namespace";
			error = PTR_ERR(ns);
			ns = NULL;
			ent = NULL;
			goto fail;
		}
	} else
		ns = aa_get_ns(view);

	mutex_lock(&ns->lock);
	/* setup parent and ns info */
	list_for_each_entry(ent, &lh, list) {
		struct aa_policy *policy;
		ent->new->rawdata = aa_get_loaddata(udata);
		error = __lookup_replace(ns, ent->new->base.hname, noreplace,
					 &ent->old, &info);
		if (error)
			goto fail_lock;

		if (ent->new->rename) {
			error = __lookup_replace(ns, ent->new->rename,
						 noreplace, &ent->rename,
						 &info);
			if (error)
				goto fail_lock;
		}

		/* released when @new is freed */
		ent->new->ns = aa_get_ns(ns);

		if (ent->old || ent->rename)
			continue;

		/* no ref on policy only use inside lock */
		policy = __lookup_parent(ns, ent->new->base.hname);
		if (!policy) {
			struct aa_profile *p;
			p = __list_lookup_parent(&lh, ent->new);
			if (!p) {
				error = -ENOENT;
				info = "parent does not exist";
				goto fail_lock;
			}
			rcu_assign_pointer(ent->new->parent, aa_get_profile(p));
		} else if (policy != &ns->base) {
			/* released on profile replacement or free_profile */
			struct aa_profile *p = (struct aa_profile *) policy;
			rcu_assign_pointer(ent->new->parent, aa_get_profile(p));
		}
	}

	/* create new fs entries for introspection if needed */
	list_for_each_entry(ent, &lh, list) {
		if (ent->old) {
			/* inherit old interface files */

			/* if (ent->rename)
				TODO: support rename */
		/* } else if (ent->rename) {
			TODO: support rename */
		} else {
			struct dentry *parent;
			if (rcu_access_pointer(ent->new->parent)) {
				struct aa_profile *p;
				p = aa_deref_parent(ent->new);
				parent = prof_child_dir(p);
			} else
				parent = ns_subprofs_dir(ent->new->ns);
			error = __aa_fs_profile_mkdir(ent->new, parent);
		}

		if (error) {
			info = "failed to create ";
			goto fail_lock;
		}
	}

	/* Done with checks that may fail - do actual replacement */
	list_for_each_entry_safe(ent, tmp, &lh, list) {
		list_del_init(&ent->list);
		op = (!ent->old && !ent->rename) ? OP_PROF_LOAD : OP_PROF_REPL;

		audit_policy(profile, op, NULL, ent->new->base.hname,
			     NULL, error);

		if (ent->old) {
			__replace_profile(ent->old, ent->new, 1);
			if (ent->rename) {
				/* aafs interface uses proxy */
				struct aa_proxy *r = ent->new->proxy;
				rcu_assign_pointer(r->profile,
						   aa_get_profile(ent->new));
				__replace_profile(ent->rename, ent->new, 0);
			}
		} else if (ent->rename) {
			/* aafs interface uses proxy */
			rcu_assign_pointer(ent->new->proxy->profile,
					   aa_get_profile(ent->new));
			__replace_profile(ent->rename, ent->new, 0);
		} else if (ent->new->parent) {
			struct aa_profile *parent, *newest;
			parent = aa_deref_parent(ent->new);
			newest = aa_get_newest_profile(parent);

			/* parent replaced in this atomic set? */
			if (newest != parent) {
				aa_get_profile(newest);
				rcu_assign_pointer(ent->new->parent, newest);
				aa_put_profile(parent);
			}
			/* aafs interface uses proxy */
			rcu_assign_pointer(ent->new->proxy->profile,
					   aa_get_profile(ent->new));
			__list_add_profile(&newest->base.profiles, ent->new);
			aa_put_profile(newest);
		} else {
			/* aafs interface uses proxy */
			rcu_assign_pointer(ent->new->proxy->profile,
					   aa_get_profile(ent->new));
			__list_add_profile(&ns->base.profiles, ent->new);
		}
		aa_load_ent_free(ent);
	}
	mutex_unlock(&ns->lock);

out:
	aa_put_ns(ns);

	if (error)
		return error;
	return udata->size;

fail_lock:
	mutex_unlock(&ns->lock);

	/* audit cause of failure */
	op = (!ent->old) ? OP_PROF_LOAD : OP_PROF_REPL;
fail:
	audit_policy(profile, op, ns_name, ent ? ent->new->base.hname : NULL,
		     info, error);
	/* audit status that rest of profiles in the atomic set failed too */
	info = "valid profile in failed atomic policy load";
	list_for_each_entry(tmp, &lh, list) {
		if (tmp == ent) {
			info = "unchecked profile in failed atomic policy load";
			/* skip entry that caused failure */
			continue;
		}
		op = (!tmp->old) ? OP_PROF_LOAD : OP_PROF_REPL;
		audit_policy(profile, op, ns_name,
			     tmp->new->base.hname, info, error);
	}
	list_for_each_entry_safe(ent, tmp, &lh, list) {
		list_del_init(&ent->list);
		aa_load_ent_free(ent);
	}

	goto out;
}

/**
 * aa_remove_profiles - remove profile(s) from the system
 * @view: namespace the remove is being done from
 * @subj: profile attempting to remove policy
 * @fqname: name of the profile or namespace to remove  (NOT NULL)
 * @size: size of the name
 *
 * Remove a profile or sub namespace from the current namespace, so that
 * they can not be found anymore and mark them as replaced by unconfined
 *
 * NOTE: removing confinement does not restore rlimits to preconfinemnet values
 *
 * Returns: size of data consume else error code if fails
 */
ssize_t aa_remove_profiles(struct aa_ns *view, struct aa_profile *subj,
			   char *fqname, size_t size)
{
	struct aa_ns *root = NULL, *ns = NULL;
	struct aa_profile *profile = NULL;
	const char *name = fqname, *info = NULL;
	char *ns_name = NULL;
	ssize_t error = 0;

	if (*fqname == 0) {
		info = "no profile specified";
		error = -ENOENT;
		goto fail;
	}

	root = view;

	if (fqname[0] == ':') {
		name = aa_split_fqname(fqname, &ns_name);
		/* released below */
		ns = aa_find_ns(root, ns_name);
		if (!ns) {
			info = "namespace does not exist";
			error = -ENOENT;
			goto fail;
		}
	} else
		/* released below */
		ns = aa_get_ns(root);

	if (!name) {
		/* remove namespace - can only happen if fqname[0] == ':' */
		mutex_lock(&ns->parent->lock);
		__aa_remove_ns(ns);
		mutex_unlock(&ns->parent->lock);
	} else {
		/* remove profile */
		mutex_lock(&ns->lock);
		profile = aa_get_profile(__lookup_profile(&ns->base, name));
		if (!profile) {
			error = -ENOENT;
			info = "profile does not exist";
			goto fail_ns_lock;
		}
		name = profile->base.hname;
		__remove_profile(profile);
		mutex_unlock(&ns->lock);
	}

	/* don't fail removal if audit fails */
	(void) audit_policy(subj, OP_PROF_RM, ns_name, name, info,
			    error);
	aa_put_ns(ns);
	aa_put_profile(profile);
	return size;

fail_ns_lock:
	mutex_unlock(&ns->lock);
	aa_put_ns(ns);

fail:
	(void) audit_policy(subj, OP_PROF_RM, ns_name, name, info,
			    error);
	return error;
}
