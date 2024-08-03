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

#endif /* _IPE_H */
