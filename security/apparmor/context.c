/*
 * AppArmor security module
 *
 * This file contains AppArmor functions used to manipulate object security
 * contexts.
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
 * AppArmor sets confinement on every task, via the the aa_task_cxt and
 * the aa_task_cxt.profile, both of which are required and are not allowed
 * to be NULL.  The aa_task_cxt is not reference counted and is unique
 * to each cred (which is reference count).  The profile pointed to by
 * the task_cxt is reference counted.
 *
 * TODO
 * If a task uses change_hat it currently does not return to the old
 * cred or task context but instead creates a new one.  Ideally the task
 * should return to the previous cred if it has not been modified.
 *
 */

#include "include/context.h"
#include "include/policy.h"

/**
 * aa_alloc_task_context - allocate a new task_cxt
 * @flags: gfp flags for allocation
 *
 * Returns: allocated buffer or NULL on failure
 */
struct aa_task_cxt *aa_alloc_task_context(gfp_t flags)
{
	return kzalloc(sizeof(struct aa_task_cxt), flags);
}

/**
 * aa_free_task_context - free a task_cxt
 * @cxt: task_cxt to free (MAYBE NULL)
 */
void aa_free_task_context(struct aa_task_cxt *cxt)
{
	if (cxt) {
		aa_put_profile(cxt->profile);
		aa_put_profile(cxt->previous);
		aa_put_profile(cxt->onexec);

		kzfree(cxt);
	}
}

/**
 * aa_dup_task_context - duplicate a task context, incrementing reference counts
 * @new: a blank task context      (NOT NULL)
 * @old: the task context to copy  (NOT NULL)
 */
void aa_dup_task_context(struct aa_task_cxt *new, const struct aa_task_cxt *old)
{
	*new = *old;
	aa_get_profile(new->profile);
	aa_get_profile(new->previous);
	aa_get_profile(new->onexec);
}

/**
 * aa_replace_current_profile - replace the current tasks profiles
 * @profile: new profile  (NOT NULL)
 *
 * Returns: 0 or error on failure
 */
int aa_replace_current_profile(struct aa_profile *profile)
{
	struct aa_task_cxt *cxt = current_cred()->security;
	struct cred *new;
	BUG_ON(!profile);

	if (cxt->profile == profile)
		return 0;

	new  = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	if (unconfined(profile) || (cxt->profile->ns != profile->ns)) {
		/* if switching to unconfined or a different profile namespace
		 * clear out context state
		 */
		aa_put_profile(cxt->previous);
		aa_put_profile(cxt->onexec);
		cxt->previous = NULL;
		cxt->onexec = NULL;
		cxt->token = 0;
	}
	/* be careful switching cxt->profile, when racing replacement it
	 * is possible that cxt->profile->replacedby is the reference keeping
	 * @profile valid, so make sure to get its reference before dropping
	 * the reference on cxt->profile */
	aa_get_profile(profile);
	aa_put_profile(cxt->profile);
	cxt->profile = profile;

	commit_creds(new);
	return 0;
}

/**
 * aa_set_current_onexec - set the tasks change_profile to happen onexec
 * @profile: system profile to set at exec  (MAYBE NULL to clear value)
 *
 * Returns: 0 or error on failure
 */
int aa_set_current_onexec(struct aa_profile *profile)
{
	struct aa_task_cxt *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	aa_get_profile(profile);
	aa_put_profile(cxt->onexec);
	cxt->onexec = profile;

	commit_creds(new);
	return 0;
}

/**
 * aa_set_current_hat - set the current tasks hat
 * @profile: profile to set as the current hat  (NOT NULL)
 * @token: token value that must be specified to change from the hat
 *
 * Do switch of tasks hat.  If the task is currently in a hat
 * validate the token to match.
 *
 * Returns: 0 or error on failure
 */
int aa_set_current_hat(struct aa_profile *profile, u64 token)
{
	struct aa_task_cxt *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;
	BUG_ON(!profile);

	cxt = new->security;
	if (!cxt->previous) {
		/* transfer refcount */
		cxt->previous = cxt->profile;
		cxt->token = token;
	} else if (cxt->token == token) {
		aa_put_profile(cxt->profile);
	} else {
		/* previous_profile && cxt->token != token */
		abort_creds(new);
		return -EACCES;
	}
	cxt->profile = aa_get_profile(aa_newest_version(profile));
	/* clear exec on switching context */
	aa_put_profile(cxt->onexec);
	cxt->onexec = NULL;

	commit_creds(new);
	return 0;
}

/**
 * aa_restore_previous_profile - exit from hat context restoring the profile
 * @token: the token that must be matched to exit hat context
 *
 * Attempt to return out of a hat to the previous profile.  The token
 * must match the stored token value.
 *
 * Returns: 0 or error of failure
 */
int aa_restore_previous_profile(u64 token)
{
	struct aa_task_cxt *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	if (cxt->token != token) {
		abort_creds(new);
		return -EACCES;
	}
	/* ignore restores when there is no saved profile */
	if (!cxt->previous) {
		abort_creds(new);
		return 0;
	}

	aa_put_profile(cxt->profile);
	cxt->profile = aa_newest_version(cxt->previous);
	BUG_ON(!cxt->profile);
	if (unlikely(cxt->profile != cxt->previous)) {
		aa_get_profile(cxt->profile);
		aa_put_profile(cxt->previous);
	}
	/* clear exec && prev information when restoring to previous context */
	cxt->previous = NULL;
	cxt->token = 0;
	aa_put_profile(cxt->onexec);
	cxt->onexec = NULL;

	commit_creds(new);
	return 0;
}
