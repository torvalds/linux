/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2021 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#ifndef __FIRMWARE_ZYNQMP_H__
#define __FIRMWARE_ZYNQMP_H__
#include <linux/types.h>

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

/* PM API versions */
#define PM_API_VERSION_2	2

/* ATF only commands */
#define TF_A_PM_REGISTER_SGI		0xa04
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

/* Loader commands */
#define PM_LOAD_PDI	0x701
#define PDI_SRC_DDR	0xF

/*
 * Firmware FPGA Manager flags
 * XILINX_ZYNQMP_PM_FPGA_FULL:	FPGA full reconfiguration
 * XILINX_ZYNQMP_PM_FPGA_PARTIAL: FPGA partial reconfiguration
 */
#define XILINX_ZYNQMP_PM_FPGA_FULL	0x0U
#define XILINX_ZYNQMP_PM_FPGA_PARTIAL	BIT(0)

/*
 * Node IDs for the Error Events.
 */
#define EVENT_ERROR_PMC_ERR1	(0x28100000U)
#define EVENT_ERROR_PMC_ERR2	(0x28104000U)
#define EVENT_ERROR_PSM_ERR1	(0x28108000U)
#define EVENT_ERROR_PSM_ERR2	(0x2810C000U)

/* ZynqMP SD tap delay tuning */
#define SD_ITAPDLY	0xFF180314
#define SD_OTAPDLYSEL	0xFF180318

enum pm_api_cb_id {
	PM_INIT_SUSPEND_CB = 30,
	PM_ACKNOWLEDGE_CB = 31,
	PM_NOTIFY_CB = 32,
};

enum pm_api_id {
	PM_GET_API_VERSION = 1,
	PM_REGISTER_NOTIFIER = 5,
	PM_FORCE_POWERDOWN = 8,
	PM_REQUEST_WAKEUP = 10,
	PM_SYSTEM_SHUTDOWN = 12,
	PM_REQUEST_NODE = 13,
	PM_RELEASE_NODE = 14,
	PM_SET_REQUIREMENT = 15,
	PM_RESET_ASSERT = 17,
	PM_RESET_GET_STATUS = 18,
	PM_MMIO_WRITE = 19,
	PM_MMIO_READ = 20,
	PM_PM_INIT_FINALIZE = 21,
	PM_FPGA_LOAD = 22,
	PM_FPGA_GET_STATUS = 23,
	PM_GET_CHIPID = 24,
	PM_SECURE_SHA = 26,
	PM_PINCTRL_REQUEST = 28,
	PM_PINCTRL_RELEASE = 29,
	PM_PINCTRL_GET_FUNCTION = 30,
	PM_PINCTRL_SET_FUNCTION = 31,
	PM_PINCTRL_CONFIG_PARAM_GET = 32,
	PM_PINCTRL_CONFIG_PARAM_SET = 33,
	PM_IOCTL = 34,
	PM_QUERY_DATA = 35,
	PM_CLOCK_ENABLE = 36,
	PM_CLOCK_DISABLE = 37,
	PM_CLOCK_GETSTATE = 38,
	PM_CLOCK_SETDIVIDER = 39,
	PM_CLOCK_GETDIVIDER = 40,
	PM_CLOCK_SETRATE = 41,
	PM_CLOCK_GETRATE = 42,
	PM_CLOCK_SETPARENT = 43,
	PM_CLOCK_GETPARENT = 44,
	PM_SECURE_AES = 47,
	PM_FEATURE_CHECK = 63,
};

/* PMU-FW return status codes */
enum pm_ret_status {
	XST_PM_SUCCESS = 0,
	XST_PM_NO_FEATURE = 19,
	XST_PM_INTERNAL = 2000,
	XST_PM_CONFLICT = 2001,
	XST_PM_NO_ACCESS = 2002,
	XST_PM_INVALID_NODE = 2003,
	XST_PM_DOUBLE_REQ = 2004,
	XST_PM_ABORT_SUSPEND = 2005,
	XST_PM_MULT_USER = 2008,
};

