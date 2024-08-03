// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>

#include "ipe.h"
#include "eval.h"
#include "policy.h"

struct ipe_policy __rcu *ipe_active_policy;

/**
 * ipe_build_eval_ctx() - Build an ipe evaluation context.
 * @ctx: Supplies a pointer to the context to be populated.
 * @file: Supplies a pointer to the file to associated with the evaluation.
 * @op: Supplies the IPE policy operation associated with the evaluation.
 */
void ipe_build_eval_ctx(struct ipe_eval_ctx *ctx,
			const struct file *file,
			enum ipe_op_type op)
{
	ctx->file = file;
	ctx->op = op;
}

/**
 * evaluate_property() - Analyze @ctx against a rule property.
 * @ctx: Supplies a pointer to the context to be evaluated.
 * @p: Supplies a pointer to the property to be evaluated.
 *
 * This is a placeholder. The actual function will be introduced in the
 * latter commits.
 *
 * Return:
 * * %true	- The current @ctx match the @p
 * * %false	- The current @ctx doesn't match the @p
 */
static bool evaluate_property(const struct ipe_eval_ctx *const ctx,
			      struct ipe_prop *p)
{
	return false;
}

/**
 * ipe_evaluate_event() - Analyze @ctx against the current active policy.
 * @ctx: Supplies a pointer to the context to be evaluated.
 *
 * This is the loop where all policy evaluations happen against the IPE policy.
 *
 * Return:
 * * %0		- Success
 * * %-EACCES	- @ctx did not pass evaluation
 */
int ipe_evaluate_event(const struct ipe_eval_ctx *const ctx)
{
	const struct ipe_op_table *rules = NULL;
	const struct ipe_rule *rule = NULL;
	struct ipe_policy *pol = NULL;
	struct ipe_prop *prop = NULL;
	enum ipe_action_type action;
	bool match = false;

	rcu_read_lock();

	pol = rcu_dereference(ipe_active_policy);
	if (!pol) {
		rcu_read_unlock();
		return 0;
	}

	if (ctx->op == IPE_OP_INVALID) {
		if (pol->parsed->global_default_action == IPE_ACTION_DENY) {
			rcu_read_unlock();
			return -EACCES;
		}
		if (pol->parsed->global_default_action == IPE_ACTION_INVALID)
			WARN(1, "no default rule set for unknown op, ALLOW it");
		rcu_read_unlock();
		return 0;
	}

	rules = &pol->parsed->rules[ctx->op];

	list_for_each_entry(rule, &rules->rules, next) {
		match = true;

		list_for_each_entry(prop, &rule->props, next) {
			match = evaluate_property(ctx, prop);
			if (!match)
				break;
		}

		if (match)
			break;
	}

	if (match)
		action = rule->action;
	else if (rules->default_action != IPE_ACTION_INVALID)
		action = rules->default_action;
	else
		action = pol->parsed->global_default_action;

	rcu_read_unlock();
	if (action == IPE_ACTION_DENY)
		return -EACCES;

	return 0;
}
