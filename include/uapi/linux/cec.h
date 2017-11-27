/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * cec - HDMI Consumer Electronics Control public header
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Alternatively you can redistribute this file under the terms of the
 * BSD license as stated below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. The names of its contributors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _CEC_UAPI_H
#define _CEC_UAPI_H

#include <linux/types.h>
#include <linux/string.h>

#define CEC_MAX_MSG_SIZE	16

/**
 * struct cec_msg - CEC message structure.
 * @tx_ts:	Timestamp in nanoseconds using CLOCK_MONOTONIC. Set by the
 *		driver when the message transmission has finished.
 * @rx_ts:	Timestamp in nanoseconds using CLOCK_MONOTONIC. Set by the
 *		driver when the message was received.
 * @len:	Length in bytes of the message.
 * @timeout:	The timeout (in ms) that is used to timeout CEC_RECEIVE.
 *		Set to 0 if you want to wait forever. This timeout can also be
 *		used with CEC_TRANSMIT as the timeout for waiting for a reply.
 *		If 0, then it will use a 1 second timeout instead of waiting
 *		forever as is done with CEC_RECEIVE.
 * @sequence:	The framework assigns a sequence number to messages that are
 *		sent. This can be used to track replies to previously sent
 *		messages.
 * @flags:	Set to 0.
 * @msg:	The message payload.
 * @reply:	This field is ignored with CEC_RECEIVE and is only used by
 *		CEC_TRANSMIT. If non-zero, then wait for a reply with this
 *		opcode. Set to CEC_MSG_FEATURE_ABORT if you want to wait for
 *		a possible ABORT reply. If there was an error when sending the
 *		msg or FeatureAbort was returned, then reply is set to 0.
 *		If reply is non-zero upon return, then len/msg are set to
 *		the received message.
 *		If reply is zero upon return and status has the
 *		CEC_TX_STATUS_FEATURE_ABORT bit set, then len/msg are set to
 *		the received feature abort message.
 *		If reply is zero upon return and status has the
 *		CEC_TX_STATUS_MAX_RETRIES bit set, then no reply was seen at
 *		all. If reply is non-zero for CEC_TRANSMIT and the message is a
 *		broadcast, then -EINVAL is returned.
 *		if reply is non-zero, then timeout is set to 1000 (the required
 *		maximum response time).
 * @rx_status:	The message receive status bits. Set by the driver.
 * @tx_status:	The message transmit status bits. Set by the driver.
 * @tx_arb_lost_cnt: The number of 'Arbitration Lost' events. Set by the driver.
 * @tx_nack_cnt: The number of 'Not Acknowledged' events. Set by the driver.
 * @tx_low_drive_cnt: The number of 'Low Drive Detected' events. Set by the
 *		driver.
 * @tx_error_cnt: The number of 'Error' events. Set by the driver.
 */
struct cec_msg {
	__u64 tx_ts;
	__u64 rx_ts;
	__u32 len;
	__u32 timeout;
	__u32 sequence;
	__u32 flags;
	__u8 msg[CEC_MAX_MSG_SIZE];
	__u8 reply;
	__u8 rx_status;
	__u8 tx_status;
	__u8 tx_arb_lost_cnt;
	__u8 tx_nack_cnt;
	__u8 tx_low_drive_cnt;
	__u8 tx_error_cnt;
};

/**
 * cec_msg_initiator - return the initiator's logical address.
 * @msg:	the message structure
 */
static inline __u8 cec_msg_initiator(const struct cec_msg *msg)
{
	return msg->msg[0] >> 4;
}

/**
 * cec_msg_destination - return the destination's logical address.
 * @msg:	the message structure
 */
static inline __u8 cec_msg_destination(const struct cec_msg *msg)
{
	return msg->msg[0] & 0xf;
}

/**
 * cec_msg_opcode - return the opcode of the message, -1 for poll
 * @msg:	the message structure
 */
static inline int cec_msg_opcode(const struct cec_msg *msg)
{
	return msg->len > 1 ? msg->msg[1] : -1;
}

/**
 * cec_msg_is_broadcast - return true if this is a broadcast message.
 * @msg:	the message structure
 */
static inline int cec_msg_is_broadcast(const struct cec_msg *msg)
{
	return (msg->msg[0] & 0xf) == 0xf;
}

/**
 * cec_msg_init - initialize the message structure.
 * @msg:	the message structure
 * @initiator:	the logical address of the initiator
 * @destination:the logical address of the destination (0xf for broadcast)
 *
 * The whole structure is zeroed, the len field is set to 1 (i.e. a poll
 * message) and the initiator and destination are filled in.
 */
static inline void cec_msg_init(struct cec_msg *msg,
				__u8 initiator, __u8 destination)
{
	memset(msg, 0, sizeof(*msg));
	msg->msg[0] = (initiator << 4) | destination;
	msg->len = 1;
}

/**
 * cec_msg_set_reply_to - fill in destination/initiator in a reply message.
 * @msg:	the message structure for the reply
 * @orig:	the original message structure
 *
 * Set the msg destination to the orig initiator and the msg initiator to the
 * orig destination. Note that msg and orig may be the same pointer, in which
 * case the change is done in place.
 */
static inline void cec_msg_set_reply_to(struct cec_msg *msg,
					struct cec_msg *orig)
{
	/* The destination becomes the initiator and vice versa */
	msg->msg[0] = (cec_msg_destination(orig) << 4) |
		      cec_msg_initiator(orig);
	msg->reply = msg->timeout = 0;
}

/* cec_msg flags field */
#define CEC_MSG_FL_REPLY_TO_FOLLOWERS	(1 << 0)

/* cec_msg tx/rx_status field */
#define CEC_TX_STATUS_OK		(1 << 0)
#define CEC_TX_STATUS_ARB_LOST		(1 << 1)
#define CEC_TX_STATUS_NACK		(1 << 2)
#define CEC_TX_STATUS_LOW_DRIVE		(1 << 3)
#define CEC_TX_STATUS_ERROR		(1 << 4)
#define CEC_TX_STATUS_MAX_RETRIES	(1 << 5)

#define CEC_RX_STATUS_OK		(1 << 0)
#define CEC_RX_STATUS_TIMEOUT		(1 << 1)
#define CEC_RX_STATUS_FEATURE_ABORT	(1 << 2)

static inline int cec_msg_status_is_ok(const struct cec_msg *msg)
{
	if (msg->tx_status && !(msg->tx_status & CEC_TX_STATUS_OK))
		return 0;
	if (msg->rx_status && !(msg->rx_status & CEC_RX_STATUS_OK))
		return 0;
	if (!msg->tx_status && !msg->rx_status)
		return 0;
	return !(msg->rx_status & CEC_RX_STATUS_FEATURE_ABORT);
}

