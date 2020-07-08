/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2020, Mellanox Technologies inc. All rights reserved.
 *
 * This file is IBTA volume 1, chapter 12 declarations:
 *  CHAPTER 12: COMMUNICATION MANAGEMENT
 */
#ifndef _IBTA_VOL1_C12_H_
#define _IBTA_VOL1_C12_H_

#include <rdma/iba.h>

#define CM_FIELD_BLOC(field_struct, byte_offset, bits_offset, width)           \
	IBA_FIELD_BLOC(field_struct,                                           \
		       (byte_offset + sizeof(struct ib_mad_hdr)), bits_offset, \
		       width)
#define CM_FIELD8_LOC(field_struct, byte_offset, width)                        \
	IBA_FIELD8_LOC(field_struct,                                           \
		       (byte_offset + sizeof(struct ib_mad_hdr)), width)
#define CM_FIELD16_LOC(field_struct, byte_offset, width)                       \
	IBA_FIELD16_LOC(field_struct,                                          \
			(byte_offset + sizeof(struct ib_mad_hdr)), width)
#define CM_FIELD32_LOC(field_struct, byte_offset, width)                       \
	IBA_FIELD32_LOC(field_struct,                                          \
			(byte_offset + sizeof(struct ib_mad_hdr)), width)
#define CM_FIELD64_LOC(field_struct, byte_offset)                              \
	IBA_FIELD64_LOC(field_struct, (byte_offset + sizeof(struct ib_mad_hdr)))
#define CM_FIELD_MLOC(field_struct, byte_offset, width, type)                  \
	IBA_FIELD_MLOC(field_struct,                                           \
		       (byte_offset + sizeof(struct ib_mad_hdr)), width, type)
#define CM_STRUCT(field_struct, total_len)                                     \
	field_struct                                                           \
	{                                                                      \
		struct ib_mad_hdr hdr;                                         \
		u32 _data[(total_len) / 32 +                                   \
			  BUILD_BUG_ON_ZERO((total_len) % 32 != 0)];           \
	}

/* Table 106 REQ Message Contents */
#define CM_REQ_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_req_msg, 0, 32)
#define CM_REQ_VENDOR_ID CM_FIELD32_LOC(struct cm_req_msg, 5, 24)
#define CM_REQ_SERVICE_ID CM_FIELD64_LOC(struct cm_req_msg, 8)
#define CM_REQ_LOCAL_CA_GUID CM_FIELD64_LOC(struct cm_req_msg, 16)
#define CM_REQ_LOCAL_Q_KEY CM_FIELD32_LOC(struct cm_req_msg, 28, 32)
#define CM_REQ_LOCAL_QPN CM_FIELD32_LOC(struct cm_req_msg, 32, 24)
#define CM_REQ_RESPONDER_RESOURCES CM_FIELD8_LOC(struct cm_req_msg, 35, 8)
#define CM_REQ_LOCAL_EECN CM_FIELD32_LOC(struct cm_req_msg, 36, 24)
#define CM_REQ_INITIATOR_DEPTH CM_FIELD8_LOC(struct cm_req_msg, 39, 8)
#define CM_REQ_REMOTE_EECN CM_FIELD32_LOC(struct cm_req_msg, 40, 24)
#define CM_REQ_REMOTE_CM_RESPONSE_TIMEOUT                                      \
	CM_FIELD8_LOC(struct cm_req_msg, 43, 5)
#define CM_REQ_TRANSPORT_SERVICE_TYPE CM_FIELD_BLOC(struct cm_req_msg, 43, 5, 2)
#define CM_REQ_END_TO_END_FLOW_CONTROL                                         \
	CM_FIELD_BLOC(struct cm_req_msg, 43, 7, 1)
