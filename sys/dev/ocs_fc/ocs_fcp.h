/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * Define Fibre Channel types and structures.
 */

#ifndef _OCS_FCP_H
#define _OCS_FCP_H

#define FC_ELS_CMD_RJT		0x01
#define FC_ELS_CMD_ACC		0x02
#define FC_ELS_CMD_PLOGI	0x03
#define FC_ELS_CMD_FLOGI	0x04
#define FC_ELS_CMD_LOGO		0x05
#define FC_ELS_CMD_RRQ		0x12
#define FC_ELS_CMD_PRLI		0x20
#define FC_ELS_CMD_PRLO		0x21
#define FC_ELS_CMD_PDISC	0x50
#define FC_ELS_CMD_FDISC	0x51
#define FC_ELS_CMD_ADISC	0x52
#define FC_ELS_CMD_RSCN		0x61
#define FC_ELS_CMD_SCR		0x62

#define FC_TYPE_BASIC_LINK	0
#define FC_TYPE_FCP		0x08
#define FC_TYPE_GS		0x20
#define FC_TYPE_SW		0x22

#define FC_ADDR_FABRIC			0xfffffe	/** well known fabric address */
#define FC_ADDR_CONTROLLER              0xfffffd        /** well known fabric controller address */
#define FC_ADDR_IS_DOMAIN_CTRL(x)	(((x) & 0xffff00) == 0xfffc00)	/** is well known domain controller */
#define FC_ADDR_GET_DOMAIN_CTRL(x)	((x) & 0x0000ff)	/** get domain controller number */
#define FC_ADDR_NAMESERVER              0xfffffc        /** well known directory server address */

#define FC_GS_TYPE_ALIAS_SERVICE		0xf8
#define FC_GS_TYPE_MANAGEMENT_SERVICE		0xfa
#define FC_GS_TYPE_DIRECTORY_SERVICE		0xfc

#define FC_GS_SUBTYPE_NAME_SERVER		0x02

/**
 * Generic Services FC Type Bit mask macros:
 */
#define FC_GS_TYPE_WORD(type)	((type) >> 5)
#define FC_GS_TYPE_BIT(type)	((type) & 0x1f)

/**
 * Generic Services Name Server Request Command codes:
 */
#define FC_GS_NAMESERVER_GPN_ID		0x0112
#define FC_GS_NAMESERVER_GNN_ID		0x0113
#define FC_GS_NAMESERVER_GFPN_ID	0x011c
#define FC_GS_NAMESERVER_GFF_ID		0x011f
#define FC_GS_NAMESERVER_GID_FT		0x0171
#define FC_GS_NAMESERVER_GID_PT		0x01a1
#define FC_GS_NAMESERVER_RHBA		0x0200
#define FC_GS_NAMESERVER_RPA		0x0211
#define FC_GS_NAMESERVER_RPN_ID		0x0212
#define FC_GS_NAMESERVER_RNN_ID		0x0213
#define FC_GS_NAMESERVER_RCS_ID		0x0214
#define FC_GS_NAMESERVER_RFT_ID		0x0217
#define FC_GS_NAMESERVER_RFF_ID		0x021f
#define FC_GS_NAMESERVER_RSNN_NN	0x0239
#define FC_GS_NAMESERVER_RSPN_ID	0x0218


#define FC_GS_REVISION		0x03

#define FC_GS_IO_PARAMS		{ .fc_ct.r_ctl = 0x02, \
				.fc_ct.type = FC_TYPE_GS, \
				.fc_ct.df_ctl = 0x00 }

typedef struct fc_vft_header_s {
	uint32_t	:1,
			vf_id:12,
			priority:3,
			e:1,
			:1,
			type:4,
			ver:2,
			r_ctl:8;
	uint32_t	:24,
			hopct:8;
} fc_vft_header_t;


