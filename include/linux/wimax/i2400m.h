/*
 * Intel Wireless WiMax Connection 2400m
 * Host-Device protocol interface definitions
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *  - Initial implementation
 *
 *
 * This header defines the data structures and constants used to
 * communicate with the device.
 *
 * BOOTMODE/BOOTROM/FIRMWARE UPLOAD PROTOCOL
 *
 * The firmware upload protocol is quite simple and only requires a
 * handful of commands. See drivers/net/wimax/i2400m/fw.c for more
 * details.
 *
 * The BCF data structure is for the firmware file header.
 *
 *
 * THE DATA / CONTROL PROTOCOL
 *
 * This is the normal protocol spoken with the device once the
 * firmware is uploaded. It transports data payloads and control
 * messages back and forth.
 *
 * It consists 'messages' that pack one or more payloads each. The
 * format is described in detail in drivers/net/wimax/i2400m/rx.c and
 * tx.c.
 *
 *
 * THE L3L4 PROTOCOL
 *
 * The term L3L4 refers to Layer 3 (the device), Layer 4 (the
 * driver/host software).
 *
 * This is the control protocol used by the host to control the i2400m
 * device (scan, connect, disconnect...). This is sent to / received
 * as control frames. These frames consist of a header and zero or
 * more TLVs with information. We call each control frame a "message".
 *
 * Each message is composed of:
 *
 * HEADER
 * [TLV0 + PAYLOAD0]
 * [TLV1 + PAYLOAD1]
 * [...]
 * [TLVN + PAYLOADN]
 *
 * The HEADER is defined by 'struct i2400m_l3l4_hdr'. The payloads are
 * defined by a TLV structure (Type Length Value) which is a 'header'
 * (struct i2400m_tlv_hdr) and then the payload.
 *
 * All integers are represented as Little Endian.
 *
 * - REQUESTS AND EVENTS
 *
 * The requests can be clasified as follows:
 *
 *   COMMAND:  implies a request from the host to the device requesting
 *             an action being performed. The device will reply with a
 *             message (with the same type as the command), status and
 *             no (TLV) payload. Execution of a command might cause
 *             events (of different type) to be sent later on as
 *             device's state changes.
 *
 *   GET/SET:  similar to COMMAND, but will not cause other
 *             EVENTs. The reply, in the case of GET, will contain
 *             TLVs with the requested information.
 *
 *   EVENT:    asynchronous messages sent from the device, maybe as a
 *             consequence of previous COMMANDs but disassociated from
 *             them.
 *
 * Only one request might be pending at the same time (ie: don't
 * parallelize nor post another GET request before the previous
 * COMMAND has been acknowledged with it's corresponding reply by the
 * device).
 *
 * The different requests and their formats are described below:
 *
 *  I2400M_MT_*   Message types
 *  I2400M_MS_*   Message status (for replies, events)
 *  i2400m_tlv_*  TLVs
 *
 * data types are named 'struct i2400m_msg_OPNAME', OPNAME matching the
 * operation.
 */

#ifndef __LINUX__WIMAX__I2400M_H__
#define __LINUX__WIMAX__I2400M_H__

#include <linux/types.h>


/*
 * Host Device Interface (HDI) common to all busses
 */

/* Boot-mode (firmware upload mode) commands */

/* Header for the firmware file */
struct i2400m_bcf_hdr {
	__le32 module_type;
	__le32 header_len;
	__le32 header_version;
	__le32 module_id;
	__le32 module_vendor;
	__le32 date;		/* BCD YYYMMDD */
	__le32 size;            /* in dwords */
	__le32 key_size;	/* in dwords */
	__le32 modulus_size;	/* in dwords */
	__le32 exponent_size;	/* in dwords */
	__u8 reserved[88];
} __attribute__ ((packed));

/* Boot mode opcodes */
enum i2400m_brh_opcode {
	I2400M_BRH_READ = 1,
	I2400M_BRH_WRITE = 2,
	I2400M_BRH_JUMP = 3,
	I2400M_BRH_SIGNED_JUMP = 8,
	I2400M_BRH_HASH_PAYLOAD_ONLY = 9,
};

