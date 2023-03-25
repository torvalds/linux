/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qcom scmi vendor protocol's header
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_SCMI_VENDOR_H
#define _QCOM_SCMI_VENDOR_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/types.h>

#define QCOM_SCMI_VENDOR_PROTOCOL    0x80

struct scmi_protocol_handle;
extern struct scmi_device *get_qcom_scmi_device(void);


/**
 * struct qcom_scmi_vendor_ops - represents the various operations provided
 *      by qcom scmi vendor protocol
 */
struct qcom_scmi_vendor_ops {
	int (*set_param)(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
		u32 param_id, size_t size);
	int (*get_param)(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
		u32 param_id, size_t tx_size, size_t rx_size);
	int (*start_activity)(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
		u32 param_id, size_t size);
	int (*stop_activity)(const struct scmi_protocol_handle *ph, void *buf, u64 algo_str,
		u32 param_id, size_t size);
};


#endif /* _QCOM_SCMI_VENDOR_H */

