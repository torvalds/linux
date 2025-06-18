/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Copyright (C) 2022-2024 OpenSynergy GmbH
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_VIRTIO_RTC_H
#define _LINUX_VIRTIO_RTC_H

#include <linux/types.h>

/* alarm feature */
#define VIRTIO_RTC_F_ALARM	0

/* read request message types */

#define VIRTIO_RTC_REQ_READ			0x0001
#define VIRTIO_RTC_REQ_READ_CROSS		0x0002

/* control request message types */

#define VIRTIO_RTC_REQ_CFG			0x1000
#define VIRTIO_RTC_REQ_CLOCK_CAP		0x1001
#define VIRTIO_RTC_REQ_CROSS_CAP		0x1002
#define VIRTIO_RTC_REQ_READ_ALARM		0x1003
#define VIRTIO_RTC_REQ_SET_ALARM		0x1004
#define VIRTIO_RTC_REQ_SET_ALARM_ENABLED	0x1005

/* alarmq message types */

#define VIRTIO_RTC_NOTIF_ALARM			0x2000

/* Message headers */

/** common request header */
struct virtio_rtc_req_head {
	__le16 msg_type;
	__u8 reserved[6];
};

/** common response header */
struct virtio_rtc_resp_head {
#define VIRTIO_RTC_S_OK			0
#define VIRTIO_RTC_S_EOPNOTSUPP		2
#define VIRTIO_RTC_S_ENODEV		3
#define VIRTIO_RTC_S_EINVAL		4
#define VIRTIO_RTC_S_EIO		5
	__u8 status;
	__u8 reserved[7];
};

/** common notification header */
struct virtio_rtc_notif_head {
	__le16 msg_type;
	__u8 reserved[6];
};

/* read requests */

/* VIRTIO_RTC_REQ_READ message */

struct virtio_rtc_req_read {
	struct virtio_rtc_req_head head;
	__le16 clock_id;
	__u8 reserved[6];
};

struct virtio_rtc_resp_read {
	struct virtio_rtc_resp_head head;
	__le64 clock_reading;
};

/* VIRTIO_RTC_REQ_READ_CROSS message */

struct virtio_rtc_req_read_cross {
	struct virtio_rtc_req_head head;
	__le16 clock_id;
/* Arm Generic Timer Counter-timer Virtual Count Register (CNTVCT_EL0) */
#define VIRTIO_RTC_COUNTER_ARM_VCT	0
/* x86 Time-Stamp Counter */
#define VIRTIO_RTC_COUNTER_X86_TSC	1
/* Invalid */
#define VIRTIO_RTC_COUNTER_INVALID	0xFF
	__u8 hw_counter;
	__u8 reserved[5];
};

struct virtio_rtc_resp_read_cross {
	struct virtio_rtc_resp_head head;
	__le64 clock_reading;
	__le64 counter_cycles;
};

/* control requests */

/* VIRTIO_RTC_REQ_CFG message */

struct virtio_rtc_req_cfg {
	struct virtio_rtc_req_head head;
	/* no request params */
};

struct virtio_rtc_resp_cfg {
	struct virtio_rtc_resp_head head;
	/** # of clocks -> clock ids < num_clocks are valid */
	__le16 num_clocks;
	__u8 reserved[6];
};

/* VIRTIO_RTC_REQ_CLOCK_CAP message */

struct virtio_rtc_req_clock_cap {
	struct virtio_rtc_req_head head;
	__le16 clock_id;
	__u8 reserved[6];
};

struct virtio_rtc_resp_clock_cap {
	struct virtio_rtc_resp_head head;
#define VIRTIO_RTC_CLOCK_UTC			0
#define VIRTIO_RTC_CLOCK_TAI			1
#define VIRTIO_RTC_CLOCK_MONOTONIC		2
#define VIRTIO_RTC_CLOCK_UTC_SMEARED		3
#define VIRTIO_RTC_CLOCK_UTC_MAYBE_SMEARED	4
	__u8 type;
#define VIRTIO_RTC_SMEAR_UNSPECIFIED	0
#define VIRTIO_RTC_SMEAR_NOON_LINEAR	1
#define VIRTIO_RTC_SMEAR_UTC_SLS	2
	__u8 leap_second_smearing;
#define VIRTIO_RTC_FLAG_ALARM_CAP		(1 << 0)
	__u8 flags;
	__u8 reserved[5];
};

