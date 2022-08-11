/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GUNYAH_RSC_MGR_H
#define _GUNYAH_RSC_MGR_H

#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/gunyah.h>

#define GH_VMID_INVAL	U16_MAX

struct gh_rm;
int gh_rm_notifier_register(struct gh_rm *rm, struct notifier_block *nb);
int gh_rm_notifier_unregister(struct gh_rm *rm, struct notifier_block *nb);
struct device *gh_rm_get(struct gh_rm *rm);
void gh_rm_put(struct gh_rm *rm);

#endif