#if BYTE_ORDER == LITTLE_ENDIAN
static inline uint32_t fc_be24toh(uint32_t x) { return (ocs_be32toh(x) >> 8); }
#else
static inline uint32_t fc_be24toh(uint32_t x) { }
#endif
static inline uint32_t fc_htobe24(uint32_t x) { return fc_be24toh(x); }

#define FC_SOFI3	0x2e
#define FC_SOFn3	0x36
#define FC_EOFN		0x41
#define FC_EOFT		0x42

/**
 * @brief FC header in big-endian order
 */
typedef struct fc_header_s {
	uint32_t	info:4,
			r_ctl:4,
			d_id:24;
	uint32_t	cs_ctl:8,
			s_id:24;
	uint32_t	type:8,
			f_ctl:24;
	uint32_t	seq_id:8,
			df_ctl:8,
			seq_cnt:16;
	uint32_t	ox_id:16,
			rx_id:16;
	uint32_t	parameter;
} fc_header_t;


/**
 * @brief FC header in little-endian order
 */
typedef struct fc_header_le_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	d_id:24,
			info:4,
			r_ctl:4;
	uint32_t	s_id:24,
			cs_ctl:8;
	uint32_t	f_ctl:24,
			type:8;
	uint32_t	seq_cnt:16,
			df_ctl:8,
			seq_id:8;
	uint32_t	rx_id:16,
			ox_id:16;
	uint32_t	parameter;
#else
#error big endian version not defined
#endif
} fc_header_le_t;

/**
 * @brief FC VM header in big-endian order
 */
typedef struct fc_vm_header_s {
	uint32_t	dst_vmid;
	uint32_t	src_vmid;
	uint32_t	rsvd0;
	uint32_t	rsvd1;
} fc_vm_header_t;

#define FC_DFCTL_DEVICE_HDR_16_MASK	0x1
#define FC_DFCTL_NETWORK_HDR_MASK	0x20	
#define FC_DFCTL_ESP_HDR_MASK		0x40				
#define FC_DFCTL_NETWORK_HDR_SIZE	16	
#define FC_DFCTL_ESP_HDR_SIZE		0 //FIXME				

#define FC_RCTL_FC4_DATA	0
#define FC_RCTL_ELS		2
#define FC_RCTL_BLS		8

#define FC_RCTL_INFO_UNCAT	0
#define FC_RCTL_INFO_SOL_DATA	1
#define FC_RCTL_INFO_UNSOL_CTRL	2
#define FC_RCTL_INFO_SOL_CTRL	3
#define FC_RCTL_INFO_UNSOL_DATA	4
#define FC_RCTL_INFO_DATA_DESC	5
#define FC_RCTL_INFO_UNSOL_CMD	6
#define FC_RCTL_INFO_CMD_STATUS	7

#define FC_FCTL_EXCHANGE_RESPONDER	0x800000
#define FC_FCTL_SEQUENCE_CONTEXT	0x400000
#define FC_FCTL_FIRST_SEQUENCE		0x200000
#define FC_FCTL_LAST_SEQUENCE		0x100000
#define FC_FCTL_END_SEQUENCE		0x080000
#define FC_FCTL_END_CONNECTION		0x040000
#define FC_FCTL_PRIORITY_ENABLE		0x020000
#define FC_FCTL_SEQUENCE_INITIATIVE	0x010000
#define FC_FCTL_FILL_DATA_BYTES_MASK	0x000003

/**
 * Common BLS definitions:
 */
#define FC_INFO_NOP			0x0
#define FC_INFO_ABTS			0x1
#define FC_INFO_RMC			0x2
/* reserved				0x3 */
#define FC_INFO_BA_ACC			0x4
#define FC_INFO_BA_RJT			0x5
#define FC_INFO_PRMT			0x6

