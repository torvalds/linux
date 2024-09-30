/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SCMI Message Protocol driver NXP extension header
 *
 * Copyright 2024 NXP.
 */

#ifndef _LINUX_SCMI_NXP_PROTOCOL_H
#define _LINUX_SCMI_NXP_PROTOCOL_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/types.h>

enum scmi_nxp_protocol {
	SCMI_PROTOCOL_IMX_BBM = 0x81,
	SCMI_PROTOCOL_IMX_MISC = 0x84,
};

struct scmi_imx_bbm_proto_ops {
	int (*rtc_time_set)(const struct scmi_protocol_handle *ph, u32 id,
			    uint64_t sec);
	int (*rtc_time_get)(const struct scmi_protocol_handle *ph, u32 id,
			    u64 *val);
	int (*rtc_alarm_set)(const struct scmi_protocol_handle *ph, u32 id,
			     bool enable, u64 sec);
	int (*button_get)(const struct scmi_protocol_handle *ph, u32 *state);
};

enum scmi_nxp_notification_events {
	SCMI_EVENT_IMX_BBM_RTC = 0x0,
	SCMI_EVENT_IMX_BBM_BUTTON = 0x1,
	SCMI_EVENT_IMX_MISC_CONTROL = 0x0,
};

struct scmi_imx_bbm_notif_report {
	bool			is_rtc;
	bool			is_button;
	ktime_t			timestamp;
	unsigned int		rtc_id;
	unsigned int		rtc_evt;
};

struct scmi_imx_misc_ctrl_notify_report {
	ktime_t			timestamp;
	unsigned int		ctrl_id;
	unsigned int		flags;
};

struct scmi_imx_misc_proto_ops {
	int (*misc_ctrl_set)(const struct scmi_protocol_handle *ph, u32 id,
			     u32 num, u32 *val);
	int (*misc_ctrl_get)(const struct scmi_protocol_handle *ph, u32 id,
			     u32 *num, u32 *val);
	int (*misc_ctrl_req_notify)(const struct scmi_protocol_handle *ph,
				    u32 ctrl_id, u32 evt_id, u32 flags);
};
#endif
