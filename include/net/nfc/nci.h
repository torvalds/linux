/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci.h, which was written
 *  by Maxim Krasnyansky.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __NCI_H
#define __NCI_H

/* NCI constants */
#define NCI_MAX_NUM_MAPPING_CONFIGS				10
#define NCI_MAX_NUM_RF_CONFIGS					10
#define NCI_MAX_NUM_CONN					10

/* NCI Status Codes */
#define	NCI_STATUS_OK						0x00
#define	NCI_STATUS_REJECTED					0x01
#define	NCI_STATUS_MESSAGE_CORRUPTED				0x02
#define	NCI_STATUS_BUFFER_FULL					0x03
#define	NCI_STATUS_FAILED					0x04
#define	NCI_STATUS_NOT_INITIALIZED				0x05
#define	NCI_STATUS_SYNTAX_ERROR					0x06
#define	NCI_STATUS_SEMANTIC_ERROR				0x07
#define	NCI_STATUS_UNKNOWN_GID					0x08
#define	NCI_STATUS_UNKNOWN_OID					0x09
#define	NCI_STATUS_INVALID_PARAM				0x0a
#define	NCI_STATUS_MESSAGE_SIZE_EXCEEDED			0x0b
/* Discovery Specific Status Codes */
#define	NCI_STATUS_DISCOVERY_ALREADY_STARTED			0xa0
#define	NCI_STATUS_DISCOVERY_TARGET_ACTIVATION_FAILED		0xa1
/* RF Interface Specific Status Codes */
#define	NCI_STATUS_RF_TRANSMISSION_ERROR			0xb0
#define	NCI_STATUS_RF_PROTOCOL_ERROR				0xb1
#define	NCI_STATUS_RF_TIMEOUT_ERROR				0xb2
#define	NCI_STATUS_RF_LINK_LOSS_ERROR				0xb3
/* NFCEE Interface Specific Status Codes */
#define	NCI_STATUS_MAX_ACTIVE_NFCEE_INTERFACES_REACHED		0xc0
#define	NCI_STATUS_NFCEE_INTERFACE_ACTIVATION_FAILED		0xc1
#define	NCI_STATUS_NFCEE_TRANSMISSION_ERROR			0xc2
#define	NCI_STATUS_NFCEE_PROTOCOL_ERROR				0xc3
#define NCI_STATUS_NFCEE_TIMEOUT_ERROR				0xc4

/* NCI RF Technology and Mode */
#define NCI_NFC_A_PASSIVE_POLL_MODE				0x00
#define NCI_NFC_B_PASSIVE_POLL_MODE				0x01
#define NCI_NFC_F_PASSIVE_POLL_MODE				0x02
#define NCI_NFC_A_ACTIVE_POLL_MODE				0x03
#define NCI_NFC_F_ACTIVE_POLL_MODE				0x05
#define NCI_NFC_A_PASSIVE_LISTEN_MODE				0x80
#define NCI_NFC_B_PASSIVE_LISTEN_MODE				0x81
#define NCI_NFC_F_PASSIVE_LISTEN_MODE				0x82
#define NCI_NFC_A_ACTIVE_LISTEN_MODE				0x83
#define NCI_NFC_F_ACTIVE_LISTEN_MODE				0x85

/* NCI RF Protocols */
#define NCI_RF_PROTOCOL_UNKNOWN					0x00
#define NCI_RF_PROTOCOL_T1T					0x01
#define NCI_RF_PROTOCOL_T2T					0x02
#define NCI_RF_PROTOCOL_T3T					0x03
#define NCI_RF_PROTOCOL_ISO_DEP					0x04
#define NCI_RF_PROTOCOL_NFC_DEP					0x05

/* NCI RF Interfaces */
#define NCI_RF_INTERFACE_RFU					0x00
#define	NCI_RF_INTERFACE_FRAME					0x01
#define	NCI_RF_INTERFACE_ISO_DEP				0x02
#define	NCI_RF_INTERFACE_NFC_DEP				0x03

