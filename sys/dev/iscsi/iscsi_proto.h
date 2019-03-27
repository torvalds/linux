/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef ISCSI_PROTO_H
#define	ISCSI_PROTO_H

#ifndef CTASSERT
#define CTASSERT(x)		_CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)		__CTASSERT(x, y)
#define __CTASSERT(x, y)	typedef char __assert_ ## y [(x) ? 1 : -1]
#endif

#define	ISCSI_SNGT(x, y)	((int32_t)(x) - (int32_t)(y) > 0)
#define	ISCSI_SNLT(x, y)	((int32_t)(x) - (int32_t)(y) < 0)

#define	ISCSI_BHS_SIZE			48
#define	ISCSI_HEADER_DIGEST_SIZE	4
#define	ISCSI_DATA_DIGEST_SIZE		4

#define	ISCSI_BHS_OPCODE_IMMEDIATE	0x40

#define	ISCSI_BHS_OPCODE_NOP_OUT	0x00
#define	ISCSI_BHS_OPCODE_SCSI_COMMAND	0x01
#define	ISCSI_BHS_OPCODE_TASK_REQUEST	0x02
#define	ISCSI_BHS_OPCODE_LOGIN_REQUEST	0x03
#define	ISCSI_BHS_OPCODE_TEXT_REQUEST	0x04
#define	ISCSI_BHS_OPCODE_SCSI_DATA_OUT	0x05
#define	ISCSI_BHS_OPCODE_LOGOUT_REQUEST	0x06

#define	ISCSI_BHS_OPCODE_NOP_IN		0x20
#define	ISCSI_BHS_OPCODE_SCSI_RESPONSE	0x21
#define	ISCSI_BHS_OPCODE_TASK_RESPONSE	0x22
#define	ISCSI_BHS_OPCODE_LOGIN_RESPONSE	0x23
#define	ISCSI_BHS_OPCODE_TEXT_RESPONSE	0x24
#define	ISCSI_BHS_OPCODE_SCSI_DATA_IN	0x25
#define	ISCSI_BHS_OPCODE_LOGOUT_RESPONSE	0x26
#define	ISCSI_BHS_OPCODE_R2T		0x31
#define	ISCSI_BHS_OPCODE_ASYNC_MESSAGE	0x32
#define	ISCSI_BHS_OPCODE_REJECT		0x3f

struct iscsi_bhs {
	uint8_t		bhs_opcode;
	uint8_t		bhs_opcode_specific1[3];
	uint8_t		bhs_total_ahs_len;
	uint8_t		bhs_data_segment_len[3];
	uint64_t	bhs_lun;
	uint8_t		bhs_inititator_task_tag[4];
	uint8_t		bhs_opcode_specific4[28];
};
CTASSERT(sizeof(struct iscsi_bhs) == ISCSI_BHS_SIZE);

#define	BHSSC_FLAGS_F		0x80
#define	BHSSC_FLAGS_R		0x40
#define	BHSSC_FLAGS_W		0x20
#define	BHSSC_FLAGS_ATTR	0x07

#define	BHSSC_FLAGS_ATTR_UNTAGGED	0
#define	BHSSC_FLAGS_ATTR_SIMPLE		1
#define	BHSSC_FLAGS_ATTR_ORDERED	2
#define	BHSSC_FLAGS_ATTR_HOQ		3
#define	BHSSC_FLAGS_ATTR_ACA		4

struct iscsi_bhs_scsi_command {
	uint8_t		bhssc_opcode;
	uint8_t		bhssc_flags;
	uint8_t		bhssc_reserved[2];
	uint8_t		bhssc_total_ahs_len;
	uint8_t		bhssc_data_segment_len[3];
	uint64_t	bhssc_lun;
	uint32_t	bhssc_initiator_task_tag;
	uint32_t	bhssc_expected_data_transfer_length;
	uint32_t	bhssc_cmdsn;
	uint32_t	bhssc_expstatsn;
	uint8_t		bhssc_cdb[16];
};
CTASSERT(sizeof(struct iscsi_bhs_scsi_command) == ISCSI_BHS_SIZE);

#define	BHSSR_FLAGS_RESIDUAL_UNDERFLOW		0x02
#define	BHSSR_FLAGS_RESIDUAL_OVERFLOW		0x04