#define CEC_LOG_ADDR_INVALID		0xff
#define CEC_PHYS_ADDR_INVALID		0xffff

/*
 * The maximum number of logical addresses one device can be assigned to.
 * The CEC 2.0 spec allows for only 2 logical addresses at the moment. The
 * Analog Devices CEC hardware supports 3. So let's go wild and go for 4.
 */
#define CEC_MAX_LOG_ADDRS 4

/* The logical addresses defined by CEC 2.0 */
#define CEC_LOG_ADDR_TV			0
#define CEC_LOG_ADDR_RECORD_1		1
#define CEC_LOG_ADDR_RECORD_2		2
#define CEC_LOG_ADDR_TUNER_1		3
#define CEC_LOG_ADDR_PLAYBACK_1		4
#define CEC_LOG_ADDR_AUDIOSYSTEM	5
#define CEC_LOG_ADDR_TUNER_2		6
#define CEC_LOG_ADDR_TUNER_3		7
#define CEC_LOG_ADDR_PLAYBACK_2		8
#define CEC_LOG_ADDR_RECORD_3		9
#define CEC_LOG_ADDR_TUNER_4		10
#define CEC_LOG_ADDR_PLAYBACK_3		11
#define CEC_LOG_ADDR_BACKUP_1		12
#define CEC_LOG_ADDR_BACKUP_2		13
#define CEC_LOG_ADDR_SPECIFIC		14
#define CEC_LOG_ADDR_UNREGISTERED	15 /* as initiator address */
#define CEC_LOG_ADDR_BROADCAST		15 /* as destination address */

/* The logical address types that the CEC device wants to claim */
#define CEC_LOG_ADDR_TYPE_TV		0
#define CEC_LOG_ADDR_TYPE_RECORD	1
#define CEC_LOG_ADDR_TYPE_TUNER		2
#define CEC_LOG_ADDR_TYPE_PLAYBACK	3
#define CEC_LOG_ADDR_TYPE_AUDIOSYSTEM	4
#define CEC_LOG_ADDR_TYPE_SPECIFIC	5
#define CEC_LOG_ADDR_TYPE_UNREGISTERED	6
/*
 * Switches should use UNREGISTERED.
 * Processors should use SPECIFIC.
 */

#define CEC_LOG_ADDR_MASK_TV		(1 << CEC_LOG_ADDR_TV)
#define CEC_LOG_ADDR_MASK_RECORD	((1 << CEC_LOG_ADDR_RECORD_1) | \
					 (1 << CEC_LOG_ADDR_RECORD_2) | \
					 (1 << CEC_LOG_ADDR_RECORD_3))
#define CEC_LOG_ADDR_MASK_TUNER		((1 << CEC_LOG_ADDR_TUNER_1) | \
					 (1 << CEC_LOG_ADDR_TUNER_2) | \
					 (1 << CEC_LOG_ADDR_TUNER_3) | \
					 (1 << CEC_LOG_ADDR_TUNER_4))
#define CEC_LOG_ADDR_MASK_PLAYBACK	((1 << CEC_LOG_ADDR_PLAYBACK_1) | \
					 (1 << CEC_LOG_ADDR_PLAYBACK_2) | \
					 (1 << CEC_LOG_ADDR_PLAYBACK_3))
#define CEC_LOG_ADDR_MASK_AUDIOSYSTEM	(1 << CEC_LOG_ADDR_AUDIOSYSTEM)
#define CEC_LOG_ADDR_MASK_BACKUP	((1 << CEC_LOG_ADDR_BACKUP_1) | \
					 (1 << CEC_LOG_ADDR_BACKUP_2))
#define CEC_LOG_ADDR_MASK_SPECIFIC	(1 << CEC_LOG_ADDR_SPECIFIC)
#define CEC_LOG_ADDR_MASK_UNREGISTERED	(1 << CEC_LOG_ADDR_UNREGISTERED)

static inline int cec_has_tv(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_TV;
}

static inline int cec_has_record(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_RECORD;
}

static inline int cec_has_tuner(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_TUNER;
}

static inline int cec_has_playback(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_PLAYBACK;
}

static inline int cec_has_audiosystem(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_AUDIOSYSTEM;
}

static inline int cec_has_backup(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_BACKUP;
}

static inline int cec_has_specific(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_SPECIFIC;
}

static inline int cec_is_unregistered(__u16 log_addr_mask)
{
	return log_addr_mask & CEC_LOG_ADDR_MASK_UNREGISTERED;
}

static inline int cec_is_unconfigured(__u16 log_addr_mask)
{
	return log_addr_mask == 0;
}

/*
 * Use this if there is no vendor ID (CEC_G_VENDOR_ID) or if the vendor ID
 * should be disabled (CEC_S_VENDOR_ID)
 */
#define CEC_VENDOR_ID_NONE		0xffffffff

/* The message handling modes */
/* Modes for initiator */
#define CEC_MODE_NO_INITIATOR		(0x0 << 0)
#define CEC_MODE_INITIATOR		(0x1 << 0)
#define CEC_MODE_EXCL_INITIATOR		(0x2 << 0)
#define CEC_MODE_INITIATOR_MSK		0x0f

/* Modes for follower */
#define CEC_MODE_NO_FOLLOWER		(0x0 << 4)
#define CEC_MODE_FOLLOWER		(0x1 << 4)
#define CEC_MODE_EXCL_FOLLOWER		(0x2 << 4)
#define CEC_MODE_EXCL_FOLLOWER_PASSTHRU	(0x3 << 4)
#define CEC_MODE_MONITOR_PIN		(0xd << 4)
#define CEC_MODE_MONITOR		(0xe << 4)
#define CEC_MODE_MONITOR_ALL		(0xf << 4)
#define CEC_MODE_FOLLOWER_MSK		0xf0

/* Userspace has to configure the physical address */
#define CEC_CAP_PHYS_ADDR	(1 << 0)
/* Userspace has to configure the logical addresses */
#define CEC_CAP_LOG_ADDRS	(1 << 1)
/* Userspace can transmit messages (and thus become follower as well) */
#define CEC_CAP_TRANSMIT	(1 << 2)
/*
 * Passthrough all messages instead of processing them.
 */
#define CEC_CAP_PASSTHROUGH	(1 << 3)
/* Supports remote control */
#define CEC_CAP_RC		(1 << 4)
/* Hardware can monitor all messages, not just directed and broadcast. */
#define CEC_CAP_MONITOR_ALL	(1 << 5)
/* Hardware can use CEC only if the HDMI HPD pin is high. */
#define CEC_CAP_NEEDS_HPD	(1 << 6)
/* Hardware can monitor CEC pin transitions */
#define CEC_CAP_MONITOR_PIN	(1 << 7)

