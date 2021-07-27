/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for Altera Partial Reconfiguration IP Core
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Based on socfpga-a10.c Copyright (C) 2015-2016 Altera Corporation
 *  by Alan Tull <atull@opensource.altera.com>
 */

#ifndef _ALT_PR_IP_CORE_H
#define _ALT_PR_IP_CORE_H
#include <linux/io.h>

int alt_pr_register(struct device *dev, void __iomem *reg_base);

#endif /* _ALT_PR_IP_CORE_H */
