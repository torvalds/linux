// SPDX-License-Identifier: GPL-2.0
/*
 * SafeSetID Linux Security Module
 *
 * Author: Micah Morton <mortonm@chromium.org>
 *
 * Copyright (C) 2018 The Chromium OS Authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "SafeSetID: " fmt

#include <linux/lsm_hooks.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include "lsm.h"

/* Flag indicating whether initialization completed */
int safesetid_initialized;

struct setid_ruleset __rcu *safesetid_setuid_rules;
struct setid_ruleset __rcu *safesetid_setgid_rules;


/* Compute a decision for a transition from @src to @dst under @policy. */
enum sid_policy_type _setid_policy_lookup(struct setid_ruleset *policy,
		kid_t src, kid_t dst)
{
	struct setid_rule *rule;
	enum sid_policy_type result = SIDPOL_DEFAULT;

	if (policy->type == UID) {
		hash_for_each_possible(policy->rules, rule, next, __kuid_val(src.uid)) {
			if (!uid_eq(rule->src_id.uid, src.uid))
				continue;
			if (uid_eq(rule->dst_id.uid, dst.uid))
				return SIDPOL_ALLOWED;
			result = SIDPOL_CONSTRAINED;
		}
	} else if (policy->type == GID) {
		hash_for_each_possible(policy->rules, rule, next, __kgid_val(src.gid)) {
			if (!gid_eq(rule->src_id.gid, src.gid))
				continue;
			if (gid_eq(rule->dst_id.gid, dst.gid)){
				return SIDPOL_ALLOWED;
			}
			result = SIDPOL_CONSTRAINED;
		}
	} else {
		/* Should not reach here, report the ID as contrainsted */
		result = SIDPOL_CONSTRAINED;
	}
	return result;
}

/*
 * Compute a decision for a transition from @src to @dst under the active
 * policy.
 */
static enum sid_policy_type setid_policy_lookup(kid_t src, kid_t dst, enum setid_type new_type)
{
	enum sid_policy_type result = SIDPOL_DEFAULT;
	struct setid_ruleset *pol;

	rcu_read_lock();
	if (new_type == UID)
		pol = rcu_dereference(safesetid_setuid_rules);
	else if (new_type == GID)
		pol = rcu_dereference(safesetid_setgid_rules);
	else { /* Should not reach here */
		result = SIDPOL_CONSTRAINED;
		rcu_read_unlock();
		return result;
	}

	if (pol) {
		pol->type = new_type;
		result = _setid_policy_lookup(pol, src, dst);
	}
	rcu_read_unlock();
	return result;
}

static int safesetid_security_capable(const struct cred *cred,
				      struct user_namespace *ns,
				      int cap,
				      unsigned int opts)
{
	/* We're only interested in CAP_SETUID and CAP_SETGID. */
	if (cap != CAP_SETUID && cap != CAP_SETGID)
		return 0;

	/*
	 * If CAP_SET{U/G}ID is currently used for a setid() syscall, we want to
	 * let it go through here; the real security check happens later, in the
	 * task_fix_set{u/g}id hook.
         *
         * NOTE:
         * Until we add support for restricting setgroups() calls, GID security
         * policies offer no meaningful security since we always return 0 here
         * when called from within the setgroups() syscall and there is no
         * additional hook later on to enforce security policies for setgroups().
	 */
	if ((opts & CAP_OPT_INSETID) != 0)
		return 0;

	switch (cap) {
	case CAP_SETUID:
		/*
		* If no policy applies to this task, allow the use of CAP_SETUID for
		* other purposes.
		*/
		if (setid_policy_lookup((kid_t){.uid = cred->uid}, INVALID_ID, UID) == SIDPOL_DEFAULT)
			return 0;
		/*
		 * Reject use of CAP_SETUID for functionality other than calling
		 * set*uid() (e.g. setting up userns uid mappings).
		 */
		pr_warn("Operation requires CAP_SETUID, which is not available to UID %u for operations besides approved set*uid transitions\n",
			__kuid_val(cred->uid));
		return -EPERM;
	case CAP_SETGID:
		/*
		* If no policy applies to this task, allow the use of CAP_SETGID for
		* other purposes.
		*/
		if (setid_policy_lookup((kid_t){.gid = cred->gid}, INVALID_ID, GID) == SIDPOL_DEFAULT)
			return 0;
		/*
		 * Reject use of CAP_SETUID for functionality other than calling
		 * set*gid() (e.g. setting up userns gid mappings).
		 */
		pr_warn("Operation requires CAP_SETGID, which is not available to GID %u for operations besides approved set*gid transitions\n",
			__kuid_val(cred->uid));
		return -EPERM;
	default:
		/* Error, the only capabilities were checking for is CAP_SETUID/GID */
		return 0;
	}
	return 0;
}

