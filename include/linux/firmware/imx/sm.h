/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef _SCMI_IMX_H
#define _SCMI_IMX_H

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/scmi_imx_protocol.h>
#include <linux/types.h>

#define SCMI_IMX_CTRL_PDM_CLK_SEL	0	/* AON PDM clock sel */
#define SCMI_IMX_CTRL_MQS1_SETTINGS	1	/* AON MQS settings */
#define SCMI_IMX_CTRL_SAI1_MCLK		2	/* AON SAI1 MCLK */
#define SCMI_IMX_CTRL_SAI3_MCLK		3	/* WAKE SAI3 MCLK */
#define SCMI_IMX_CTRL_SAI4_MCLK		4	/* WAKE SAI4 MCLK */
#define SCMI_IMX_CTRL_SAI5_MCLK		5	/* WAKE SAI5 MCLK */

int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val);
int scmi_imx_misc_ctrl_set(u32 id, u32 val);

int scmi_imx_cpu_start(u32 cpuid, bool start);
int scmi_imx_cpu_started(u32 cpuid, bool *started);
int scmi_imx_cpu_reset_vector_set(u32 cpuid, u64 vector, bool start, bool boot,
				  bool resume);

enum scmi_imx_lmm_op {
	SCMI_IMX_LMM_BOOT,
	SCMI_IMX_LMM_POWER_ON,
	SCMI_IMX_LMM_SHUTDOWN,
};

/* For shutdown pperation */
#define SCMI_IMX_LMM_OP_FORCEFUL	0
#define SCMI_IMX_LMM_OP_GRACEFUL	BIT(0)

int scmi_imx_lmm_operation(u32 lmid, enum scmi_imx_lmm_op op, u32 flags);
int scmi_imx_lmm_info(u32 lmid, struct scmi_imx_lmm_info *info);
int scmi_imx_lmm_reset_vector_set(u32 lmid, u32 cpuid, u32 flags, u64 vector);
#endif