/**
 * struct cec_caps - CEC capabilities structure.
 * @driver: name of the CEC device driver.
 * @name: name of the CEC device. @driver + @name must be unique.
 * @available_log_addrs: number of available logical addresses.
 * @capabilities: capabilities of the CEC adapter.
 * @version: version of the CEC adapter framework.
 */
struct cec_caps {
	char driver[32];
	char name[32];
	__u32 available_log_addrs;
	__u32 capabilities;
	__u32 version;
};

/**
 * struct cec_log_addrs - CEC logical addresses structure.
 * @log_addr: the claimed logical addresses. Set by the driver.
 * @log_addr_mask: current logical address mask. Set by the driver.
 * @cec_version: the CEC version that the adapter should implement. Set by the
 *	caller.
 * @num_log_addrs: how many logical addresses should be claimed. Set by the
 *	caller.
 * @vendor_id: the vendor ID of the device. Set by the caller.
 * @flags: flags.
 * @osd_name: the OSD name of the device. Set by the caller.
 * @primary_device_type: the primary device type for each logical address.
 *	Set by the caller.
 * @log_addr_type: the logical address types. Set by the caller.
 * @all_device_types: CEC 2.0: all device types represented by the logical
 *	address. Set by the caller.
 * @features:	CEC 2.0: The logical address features. Set by the caller.
 */
struct cec_log_addrs {
	__u8 log_addr[CEC_MAX_LOG_ADDRS];
	__u16 log_addr_mask;
	__u8 cec_version;
	__u8 num_log_addrs;
	__u32 vendor_id;
	__u32 flags;
	char osd_name[15];
	__u8 primary_device_type[CEC_MAX_LOG_ADDRS];
	__u8 log_addr_type[CEC_MAX_LOG_ADDRS];

	/* CEC 2.0 */
	__u8 all_device_types[CEC_MAX_LOG_ADDRS];
	__u8 features[CEC_MAX_LOG_ADDRS][12];
};

/* Allow a fallback to unregistered */
#define CEC_LOG_ADDRS_FL_ALLOW_UNREG_FALLBACK	(1 << 0)
/* Passthrough RC messages to the input subsystem */
#define CEC_LOG_ADDRS_FL_ALLOW_RC_PASSTHRU	(1 << 1)
/* CDC-Only device: supports only CDC messages */
#define CEC_LOG_ADDRS_FL_CDC_ONLY		(1 << 2)

/* Events */

/* Event that occurs when the adapter state changes */
#define CEC_EVENT_STATE_CHANGE		1
/*
 * This event is sent when messages are lost because the application
 * didn't empty the message queue in time
 */
#define CEC_EVENT_LOST_MSGS		2
#define CEC_EVENT_PIN_CEC_LOW		3
#define CEC_EVENT_PIN_CEC_HIGH		4
#define CEC_EVENT_PIN_HPD_LOW		5
#define CEC_EVENT_PIN_HPD_HIGH		6

#define CEC_EVENT_FL_INITIAL_STATE	(1 << 0)
#define CEC_EVENT_FL_DROPPED_EVENTS	(1 << 1)

/**
 * struct cec_event_state_change - used when the CEC adapter changes state.
 * @phys_addr: the current physical address
 * @log_addr_mask: the current logical address mask
 */
struct cec_event_state_change {
	__u16 phys_addr;
	__u16 log_addr_mask;
};

/**
 * struct cec_event_lost_msgs - tells you how many messages were lost.
 * @lost_msgs: how many messages were lost.
 */
struct cec_event_lost_msgs {
	__u32 lost_msgs;
};

/**
 * struct cec_event - CEC event structure
 * @ts: the timestamp of when the event was sent.
 * @event: the event.
 * array.
 * @state_change: the event payload for CEC_EVENT_STATE_CHANGE.
 * @lost_msgs: the event payload for CEC_EVENT_LOST_MSGS.
 * @raw: array to pad the union.
 */
struct cec_event {
	__u64 ts;
	__u32 event;
	__u32 flags;
	union {
		struct cec_event_state_change state_change;
		struct cec_event_lost_msgs lost_msgs;
		__u32 raw[16];
	};
};

/* ioctls */

/* Adapter capabilities */
#define CEC_ADAP_G_CAPS		_IOWR('a',  0, struct cec_caps)

/*
 * phys_addr is either 0 (if this is the CEC root device)
 * or a valid physical address obtained from the sink's EDID
 * as read by this CEC device (if this is a source device)
 * or a physical address obtained and modified from a sink
 * EDID and used for a sink CEC device.
 * If nothing is connected, then phys_addr is 0xffff.
 * See HDMI 1.4b, section 8.7 (Physical Address).
 *
 * The CEC_ADAP_S_PHYS_ADDR ioctl may not be available if that is handled
 * internally.
 */
#define CEC_ADAP_G_PHYS_ADDR	_IOR('a',  1, __u16)
#define CEC_ADAP_S_PHYS_ADDR	_IOW('a',  2, __u16)

/*
 * Configure the CEC adapter. It sets the device type and which
 * logical types it will try to claim. It will return which
 * logical addresses it could actually claim.
 * An error is returned if the adapter is disabled or if there
 * is no physical address assigned.
 */

#define CEC_ADAP_G_LOG_ADDRS	_IOR('a',  3, struct cec_log_addrs)
#define CEC_ADAP_S_LOG_ADDRS	_IOWR('a',  4, struct cec_log_addrs)

/* Transmit/receive a CEC command */
#define CEC_TRANSMIT		_IOWR('a',  5, struct cec_msg)
#define CEC_RECEIVE		_IOWR('a',  6, struct cec_msg)

/* Dequeue CEC events */
#define CEC_DQEVENT		_IOWR('a',  7, struct cec_event)

/*
 * Get and set the message handling mode for this filehandle.
 */
#define CEC_G_MODE		_IOR('a',  8, __u32)
#define CEC_S_MODE		_IOW('a',  9, __u32)

/*
 * The remainder of this header defines all CEC messages and operands.
 * The format matters since it the cec-ctl utility parses it to generate
 * code for implementing all these messages.
 *
 * Comments ending with 'Feature' group messages for each feature.
 * If messages are part of multiple features, then the "Has also"
 * comment is used to list the previously defined messages that are
 * supported by the feature.
 *
 * Before operands are defined a comment is added that gives the
 * name of the operand and in brackets the variable name of the
 * corresponding argument in the cec-funcs.h function.
 */