/*
 * Check whether a caller with old credentials @old is allowed to switch to
 * credentials that contain @new_id.
 */
static bool id_permitted_for_cred(const struct cred *old, kid_t new_id, enum setid_type new_type)
{
	bool permitted;

	/* If our old creds already had this ID in it, it's fine. */
	if (new_type == UID) {
		if (uid_eq(new_id.uid, old->uid) || uid_eq(new_id.uid, old->euid) ||
			uid_eq(new_id.uid, old->suid))
			return true;
	} else if (new_type == GID){
		if (gid_eq(new_id.gid, old->gid) || gid_eq(new_id.gid, old->egid) ||
			gid_eq(new_id.gid, old->sgid))
			return true;
	} else /* Error, new_type is an invalid type */
		return false;

	/*
	 * Transitions to new UIDs require a check against the policy of the old
	 * RUID.
	 */
	permitted =
	    setid_policy_lookup((kid_t){.uid = old->uid}, new_id, new_type) != SIDPOL_CONSTRAINED;

	if (!permitted) {
		if (new_type == UID) {
			pr_warn("UID transition ((%d,%d,%d) -> %d) blocked\n",
				__kuid_val(old->uid), __kuid_val(old->euid),
				__kuid_val(old->suid), __kuid_val(new_id.uid));
		} else if (new_type == GID) {
			pr_warn("GID transition ((%d,%d,%d) -> %d) blocked\n",
				__kgid_val(old->gid), __kgid_val(old->egid),
				__kgid_val(old->sgid), __kgid_val(new_id.gid));
		} else /* Error, new_type is an invalid type */
			return false;
	}
	return permitted;
}

/*
 * Check whether there is either an exception for user under old cred struct to
 * set*uid to user under new cred struct, or the UID transition is allowed (by
 * Linux set*uid rules) even without CAP_SETUID.
 */
static int safesetid_task_fix_setuid(struct cred *new,
				     const struct cred *old,
				     int flags)
{

	/* Do nothing if there are no setuid restrictions for our old RUID. */
	if (setid_policy_lookup((kid_t){.uid = old->uid}, INVALID_ID, UID) == SIDPOL_DEFAULT)
		return 0;

	if (id_permitted_for_cred(old, (kid_t){.uid = new->uid}, UID) &&
	    id_permitted_for_cred(old, (kid_t){.uid = new->euid}, UID) &&
	    id_permitted_for_cred(old, (kid_t){.uid = new->suid}, UID) &&
	    id_permitted_for_cred(old, (kid_t){.uid = new->fsuid}, UID))
		return 0;

	/*
	 * Kill this process to avoid potential security vulnerabilities
	 * that could arise from a missing allowlist entry preventing a
	 * privileged process from dropping to a lesser-privileged one.
	 */
	force_sig(SIGKILL);
	return -EACCES;
}

static int safesetid_task_fix_setgid(struct cred *new,
				     const struct cred *old,
				     int flags)
{

	/* Do nothing if there are no setgid restrictions for our old RGID. */
	if (setid_policy_lookup((kid_t){.gid = old->gid}, INVALID_ID, GID) == SIDPOL_DEFAULT)
		return 0;

	if (id_permitted_for_cred(old, (kid_t){.gid = new->gid}, GID) &&
	    id_permitted_for_cred(old, (kid_t){.gid = new->egid}, GID) &&
	    id_permitted_for_cred(old, (kid_t){.gid = new->sgid}, GID) &&
	    id_permitted_for_cred(old, (kid_t){.gid = new->fsgid}, GID))
		return 0;

	/*
	 * Kill this process to avoid potential security vulnerabilities
	 * that could arise from a missing allowlist entry preventing a
	 * privileged process from dropping to a lesser-privileged one.
	 */
	force_sig(SIGKILL);
	return -EACCES;
}

static struct security_hook_list safesetid_security_hooks[] = {
	LSM_HOOK_INIT(task_fix_setuid, safesetid_task_fix_setuid),
	LSM_HOOK_INIT(task_fix_setgid, safesetid_task_fix_setgid),
	LSM_HOOK_INIT(capable, safesetid_security_capable)
};

static int __init safesetid_security_init(void)
{
	security_add_hooks(safesetid_security_hooks,
			   ARRAY_SIZE(safesetid_security_hooks), "safesetid");

	/* Report that SafeSetID successfully initialized */
	safesetid_initialized = 1;

	return 0;
}

DEFINE_LSM(safesetid_security_init) = {
	.init = safesetid_security_init,
	.name = "safesetid",
};
