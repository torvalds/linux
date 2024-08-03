/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#ifndef _IPE_EVAL_H
#define _IPE_EVAL_H

#include <linux/file.h>
#include <linux/types.h>

#include "policy.h"

extern struct ipe_policy __rcu *ipe_active_policy;

struct ipe_eval_ctx {
	enum ipe_op_type op;

	const struct file *file;
};

int ipe_evaluate_event(const struct ipe_eval_ctx *const ctx);

#endif /* _IPE_EVAL_H */