/* Messages */

/* One Touch Play Feature */
#define CEC_MSG_ACTIVE_SOURCE				0x82
#define CEC_MSG_IMAGE_VIEW_ON				0x04
#define CEC_MSG_TEXT_VIEW_ON				0x0d


/* Routing Control Feature */

/*
 * Has also:
 *	CEC_MSG_ACTIVE_SOURCE
 */

#define CEC_MSG_INACTIVE_SOURCE				0x9d
#define CEC_MSG_REQUEST_ACTIVE_SOURCE			0x85
#define CEC_MSG_ROUTING_CHANGE				0x80
#define CEC_MSG_ROUTING_INFORMATION			0x81
#define CEC_MSG_SET_STREAM_PATH				0x86


/* Standby Feature */
#define CEC_MSG_STANDBY					0x36


/* One Touch Record Feature */
#define CEC_MSG_RECORD_OFF				0x0b
#define CEC_MSG_RECORD_ON				0x09
/* Record Source Type Operand (rec_src_type) */
#define CEC_OP_RECORD_SRC_OWN				1
#define CEC_OP_RECORD_SRC_DIGITAL			2
#define CEC_OP_RECORD_SRC_ANALOG			3
#define CEC_OP_RECORD_SRC_EXT_PLUG			4
#define CEC_OP_RECORD_SRC_EXT_PHYS_ADDR			5
/* Service Identification Method Operand (service_id_method) */
#define CEC_OP_SERVICE_ID_METHOD_BY_DIG_ID		0
#define CEC_OP_SERVICE_ID_METHOD_BY_CHANNEL		1
/* Digital Service Broadcast System Operand (dig_bcast_system) */
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ARIB_GEN	0x00
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_GEN	0x01
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_DVB_GEN		0x02
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ARIB_BS		0x08
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ARIB_CS		0x09
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ARIB_T		0x0a
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_CABLE	0x10
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_SAT	0x11
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_T		0x12
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_DVB_C		0x18
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_DVB_S		0x19
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_DVB_S2		0x1a
#define CEC_OP_DIG_SERVICE_BCAST_SYSTEM_DVB_T		0x1b
/* Analogue Broadcast Type Operand (ana_bcast_type) */
#define CEC_OP_ANA_BCAST_TYPE_CABLE			0
#define CEC_OP_ANA_BCAST_TYPE_SATELLITE			1
#define CEC_OP_ANA_BCAST_TYPE_TERRESTRIAL		2
/* Broadcast System Operand (bcast_system) */
#define CEC_OP_BCAST_SYSTEM_PAL_BG			0x00
#define CEC_OP_BCAST_SYSTEM_SECAM_LQ			0x01 /* SECAM L' */
#define CEC_OP_BCAST_SYSTEM_PAL_M			0x02
#define CEC_OP_BCAST_SYSTEM_NTSC_M			0x03
#define CEC_OP_BCAST_SYSTEM_PAL_I			0x04
#define CEC_OP_BCAST_SYSTEM_SECAM_DK			0x05
#define CEC_OP_BCAST_SYSTEM_SECAM_BG			0x06
#define CEC_OP_BCAST_SYSTEM_SECAM_L			0x07
#define CEC_OP_BCAST_SYSTEM_PAL_DK			0x08
#define CEC_OP_BCAST_SYSTEM_OTHER			0x1f
/* Channel Number Format Operand (channel_number_fmt) */
#define CEC_OP_CHANNEL_NUMBER_FMT_1_PART		0x01
#define CEC_OP_CHANNEL_NUMBER_FMT_2_PART		0x02

#define CEC_MSG_RECORD_STATUS				0x0a
/* Record Status Operand (rec_status) */
#define CEC_OP_RECORD_STATUS_CUR_SRC			0x01
#define CEC_OP_RECORD_STATUS_DIG_SERVICE		0x02
#define CEC_OP_RECORD_STATUS_ANA_SERVICE		0x03
#define CEC_OP_RECORD_STATUS_EXT_INPUT			0x04
#define CEC_OP_RECORD_STATUS_NO_DIG_SERVICE		0x05
#define CEC_OP_RECORD_STATUS_NO_ANA_SERVICE		0x06
#define CEC_OP_RECORD_STATUS_NO_SERVICE			0x07
#define CEC_OP_RECORD_STATUS_INVALID_EXT_PLUG		0x09
#define CEC_OP_RECORD_STATUS_INVALID_EXT_PHYS_ADDR	0x0a
#define CEC_OP_RECORD_STATUS_UNSUP_CA			0x0b
#define CEC_OP_RECORD_STATUS_NO_CA_ENTITLEMENTS		0x0c
#define CEC_OP_RECORD_STATUS_CANT_COPY_SRC		0x0d
#define CEC_OP_RECORD_STATUS_NO_MORE_COPIES		0x0e
#define CEC_OP_RECORD_STATUS_NO_MEDIA			0x10
#define CEC_OP_RECORD_STATUS_PLAYING			0x11
#define CEC_OP_RECORD_STATUS_ALREADY_RECORDING		0x12
#define CEC_OP_RECORD_STATUS_MEDIA_PROT			0x13
#define CEC_OP_RECORD_STATUS_NO_SIGNAL			0x14
#define CEC_OP_RECORD_STATUS_MEDIA_PROBLEM		0x15
#define CEC_OP_RECORD_STATUS_NO_SPACE			0x16
#define CEC_OP_RECORD_STATUS_PARENTAL_LOCK		0x17
#define CEC_OP_RECORD_STATUS_TERMINATED_OK		0x1a
#define CEC_OP_RECORD_STATUS_ALREADY_TERM		0x1b
#define CEC_OP_RECORD_STATUS_OTHER			0x1f

#define CEC_MSG_RECORD_TV_SCREEN			0x0f


/* Timer Programming Feature */
#define CEC_MSG_CLEAR_ANALOGUE_TIMER			0x33
/* Recording Sequence Operand (recording_seq) */
#define CEC_OP_REC_SEQ_SUNDAY				0x01
#define CEC_OP_REC_SEQ_MONDAY				0x02
#define CEC_OP_REC_SEQ_TUESDAY				0x04
#define CEC_OP_REC_SEQ_WEDNESDAY			0x08
#define CEC_OP_REC_SEQ_THURSDAY				0x10
#define CEC_OP_REC_SEQ_FRIDAY				0x20
#define CEC_OP_REC_SEQ_SATERDAY				0x40
#define CEC_OP_REC_SEQ_ONCE_ONLY			0x00

#define CEC_MSG_CLEAR_DIGITAL_TIMER			0x99

