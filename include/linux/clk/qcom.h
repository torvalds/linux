/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_CLK_QCOM_H_
#define __LINUX_CLK_QCOM_H_

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <soc/qcom/crm.h>

enum branch_mem_flags {
	CLKFLAG_RETAIN_PERIPH,
	CLKFLAG_NORETAIN_PERIPH,
	CLKFLAG_RETAIN_MEM,
	CLKFLAG_NORETAIN_MEM,
	CLKFLAG_PERIPH_OFF_SET,
	CLKFLAG_PERIPH_OFF_CLEAR,
};

int qcom_clk_get_voltage(struct clk *clk, unsigned long rate);
int qcom_clk_set_flags(struct clk *clk, unsigned long flags);
void qcom_clk_dump(struct clk *clk, struct regulator *regulator,
		   bool calltrace);
void qcom_clk_bulk_dump(int num_clks, struct clk_bulk_data *clks,
			struct regulator *regulator, bool calltrace);
int qcom_clk_crm_set_rate(struct clk *clk,
			  enum crm_drv_type client_type, u32 client_idx,
			  u32 pwr_st, unsigned long rate);
#endif  /* __LINUX_CLK_QCOM_H_ */
