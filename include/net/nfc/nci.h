/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2014 Marvell International Ltd.
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci.h, which was written
 *  by Maxim Krasnyansky.
 */

#ifndef __NCI_H
#define __NCI_H

#include <net/nfc/nfc.h>

/* NCI constants */
#define NCI_MAX_NUM_MAPPING_CONFIGS				10
#define NCI_MAX_NUM_RF_CONFIGS					10
#define NCI_MAX_NUM_CONN					10
#define NCI_MAX_PARAM_LEN					251
#define NCI_MAX_PAYLOAD_SIZE					255
#define NCI_MAX_PACKET_SIZE					258
#define NCI_MAX_LARGE_PARAMS_NCI_v2				15
#define NCI_VER_2_MASK						0x20

/* NCI Status Codes */
#define NCI_STATUS_OK						0x00
#define NCI_STATUS_REJECTED					0x01
#define NCI_STATUS_RF_FRAME_CORRUPTED				0x02
#define NCI_STATUS_FAILED					0x03
#define NCI_STATUS_NOT_INITIALIZED				0x04
#define NCI_STATUS_SYNTAX_ERROR					0x05
#define NCI_STATUS_SEMANTIC_ERROR				0x06
#define NCI_STATUS_UNKNOWN_GID					0x07
#define NCI_STATUS_UNKNOWN_OID					0x08
#define NCI_STATUS_INVALID_PARAM				0x09
#define NCI_STATUS_MESSAGE_SIZE_EXCEEDED			0x0a
/* Discovery Specific Status Codes */
#define NCI_STATUS_DISCOVERY_ALREADY_STARTED			0xa0
#define NCI_STATUS_DISCOVERY_TARGET_ACTIVATION_FAILED		0xa1
#define NCI_STATUS_DISCOVERY_TEAR_DOWN				0xa2
/* RF Interface Specific Status Codes */
#define NCI_STATUS_RF_TRANSMISSION_ERROR			0xb0
#define NCI_STATUS_RF_PROTOCOL_ERROR				0xb1
#define NCI_STATUS_RF_TIMEOUT_ERROR				0xb2
/* NFCEE Interface Specific Status Codes */
#define NCI_STATUS_NFCEE_INTERFACE_ACTIVATION_FAILED		0xc0
#define NCI_STATUS_NFCEE_TRANSMISSION_ERROR			0xc1
#define NCI_STATUS_NFCEE_PROTOCOL_ERROR				0xc2
#define NCI_STATUS_NFCEE_TIMEOUT_ERROR				0xc3

/* NFCEE Interface/Protocols */
#define NCI_NFCEE_INTERFACE_APDU           0x00
#define NCI_NFCEE_INTERFACE_HCI_ACCESS     0x01
#define NCI_NFCEE_INTERFACE_TYPE3_CMD_SET  0x02
#define NCI_NFCEE_INTERFACE_TRANSPARENT        0x03

/* Destination type */
#define NCI_DESTINATION_NFCC_LOOPBACK      0x01
#define NCI_DESTINATION_REMOTE_NFC_ENDPOINT    0x02
#define NCI_DESTINATION_NFCEE              0x03

/* Destination-specific parameters type */
#define NCI_DESTINATION_SPECIFIC_PARAM_RF_TYPE     0x00
#define NCI_DESTINATION_SPECIFIC_PARAM_NFCEE_TYPE  0x01

/* NFCEE Discovery Action */
#define NCI_NFCEE_DISCOVERY_ACTION_DISABLE			0x00
#define NCI_NFCEE_DISCOVERY_ACTION_ENABLE			0x01

/* NCI RF Technology and Mode */
#define NCI_NFC_A_PASSIVE_POLL_MODE				0x00
#define NCI_NFC_B_PASSIVE_POLL_MODE				0x01
#define NCI_NFC_F_PASSIVE_POLL_MODE				0x02
#define NCI_NFC_A_ACTIVE_POLL_MODE				0x03
#define NCI_NFC_F_ACTIVE_POLL_MODE				0x05
#define NCI_NFC_V_PASSIVE_POLL_MODE				0x06
#define NCI_NFC_A_PASSIVE_LISTEN_MODE				0x80
#define NCI_NFC_B_PASSIVE_LISTEN_MODE				0x81
#define NCI_NFC_F_PASSIVE_LISTEN_MODE				0x82
#define NCI_NFC_A_ACTIVE_LISTEN_MODE				0x83
#define NCI_NFC_F_ACTIVE_LISTEN_MODE				0x85

