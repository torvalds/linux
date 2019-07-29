/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2018 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#ifndef __FIRMWARE_ZYNQMP_H__
#define __FIRMWARE_ZYNQMP_H__

#define ZYNQMP_PM_VERSION_MAJOR	1
#define ZYNQMP_PM_VERSION_MINOR	0

#define ZYNQMP_PM_VERSION	((ZYNQMP_PM_VERSION_MAJOR << 16) | \
					ZYNQMP_PM_VERSION_MINOR)

#define ZYNQMP_TZ_VERSION_MAJOR	1
#define ZYNQMP_TZ_VERSION_MINOR	0

#define ZYNQMP_TZ_VERSION	((ZYNQMP_TZ_VERSION_MAJOR << 16) | \
					ZYNQMP_TZ_VERSION_MINOR)

/* SMC SIP service Call Function Identifier Prefix */
#define PM_SIP_SVC			0xC2000000
#define PM_GET_TRUSTZONE_VERSION	0xa03

/* Number of 32bits values in payload */
#define PAYLOAD_ARG_CNT	4U

enum pm_api_id {
	PM_GET_API_VERSION = 1,
	PM_IOCTL = 34,
	PM_QUERY_DATA,
	PM_CLOCK_ENABLE,
	PM_CLOCK_DISABLE,
	PM_CLOCK_GETSTATE,
	PM_CLOCK_SETDIVIDER,
	PM_CLOCK_GETDIVIDER,
	PM_CLOCK_SETRATE,
	PM_CLOCK_GETRATE,
	PM_CLOCK_SETPARENT,
	PM_CLOCK_GETPARENT,
};

/* PMU-FW return status codes */
enum pm_ret_status {
	XST_PM_SUCCESS = 0,
	XST_PM_INTERNAL = 2000,
	XST_PM_CONFLICT,
	XST_PM_NO_ACCESS,
	XST_PM_INVALID_NODE,
	XST_PM_DOUBLE_REQ,
	XST_PM_ABORT_SUSPEND,
};

enum pm_ioctl_id {
	IOCTL_SET_PLL_FRAC_MODE = 8,
	IOCTL_GET_PLL_FRAC_MODE,
	IOCTL_SET_PLL_FRAC_DATA,
	IOCTL_GET_PLL_FRAC_DATA,
};

enum pm_query_id {
	PM_QID_INVALID,
	PM_QID_CLOCK_GET_NAME,
	PM_QID_CLOCK_GET_TOPOLOGY,
	PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS,
	PM_QID_CLOCK_GET_PARENTS,
	PM_QID_CLOCK_GET_ATTRIBUTES,
	PM_QID_CLOCK_GET_NUM_CLOCKS = 12,
};

/**
 * struct zynqmp_pm_query_data - PM query data
 * @qid:	query ID
 * @arg1:	Argument 1 of query data
 * @arg2:	Argument 2 of query data
 * @arg3:	Argument 3 of query data
 */
struct zynqmp_pm_query_data {
	u32 qid;
	u32 arg1;
	u32 arg2;
	u32 arg3;
};

struct zynqmp_eemi_ops {
	int (*get_api_version)(u32 *version);
	int (*query_data)(struct zynqmp_pm_query_data qdata, u32 *out);
	int (*clock_enable)(u32 clock_id);
	int (*clock_disable)(u32 clock_id);
	int (*clock_getstate)(u32 clock_id, u32 *state);
	int (*clock_setdivider)(u32 clock_id, u32 divider);
	int (*clock_getdivider)(u32 clock_id, u32 *divider);
	int (*clock_setrate)(u32 clock_id, u64 rate);
	int (*clock_getrate)(u32 clock_id, u64 *rate);
	int (*clock_setparent)(u32 clock_id, u32 parent_id);
	int (*clock_getparent)(u32 clock_id, u32 *parent_id);
	int (*ioctl)(u32 node_id, u32 ioctl_id, u32 arg1, u32 arg2, u32 *out);
};

#if IS_REACHABLE(CONFIG_ARCH_ZYNQMP)
const struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void);
#else
static inline struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void)
{
	return NULL;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_H__ */