/* (FC-LS) LS_RJT Reason Codes */
#define FC_REASON_INVALID_COMMAND_CODE		0x01
#define FC_REASON_LOGICAL_ERROR			0x03
#define FC_REASON_LOGICAL_BUSY			0x05
#define FC_REASON_PROTOCOL_ERROR		0x07
#define FC_REASON_UNABLE_TO_PERFORM		0x09
#define FC_REASON_COMMAND_NOT_SUPPORTED		0x0b
#define FC_REASON_COMMAND_IN_PROGRESS   	0x0e
#define FC_REASON_VENDOR_SPECIFIC		0xff

/* (FC-LS) LS_RJT Reason Codes Explanations */
#define FC_EXPL_NO_ADDITIONAL			0x00
#define FC_EXPL_SPARAM_OPTIONS			0x01
#define FC_EXPL_SPARAM_INITIATOR		0x03
#define FC_EXPL_SPARAM_RECPIENT			0x05
#define FC_EXPL_SPARM_DATA_SIZE			0x07
#define FC_EXPL_SPARM_CONCURRENT		0x09
#define FC_EXPL_SPARM_CREDIT			0x0b
#define FC_EXPL_INV_PORT_NAME 			0x0d
#define FC_EXPL_INV_NODE_NAME 			0x0e
#define FC_EXPL_INV_COMMON_SPARAMS 		0x0f
#define FC_EXPL_INV_ASSOC_HEADER		0x11
#define FC_EXPL_ASSOC_HDR_REQUIRED		0x13
#define FC_EXPL_INV_ORIGINATOR_S_ID		0x15
#define FC_EXPL_INV_X_ID_COMBINATION		0x17
#define FC_EXPL_COMMAND_IN_PROGRESS		0x19
#define FC_EXPL_NPORT_LOGIN_REQUIRED		0x1e
#define FC_EXPL_N_PORT_ID			0x1f
#define FC_EXPL_INSUFFICIENT_RESOURCES		0x29
#define FC_EXPL_UNABLE_TO_SUPPLY_DATA		0x2a
#define FC_EXPL_REQUEST_NOT_SUPPORTED		0x2c
#define FC_EXPL_INV_PAYLOAD_LEN			0x1d
#define FC_EXPL_INV_PORT_NODE_NAME		0x44
#define FC_EXPL_LOGIN_EXT_NOT_SUPPORTED		0x46
#define FC_EXPL_AUTH_REQUIRED			0x48
#define FC_EXPL_SCAN_VALUE_NOT_ALLOWED		0x50
#define FC_EXPL_SCAN_VALUE_NOT_SUPPORTED 	0x51
#define FC_EXPL_NO_RESOURCES_ASSIGNED		0x52
#define FC_EXPL_MAC_ADDR_MODE_NOT_SUPPORTED	0x60
#define FC_EXPL_MAC_ADDR_INCORRECTLY_FORMED	0x61
#define FC_EXPL_VN2VN_PORT_NOT_IN_NEIGHBOR_SET	0x62

#define FC_EXPL_INV_X_ID			0x03	/* invalid OX_ID - RX_ID combination */
#define FC_EXPL_SEQUENCE_ABORTED		0x05

typedef struct fc_ba_acc_payload_s {
#define FC_SEQ_ID_VALID			0x80
#define FC_SEQ_ID_INVALID		0x00
	uint32_t	seq_id_validity:8,
			seq_id:8,
			:16;
	uint32_t	ox_id:16,
			rx_id:16;
	uint32_t	low_seq_cnt:16,
			high_seq_cnt:16;
} fc_ba_acc_payload_t;

typedef struct fc_ba_rjt_payload_s {
	uint32_t	vendor_unique:8,
			reason_explanation:8,
			reason_code:8,
			:8;
} fc_ba_rjt_payload_t;

typedef struct fc_els_gen_s {
	uint32_t	command_code: 8,
			resv1: 24;
} fc_els_gen_t;

