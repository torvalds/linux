/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DT_BINDINGS_QCOM_IPCC_H
#define __DT_BINDINGS_QCOM_IPCC_H

/* Signal IDs for MPROC protocol */
#define IPCC_MPROC_SIGNAL_GLINK_QMP	0
#define IPCC_MPROC_SIGNAL_TZ		1
#define IPCC_MPROC_SIGNAL_SMP2P		2
#define IPCC_MPROC_SIGNAL_PING		3
#define IPCC_MPROC_SIGNAL_MAX		4 /* Used by driver only */

#define IPCC_COMPUTE_L0_SIGNAL_MAX					32 /* Used by driver only */
#define IPCC_COMPUTE_L1_SIGNAL_MAX					32 /* Used by driver only */

/* Client IDs */
#define IPCC_CLIENT_AOP			0
#define IPCC_CLIENT_TZ			1
#define IPCC_CLIENT_MPSS		2
#define IPCC_CLIENT_LPASS		3
#define IPCC_CLIENT_SLPI		4
#define IPCC_CLIENT_SDC			5
#define IPCC_CLIENT_CDSP		6
#define IPCC_CLIENT_NPU			7
#define IPCC_CLIENT_APSS		8
#define IPCC_CLIENT_GPU			9
#define IPCC_CLIENT_CVP			10
#define IPCC_CLIENT_CAM			11
#define IPCC_CLIENT_VPU			12
#define IPCC_CLIENT_PCIE0		13
#define IPCC_CLIENT_PCIE1		14
#define IPCC_CLIENT_PCIE2		15
#define IPCC_CLIENT_SPSS		16
#define IPCC_CLIENT_NSP1                18
#define IPCC_CLIENT_TME			23
#define IPCC_CLIENT_WPSS		24
#define IPCC_CLIENT_SAIL0               27
#define IPCC_CLIENT_SAIL1               28
#define IPCC_CLIENT_SAIL2               29
#define IPCC_CLIENT_SAIL3               30
#define IPCC_CLIENT_SOCCP		46
#define IPCC_CLIENT_BROADCAST	0xF000
#define IPCC_CLIENT_MAX			47 /* Used by driver only */
#define IPCC_CLIENT_GPDSP0		31
#endif
