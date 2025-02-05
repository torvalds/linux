/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#ifndef _IPE_H
#define _IPE_H

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ipe: " fmt

#include <linux/lsm_hooks.h>
struct ipe_superblock *ipe_sb(const struct super_block *sb);

extern bool ipe_enabled;

#ifdef CONFIG_IPE_PROP_DM_VERITY
struct ipe_bdev *ipe_bdev(struct block_device *b);
#endif /* CONFIG_IPE_PROP_DM_VERITY */
#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
struct ipe_inode *ipe_inode(const struct inode *inode);
#endif /* CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */

#endif /* _IPE_H */
