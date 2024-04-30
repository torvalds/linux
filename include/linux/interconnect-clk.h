/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, Linaro Ltd.
 */

#ifndef __LINUX_INTERCONNECT_CLK_H
#define __LINUX_INTERCONNECT_CLK_H

struct device;

struct icc_clk_data {
	struct clk *clk;
	const char *name;
	unsigned int master_id;
	unsigned int slave_id;
};

struct icc_provider *icc_clk_register(struct device *dev,
				      unsigned int first_id,
				      unsigned int num_clocks,
				      const struct icc_clk_data *data);
int devm_icc_clk_register(struct device *dev, unsigned int first_id,
			  unsigned int num_clocks, const struct icc_clk_data *data);
void icc_clk_unregister(struct icc_provider *provider);

#endif
