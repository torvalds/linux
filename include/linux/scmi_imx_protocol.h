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
#include <linux/scmi_protocol.h>
#include <linux/types.h>

#define SCMI_PROTOCOL_IMX_LMM	0x80
#define	SCMI_PROTOCOL_IMX_BBM	0x81
#define SCMI_PROTOCOL_IMX_CPU	0x82
#define	SCMI_PROTOCOL_IMX_MISC	0x84

#define SCMI_IMX_VENDOR		"NXP"
#define SCMI_IMX_SUBVENDOR	"IMX"

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

/* See LMM_ATTRIBUTES in imx95.rst */
#define	LMM_ID_DISCOVER	0xFFFFFFFFU
#define	LMM_MAX_NAME	16

enum scmi_imx_lmm_state {
	LMM_STATE_LM_OFF,
	LMM_STATE_LM_ON,
	LMM_STATE_LM_SUSPEND,
	LMM_STATE_LM_POWERED,
};

struct scmi_imx_lmm_info {
	u32 lmid;
	enum scmi_imx_lmm_state state;
	u32 errstatus;
	u8 name[LMM_MAX_NAME];
};

struct scmi_imx_lmm_proto_ops {
	int (*lmm_power_boot)(const struct scmi_protocol_handle *ph, u32 lmid,
			      bool boot);
	int (*lmm_info)(const struct scmi_protocol_handle *ph, u32 lmid,
			struct scmi_imx_lmm_info *info);
	int (*lmm_reset_vector_set)(const struct scmi_protocol_handle *ph,
				    u32 lmid, u32 cpuid, u32 flags, u64 vector);
	int (*lmm_shutdown)(const struct scmi_protocol_handle *ph, u32 lmid,
			    u32 flags);
};

struct scmi_imx_cpu_proto_ops {
	int (*cpu_reset_vector_set)(const struct scmi_protocol_handle *ph,
				    u32 cpuid, u64 vector, bool start,
				    bool boot, bool resume);
	int (*cpu_start)(const struct scmi_protocol_handle *ph, u32 cpuid,
			 bool start);
	int (*cpu_started)(const struct scmi_protocol_handle *ph, u32 cpuid,
			   bool *started);
};
#endif
