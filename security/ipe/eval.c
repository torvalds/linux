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
#include <linux/fsverity.h>

#include "ipe.h"
#include "eval.h"
#include "policy.h"
#include "audit.h"
#include "digest.h"

struct ipe_policy __rcu *ipe_active_policy;
bool success_audit;
bool enforce = true;
#define INO_BLOCK_DEV(ino) ((ino)->i_sb->s_bdev)

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

#ifdef CONFIG_IPE_PROP_DM_VERITY
/**
 * build_ipe_bdev_ctx() - Build ipe_bdev field of an evaluation context.
 * @ctx: Supplies a pointer to the context to be populated.
 * @ino: Supplies the inode struct of the file triggered IPE event.
 */
static void build_ipe_bdev_ctx(struct ipe_eval_ctx *ctx, const struct inode *const ino)
{
	if (INO_BLOCK_DEV(ino))
		ctx->ipe_bdev = ipe_bdev(INO_BLOCK_DEV(ino));
}
#else
static void build_ipe_bdev_ctx(struct ipe_eval_ctx *ctx, const struct inode *const ino)
{
}
#endif /* CONFIG_IPE_PROP_DM_VERITY */

#ifdef CONFIG_IPE_PROP_FS_VERITY
#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
static void build_ipe_inode_blob_ctx(struct ipe_eval_ctx *ctx,
				     const struct inode *const ino)
{
	ctx->ipe_inode = ipe_inode(ctx->ino);
}
#else
static inline void build_ipe_inode_blob_ctx(struct ipe_eval_ctx *ctx,
					    const struct inode *const ino)
{
}
#endif /* CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */

/**
 * build_ipe_inode_ctx() - Build inode fields of an evaluation context.
 * @ctx: Supplies a pointer to the context to be populated.
 * @ino: Supplies the inode struct of the file triggered IPE event.
 */
static void build_ipe_inode_ctx(struct ipe_eval_ctx *ctx, const struct inode *const ino)
{
	ctx->ino = ino;
	build_ipe_inode_blob_ctx(ctx, ino);
}
#else
static void build_ipe_inode_ctx(struct ipe_eval_ctx *ctx, const struct inode *const ino)
{
}
#endif /* CONFIG_IPE_PROP_FS_VERITY */

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
	struct inode *ino;

	ctx->file = file;
	ctx->op = op;
	ctx->hook = hook;

	if (file) {
		build_ipe_sb_ctx(ctx, file);
		ino = d_real_inode(file->f_path.dentry);
		build_ipe_bdev_ctx(ctx, ino);
		build_ipe_inode_ctx(ctx, ino);
	}
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

#ifdef CONFIG_IPE_PROP_DM_VERITY
/**
 * evaluate_dmv_roothash() - Evaluate @ctx against a dmv roothash property.
 * @ctx: Supplies a pointer to the context being evaluated.
 * @p: Supplies a pointer to the property being evaluated.
 *
 * Return:
 * * %true	- The current @ctx match the @p
 * * %false	- The current @ctx doesn't match the @p
 */
static bool evaluate_dmv_roothash(const struct ipe_eval_ctx *const ctx,
				  struct ipe_prop *p)
{
	return !!ctx->ipe_bdev &&
	       !!ctx->ipe_bdev->root_hash &&
	       ipe_digest_eval(p->value,
			       ctx->ipe_bdev->root_hash);
}
#else
static bool evaluate_dmv_roothash(const struct ipe_eval_ctx *const ctx,
				  struct ipe_prop *p)
{
	return false;
}
#endif /* CONFIG_IPE_PROP_DM_VERITY */

#ifdef CONFIG_IPE_PROP_DM_VERITY_SIGNATURE
/**
 * evaluate_dmv_sig_false() - Evaluate @ctx against a dmv sig false property.
 * @ctx: Supplies a pointer to the context being evaluated.
 *
 * Return:
 * * %true	- The current @ctx match the property
 * * %false	- The current @ctx doesn't match the property
 */
static bool evaluate_dmv_sig_false(const struct ipe_eval_ctx *const ctx)
{
	return !ctx->ipe_bdev || (!ctx->ipe_bdev->dm_verity_signed);
}

