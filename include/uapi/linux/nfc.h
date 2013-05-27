/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __LINUX_NFC_H
#define __LINUX_NFC_H

#include <linux/types.h>
#include <linux/socket.h>

#define NFC_GENL_NAME "nfc"
#define NFC_GENL_VERSION 1

#define NFC_GENL_MCAST_EVENT_NAME "events"

/**
 * enum nfc_commands - supported nfc commands
 *
 * @NFC_CMD_UNSPEC: unspecified command
 *
 * @NFC_CMD_GET_DEVICE: request information about a device (requires
 *	%NFC_ATTR_DEVICE_INDEX) or dump request to get a list of all nfc devices
 * @NFC_CMD_DEV_UP: turn on the nfc device
 *	(requires %NFC_ATTR_DEVICE_INDEX)
 * @NFC_CMD_DEV_DOWN: turn off the nfc device
 *	(requires %NFC_ATTR_DEVICE_INDEX)
 * @NFC_CMD_START_POLL: start polling for targets using the given protocols
 *	(requires %NFC_ATTR_DEVICE_INDEX and %NFC_ATTR_PROTOCOLS)
 * @NFC_CMD_STOP_POLL: stop polling for targets (requires
 *	%NFC_ATTR_DEVICE_INDEX)
 * @NFC_CMD_GET_TARGET: dump all targets found by the previous poll (requires
 *	%NFC_ATTR_DEVICE_INDEX)
 * @NFC_EVENT_TARGETS_FOUND: event emitted when a new target is found
 *	(it sends %NFC_ATTR_DEVICE_INDEX)
 * @NFC_EVENT_DEVICE_ADDED: event emitted when a new device is registred
 *	(it sends %NFC_ATTR_DEVICE_NAME, %NFC_ATTR_DEVICE_INDEX and
 *	%NFC_ATTR_PROTOCOLS)
 * @NFC_EVENT_DEVICE_REMOVED: event emitted when a device is removed
 *	(it sends %NFC_ATTR_DEVICE_INDEX)
 * @NFC_EVENT_TM_ACTIVATED: event emitted when the adapter is activated in
 *      target mode.
 * @NFC_EVENT_DEVICE_DEACTIVATED: event emitted when the adapter is deactivated
 *      from target mode.
 * @NFC_CMD_LLC_GET_PARAMS: request LTO, RW, and MIUX parameters for a device
 * @NFC_CMD_LLC_SET_PARAMS: set one or more of LTO, RW, and MIUX parameters for
 *	a device. LTO must be set before the link is up otherwise -EINPROGRESS
 *	is returned. RW and MIUX can be set at anytime and will be passed in
 *	subsequent CONNECT and CC messages.
 *	If one of the passed parameters is wrong none is set and -EINVAL is
 *	returned.
 * @NFC_CMD_ENABLE_SE: Enable the physical link to a specific secure element.
 *	Once enabled a secure element will handle card emulation mode, i.e.
 *	starting a poll from a device which has a secure element enabled means
 *	we want to do SE based card emulation.
 * @NFC_CMD_DISABLE_SE: Disable the physical link to a specific secure element.
 * @NFC_CMD_FW_UPLOAD: Request to Load/flash firmware, or event to inform that
 *	some firmware was loaded
 */
enum nfc_commands {
	NFC_CMD_UNSPEC,
	NFC_CMD_GET_DEVICE,
	NFC_CMD_DEV_UP,
	NFC_CMD_DEV_DOWN,
	NFC_CMD_DEP_LINK_UP,
	NFC_CMD_DEP_LINK_DOWN,
	NFC_CMD_START_POLL,
	NFC_CMD_STOP_POLL,
	NFC_CMD_GET_TARGET,
	NFC_EVENT_TARGETS_FOUND,
	NFC_EVENT_DEVICE_ADDED,
	NFC_EVENT_DEVICE_REMOVED,
	NFC_EVENT_TARGET_LOST,
	NFC_EVENT_TM_ACTIVATED,
	NFC_EVENT_TM_DEACTIVATED,
	NFC_CMD_LLC_GET_PARAMS,
	NFC_CMD_LLC_SET_PARAMS,
	NFC_CMD_ENABLE_SE,
	NFC_CMD_DISABLE_SE,
	NFC_CMD_LLC_SDREQ,
	NFC_EVENT_LLC_SDRES,
	NFC_CMD_FW_UPLOAD,
/* private: internal use only */
	__NFC_CMD_AFTER_LAST
};
#define NFC_CMD_MAX (__NFC_CMD_AFTER_LAST - 1)

