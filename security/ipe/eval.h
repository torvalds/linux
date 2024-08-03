/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#ifndef _IPE_EVAL_H
#define _IPE_EVAL_H

#include <linux/file.h>
#include <linux/types.h>

#include "policy.h"
#include "hooks.h"

#define IPE_EVAL_CTX_INIT ((struct ipe_eval_ctx){ 0 })

extern struct ipe_policy __rcu *ipe_active_policy;
extern bool success_audit;

struct ipe_superblock {
	bool initramfs;
};

struct ipe_eval_ctx {
	enum ipe_op_type op;
	enum ipe_hook_type hook;

	const struct file *file;
	bool initramfs;
};

enum ipe_match {
	IPE_MATCH_RULE = 0,
	IPE_MATCH_TABLE,
	IPE_MATCH_GLOBAL,
	__IPE_MATCH_MAX
};

void ipe_build_eval_ctx(struct ipe_eval_ctx *ctx,
			const struct file *file,
			enum ipe_op_type op,
			enum ipe_hook_type hook);
int ipe_evaluate_event(const struct ipe_eval_ctx *const ctx);

#endif /* _IPE_EVAL_H */