/* Boot mode command masks and stuff */
enum i2400m_brh {
	I2400M_BRH_SIGNATURE = 0xcbbc0000,
	I2400M_BRH_SIGNATURE_MASK = 0xffff0000,
	I2400M_BRH_SIGNATURE_SHIFT = 16,
	I2400M_BRH_OPCODE_MASK = 0x0000000f,
	I2400M_BRH_RESPONSE_MASK = 0x000000f0,
	I2400M_BRH_RESPONSE_SHIFT = 4,
	I2400M_BRH_DIRECT_ACCESS = 0x00000400,
	I2400M_BRH_RESPONSE_REQUIRED = 0x00000200,
	I2400M_BRH_USE_CHECKSUM = 0x00000100,
};


/**
 * i2400m_bootrom_header - Header for a boot-mode command
 *
 * @cmd: the above command descriptor
 * @target_addr: where on the device memory should the action be performed.
 * @data_size: for read/write, amount of data to be read/written
 * @block_checksum: checksum value (if applicable)
 * @payload: the beginning of data attached to this header
 */
struct i2400m_bootrom_header {
	__le32 command;		/* Compose with enum i2400_brh */
	__le32 target_addr;
	__le32 data_size;
	__le32 block_checksum;
	char payload[0];
} __attribute__ ((packed));


/*
 * Data / control protocol
 */

/* Packet types for the host-device interface */
enum i2400m_pt {
	I2400M_PT_DATA = 0,
	I2400M_PT_CTRL,
	I2400M_PT_TRACE,	/* For device debug */
	I2400M_PT_RESET_WARM,	/* device reset */
	I2400M_PT_RESET_COLD,	/* USB[transport] reset, like reconnect */
	I2400M_PT_EDATA,	/* Extended RX data */
	I2400M_PT_ILLEGAL
};


/*
 * Payload for a data packet
 *
 * This is prefixed to each and every outgoing DATA type.
 */
struct i2400m_pl_data_hdr {
	__le32 reserved;
} __attribute__((packed));


/*
 * Payload for an extended data packet
 *
 * New in fw v1.4
 *
 * @reorder: if this payload has to be reorder or not (and how)
 * @cs: the type of data in the packet, as defined per (802.16e
 *     T11.13.19.1). Currently only 2 (IPv4 packet) supported.
 *
 * This is prefixed to each and every INCOMING DATA packet.
 */
struct i2400m_pl_edata_hdr {
	__le32 reorder;		/* bits defined in i2400m_ro */
	__u8 cs;
	__u8 reserved[11];
} __attribute__((packed));

enum i2400m_cs {
	I2400M_CS_IPV4_0 = 0,
	I2400M_CS_IPV4 = 2,
};

enum i2400m_ro {
	I2400M_RO_NEEDED     = 0x01,
	I2400M_RO_TYPE       = 0x03,
	I2400M_RO_TYPE_SHIFT = 1,
	I2400M_RO_CIN        = 0x0f,
	I2400M_RO_CIN_SHIFT  = 4,
	I2400M_RO_FBN        = 0x07ff,
	I2400M_RO_FBN_SHIFT  = 8,
	I2400M_RO_SN         = 0x07ff,
	I2400M_RO_SN_SHIFT   = 21,
};

enum i2400m_ro_type {
	I2400M_RO_TYPE_RESET = 0,
	I2400M_RO_TYPE_PACKET,
	I2400M_RO_TYPE_WS,
	I2400M_RO_TYPE_PACKET_WS,
};