/**
 * enum nfc_attrs - supported nfc attributes
 *
 * @NFC_ATTR_UNSPEC: unspecified attribute
 *
 * @NFC_ATTR_DEVICE_INDEX: index of nfc device
 * @NFC_ATTR_DEVICE_NAME: device name, max 8 chars
 * @NFC_ATTR_PROTOCOLS: nfc protocols - bitwise or-ed combination from
 *	NFC_PROTO_*_MASK constants
 * @NFC_ATTR_TARGET_INDEX: index of the nfc target
 * @NFC_ATTR_TARGET_SENS_RES: NFC-A targets extra information such as NFCID
 * @NFC_ATTR_TARGET_SEL_RES: NFC-A targets extra information (useful if the
 *	target is not NFC-Forum compliant)
 * @NFC_ATTR_TARGET_NFCID1: NFC-A targets identifier, max 10 bytes
 * @NFC_ATTR_TARGET_SENSB_RES: NFC-B targets extra information, max 12 bytes
 * @NFC_ATTR_TARGET_SENSF_RES: NFC-F targets extra information, max 18 bytes
 * @NFC_ATTR_COMM_MODE: Passive or active mode
 * @NFC_ATTR_RF_MODE: Initiator or target
 * @NFC_ATTR_IM_PROTOCOLS: Initiator mode protocols to poll for
 * @NFC_ATTR_TM_PROTOCOLS: Target mode protocols to listen for
 * @NFC_ATTR_LLC_PARAM_LTO: Link TimeOut parameter
 * @NFC_ATTR_LLC_PARAM_RW: Receive Window size parameter
 * @NFC_ATTR_LLC_PARAM_MIUX: MIU eXtension parameter
 * @NFC_ATTR_SE: Available Secure Elements
 * @NFC_ATTR_FIRMWARE_NAME: Free format firmware version
 */
enum nfc_attrs {
	NFC_ATTR_UNSPEC,
	NFC_ATTR_DEVICE_INDEX,
	NFC_ATTR_DEVICE_NAME,
	NFC_ATTR_PROTOCOLS,
	NFC_ATTR_TARGET_INDEX,
	NFC_ATTR_TARGET_SENS_RES,
	NFC_ATTR_TARGET_SEL_RES,
	NFC_ATTR_TARGET_NFCID1,
	NFC_ATTR_TARGET_SENSB_RES,
	NFC_ATTR_TARGET_SENSF_RES,
	NFC_ATTR_COMM_MODE,
	NFC_ATTR_RF_MODE,
	NFC_ATTR_DEVICE_POWERED,
	NFC_ATTR_IM_PROTOCOLS,
	NFC_ATTR_TM_PROTOCOLS,
	NFC_ATTR_LLC_PARAM_LTO,
	NFC_ATTR_LLC_PARAM_RW,
	NFC_ATTR_LLC_PARAM_MIUX,
	NFC_ATTR_SE,
	NFC_ATTR_LLC_SDP,
	NFC_ATTR_FIRMWARE_NAME,
/* private: internal use only */
	__NFC_ATTR_AFTER_LAST
};
#define NFC_ATTR_MAX (__NFC_ATTR_AFTER_LAST - 1)