#define CM_REQ_STARTING_PSN CM_FIELD32_LOC(struct cm_req_msg, 44, 24)
#define CM_REQ_LOCAL_CM_RESPONSE_TIMEOUT CM_FIELD8_LOC(struct cm_req_msg, 47, 5)
#define CM_REQ_RETRY_COUNT CM_FIELD_BLOC(struct cm_req_msg, 47, 5, 3)
#define CM_REQ_PARTITION_KEY CM_FIELD16_LOC(struct cm_req_msg, 48, 16)
#define CM_REQ_PATH_PACKET_PAYLOAD_MTU CM_FIELD8_LOC(struct cm_req_msg, 50, 4)
#define CM_REQ_RDC_EXISTS CM_FIELD_BLOC(struct cm_req_msg, 50, 4, 1)
#define CM_REQ_RNR_RETRY_COUNT CM_FIELD_BLOC(struct cm_req_msg, 50, 5, 3)
#define CM_REQ_MAX_CM_RETRIES CM_FIELD8_LOC(struct cm_req_msg, 51, 4)
#define CM_REQ_SRQ CM_FIELD_BLOC(struct cm_req_msg, 51, 4, 1)
#define CM_REQ_EXTENDED_TRANSPORT_TYPE                                         \
	CM_FIELD_BLOC(struct cm_req_msg, 51, 5, 3)
#define CM_REQ_PRIMARY_LOCAL_PORT_LID CM_FIELD16_LOC(struct cm_req_msg, 52, 16)
#define CM_REQ_PRIMARY_REMOTE_PORT_LID CM_FIELD16_LOC(struct cm_req_msg, 54, 16)
#define CM_REQ_PRIMARY_LOCAL_PORT_GID                                          \
	CM_FIELD_MLOC(struct cm_req_msg, 56, 128, union ib_gid)
#define CM_REQ_PRIMARY_REMOTE_PORT_GID                                         \
	CM_FIELD_MLOC(struct cm_req_msg, 72, 128, union ib_gid)
#define CM_REQ_PRIMARY_FLOW_LABEL CM_FIELD32_LOC(struct cm_req_msg, 88, 20)
#define CM_REQ_PRIMARY_PACKET_RATE CM_FIELD_BLOC(struct cm_req_msg, 91, 2, 6)
#define CM_REQ_PRIMARY_TRAFFIC_CLASS CM_FIELD8_LOC(struct cm_req_msg, 92, 8)
#define CM_REQ_PRIMARY_HOP_LIMIT CM_FIELD8_LOC(struct cm_req_msg, 93, 8)
#define CM_REQ_PRIMARY_SL CM_FIELD8_LOC(struct cm_req_msg, 94, 4)
#define CM_REQ_PRIMARY_SUBNET_LOCAL CM_FIELD_BLOC(struct cm_req_msg, 94, 4, 1)
#define CM_REQ_PRIMARY_LOCAL_ACK_TIMEOUT CM_FIELD8_LOC(struct cm_req_msg, 95, 5)
#define CM_REQ_ALTERNATE_LOCAL_PORT_LID                                        \
	CM_FIELD16_LOC(struct cm_req_msg, 96, 16)
#define CM_REQ_ALTERNATE_REMOTE_PORT_LID                                       \
	CM_FIELD16_LOC(struct cm_req_msg, 98, 16)
#define CM_REQ_ALTERNATE_LOCAL_PORT_GID                                        \
	CM_FIELD_MLOC(struct cm_req_msg, 100, 128, union ib_gid)
#define CM_REQ_ALTERNATE_REMOTE_PORT_GID                                       \
	CM_FIELD_MLOC(struct cm_req_msg, 116, 128, union ib_gid)
#define CM_REQ_ALTERNATE_FLOW_LABEL CM_FIELD32_LOC(struct cm_req_msg, 132, 20)
#define CM_REQ_ALTERNATE_PACKET_RATE CM_FIELD_BLOC(struct cm_req_msg, 135, 2, 6)
#define CM_REQ_ALTERNATE_TRAFFIC_CLASS CM_FIELD8_LOC(struct cm_req_msg, 136, 8)
#define CM_REQ_ALTERNATE_HOP_LIMIT CM_FIELD8_LOC(struct cm_req_msg, 137, 8)
#define CM_REQ_ALTERNATE_SL CM_FIELD8_LOC(struct cm_req_msg, 138, 4)
#define CM_REQ_ALTERNATE_SUBNET_LOCAL                                          \
	CM_FIELD_BLOC(struct cm_req_msg, 138, 4, 1)