enum pm_ioctl_id {
	IOCTL_GET_RPU_OPER_MODE = 0,
	IOCTL_SET_RPU_OPER_MODE = 1,
	IOCTL_RPU_BOOT_ADDR_CONFIG = 2,
	IOCTL_TCM_COMB_CONFIG = 3,
	IOCTL_SET_TAPDELAY_BYPASS = 4,
	IOCTL_SD_DLL_RESET = 6,
	IOCTL_SET_SD_TAPDELAY = 7,
	IOCTL_SET_PLL_FRAC_MODE = 8,
	IOCTL_GET_PLL_FRAC_MODE = 9,
	IOCTL_SET_PLL_FRAC_DATA = 10,
	IOCTL_GET_PLL_FRAC_DATA = 11,
	IOCTL_WRITE_GGS = 12,
	IOCTL_READ_GGS = 13,
	IOCTL_WRITE_PGGS = 14,
	IOCTL_READ_PGGS = 15,
	/* Set healthy bit value */
	IOCTL_SET_BOOT_HEALTH_STATUS = 17,
	IOCTL_OSPI_MUX_SELECT = 21,
	/* Register SGI to ATF */
	IOCTL_REGISTER_SGI = 25,
	/* Runtime feature configuration */
	IOCTL_SET_FEATURE_CONFIG = 26,
	IOCTL_GET_FEATURE_CONFIG = 27,
	/* Dynamic SD/GEM configuration */
	IOCTL_SET_SD_CONFIG = 30,
	IOCTL_SET_GEM_CONFIG = 31,
};

enum pm_query_id {
	PM_QID_INVALID = 0,
	PM_QID_CLOCK_GET_NAME = 1,
	PM_QID_CLOCK_GET_TOPOLOGY = 2,
	PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS = 3,
	PM_QID_CLOCK_GET_PARENTS = 4,
	PM_QID_CLOCK_GET_ATTRIBUTES = 5,
	PM_QID_PINCTRL_GET_NUM_PINS = 6,
	PM_QID_PINCTRL_GET_NUM_FUNCTIONS = 7,
	PM_QID_PINCTRL_GET_NUM_FUNCTION_GROUPS = 8,
	PM_QID_PINCTRL_GET_FUNCTION_NAME = 9,
	PM_QID_PINCTRL_GET_FUNCTION_GROUPS = 10,
	PM_QID_PINCTRL_GET_PIN_GROUPS = 11,
	PM_QID_CLOCK_GET_NUM_CLOCKS = 12,
	PM_QID_CLOCK_GET_MAX_DIVISOR = 13,
};

enum rpu_oper_mode {
	PM_RPU_MODE_LOCKSTEP = 0,
	PM_RPU_MODE_SPLIT = 1,
};

enum rpu_boot_mem {
	PM_RPU_BOOTMEM_LOVEC = 0,
	PM_RPU_BOOTMEM_HIVEC = 1,
};

enum rpu_tcm_comb {
	PM_RPU_TCM_SPLIT = 0,
	PM_RPU_TCM_COMB = 1,
};

enum zynqmp_pm_reset_action {
	PM_RESET_ACTION_RELEASE = 0,
	PM_RESET_ACTION_ASSERT = 1,
	PM_RESET_ACTION_PULSE = 2,
};