enum nfc_sdp_attr {
	NFC_SDP_ATTR_UNSPEC,
	NFC_SDP_ATTR_URI,
	NFC_SDP_ATTR_SAP,
/* private: internal use only */
	__NFC_SDP_ATTR_AFTER_LAST
};
#define NFC_SDP_ATTR_MAX (__NFC_SDP_ATTR_AFTER_LAST - 1)

#define NFC_DEVICE_NAME_MAXSIZE 8
#define NFC_NFCID1_MAXSIZE 10
#define NFC_NFCID2_MAXSIZE 8
#define NFC_NFCID3_MAXSIZE 10
#define NFC_SENSB_RES_MAXSIZE 12
#define NFC_SENSF_RES_MAXSIZE 18
#define NFC_GB_MAXSIZE        48
#define NFC_FIRMWARE_NAME_MAXSIZE 32

/* NFC protocols */
#define NFC_PROTO_JEWEL		1
#define NFC_PROTO_MIFARE	2
#define NFC_PROTO_FELICA	3
#define NFC_PROTO_ISO14443	4
#define NFC_PROTO_NFC_DEP	5
#define NFC_PROTO_ISO14443_B	6

#define NFC_PROTO_MAX		7

/* NFC communication modes */
#define NFC_COMM_ACTIVE  0
#define NFC_COMM_PASSIVE 1

/* NFC RF modes */
#define NFC_RF_INITIATOR 0
#define NFC_RF_TARGET    1
#define NFC_RF_NONE      2

/* NFC protocols masks used in bitsets */
#define NFC_PROTO_JEWEL_MASK      (1 << NFC_PROTO_JEWEL)
#define NFC_PROTO_MIFARE_MASK     (1 << NFC_PROTO_MIFARE)
#define NFC_PROTO_FELICA_MASK	  (1 << NFC_PROTO_FELICA)
#define NFC_PROTO_ISO14443_MASK	  (1 << NFC_PROTO_ISO14443)
#define NFC_PROTO_NFC_DEP_MASK	  (1 << NFC_PROTO_NFC_DEP)
#define NFC_PROTO_ISO14443_B_MASK (1 << NFC_PROTO_ISO14443_B)

/* NFC Secure Elements */
#define NFC_SE_NONE     0x0
#define NFC_SE_UICC     0x1
#define NFC_SE_EMBEDDED 0x2

struct sockaddr_nfc {
	sa_family_t sa_family;
	__u32 dev_idx;
	__u32 target_idx;
	__u32 nfc_protocol;
};

#define NFC_LLCP_MAX_SERVICE_NAME 63
struct sockaddr_nfc_llcp {
	sa_family_t sa_family;
	__u32 dev_idx;
	__u32 target_idx;
	__u32 nfc_protocol;
	__u8 dsap; /* Destination SAP, if known */
	__u8 ssap; /* Source SAP to be bound to */
	char service_name[NFC_LLCP_MAX_SERVICE_NAME]; /* Service name URI */;
	size_t service_name_len;
};

/* NFC socket protocols */
#define NFC_SOCKPROTO_RAW	0
#define NFC_SOCKPROTO_LLCP	1
#define NFC_SOCKPROTO_MAX	2

#define NFC_HEADER_SIZE 1

/**
 * Pseudo-header info for raw socket packets
 * First byte is the adapter index
 * Second byte contains flags
 *  - 0x01 - Direction (0=RX, 1=TX)
 *  - 0x02-0x80 - Reserved
 **/
#define NFC_LLCP_RAW_HEADER_SIZE	2
#define NFC_LLCP_DIRECTION_RX		0x00
#define NFC_LLCP_DIRECTION_TX		0x01

/* socket option names */
#define NFC_LLCP_RW		0
#define NFC_LLCP_MIUX		1
#define NFC_LLCP_REMOTE_MIU	2
#define NFC_LLCP_REMOTE_LTO	3
#define NFC_LLCP_REMOTE_RW	4

#endif /*__LINUX_NFC_H */
