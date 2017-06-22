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
 * AppArmor sets confinement on every task, via the the aa_task_ctx and
 * the aa_task_ctx.profile, both of which are required and are not allowed
 * to be NULL.  The aa_task_ctx is not reference counted and is unique
 * to each cred (which is reference count).  The profile pointed to by
 * the task_ctx is reference counted.
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
 * aa_alloc_task_context - allocate a new task_ctx
 * @flags: gfp flags for allocation
 *
 * Returns: allocated buffer or NULL on failure
 */
struct aa_task_ctx *aa_alloc_task_context(gfp_t flags)
{
	return kzalloc(sizeof(struct aa_task_ctx), flags);
}

/**
 * aa_free_task_context - free a task_ctx
 * @ctx: task_ctx to free (MAYBE NULL)
 */
void aa_free_task_context(struct aa_task_ctx *ctx)
{
	if (ctx) {
		aa_put_profile(ctx->profile);
		aa_put_profile(ctx->previous);
		aa_put_profile(ctx->onexec);

		kzfree(ctx);
	}
}

/**
 * aa_dup_task_context - duplicate a task context, incrementing reference counts
 * @new: a blank task context      (NOT NULL)
 * @old: the task context to copy  (NOT NULL)
 */
void aa_dup_task_context(struct aa_task_ctx *new, const struct aa_task_ctx *old)
{
	*new = *old;
	aa_get_profile(new->profile);
	aa_get_profile(new->previous);
	aa_get_profile(new->onexec);
}

/**
 * aa_get_task_profile - Get another task's profile
 * @task: task to query  (NOT NULL)
 *
 * Returns: counted reference to @task's profile
 */
struct aa_profile *aa_get_task_profile(struct task_struct *task)
{
	struct aa_profile *p;

	rcu_read_lock();
	p = aa_get_profile(__aa_task_profile(task));
	rcu_read_unlock();

	return p;
}

/**
 * aa_replace_current_profile - replace the current tasks profiles
 * @profile: new profile  (NOT NULL)
 *
 * Returns: 0 or error on failure
 */
int aa_replace_current_profile(struct aa_profile *profile)
{
	struct aa_task_ctx *ctx = current_ctx();
	struct cred *new;
	AA_BUG(!profile);

	if (ctx->profile == profile)
		return 0;

	if (current_cred() != current_real_cred())
		return -EBUSY;

	new  = prepare_creds();
	if (!new)
		return -ENOMEM;

	ctx = cred_ctx(new);
	if (unconfined(profile) || (ctx->profile->ns != profile->ns))
		/* if switching to unconfined or a different profile namespace
		 * clear out context state
		 */
		aa_clear_task_ctx_trans(ctx);

	/*
	 * be careful switching ctx->profile, when racing replacement it
	 * is possible that ctx->profile->proxy->profile is the reference
	 * keeping @profile valid, so make sure to get its reference before
	 * dropping the reference on ctx->profile
	 */
	aa_get_profile(profile);
	aa_put_profile(ctx->profile);
	ctx->profile = profile;

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
	struct aa_task_ctx *ctx;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	ctx = cred_ctx(new);
	aa_get_profile(profile);
	aa_put_profile(ctx->onexec);
	ctx->onexec = profile;

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
	struct aa_task_ctx *ctx;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;
	AA_BUG(!profile);

	ctx = cred_ctx(new);
	if (!ctx->previous) {
		/* transfer refcount */
		ctx->previous = ctx->profile;
		ctx->token = token;
	} else if (ctx->token == token) {
		aa_put_profile(ctx->profile);
	} else {
		/* previous_profile && ctx->token != token */
		abort_creds(new);
		return -EACCES;
	}
	ctx->profile = aa_get_newest_profile(profile);
	/* clear exec on switching context */
	aa_put_profile(ctx->onexec);
	ctx->onexec = NULL;

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
	struct aa_task_ctx *ctx;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	ctx = cred_ctx(new);
	if (ctx->token != token) {
		abort_creds(new);
		return -EACCES;
	}
	/* ignore restores when there is no saved profile */
	if (!ctx->previous) {
		abort_creds(new);
		return 0;
	}

	aa_put_profile(ctx->profile);
	ctx->profile = aa_get_newest_profile(ctx->previous);
	AA_BUG(!ctx->profile);
	/* clear exec && prev information when restoring to previous context */
	aa_clear_task_ctx_trans(ctx);

	commit_creds(new);
	return 0;
}
