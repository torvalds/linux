/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2019 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#ifndef __FIRMWARE_ZYNQMP_H__
#define __FIRMWARE_ZYNQMP_H__

#include <linux/err.h>

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
#define PM_SET_SUSPEND_MODE		0xa02
#define GET_CALLBACK_DATA		0xa01

/* Number of 32bits values in payload */
#define PAYLOAD_ARG_CNT	4U

/* Number of arguments for a callback */
#define CB_ARG_CNT     4

/* Payload size (consists of callback API ID + arguments) */
#define CB_PAYLOAD_SIZE (CB_ARG_CNT + 1)

#define ZYNQMP_PM_MAX_QOS		100U

#define GSS_NUM_REGS	(4)

/* Node capabilities */
#define	ZYNQMP_PM_CAPABILITY_ACCESS	0x1U
#define	ZYNQMP_PM_CAPABILITY_CONTEXT	0x2U
#define	ZYNQMP_PM_CAPABILITY_WAKEUP	0x4U
#define	ZYNQMP_PM_CAPABILITY_UNUSABLE	0x8U

/*
 * Firmware FPGA Manager flags
 * XILINX_ZYNQMP_PM_FPGA_FULL:	FPGA full reconfiguration
 * XILINX_ZYNQMP_PM_FPGA_PARTIAL: FPGA partial reconfiguration
 */
#define XILINX_ZYNQMP_PM_FPGA_FULL	0x0U
#define XILINX_ZYNQMP_PM_FPGA_PARTIAL	BIT(0)

enum pm_api_id {
	PM_GET_API_VERSION = 1,
	PM_SYSTEM_SHUTDOWN = 12,
	PM_REQUEST_NODE = 13,
	PM_RELEASE_NODE,
	PM_SET_REQUIREMENT,
	PM_RESET_ASSERT = 17,
	PM_RESET_GET_STATUS,
	PM_PM_INIT_FINALIZE = 21,
	PM_FPGA_LOAD,
	PM_FPGA_GET_STATUS,
	PM_GET_CHIPID = 24,
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
	PM_SECURE_AES = 47,
	PM_FEATURE_CHECK = 63,
	PM_API_MAX,
};

/* PMU-FW return status codes */
enum pm_ret_status {
	XST_PM_SUCCESS = 0,
	XST_PM_NO_FEATURE = 19,
	XST_PM_INTERNAL = 2000,
	XST_PM_CONFLICT,
	XST_PM_NO_ACCESS,
	XST_PM_INVALID_NODE,
	XST_PM_DOUBLE_REQ,
	XST_PM_ABORT_SUSPEND,
	XST_PM_MULT_USER = 2008,
};

enum pm_ioctl_id {
	IOCTL_SD_DLL_RESET = 6,
	IOCTL_SET_SD_TAPDELAY,
	IOCTL_SET_PLL_FRAC_MODE,
	IOCTL_GET_PLL_FRAC_MODE,
	IOCTL_SET_PLL_FRAC_DATA,
	IOCTL_GET_PLL_FRAC_DATA,
	IOCTL_WRITE_GGS = 12,
	IOCTL_READ_GGS = 13,
	IOCTL_WRITE_PGGS = 14,
	IOCTL_READ_PGGS = 15,
	/* Set healthy bit value */
	IOCTL_SET_BOOT_HEALTH_STATUS = 17,
};

enum pm_query_id {
	PM_QID_INVALID,
	PM_QID_CLOCK_GET_NAME,
	PM_QID_CLOCK_GET_TOPOLOGY,
	PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS,
	PM_QID_CLOCK_GET_PARENTS,
	PM_QID_CLOCK_GET_ATTRIBUTES,
	PM_QID_CLOCK_GET_NUM_CLOCKS = 12,
	PM_QID_CLOCK_GET_MAX_DIVISOR,
};

enum zynqmp_pm_reset_action {
	PM_RESET_ACTION_RELEASE,
	PM_RESET_ACTION_ASSERT,
	PM_RESET_ACTION_PULSE,
};

