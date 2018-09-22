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
 */

#ifndef __AA_TASK_H
#define __AA_TASK_H

static inline struct aa_task_ctx *task_ctx(struct task_struct *task)
{
	return task->security;
}

/*
 * struct aa_task_ctx - information for current task label change
 * @nnp: snapshot of label at time of no_new_privs
 * @onexec: profile to transition to on next exec  (MAY BE NULL)
 * @previous: profile the task may return to     (MAY BE NULL)
 * @token: magic value the task must know for returning to @previous_profile
 */
struct aa_task_ctx {
	struct aa_label *nnp;
	struct aa_label *onexec;
	struct aa_label *previous;
	u64 token;
};

int aa_replace_current_label(struct aa_label *label);
int aa_set_current_onexec(struct aa_label *label, bool stack);
int aa_set_current_hat(struct aa_label *label, u64 token);
int aa_restore_previous_label(u64 cookie);
struct aa_label *aa_get_task_label(struct task_struct *task);

/**
 * aa_free_task_ctx - free a task_ctx
 * @ctx: task_ctx to free (MAYBE NULL)
 */
static inline void aa_free_task_ctx(struct aa_task_ctx *ctx)
{
	if (ctx) {
		aa_put_label(ctx->nnp);
		aa_put_label(ctx->previous);
		aa_put_label(ctx->onexec);
	}
}

/**
 * aa_dup_task_ctx - duplicate a task context, incrementing reference counts
 * @new: a blank task context      (NOT NULL)
 * @old: the task context to copy  (NOT NULL)
 */
static inline void aa_dup_task_ctx(struct aa_task_ctx *new,
				   const struct aa_task_ctx *old)
{
	*new = *old;
	aa_get_label(new->nnp);
	aa_get_label(new->previous);
	aa_get_label(new->onexec);
}

/**
 * aa_clear_task_ctx_trans - clear transition tracking info from the ctx
 * @ctx: task context to clear (NOT NULL)
 */
static inline void aa_clear_task_ctx_trans(struct aa_task_ctx *ctx)
{
	AA_BUG(!ctx);

	aa_put_label(ctx->previous);
	aa_put_label(ctx->onexec);
	ctx->previous = NULL;
	ctx->onexec = NULL;
	ctx->token = 0;
}

#endif /* __AA_TASK_H */
