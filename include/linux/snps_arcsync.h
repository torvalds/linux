/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __SNPS_ARCSYNC_H__
#define __SNPS_ARCSYNC_H__

#include <linux/device.h>
#include <linux/interrupt.h>

typedef irqreturn_t (*intr_callback_t)(int irq, void *data);

/**
 * struct arcsync_funcs - ARCSync control functions
 * @get_version: get ARCSync IP version
 * @get_has_pmu: get ARCSync has_pmu flag
 * @get_arcnet_id: get the index of ARCSync IP unit
 * @clk_ctrl: core clock enable/disable control
 * @power_ctrl: send a power up/down signal to the specified core
 * @reset: send a reset signal to the specified core
 * @start: send a start signal
 * @halt: send a halt signal
 * @set_ivt: set the interrupt vector table base address for the specified core
 * @get_status: get status of the specified core
 * @reset_cluster_group: reset the NPX L2 group or L1 slice group
 * @clk_ctrl_cluster_group: slice group clock enable/disable control
 * @power_ctrl_cluster_group: power up/down the NPX L2 group or L1 slice group
 * @set_interrupt_callback: set extra callback for ARCSync interrupt handler
 * @remove_interrupt_callback: remove extra callback for interrupt handler
 */
struct arcsync_funcs {
	int (*get_version)(struct device *dev);
	int (*get_has_pmu)(struct device *dev);
	int (*get_arcnet_id)(struct device *dev);
	int (*clk_ctrl)(struct device *dev, u32 clid, u32 cid, u32 val);
	int (*power_ctrl)(struct device *dev, u32 clid, u32 cid, u32 cmd);
	int (*reset)(struct device *dev, u32 clid, u32 cid, u32 cmd);
	int (*start)(struct device *dev, u32 clid, u32 cid);
	int (*halt)(struct device *dev, u32 clid, u32 cid);
	int (*set_ivt)(struct device *dev, u32 clid, u32 cid, phys_addr_t ivt_addr);
	int (*get_status)(struct device *dev, u32 clid, u32 cid);
	int (*reset_cluster_group)(struct device *dev, u32 clid, u32 grp, u32 cmd);
	int (*clk_ctrl_cluster_group)(struct device *dev, u32 clid, u32 grp, u32 cmd);
	int (*power_ctrl_cluster_group)(struct device *dev, u32 clid, u32 grp, u32 cmd);
	int (*set_interrupt_callback)(struct device *dev, u32 irq, intr_callback_t cb, void *data);
	int (*remove_interrupt_callback)(struct device *dev, u32 irq, void *data);
};

/* valid cmd arg values of the reset and reset_cluster_group funcs*/
#define ARCSYNC_RESET_DEASSERT	0x0
#define ARCSYNC_RESET_ASSERT	0x1

/* valid cmd arg values of the clkctrl and clk_ctrl_cluster_group funcs*/
#define ARCSYNC_CLK_DIS	0x0
#define ARCSYNC_CLK_EN	0x1

/* valid cmd arg values of the power_ctrl and power_ctrl_cluster_group funcs*/
#define ARCSYNC_POWER_UP	0x0
#define ARCSYNC_POWER_DOWN	0x1

/* valid grp arg values */
#define ARCSYNC_NPX_L1GRP0	0x00
#define ARCSYNC_NPX_L1GRP1	0x01
#define ARCSYNC_NPX_L1GRP2	0x02
#define ARCSYNC_NPX_L1GRP3	0x03
#define ARCSYNC_NPX_L2GRP	0x04

/* valid return values of the get_status func */
#define ARCSYNC_CORE_RUNNING	0x01
#define ARCSYNC_CORE_HALTED	0x02
#define ARCSYNC_CORE_POWERDOWN	0x04
#define ARCSYNC_CORE_SLEEPING	0x08

#define ARCSYNC_HOST_MAX_IRQS	16

struct device *arcsync_get_device_by_phandle(struct device_node *np,
					     const char *phandle_name);
const struct arcsync_funcs *arcsync_get_ctrl_fn(struct device *dev);

#endif /* __SNPS_ARCSYNC_H__ */
