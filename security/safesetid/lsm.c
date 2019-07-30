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

struct setuid_ruleset __rcu *safesetid_setuid_rules;

/* Compute a decision for a transition from @src to @dst under @policy. */
enum sid_policy_type _setuid_policy_lookup(struct setuid_ruleset *policy,
		kuid_t src, kuid_t dst)
{
	struct setuid_rule *rule;
	enum sid_policy_type result = SIDPOL_DEFAULT;

	hash_for_each_possible(policy->rules, rule, next, __kuid_val(src)) {
		if (!uid_eq(rule->src_uid, src))
			continue;
		if (uid_eq(rule->dst_uid, dst))
			return SIDPOL_ALLOWED;
		result = SIDPOL_CONSTRAINED;
	}
	return result;
}

/*
 * Compute a decision for a transition from @src to @dst under the active
 * policy.
 */
static enum sid_policy_type setuid_policy_lookup(kuid_t src, kuid_t dst)
{
	enum sid_policy_type result = SIDPOL_DEFAULT;
	struct setuid_ruleset *pol;

	rcu_read_lock();
	pol = rcu_dereference(safesetid_setuid_rules);
	if (pol)
		result = _setuid_policy_lookup(pol, src, dst);
	rcu_read_unlock();
	return result;
}

static int safesetid_security_capable(const struct cred *cred,
				      struct user_namespace *ns,
				      int cap,
				      unsigned int opts)
{
	/* We're only interested in CAP_SETUID. */
	if (cap != CAP_SETUID)
		return 0;

	/*
	 * If CAP_SETUID is currently used for a set*uid() syscall, we want to
	 * let it go through here; the real security check happens later, in the
	 * task_fix_setuid hook.
	 */
	if ((opts & CAP_OPT_INSETID) != 0)
		return 0;

	/*
	 * If no policy applies to this task, allow the use of CAP_SETUID for
	 * other purposes.
	 */
	if (setuid_policy_lookup(cred->uid, INVALID_UID) == SIDPOL_DEFAULT)
		return 0;

	/*
	 * Reject use of CAP_SETUID for functionality other than calling
	 * set*uid() (e.g. setting up userns uid mappings).
	 */
	pr_warn("Operation requires CAP_SETUID, which is not available to UID %u for operations besides approved set*uid transitions\n",
		__kuid_val(cred->uid));
	return -EPERM;
}

/*
 * Check whether a caller with old credentials @old is allowed to switch to
 * credentials that contain @new_uid.
 */
static bool uid_permitted_for_cred(const struct cred *old, kuid_t new_uid)
{
	bool permitted;

	/* If our old creds already had this UID in it, it's fine. */
	if (uid_eq(new_uid, old->uid) || uid_eq(new_uid, old->euid) ||
	    uid_eq(new_uid, old->suid))
		return true;

	/*
	 * Transitions to new UIDs require a check against the policy of the old
	 * RUID.
	 */
	permitted =
	    setuid_policy_lookup(old->uid, new_uid) != SIDPOL_CONSTRAINED;
	if (!permitted) {
		pr_warn("UID transition ((%d,%d,%d) -> %d) blocked\n",
			__kuid_val(old->uid), __kuid_val(old->euid),
			__kuid_val(old->suid), __kuid_val(new_uid));
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
	if (setuid_policy_lookup(old->uid, INVALID_UID) == SIDPOL_DEFAULT)
		return 0;

	if (uid_permitted_for_cred(old, new->uid) &&
	    uid_permitted_for_cred(old, new->euid) &&
	    uid_permitted_for_cred(old, new->suid) &&
	    uid_permitted_for_cred(old, new->fsuid))
		return 0;

	/*
	 * Kill this process to avoid potential security vulnerabilities
	 * that could arise from a missing whitelist entry preventing a
	 * privileged process from dropping to a lesser-privileged one.
	 */
	force_sig(SIGKILL);
	return -EACCES;
}

static struct security_hook_list safesetid_security_hooks[] = {
	LSM_HOOK_INIT(task_fix_setuid, safesetid_task_fix_setuid),
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
