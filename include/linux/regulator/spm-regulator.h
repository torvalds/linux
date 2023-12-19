/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2014, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_REGULATOR_SPM_H
#define _LINUX_REGULATOR_SPM_H

#include <linux/err.h>
#include <linux/init.h>

#ifdef CONFIG_REGULATOR_SPM
int __init spm_regulator_init(void);
#else
static inline int __init spm_regulator_init(void) { return -ENODEV; }
#endif

#endif