/* VIRTIO_RTC_REQ_CROSS_CAP message */

struct virtio_rtc_req_cross_cap {
	struct virtio_rtc_req_head head;
	__le16 clock_id;
	__u8 hw_counter;
	__u8 reserved[5];
};

struct virtio_rtc_resp_cross_cap {
	struct virtio_rtc_resp_head head;
#define VIRTIO_RTC_FLAG_CROSS_CAP	(1 << 0)
	__u8 flags;
	__u8 reserved[7];
};

/* VIRTIO_RTC_REQ_READ_ALARM message */

struct virtio_rtc_req_read_alarm {
	struct virtio_rtc_req_head head;
	__le16 clock_id;
	__u8 reserved[6];
};

struct virtio_rtc_resp_read_alarm {
	struct virtio_rtc_resp_head head;
	__le64 alarm_time;
#define VIRTIO_RTC_FLAG_ALARM_ENABLED	(1 << 0)
	__u8 flags;
	__u8 reserved[7];
};

/* VIRTIO_RTC_REQ_SET_ALARM message */

struct virtio_rtc_req_set_alarm {
	struct virtio_rtc_req_head head;
	__le64 alarm_time;
	__le16 clock_id;
	/* flag VIRTIO_RTC_FLAG_ALARM_ENABLED */
	__u8 flags;
	__u8 reserved[5];
};

struct virtio_rtc_resp_set_alarm {
	struct virtio_rtc_resp_head head;
	/* no response params */
};

/* VIRTIO_RTC_REQ_SET_ALARM_ENABLED message */

struct virtio_rtc_req_set_alarm_enabled {
	struct virtio_rtc_req_head head;
	__le16 clock_id;
	/* flag VIRTIO_RTC_ALARM_ENABLED */
	__u8 flags;
	__u8 reserved[5];
};

struct virtio_rtc_resp_set_alarm_enabled {
	struct virtio_rtc_resp_head head;
	/* no response params */
};

/** Union of request types for requestq */
union virtio_rtc_req_requestq {
	struct virtio_rtc_req_read read;
	struct virtio_rtc_req_read_cross read_cross;
	struct virtio_rtc_req_cfg cfg;
	struct virtio_rtc_req_clock_cap clock_cap;
	struct virtio_rtc_req_cross_cap cross_cap;
	struct virtio_rtc_req_read_alarm read_alarm;
	struct virtio_rtc_req_set_alarm set_alarm;
	struct virtio_rtc_req_set_alarm_enabled set_alarm_enabled;
};

/** Union of response types for requestq */
union virtio_rtc_resp_requestq {
	struct virtio_rtc_resp_read read;
	struct virtio_rtc_resp_read_cross read_cross;
	struct virtio_rtc_resp_cfg cfg;
	struct virtio_rtc_resp_clock_cap clock_cap;
	struct virtio_rtc_resp_cross_cap cross_cap;
	struct virtio_rtc_resp_read_alarm read_alarm;
	struct virtio_rtc_resp_set_alarm set_alarm;
	struct virtio_rtc_resp_set_alarm_enabled set_alarm_enabled;
};

/* alarmq notifications */

/* VIRTIO_RTC_NOTIF_ALARM notification */

struct virtio_rtc_notif_alarm {
	struct virtio_rtc_notif_head head;
	__le16 clock_id;
	__u8 reserved[6];
};

/** Union of notification types for alarmq */
union virtio_rtc_notif_alarmq {
	struct virtio_rtc_notif_alarm alarm;
};

#endif /* _LINUX_VIRTIO_RTC_H */