#define CM_REQ_ALTERNATE_LOCAL_ACK_TIMEOUT                                     \
	CM_FIELD8_LOC(struct cm_req_msg, 139, 5)
#define CM_REQ_SAP_SUPPORTED CM_FIELD_BLOC(struct cm_req_msg, 139, 5, 1)
#define CM_REQ_PRIVATE_DATA CM_FIELD_MLOC(struct cm_req_msg, 140, 736, void)
CM_STRUCT(struct cm_req_msg, 140 * 8 + 736);

/* Table 107 MRA Message Contents */
#define CM_MRA_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_mra_msg, 0, 32)
#define CM_MRA_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_mra_msg, 4, 32)
#define CM_MRA_MESSAGE_MRAED CM_FIELD8_LOC(struct cm_mra_msg, 8, 2)
#define CM_MRA_SERVICE_TIMEOUT CM_FIELD8_LOC(struct cm_mra_msg, 9, 5)
#define CM_MRA_PRIVATE_DATA CM_FIELD_MLOC(struct cm_mra_msg, 10, 1776, void)
CM_STRUCT(struct cm_mra_msg, 10 * 8 + 1776);

/* Table 108 REJ Message Contents */
#define CM_REJ_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_rej_msg, 0, 32)
#define CM_REJ_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_rej_msg, 4, 32)
#define CM_REJ_MESSAGE_REJECTED CM_FIELD8_LOC(struct cm_rej_msg, 8, 2)
#define CM_REJ_REJECTED_INFO_LENGTH CM_FIELD8_LOC(struct cm_rej_msg, 9, 7)
#define CM_REJ_REASON CM_FIELD16_LOC(struct cm_rej_msg, 10, 16)
#define CM_REJ_ARI CM_FIELD_MLOC(struct cm_rej_msg, 12, 576, void)
#define CM_REJ_PRIVATE_DATA CM_FIELD_MLOC(struct cm_rej_msg, 84, 1184, void)
CM_STRUCT(struct cm_rej_msg, 84 * 8 + 1184);

/* Table 110 REP Message Contents */
#define CM_REP_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_rep_msg, 0, 32)
#define CM_REP_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_rep_msg, 4, 32)
#define CM_REP_LOCAL_Q_KEY CM_FIELD32_LOC(struct cm_rep_msg, 8, 32)
#define CM_REP_LOCAL_QPN CM_FIELD32_LOC(struct cm_rep_msg, 12, 24)
#define CM_REP_VENDOR_ID_H CM_FIELD8_LOC(struct cm_rep_msg, 15, 8)
#define CM_REP_LOCAL_EE_CONTEXT_NUMBER CM_FIELD32_LOC(struct cm_rep_msg, 16, 24)
#define CM_REP_VENDOR_ID_M CM_FIELD8_LOC(struct cm_rep_msg, 19, 8)
#define CM_REP_STARTING_PSN CM_FIELD32_LOC(struct cm_rep_msg, 20, 24)
#define CM_REP_VENDOR_ID_L CM_FIELD8_LOC(struct cm_rep_msg, 23, 8)
#define CM_REP_RESPONDER_RESOURCES CM_FIELD8_LOC(struct cm_rep_msg, 24, 8)
#define CM_REP_INITIATOR_DEPTH CM_FIELD8_LOC(struct cm_rep_msg, 25, 8)
#define CM_REP_TARGET_ACK_DELAY CM_FIELD8_LOC(struct cm_rep_msg, 26, 5)
#define CM_REP_FAILOVER_ACCEPTED CM_FIELD_BLOC(struct cm_rep_msg, 26, 5, 2)
#define CM_REP_END_TO_END_FLOW_CONTROL                                         \
	CM_FIELD_BLOC(struct cm_rep_msg, 26, 7, 1)