/* Misc constants */
enum {
	I2400M_PL_ALIGN = 16,	/* Payload data size alignment */
	I2400M_PL_SIZE_MAX = 0x3EFF,
	I2400M_MAX_PLS_IN_MSG = 60,
	/* protocol barkers: sync sequences; for notifications they
	 * are sent in groups of four. */
	I2400M_H2D_PREVIEW_BARKER = 0xcafe900d,
	I2400M_COLD_RESET_BARKER = 0xc01dc01d,
	I2400M_WARM_RESET_BARKER = 0x50f750f7,
	I2400M_NBOOT_BARKER = 0xdeadbeef,
	I2400M_SBOOT_BARKER = 0x0ff1c1a1,
	I2400M_SBOOT_BARKER_6050 = 0x80000001,
	I2400M_ACK_BARKER = 0xfeedbabe,
	I2400M_D2H_MSG_BARKER = 0xbeefbabe,
};


/*
 * Hardware payload descriptor
 *
 * Bitfields encoded in a struct to enforce typing semantics.
 *
 * Look in rx.c and tx.c for a full description of the format.
 */
struct i2400m_pld {
	__le32 val;
} __attribute__ ((packed));

#define I2400M_PLD_SIZE_MASK 0x00003fff
#define I2400M_PLD_TYPE_SHIFT 16
#define I2400M_PLD_TYPE_MASK 0x000f0000

/*
 * Header for a TX message or RX message
 *
 * @barker: preamble
 * @size: used for management of the FIFO queue buffer; before
 *     sending, this is converted to be a real preamble. This
 *     indicates the real size of the TX message that starts at this
 *     point. If the highest bit is set, then this message is to be
 *     skipped.
 * @sequence: sequence number of this message
 * @offset: offset where the message itself starts -- see the comments
 *     in the file header about message header and payload descriptor
 *     alignment.
 * @num_pls: number of payloads in this message
 * @padding: amount of padding bytes at the end of the message to make
 *           it be of block-size aligned
 *
 * Look in rx.c and tx.c for a full description of the format.
 */
struct i2400m_msg_hdr {
	union {
		__le32 barker;
		__u32 size;	/* same size type as barker!! */
	};
	union {
		__le32 sequence;
		__u32 offset;	/* same size type as barker!! */
	};
	__le16 num_pls;
	__le16 rsv1;
	__le16 padding;
	__le16 rsv2;
	struct i2400m_pld pld[0];
} __attribute__ ((packed));



/*
 * L3/L4 control protocol
 */

enum {
	/* Interface version */
	I2400M_L3L4_VERSION             = 0x0100,
};

/* Message types */
enum i2400m_mt {
	I2400M_MT_RESERVED              = 0x0000,
	I2400M_MT_INVALID               = 0xffff,
	I2400M_MT_REPORT_MASK		= 0x8000,

	I2400M_MT_GET_SCAN_RESULT  	= 0x4202,
	I2400M_MT_SET_SCAN_PARAM   	= 0x4402,
	I2400M_MT_CMD_RF_CONTROL   	= 0x4602,
	I2400M_MT_CMD_SCAN         	= 0x4603,
	I2400M_MT_CMD_CONNECT      	= 0x4604,
	I2400M_MT_CMD_DISCONNECT   	= 0x4605,
	I2400M_MT_CMD_EXIT_IDLE   	= 0x4606,
	I2400M_MT_GET_LM_VERSION   	= 0x5201,
	I2400M_MT_GET_DEVICE_INFO  	= 0x5202,
	I2400M_MT_GET_LINK_STATUS  	= 0x5203,
	I2400M_MT_GET_STATISTICS   	= 0x5204,
	I2400M_MT_GET_STATE        	= 0x5205,
	I2400M_MT_GET_MEDIA_STATUS	= 0x5206,
	I2400M_MT_SET_INIT_CONFIG	= 0x5404,
	I2400M_MT_CMD_INIT	        = 0x5601,
	I2400M_MT_CMD_TERMINATE		= 0x5602,
	I2400M_MT_CMD_MODE_OF_OP	= 0x5603,
	I2400M_MT_CMD_RESET_DEVICE	= 0x5604,
	I2400M_MT_CMD_MONITOR_CONTROL   = 0x5605,
	I2400M_MT_CMD_ENTER_POWERSAVE   = 0x5606,
	I2400M_MT_GET_TLS_OPERATION_RESULT = 0x6201,
	I2400M_MT_SET_EAP_SUCCESS       = 0x6402,
	I2400M_MT_SET_EAP_FAIL          = 0x6403,
	I2400M_MT_SET_EAP_KEY          	= 0x6404,
	I2400M_MT_CMD_SEND_EAP_RESPONSE = 0x6602,
	I2400M_MT_REPORT_SCAN_RESULT    = 0xc002,
	I2400M_MT_REPORT_STATE		= 0xd002,
	I2400M_MT_REPORT_POWERSAVE_READY = 0xd005,
	I2400M_MT_REPORT_EAP_REQUEST    = 0xe002,
	I2400M_MT_REPORT_EAP_RESTART    = 0xe003,
	I2400M_MT_REPORT_ALT_ACCEPT    	= 0xe004,
	I2400M_MT_REPORT_KEY_REQUEST 	= 0xe005,
};


