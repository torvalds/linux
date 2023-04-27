/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SCMI Vendor Protocols header
 *
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _SCMI_PLH_VENDOR_H
#define _SCMI_PLH_VENDOR_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/types.h>

#define SCMI_PROTOCOL_PLH      0x81

enum plh_features {
	PERF_LOCK_SCROLL,
	PERF_LOCK_LAUNCH,
};

struct scmi_protocol_handle;
extern struct scmi_device *get_plh_scmi_device(void);

/**
 * struct scmi_plh_vendor_ops - represents the various operations provided
 *	by SCMI PLH Protocol
 *
 * @init_plh_ipc_freq_tbl: initialize plh ipc freq voting table in cpucp
 * @start_plh: starts plh in cpucp
 * @stop_plh: stops plh in cpucp
 * @set_plh_sample_ms: configure the sampling duration of plh in cpucp
 * @set_plh_log_level: configure the supported log_level of plh in cpucp
 */
struct scmi_plh_vendor_ops {
	int (*init_plh_ipc_freq_tbl)(const struct scmi_protocol_handle *ph,
				u16 *p_init_args, u16 init_len, enum plh_features feature);
	int (*start_plh)(const struct scmi_protocol_handle *ph, u16 fps, enum plh_features feature);
	int (*stop_plh)(const struct scmi_protocol_handle *ph, enum plh_features feature);
	int (*set_plh_sample_ms)(const struct scmi_protocol_handle *ph,
				u16 sample_ms, enum plh_features feature);
	int (*set_plh_log_level)(const struct scmi_protocol_handle *ph,
				u16 log_level, enum plh_features feature);
};

#endif