enum zynqmp_pm_reset {
	ZYNQMP_PM_RESET_START = 1000,
	ZYNQMP_PM_RESET_PCIE_CFG = ZYNQMP_PM_RESET_START,
	ZYNQMP_PM_RESET_PCIE_BRIDGE = 1001,
	ZYNQMP_PM_RESET_PCIE_CTRL = 1002,
	ZYNQMP_PM_RESET_DP = 1003,
	ZYNQMP_PM_RESET_SWDT_CRF = 1004,
	ZYNQMP_PM_RESET_AFI_FM5 = 1005,
	ZYNQMP_PM_RESET_AFI_FM4 = 1006,
	ZYNQMP_PM_RESET_AFI_FM3 = 1007,
	ZYNQMP_PM_RESET_AFI_FM2 = 1008,
	ZYNQMP_PM_RESET_AFI_FM1 = 1009,
	ZYNQMP_PM_RESET_AFI_FM0 = 1010,
	ZYNQMP_PM_RESET_GDMA = 1011,
	ZYNQMP_PM_RESET_GPU_PP1 = 1012,
	ZYNQMP_PM_RESET_GPU_PP0 = 1013,
	ZYNQMP_PM_RESET_GPU = 1014,
	ZYNQMP_PM_RESET_GT = 1015,
	ZYNQMP_PM_RESET_SATA = 1016,
	ZYNQMP_PM_RESET_ACPU3_PWRON = 1017,
	ZYNQMP_PM_RESET_ACPU2_PWRON = 1018,
	ZYNQMP_PM_RESET_ACPU1_PWRON = 1019,
	ZYNQMP_PM_RESET_ACPU0_PWRON = 1020,
	ZYNQMP_PM_RESET_APU_L2 = 1021,
	ZYNQMP_PM_RESET_ACPU3 = 1022,
	ZYNQMP_PM_RESET_ACPU2 = 1023,
	ZYNQMP_PM_RESET_ACPU1 = 1024,
	ZYNQMP_PM_RESET_ACPU0 = 1025,
	ZYNQMP_PM_RESET_DDR = 1026,
	ZYNQMP_PM_RESET_APM_FPD = 1027,
	ZYNQMP_PM_RESET_SOFT = 1028,
	ZYNQMP_PM_RESET_GEM0 = 1029,
	ZYNQMP_PM_RESET_GEM1 = 1030,
	ZYNQMP_PM_RESET_GEM2 = 1031,
	ZYNQMP_PM_RESET_GEM3 = 1032,
	ZYNQMP_PM_RESET_QSPI = 1033,
	ZYNQMP_PM_RESET_UART0 = 1034,
	ZYNQMP_PM_RESET_UART1 = 1035,
	ZYNQMP_PM_RESET_SPI0 = 1036,
	ZYNQMP_PM_RESET_SPI1 = 1037,
	ZYNQMP_PM_RESET_SDIO0 = 1038,
	ZYNQMP_PM_RESET_SDIO1 = 1039,
	ZYNQMP_PM_RESET_CAN0 = 1040,
	ZYNQMP_PM_RESET_CAN1 = 1041,
	ZYNQMP_PM_RESET_I2C0 = 1042,
	ZYNQMP_PM_RESET_I2C1 = 1043,
	ZYNQMP_PM_RESET_TTC0 = 1044,
	ZYNQMP_PM_RESET_TTC1 = 1045,
	ZYNQMP_PM_RESET_TTC2 = 1046,
	ZYNQMP_PM_RESET_TTC3 = 1047,
	ZYNQMP_PM_RESET_SWDT_CRL = 1048,
	ZYNQMP_PM_RESET_NAND = 1049,
	ZYNQMP_PM_RESET_ADMA = 1050,
	ZYNQMP_PM_RESET_GPIO = 1051,
	ZYNQMP_PM_RESET_IOU_CC = 1052,
	ZYNQMP_PM_RESET_TIMESTAMP = 1053,
	ZYNQMP_PM_RESET_RPU_R50 = 1054,
	ZYNQMP_PM_RESET_RPU_R51 = 1055,
	ZYNQMP_PM_RESET_RPU_AMBA = 1056,
	ZYNQMP_PM_RESET_OCM = 1057,
	ZYNQMP_PM_RESET_RPU_PGE = 1058,
	ZYNQMP_PM_RESET_USB0_CORERESET = 1059,
	ZYNQMP_PM_RESET_USB1_CORERESET = 1060,
	ZYNQMP_PM_RESET_USB0_HIBERRESET = 1061,
	ZYNQMP_PM_RESET_USB1_HIBERRESET = 1062,
	ZYNQMP_PM_RESET_USB0_APB = 1063,
	ZYNQMP_PM_RESET_USB1_APB = 1064,
	ZYNQMP_PM_RESET_IPI = 1065,
	ZYNQMP_PM_RESET_APM_LPD = 1066,
	ZYNQMP_PM_RESET_RTC = 1067,
	ZYNQMP_PM_RESET_SYSMON = 1068,
	ZYNQMP_PM_RESET_AFI_FM6 = 1069,
	ZYNQMP_PM_RESET_LPD_SWDT = 1070,
	ZYNQMP_PM_RESET_FPD = 1071,
	ZYNQMP_PM_RESET_RPU_DBG1 = 1072,
	ZYNQMP_PM_RESET_RPU_DBG0 = 1073,
	ZYNQMP_PM_RESET_DBG_LPD = 1074,
	ZYNQMP_PM_RESET_DBG_FPD = 1075,
	ZYNQMP_PM_RESET_APLL = 1076,
	ZYNQMP_PM_RESET_DPLL = 1077,
	ZYNQMP_PM_RESET_VPLL = 1078,
	ZYNQMP_PM_RESET_IOPLL = 1079,
	ZYNQMP_PM_RESET_RPLL = 1080,
	ZYNQMP_PM_RESET_GPO3_PL_0 = 1081,
	ZYNQMP_PM_RESET_GPO3_PL_1 = 1082,
	ZYNQMP_PM_RESET_GPO3_PL_2 = 1083,
	ZYNQMP_PM_RESET_GPO3_PL_3 = 1084,
	ZYNQMP_PM_RESET_GPO3_PL_4 = 1085,
	ZYNQMP_PM_RESET_GPO3_PL_5 = 1086,
	ZYNQMP_PM_RESET_GPO3_PL_6 = 1087,
	ZYNQMP_PM_RESET_GPO3_PL_7 = 1088,
	ZYNQMP_PM_RESET_GPO3_PL_8 = 1089,
	ZYNQMP_PM_RESET_GPO3_PL_9 = 1090,
	ZYNQMP_PM_RESET_GPO3_PL_10 = 1091,
	ZYNQMP_PM_RESET_GPO3_PL_11 = 1092,
	ZYNQMP_PM_RESET_GPO3_PL_12 = 1093,
	ZYNQMP_PM_RESET_GPO3_PL_13 = 1094,
	ZYNQMP_PM_RESET_GPO3_PL_14 = 1095,
	ZYNQMP_PM_RESET_GPO3_PL_15 = 1096,
	ZYNQMP_PM_RESET_GPO3_PL_16 = 1097,
	ZYNQMP_PM_RESET_GPO3_PL_17 = 1098,
	ZYNQMP_PM_RESET_GPO3_PL_18 = 1099,
	ZYNQMP_PM_RESET_GPO3_PL_19 = 1100,
	ZYNQMP_PM_RESET_GPO3_PL_20 = 1101,
	ZYNQMP_PM_RESET_GPO3_PL_21 = 1102,
	ZYNQMP_PM_RESET_GPO3_PL_22 = 1103,
	ZYNQMP_PM_RESET_GPO3_PL_23 = 1104,
	ZYNQMP_PM_RESET_GPO3_PL_24 = 1105,
	ZYNQMP_PM_RESET_GPO3_PL_25 = 1106,
	ZYNQMP_PM_RESET_GPO3_PL_26 = 1107,
	ZYNQMP_PM_RESET_GPO3_PL_27 = 1108,
	ZYNQMP_PM_RESET_GPO3_PL_28 = 1109,
	ZYNQMP_PM_RESET_GPO3_PL_29 = 1110,
	ZYNQMP_PM_RESET_GPO3_PL_30 = 1111,
	ZYNQMP_PM_RESET_GPO3_PL_31 = 1112,
	ZYNQMP_PM_RESET_RPU_LS = 1113,
	ZYNQMP_PM_RESET_PS_ONLY = 1114,
	ZYNQMP_PM_RESET_PL = 1115,
	ZYNQMP_PM_RESET_PS_PL0 = 1116,
	ZYNQMP_PM_RESET_PS_PL1 = 1117,
	ZYNQMP_PM_RESET_PS_PL2 = 1118,
	ZYNQMP_PM_RESET_PS_PL3 = 1119,
	ZYNQMP_PM_RESET_END = ZYNQMP_PM_RESET_PS_PL3
};

