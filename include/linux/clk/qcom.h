/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#ifndef __LINUX_CLK_QCOM_H_
#define __LINUX_CLK_QCOM_H_

#include <linux/clk-provider.h>

int qcom_clk_get_voltage(struct clk *clk, unsigned long rate);

#endif  /* __LINUX_CLK_QCOM_H_ */
