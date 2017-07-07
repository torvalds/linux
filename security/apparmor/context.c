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
 * the aa_task_ctx.label, both of which are required and are not allowed
 * to be NULL.  The aa_task_ctx is not reference counted and is unique
 * to each cred (which is reference count).  The label pointed to by
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
		aa_put_label(ctx->label);
		aa_put_label(ctx->previous);
		aa_put_label(ctx->onexec);

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
	aa_get_label(new->label);
	aa_get_label(new->previous);
	aa_get_label(new->onexec);
}

/**
 * aa_get_task_label - Get another task's label
 * @task: task to query  (NOT NULL)
 *
 * Returns: counted reference to @task's label
 */
struct aa_label *aa_get_task_label(struct task_struct *task)
{
	struct aa_label *p;

	rcu_read_lock();
	p = aa_get_newest_label(__aa_task_raw_label(task));
	rcu_read_unlock();

	return p;
}

/**
 * aa_replace_current_label - replace the current tasks label
 * @label: new label  (NOT NULL)
 *
 * Returns: 0 or error on failure
 */
int aa_replace_current_label(struct aa_label *label)
{
	struct aa_task_ctx *ctx = current_ctx();
	struct cred *new;
	AA_BUG(!label);

	if (ctx->label == label)
		return 0;

	if (current_cred() != current_real_cred())
		return -EBUSY;

	new  = prepare_creds();
	if (!new)
		return -ENOMEM;

	ctx = cred_ctx(new);
	if (unconfined(label) || (labels_ns(ctx->label) != labels_ns(label)))
		/* if switching to unconfined or a different label namespace
		 * clear out context state
		 */
		aa_clear_task_ctx_trans(ctx);

	/*
	 * be careful switching ctx->profile, when racing replacement it
	 * is possible that ctx->profile->proxy->profile is the reference
	 * keeping @profile valid, so make sure to get its reference before
	 * dropping the reference on ctx->profile
	 */
	aa_get_label(label);
	aa_put_label(ctx->label);
	ctx->label = label;

	commit_creds(new);
	return 0;
}

/**
 * aa_set_current_onexec - set the tasks change_profile to happen onexec
 * @label: system label to set at exec  (MAYBE NULL to clear value)
 * @stack: whether stacking should be done
 * Returns: 0 or error on failure
 */
int aa_set_current_onexec(struct aa_label *label, bool stack)
{
	struct aa_task_ctx *ctx;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	ctx = cred_ctx(new);
	aa_get_label(label);
	aa_clear_task_ctx_trans(ctx);
	ctx->onexec = label;
	ctx->token = stack;

	commit_creds(new);
	return 0;
}

/**
 * aa_set_current_hat - set the current tasks hat
 * @label: label to set as the current hat  (NOT NULL)
 * @token: token value that must be specified to change from the hat
 *
 * Do switch of tasks hat.  If the task is currently in a hat
 * validate the token to match.
 *
 * Returns: 0 or error on failure
 */
int aa_set_current_hat(struct aa_label *label, u64 token)
{
	struct aa_task_ctx *ctx;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;
	AA_BUG(!label);

	ctx = cred_ctx(new);
	if (!ctx->previous) {
		/* transfer refcount */
		ctx->previous = ctx->label;
		ctx->token = token;
	} else if (ctx->token == token) {
		aa_put_label(ctx->label);
	} else {
		/* previous_profile && ctx->token != token */
		abort_creds(new);
		return -EACCES;
	}
	ctx->label = aa_get_newest_label(label);
	/* clear exec on switching context */
	aa_put_label(ctx->onexec);
	ctx->onexec = NULL;

	commit_creds(new);
	return 0;
}

/**
 * aa_restore_previous_label - exit from hat context restoring previous label
 * @token: the token that must be matched to exit hat context
 *
 * Attempt to return out of a hat to the previous label.  The token
 * must match the stored token value.
 *
 * Returns: 0 or error of failure
 */
int aa_restore_previous_label(u64 token)
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
	/* ignore restores when there is no saved label */
	if (!ctx->previous) {
		abort_creds(new);
		return 0;
	}

	aa_put_label(ctx->label);
	ctx->label = aa_get_newest_label(ctx->previous);
	AA_BUG(!ctx->label);
	/* clear exec && prev information when restoring to previous context */
	aa_clear_task_ctx_trans(ctx);

	commit_creds(new);
	return 0;
}