#define	BHSSR_RESPONSE_COMMAND_COMPLETED	0x00

struct iscsi_bhs_scsi_response {
	uint8_t		bhssr_opcode;
	uint8_t		bhssr_flags;
	uint8_t		bhssr_response;
	uint8_t		bhssr_status;
	uint8_t		bhssr_total_ahs_len;
	uint8_t		bhssr_data_segment_len[3];
	uint16_t	bhssr_status_qualifier;
	uint16_t	bhssr_reserved;
	uint32_t	bhssr_reserved2;
	uint32_t	bhssr_initiator_task_tag;
	uint32_t	bhssr_snack_tag;
	uint32_t	bhssr_statsn;
	uint32_t	bhssr_expcmdsn;
	uint32_t	bhssr_maxcmdsn;
	uint32_t	bhssr_expdatasn;
	uint32_t	bhssr_bidirectional_read_residual_count;
	uint32_t	bhssr_residual_count;
};
CTASSERT(sizeof(struct iscsi_bhs_scsi_response) == ISCSI_BHS_SIZE);

#define	BHSTMR_FUNCTION_ABORT_TASK		1
#define	BHSTMR_FUNCTION_ABORT_TASK_SET		2
#define	BHSTMR_FUNCTION_CLEAR_ACA		3
#define	BHSTMR_FUNCTION_CLEAR_TASK_SET		4
#define	BHSTMR_FUNCTION_LOGICAL_UNIT_RESET	5
#define	BHSTMR_FUNCTION_TARGET_WARM_RESET	6
#define	BHSTMR_FUNCTION_TARGET_COLD_RESET	7
#define	BHSTMR_FUNCTION_TASK_REASSIGN		8
#define	BHSTMR_FUNCTION_QUERY_TASK		9
#define	BHSTMR_FUNCTION_QUERY_TASK_SET		10
#define	BHSTMR_FUNCTION_I_T_NEXUS_RESET		11
#define	BHSTMR_FUNCTION_QUERY_ASYNC_EVENT	12

struct iscsi_bhs_task_management_request {
	uint8_t		bhstmr_opcode;
	uint8_t		bhstmr_function;
	uint8_t		bhstmr_reserved[2];
	uint8_t		bhstmr_total_ahs_len;
	uint8_t		bhstmr_data_segment_len[3];
	uint64_t	bhstmr_lun;
	uint32_t	bhstmr_initiator_task_tag;
	uint32_t	bhstmr_referenced_task_tag;
	uint32_t	bhstmr_cmdsn;
	uint32_t	bhstmr_expstatsn;
	uint32_t	bhstmr_refcmdsn;
	uint32_t	bhstmr_expdatasn;
	uint64_t	bhstmr_reserved2;
};
CTASSERT(sizeof(struct iscsi_bhs_task_management_request) == ISCSI_BHS_SIZE);

#define	BHSTMR_RESPONSE_FUNCTION_COMPLETE	0
#define	BHSTMR_RESPONSE_TASK_DOES_NOT_EXIST	1
#define	BHSTMR_RESPONSE_LUN_DOES_NOT_EXIST	2
#define	BHSTMR_RESPONSE_TASK_STILL_ALLEGIANT	3
#define	BHSTMR_RESPONSE_TASK_ALL_REASS_NOT_SUPP	4
#define	BHSTMR_RESPONSE_FUNCTION_NOT_SUPPORTED	5
#define	BHSTMR_RESPONSE_FUNCTION_AUTH_FAIL	6
#define	BHSTMR_RESPONSE_FUNCTION_SUCCEEDED	7
#define	BHSTMR_RESPONSE_FUNCTION_REJECTED	255

struct iscsi_bhs_task_management_response {
	uint8_t		bhstmr_opcode;
	uint8_t		bhstmr_flags;
	uint8_t		bhstmr_response;
	uint8_t		bhstmr_reserved;
	uint8_t		bhstmr_total_ahs_len;
	uint8_t		bhstmr_data_segment_len[3];
	uint8_t		bhstmr_additional_reponse_information[3];
	uint8_t		bhstmr_reserved2[5];
	uint32_t	bhstmr_initiator_task_tag;
	uint32_t	bhstmr_reserved3;
	uint32_t	bhstmr_statsn;
	uint32_t	bhstmr_expcmdsn;
	uint32_t	bhstmr_maxcmdsn;
	uint8_t		bhstmr_reserved4[12];
};
CTASSERT(sizeof(struct iscsi_bhs_task_management_response) == ISCSI_BHS_SIZE);