/* NCI RF_DISCOVER_MAP_CMD modes */
#define NCI_DISC_MAP_MODE_POLL					0x01
#define NCI_DISC_MAP_MODE_LISTEN				0x02
#define NCI_DISC_MAP_MODE_BOTH					0x03

/* NCI Discovery Types */
#define NCI_DISCOVERY_TYPE_POLL_A_PASSIVE			0x00
#define	NCI_DISCOVERY_TYPE_POLL_B_PASSIVE			0x01
#define	NCI_DISCOVERY_TYPE_POLL_F_PASSIVE			0x02
#define	NCI_DISCOVERY_TYPE_POLL_A_ACTIVE			0x03
#define	NCI_DISCOVERY_TYPE_POLL_F_ACTIVE			0x05
#define	NCI_DISCOVERY_TYPE_WAKEUP_A_PASSIVE			0x06
#define	NCI_DISCOVERY_TYPE_WAKEUP_B_PASSIVE			0x07
#define	NCI_DISCOVERY_TYPE_WAKEUP_A_ACTIVE			0x09
#define	NCI_DISCOVERY_TYPE_LISTEN_A_PASSIVE			0x80
#define	NCI_DISCOVERY_TYPE_LISTEN_B_PASSIVE			0x81
#define	NCI_DISCOVERY_TYPE_LISTEN_F_PASSIVE			0x82
#define	NCI_DISCOVERY_TYPE_LISTEN_A_ACTIVE			0x83
#define	NCI_DISCOVERY_TYPE_LISTEN_F_ACTIVE			0x85

/* NCI Deactivation Type */
#define	NCI_DEACTIVATE_TYPE_IDLE_MODE				0x00
#define	NCI_DEACTIVATE_TYPE_SLEEP_MODE				0x01
#define	NCI_DEACTIVATE_TYPE_SLEEP_AF_MODE			0x02
#define	NCI_DEACTIVATE_TYPE_RF_LINK_LOSS			0x03
#define	NCI_DEACTIVATE_TYPE_DISCOVERY_ERROR			0x04

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
#define	NCI_GID_CORE						0x0
#define	NCI_GID_RF_MGMT						0x1
#define	NCI_GID_NFCEE_MGMT					0x2
#define	NCI_GID_PROPRIETARY					0xf

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

#define NCI_OP_CORE_INIT_CMD		nci_opcode_pack(NCI_GID_CORE, 0x01)

#define NCI_OP_CORE_SET_CONFIG_CMD	nci_opcode_pack(NCI_GID_CORE, 0x02)

#define NCI_OP_CORE_CONN_CREATE_CMD	nci_opcode_pack(NCI_GID_CORE, 0x04)
struct nci_core_conn_create_cmd {
	__u8	target_handle;
	__u8	num_target_specific_params;
} __packed;

#define NCI_OP_CORE_CONN_CLOSE_CMD	nci_opcode_pack(NCI_GID_CORE, 0x06)

#define NCI_OP_RF_DISCOVER_MAP_CMD	nci_opcode_pack(NCI_GID_RF_MGMT, 0x00)
struct disc_map_config {
	__u8	rf_protocol;
	__u8	mode;
	__u8	rf_interface_type;
} __packed;

struct nci_rf_disc_map_cmd {
	__u8				num_mapping_configs;
	struct disc_map_config		mapping_configs
					[NCI_MAX_NUM_MAPPING_CONFIGS];
} __packed;

#define NCI_OP_RF_DISCOVER_CMD		nci_opcode_pack(NCI_GID_RF_MGMT, 0x03)
struct disc_config {
	__u8	type;
	__u8	frequency;
} __packed;

struct nci_rf_disc_cmd {
	__u8				num_disc_configs;
	struct disc_config		disc_configs[NCI_MAX_NUM_RF_CONFIGS];
} __packed;

#define NCI_OP_RF_DEACTIVATE_CMD	nci_opcode_pack(NCI_GID_RF_MGMT, 0x06)
struct nci_rf_deactivate_cmd {
	__u8	type;
} __packed;