#define NCI_RF_TECH_MODE_LISTEN_MASK				0x80

/* NCI RF Technologies */
#define NCI_NFC_RF_TECHNOLOGY_A					0x00
#define NCI_NFC_RF_TECHNOLOGY_B					0x01
#define NCI_NFC_RF_TECHNOLOGY_F					0x02
#define NCI_NFC_RF_TECHNOLOGY_V					0x03

/* NCI Bit Rates */
#define NCI_NFC_BIT_RATE_106					0x00
#define NCI_NFC_BIT_RATE_212					0x01
#define NCI_NFC_BIT_RATE_424					0x02
#define NCI_NFC_BIT_RATE_848					0x03
#define NCI_NFC_BIT_RATE_1695					0x04
#define NCI_NFC_BIT_RATE_3390					0x05
#define NCI_NFC_BIT_RATE_6780					0x06
#define NCI_NFC_BIT_RATE_26					0x20

/* NCI RF Protocols */
#define NCI_RF_PROTOCOL_UNKNOWN					0x00
#define NCI_RF_PROTOCOL_T1T					0x01
#define NCI_RF_PROTOCOL_T2T					0x02
#define NCI_RF_PROTOCOL_T3T					0x03
#define NCI_RF_PROTOCOL_ISO_DEP					0x04
#define NCI_RF_PROTOCOL_NFC_DEP					0x05
#define NCI_RF_PROTOCOL_T5T					0x06

/* NCI RF Interfaces */
#define NCI_RF_INTERFACE_NFCEE_DIRECT				0x00
#define NCI_RF_INTERFACE_FRAME					0x01
#define NCI_RF_INTERFACE_ISO_DEP				0x02
#define NCI_RF_INTERFACE_NFC_DEP				0x03

/* NCI Configuration Parameter Tags */
#define NCI_PN_ATR_REQ_GEN_BYTES				0x29
#define NCI_LN_ATR_RES_GEN_BYTES				0x61
#define NCI_LA_SEL_INFO						0x32
#define NCI_LF_PROTOCOL_TYPE					0x50
#define NCI_LF_CON_BITR_F					0x54

/* NCI Configuration Parameters masks */
#define NCI_LA_SEL_INFO_ISO_DEP_MASK				0x20
#define NCI_LA_SEL_INFO_NFC_DEP_MASK				0x40
#define NCI_LF_PROTOCOL_TYPE_NFC_DEP_MASK			0x02
#define NCI_LF_CON_BITR_F_212					0x02
#define NCI_LF_CON_BITR_F_424					0x04

/* NCI 2.x Feature Enable Bit */
#define NCI_FEATURE_DISABLE					0x00

/* NCI Reset types */
#define NCI_RESET_TYPE_KEEP_CONFIG				0x00
#define NCI_RESET_TYPE_RESET_CONFIG				0x01

/* NCI Static RF connection ID */
#define NCI_STATIC_RF_CONN_ID					0x00

/* NCI Data Flow Control */
#define NCI_DATA_FLOW_CONTROL_NOT_USED				0xff

/* NCI RF_DISCOVER_MAP_CMD modes */
#define NCI_DISC_MAP_MODE_POLL					0x01
#define NCI_DISC_MAP_MODE_LISTEN				0x02

/* NCI Discover Notification Type */
#define NCI_DISCOVER_NTF_TYPE_LAST				0x00
#define NCI_DISCOVER_NTF_TYPE_LAST_NFCC				0x01
#define NCI_DISCOVER_NTF_TYPE_MORE				0x02

/* NCI Deactivation Type */
#define NCI_DEACTIVATE_TYPE_IDLE_MODE				0x00
#define NCI_DEACTIVATE_TYPE_SLEEP_MODE				0x01
#define NCI_DEACTIVATE_TYPE_SLEEP_AF_MODE			0x02
#define NCI_DEACTIVATE_TYPE_DISCOVERY				0x03

