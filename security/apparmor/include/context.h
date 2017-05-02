/*
 * AppArmor security module
 *
 * This file contains AppArmor contexts used to associate "labels" to objects.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_CONTEXT_H
#define __AA_CONTEXT_H

#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "policy.h"
#include "policy_ns.h"

#define cred_ctx(X) ((X)->security)
#define current_ctx() cred_ctx(current_cred())

/* struct aa_file_ctx - the AppArmor context the file was opened in
 * @perms: the permission the file was opened with
 *
 * The file_ctx could currently be directly stored in file->f_security
 * as the profile reference is now stored in the f_cred.  However the
 * ctx struct will expand in the future so we keep the struct.
 */
struct aa_file_ctx {
	u16 allow;
};

/**
 * aa_alloc_file_context - allocate file_ctx
 * @gfp: gfp flags for allocation
 *
 * Returns: file_ctx or NULL on failure
 */
static inline struct aa_file_ctx *aa_alloc_file_context(gfp_t gfp)
{
	return kzalloc(sizeof(struct aa_file_ctx), gfp);
}

/**
 * aa_free_file_context - free a file_ctx
 * @ctx: file_ctx to free  (MAYBE_NULL)
 */
static inline void aa_free_file_context(struct aa_file_ctx *ctx)
{
	if (ctx)
		kzfree(ctx);
}

/**
 * struct aa_task_ctx - primary label for confined tasks
 * @profile: the current profile   (NOT NULL)
 * @exec: profile to transition to on next exec  (MAYBE NULL)
 * @previous: profile the task may return to     (MAYBE NULL)
 * @token: magic value the task must know for returning to @previous_profile
 *
 * Contains the task's current profile (which could change due to
 * change_hat).  Plus the hat_magic needed during change_hat.
 *
 * TODO: make so a task can be confined by a stack of contexts
 */
struct aa_task_ctx {
	struct aa_profile *profile;
	struct aa_profile *onexec;
	struct aa_profile *previous;
	u64 token;
};

struct aa_task_ctx *aa_alloc_task_context(gfp_t flags);
void aa_free_task_context(struct aa_task_ctx *ctx);
void aa_dup_task_context(struct aa_task_ctx *new,
			 const struct aa_task_ctx *old);
int aa_replace_current_profile(struct aa_profile *profile);
int aa_set_current_onexec(struct aa_profile *profile);
int aa_set_current_hat(struct aa_profile *profile, u64 token);
int aa_restore_previous_profile(u64 cookie);
struct aa_profile *aa_get_task_profile(struct task_struct *task);


/**
 * aa_cred_profile - obtain cred's profiles
 * @cred: cred to obtain profiles from  (NOT NULL)
 *
 * Returns: confining profile
 *
 * does NOT increment reference count
 */
static inline struct aa_profile *aa_cred_profile(const struct cred *cred)
{
	struct aa_task_ctx *ctx = cred_ctx(cred);

	AA_BUG(!ctx || !ctx->profile);
	return ctx->profile;
}

/**
 * __aa_task_profile - retrieve another task's profile
 * @task: task to query  (NOT NULL)
 *
 * Returns: @task's profile without incrementing its ref count
 *
 * If @task != current needs to be called in RCU safe critical section
 */
static inline struct aa_profile *__aa_task_profile(struct task_struct *task)
{
	return aa_cred_profile(__task_cred(task));
}

/**
 * __aa_task_is_confined - determine if @task has any confinement
 * @task: task to check confinement of  (NOT NULL)
 *
 * If @task != current needs to be called in RCU safe critical section
 */
static inline bool __aa_task_is_confined(struct task_struct *task)
{
	return !unconfined(__aa_task_profile(task));
}

/**
 * __aa_current_profile - find the current tasks confining profile
 *
 * Returns: up to date confining profile or the ns unconfined profile (NOT NULL)
 *
 * This fn will not update the tasks cred to the most up to date version
 * of the profile so it is safe to call when inside of locks.
 */
static inline struct aa_profile *__aa_current_profile(void)
{
	return aa_cred_profile(current_cred());
}

/**
 * aa_current_profile - find the current tasks confining profile and do updates
 *
 * Returns: up to date confining profile or the ns unconfined profile (NOT NULL)
 *
 * This fn will update the tasks cred structure if the profile has been
 * replaced.  Not safe to call inside locks
 */
static inline struct aa_profile *aa_current_profile(void)
{
	const struct aa_task_ctx *ctx = current_ctx();
	struct aa_profile *profile;

	AA_BUG(!ctx || !ctx->profile);

	if (profile_is_stale(ctx->profile)) {
		profile = aa_get_newest_profile(ctx->profile);
		aa_replace_current_profile(profile);
		aa_put_profile(profile);
		ctx = current_ctx();
	}

	return ctx->profile;
}

static inline struct aa_ns *aa_get_current_ns(void)
{
	return aa_get_ns(__aa_current_profile()->ns);
}

/**
 * aa_clear_task_ctx_trans - clear transition tracking info from the ctx
 * @ctx: task context to clear (NOT NULL)
 */
static inline void aa_clear_task_ctx_trans(struct aa_task_ctx *ctx)
{
	aa_put_profile(ctx->previous);
	aa_put_profile(ctx->onexec);
	ctx->previous = NULL;
	ctx->onexec = NULL;
	ctx->token = 0;
}

#endif /* __AA_CONTEXT_H */