/**
 * evaluate_dmv_sig_true() - Evaluate @ctx against a dmv sig true property.
 * @ctx: Supplies a pointer to the context being evaluated.
 *
 * Return:
 * * %true	- The current @ctx match the property
 * * %false	- The current @ctx doesn't match the property
 */
static bool evaluate_dmv_sig_true(const struct ipe_eval_ctx *const ctx)
{
	return !evaluate_dmv_sig_false(ctx);
}
#else
static bool evaluate_dmv_sig_false(const struct ipe_eval_ctx *const ctx)
{
	return false;
}

static bool evaluate_dmv_sig_true(const struct ipe_eval_ctx *const ctx)
{
	return false;
}
#endif /* CONFIG_IPE_PROP_DM_VERITY_SIGNATURE */

#ifdef CONFIG_IPE_PROP_FS_VERITY
/**
 * evaluate_fsv_digest() - Evaluate @ctx against a fsv digest property.
 * @ctx: Supplies a pointer to the context being evaluated.
 * @p: Supplies a pointer to the property being evaluated.
 *
 * Return:
 * * %true	- The current @ctx match the @p
 * * %false	- The current @ctx doesn't match the @p
 */
static bool evaluate_fsv_digest(const struct ipe_eval_ctx *const ctx,
				struct ipe_prop *p)
{
	enum hash_algo alg;
	u8 digest[FS_VERITY_MAX_DIGEST_SIZE];
	struct digest_info info;

	if (!ctx->ino)
		return false;
	if (!fsverity_get_digest((struct inode *)ctx->ino,
				 digest,
				 NULL,
				 &alg))
		return false;

	info.alg = hash_algo_name[alg];
	info.digest = digest;
	info.digest_len = hash_digest_size[alg];

	return ipe_digest_eval(p->value, &info);
}
#else
static bool evaluate_fsv_digest(const struct ipe_eval_ctx *const ctx,
				struct ipe_prop *p)
{
	return false;
}
#endif /* CONFIG_IPE_PROP_FS_VERITY */

#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
/**
 * evaluate_fsv_sig_false() - Evaluate @ctx against a fsv sig false property.
 * @ctx: Supplies a pointer to the context being evaluated.
 *
 * Return:
 * * %true	- The current @ctx match the property
 * * %false	- The current @ctx doesn't match the property
 */
static bool evaluate_fsv_sig_false(const struct ipe_eval_ctx *const ctx)
{
	return !ctx->ino ||
	       !IS_VERITY(ctx->ino) ||
	       !ctx->ipe_inode ||
	       !ctx->ipe_inode->fs_verity_signed;
}

/**
 * evaluate_fsv_sig_true() - Evaluate @ctx against a fsv sig true property.
 * @ctx: Supplies a pointer to the context being evaluated.
 *
 * Return:
 * * %true - The current @ctx match the property
 * * %false - The current @ctx doesn't match the property
 */
static bool evaluate_fsv_sig_true(const struct ipe_eval_ctx *const ctx)
{
	return !evaluate_fsv_sig_false(ctx);
}
#else
static bool evaluate_fsv_sig_false(const struct ipe_eval_ctx *const ctx)
{
	return false;
}

static bool evaluate_fsv_sig_true(const struct ipe_eval_ctx *const ctx)
{
	return false;
}
#endif /* CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */

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
	case IPE_PROP_DMV_ROOTHASH:
		return evaluate_dmv_roothash(ctx, p);
	case IPE_PROP_DMV_SIG_FALSE:
		return evaluate_dmv_sig_false(ctx);
	case IPE_PROP_DMV_SIG_TRUE:
		return evaluate_dmv_sig_true(ctx);
	case IPE_PROP_FSV_DIGEST:
		return evaluate_fsv_digest(ctx, p);
	case IPE_PROP_FSV_SIG_FALSE:
		return evaluate_fsv_sig_false(ctx);
	case IPE_PROP_FSV_SIG_TRUE:
		return evaluate_fsv_sig_true(ctx);
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