enum zynqmp_pm_suspend_reason {
	SUSPEND_POWER_REQUEST = 201,
	SUSPEND_ALERT = 202,
	SUSPEND_SYSTEM_SHUTDOWN = 203,
};

enum zynqmp_pm_request_ack {
	ZYNQMP_PM_REQUEST_ACK_NO = 1,
	ZYNQMP_PM_REQUEST_ACK_BLOCKING = 2,
	ZYNQMP_PM_REQUEST_ACK_NON_BLOCKING = 3,
};

enum pm_node_id {
	NODE_SD_0 = 39,
	NODE_SD_1 = 40,
};

enum tap_delay_type {
	PM_TAPDELAY_INPUT = 0,
	PM_TAPDELAY_OUTPUT = 1,
};

enum dll_reset_type {
	PM_DLL_RESET_ASSERT = 0,
	PM_DLL_RESET_RELEASE = 1,
	PM_DLL_RESET_PULSE = 2,
};

enum pm_pinctrl_config_param {
	PM_PINCTRL_CONFIG_SLEW_RATE = 0,
	PM_PINCTRL_CONFIG_BIAS_STATUS = 1,
	PM_PINCTRL_CONFIG_PULL_CTRL = 2,
	PM_PINCTRL_CONFIG_SCHMITT_CMOS = 3,
	PM_PINCTRL_CONFIG_DRIVE_STRENGTH = 4,
	PM_PINCTRL_CONFIG_VOLTAGE_STATUS = 5,
	PM_PINCTRL_CONFIG_TRI_STATE = 6,
	PM_PINCTRL_CONFIG_MAX = 7,
};

