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

#define SCMI_IMX95_CTRL_PDM_CLK_SEL	0	/* AON PDM clock sel */
#define SCMI_IMX95_CTRL_MQS1_SETTINGS	1	/* AON MQS settings */
#define SCMI_IMX95_CTRL_SAI1_MCLK	2	/* AON SAI1 MCLK */
#define SCMI_IMX95_CTRL_SAI3_MCLK	3	/* WAKE SAI3 MCLK */
#define SCMI_IMX95_CTRL_SAI4_MCLK	4	/* WAKE SAI4 MCLK */
#define SCMI_IMX95_CTRL_SAI5_MCLK	5	/* WAKE SAI5 MCLK */

#define SCMI_IMX94_CTRL_PDM_CLK_SEL	0U	/*!< AON PDM clock sel */
#define SCMI_IMX94_CTRL_MQS1_SETTINGS	1U	/*!< AON MQS settings */
#define SCMI_IMX94_CTRL_MQS2_SETTINGS	2U	/*!< WAKE MQS settings */
#define SCMI_IMX94_CTRL_SAI1_MCLK	3U	/*!< AON SAI1 MCLK */
#define SCMI_IMX94_CTRL_SAI2_MCLK	4U	/*!< WAKE SAI2 MCLK */
#define SCMI_IMX94_CTRL_SAI3_MCLK	5U	/*!< WAKE SAI3 MCLK */
#define SCMI_IMX94_CTRL_SAI4_MCLK	6U	/*!< WAKE SAI4 MCLK */

#if IS_ENABLED(CONFIG_IMX_SCMI_MISC_DRV)
int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val);
int scmi_imx_misc_ctrl_set(u32 id, u32 val);
#else
static inline int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val)
{
	return -EOPNOTSUPP;
}

static inline int scmi_imx_misc_ctrl_set(u32 id, u32 val)
{
	return -EOPNOTSUPP;
}
#endif

#if IS_ENABLED(CONFIG_IMX_SCMI_CPU_DRV)
int scmi_imx_cpu_start(u32 cpuid, bool start);
int scmi_imx_cpu_started(u32 cpuid, bool *started);
int scmi_imx_cpu_reset_vector_set(u32 cpuid, u64 vector, bool start, bool boot,
				  bool resume);
#else
static inline int scmi_imx_cpu_start(u32 cpuid, bool start)
{
	return -EOPNOTSUPP;
}

static inline int scmi_imx_cpu_started(u32 cpuid, bool *started)
{
	return -EOPNOTSUPP;
}

static inline int scmi_imx_cpu_reset_vector_set(u32 cpuid, u64 vector, bool start,
						bool boot, bool resume)
{
	return -EOPNOTSUPP;
}
#endif

enum scmi_imx_lmm_op {
	SCMI_IMX_LMM_BOOT,
	SCMI_IMX_LMM_POWER_ON,
	SCMI_IMX_LMM_SHUTDOWN,
};

/* For shutdown pperation */
#define SCMI_IMX_LMM_OP_FORCEFUL	0
#define SCMI_IMX_LMM_OP_GRACEFUL	BIT(0)

#if IS_ENABLED(CONFIG_IMX_SCMI_LMM_DRV)
int scmi_imx_lmm_operation(u32 lmid, enum scmi_imx_lmm_op op, u32 flags);
int scmi_imx_lmm_info(u32 lmid, struct scmi_imx_lmm_info *info);
int scmi_imx_lmm_reset_vector_set(u32 lmid, u32 cpuid, u32 flags, u64 vector);
#else
static inline int scmi_imx_lmm_operation(u32 lmid, enum scmi_imx_lmm_op op, u32 flags)
{
	return -EOPNOTSUPP;
}

static inline int scmi_imx_lmm_info(u32 lmid, struct scmi_imx_lmm_info *info)
{
	return -EOPNOTSUPP;
}

static inline int scmi_imx_lmm_reset_vector_set(u32 lmid, u32 cpuid, u32 flags, u64 vector)
{
	return -EOPNOTSUPP;
}
#endif
#endif
