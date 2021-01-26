// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor policy manipulation functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
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
#include "include/cred.h"
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


/**
 * __add_profile - add a profiles to list and label tree
 * @list: list to add it to  (NOT NULL)
 * @profile: the profile to add  (NOT NULL)
 *
 * refcount @profile, should be put by __list_remove_profile
 *
 * Requires: namespace lock be held, or list not be shared
 */
static void __add_profile(struct list_head *list, struct aa_profile *profile)
{
	struct aa_label *l;

	AA_BUG(!list);
	AA_BUG(!profile);
	AA_BUG(!profile->ns);
	AA_BUG(!mutex_is_locked(&profile->ns->lock));

	list_add_rcu(&profile->base.list, list);
	/* get list reference */
	aa_get_profile(profile);
	l = aa_label_insert(&profile->ns->labels, &profile->label);
	AA_BUG(l != &profile->label);
	aa_put_label(l);
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
	AA_BUG(!profile);
	AA_BUG(!profile->ns);
	AA_BUG(!mutex_is_locked(&profile->ns->lock));

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
	AA_BUG(!profile);
	AA_BUG(!profile->ns);
	AA_BUG(!mutex_is_locked(&profile->ns->lock));

	/* release any children lists first */
	__aa_profile_list_release(&profile->base.profiles);
	/* released by free_profile */
	aa_label_remove(&profile->label);
	__aafs_profile_rmdir(profile);
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

/**
 * aa_free_data - free a data blob
 * @ptr: data to free
 * @arg: unused
 */
static void aa_free_data(void *ptr, void *arg)
{
	struct aa_data *data = ptr;

	kfree_sensitive(data->data);
	kfree_sensitive(data->key);
	kfree_sensitive(data);
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
	int i;

	AA_DEBUG("%s(%p)\n", __func__, profile);

	if (!profile)
		return;

	/* free children profiles */
	aa_policy_destroy(&profile->base);
	aa_put_profile(rcu_access_pointer(profile->parent));

	aa_put_ns(profile->ns);
	kfree_sensitive(profile->rename);

	aa_free_file_rules(&profile->file);
	aa_free_cap_rules(&profile->caps);
	aa_free_rlimit_rules(&profile->rlimits);

	for (i = 0; i < profile->xattr_count; i++)
		kfree_sensitive(profile->xattrs[i]);
	kfree_sensitive(profile->xattrs);
	for (i = 0; i < profile->secmark_count; i++)
		kfree_sensitive(profile->secmark[i].label);
	kfree_sensitive(profile->secmark);
	kfree_sensitive(profile->dirname);
	aa_put_dfa(profile->xmatch);
	aa_put_dfa(profile->policy.dfa);

	if (profile->data) {
		rht = profile->data;
		profile->data = NULL;
		rhashtable_free_and_destroy(rht, aa_free_data, NULL);
		kfree_sensitive(rht);
	}

	kfree_sensitive(profile->hash);
	aa_put_loaddata(profile->rawdata);
	aa_label_destroy(&profile->label);

	kfree_sensitive(profile);
}

/**
 * aa_alloc_profile - allocate, initialize and return a new profile
 * @hname: name of the profile  (NOT NULL)
 * @gfp: allocation type
 *
 * Returns: refcount profile or NULL on failure
 */
struct aa_profile *aa_alloc_profile(const char *hname, struct aa_proxy *proxy,
				    gfp_t gfp)
{
	struct aa_profile *profile;

	/* freed by free_profile - usually through aa_put_profile */
	profile = kzalloc(sizeof(*profile) + sizeof(struct aa_profile *) * 2,
			  gfp);
	if (!profile)
		return NULL;

	if (!aa_policy_init(&profile->base, NULL, hname, gfp))
		goto fail;
	if (!aa_label_init(&profile->label, 1, gfp))
		goto fail;

	/* update being set needed by fs interface */
	if (!proxy) {
		proxy = aa_alloc_proxy(&profile->label, gfp);
		if (!proxy)
			goto fail;
	} else
		aa_get_proxy(proxy);
	profile->label.proxy = proxy;

	profile->label.hname = profile->base.hname;
	profile->label.flags |= FLAG_PROFILE;
	profile->label.vec[0] = profile;

	/* refcount released by caller */
	return profile;

fail:
	aa_free_profile(profile);

	return NULL;
}

/* TODO: profile accounting - setup in remove */

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
	return __strn_find_child(head, name, strlen(name));
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

struct aa_profile *aa_fqlookupn_profile(struct aa_label *base,
					const char *fqname, size_t n)
{
	struct aa_profile *profile;
	struct aa_ns *ns;
	const char *name, *ns_name;
	size_t ns_len;

	name = aa_splitn_fqname(fqname, n, &ns_name, &ns_len);
	if (ns_name) {
		ns = aa_lookupn_ns(labels_ns(base), ns_name, ns_len);
		if (!ns)
			return NULL;
	} else
		ns = aa_get_ns(labels_ns(base));

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
	struct aa_profile *p, *profile;
	const char *bname;
	char *name = NULL;

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
	bname = basename(name);
	profile = aa_find_child(parent, bname);
	if (profile)
		goto out;

	profile = aa_alloc_profile(name, NULL, gfp);
	if (!profile)
		goto fail;

	profile->mode = APPARMOR_COMPLAIN;
	profile->label.flags |= FLAG_NULL;
	if (hat)
		profile->label.flags |= FLAG_HAT;
	profile->path_flags = parent->path_flags;

	/* released on free_profile */
	rcu_assign_pointer(profile->parent, aa_get_profile(parent));
	profile->ns = aa_get_ns(parent->ns);
	profile->file.dfa = aa_get_dfa(nulldfa);
	profile->policy.dfa = aa_get_dfa(nulldfa);

	mutex_lock_nested(&profile->ns->lock, profile->ns->level);
	p = __find_child(&parent->base.profiles, bname);
	if (p) {
		aa_free_profile(profile);
		profile = aa_get_profile(p);
	} else {
		__add_profile(&parent->base.profiles, profile);
	}
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
		if (profile->label.flags & FLAG_IMMUTIBLE) {
			*info = "cannot replace immutable profile";
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
 * audit_policy - Do auditing of policy changes
 * @label: label to check if it can manage policy
 * @op: policy operation being performed
 * @ns_name: name of namespace being manipulated
 * @name: name of profile being manipulated (NOT NULL)
 * @info: any extra information to be audited (MAYBE NULL)
 * @error: error code
 *
 * Returns: the error to be returned after audit is done
 */
static int audit_policy(struct aa_label *label, const char *op,
			const char *ns_name, const char *name,
			const char *info, int error)
{
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, op);

	aad(&sa)->iface.ns = ns_name;
	aad(&sa)->name = name;
	aad(&sa)->info = info;
	aad(&sa)->error = error;
	aad(&sa)->label = label;

	aa_audit_msg(AUDIT_APPARMOR_STATUS, &sa, audit_cb);

	return error;
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
 * @label: label to check if it can manage policy
 * @op: the policy manipulation operation being done
 *
 * Returns: 0 if the task is allowed to manipulate policy else error
 */
int aa_may_manage_policy(struct aa_label *label, struct aa_ns *ns, u32 mask)
{
	const char *op;

	if (mask & AA_MAY_REMOVE_POLICY)
		op = OP_PROF_RM;
	else if (mask & AA_MAY_REPLACE_POLICY)
		op = OP_PROF_REPL;
	else
		op = OP_PROF_LOAD;

	/* check if loading policy is locked out */
	if (aa_g_lock_policy)
		return audit_policy(label, op, NULL, NULL, "policy_locked",
				    -EACCES);

	if (!policy_admin_capable(ns))
		return audit_policy(label, op, NULL, NULL, "not policy admin",
				    -EACCES);

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
static void __replace_profile(struct aa_profile *old, struct aa_profile *new)
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
				__replace_profile(child, p);
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
	aa_label_replace(&old->label, &new->label);
	/* migrate dents must come after label replacement b/c update */
	__aafs_profile_migrate_dents(old, new);

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

static void share_name(struct aa_profile *old, struct aa_profile *new)
{
	aa_put_str(new->base.hname);
	aa_get_str(old->base.hname);
	new->base.hname = old->base.hname;
	new->base.name = old->base.name;
	new->label.hname = old->label.hname;
}

/* Update to newest version of parent after previous replacements
 * Returns: unrefcount newest version of parent
 */
static struct aa_profile *update_to_newest_parent(struct aa_profile *new)
{
	struct aa_profile *parent, *newest;

	parent = rcu_dereference_protected(new->parent,
					   mutex_is_locked(&new->ns->lock));
	newest = aa_get_newest_profile(parent);

	/* parent replaced in this atomic set? */
	if (newest != parent) {
		aa_put_profile(parent);
		rcu_assign_pointer(new->parent, newest);
	} else
		aa_put_profile(newest);

	return newest;
}

/**
 * aa_replace_profiles - replace profile(s) on the profile list
 * @policy_ns: namespace load is occurring on
 * @label: label that is attempting to load/replace policy
 * @mask: permission mask
 * @udata: serialized data stream  (NOT NULL)
 *
 * unpack and replace a profile on the profile list and uses of that profile
 * by any task creds via invalidating the old version of the profile, which
 * tasks will notice to update their own cred.  If the profile does not exist
 * on the profile list it is added.
 *
 * Returns: size of data consumed else error code on failure.
 */
ssize_t aa_replace_profiles(struct aa_ns *policy_ns, struct aa_label *label,
			    u32 mask, struct aa_loaddata *udata)
{
	const char *ns_name = NULL, *info = NULL;
	struct aa_ns *ns = NULL;
	struct aa_load_ent *ent, *tmp;
	struct aa_loaddata *rawdata_ent;
	const char *op;
	ssize_t count, error;
	LIST_HEAD(lh);

	op = mask & AA_MAY_REPLACE_POLICY ? OP_PROF_REPL : OP_PROF_LOAD;
	aa_get_loaddata(udata);
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
		ns = aa_prepare_ns(policy_ns ? policy_ns : labels_ns(label),
				   ns_name);
		if (IS_ERR(ns)) {
			op = OP_PROF_LOAD;
			info = "failed to prepare namespace";
			error = PTR_ERR(ns);
			ns = NULL;
			ent = NULL;
			goto fail;
		}
	} else
		ns = aa_get_ns(policy_ns ? policy_ns : labels_ns(label));

	mutex_lock_nested(&ns->lock, ns->level);
	/* check for duplicate rawdata blobs: space and file dedup */
	list_for_each_entry(rawdata_ent, &ns->rawdata_list, list) {
		if (aa_rawdata_eq(rawdata_ent, udata)) {
			struct aa_loaddata *tmp;

			tmp = __aa_get_loaddata(rawdata_ent);
			/* check we didn't fail the race */
			if (tmp) {
				aa_put_loaddata(udata);
				udata = tmp;
				break;
			}
		}
	}
	/* setup parent and ns info */
	list_for_each_entry(ent, &lh, list) {
		struct aa_policy *policy;

		ent->new->rawdata = aa_get_loaddata(udata);
		error = __lookup_replace(ns, ent->new->base.hname,
					 !(mask & AA_MAY_REPLACE_POLICY),
					 &ent->old, &info);
		if (error)
			goto fail_lock;

		if (ent->new->rename) {
			error = __lookup_replace(ns, ent->new->rename,
						!(mask & AA_MAY_REPLACE_POLICY),
						&ent->rename, &info);
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
	if (!udata->dents[AAFS_LOADDATA_DIR]) {
		error = __aa_fs_create_rawdata(ns, udata);
		if (error) {
			info = "failed to create raw_data dir and files";
			ent = NULL;
			goto fail_lock;
		}
	}
	list_for_each_entry(ent, &lh, list) {
		if (!ent->old) {
			struct dentry *parent;
			if (rcu_access_pointer(ent->new->parent)) {
				struct aa_profile *p;
				p = aa_deref_parent(ent->new);
				parent = prof_child_dir(p);
			} else
				parent = ns_subprofs_dir(ent->new->ns);
			error = __aafs_profile_mkdir(ent->new, parent);
		}

		if (error) {
			info = "failed to create";
			goto fail_lock;
		}
	}

	/* Done with checks that may fail - do actual replacement */
	__aa_bump_ns_revision(ns);
	__aa_loaddata_update(udata, ns->revision);
	list_for_each_entry_safe(ent, tmp, &lh, list) {
		list_del_init(&ent->list);
		op = (!ent->old && !ent->rename) ? OP_PROF_LOAD : OP_PROF_REPL;

		if (ent->old && ent->old->rawdata == ent->new->rawdata) {
			/* dedup actual profile replacement */
			audit_policy(label, op, ns_name, ent->new->base.hname,
				     "same as current profile, skipping",
				     error);
			/* break refcount cycle with proxy. */
			aa_put_proxy(ent->new->label.proxy);
			ent->new->label.proxy = NULL;
			goto skip;
		}

		/*
		 * TODO: finer dedup based on profile range in data. Load set
		 * can differ but profile may remain unchanged
		 */
		audit_policy(label, op, ns_name, ent->new->base.hname, NULL,
			     error);

		if (ent->old) {
			share_name(ent->old, ent->new);
			__replace_profile(ent->old, ent->new);
		} else {
			struct list_head *lh;

			if (rcu_access_pointer(ent->new->parent)) {
				struct aa_profile *parent;

				parent = update_to_newest_parent(ent->new);
				lh = &parent->base.profiles;
			} else
				lh = &ns->base.profiles;
			__add_profile(lh, ent->new);
		}
	skip:
		aa_load_ent_free(ent);
	}
	__aa_labelset_update_subtree(ns);
	mutex_unlock(&ns->lock);

out:
	aa_put_ns(ns);
	aa_put_loaddata(udata);
	kfree(ns_name);

	if (error)
		return error;
	return udata->size;

fail_lock:
	mutex_unlock(&ns->lock);

	/* audit cause of failure */
	op = (ent && !ent->old) ? OP_PROF_LOAD : OP_PROF_REPL;
fail:
	  audit_policy(label, op, ns_name, ent ? ent->new->base.hname : NULL,
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
		audit_policy(label, op, ns_name, tmp->new->base.hname, info,
			     error);
	}
	list_for_each_entry_safe(ent, tmp, &lh, list) {
		list_del_init(&ent->list);
		aa_load_ent_free(ent);
	}

	goto out;
}

/**
 * aa_remove_profiles - remove profile(s) from the system
 * @policy_ns: namespace the remove is being done from
 * @subj: label attempting to remove policy
 * @fqname: name of the profile or namespace to remove  (NOT NULL)
 * @size: size of the name
 *
 * Remove a profile or sub namespace from the current namespace, so that
 * they can not be found anymore and mark them as replaced by unconfined
 *
 * NOTE: removing confinement does not restore rlimits to preconfinement values
 *
 * Returns: size of data consume else error code if fails
 */
ssize_t aa_remove_profiles(struct aa_ns *policy_ns, struct aa_label *subj,
			   char *fqname, size_t size)
{
	struct aa_ns *ns = NULL;
	struct aa_profile *profile = NULL;
	const char *name = fqname, *info = NULL;
	const char *ns_name = NULL;
	ssize_t error = 0;

	if (*fqname == 0) {
		info = "no profile specified";
		error = -ENOENT;
		goto fail;
	}

	if (fqname[0] == ':') {
		size_t ns_len;

		name = aa_splitn_fqname(fqname, size, &ns_name, &ns_len);
		/* released below */
		ns = aa_lookupn_ns(policy_ns ? policy_ns : labels_ns(subj),
				   ns_name, ns_len);
		if (!ns) {
			info = "namespace does not exist";
			error = -ENOENT;
			goto fail;
		}
	} else
		/* released below */
		ns = aa_get_ns(policy_ns ? policy_ns : labels_ns(subj));

	if (!name) {
		/* remove namespace - can only happen if fqname[0] == ':' */
		mutex_lock_nested(&ns->parent->lock, ns->level);
		__aa_bump_ns_revision(ns);
		__aa_remove_ns(ns);
		mutex_unlock(&ns->parent->lock);
	} else {
		/* remove profile */
		mutex_lock_nested(&ns->lock, ns->level);
		profile = aa_get_profile(__lookup_profile(&ns->base, name));
		if (!profile) {
			error = -ENOENT;
			info = "profile does not exist";
			goto fail_ns_lock;
		}
		name = profile->base.hname;
		__aa_bump_ns_revision(ns);
		__remove_profile(profile);
		__aa_labelset_update_subtree(ns);
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