enum zynqmp_pm_reset {
	ZYNQMP_PM_RESET_START = 1000,
	ZYNQMP_PM_RESET_PCIE_CFG = ZYNQMP_PM_RESET_START,
	ZYNQMP_PM_RESET_PCIE_BRIDGE,
	ZYNQMP_PM_RESET_PCIE_CTRL,
	ZYNQMP_PM_RESET_DP,
	ZYNQMP_PM_RESET_SWDT_CRF,
	ZYNQMP_PM_RESET_AFI_FM5,
	ZYNQMP_PM_RESET_AFI_FM4,
	ZYNQMP_PM_RESET_AFI_FM3,
	ZYNQMP_PM_RESET_AFI_FM2,
	ZYNQMP_PM_RESET_AFI_FM1,
	ZYNQMP_PM_RESET_AFI_FM0,
	ZYNQMP_PM_RESET_GDMA,
	ZYNQMP_PM_RESET_GPU_PP1,
	ZYNQMP_PM_RESET_GPU_PP0,
	ZYNQMP_PM_RESET_GPU,
	ZYNQMP_PM_RESET_GT,
	ZYNQMP_PM_RESET_SATA,
	ZYNQMP_PM_RESET_ACPU3_PWRON,
	ZYNQMP_PM_RESET_ACPU2_PWRON,
	ZYNQMP_PM_RESET_ACPU1_PWRON,
	ZYNQMP_PM_RESET_ACPU0_PWRON,
	ZYNQMP_PM_RESET_APU_L2,
	ZYNQMP_PM_RESET_ACPU3,
	ZYNQMP_PM_RESET_ACPU2,
	ZYNQMP_PM_RESET_ACPU1,
	ZYNQMP_PM_RESET_ACPU0,
	ZYNQMP_PM_RESET_DDR,
	ZYNQMP_PM_RESET_APM_FPD,
	ZYNQMP_PM_RESET_SOFT,
	ZYNQMP_PM_RESET_GEM0,
	ZYNQMP_PM_RESET_GEM1,
	ZYNQMP_PM_RESET_GEM2,
	ZYNQMP_PM_RESET_GEM3,
	ZYNQMP_PM_RESET_QSPI,
	ZYNQMP_PM_RESET_UART0,
	ZYNQMP_PM_RESET_UART1,
	ZYNQMP_PM_RESET_SPI0,
	ZYNQMP_PM_RESET_SPI1,
	ZYNQMP_PM_RESET_SDIO0,
	ZYNQMP_PM_RESET_SDIO1,
	ZYNQMP_PM_RESET_CAN0,
	ZYNQMP_PM_RESET_CAN1,
	ZYNQMP_PM_RESET_I2C0,
	ZYNQMP_PM_RESET_I2C1,
	ZYNQMP_PM_RESET_TTC0,
	ZYNQMP_PM_RESET_TTC1,
	ZYNQMP_PM_RESET_TTC2,
	ZYNQMP_PM_RESET_TTC3,
	ZYNQMP_PM_RESET_SWDT_CRL,
	ZYNQMP_PM_RESET_NAND,
	ZYNQMP_PM_RESET_ADMA,
	ZYNQMP_PM_RESET_GPIO,
	ZYNQMP_PM_RESET_IOU_CC,
	ZYNQMP_PM_RESET_TIMESTAMP,
	ZYNQMP_PM_RESET_RPU_R50,
	ZYNQMP_PM_RESET_RPU_R51,
	ZYNQMP_PM_RESET_RPU_AMBA,
	ZYNQMP_PM_RESET_OCM,
	ZYNQMP_PM_RESET_RPU_PGE,
	ZYNQMP_PM_RESET_USB0_CORERESET,
	ZYNQMP_PM_RESET_USB1_CORERESET,
	ZYNQMP_PM_RESET_USB0_HIBERRESET,
	ZYNQMP_PM_RESET_USB1_HIBERRESET,
	ZYNQMP_PM_RESET_USB0_APB,
	ZYNQMP_PM_RESET_USB1_APB,
	ZYNQMP_PM_RESET_IPI,
	ZYNQMP_PM_RESET_APM_LPD,
	ZYNQMP_PM_RESET_RTC,
	ZYNQMP_PM_RESET_SYSMON,
	ZYNQMP_PM_RESET_AFI_FM6,
	ZYNQMP_PM_RESET_LPD_SWDT,
	ZYNQMP_PM_RESET_FPD,
	ZYNQMP_PM_RESET_RPU_DBG1,
	ZYNQMP_PM_RESET_RPU_DBG0,
	ZYNQMP_PM_RESET_DBG_LPD,
	ZYNQMP_PM_RESET_DBG_FPD,
	ZYNQMP_PM_RESET_APLL,
	ZYNQMP_PM_RESET_DPLL,
	ZYNQMP_PM_RESET_VPLL,
	ZYNQMP_PM_RESET_IOPLL,
	ZYNQMP_PM_RESET_RPLL,
	ZYNQMP_PM_RESET_GPO3_PL_0,
	ZYNQMP_PM_RESET_GPO3_PL_1,
	ZYNQMP_PM_RESET_GPO3_PL_2,
	ZYNQMP_PM_RESET_GPO3_PL_3,
	ZYNQMP_PM_RESET_GPO3_PL_4,
	ZYNQMP_PM_RESET_GPO3_PL_5,
	ZYNQMP_PM_RESET_GPO3_PL_6,
	ZYNQMP_PM_RESET_GPO3_PL_7,
	ZYNQMP_PM_RESET_GPO3_PL_8,
	ZYNQMP_PM_RESET_GPO3_PL_9,
	ZYNQMP_PM_RESET_GPO3_PL_10,
	ZYNQMP_PM_RESET_GPO3_PL_11,
	ZYNQMP_PM_RESET_GPO3_PL_12,
	ZYNQMP_PM_RESET_GPO3_PL_13,
	ZYNQMP_PM_RESET_GPO3_PL_14,
	ZYNQMP_PM_RESET_GPO3_PL_15,
	ZYNQMP_PM_RESET_GPO3_PL_16,
	ZYNQMP_PM_RESET_GPO3_PL_17,
	ZYNQMP_PM_RESET_GPO3_PL_18,
	ZYNQMP_PM_RESET_GPO3_PL_19,
	ZYNQMP_PM_RESET_GPO3_PL_20,
	ZYNQMP_PM_RESET_GPO3_PL_21,
	ZYNQMP_PM_RESET_GPO3_PL_22,
	ZYNQMP_PM_RESET_GPO3_PL_23,
	ZYNQMP_PM_RESET_GPO3_PL_24,
	ZYNQMP_PM_RESET_GPO3_PL_25,
	ZYNQMP_PM_RESET_GPO3_PL_26,
	ZYNQMP_PM_RESET_GPO3_PL_27,
	ZYNQMP_PM_RESET_GPO3_PL_28,
	ZYNQMP_PM_RESET_GPO3_PL_29,
	ZYNQMP_PM_RESET_GPO3_PL_30,
	ZYNQMP_PM_RESET_GPO3_PL_31,
	ZYNQMP_PM_RESET_RPU_LS,
	ZYNQMP_PM_RESET_PS_ONLY,
	ZYNQMP_PM_RESET_PL,
	ZYNQMP_PM_RESET_PS_PL0,
	ZYNQMP_PM_RESET_PS_PL1,
	ZYNQMP_PM_RESET_PS_PL2,
	ZYNQMP_PM_RESET_PS_PL3,
	ZYNQMP_PM_RESET_END = ZYNQMP_PM_RESET_PS_PL3
};