/* Message Type (MT) */
#define NCI_MT_DATA_PKT						0x00
#define NCI_MT_CMD_PKT						0x01
#define NCI_MT_RSP_PKT						0x02
#define NCI_MT_NTF_PKT						0x03

#define nci_mt(hdr)			(((hdr)[0]>>5)&0x07)
#define nci_mt_set(hdr, mt)		((hdr)[0] |= (__u8)(((mt)&0x07)<<5))

/* Packet Boundary Flag (PBF) */
#define NCI_PBF_LAST						0x00
#define NCI_PBF_CONT						0x01

#define nci_pbf(hdr)			(__u8)(((hdr)[0]>>4)&0x01)
#define nci_pbf_set(hdr, pbf)		((hdr)[0] |= (__u8)(((pbf)&0x01)<<4))

/* Control Opcode manipulation */
#define nci_opcode_pack(gid, oid)	(__u16)((((__u16)((gid)&0x0f))<<8)|\
					((__u16)((oid)&0x3f)))
#define nci_opcode(hdr)			nci_opcode_pack(hdr[0], hdr[1])
#define nci_opcode_gid(op)		(__u8)(((op)&0x0f00)>>8)
#define nci_opcode_oid(op)		(__u8)((op)&0x003f)

/* Payload Length */
#define nci_plen(hdr)			(__u8)((hdr)[2])

/* Connection ID */
#define nci_conn_id(hdr)		(__u8)(((hdr)[0])&0x0f)

/* GID values */
#define NCI_GID_CORE						0x0
#define NCI_GID_RF_MGMT						0x1
#define NCI_GID_NFCEE_MGMT					0x2
#define NCI_GID_PROPRIETARY					0xf

/* ----- NCI over SPI head/crc(tail) room needed for outgoing frames ----- */
#define NCI_SPI_HDR_LEN						4
#define NCI_SPI_CRC_LEN						2

/* ---- NCI Packet structures ---- */
#define NCI_CTRL_HDR_SIZE					3
#define NCI_DATA_HDR_SIZE					3

struct nci_ctrl_hdr {
	__u8	gid;		/* MT & PBF & GID */
	__u8	oid;
	__u8	plen;
} __packed;

struct nci_data_hdr {
	__u8	conn_id;	/* MT & PBF & ConnID */
	__u8	rfu;
	__u8	plen;
} __packed;

/* ------------------------ */
/* -----  NCI Commands ---- */
/* ------------------------ */
#define NCI_OP_CORE_RESET_CMD		nci_opcode_pack(NCI_GID_CORE, 0x00)
struct nci_core_reset_cmd {
	__u8	reset_type;
} __packed;

#define NCI_OP_CORE_INIT_CMD		nci_opcode_pack(NCI_GID_CORE, 0x01)
/* To support NCI 2.x */
struct nci_core_init_v2_cmd {
	u8	feature1;
	u8	feature2;
};

#define NCI_OP_CORE_SET_CONFIG_CMD	nci_opcode_pack(NCI_GID_CORE, 0x02)
struct set_config_param {
	__u8	id;
	__u8	len;
	__u8	val[NCI_MAX_PARAM_LEN];
} __packed;

struct nci_core_set_config_cmd {
	__u8	num_params;
	struct	set_config_param param; /* support 1 param per cmd is enough */
} __packed;

#define NCI_OP_CORE_CONN_CREATE_CMD	nci_opcode_pack(NCI_GID_CORE, 0x04)
#define DEST_SPEC_PARAMS_ID_INDEX	0
#define DEST_SPEC_PARAMS_PROTOCOL_INDEX	1
struct dest_spec_params {
	__u8    id;
	__u8    protocol;
} __packed;

struct core_conn_create_dest_spec_params {
	__u8    type;
	__u8    length;
	__u8    value[];
} __packed;

struct nci_core_conn_create_cmd {
	__u8    destination_type;
	__u8    number_destination_params;
	struct core_conn_create_dest_spec_params params[];
} __packed;