#define CEC_MSG_CLEAR_EXT_TIMER				0xa1
/* External Source Specifier Operand (ext_src_spec) */
#define CEC_OP_EXT_SRC_PLUG				0x04
#define CEC_OP_EXT_SRC_PHYS_ADDR			0x05

#define CEC_MSG_SET_ANALOGUE_TIMER			0x34
#define CEC_MSG_SET_DIGITAL_TIMER			0x97
#define CEC_MSG_SET_EXT_TIMER				0xa2

#define CEC_MSG_SET_TIMER_PROGRAM_TITLE			0x67
#define CEC_MSG_TIMER_CLEARED_STATUS			0x43
/* Timer Cleared Status Data Operand (timer_cleared_status) */
#define CEC_OP_TIMER_CLR_STAT_RECORDING			0x00
#define CEC_OP_TIMER_CLR_STAT_NO_MATCHING		0x01
#define CEC_OP_TIMER_CLR_STAT_NO_INFO			0x02
#define CEC_OP_TIMER_CLR_STAT_CLEARED			0x80

#define CEC_MSG_TIMER_STATUS				0x35
/* Timer Overlap Warning Operand (timer_overlap_warning) */
#define CEC_OP_TIMER_OVERLAP_WARNING_NO_OVERLAP		0
#define CEC_OP_TIMER_OVERLAP_WARNING_OVERLAP		1
/* Media Info Operand (media_info) */
#define CEC_OP_MEDIA_INFO_UNPROT_MEDIA			0
#define CEC_OP_MEDIA_INFO_PROT_MEDIA			1
#define CEC_OP_MEDIA_INFO_NO_MEDIA			2
/* Programmed Indicator Operand (prog_indicator) */
#define CEC_OP_PROG_IND_NOT_PROGRAMMED			0
#define CEC_OP_PROG_IND_PROGRAMMED			1
/* Programmed Info Operand (prog_info) */
#define CEC_OP_PROG_INFO_ENOUGH_SPACE			0x08
#define CEC_OP_PROG_INFO_NOT_ENOUGH_SPACE		0x09
#define CEC_OP_PROG_INFO_MIGHT_NOT_BE_ENOUGH_SPACE	0x0b
#define CEC_OP_PROG_INFO_NONE_AVAILABLE			0x0a
/* Not Programmed Error Info Operand (prog_error) */
#define CEC_OP_PROG_ERROR_NO_FREE_TIMER			0x01
#define CEC_OP_PROG_ERROR_DATE_OUT_OF_RANGE		0x02
#define CEC_OP_PROG_ERROR_REC_SEQ_ERROR			0x03
#define CEC_OP_PROG_ERROR_INV_EXT_PLUG			0x04
#define CEC_OP_PROG_ERROR_INV_EXT_PHYS_ADDR		0x05
#define CEC_OP_PROG_ERROR_CA_UNSUPP			0x06
#define CEC_OP_PROG_ERROR_INSUF_CA_ENTITLEMENTS		0x07
#define CEC_OP_PROG_ERROR_RESOLUTION_UNSUPP		0x08
#define CEC_OP_PROG_ERROR_PARENTAL_LOCK			0x09
#define CEC_OP_PROG_ERROR_CLOCK_FAILURE			0x0a
#define CEC_OP_PROG_ERROR_DUPLICATE			0x0e


/* System Information Feature */
#define CEC_MSG_CEC_VERSION				0x9e
/* CEC Version Operand (cec_version) */
#define CEC_OP_CEC_VERSION_1_3A				4
#define CEC_OP_CEC_VERSION_1_4				5
#define CEC_OP_CEC_VERSION_2_0				6

#define CEC_MSG_GET_CEC_VERSION				0x9f
#define CEC_MSG_GIVE_PHYSICAL_ADDR			0x83
#define CEC_MSG_GET_MENU_LANGUAGE			0x91
#define CEC_MSG_REPORT_PHYSICAL_ADDR			0x84
/* Primary Device Type Operand (prim_devtype) */
#define CEC_OP_PRIM_DEVTYPE_TV				0
#define CEC_OP_PRIM_DEVTYPE_RECORD			1
#define CEC_OP_PRIM_DEVTYPE_TUNER			3
#define CEC_OP_PRIM_DEVTYPE_PLAYBACK			4
#define CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM			5
#define CEC_OP_PRIM_DEVTYPE_SWITCH			6
#define CEC_OP_PRIM_DEVTYPE_PROCESSOR			7

#define CEC_MSG_SET_MENU_LANGUAGE			0x32
#define CEC_MSG_REPORT_FEATURES				0xa6	/* HDMI 2.0 */
/* All Device Types Operand (all_device_types) */
#define CEC_OP_ALL_DEVTYPE_TV				0x80
#define CEC_OP_ALL_DEVTYPE_RECORD			0x40
#define CEC_OP_ALL_DEVTYPE_TUNER			0x20
#define CEC_OP_ALL_DEVTYPE_PLAYBACK			0x10
#define CEC_OP_ALL_DEVTYPE_AUDIOSYSTEM			0x08
#define CEC_OP_ALL_DEVTYPE_SWITCH			0x04
/*
 * And if you wondering what happened to PROCESSOR devices: those should
 * be mapped to a SWITCH.
 */

/* Valid for RC Profile and Device Feature operands */
#define CEC_OP_FEAT_EXT					0x80	/* Extension bit */
/* RC Profile Operand (rc_profile) */
#define CEC_OP_FEAT_RC_TV_PROFILE_NONE			0x00
#define CEC_OP_FEAT_RC_TV_PROFILE_1			0x02
#define CEC_OP_FEAT_RC_TV_PROFILE_2			0x06
#define CEC_OP_FEAT_RC_TV_PROFILE_3			0x0a
#define CEC_OP_FEAT_RC_TV_PROFILE_4			0x0e
#define CEC_OP_FEAT_RC_SRC_HAS_DEV_ROOT_MENU		0x50
#define CEC_OP_FEAT_RC_SRC_HAS_DEV_SETUP_MENU		0x48
#define CEC_OP_FEAT_RC_SRC_HAS_CONTENTS_MENU		0x44
#define CEC_OP_FEAT_RC_SRC_HAS_MEDIA_TOP_MENU		0x42
#define CEC_OP_FEAT_RC_SRC_HAS_MEDIA_CONTEXT_MENU	0x41
/* Device Feature Operand (dev_features) */
#define CEC_OP_FEAT_DEV_HAS_RECORD_TV_SCREEN		0x40
#define CEC_OP_FEAT_DEV_HAS_SET_OSD_STRING		0x20
#define CEC_OP_FEAT_DEV_HAS_DECK_CONTROL		0x10
#define CEC_OP_FEAT_DEV_HAS_SET_AUDIO_RATE		0x08
#define CEC_OP_FEAT_DEV_SINK_HAS_ARC_TX			0x04
#define CEC_OP_FEAT_DEV_SOURCE_HAS_ARC_RX		0x02