typedef struct fc_plogi_playload_s {
	uint32_t	command_code: 8,
			resv1: 24;
	uint32_t	common_service_parameters[4];
	uint32_t	port_name_hi;
	uint32_t	port_name_lo;
	uint32_t	node_name_hi;
	uint32_t	node_name_lo;
	uint32_t	class1_service_parameters[4];
	uint32_t	class2_service_parameters[4];
	uint32_t	class3_service_parameters[4];
	uint32_t	class4_service_parameters[4];
	uint32_t	vendor_version_level[4];
} fc_plogi_payload_t;

typedef fc_plogi_payload_t fc_sparms_t;

typedef struct fc_logo_payload_s {
	uint32_t	command_code: 8,
			resv1:24;
	uint32_t	:8,
			port_id:24;
	uint32_t	port_name_hi;
	uint32_t	port_name_lo;
} fc_logo_payload_t;

typedef struct fc_acc_payload_s {
	uint32_t	command_code: 8,
			resv1:24;
} fc_acc_payload_t;


typedef struct fc_ls_rjt_payload_s {
	uint32_t	command_code:8,
			resv1:24;
	uint32_t	resv2:8,
			reason_code:8,
			reason_code_exp:8,
			vendor_unique:8;
} fc_ls_rjt_payload_t;

typedef struct fc_prli_payload_s {
	uint32_t	command_code:8,
			page_length:8,
			payload_length:16;
	uint32_t	type:8,
			type_ext:8,
			flags:16;
	uint32_t	originator_pa;
	uint32_t	responder_pa;
	uint32_t	:16,
			service_params:16;
} fc_prli_payload_t;

typedef struct fc_prlo_payload_s {
	uint32_t	command_code:8,
			page_length:8,
			payload_length:16;
	uint32_t	type:8,
			type_ext:8,
			:16;
	uint32_t	:32;
	uint32_t	:32;
	uint32_t	:32;
} fc_prlo_payload_t;

typedef struct fc_prlo_acc_payload_s {
	uint32_t	command_code:8,
			page_length:8,
			payload_length:16;
	uint32_t	type:8,
			type_ext:8,
			:4,
			response_code:4,
			:8;
	uint32_t	:32;
	uint32_t	:32;
	uint32_t	:32;
} fc_prlo_acc_payload_t;

typedef struct fc_adisc_payload_s {
	uint32_t	command_code:8,
			payload_length:24;
	uint32_t	:8,
			hard_address:24;
	uint32_t	port_name_hi;
	uint32_t	port_name_lo;
	uint32_t	node_name_hi;
	uint32_t	node_name_lo;
	uint32_t	:8,
			port_id:24;
} fc_adisc_payload_t;

/* PRLI flags */
#define FC_PRLI_ORIGINATOR_PA_VALID	0x8000
#define FC_PRLI_RESPONDER_PA_VALID	0x4000
#define FC_PRLI_ESTABLISH_IMAGE_PAIR	0x2000
#define FC_PRLI_SERVICE_PARAM_INVALID	0x0800
#define FC_PRLI_REQUEST_EXECUTED	0x0100

/* PRLI Service Parameters */
#define FC_PRLI_REC_SUPPORT		0x0400
#define FC_PRLI_TASK_RETRY_ID_REQ	0x0200
#define FC_PRLI_RETRY			0x0100
#define FC_PRLI_CONFIRMED_COMPLETION	0x0080
#define FC_PRLI_DATA_OVERLAY		0x0040
#define FC_PRLI_INITIATOR_FUNCTION      0x0020
#define FC_PRLI_TARGET_FUNCTION         0x0010
#define FC_PRLI_READ_XRDY_DISABLED	0x0002
#define FC_PRLI_WRITE_XRDY_DISABLED	0x0001

/* PRLO Logout flags */
#define FC_PRLO_REQUEST_EXECUTED	0x0001

typedef struct fc_scr_payload_s {
	uint32_t	command_code:8,
			:24;
	uint32_t	:24,
			function:8;
} fc_scr_payload_t;

#define FC_SCR_REG_FABRIC		1
#define FC_SCR_REG_NPORT		2
#define FC_SCR_REG_FULL			3