#define	BHSLR_FLAGS_TRANSIT		0x80
#define	BHSLR_FLAGS_CONTINUE		0x40

#define	BHSLR_STAGE_SECURITY_NEGOTIATION	0
#define	BHSLR_STAGE_OPERATIONAL_NEGOTIATION	1
#define	BHSLR_STAGE_FULL_FEATURE_PHASE		3 /* Yes, 3. */

struct iscsi_bhs_login_request {
	uint8_t		bhslr_opcode;
	uint8_t		bhslr_flags;
	uint8_t		bhslr_version_max;
	uint8_t		bhslr_version_min;
	uint8_t		bhslr_total_ahs_len;
	uint8_t		bhslr_data_segment_len[3];
	uint8_t		bhslr_isid[6];
	uint16_t	bhslr_tsih;
	uint32_t	bhslr_initiator_task_tag;
	uint16_t	bhslr_cid;
	uint16_t	bhslr_reserved;
	uint32_t	bhslr_cmdsn;
	uint32_t	bhslr_expstatsn;
	uint8_t		bhslr_reserved2[16];
};
CTASSERT(sizeof(struct iscsi_bhs_login_request) == ISCSI_BHS_SIZE);

struct iscsi_bhs_login_response {
	uint8_t		bhslr_opcode;
	uint8_t		bhslr_flags;
	uint8_t		bhslr_version_max;
	uint8_t		bhslr_version_active;
	uint8_t		bhslr_total_ahs_len;
	uint8_t		bhslr_data_segment_len[3];
	uint8_t		bhslr_isid[6];
	uint16_t	bhslr_tsih;
	uint32_t	bhslr_initiator_task_tag;
	uint32_t	bhslr_reserved;
	uint32_t	bhslr_statsn;
	uint32_t	bhslr_expcmdsn;
	uint32_t	bhslr_maxcmdsn;
	uint8_t		bhslr_status_class;
	uint8_t		bhslr_status_detail;
	uint16_t	bhslr_reserved2;
	uint8_t		bhslr_reserved3[8];
};
CTASSERT(sizeof(struct iscsi_bhs_login_response) == ISCSI_BHS_SIZE);

#define	BHSTR_FLAGS_FINAL		0x80
#define	BHSTR_FLAGS_CONTINUE		0x40

struct iscsi_bhs_text_request {
	uint8_t		bhstr_opcode;
	uint8_t		bhstr_flags;
	uint16_t	bhstr_reserved;
	uint8_t		bhstr_total_ahs_len;
	uint8_t		bhstr_data_segment_len[3];
	uint64_t	bhstr_lun;
	uint32_t	bhstr_initiator_task_tag;
	uint32_t	bhstr_target_transfer_tag;
	uint32_t	bhstr_cmdsn;
	uint32_t	bhstr_expstatsn;
	uint8_t		bhstr_reserved2[16];
};
CTASSERT(sizeof(struct iscsi_bhs_text_request) == ISCSI_BHS_SIZE);

struct iscsi_bhs_text_response {
	uint8_t		bhstr_opcode;
	uint8_t		bhstr_flags;
	uint16_t	bhstr_reserved;
	uint8_t		bhstr_total_ahs_len;
	uint8_t		bhstr_data_segment_len[3];
	uint64_t	bhstr_lun;
	uint32_t	bhstr_initiator_task_tag;
	uint32_t	bhstr_target_transfer_tag;
	uint32_t	bhstr_statsn;
	uint32_t	bhstr_expcmdsn;
	uint32_t	bhstr_maxcmdsn;
	uint8_t		bhstr_reserved2[12];
};
CTASSERT(sizeof(struct iscsi_bhs_text_response) == ISCSI_BHS_SIZE);

#define	BHSDO_FLAGS_F	0x80