#define CEC_MSG_GIVE_FEATURES				0xa5	/* HDMI 2.0 */


/* Deck Control Feature */
#define CEC_MSG_DECK_CONTROL				0x42
/* Deck Control Mode Operand (deck_control_mode) */
#define CEC_OP_DECK_CTL_MODE_SKIP_FWD			1
#define CEC_OP_DECK_CTL_MODE_SKIP_REV			2
#define CEC_OP_DECK_CTL_MODE_STOP			3
#define CEC_OP_DECK_CTL_MODE_EJECT			4

#define CEC_MSG_DECK_STATUS				0x1b
/* Deck Info Operand (deck_info) */
#define CEC_OP_DECK_INFO_PLAY				0x11
#define CEC_OP_DECK_INFO_RECORD				0x12
#define CEC_OP_DECK_INFO_PLAY_REV			0x13
#define CEC_OP_DECK_INFO_STILL				0x14
#define CEC_OP_DECK_INFO_SLOW				0x15
#define CEC_OP_DECK_INFO_SLOW_REV			0x16
#define CEC_OP_DECK_INFO_FAST_FWD			0x17
#define CEC_OP_DECK_INFO_FAST_REV			0x18
#define CEC_OP_DECK_INFO_NO_MEDIA			0x19
#define CEC_OP_DECK_INFO_STOP				0x1a
#define CEC_OP_DECK_INFO_SKIP_FWD			0x1b
#define CEC_OP_DECK_INFO_SKIP_REV			0x1c
#define CEC_OP_DECK_INFO_INDEX_SEARCH_FWD		0x1d
#define CEC_OP_DECK_INFO_INDEX_SEARCH_REV		0x1e
#define CEC_OP_DECK_INFO_OTHER				0x1f

#define CEC_MSG_GIVE_DECK_STATUS			0x1a
/* Status Request Operand (status_req) */
#define CEC_OP_STATUS_REQ_ON				1
#define CEC_OP_STATUS_REQ_OFF				2
#define CEC_OP_STATUS_REQ_ONCE				3

#define CEC_MSG_PLAY					0x41
/* Play Mode Operand (play_mode) */
#define CEC_OP_PLAY_MODE_PLAY_FWD			0x24
#define CEC_OP_PLAY_MODE_PLAY_REV			0x20
#define CEC_OP_PLAY_MODE_PLAY_STILL			0x25
#define CEC_OP_PLAY_MODE_PLAY_FAST_FWD_MIN		0x05
#define CEC_OP_PLAY_MODE_PLAY_FAST_FWD_MED		0x06
#define CEC_OP_PLAY_MODE_PLAY_FAST_FWD_MAX		0x07
#define CEC_OP_PLAY_MODE_PLAY_FAST_REV_MIN		0x09
#define CEC_OP_PLAY_MODE_PLAY_FAST_REV_MED		0x0a
#define CEC_OP_PLAY_MODE_PLAY_FAST_REV_MAX		0x0b
#define CEC_OP_PLAY_MODE_PLAY_SLOW_FWD_MIN		0x15
#define CEC_OP_PLAY_MODE_PLAY_SLOW_FWD_MED		0x16
#define CEC_OP_PLAY_MODE_PLAY_SLOW_FWD_MAX		0x17
#define CEC_OP_PLAY_MODE_PLAY_SLOW_REV_MIN		0x19
#define CEC_OP_PLAY_MODE_PLAY_SLOW_REV_MED		0x1a
#define CEC_OP_PLAY_MODE_PLAY_SLOW_REV_MAX		0x1b


/* Tuner Control Feature */
#define CEC_MSG_GIVE_TUNER_DEVICE_STATUS		0x08
#define CEC_MSG_SELECT_ANALOGUE_SERVICE			0x92
#define CEC_MSG_SELECT_DIGITAL_SERVICE			0x93
#define CEC_MSG_TUNER_DEVICE_STATUS			0x07
/* Recording Flag Operand (rec_flag) */
#define CEC_OP_REC_FLAG_USED				0
#define CEC_OP_REC_FLAG_NOT_USED			1
/* Tuner Display Info Operand (tuner_display_info) */
#define CEC_OP_TUNER_DISPLAY_INFO_DIGITAL		0
#define CEC_OP_TUNER_DISPLAY_INFO_NONE			1
#define CEC_OP_TUNER_DISPLAY_INFO_ANALOGUE		2

#define CEC_MSG_TUNER_STEP_DECREMENT			0x06
#define CEC_MSG_TUNER_STEP_INCREMENT			0x05


/* Vendor Specific Commands Feature */

/*
 * Has also:
 *	CEC_MSG_CEC_VERSION
 *	CEC_MSG_GET_CEC_VERSION
 */
#define CEC_MSG_DEVICE_VENDOR_ID			0x87
#define CEC_MSG_GIVE_DEVICE_VENDOR_ID			0x8c
#define CEC_MSG_VENDOR_COMMAND				0x89
#define CEC_MSG_VENDOR_COMMAND_WITH_ID			0xa0
#define CEC_MSG_VENDOR_REMOTE_BUTTON_DOWN		0x8a
#define CEC_MSG_VENDOR_REMOTE_BUTTON_UP			0x8b


/* OSD Display Feature */
#define CEC_MSG_SET_OSD_STRING				0x64
/* Display Control Operand (disp_ctl) */
#define CEC_OP_DISP_CTL_DEFAULT				0x00
#define CEC_OP_DISP_CTL_UNTIL_CLEARED			0x40
#define CEC_OP_DISP_CTL_CLEAR				0x80


/* Device OSD Transfer Feature */
#define CEC_MSG_GIVE_OSD_NAME				0x46
#define CEC_MSG_SET_OSD_NAME				0x47


/* Device Menu Control Feature */
#define CEC_MSG_MENU_REQUEST				0x8d
/* Menu Request Type Operand (menu_req) */
#define CEC_OP_MENU_REQUEST_ACTIVATE			0x00
#define CEC_OP_MENU_REQUEST_DEACTIVATE			0x01
#define CEC_OP_MENU_REQUEST_QUERY			0x02

#define CEC_MSG_MENU_STATUS				0x8e
/* Menu State Operand (menu_state) */
#define CEC_OP_MENU_STATE_ACTIVATED			0x00
#define CEC_OP_MENU_STATE_DEACTIVATED			0x01

