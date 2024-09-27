/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#ifndef _IPE_FS_H
#define _IPE_FS_H

#include "policy.h"

extern struct dentry *policy_root __ro_after_init;

int ipe_new_policyfs_node(struct ipe_policy *p);
void ipe_del_policyfs_node(struct ipe_policy *p);

#endif /* _IPE_FS_H */