typedef struct {
	uint32_t :2,
		rscn_event_qualifier:4,
		address_format:2,
		port_id:24;
} fc_rscn_affected_port_id_page_t;

typedef struct fc_rscn_payload_s {
	uint32_t	command_code:8,
			page_length:8,
			payload_length:16;
	fc_rscn_affected_port_id_page_t port_list[1];
} fc_rscn_payload_t;

typedef struct fcct_iu_header_s {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	revision:8,
			in_id:24;
	uint32_t	gs_type:8,
			gs_subtype:8,
			options:8,
			resv1:8;
	uint32_t	cmd_rsp_code:16,
			max_residual_size:16;
	uint32_t	fragment_id:8,
			reason_code:8,
			reason_code_explanation:8,
			vendor_specific:8;
#else
#error big endian version not defined
#endif
} fcct_iu_header_t;

#define FCCT_REJECT_INVALID_COMMAND_CODE	1
#define FCCT_REJECT_INVALID_VERSION_LEVEL	2
#define FCCT_LOGICAL_ERROR			3
#define FCCT_INVALID_CT_IU_SIZE			4
#define FCCT_LOGICAL_BUSY			5
#define FCCT_PROTOCOL_ERROR			7
#define FCCT_UNABLE_TO_PERFORM			9
#define FCCT_COMMAND_NOT_SUPPORTED		0x0b
#define FCCT_FABRIC_PORT_NAME_NOT_REGISTERED	0x0c
#define FCCT_SERVER_NOT_AVAILABLE		0x0d
#define FCCT_SESSION_COULD_NOT_BE_ESTABLISHED	0x0e
#define FCCT_VENDOR_SPECIFIC_ERROR		0xff

#define FCCT_NO_ADDITIONAL_EXPLANATION		0
#define FCCT_AUTHORIZATION_EXCEPTION		0xf0
#define FCCT_AUTHENTICATION_EXCEPTION		0xf1
#define FCCT_DATA_BASE_FULL			0xf2
#define FCCT_DATA_BASE_EMPTY			0xf3
#define FCCT_PROCESSING_REQUEST			0xf4
#define FCCT_UNABLE_TO_VERIFY_CONNECTION	0xf5
#define FCCT_DEVICES_NOT_IN_COMMON_ZONE		0xf6

typedef struct {
	fcct_iu_header_t hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	port_id;
	uint32_t	fc4_types;
#else
#error big endian version not defined
#endif
} fcgs_rft_id_t;

typedef struct {
	fcct_iu_header_t hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	port_id;
	uint32_t	:16,
			fc4_features:8,
			type_code:8;
#else
#error big endian version not defined
#endif
} fcgs_rff_id_t;

#pragma pack(1)
typedef struct {
	fcct_iu_header_t hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	port_id;
	uint64_t	port_name;
#else
#error big endian version not defined
#endif
} fcgs_rpn_id_t;
#pragma pack()

#pragma pack(1)
typedef struct {
	fcct_iu_header_t hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	port_id;
	uint64_t	node_name;
#else
#error big endian version not defined
#endif
} fcgs_rnn_id_t;
#pragma pack()

#define FCCT_CLASS_OF_SERVICE_F	0x1
#define FCCT_CLASS_OF_SERVICE_2	0x4
#define FCCT_CLASS_OF_SERVICE_3	0x8
#pragma pack(1)
typedef struct {
	fcct_iu_header_t hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint32_t	port_id;
	uint32_t	class_of_srvc;
#else
#error big endian version not defined
#endif
} fcgs_rcs_id_t;
#pragma pack()

#pragma pack(1)
typedef struct {
	fcct_iu_header_t hdr;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint64_t	node_name;
	uint8_t		name_len;
	char		sym_node_name[1];
/*TODO: need name length and symbolic name */
#else
#error big endian version not defined
#endif
} fcgs_rsnn_nn_t;
#pragma pack()