#define CEC_MSG_USER_CONTROL_PRESSED			0x44
/* UI Broadcast Type Operand (ui_bcast_type) */
#define CEC_OP_UI_BCAST_TYPE_TOGGLE_ALL			0x00
#define CEC_OP_UI_BCAST_TYPE_TOGGLE_DIG_ANA		0x01
#define CEC_OP_UI_BCAST_TYPE_ANALOGUE			0x10
#define CEC_OP_UI_BCAST_TYPE_ANALOGUE_T			0x20
#define CEC_OP_UI_BCAST_TYPE_ANALOGUE_CABLE		0x30
#define CEC_OP_UI_BCAST_TYPE_ANALOGUE_SAT		0x40
#define CEC_OP_UI_BCAST_TYPE_DIGITAL			0x50
#define CEC_OP_UI_BCAST_TYPE_DIGITAL_T			0x60
#define CEC_OP_UI_BCAST_TYPE_DIGITAL_CABLE		0x70
#define CEC_OP_UI_BCAST_TYPE_DIGITAL_SAT		0x80
#define CEC_OP_UI_BCAST_TYPE_DIGITAL_COM_SAT		0x90
#define CEC_OP_UI_BCAST_TYPE_DIGITAL_COM_SAT2		0x91
#define CEC_OP_UI_BCAST_TYPE_IP				0xa0
/* UI Sound Presentation Control Operand (ui_snd_pres_ctl) */
#define CEC_OP_UI_SND_PRES_CTL_DUAL_MONO		0x10
#define CEC_OP_UI_SND_PRES_CTL_KARAOKE			0x20
#define CEC_OP_UI_SND_PRES_CTL_DOWNMIX			0x80
#define CEC_OP_UI_SND_PRES_CTL_REVERB			0x90
#define CEC_OP_UI_SND_PRES_CTL_EQUALIZER		0xa0
#define CEC_OP_UI_SND_PRES_CTL_BASS_UP			0xb1
#define CEC_OP_UI_SND_PRES_CTL_BASS_NEUTRAL		0xb2
#define CEC_OP_UI_SND_PRES_CTL_BASS_DOWN		0xb3
#define CEC_OP_UI_SND_PRES_CTL_TREBLE_UP		0xc1
#define CEC_OP_UI_SND_PRES_CTL_TREBLE_NEUTRAL		0xc2
#define CEC_OP_UI_SND_PRES_CTL_TREBLE_DOWN		0xc3

#define CEC_MSG_USER_CONTROL_RELEASED			0x45


/* Remote Control Passthrough Feature */

/*
 * Has also:
 *	CEC_MSG_USER_CONTROL_PRESSED
 *	CEC_MSG_USER_CONTROL_RELEASED
 */


/* Power Status Feature */
#define CEC_MSG_GIVE_DEVICE_POWER_STATUS		0x8f
#define CEC_MSG_REPORT_POWER_STATUS			0x90
/* Power Status Operand (pwr_state) */
#define CEC_OP_POWER_STATUS_ON				0
#define CEC_OP_POWER_STATUS_STANDBY			1
#define CEC_OP_POWER_STATUS_TO_ON			2
#define CEC_OP_POWER_STATUS_TO_STANDBY			3


/* General Protocol Messages */
#define CEC_MSG_FEATURE_ABORT				0x00
/* Abort Reason Operand (reason) */
#define CEC_OP_ABORT_UNRECOGNIZED_OP			0
#define CEC_OP_ABORT_INCORRECT_MODE			1
#define CEC_OP_ABORT_NO_SOURCE				2
#define CEC_OP_ABORT_INVALID_OP				3
#define CEC_OP_ABORT_REFUSED				4
#define CEC_OP_ABORT_UNDETERMINED			5

#define CEC_MSG_ABORT					0xff


/* System Audio Control Feature */

/*
 * Has also:
 *	CEC_MSG_USER_CONTROL_PRESSED
 *	CEC_MSG_USER_CONTROL_RELEASED
 */
#define CEC_MSG_GIVE_AUDIO_STATUS			0x71
#define CEC_MSG_GIVE_SYSTEM_AUDIO_MODE_STATUS		0x7d
#define CEC_MSG_REPORT_AUDIO_STATUS			0x7a
/* Audio Mute Status Operand (aud_mute_status) */
#define CEC_OP_AUD_MUTE_STATUS_OFF			0
#define CEC_OP_AUD_MUTE_STATUS_ON			1

#define CEC_MSG_REPORT_SHORT_AUDIO_DESCRIPTOR		0xa3
#define CEC_MSG_REQUEST_SHORT_AUDIO_DESCRIPTOR		0xa4
#define CEC_MSG_SET_SYSTEM_AUDIO_MODE			0x72
/* System Audio Status Operand (sys_aud_status) */
#define CEC_OP_SYS_AUD_STATUS_OFF			0
#define CEC_OP_SYS_AUD_STATUS_ON			1

#define CEC_MSG_SYSTEM_AUDIO_MODE_REQUEST		0x70
#define CEC_MSG_SYSTEM_AUDIO_MODE_STATUS		0x7e
/* Audio Format ID Operand (audio_format_id) */
#define CEC_OP_AUD_FMT_ID_CEA861			0
#define CEC_OP_AUD_FMT_ID_CEA861_CXT			1


/* Audio Rate Control Feature */
#define CEC_MSG_SET_AUDIO_RATE				0x9a
/* Audio Rate Operand (audio_rate) */
#define CEC_OP_AUD_RATE_OFF				0
#define CEC_OP_AUD_RATE_WIDE_STD			1
#define CEC_OP_AUD_RATE_WIDE_FAST			2
#define CEC_OP_AUD_RATE_WIDE_SLOW			3
#define CEC_OP_AUD_RATE_NARROW_STD			4
#define CEC_OP_AUD_RATE_NARROW_FAST			5
#define CEC_OP_AUD_RATE_NARROW_SLOW			6


/* Audio Return Channel Control Feature */
#define CEC_MSG_INITIATE_ARC				0xc0
#define CEC_MSG_REPORT_ARC_INITIATED			0xc1
#define CEC_MSG_REPORT_ARC_TERMINATED			0xc2
#define CEC_MSG_REQUEST_ARC_INITIATION			0xc3
#define CEC_MSG_REQUEST_ARC_TERMINATION			0xc4
#define CEC_MSG_TERMINATE_ARC				0xc5


