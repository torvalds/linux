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

#include "label.h"
#include "policy_ns.h"

#define cred_ctx(X) ((X)->security)
#define current_ctx() cred_ctx(current_cred())

/**
 * struct aa_task_ctx - primary label for confined tasks
 * @label: the current label   (NOT NULL)
 * @exec: label to transition to on next exec  (MAYBE NULL)
 * @previous: label the task may return to     (MAYBE NULL)
 * @token: magic value the task must know for returning to @previous
 *
 * Contains the task's current label (which could change due to
 * change_hat).  Plus the hat_magic needed during change_hat.
 *
 * TODO: make so a task can be confined by a stack of contexts
 */
struct aa_task_ctx {
	struct aa_label *label;
	struct aa_label *onexec;
	struct aa_label *previous;
	u64 token;
};

struct aa_task_ctx *aa_alloc_task_context(gfp_t flags);
void aa_free_task_context(struct aa_task_ctx *ctx);
void aa_dup_task_context(struct aa_task_ctx *new,
			 const struct aa_task_ctx *old);
int aa_replace_current_label(struct aa_label *label);
int aa_set_current_onexec(struct aa_label *label, bool stack);
int aa_set_current_hat(struct aa_label *label, u64 token);
int aa_restore_previous_label(u64 cookie);
struct aa_label *aa_get_task_label(struct task_struct *task);


/**
 * aa_cred_raw_label - obtain cred's label
 * @cred: cred to obtain label from  (NOT NULL)
 *
 * Returns: confining label
 *
 * does NOT increment reference count
 */
static inline struct aa_label *aa_cred_raw_label(const struct cred *cred)
{
	struct aa_task_ctx *ctx = cred_ctx(cred);

	AA_BUG(!ctx || !ctx->label);
	return ctx->label;
}

/**
 * aa_get_newest_cred_label - obtain the newest label on a cred
 * @cred: cred to obtain label from (NOT NULL)
 *
 * Returns: newest version of confining label
 */
static inline struct aa_label *aa_get_newest_cred_label(const struct cred *cred)
{
	return aa_get_newest_label(aa_cred_raw_label(cred));
}

/**
 * __aa_task_raw_label - retrieve another task's label
 * @task: task to query  (NOT NULL)
 *
 * Returns: @task's label without incrementing its ref count
 *
 * If @task != current needs to be called in RCU safe critical section
 */
static inline struct aa_label *__aa_task_raw_label(struct task_struct *task)
{
	return aa_cred_raw_label(__task_cred(task));
}

/**
 * __aa_task_is_confined - determine if @task has any confinement
 * @task: task to check confinement of  (NOT NULL)
 *
 * If @task != current needs to be called in RCU safe critical section
 */
static inline bool __aa_task_is_confined(struct task_struct *task)
{
	return !unconfined(__aa_task_raw_label(task));
}

/**
 * aa_current_raw_label - find the current tasks confining label
 *
 * Returns: up to date confining label or the ns unconfined label (NOT NULL)
 *
 * This fn will not update the tasks cred to the most up to date version
 * of the label so it is safe to call when inside of locks.
 */
static inline struct aa_label *aa_current_raw_label(void)
{
	return aa_cred_raw_label(current_cred());
}

/**
 * aa_get_current_label - get the newest version of the current tasks label
 *
 * Returns: newest version of confining label (NOT NULL)
 *
 * This fn will not update the tasks cred, so it is safe inside of locks
 *
 * The returned reference must be put with aa_put_label()
 */
static inline struct aa_label *aa_get_current_label(void)
{
	struct aa_label *l = aa_current_raw_label();

	if (label_is_stale(l))
		return aa_get_newest_label(l);
	return aa_get_label(l);
}

#define __end_current_label_crit_section(X) end_current_label_crit_section(X)

/**
 * end_label_crit_section - put a reference found with begin_current_label..
 * @label: label reference to put
 *
 * Should only be used with a reference obtained with
 * begin_current_label_crit_section and never used in situations where the
 * task cred may be updated
 */
static inline void end_current_label_crit_section(struct aa_label *label)
{
	if (label != aa_current_raw_label())
		aa_put_label(label);
}

/**
 * __begin_current_label_crit_section - current's confining label
 *
 * Returns: up to date confining label or the ns unconfined label (NOT NULL)
 *
 * safe to call inside locks
 *
 * The returned reference must be put with __end_current_label_crit_section()
 * This must NOT be used if the task cred could be updated within the
 * critical section between __begin_current_label_crit_section() ..
 * __end_current_label_crit_section()
 */
static inline struct aa_label *__begin_current_label_crit_section(void)
{
	struct aa_label *label = aa_current_raw_label();

	if (label_is_stale(label))
		label = aa_get_newest_label(label);

	return label;
}

/**
 * begin_current_label_crit_section - current's confining label and update it
 *
 * Returns: up to date confining label or the ns unconfined label (NOT NULL)
 *
 * Not safe to call inside locks
 *
 * The returned reference must be put with end_current_label_crit_section()
 * This must NOT be used if the task cred could be updated within the
 * critical section between begin_current_label_crit_section() ..
 * end_current_label_crit_section()
 */
static inline struct aa_label *begin_current_label_crit_section(void)
{
	struct aa_label *label = aa_current_raw_label();

	if (label_is_stale(label)) {
		label = aa_get_newest_label(label);
		if (aa_replace_current_label(label) == 0)
			/* task cred will keep the reference */
			aa_put_label(label);
	}

	return label;
}

static inline struct aa_ns *aa_get_current_ns(void)
{
	struct aa_label *label;
	struct aa_ns *ns;

	label  = __begin_current_label_crit_section();
	ns = aa_get_ns(labels_ns(label));
	__end_current_label_crit_section(label);

	return ns;
}

/**
 * aa_clear_task_ctx_trans - clear transition tracking info from the ctx
 * @ctx: task context to clear (NOT NULL)
 */
static inline void aa_clear_task_ctx_trans(struct aa_task_ctx *ctx)
{
	aa_put_label(ctx->previous);
	aa_put_label(ctx->onexec);
	ctx->previous = NULL;
	ctx->onexec = NULL;
	ctx->token = 0;
}

#endif /* __AA_CONTEXT_H */
