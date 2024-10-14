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
extern bool enforce;

struct ipe_superblock {
	bool initramfs;
};

#ifdef CONFIG_IPE_PROP_DM_VERITY
struct ipe_bdev {
#ifdef CONFIG_IPE_PROP_DM_VERITY_SIGNATURE
	bool dm_verity_signed;
#endif /* CONFIG_IPE_PROP_DM_VERITY_SIGNATURE */
	struct digest_info *root_hash;
};
#endif /* CONFIG_IPE_PROP_DM_VERITY */

#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
struct ipe_inode {
	bool fs_verity_signed;
};
#endif /* CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */

struct ipe_eval_ctx {
	enum ipe_op_type op;
	enum ipe_hook_type hook;

	const struct file *file;
	bool initramfs;
#ifdef CONFIG_IPE_PROP_DM_VERITY
	const struct ipe_bdev *ipe_bdev;
#endif /* CONFIG_IPE_PROP_DM_VERITY */
#ifdef CONFIG_IPE_PROP_FS_VERITY
	const struct inode *ino;
#endif /* CONFIG_IPE_PROP_FS_VERITY */
#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
	const struct ipe_inode *ipe_inode;
#endif /* CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */
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