/*
 * Message Ack Status codes
 *
 * When a message is replied-to, this status is reported.
 */
enum i2400m_ms {
	I2400M_MS_DONE_OK                  = 0,
	I2400M_MS_DONE_IN_PROGRESS         = 1,
	I2400M_MS_INVALID_OP               = 2,
	I2400M_MS_BAD_STATE                = 3,
	I2400M_MS_ILLEGAL_VALUE            = 4,
	I2400M_MS_MISSING_PARAMS           = 5,
	I2400M_MS_VERSION_ERROR            = 6,
	I2400M_MS_ACCESSIBILITY_ERROR      = 7,
	I2400M_MS_BUSY                     = 8,
	I2400M_MS_CORRUPTED_TLV            = 9,
	I2400M_MS_UNINITIALIZED            = 10,
	I2400M_MS_UNKNOWN_ERROR            = 11,
	I2400M_MS_PRODUCTION_ERROR         = 12,
	I2400M_MS_NO_RF                    = 13,
	I2400M_MS_NOT_READY_FOR_POWERSAVE  = 14,
	I2400M_MS_THERMAL_CRITICAL         = 15,
	I2400M_MS_MAX
};


/**
 * i2400m_tlv - enumeration of the different types of TLVs
 *
 * TLVs stand for type-length-value and are the header for a payload
 * composed of almost anything. Each payload has a type assigned
 * and a length.
 */
enum i2400m_tlv {
	I2400M_TLV_L4_MESSAGE_VERSIONS = 129,
	I2400M_TLV_SYSTEM_STATE = 141,
	I2400M_TLV_MEDIA_STATUS = 161,
	I2400M_TLV_RF_OPERATION = 162,
	I2400M_TLV_RF_STATUS = 163,
	I2400M_TLV_DEVICE_RESET_TYPE = 132,
	I2400M_TLV_CONFIG_IDLE_PARAMETERS = 601,
	I2400M_TLV_CONFIG_IDLE_TIMEOUT = 611,
	I2400M_TLV_CONFIG_D2H_DATA_FORMAT = 614,
	I2400M_TLV_CONFIG_DL_HOST_REORDER = 615,
};


struct i2400m_tlv_hdr {
	__le16 type;
	__le16 length;		/* payload's */
	__u8   pl[0];
} __attribute__((packed));


struct i2400m_l3l4_hdr {
	__le16 type;
	__le16 length;		/* payload's */
	__le16 version;
	__le16 resv1;
	__le16 status;
	__le16 resv2;
	struct i2400m_tlv_hdr pl[0];
} __attribute__((packed));


/**
 * i2400m_system_state - different states of the device
 */