struct iscsi_bhs_data_out {
	uint8_t		bhsdo_opcode;
	uint8_t		bhsdo_flags;
	uint8_t		bhsdo_reserved[2];
	uint8_t		bhsdo_total_ahs_len;
	uint8_t		bhsdo_data_segment_len[3];
	uint64_t	bhsdo_lun;
	uint32_t	bhsdo_initiator_task_tag;
	uint32_t	bhsdo_target_transfer_tag;
	uint32_t	bhsdo_reserved2;
	uint32_t	bhsdo_expstatsn;
	uint32_t	bhsdo_reserved3;
	uint32_t	bhsdo_datasn;
	uint32_t	bhsdo_buffer_offset;
	uint32_t	bhsdo_reserved4;
};
CTASSERT(sizeof(struct iscsi_bhs_data_out) == ISCSI_BHS_SIZE);

#define	BHSDI_FLAGS_F	0x80
#define	BHSDI_FLAGS_A	0x40
#define	BHSDI_FLAGS_O	0x04
#define	BHSDI_FLAGS_U	0x02
#define	BHSDI_FLAGS_S	0x01

struct iscsi_bhs_data_in {
	uint8_t		bhsdi_opcode;
	uint8_t		bhsdi_flags;
	uint8_t		bhsdi_reserved;
	uint8_t		bhsdi_status;
	uint8_t		bhsdi_total_ahs_len;
	uint8_t		bhsdi_data_segment_len[3];
	uint64_t	bhsdi_lun;
	uint32_t	bhsdi_initiator_task_tag;
	uint32_t	bhsdi_target_transfer_tag;
	uint32_t	bhsdi_statsn;
	uint32_t	bhsdi_expcmdsn;
	uint32_t	bhsdi_maxcmdsn;
	uint32_t	bhsdi_datasn;
	uint32_t	bhsdi_buffer_offset;
	uint32_t	bhsdi_residual_count;
};
CTASSERT(sizeof(struct iscsi_bhs_data_in) == ISCSI_BHS_SIZE);

struct iscsi_bhs_r2t {
	uint8_t		bhsr2t_opcode;
	uint8_t		bhsr2t_flags;
	uint16_t	bhsr2t_reserved;
	uint8_t		bhsr2t_total_ahs_len;
	uint8_t		bhsr2t_data_segment_len[3];
	uint64_t	bhsr2t_lun;
	uint32_t	bhsr2t_initiator_task_tag;
	uint32_t	bhsr2t_target_transfer_tag;
	uint32_t	bhsr2t_statsn;
	uint32_t	bhsr2t_expcmdsn;
	uint32_t	bhsr2t_maxcmdsn;
	uint32_t	bhsr2t_r2tsn;
	uint32_t	bhsr2t_buffer_offset;
	uint32_t	bhsr2t_desired_data_transfer_length;
};
CTASSERT(sizeof(struct iscsi_bhs_r2t) == ISCSI_BHS_SIZE);

struct iscsi_bhs_nop_out {
	uint8_t		bhsno_opcode;
	uint8_t		bhsno_flags;
	uint16_t	bhsno_reserved;
	uint8_t		bhsno_total_ahs_len;
	uint8_t		bhsno_data_segment_len[3];
	uint64_t	bhsno_lun;
	uint32_t	bhsno_initiator_task_tag;
	uint32_t	bhsno_target_transfer_tag;
	uint32_t	bhsno_cmdsn;
	uint32_t	bhsno_expstatsn;
	uint8_t		bhsno_reserved2[16];
};
CTASSERT(sizeof(struct iscsi_bhs_nop_out) == ISCSI_BHS_SIZE);

struct iscsi_bhs_nop_in {
	uint8_t		bhsni_opcode;
	uint8_t		bhsni_flags;
	uint16_t	bhsni_reserved;
	uint8_t		bhsni_total_ahs_len;
	uint8_t		bhsni_data_segment_len[3];
	uint64_t	bhsni_lun;
	uint32_t	bhsni_initiator_task_tag;
	uint32_t	bhsni_target_transfer_tag;
	uint32_t	bhsni_statsn;
	uint32_t	bhsni_expcmdsn;
	uint32_t	bhsni_maxcmdsn;
	uint8_t		bhsno_reserved2[12];
};
CTASSERT(sizeof(struct iscsi_bhs_nop_in) == ISCSI_BHS_SIZE);

#define	BHSLR_REASON_CLOSE_SESSION		0
#define	BHSLR_REASON_CLOSE_CONNECTION		1
#define	BHSLR_REASON_REMOVE_FOR_RECOVERY	2

