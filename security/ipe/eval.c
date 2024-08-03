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
#include <linux/moduleparam.h>

#include "ipe.h"
#include "eval.h"
#include "policy.h"
#include "audit.h"

struct ipe_policy __rcu *ipe_active_policy;
bool success_audit;
bool enforce = true;

#define FILE_SUPERBLOCK(f) ((f)->f_path.mnt->mnt_sb)

/**
 * build_ipe_sb_ctx() - Build initramfs field of an ipe evaluation context.
 * @ctx: Supplies a pointer to the context to be populated.
 * @file: Supplies the file struct of the file triggered IPE event.
 */
static void build_ipe_sb_ctx(struct ipe_eval_ctx *ctx, const struct file *const file)
{
	ctx->initramfs = ipe_sb(FILE_SUPERBLOCK(file))->initramfs;
}

/**
 * ipe_build_eval_ctx() - Build an ipe evaluation context.
 * @ctx: Supplies a pointer to the context to be populated.
 * @file: Supplies a pointer to the file to associated with the evaluation.
 * @op: Supplies the IPE policy operation associated with the evaluation.
 * @hook: Supplies the LSM hook associated with the evaluation.
 */
void ipe_build_eval_ctx(struct ipe_eval_ctx *ctx,
			const struct file *file,
			enum ipe_op_type op,
			enum ipe_hook_type hook)
{
	ctx->file = file;
	ctx->op = op;
	ctx->hook = hook;

	if (file)
		build_ipe_sb_ctx(ctx, file);
}

/**
 * evaluate_boot_verified() - Evaluate @ctx for the boot verified property.
 * @ctx: Supplies a pointer to the context being evaluated.
 *
 * Return:
 * * %true	- The current @ctx match the @p
 * * %false	- The current @ctx doesn't match the @p
 */
static bool evaluate_boot_verified(const struct ipe_eval_ctx *const ctx)
{
	return ctx->initramfs;
}

/**
 * evaluate_property() - Analyze @ctx against a rule property.
 * @ctx: Supplies a pointer to the context to be evaluated.
 * @p: Supplies a pointer to the property to be evaluated.
 *
 * This function Determines whether the specified @ctx
 * matches the conditions defined by a rule property @p.
 *
 * Return:
 * * %true	- The current @ctx match the @p
 * * %false	- The current @ctx doesn't match the @p
 */
static bool evaluate_property(const struct ipe_eval_ctx *const ctx,
			      struct ipe_prop *p)
{
	switch (p->type) {
	case IPE_PROP_BOOT_VERIFIED_FALSE:
		return !evaluate_boot_verified(ctx);
	case IPE_PROP_BOOT_VERIFIED_TRUE:
		return evaluate_boot_verified(ctx);
	default:
		return false;
	}
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
	enum ipe_match match_type;
	bool match = false;
	int rc = 0;

	rcu_read_lock();

	pol = rcu_dereference(ipe_active_policy);
	if (!pol) {
		rcu_read_unlock();
		return 0;
	}

	if (ctx->op == IPE_OP_INVALID) {
		if (pol->parsed->global_default_action == IPE_ACTION_INVALID) {
			WARN(1, "no default rule set for unknown op, ALLOW it");
			action = IPE_ACTION_ALLOW;
		} else {
			action = pol->parsed->global_default_action;
		}
		match_type = IPE_MATCH_GLOBAL;
		goto eval;
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

	if (match) {
		action = rule->action;
		match_type = IPE_MATCH_RULE;
	} else if (rules->default_action != IPE_ACTION_INVALID) {
		action = rules->default_action;
		match_type = IPE_MATCH_TABLE;
	} else {
		action = pol->parsed->global_default_action;
		match_type = IPE_MATCH_GLOBAL;
	}

eval:
	ipe_audit_match(ctx, match_type, action, rule);
	rcu_read_unlock();

	if (action == IPE_ACTION_DENY)
		rc = -EACCES;

	if (!READ_ONCE(enforce))
		rc = 0;

	return rc;
}

/* Set the right module name */
#ifdef KBUILD_MODNAME
#undef KBUILD_MODNAME
#define KBUILD_MODNAME "ipe"
#endif

module_param(success_audit, bool, 0400);
MODULE_PARM_DESC(success_audit, "Start IPE with success auditing enabled");
module_param(enforce, bool, 0400);
MODULE_PARM_DESC(enforce, "Start IPE in enforce or permissive mode");
