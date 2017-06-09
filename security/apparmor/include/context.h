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
 * aa_cred_raw_profile - obtain cred's profiles
 * @cred: cred to obtain profiles from  (NOT NULL)
 *
 * Returns: confining profile
 *
 * does NOT increment reference count
 */
static inline struct aa_profile *aa_cred_raw_profile(const struct cred *cred)
{
	struct aa_task_ctx *ctx = cred_ctx(cred);

	AA_BUG(!ctx || !ctx->profile);
	return ctx->profile;
}

/**
 * aa_get_newest_cred_profile - obtain the newest profile on a cred
 * @cred: cred to obtain profile from (NOT NULL)
 *
 * Returns: newest version of confining profile
 */
static inline
struct aa_profile *aa_get_newest_cred_profile(const struct cred *cred)
{
	return aa_get_newest_profile(aa_cred_raw_profile(cred));
}

/**
 * __aa_task_raw_profile - retrieve another task's profile
 * @task: task to query  (NOT NULL)
 *
 * Returns: @task's profile without incrementing its ref count
 *
 * If @task != current needs to be called in RCU safe critical section
 */
static inline struct aa_profile *__aa_task_raw_profile(struct task_struct *task)
{
	return aa_cred_raw_profile(__task_cred(task));
}

/**
 * __aa_task_is_confined - determine if @task has any confinement
 * @task: task to check confinement of  (NOT NULL)
 *
 * If @task != current needs to be called in RCU safe critical section
 */
static inline bool __aa_task_is_confined(struct task_struct *task)
{
	return !unconfined(__aa_task_raw_profile(task));
}

/**
 * aa_current_raw_profile - find the current tasks confining profile
 *
 * Returns: up to date confining profile or the ns unconfined profile (NOT NULL)
 *
 * This fn will not update the tasks cred to the most up to date version
 * of the profile so it is safe to call when inside of locks.
 */
static inline struct aa_profile *aa_current_raw_profile(void)
{
	return aa_cred_raw_profile(current_cred());
}

/**
 * aa_get_current_profile - get the newest version of the current tasks profile
 *
 * Returns: newest version of confining profile (NOT NULL)
 *
 * This fn will not update the tasks cred, so it is safe inside of locks
 *
 * The returned reference must be put with aa_put_profile()
 */
static inline struct aa_profile *aa_get_current_profile(void)
{
	struct aa_profile *p = aa_current_raw_profile();

	if (profile_is_stale(p))
		return aa_get_newest_profile(p);
	return aa_get_profile(p);
}

#define __end_current_profile_crit_section(X) \
	end_current_profile_crit_section(X)

/**
 * end_profile_crit_section - put a reference found with begin_current_profile..
 * @profile: profile reference to put
 *
 * Should only be used with a reference obtained with
 * begin_current_profile_crit_section and never used in situations where the
 * task cred may be updated
 */
static inline void end_current_profile_crit_section(struct aa_profile *profile)
{
	if (profile != aa_current_raw_profile())
		aa_put_profile(profile);
}

/**
 * __begin_current_profile_crit_section - current's confining profile
 *
 * Returns: up to date confining profile or the ns unconfined profile (NOT NULL)
 *
 * safe to call inside locks
 *
 * The returned reference must be put with __end_current_profile_crit_section()
 * This must NOT be used if the task cred could be updated within the
 * critical section between __begin_current_profile_crit_section() ..
 * __end_current_profile_crit_section()
 */
static inline struct aa_profile *__begin_current_profile_crit_section(void)
{
	struct aa_profile *profile = aa_current_raw_profile();

	if (profile_is_stale(profile))
		profile = aa_get_newest_profile(profile);

	return profile;
}

/**
 * begin_current_profile_crit_section - current's profile and update if needed
 *
 * Returns: up to date confining profile or the ns unconfined profile (NOT NULL)
 *
 * Not safe to call inside locks
 *
 * The returned reference must be put with end_current_profile_crit_section()
 * This must NOT be used if the task cred could be updated within the
 * critical section between begin_current_profile_crit_section() ..
 * end_current_profile_crit_section()
 */
static inline struct aa_profile *begin_current_profile_crit_section(void)
{
	struct aa_profile *profile = aa_current_raw_profile();

	if (profile_is_stale(profile)) {
		profile = aa_get_newest_profile(profile);
		if (aa_replace_current_profile(profile) == 0)
			/* task cred will keep the reference */
			aa_put_profile(profile);
	}

	return profile;
}

static inline struct aa_ns *aa_get_current_ns(void)
{
	struct aa_profile *profile;
	struct aa_ns *ns;

	profile  = __begin_current_profile_crit_section();
	ns = aa_get_ns(profile->ns);
	__end_current_profile_crit_section(profile);

	return ns;
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