#define NCI_OP_CORE_CONN_CLOSE_CMD	nci_opcode_pack(NCI_GID_CORE, 0x05)

#define NCI_OP_RF_DISCOVER_MAP_CMD	nci_opcode_pack(NCI_GID_RF_MGMT, 0x00)
struct disc_map_config {
	__u8	rf_protocol;
	__u8	mode;
	__u8	rf_interface;
} __packed;

struct nci_rf_disc_map_cmd {
	__u8				num_mapping_configs;
	struct disc_map_config		mapping_configs
					[NCI_MAX_NUM_MAPPING_CONFIGS];
} __packed;

#define NCI_OP_RF_DISCOVER_CMD		nci_opcode_pack(NCI_GID_RF_MGMT, 0x03)
struct disc_config {
	__u8	rf_tech_and_mode;
	__u8	frequency;
} __packed;

struct nci_rf_disc_cmd {
	__u8				num_disc_configs;
	struct disc_config		disc_configs[NCI_MAX_NUM_RF_CONFIGS];
} __packed;

#define NCI_OP_RF_DISCOVER_SELECT_CMD	nci_opcode_pack(NCI_GID_RF_MGMT, 0x04)
struct nci_rf_discover_select_cmd {
	__u8	rf_discovery_id;
	__u8	rf_protocol;
	__u8	rf_interface;
} __packed;

#define NCI_OP_RF_DEACTIVATE_CMD	nci_opcode_pack(NCI_GID_RF_MGMT, 0x06)
struct nci_rf_deactivate_cmd {
	__u8	type;
} __packed;

#define NCI_OP_NFCEE_DISCOVER_CMD nci_opcode_pack(NCI_GID_NFCEE_MGMT, 0x00)
struct nci_nfcee_discover_cmd {
	__u8	discovery_action;
} __packed;

#define NCI_OP_NFCEE_MODE_SET_CMD nci_opcode_pack(NCI_GID_NFCEE_MGMT, 0x01)
#define NCI_NFCEE_DISABLE	0x00
#define NCI_NFCEE_ENABLE	0x01
struct nci_nfcee_mode_set_cmd {
	__u8	nfcee_id;
	__u8	nfcee_mode;
} __packed;

#define NCI_OP_CORE_GET_CONFIG_CMD	nci_opcode_pack(NCI_GID_CORE, 0x03)

/* ----------------------- */
/* ---- NCI Responses ---- */
/* ----------------------- */
#define NCI_OP_CORE_RESET_RSP		nci_opcode_pack(NCI_GID_CORE, 0x00)
struct nci_core_reset_rsp {
	__u8	status;
	__u8	nci_ver;
	__u8	config_status;
} __packed;

#define NCI_OP_CORE_INIT_RSP		nci_opcode_pack(NCI_GID_CORE, 0x01)
struct nci_core_init_rsp_1 {
	__u8	status;
	__le32	nfcc_features;
	__u8	num_supported_rf_interfaces;
	__u8	supported_rf_interfaces[];	/* variable size array */
	/* continued in nci_core_init_rsp_2 */
} __packed;

struct nci_core_init_rsp_2 {
	__u8	max_logical_connections;
	__le16	max_routing_table_size;
	__u8	max_ctrl_pkt_payload_len;
	__le16	max_size_for_large_params;
	__u8	manufact_id;
	__le32	manufact_specific_info;
} __packed;

/* To support NCI ver 2.x */
struct nci_core_init_rsp_nci_ver2 {
	u8	status;
	__le32	nfcc_features;
	u8	max_logical_connections;
	__le16	max_routing_table_size;
	u8	max_ctrl_pkt_payload_len;
	u8	max_data_pkt_hci_payload_len;
	u8	number_of_hci_credit;
	__le16	max_nfc_v_frame_size;
	u8	num_supported_rf_interfaces;
	u8	supported_rf_interfaces[];
} __packed;

#define NCI_OP_CORE_SET_CONFIG_RSP	nci_opcode_pack(NCI_GID_CORE, 0x02)
struct nci_core_set_config_rsp {
	__u8	status;
	__u8	num_params;
	__u8	params_id[];	/* variable size array */
} __packed;