#define FCCT_HDR_CMDRSP_ACCEPT	0x8002
#define FCCT_HDR_CMDRSP_REJECT	0x8001

static inline void fcct_build_req_header(fcct_iu_header_t *hdr, uint16_t cmd, uint16_t max_size)
{
	/* use old rev (1) to accommodate older switches */
	hdr->revision = 1;
	hdr->in_id = 0;
	hdr->gs_type = FC_GS_TYPE_DIRECTORY_SERVICE;
	hdr->gs_subtype = FC_GS_SUBTYPE_NAME_SERVER;
	hdr->options = 0;
	hdr->resv1 = 0;
	hdr->cmd_rsp_code = ocs_htobe16(cmd);
	hdr->max_residual_size = ocs_htobe16(max_size/(sizeof(uint32_t))); /* words */
	hdr->fragment_id = 0;
	hdr->reason_code = 0;
	hdr->reason_code_explanation = 0;
	hdr->vendor_specific = 0;
}

typedef struct fcct_rftid_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		port_id;
	uint32_t		fc4_types[8];
} fcct_rftid_req_t;

#define FC4_FEATURE_TARGET	(1U << 0)
#define FC4_FEATURE_INITIATOR	(1U << 1)

typedef struct fcct_rffid_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		port_id;
	uint32_t		:16,
				fc4_feature_bits:8,
				type:8;
} fcct_rffid_req_t;

typedef struct fcct_gnnid_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		:8,
				port_id:24;
} fcct_gnnid_req_t;

typedef struct fcct_gpnid_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		:8,
				port_id:24;
} fcct_gpnid_req_t;

typedef struct fcct_gffid_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		:8,
				port_id:24;
} fcct_gffid_req_t;

typedef struct fcct_gidft_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		:8,
				domain_id_scope:8,
				area_id_scope:8,
				type:8;
} fcct_gidft_req_t;

typedef struct fcct_gidpt_req_s {
	fcct_iu_header_t	hdr;
	uint32_t		port_type:8,
				domain_id_scope:8,
				area_id_scope:8,
				flags:8;
} fcct_gidpt_req_t;

typedef struct fcct_gnnid_acc_s {
	fcct_iu_header_t	hdr;
	uint64_t		node_name;
} fcct_gnnid_acc_t;

typedef struct fcct_gpnid_acc_s {
	fcct_iu_header_t	hdr;
	uint64_t		port_name;
} fcct_gpnid_acc_t;

typedef struct fcct_gffid_acc_s {
	fcct_iu_header_t	hdr;
	uint8_t			fc4_feature_bits;
} fcct_gffid_acc_t;

typedef struct fcct_gidft_acc_s {
	fcct_iu_header_t	hdr;
	struct {
		uint32_t	ctl:8,
				port_id:24;
	} port_list[1];
} fcct_gidft_acc_t;

typedef struct fcct_gidpt_acc_s {
	fcct_iu_header_t	hdr;
	struct {
		uint32_t	ctl:8,
				port_id:24;
	} port_list[1];
} fcct_gidpt_acc_t;

#define FCCT_GID_PT_LAST_ID	0x80
#define FCCT_GIDPT_ID_MASK	0x00ffffff

typedef struct fcp_cmnd_iu_s {
	uint8_t		fcp_lun[8];
	uint8_t		command_reference_number;
	uint8_t		task_attribute:3,
			command_priority:4,
			:1;
	uint8_t		task_management_flags;
	uint8_t		wrdata:1,
			rddata:1,
			additional_fcp_cdb_length:6;
	uint8_t		fcp_cdb[16];
	uint8_t		fcp_cdb_and_dl[20];	/* < May contain up to 16 bytes of CDB, followed by fcp_dl */
} fcp_cmnd_iu_t;