enum zynqmp_pm_suspend_reason {
	SUSPEND_POWER_REQUEST = 201,
	SUSPEND_ALERT,
	SUSPEND_SYSTEM_SHUTDOWN,
};

enum zynqmp_pm_request_ack {
	ZYNQMP_PM_REQUEST_ACK_NO = 1,
	ZYNQMP_PM_REQUEST_ACK_BLOCKING,
	ZYNQMP_PM_REQUEST_ACK_NON_BLOCKING,
};

enum pm_node_id {
	NODE_SD_0 = 39,
	NODE_SD_1,
};

enum tap_delay_type {
	PM_TAPDELAY_INPUT = 0,
	PM_TAPDELAY_OUTPUT,
};

enum dll_reset_type {
	PM_DLL_RESET_ASSERT,
	PM_DLL_RESET_RELEASE,
	PM_DLL_RESET_PULSE,
};

enum zynqmp_pm_shutdown_type {
	ZYNQMP_PM_SHUTDOWN_TYPE_SHUTDOWN,
	ZYNQMP_PM_SHUTDOWN_TYPE_RESET,
	ZYNQMP_PM_SHUTDOWN_TYPE_SETSCOPE_ONLY,
};

enum zynqmp_pm_shutdown_subtype {
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM,
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY,
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM,
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

int zynqmp_pm_invoke_fn(u32 pm_api_id, u32 arg0, u32 arg1,
			u32 arg2, u32 arg3, u32 *ret_payload);

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_get_api_version(u32 *version);
int zynqmp_pm_get_chipid(u32 *idcode, u32 *version);
int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata, u32 *out);
int zynqmp_pm_clock_enable(u32 clock_id);
int zynqmp_pm_clock_disable(u32 clock_id);
int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state);
int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider);
int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider);
int zynqmp_pm_clock_setrate(u32 clock_id, u64 rate);
int zynqmp_pm_clock_getrate(u32 clock_id, u64 *rate);
int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id);
int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id);
int zynqmp_pm_set_pll_frac_mode(u32 clk_id, u32 mode);
int zynqmp_pm_get_pll_frac_mode(u32 clk_id, u32 *mode);
int zynqmp_pm_set_pll_frac_data(u32 clk_id, u32 data);
int zynqmp_pm_get_pll_frac_data(u32 clk_id, u32 *data);
int zynqmp_pm_set_sd_tapdelay(u32 node_id, u32 type, u32 value);
int zynqmp_pm_sd_dll_reset(u32 node_id, u32 type);
int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
			   const enum zynqmp_pm_reset_action assert_flag);