#define NCI_OP_CORE_CONN_CREATE_RSP	nci_opcode_pack(NCI_GID_CORE, 0x04)
struct nci_core_conn_create_rsp {
	__u8	status;
	__u8	max_ctrl_pkt_payload_len;
	__u8    credits_cnt;
	__u8	conn_id;
} __packed;

#define NCI_OP_CORE_CONN_CLOSE_RSP	nci_opcode_pack(NCI_GID_CORE, 0x05)

#define NCI_OP_RF_DISCOVER_MAP_RSP	nci_opcode_pack(NCI_GID_RF_MGMT, 0x00)

#define NCI_OP_RF_DISCOVER_RSP		nci_opcode_pack(NCI_GID_RF_MGMT, 0x03)

#define NCI_OP_RF_DISCOVER_SELECT_RSP	nci_opcode_pack(NCI_GID_RF_MGMT, 0x04)

#define NCI_OP_RF_DEACTIVATE_RSP	nci_opcode_pack(NCI_GID_RF_MGMT, 0x06)

#define NCI_OP_NFCEE_DISCOVER_RSP nci_opcode_pack(NCI_GID_NFCEE_MGMT, 0x00)
struct nci_nfcee_discover_rsp {
	__u8	status;
	__u8	num_nfcee;
} __packed;

#define NCI_OP_NFCEE_MODE_SET_RSP nci_opcode_pack(NCI_GID_NFCEE_MGMT, 0x01)

#define NCI_OP_CORE_GET_CONFIG_RSP	nci_opcode_pack(NCI_GID_CORE, 0x03)

/* --------------------------- */
/* ---- NCI Notifications ---- */
/* --------------------------- */
#define NCI_OP_CORE_RESET_NTF		nci_opcode_pack(NCI_GID_CORE, 0x00)
struct nci_core_reset_ntf {
	u8	reset_trigger;
	u8	config_status;
	u8	nci_ver;
	u8	manufact_id;
	u8	manufacturer_specific_len;
	__le32	manufact_specific_info;
} __packed;

#define NCI_OP_CORE_CONN_CREDITS_NTF	nci_opcode_pack(NCI_GID_CORE, 0x06)
struct conn_credit_entry {
	__u8	conn_id;
	__u8	credits;
} __packed;

struct nci_core_conn_credit_ntf {
	__u8				num_entries;
	struct conn_credit_entry	conn_entries[NCI_MAX_NUM_CONN];
} __packed;

#define NCI_OP_CORE_GENERIC_ERROR_NTF	nci_opcode_pack(NCI_GID_CORE, 0x07)

#define NCI_OP_CORE_INTF_ERROR_NTF	nci_opcode_pack(NCI_GID_CORE, 0x08)
struct nci_core_intf_error_ntf {
	__u8	status;
	__u8	conn_id;
} __packed;

#define NCI_OP_RF_DISCOVER_NTF		nci_opcode_pack(NCI_GID_RF_MGMT, 0x03)
struct rf_tech_specific_params_nfca_poll {
	__u16	sens_res;
	__u8	nfcid1_len;	/* 0, 4, 7, or 10 Bytes */
	__u8	nfcid1[NFC_NFCID1_MAXSIZE];
	__u8	sel_res_len;	/* 0 or 1 Bytes */
	__u8	sel_res;
} __packed;

struct rf_tech_specific_params_nfcb_poll {
	__u8	sensb_res_len;
	__u8	sensb_res[NFC_SENSB_RES_MAXSIZE];	/* 11 or 12 Bytes */
} __packed;

struct rf_tech_specific_params_nfcf_poll {
	__u8	bit_rate;
	__u8	sensf_res_len;
	__u8	sensf_res[NFC_SENSF_RES_MAXSIZE];	/* 16 or 18 Bytes */
} __packed;

struct rf_tech_specific_params_nfcv_poll {
	__u8	res_flags;
	__u8	dsfid;
	__u8	uid[NFC_ISO15693_UID_MAXSIZE];	/* 8 Bytes */
} __packed;

struct rf_tech_specific_params_nfcf_listen {
	__u8	local_nfcid2_len;
	__u8	local_nfcid2[NFC_NFCID2_MAXSIZE];	/* 0 or 8 Bytes */
} __packed;

