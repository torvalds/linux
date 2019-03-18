/*
 * AppArmor security module
 *
 * This file contains AppArmor task related definitions and mediation
 *
 * Copyright 2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * TODO
 * If a task uses change_hat it currently does not return to the old
 * cred or task context but instead creates a new one.  Ideally the task
 * should return to the previous cred if it has not been modified.
 */

#include "include/cred.h"
#include "include/task.h"

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
	struct aa_label *old = aa_current_raw_label();
	struct aa_task_ctx *ctx = task_ctx(current);
	struct cred *new;

	AA_BUG(!label);

	if (old == label)
		return 0;

	if (current_cred() != current_real_cred())
		return -EBUSY;

	new  = prepare_creds();
	if (!new)
		return -ENOMEM;

	if (ctx->nnp && label_is_stale(ctx->nnp)) {
		struct aa_label *tmp = ctx->nnp;

		ctx->nnp = aa_get_newest_label(tmp);
		aa_put_label(tmp);
	}
	if (unconfined(label) || (labels_ns(old) != labels_ns(label)))
		/*
		 * if switching to unconfined or a different label namespace
		 * clear out context state
		 */
		aa_clear_task_ctx_trans(task_ctx(current));

	/*
	 * be careful switching cred label, when racing replacement it
	 * is possible that the cred labels's->proxy->label is the reference
	 * keeping @label valid, so make sure to get its reference before
	 * dropping the reference on the cred's label
	 */
	aa_get_label(label);
	aa_put_label(cred_label(new));
	set_cred_label(new, label);

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
	struct aa_task_ctx *ctx = task_ctx(current);

	aa_get_label(label);
	aa_put_label(ctx->onexec);
	ctx->onexec = label;
	ctx->token = stack;

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
	struct aa_task_ctx *ctx = task_ctx(current);
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	AA_BUG(!label);

	if (!ctx->previous) {
		/* transfer refcount */
		ctx->previous = cred_label(new);
		ctx->token = token;
	} else if (ctx->token == token) {
		aa_put_label(cred_label(new));
	} else {
		/* previous_profile && ctx->token != token */
		abort_creds(new);
		return -EACCES;
	}

	set_cred_label(new, aa_get_newest_label(label));
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
	struct aa_task_ctx *ctx = task_ctx(current);
	struct cred *new;

	if (ctx->token != token)
		return -EACCES;
	/* ignore restores when there is no saved label */
	if (!ctx->previous)
		return 0;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	aa_put_label(cred_label(new));
	set_cred_label(new, aa_get_newest_label(ctx->previous));
	AA_BUG(!cred_label(new));
	/* clear exec && prev information when restoring to previous context */
	aa_clear_task_ctx_trans(ctx);

	commit_creds(new);

	return 0;
}