int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset, u32 *status);
int zynqmp_pm_init_finalize(void);
int zynqmp_pm_set_suspend_mode(u32 mode);
int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
			   const u32 qos, const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_release_node(const u32 node);
int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
			      const u32 qos,
			      const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_fpga_get_status(u32 *value);
int zynqmp_pm_write_ggs(u32 index, u32 value);
int zynqmp_pm_read_ggs(u32 index, u32 *value);
int zynqmp_pm_write_pggs(u32 index, u32 value);
int zynqmp_pm_read_pggs(u32 index, u32 *value);
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype);
int zynqmp_pm_set_boot_health_status(u32 value);
#else
static inline struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void)
{
	return ERR_PTR(-ENODEV);
}

static inline int zynqmp_pm_get_api_version(u32 *version)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_chipid(u32 *idcode, u32 *version)
{
	return -ENODEV;
}

static inline int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata,
				       u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_enable(u32 clock_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_disable(u32 clock_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_setrate(u32 clock_id, u64 rate)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getrate(u32 clock_id, u64 *rate)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_pll_frac_mode(u32 clk_id, u32 mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_pll_frac_mode(u32 clk_id, u32 *mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_pll_frac_data(u32 clk_id, u32 data)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_pll_frac_data(u32 clk_id, u32 *data)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_sd_tapdelay(u32 node_id, u32 type, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_sd_dll_reset(u32 node_id, u32 type)
{
	return -ENODEV;
}

static inline int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
					 const enum zynqmp_pm_reset_action assert_flag)
{
	return -ENODEV;
}

static inline int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset,
					     u32 *status)
{
	return -ENODEV;
}

static inline int zynqmp_pm_init_finalize(void)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_suspend_mode(u32 mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
					 const u32 qos,
					 const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_release_node(const u32 node)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_requirement(const u32 node,
					    const u32 capabilities,
					    const u32 qos,
					    const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_load(const u64 address, const u32 size,
				      const u32 flags)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_get_status(u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_write_ggs(u32 index, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_read_ggs(u32 index, u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_write_pggs(u32 index, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_read_pggs(u32 index, u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_boot_health_status(u32 value)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_H__ */