struct iscsi_bhs_logout_request {
	uint8_t		bhslr_opcode;
	uint8_t		bhslr_reason;
	uint16_t	bhslr_reserved;
	uint8_t		bhslr_total_ahs_len;
	uint8_t		bhslr_data_segment_len[3];
	uint64_t	bhslr_reserved2;
	uint32_t	bhslr_initiator_task_tag;
	uint16_t	bhslr_cid;
	uint16_t	bhslr_reserved3;
	uint32_t	bhslr_cmdsn;
	uint32_t	bhslr_expstatsn;
	uint8_t		bhslr_reserved4[16];
};
CTASSERT(sizeof(struct iscsi_bhs_logout_request) == ISCSI_BHS_SIZE);

#define	BHSLR_RESPONSE_CLOSED_SUCCESSFULLY	0
#define	BHSLR_RESPONSE_RECOVERY_NOT_SUPPORTED	2

struct iscsi_bhs_logout_response {
	uint8_t		bhslr_opcode;
	uint8_t		bhslr_flags;
	uint8_t		bhslr_response;
	uint8_t		bhslr_reserved;
	uint8_t		bhslr_total_ahs_len;
	uint8_t		bhslr_data_segment_len[3];
	uint64_t	bhslr_reserved2;
	uint32_t	bhslr_initiator_task_tag;
	uint32_t	bhslr_reserved3;
	uint32_t	bhslr_statsn;
	uint32_t	bhslr_expcmdsn;
	uint32_t	bhslr_maxcmdsn;
	uint32_t	bhslr_reserved4;
	uint16_t	bhslr_time2wait;
	uint16_t	bhslr_time2retain;
	uint32_t	bhslr_reserved5;
};
CTASSERT(sizeof(struct iscsi_bhs_logout_response) == ISCSI_BHS_SIZE);

#define	BHSAM_EVENT_TARGET_REQUESTS_LOGOUT		1
#define	BHSAM_EVENT_TARGET_TERMINATES_CONNECTION	2
#define	BHSAM_EVENT_TARGET_TERMINATES_SESSION		3

struct iscsi_bhs_asynchronous_message {
	uint8_t		bhsam_opcode;
	uint8_t		bhsam_flags;
	uint16_t	bhsam_reserved;
	uint8_t		bhsam_total_ahs_len;
	uint8_t		bhsam_data_segment_len[3];
	uint64_t	bhsam_lun;
	uint32_t	bhsam_0xffffffff;
	uint32_t	bhsam_reserved2;
	uint32_t	bhsam_statsn;
	uint32_t	bhsam_expcmdsn;
	uint32_t	bhsam_maxcmdsn;
	uint8_t		bhsam_async_event;
	uint8_t		bhsam_async_vcode;
	uint16_t	bhsam_parameter1;
	uint16_t	bhsam_parameter2;
	uint16_t	bhsam_parameter3;
	uint32_t	bhsam_reserved3;
};
CTASSERT(sizeof(struct iscsi_bhs_asynchronous_message) == ISCSI_BHS_SIZE);

#define BHSSR_REASON_DATA_DIGEST_ERROR	0x02
#define BHSSR_PROTOCOL_ERROR		0x04
#define BHSSR_COMMAND_NOT_SUPPORTED	0x05
#define BHSSR_INVALID_PDU_FIELD		0x09

struct iscsi_bhs_reject {
	uint8_t		bhsr_opcode;
	uint8_t		bhsr_flags;
	uint8_t		bhsr_reason;
	uint8_t		bhsr_reserved;
	uint8_t		bhsr_total_ahs_len;
	uint8_t		bhsr_data_segment_len[3];
	uint64_t	bhsr_reserved2;
	uint32_t	bhsr_0xffffffff;
	uint32_t	bhsr_reserved3;
	uint32_t	bhsr_statsn;
	uint32_t	bhsr_expcmdsn;
	uint32_t	bhsr_maxcmdsn;
	uint32_t	bhsr_datasn_r2tsn;
	uint32_t	bhsr_reserved4;
	uint32_t	bhsr_reserved5;
};
CTASSERT(sizeof(struct iscsi_bhs_reject) == ISCSI_BHS_SIZE);

#endif /* !ISCSI_PROTO_H */