enum pm_pinctrl_slew_rate {
	PM_PINCTRL_SLEW_RATE_FAST = 0,
	PM_PINCTRL_SLEW_RATE_SLOW = 1,
};

enum pm_pinctrl_bias_status {
	PM_PINCTRL_BIAS_DISABLE = 0,
	PM_PINCTRL_BIAS_ENABLE = 1,
};

enum pm_pinctrl_pull_ctrl {
	PM_PINCTRL_BIAS_PULL_DOWN = 0,
	PM_PINCTRL_BIAS_PULL_UP = 1,
};

enum pm_pinctrl_schmitt_cmos {
	PM_PINCTRL_INPUT_TYPE_CMOS = 0,
	PM_PINCTRL_INPUT_TYPE_SCHMITT = 1,
};

enum pm_pinctrl_drive_strength {
	PM_PINCTRL_DRIVE_STRENGTH_2MA = 0,
	PM_PINCTRL_DRIVE_STRENGTH_4MA = 1,
	PM_PINCTRL_DRIVE_STRENGTH_8MA = 2,
	PM_PINCTRL_DRIVE_STRENGTH_12MA = 3,
};

enum pm_pinctrl_tri_state {
	PM_PINCTRL_TRI_STATE_DISABLE = 0,
	PM_PINCTRL_TRI_STATE_ENABLE = 1,
};

enum zynqmp_pm_shutdown_type {
	ZYNQMP_PM_SHUTDOWN_TYPE_SHUTDOWN = 0,
	ZYNQMP_PM_SHUTDOWN_TYPE_RESET = 1,
	ZYNQMP_PM_SHUTDOWN_TYPE_SETSCOPE_ONLY = 2,
};

enum zynqmp_pm_shutdown_subtype {
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM = 0,
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY = 1,
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM = 2,
};

enum tap_delay_signal_type {
	PM_TAPDELAY_NAND_DQS_IN = 0,
	PM_TAPDELAY_NAND_DQS_OUT = 1,
	PM_TAPDELAY_QSPI = 2,
	PM_TAPDELAY_MAX = 3,
};

enum tap_delay_bypass_ctrl {
	PM_TAPDELAY_BYPASS_DISABLE = 0,
	PM_TAPDELAY_BYPASS_ENABLE = 1,
};

enum ospi_mux_select_type {
	PM_OSPI_MUX_SEL_DMA = 0,
	PM_OSPI_MUX_SEL_LINEAR = 1,
};

enum pm_feature_config_id {
	PM_FEATURE_INVALID = 0,
	PM_FEATURE_OVERTEMP_STATUS = 1,
	PM_FEATURE_OVERTEMP_VALUE = 2,
	PM_FEATURE_EXTWDT_STATUS = 3,
	PM_FEATURE_EXTWDT_VALUE = 4,
};

/**
 * enum pm_sd_config_type - PM SD configuration.
 * @SD_CONFIG_EMMC_SEL: To set SD_EMMC_SEL in CTRL_REG_SD and SD_SLOTTYPE
 * @SD_CONFIG_BASECLK: To set SD_BASECLK in SD_CONFIG_REG1
 * @SD_CONFIG_8BIT: To set SD_8BIT in SD_CONFIG_REG2
 * @SD_CONFIG_FIXED: To set fixed config registers
 */
enum pm_sd_config_type {
	SD_CONFIG_EMMC_SEL = 1,
	SD_CONFIG_BASECLK = 2,
	SD_CONFIG_8BIT = 3,
	SD_CONFIG_FIXED = 4,
};

/**
 * enum pm_gem_config_type - PM GEM configuration.
 * @GEM_CONFIG_SGMII_MODE: To set GEM_SGMII_MODE in GEM_CLK_CTRL register
 * @GEM_CONFIG_FIXED: To set fixed config registers
 */
enum pm_gem_config_type {
	GEM_CONFIG_SGMII_MODE = 1,
	GEM_CONFIG_FIXED = 2,
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
int zynqmp_pm_ospi_mux_select(u32 dev_id, u32 select);
int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
			   const enum zynqmp_pm_reset_action assert_flag);
