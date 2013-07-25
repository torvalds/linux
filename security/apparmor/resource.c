/*
 * AppArmor security module
 *
 * This file contains AppArmor resource mediation and attachment
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/audit.h>

#include "include/audit.h"
#include "include/context.h"
#include "include/resource.h"
#include "include/policy.h"

/*
 * Table of rlimit names: we generate it from resource.h.
 */
#include "rlim_names.h"

struct aa_fs_entry aa_fs_entry_rlimit[] = {
	AA_FS_FILE_STRING("mask", AA_FS_RLIMIT_MASK),
	{ }
};

/* audit callback for resource specific fields */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	audit_log_format(ab, " rlimit=%s value=%lu",
			 rlim_names[sa->aad->rlim.rlim], sa->aad->rlim.max);
}

/**
 * audit_resource - audit setting resource limit
 * @profile: profile being enforced  (NOT NULL)
 * @resoure: rlimit being auditing
 * @value: value being set
 * @error: error value
 *
 * Returns: 0 or sa->error else other error code on failure
 */
static int audit_resource(struct aa_profile *profile, unsigned int resource,
			  unsigned long value, int error)
{
	struct common_audit_data sa;
	struct apparmor_audit_data aad = {0,};

	sa.type = LSM_AUDIT_DATA_NONE;
	sa.aad = &aad;
	aad.op = OP_SETRLIMIT,
	aad.rlim.rlim = resource;
	aad.rlim.max = value;
	aad.error = error;
	return aa_audit(AUDIT_APPARMOR_AUTO, profile, GFP_KERNEL, &sa,
			audit_cb);
}

/**
 * aa_map_resouce - map compiled policy resource to internal #
 * @resource: flattened policy resource number
 *
 * Returns: resource # for the current architecture.
 *
 * rlimit resource can vary based on architecture, map the compiled policy
 * resource # to the internal representation for the architecture.
 */
int aa_map_resource(int resource)
{
	return rlim_map[resource];
}

/**
 * aa_task_setrlimit - test permission to set an rlimit
 * @profile - profile confining the task  (NOT NULL)
 * @task - task the resource is being set on
 * @resource - the resource being set
 * @new_rlim - the new resource limit  (NOT NULL)
 *
 * Control raising the processes hard limit.
 *
 * Returns: 0 or error code if setting resource failed
 */
int aa_task_setrlimit(struct aa_profile *profile, struct task_struct *task,
		      unsigned int resource, struct rlimit *new_rlim)
{
	struct aa_profile *task_profile;
	int error = 0;

	rcu_read_lock();
	task_profile = aa_get_profile(aa_cred_profile(__task_cred(task)));
	rcu_read_unlock();

	/* TODO: extend resource control to handle other (non current)
	 * profiles.  AppArmor rules currently have the implicit assumption
	 * that the task is setting the resource of a task confined with
	 * the same profile.
	 */
	if (profile != task_profile ||
	    (profile->rlimits.mask & (1 << resource) &&
	     new_rlim->rlim_max > profile->rlimits.limits[resource].rlim_max))
		error = -EACCES;

	aa_put_profile(task_profile);

	return audit_resource(profile, resource, new_rlim->rlim_max, error);
}

/**
 * __aa_transition_rlimits - apply new profile rlimits
 * @old: old profile on task  (NOT NULL)
 * @new: new profile with rlimits to apply  (NOT NULL)
 */
void __aa_transition_rlimits(struct aa_profile *old, struct aa_profile *new)
{
	unsigned int mask = 0;
	struct rlimit *rlim, *initrlim;
	int i;

	/* for any rlimits the profile controlled reset the soft limit
	 * to the less of the tasks hard limit and the init tasks soft limit
	 */
	if (old->rlimits.mask) {
		for (i = 0, mask = 1; i < RLIM_NLIMITS; i++, mask <<= 1) {
			if (old->rlimits.mask & mask) {
				rlim = current->signal->rlim + i;
				initrlim = init_task.signal->rlim + i;
				rlim->rlim_cur = min(rlim->rlim_max,
						     initrlim->rlim_cur);
			}
		}
	}

	/* set any new hard limits as dictated by the new profile */
	if (!new->rlimits.mask)
		return;
	for (i = 0, mask = 1; i < RLIM_NLIMITS; i++, mask <<= 1) {
		if (!(new->rlimits.mask & mask))
			continue;

		rlim = current->signal->rlim + i;
		rlim->rlim_max = min(rlim->rlim_max,
				     new->rlimits.limits[i].rlim_max);
		/* soft limit should not exceed hard limit */
		rlim->rlim_cur = min(rlim->rlim_cur, rlim->rlim_max);
	}
}