struct nci_rf_discover_ntf {
	__u8	rf_discovery_id;
	__u8	rf_protocol;
	__u8	rf_tech_and_mode;
	__u8	rf_tech_specific_params_len;

	union {
		struct rf_tech_specific_params_nfca_poll nfca_poll;
		struct rf_tech_specific_params_nfcb_poll nfcb_poll;
		struct rf_tech_specific_params_nfcf_poll nfcf_poll;
		struct rf_tech_specific_params_nfcv_poll nfcv_poll;
	} rf_tech_specific_params;

	__u8	ntf_type;
} __packed;

#define NCI_OP_RF_INTF_ACTIVATED_NTF	nci_opcode_pack(NCI_GID_RF_MGMT, 0x05)
struct activation_params_nfca_poll_iso_dep {
	__u8	rats_res_len;
	__u8	rats_res[20];
};

struct activation_params_nfcb_poll_iso_dep {
	__u8	attrib_res_len;
	__u8	attrib_res[50];
};

struct activation_params_poll_nfc_dep {
	__u8	atr_res_len;
	__u8	atr_res[NFC_ATR_RES_MAXSIZE - 2]; /* ATR_RES from byte 3 */
};

struct activation_params_listen_nfc_dep {
	__u8	atr_req_len;
	__u8	atr_req[NFC_ATR_REQ_MAXSIZE - 2]; /* ATR_REQ from byte 3 */
};

struct nci_rf_intf_activated_ntf {
	__u8	rf_discovery_id;
	__u8	rf_interface;
	__u8	rf_protocol;
	__u8	activation_rf_tech_and_mode;
	__u8	max_data_pkt_payload_size;
	__u8	initial_num_credits;
	__u8	rf_tech_specific_params_len;

	union {
		struct rf_tech_specific_params_nfca_poll nfca_poll;
		struct rf_tech_specific_params_nfcb_poll nfcb_poll;
		struct rf_tech_specific_params_nfcf_poll nfcf_poll;
		struct rf_tech_specific_params_nfcv_poll nfcv_poll;
		struct rf_tech_specific_params_nfcf_listen nfcf_listen;
	} rf_tech_specific_params;

	__u8	data_exch_rf_tech_and_mode;
	__u8	data_exch_tx_bit_rate;
	__u8	data_exch_rx_bit_rate;
	__u8	activation_params_len;

	union {
		struct activation_params_nfca_poll_iso_dep nfca_poll_iso_dep;
		struct activation_params_nfcb_poll_iso_dep nfcb_poll_iso_dep;
		struct activation_params_poll_nfc_dep poll_nfc_dep;
		struct activation_params_listen_nfc_dep listen_nfc_dep;
	} activation_params;

} __packed;

#define NCI_OP_RF_DEACTIVATE_NTF	nci_opcode_pack(NCI_GID_RF_MGMT, 0x06)
struct nci_rf_deactivate_ntf {
	__u8	type;
	__u8	reason;
} __packed;

#define NCI_OP_RF_NFCEE_ACTION_NTF	nci_opcode_pack(NCI_GID_RF_MGMT, 0x09)
struct nci_rf_nfcee_action_ntf {
	__u8 nfcee_id;
	__u8 trigger;
	__u8 supported_data_length;
	__u8 supported_data[];
} __packed;

#define NCI_OP_NFCEE_DISCOVER_NTF nci_opcode_pack(NCI_GID_NFCEE_MGMT, 0x00)
struct nci_nfcee_supported_protocol {
	__u8	num_protocol;
	__u8	supported_protocol[];
} __packed;

struct nci_nfcee_information_tlv {
	__u8	num_tlv;
	__u8	information_tlv[];
} __packed;

struct nci_nfcee_discover_ntf {
	__u8	nfcee_id;
	__u8	nfcee_status;
	struct nci_nfcee_supported_protocol supported_protocols;
	struct nci_nfcee_information_tlv	information_tlv;
} __packed;

#define NCI_OP_CORE_RESET_NTF		nci_opcode_pack(NCI_GID_CORE, 0x00)

#endif /* __NCI_H */