int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset, u32 *status);
unsigned int zynqmp_pm_bootmode_read(u32 *ps_mode);
int zynqmp_pm_bootmode_write(u32 ps_mode);
int zynqmp_pm_init_finalize(void);
int zynqmp_pm_set_suspend_mode(u32 mode);
int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
			   const u32 qos, const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_release_node(const u32 node);
int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
			      const u32 qos,
			      const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_fpga_get_status(u32 *value);
int zynqmp_pm_write_ggs(u32 index, u32 value);
int zynqmp_pm_read_ggs(u32 index, u32 *value);
int zynqmp_pm_write_pggs(u32 index, u32 value);
int zynqmp_pm_read_pggs(u32 index, u32 *value);
int zynqmp_pm_set_tapdelay_bypass(u32 index, u32 value);
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype);
int zynqmp_pm_set_boot_health_status(u32 value);
int zynqmp_pm_pinctrl_request(const u32 pin);
int zynqmp_pm_pinctrl_release(const u32 pin);
int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *id);
int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 id);
int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param,
				 u32 *value);
int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param,
				 u32 value);
int zynqmp_pm_load_pdi(const u32 src, const u64 address);
int zynqmp_pm_register_notifier(const u32 node, const u32 event,
				const u32 wake, const u32 enable);
int zynqmp_pm_feature(const u32 api_id);
int zynqmp_pm_is_function_supported(const u32 api_id, const u32 id);
int zynqmp_pm_set_feature_config(enum pm_feature_config_id id, u32 value);
int zynqmp_pm_get_feature_config(enum pm_feature_config_id id, u32 *payload);
int zynqmp_pm_register_sgi(u32 sgi_num, u32 reset);
int zynqmp_pm_force_pwrdwn(const u32 target,
			   const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_request_wake(const u32 node,
			   const bool set_addr,
			   const u64 address,
			   const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_get_rpu_mode(u32 node_id, enum rpu_oper_mode *rpu_mode);
int zynqmp_pm_set_rpu_mode(u32 node_id, enum rpu_oper_mode rpu_mode);
int zynqmp_pm_set_tcm_config(u32 node_id, enum rpu_tcm_comb tcm_mode);
int zynqmp_pm_set_sd_config(u32 node, enum pm_sd_config_type config, u32 value);
int zynqmp_pm_set_gem_config(u32 node, enum pm_gem_config_type config,
			     u32 value);
#else
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

static inline int zynqmp_pm_ospi_mux_select(u32 dev_id, u32 select)
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

static inline unsigned int zynqmp_pm_bootmode_read(u32 *ps_mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_bootmode_write(u32 ps_mode)
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

static inline int zynqmp_pm_sha_hash(const u64 address, const u32 size,
				     const u32 flags)
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

static inline int zynqmp_pm_set_tapdelay_bypass(u32 index, u32 value)
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

static inline int zynqmp_pm_pinctrl_request(const u32 pin)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_release(const u32 pin)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_is_function_supported(const u32 api_id, const u32 id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param,
					       u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param,
					       u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_load_pdi(const u32 src, const u64 address)
{
	return -ENODEV;
}

static inline int zynqmp_pm_register_notifier(const u32 node, const u32 event,
					      const u32 wake, const u32 enable)
{
	return -ENODEV;
}

static inline int zynqmp_pm_feature(const u32 api_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_feature_config(enum pm_feature_config_id id,
					       u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_feature_config(enum pm_feature_config_id id,
					       u32 *payload)
{
	return -ENODEV;
}

static inline int zynqmp_pm_register_sgi(u32 sgi_num, u32 reset)
{
	return -ENODEV;
}

static inline int zynqmp_pm_force_pwrdwn(const u32 target,
					 const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_request_wake(const u32 node,
					 const bool set_addr,
					 const u64 address,
					 const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_rpu_mode(u32 node_id, enum rpu_oper_mode *rpu_mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_rpu_mode(u32 node_id, enum rpu_oper_mode rpu_mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_tcm_config(u32 node_id, enum rpu_tcm_comb tcm_mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_sd_config(u32 node,
					  enum pm_sd_config_type config,
					  u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_gem_config(u32 node,
					   enum pm_gem_config_type config,
					   u32 value)
{
	return -ENODEV;
}

#endif

#endif /* __FIRMWARE_ZYNQMP_H__ */