#define CM_REP_RNR_RETRY_COUNT CM_FIELD8_LOC(struct cm_rep_msg, 27, 3)
#define CM_REP_SRQ CM_FIELD_BLOC(struct cm_rep_msg, 27, 3, 1)
#define CM_REP_LOCAL_CA_GUID CM_FIELD64_LOC(struct cm_rep_msg, 28)
#define CM_REP_PRIVATE_DATA CM_FIELD_MLOC(struct cm_rep_msg, 36, 1568, void)
CM_STRUCT(struct cm_rep_msg, 36 * 8 + 1568);

/* Table 111 RTU Message Contents */
#define CM_RTU_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_rtu_msg, 0, 32)
#define CM_RTU_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_rtu_msg, 4, 32)
#define CM_RTU_PRIVATE_DATA CM_FIELD_MLOC(struct cm_rtu_msg, 8, 1792, void)
CM_STRUCT(struct cm_rtu_msg, 8 * 8 + 1792);

/* Table 112 DREQ Message Contents */
#define CM_DREQ_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_dreq_msg, 0, 32)
#define CM_DREQ_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_dreq_msg, 4, 32)
#define CM_DREQ_REMOTE_QPN_EECN CM_FIELD32_LOC(struct cm_dreq_msg, 8, 24)
#define CM_DREQ_PRIVATE_DATA CM_FIELD_MLOC(struct cm_dreq_msg, 12, 1760, void)
CM_STRUCT(struct cm_dreq_msg, 12 * 8 + 1760);

/* Table 113 DREP Message Contents */
#define CM_DREP_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_drep_msg, 0, 32)
#define CM_DREP_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_drep_msg, 4, 32)
#define CM_DREP_PRIVATE_DATA CM_FIELD_MLOC(struct cm_drep_msg, 8, 1792, void)
CM_STRUCT(struct cm_drep_msg, 8 * 8 + 1792);

/* Table 115 LAP Message Contents */
#define CM_LAP_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_lap_msg, 0, 32)
#define CM_LAP_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_lap_msg, 4, 32)
#define CM_LAP_REMOTE_QPN_EECN CM_FIELD32_LOC(struct cm_lap_msg, 12, 24)
#define CM_LAP_REMOTE_CM_RESPONSE_TIMEOUT                                      \
	CM_FIELD8_LOC(struct cm_lap_msg, 15, 5)
#define CM_LAP_ALTERNATE_LOCAL_PORT_LID                                        \
	CM_FIELD16_LOC(struct cm_lap_msg, 20, 16)
#define CM_LAP_ALTERNATE_REMOTE_PORT_LID                                       \
	CM_FIELD16_LOC(struct cm_lap_msg, 22, 16)
#define CM_LAP_ALTERNATE_LOCAL_PORT_GID                                        \
	CM_FIELD_MLOC(struct cm_lap_msg, 24, 128, union ib_gid)
#define CM_LAP_ALTERNATE_REMOTE_PORT_GID                                       \
	CM_FIELD_MLOC(struct cm_lap_msg, 40, 128, union ib_gid)
#define CM_LAP_ALTERNATE_FLOW_LABEL CM_FIELD32_LOC(struct cm_lap_msg, 56, 20)
#define CM_LAP_ALTERNATE_TRAFFIC_CLASS CM_FIELD8_LOC(struct cm_lap_msg, 59, 8)
#define CM_LAP_ALTERNATE_HOP_LIMIT CM_FIELD8_LOC(struct cm_lap_msg, 60, 8)
#define CM_LAP_ALTERNATE_PACKET_RATE CM_FIELD_BLOC(struct cm_lap_msg, 61, 2, 6)
#define CM_LAP_ALTERNATE_SL CM_FIELD8_LOC(struct cm_lap_msg, 62, 4)
#define CM_LAP_ALTERNATE_SUBNET_LOCAL CM_FIELD_BLOC(struct cm_lap_msg, 62, 4, 1)
#define CM_LAP_ALTERNATE_LOCAL_ACK_TIMEOUT                                     \
	CM_FIELD8_LOC(struct cm_lap_msg, 63, 5)
