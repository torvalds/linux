/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SCMI PMU Protocols header
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _SCMI_PMU_H
#define _SCMI_PMU_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/types.h>


#define SCMI_PMU_PROTOCOL    0x86
#define MAX_NUM_CPUS		8


struct scmi_protocol_handle;

/**
 * struct scmi_pmu_vendor_ops - represents the various operations provided
 *      by SCMI PMU Protocol
 */
struct scmi_pmu_vendor_ops {
	int (*set_pmu_map)(const struct scmi_protocol_handle *ph, void *buf);
	int (*set_enable_trace)(const struct scmi_protocol_handle *ph, void *buf);
	int (*set_cache_enable)(const struct scmi_protocol_handle *ph, void *buf);
};

#endif /* _SCMI_PMU_H */
