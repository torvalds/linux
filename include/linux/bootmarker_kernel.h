/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __BOOTMARKER_KERNEL_H_
#define __BOOTMARKER_KERNEL_H_

#include <linux/types.h>

int bootmarker_place_marker(const char *name);
#if IS_ENABLED(CONFIG_BOOTMARKER_PROXY)
struct bootmarker_drv_ops {
	int (*bootmarker_place_marker)(const char *name);
};
int provide_bootmarker_kernel_fun_ops(const struct bootmarker_drv_ops *ops);

#endif /*CONFIG_BOOTMARKER_PROXY*/
#endif /* __BOOTMARKER_KERNEL_H_ */