#define CM_LAP_PRIVATE_DATA CM_FIELD_MLOC(struct cm_lap_msg, 64, 1344, void)
CM_STRUCT(struct cm_lap_msg, 64 * 8 + 1344);

/* Table 116 APR Message Contents */
#define CM_APR_LOCAL_COMM_ID CM_FIELD32_LOC(struct cm_apr_msg, 0, 32)
#define CM_APR_REMOTE_COMM_ID CM_FIELD32_LOC(struct cm_apr_msg, 4, 32)
#define CM_APR_ADDITIONAL_INFORMATION_LENGTH                                   \
	CM_FIELD8_LOC(struct cm_apr_msg, 8, 8)
#define CM_APR_AR_STATUS CM_FIELD8_LOC(struct cm_apr_msg, 9, 8)
#define CM_APR_ADDITIONAL_INFORMATION                                          \
	CM_FIELD_MLOC(struct cm_apr_msg, 12, 576, void)
#define CM_APR_PRIVATE_DATA CM_FIELD_MLOC(struct cm_apr_msg, 84, 1184, void)
CM_STRUCT(struct cm_apr_msg, 84 * 8 + 1184);

/* Table 119 SIDR_REQ Message Contents */
#define CM_SIDR_REQ_REQUESTID CM_FIELD32_LOC(struct cm_sidr_req_msg, 0, 32)
#define CM_SIDR_REQ_PARTITION_KEY CM_FIELD16_LOC(struct cm_sidr_req_msg, 4, 16)
#define CM_SIDR_REQ_SERVICEID CM_FIELD64_LOC(struct cm_sidr_req_msg, 8)
#define CM_SIDR_REQ_PRIVATE_DATA                                               \
	CM_FIELD_MLOC(struct cm_sidr_req_msg, 16, 1728, void)
CM_STRUCT(struct cm_sidr_req_msg, 16 * 8 + 1728);

/* Table 120 SIDR_REP Message Contents */
#define CM_SIDR_REP_REQUESTID CM_FIELD32_LOC(struct cm_sidr_rep_msg, 0, 32)
#define CM_SIDR_REP_STATUS CM_FIELD8_LOC(struct cm_sidr_rep_msg, 4, 8)
#define CM_SIDR_REP_ADDITIONAL_INFORMATION_LENGTH                              \
	CM_FIELD8_LOC(struct cm_sidr_rep_msg, 5, 8)
#define CM_SIDR_REP_VENDOR_ID_H CM_FIELD16_LOC(struct cm_sidr_rep_msg, 6, 16)
#define CM_SIDR_REP_QPN CM_FIELD32_LOC(struct cm_sidr_rep_msg, 8, 24)
#define CM_SIDR_REP_VENDOR_ID_L CM_FIELD8_LOC(struct cm_sidr_rep_msg, 11, 8)
#define CM_SIDR_REP_SERVICEID CM_FIELD64_LOC(struct cm_sidr_rep_msg, 12)
#define CM_SIDR_REP_Q_KEY CM_FIELD32_LOC(struct cm_sidr_rep_msg, 20, 32)
#define CM_SIDR_REP_ADDITIONAL_INFORMATION                                     \
	CM_FIELD_MLOC(struct cm_sidr_rep_msg, 24, 576, void)
#define CM_SIDR_REP_PRIVATE_DATA                                               \
	CM_FIELD_MLOC(struct cm_sidr_rep_msg, 96, 1088, void)
CM_STRUCT(struct cm_sidr_rep_msg, 96 * 8 + 1088);

#endif /* _IBTA_VOL1_C12_H_ */