/* ----------------------- */
/* ---- NCI Responses ---- */
/* ----------------------- */
#define NCI_OP_CORE_RESET_RSP		nci_opcode_pack(NCI_GID_CORE, 0x00)
struct nci_core_reset_rsp {
	__u8	status;
	__u8	nci_ver;
} __packed;

#define NCI_OP_CORE_INIT_RSP		nci_opcode_pack(NCI_GID_CORE, 0x01)
struct nci_core_init_rsp_1 {
	__u8	status;
	__le32	nfcc_features;
	__u8	num_supported_rf_interfaces;
	__u8	supported_rf_interfaces[0];	/* variable size array */
	/* continuted in nci_core_init_rsp_2 */
} __packed;

struct nci_core_init_rsp_2 {
	__u8	max_logical_connections;
	__le16	max_routing_table_size;
	__u8	max_control_packet_payload_length;
	__le16	rf_sending_buffer_size;
	__le16	rf_receiving_buffer_size;
	__le16	manufacturer_id;
} __packed;

#define NCI_OP_CORE_SET_CONFIG_RSP	nci_opcode_pack(NCI_GID_CORE, 0x02)

#define NCI_OP_CORE_CONN_CREATE_RSP	nci_opcode_pack(NCI_GID_CORE, 0x04)
struct nci_core_conn_create_rsp {
	__u8	status;
	__u8	max_pkt_payload_size;
	__u8	initial_num_credits;
	__u8	conn_id;
} __packed;

#define NCI_OP_CORE_CONN_CLOSE_RSP	nci_opcode_pack(NCI_GID_CORE, 0x06)

#define NCI_OP_RF_DISCOVER_MAP_RSP	nci_opcode_pack(NCI_GID_RF_MGMT, 0x00)

#define NCI_OP_RF_DISCOVER_RSP		nci_opcode_pack(NCI_GID_RF_MGMT, 0x03)

#define NCI_OP_RF_DEACTIVATE_RSP	nci_opcode_pack(NCI_GID_RF_MGMT, 0x06)

/* --------------------------- */
/* ---- NCI Notifications ---- */
/* --------------------------- */
#define NCI_OP_CORE_CONN_CREDITS_NTF	nci_opcode_pack(NCI_GID_CORE, 0x07)
struct conn_credit_entry {
	__u8	conn_id;
	__u8	credits;
} __packed;

struct nci_core_conn_credit_ntf {
	__u8				num_entries;
	struct conn_credit_entry	conn_entries[NCI_MAX_NUM_CONN];
} __packed;

#define NCI_OP_RF_FIELD_INFO_NTF	nci_opcode_pack(NCI_GID_CORE, 0x08)
struct nci_rf_field_info_ntf {
	__u8	rf_field_status;
} __packed;

#define NCI_OP_RF_ACTIVATE_NTF		nci_opcode_pack(NCI_GID_RF_MGMT, 0x05)
struct rf_tech_specific_params_nfca_poll {
	__u16	sens_res;
	__u8	nfcid1_len;	/* 0, 4, 7, or 10 Bytes */
	__u8	nfcid1[10];
	__u8	sel_res_len;	/* 0 or 1 Bytes */
	__u8	sel_res;
} __packed;

struct activation_params_nfca_poll_iso_dep {
	__u8	rats_res_len;
	__u8	rats_res[20];
};

struct nci_rf_activate_ntf {
	__u8	target_handle;
	__u8	rf_protocol;
	__u8	rf_tech_and_mode;
	__u8	rf_tech_specific_params_len;

	union {
		struct rf_tech_specific_params_nfca_poll nfca_poll;
	} rf_tech_specific_params;

	__u8	rf_interface_type;
	__u8	activation_params_len;

	union {
		struct activation_params_nfca_poll_iso_dep nfca_poll_iso_dep;
	} activation_params;

} __packed;

#define NCI_OP_RF_DEACTIVATE_NTF	nci_opcode_pack(NCI_GID_RF_MGMT, 0x06)

#endif /* __NCI_H */