#define FCP_LUN_ADDRESS_METHOD_SHIFT	6
#define FCP_LUN_ADDRESS_METHOD_MASK	0xc0
#define FCP_LUN_ADDR_METHOD_PERIPHERAL	0x0
#define FCP_LUN_ADDR_METHOD_FLAT	0x1
#define FCP_LUN_ADDR_METHOD_LOGICAL	0x2
#define FCP_LUN_ADDR_METHOD_EXTENDED	0x3

#define FCP_LUN_ADDR_SIMPLE_MAX		0xff
#define FCP_LUN_ADDR_FLAT_MAX		0x3fff

#define FCP_TASK_ATTR_SIMPLE		0x0
#define FCP_TASK_ATTR_HEAD_OF_QUEUE	0x1
#define FCP_TASK_ATTR_ORDERED		0x2
#define FCP_TASK_ATTR_ACA		0x4
#define FCP_TASK_ATTR_UNTAGGED          0x5

#define FCP_QUERY_TASK_SET		BIT(0)
#define FCP_ABORT_TASK_SET		BIT(1)
#define FCP_CLEAR_TASK_SET		BIT(2)
#define FCP_QUERY_ASYNCHRONOUS_EVENT	BIT(3)
#define FCP_LOGICAL_UNIT_RESET		BIT(4)
#define FCP_TARGET_RESET		BIT(5)
#define FCP_CLEAR_ACA			BIT(6)

/* SPC-4 says that the maximum length of sense data is 252 bytes */
#define FCP_MAX_SENSE_LEN		252
#define FCP_MAX_RSP_LEN			  8
/*
 * FCP_RSP buffer will either have sense or response data, but not both
 * so pick the larger.
 */
#define FCP_MAX_RSP_INFO_LEN		FCP_MAX_SENSE_LEN

typedef struct fcp_rsp_iu_s {
	uint8_t		rsvd[8];
	uint8_t		status_qualifier[2];
	uint8_t		flags;
	uint8_t		scsi_status;
	uint8_t		fcp_resid[4];
	uint8_t		fcp_sns_len[4];
	uint8_t		fcp_rsp_len[4];
	uint8_t		data[FCP_MAX_RSP_INFO_LEN];
} fcp_rsp_iu_t;

/** Flag field defines: */
#define FCP_RSP_LEN_VALID		BIT(0)
#define FCP_SNS_LEN_VALID		BIT(1)
#define FCP_RESID_OVER			BIT(2)
#define FCP_RESID_UNDER			BIT(3)
#define FCP_CONF_REQ			BIT(4)
#define FCP_BIDI_READ_RESID_OVER	BIT(5)
#define FCP_BIDI_READ_RESID_UNDER	BIT(6)
#define FCP_BIDI_RSP			BIT(7)

/** Status values: */
#define FCP_TMF_COMPLETE		0x00
#define FCP_DATA_LENGTH_MISMATCH	0x01
#define FCP_INVALID_FIELD		0x02
#define FCP_DATA_RO_MISMATCH		0x03
#define FCP_TMF_REJECTED		0x04
#define FCP_TMF_FAILED			0x05
#define FCP_TMF_SUCCEEDED		0x08
#define FCP_TMF_INCORRECT_LUN		0x09

/** FCP-4 Table 28, TMF response information: */
typedef struct fc_rsp_info_s {
	uint8_t addl_rsp_info[3];
	uint8_t rsp_code;
	uint32_t :32;
} fcp_rsp_info_t;

typedef struct fcp_xfer_rdy_iu_s {
	uint8_t		fcp_data_ro[4];
	uint8_t		fcp_burst_len[4];
	uint8_t		rsvd[4];
} fcp_xfer_rdy_iu_t;

#define MAX_ACC_REJECT_PAYLOAD (sizeof(fc_ls_rjt_payload_t) > sizeof(fc_acc_payload_t) ? sizeof(fc_ls_rjt_payload_t) : sizeof(fc_acc_payload_t))


#endif /* !_OCS_FCP_H */