/* Dynamic Audio Lipsync Feature */
/* Only for CEC 2.0 and up */
#define CEC_MSG_REQUEST_CURRENT_LATENCY			0xa7
#define CEC_MSG_REPORT_CURRENT_LATENCY			0xa8
/* Low Latency Mode Operand (low_latency_mode) */
#define CEC_OP_LOW_LATENCY_MODE_OFF			0
#define CEC_OP_LOW_LATENCY_MODE_ON			1
/* Audio Output Compensated Operand (audio_out_compensated) */
#define CEC_OP_AUD_OUT_COMPENSATED_NA			0
#define CEC_OP_AUD_OUT_COMPENSATED_DELAY		1
#define CEC_OP_AUD_OUT_COMPENSATED_NO_DELAY		2
#define CEC_OP_AUD_OUT_COMPENSATED_PARTIAL_DELAY	3


/* Capability Discovery and Control Feature */
#define CEC_MSG_CDC_MESSAGE				0xf8
/* Ethernet-over-HDMI: nobody ever does this... */
#define CEC_MSG_CDC_HEC_INQUIRE_STATE			0x00
#define CEC_MSG_CDC_HEC_REPORT_STATE			0x01
/* HEC Functionality State Operand (hec_func_state) */
#define CEC_OP_HEC_FUNC_STATE_NOT_SUPPORTED		0
#define CEC_OP_HEC_FUNC_STATE_INACTIVE			1
#define CEC_OP_HEC_FUNC_STATE_ACTIVE			2
#define CEC_OP_HEC_FUNC_STATE_ACTIVATION_FIELD		3
/* Host Functionality State Operand (host_func_state) */
#define CEC_OP_HOST_FUNC_STATE_NOT_SUPPORTED		0
#define CEC_OP_HOST_FUNC_STATE_INACTIVE			1
#define CEC_OP_HOST_FUNC_STATE_ACTIVE			2
/* ENC Functionality State Operand (enc_func_state) */
#define CEC_OP_ENC_FUNC_STATE_EXT_CON_NOT_SUPPORTED	0
#define CEC_OP_ENC_FUNC_STATE_EXT_CON_INACTIVE		1
#define CEC_OP_ENC_FUNC_STATE_EXT_CON_ACTIVE		2
/* CDC Error Code Operand (cdc_errcode) */
#define CEC_OP_CDC_ERROR_CODE_NONE			0
#define CEC_OP_CDC_ERROR_CODE_CAP_UNSUPPORTED		1
#define CEC_OP_CDC_ERROR_CODE_WRONG_STATE		2
#define CEC_OP_CDC_ERROR_CODE_OTHER			3
/* HEC Support Operand (hec_support) */
#define CEC_OP_HEC_SUPPORT_NO				0
#define CEC_OP_HEC_SUPPORT_YES				1
/* HEC Activation Operand (hec_activation) */
#define CEC_OP_HEC_ACTIVATION_ON			0
#define CEC_OP_HEC_ACTIVATION_OFF			1

#define CEC_MSG_CDC_HEC_SET_STATE_ADJACENT		0x02
#define CEC_MSG_CDC_HEC_SET_STATE			0x03
/* HEC Set State Operand (hec_set_state) */
#define CEC_OP_HEC_SET_STATE_DEACTIVATE			0
#define CEC_OP_HEC_SET_STATE_ACTIVATE			1

#define CEC_MSG_CDC_HEC_REQUEST_DEACTIVATION		0x04
#define CEC_MSG_CDC_HEC_NOTIFY_ALIVE			0x05
#define CEC_MSG_CDC_HEC_DISCOVER			0x06
/* Hotplug Detect messages */
#define CEC_MSG_CDC_HPD_SET_STATE			0x10
/* HPD State Operand (hpd_state) */
#define CEC_OP_HPD_STATE_CP_EDID_DISABLE		0
#define CEC_OP_HPD_STATE_CP_EDID_ENABLE			1
#define CEC_OP_HPD_STATE_CP_EDID_DISABLE_ENABLE		2
#define CEC_OP_HPD_STATE_EDID_DISABLE			3
#define CEC_OP_HPD_STATE_EDID_ENABLE			4
#define CEC_OP_HPD_STATE_EDID_DISABLE_ENABLE		5
#define CEC_MSG_CDC_HPD_REPORT_STATE			0x11
/* HPD Error Code Operand (hpd_error) */
#define CEC_OP_HPD_ERROR_NONE				0
#define CEC_OP_HPD_ERROR_INITIATOR_NOT_CAPABLE		1
#define CEC_OP_HPD_ERROR_INITIATOR_WRONG_STATE		2
#define CEC_OP_HPD_ERROR_OTHER				3
#define CEC_OP_HPD_ERROR_NONE_NO_VIDEO			4

/* End of Messages */

/* Helper functions to identify the 'special' CEC devices */

static inline int cec_is_2nd_tv(const struct cec_log_addrs *las)
{
	/*
	 * It is a second TV if the logical address is 14 or 15 and the
	 * primary device type is a TV.
	 */
	return las->num_log_addrs &&
	       las->log_addr[0] >= CEC_LOG_ADDR_SPECIFIC &&
	       las->primary_device_type[0] == CEC_OP_PRIM_DEVTYPE_TV;
}

static inline int cec_is_processor(const struct cec_log_addrs *las)
{
	/*
	 * It is a processor if the logical address is 12-15 and the
	 * primary device type is a Processor.
	 */
	return las->num_log_addrs &&
	       las->log_addr[0] >= CEC_LOG_ADDR_BACKUP_1 &&
	       las->primary_device_type[0] == CEC_OP_PRIM_DEVTYPE_PROCESSOR;
}

static inline int cec_is_switch(const struct cec_log_addrs *las)
{
	/*
	 * It is a switch if the logical address is 15 and the
	 * primary device type is a Switch and the CDC-Only flag is not set.
	 */
	return las->num_log_addrs == 1 &&
	       las->log_addr[0] == CEC_LOG_ADDR_UNREGISTERED &&
	       las->primary_device_type[0] == CEC_OP_PRIM_DEVTYPE_SWITCH &&
	       !(las->flags & CEC_LOG_ADDRS_FL_CDC_ONLY);
}

static inline int cec_is_cdc_only(const struct cec_log_addrs *las)
{
	/*
	 * It is a CDC-only device if the logical address is 15 and the
	 * primary device type is a Switch and the CDC-Only flag is set.
	 */
	return las->num_log_addrs == 1 &&
	       las->log_addr[0] == CEC_LOG_ADDR_UNREGISTERED &&
	       las->primary_device_type[0] == CEC_OP_PRIM_DEVTYPE_SWITCH &&
	       (las->flags & CEC_LOG_ADDRS_FL_CDC_ONLY);
}

#endif