enum i2400m_system_state {
	I2400M_SS_UNINITIALIZED = 1,
	I2400M_SS_INIT,
	I2400M_SS_READY,
	I2400M_SS_SCAN,
	I2400M_SS_STANDBY,
	I2400M_SS_CONNECTING,
	I2400M_SS_WIMAX_CONNECTED,
	I2400M_SS_DATA_PATH_CONNECTED,
	I2400M_SS_IDLE,
	I2400M_SS_DISCONNECTING,
	I2400M_SS_OUT_OF_ZONE,
	I2400M_SS_SLEEPACTIVE,
	I2400M_SS_PRODUCTION,
	I2400M_SS_CONFIG,
	I2400M_SS_RF_OFF,
	I2400M_SS_RF_SHUTDOWN,
	I2400M_SS_DEVICE_DISCONNECT,
	I2400M_SS_MAX,
};


/**
 * i2400m_tlv_system_state - report on the state of the system
 *
 * @state: see enum i2400m_system_state
 */
struct i2400m_tlv_system_state {
	struct i2400m_tlv_hdr hdr;
	__le32 state;
} __attribute__((packed));


struct i2400m_tlv_l4_message_versions {
	struct i2400m_tlv_hdr hdr;
	__le16 major;
	__le16 minor;
	__le16 branch;
	__le16 reserved;
} __attribute__((packed));


struct i2400m_tlv_detailed_device_info {
	struct i2400m_tlv_hdr hdr;
	__u8 reserved1[400];
	__u8 mac_address[6];
	__u8 reserved2[2];
} __attribute__((packed));


enum i2400m_rf_switch_status {
	I2400M_RF_SWITCH_ON = 1,
	I2400M_RF_SWITCH_OFF = 2,
};

struct i2400m_tlv_rf_switches_status {
	struct i2400m_tlv_hdr hdr;
	__u8 sw_rf_switch;	/* 1 ON, 2 OFF */
	__u8 hw_rf_switch;	/* 1 ON, 2 OFF */
	__u8 reserved[2];
} __attribute__((packed));


enum {
	i2400m_rf_operation_on = 1,
	i2400m_rf_operation_off = 2
};

struct i2400m_tlv_rf_operation {
	struct i2400m_tlv_hdr hdr;
	__le32 status;	/* 1 ON, 2 OFF */
} __attribute__((packed));


enum i2400m_tlv_reset_type {
	I2400M_RESET_TYPE_COLD = 1,
	I2400M_RESET_TYPE_WARM
};

struct i2400m_tlv_device_reset_type {
	struct i2400m_tlv_hdr hdr;
	__le32 reset_type;
} __attribute__((packed));


struct i2400m_tlv_config_idle_parameters {
	struct i2400m_tlv_hdr hdr;
	__le32 idle_timeout;	/* 100 to 300000 ms [5min], 100 increments
				 * 0 disabled */
	__le32 idle_paging_interval;	/* frames */
} __attribute__((packed));


enum i2400m_media_status {
	I2400M_MEDIA_STATUS_LINK_UP = 1,
	I2400M_MEDIA_STATUS_LINK_DOWN,
	I2400M_MEDIA_STATUS_LINK_RENEW,
};

struct i2400m_tlv_media_status {
	struct i2400m_tlv_hdr hdr;
	__le32 media_status;
} __attribute__((packed));


/* New in v1.4 */
struct i2400m_tlv_config_idle_timeout {
	struct i2400m_tlv_hdr hdr;
	__le32 timeout;	/* 100 to 300000 ms [5min], 100 increments
			 * 0 disabled */
} __attribute__((packed));

/* New in v1.4 -- for backward compat, will be removed */
struct i2400m_tlv_config_d2h_data_format {
	struct i2400m_tlv_hdr hdr;
	__u8 format; 		/* 0 old format, 1 enhanced */
	__u8 reserved[3];
} __attribute__((packed));

/* New in v1.4 */
struct i2400m_tlv_config_dl_host_reorder {
	struct i2400m_tlv_hdr hdr;
	__u8 reorder; 		/* 0 disabled, 1 enabled */
	__u8 reserved[3];
} __attribute__((packed));


#endif /* #ifndef __LINUX__WIMAX__I2400M_H__ */
