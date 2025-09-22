/*	$OpenBSD: qlareg.h,v 1.9 2017/06/05 04:57:37 dlg Exp $ */

/*
 * Copyright (c) 2013, 2014 Jonathan Matthew <jmatthew@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

/* firmware loading */
#define QLA_2100_CODE_ORG		0x1000
#define QLA_2200_CODE_ORG		0x1000
#define QLA_2300_CODE_ORG		0x0800

/* firmware attributes */
#define QLA_FW_ATTR_EXPANDED_LUN	0x0002
#define QLA_FW_ATTR_FABRIC		0x0004
#define QLA_FW_ATTR_2K_LOGINS		0x0100

/* interrupt types */
#define QLA_INT_TYPE_MBOX		1
#define QLA_INT_TYPE_ASYNC		2
#define QLA_INT_TYPE_IO			3
#define QLA_INT_TYPE_OTHER		4

/* 23xx interrupt status codes */
#define QLA_23XX_INT_ROM_MBOX		0x01
#define QLA_23XX_INT_ROM_MBOX_FAIL	0x02
#define QLA_23XX_INT_MBOX		0x10
#define QLA_23XX_INT_MBOX_FAIL		0x11
#define QLA_23XX_INT_ASYNC		0x12
#define QLA_23XX_INT_RSPQ		0x13
#define QLA_23XX_INT_FP16		0x15
#define QLA_23XX_INT_FP_SCSI		0x16
#define QLA_23XX_INT_FP_CTIO		0x17

/* ISP registers */
#define QLA_FLASH_BIOS_ADDR		0x00
#define QLA_FLASH_BIOS_DATA		0x02
#define QLA_CTRL_STATUS			0x06
#define QLA_INT_CTRL			0x08
#define QLA_INT_STATUS			0x0A
#define QLA_SEMA			0x0C
#define QLA_NVRAM			0x0E
#define QLA_REQ_IN			0x10
#define QLA_REQ_OUT			0x12
#define QLA_RESP_IN			0x14
#define QLA_RESP_OUT			0x16
#define QLA_RISC_STATUS_LOW		0x18
#define QLA_RISC_STATUS_HIGH		0x1A
#define QLA_HOST_CMD_CTRL		0xC0
#define QLA_GPIO_DATA			0xCC
#define QLA_GPIO_ENABLE			0xCE

#define QLA_FPM_DIAG			0x96


/* mailbox base moves around between generations */
#define QLA_MBOX_BASE_23XX		0x40
#define QLA_MBOX_BASE_2100		0x10
#define QLA_MBOX_BASE_2200		0x10

/* QLA_CTRL_STATUS */
#define QLA_CTRL_RESET			0x0001
#define QLA_CTRL_RISC_REGS		0x0000
#define QLA_CTRL_FB_REGS		0x0010
#define QLA_CTRL_FPM0_REGS		0x0020
#define QLA_CTRL_FPM1_REGS		0x0030

/* QLA_INT_STATUS */
#define QLA_INT_REQ			0x8000
#define QLA_RISC_INT_REQ		0x0008

/* QLA_SEMA */
#define QLA_SEMA_STATUS			0x0002
#define QLA_SEMA_LOCK			0x0001

/* QLA_NVRAM */
#define QLA_NVRAM_DATA_IN		0x0008
#define QLA_NVRAM_DATA_OUT		0x0004
#define QLA_NVRAM_CHIP_SEL		0x0002
#define QLA_NVRAM_CLOCK			0x0001
#define QLA_NVRAM_CMD_READ		6


/* QLA_RISC_STATUS LOW/HIGH */
#define QLA_INT_INFO_SHIFT		16
#define QLA_RISC_HOST_INT_REQ		0x8000
#define QLA_RISC_PAUSED			0x0100
#define QLA_INT_STATUS_MASK		0x00FF

/* QLA_HOST_CMD_CTRL write */
#define QLA_HOST_CMD_SHIFT		12
#define QLA_HOST_CMD_NOP		0x0
#define QLA_HOST_CMD_RESET		0x1
#define QLA_HOST_CMD_PAUSE		0x2
#define QLA_HOST_CMD_RELEASE		0x3
#define QLA_HOST_CMD_MASK_PARITY	0x4
#define QLA_HOST_CMD_SET_HOST_INT	0x5
#define QLA_HOST_CMD_CLR_HOST_INT	0x6
#define QLA_HOST_CMD_CLR_RISC_INT	0x7
#define QLA_HOST_CMD_ENABLE_PARITY	0xA
#define QLA_HOST_CMD_PARITY_ERROR	0xE

/* QLA_HOST_CMD_CTRL read */
#define QLA_HOST_STATUS_HOST_INT	0x0080
#define QLA_HOST_STATUS_RISC_RESET	0x0040
#define QLA_HOST_STATUS_RISC_PAUSE	0x0020
#define QLA_HOST_STATUS_RISC_EXT	0x0010

/* QLA_FPM_DIAG */
#define QLA_FPM_RESET			0x0100

/* QLA_MBOX_BASE (reg 0) read */
#define QLA_MBOX_HAS_STATUS		0x4000
#define QLA_MBOX_COMPLETE		0x4000
#define QLA_MBOX_INVALID		0x4001
#define QLA_MBOX_INTF_ERROR		0x4002
#define QLA_MBOX_TEST_FAILED		0x4003
#define QLA_MBOX_CMD_ERROR		0x4005
#define QLA_MBOX_CMD_PARAM		0x4006
#define QLA_MBOX_PORT_USED		0x4007
#define QLA_MBOX_LOOP_USED		0x4008
#define QLA_MBOX_ALL_IDS_USED		0x4009
#define QLA_MBOX_NOT_LOGGED_IN		0x400A
#define QLA_MBOX_LINK_DOWN		0x400B
#define QLA_ASYNC_SYSTEM_ERROR		0x8002
#define QLA_ASYNC_REQ_XFER_ERROR	0x8003
#define QLA_ASYNC_RSP_XFER_ERROR	0x8004
#define QLA_ASYNC_LIP_OCCURRED		0x8010
#define QLA_ASYNC_LOOP_UP		0x8011
#define QLA_ASYNC_LOOP_DOWN		0x8012
#define QLA_ASYNC_LIP_RESET		0x8013
#define QLA_ASYNC_PORT_DB_CHANGE	0x8014
#define QLA_ASYNC_CHANGE_NOTIFY		0x8015
#define QLA_ASYNC_LIP_F8		0x8016
#define QLA_ASYNC_LOOP_INIT_ERROR	0x8017
#define QLA_ASYNC_LOGIN_REJECT		0x8018
#define QLA_ASYNC_SCSI_CMD_COMPLETE	0x8020
#define QLA_ASYNC_CTIO_COMPLETE		0x8021
#define QLA_ASYNC_POINT_TO_POINT	0x8030
#define QLA_ASYNC_ZIO_RESP_UPDATE	0x8040
#define QLA_ASYNC_RND_ERROR		0x8048
#define QLA_ASYNC_QUEUE_FULL		0x8049


/* QLA_MBOX_BASE (reg 0) write */
#define QLA_MBOX_NOP			0x0000
#define QLA_MBOX_LOAD_RAM		0x0001
#define QLA_MBOX_EXEC_FIRMWARE		0x0002
#define QLA_MBOX_WRITE_RAM_WORD		0x0004
#define QLA_MBOX_REGISTER_TEST		0x0006
#define QLA_MBOX_VERIFY_CSUM		0x0007
#define QLA_MBOX_ABOUT_FIRMWARE		0x0008
#define QLA_MBOX_LOAD_RAM_EXT		0x000B
#define QLA_MBOX_CSUM_FIRMWARE		0x000E
#define QLA_MBOX_INIT_REQ_QUEUE		0x0010
#define QLA_MBOX_INIT_RSP_QUEUE		0x0011
#define QLA_MBOX_STOP_FIRMWARE		0x0014
#define QLA_MBOX_ABORT_IOCB		0x0015
#define QLA_MBOX_ABORT_DEVICE		0x0016
#define QLA_MBOX_ABORT_TARGET		0x0017
#define QLA_MBOX_RESET			0x0018
#define QLA_MBOX_ABORT_QUEUE		0x001C
#define QLA_MBOX_GET_QUEUE_STATUS	0x001D
#define QLA_MBOX_GET_FIRMWARE_STATUS	0x001F
#define QLA_MBOX_GET_LOOP_ID		0x0020
#define QLA_MBOX_SET_FIRMWARE_OPTIONS	0x0038
#define QLA_MBOX_ENH_GET_PORT_DB	0x0047
#define QLA_MBOX_PLOGO			0x0056
#define QLA_MBOX_INIT_FIRMWARE		0x0060
#define QLA_MBOX_GET_INIT_CB		0x0061
#define QLA_MBOX_LIP			0x0062
#define QLA_MBOX_GET_FC_AL_POS		0x0063
#define QLA_MBOX_GET_PORT_DB		0x0064
#define QLA_MBOX_TARGET_RESET		0x0066
#define QLA_MBOX_GET_FIRMWARE_STATE	0x0069
#define QLA_MBOX_GET_PORT_NAME		0x006A
#define QLA_MBOX_GET_LINK_STATUS	0x006B
#define QLA_MBOX_LIP_RESET		0x006C
#define QLA_MBOX_SEND_SNS		0x006E
#define QLA_MBOX_FABRIC_PLOGI		0x006F
#define QLA_MBOX_SEND_CHANGE_REQ	0x0070
#define QLA_MBOX_FABRIC_PLOGO		0x0071
#define QLA_MBOX_LOOP_PLOGI		0x0074
#define QLA_MBOX_GET_PORT_NAME_LIST	0x0075
#define QLA_MBOX_LUN_RESET		0x007E


/* nvram layout */
struct qla_nvram {
	u_int8_t	id[4];
	u_int8_t	nvram_version;
	u_int8_t	reserved_0;

	u_int8_t	parameter_block_version;
	u_int8_t	reserved_1;

	u_int16_t	fw_options;

	u_int16_t	frame_payload_size;
	u_int16_t	max_iocb_allocation;
	u_int16_t	execution_throttle;
	u_int8_t	retry_count;
	u_int8_t	retry_delay;
	u_int64_t	port_name;
	u_int16_t	hard_address;
	u_int8_t	inquiry_data;
	u_int8_t	login_timeout;
	u_int64_t	node_name;

	u_int16_t	add_fw_options;

	u_int8_t	response_accumulation_timer;
	u_int8_t	interrupt_delay_timer;

	u_int16_t	special_options;

	u_int8_t	reserved_2[22];

	u_int8_t	seriallink_options[4];

	u_int8_t	host_p[2];

	u_int64_t	boot_node_name;
	u_int8_t	boot_lun_number;
	u_int8_t	reset_delay;
	u_int8_t	port_down_retry_count;
	u_int8_t	boot_id_number;
	u_int16_t	max_luns_per_target;
	u_int64_t	fcode_boot_port_name;
	u_int64_t	alternate_port_name;
	u_int64_t	alternate_node_name;

	u_int8_t	efi_parameters;

	u_int8_t	link_down_timeout;

	u_int8_t	adapter_id[16];

	u_int64_t	alt1_boot_node_name;
	u_int16_t	alt1_boot_lun_number;
	u_int64_t	alt2_boot_node_name;
	u_int16_t	alt2_boot_lun_number;
	u_int64_t	alt3_boot_node_name;
	u_int16_t	alt3_boot_lun_number;
	u_int64_t	alt4_boot_node_name;
	u_int16_t	alt4_boot_lun_number;
	u_int64_t	alt5_boot_node_name;
	u_int16_t	alt5_boot_lun_number;
	u_int64_t	alt6_boot_node_name;
	u_int16_t	alt6_boot_lun_number;
	u_int64_t	alt7_boot_node_name;
	u_int16_t	alt7_boot_lun_number;

	u_int8_t	reserved_3[2];

	u_int8_t	model_number[16];

	u_int8_t	oem_specific[16];

	u_int8_t	adapter_features[2];

	u_int8_t	reserved_4[16];

	u_int16_t	subsystem_vendor_id_2200;
	u_int16_t	subsystem_device_id_2200;

	u_int8_t	reserved_5;
	u_int8_t	checksum;
} __packed;

/* init firmware control block */
#define QLA_ICB_VERSION		1

#define QLA_ICB_FW_HARD_ADDR		0x0001
#define QLA_ICB_FW_FAIRNESS		0x0002
#define QLA_ICB_FW_FULL_DUPLEX		0x0004
#define QLA_ICB_FW_FAST_POST		0x0008
#define QLA_ICB_FW_TARGET_MODE		0x0010
#define QLA_ICB_FW_DISABLE_INITIATOR	0x0020
#define QLA_ICB_FW_ENABLE_ADISC		0x0040
#define QLA_ICB_FW_ENABLE_TGT_DEV	0x0080
#define QLA_ICB_FW_ENABLE_PDB_CHANGED	0x0100
#define QLA_ICB_FW_DISABLE_INIT_LIP	0x0200
#define QLA_ICB_FW_DESC_LOOP_ID		0x0400
#define QLA_ICB_FW_PREV_LOOP_ID		0x0800
#define QLA_ICB_FW_RESERVED		0x1000
#define QLA_ICB_FW_LOGIN_AFTER_LIP	0x2000
#define QLA_ICB_FW_NAME_OPTION		0x4000
#define QLA_ICB_FW_EXTENDED_INIT_CB	0x8000

#define QLA_ICB_XFW_ZIO_DISABLED	0x0000
#define QLA_ICB_XFW_ZIO_MODE_5		0x0005
#define QLA_ICB_XFW_ZIO_MODE_6		0x0006

#define QLA_ICB_XFW_LOOP_PTP		0x0020
#define QLA_ICB_XFW_PTP_ONLY		0x0010
#define QLA_ICB_XFW_LOOP_ONLY		0x0000

#define QLA_ICB_XFW_HARD_ADDR_ONLY	0x0080
#define QLA_ICB_XFW_ENABLE_CLASS_2	0x0100
#define QLA_ICB_XFW_ENABLE_ACK0		0x0200
#define QLA_ICB_XFW_ENABLE_FC_TAPE	0x1000
#define QLA_ICB_XFW_ENABLE_FC_CONFIRM	0x2000
#define QLA_ICB_XFW_ENABLE_TGT_QUEUE	0x4000
#define QLA_ICB_XFW_NO_IMPLICIT_LOGOUT	0x8000

#define QLA_ICB_ZFW_ENABLE_XFR_RDY	0x0001
#define QLA_ICB_ZFW_SOFT_ID_ONLY	0x0002
#define QLA_ICB_ZFW_FCP_RSP_12_0	0x0010
#define QLA_ICB_ZFW_FCP_RSP_24_0	0x0020
#define QLA_ICB_ZFW_FCP_RSP_32_BYTES	0x0030
#define QLA_ICB_ZFW_ENABLE_OOO		0x0040
#define QLA_ICB_ZFW_NO_AUTO_PLOGI	0x0080
#define QLA_ICB_ZFW_50_OHMS		0x2000
#define QLA_ICB_ZFW_1GBPS		0x0000
#define QLA_ICB_ZFW_2GBPS		0x4000
#define QLA_ICB_ZFW_AUTONEG		0x8000


struct qla_init_cb {
	u_int8_t	icb_version;
	u_int8_t	icb_reserved;
	u_int16_t	icb_fw_options;
	u_int16_t	icb_max_frame_len;
	u_int16_t	icb_max_alloc;
	u_int16_t	icb_exec_throttle;
	u_int8_t	icb_retry_count;
	u_int8_t	icb_retry_delay;
	u_int32_t	icb_portname_hi;
	u_int32_t	icb_portname_lo;
	u_int16_t	icb_hardaddr;
	u_int8_t	icb_inquiry_data;
	u_int8_t	icb_login_timeout;
	u_int32_t	icb_nodename_hi;
	u_int32_t	icb_nodename_lo;
	u_int16_t	icb_req_out;
	u_int16_t	icb_resp_in;
	u_int16_t	icb_req_queue_len;
	u_int16_t	icb_resp_queue_len;
	u_int32_t	icb_req_queue_addr_lo;
	u_int32_t	icb_req_queue_addr_hi;
	u_int32_t	icb_resp_queue_addr_lo;
	u_int32_t	icb_resp_queue_addr_hi;
	u_int16_t	icb_lun_enables;
	u_int8_t	icb_cmd_count;
	u_int8_t	icb_notify_count;
	u_int16_t	icb_lun_timeout;
	u_int16_t	icb_reserved2;
	u_int16_t	icb_xfwoptions;
	u_int8_t	icb_reserved3;
	u_int8_t	icb_int_delaytimer;
	u_int16_t	icb_zfwoptions;
	u_int16_t	icb_reserved4[13];
} __packed __aligned(4);

#define QLA_FW_OPTION1_ASYNC_LIP_F8	0x0001
#define QLA_FW_OPTION1_ASYNC_LIP_RESET	0x0002
#define QLA_FW_OPTION1_SYNC_LOSS_LIP	0x0010
#define QLA_FW_OPTION1_ASYNC_LIP_ERROR	0x0080
#define QLA_FW_OPTION1_ASYNC_LOGIN_RJT	0x0800

#define QLA_FW_OPTION3_EMERG_IOCB	0x0001
#define QLA_FW_OPTION3_ASYNC_RND_ERROR	0x0002

/* topology types returned from QLA_MBOX_GET_LOOP_ID */
#define QLA_TOPO_NL_PORT		0
#define QLA_TOPO_FL_PORT		1
#define QLA_TOPO_N_PORT			2
#define QLA_TOPO_F_PORT			3
#define QLA_TOPO_N_PORT_NO_TARGET	4


struct qla_get_port_db {
	u_int8_t	options;
	u_int8_t	control;
	u_int8_t	master_state;
	u_int8_t	slave_state;
	u_int32_t	adisc_hard_addr;
	u_int16_t	port_id[2];
	u_int64_t	node_name;
	u_int64_t	port_name;
	u_int16_t	exec_throttle;
	u_int16_t	exec_count;
	u_int8_t	retry_count;
	u_int8_t	reserved;
	u_int16_t	resource_alloc;
	u_int16_t	current_alloc;
	u_int16_t	queue_head;
	u_int16_t	queue_tail;
	u_int16_t	xmit_exec_list_next;
	u_int16_t	xmit_exec_list_prev;
	u_int16_t	common_features;
	u_int16_t	total_concurrent_seq;
	u_int16_t	rel_offset;
	u_int16_t	recip_control_flags;
	u_int16_t	recv_data_size;
	u_int16_t	concurrent_seq;
	u_int16_t	open_seq;
	u_int8_t	reserved2[8];
	u_int16_t	retry_timer;
	u_int16_t	next_seq_id;
	u_int16_t	frame_count;
	u_int16_t	prli_payload_len;
	u_int16_t	prli_svc_word0;
	u_int16_t	prli_svc_word3;
	u_int16_t	loop_id;
	u_int16_t	ext_lun_list_ptr;
	u_int16_t	ext_lun_stop_ptr;
} __packed;

struct qla_port_name_list {
	u_int64_t	port_name;
	u_int16_t	loop_id;
} __packed;

#define QLA_SVC3_TARGET_ROLE		0x0010

/* fabric name server commands */
#define QLA_SNS_GA_NXT			0x0100
#define QLA_SNS_GID_FT			0x0171
#define QLA_SNS_RFT_ID			0x0217

#define QLA_FC4_SCSI			8

#define	QLA_LS_REJECT			0x8001
#define QLA_LS_ACCEPT			0x8002

struct qla_sns_req_hdr {
	u_int16_t	resp_len;
	u_int16_t	reserved;
	u_int32_t	resp_addr_lo;
	u_int32_t	resp_addr_hi;
	u_int16_t	subcmd_len;
	u_int16_t	reserved2;
} __packed;

struct qla_sns_ga_nxt {
	struct qla_sns_req_hdr header;
	u_int16_t	subcmd;
	u_int16_t	max_word;
	u_int32_t	reserved3;
	u_int32_t	port_id;
} __packed;

struct qla_sns_ga_nxt_resp {
	struct qla_sns_req_hdr header;
	u_int32_t	port_type_id;
	u_int64_t	port_name;
	u_int8_t	sym_port_name_len;
	u_int8_t	sym_port_name[255];
	u_int64_t	node_name;
	u_int8_t	sym_node_name_len;
	u_int8_t	sym_node_name[255];
	u_int64_t	initial_assoc;
	u_int8_t	ip_addr[16];
	u_int32_t	cos;
	u_int32_t	fc4_types[8];
} __packed;

struct qla_sns_rft_id {
	struct qla_sns_req_hdr header;
	u_int16_t	subcmd;
	u_int16_t	max_word;
	u_int32_t	reserved3;
	u_int32_t	port_id;
	u_int32_t	fc4_types[8];
} __packed;

struct qla_sns_gid_ft {
	struct qla_sns_req_hdr header;
	u_int16_t	subcmd;
	u_int16_t	max_word;
	u_int32_t	reserved3;
	u_int32_t	fc4_proto;
} __packed;

/* available handle ranges */
#define QLA_2KL_MIN_HANDLE		0x81
#define QLA_2KL_MAX_HANDLE		0x7EF
#define QLA_2KL_BUSWIDTH		0x800

#define QLA_MIN_HANDLE			0x81
#define QLA_MAX_HANDLE			0xFE
#define QLA_BUSWIDTH			0x100

#define QLA_F_PORT_HANDLE		0x7E
#define QLA_FABRIC_CTRL_HANDLE		0x7F
#define QLA_SNS_HANDLE			0x80
/* where does this go with 2klogin firmware? */
#define QLA_IP_BCAST_HANDLE		0xFF


/* IOCB types */
/*#define QLA_IOCB_CONT_TYPE_1		0x02 */
#define QLA_IOCB_STATUS			0x03
#define QLA_IOCB_MARKER			0x04
#define QLA_IOCB_STATUS_CONT		0x10
#define QLA_IOCB_CMD_TYPE_4		0x15
#define QLA_IOCB_CMD_TYPE_3		0x19
#define QLA_IOCB_MAILBOX		0x39

#define QLA_REQ_FLAG_CONT		0x01
#define QLA_REQ_FLAG_FULL		0x02
#define QLA_REQ_FLAG_BAD_HDR		0x04
#define QLA_REQ_FLAG_BAD_PKT		0x08

#define QLA_RESP_FLAG_INVALID_COUNT	0x10
#define QLA_RESP_FLAG_INVALID_ORDER	0x20
#define QLA_RESP_FLAG_DMA_ERR		0x40
#define QLA_RESP_FLAG_RESERVED		0x80

#define QLA_IOCB_CMD_HEAD_OF_QUEUE	0x0002
#define QLA_IOCB_CMD_ORDERED_QUEUE	0x0004
#define QLA_IOCB_CMD_SIMPLE_QUEUE	0x0008
#define QLA_IOCB_CMD_NO_DATA		0x0000
#define QLA_IOCB_CMD_READ_DATA		0x0020
#define QLA_IOCB_CMD_WRITE_DATA		0x0040
#define QLA_IOCB_CMD_NO_FAST_POST	0x0080

#define QLA_IOCB_SEGS_PER_CMD		2
#define QLA_IOCB_SEGS_PER_CMD_CONT	5

#define QLA_IOCB_MARKER_SYNC_ALL	2

struct qla_iocb_seg {
	u_int32_t	seg_addr_lo;
	u_int32_t	seg_addr_hi;
	u_int32_t	seg_len;
} __packed __aligned(4);

#if 0
struct qla_iocb_cont1 {
	u_int8_t	entry_type;	/* QLA_IOCB_CONT_TYPE_1 */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	struct qla_iocb_seg segs[5];
} __packed;
#endif

struct qla_iocb_status {
	u_int8_t	entry_type;	/* QLA_IOCB_STATUS */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	handle;
	u_int16_t	scsi_status;
	u_int16_t	completion;
	u_int16_t	state_flags;
	u_int16_t	status_flags;
	u_int16_t	rsp_len;
	u_int16_t	sense_len;
	u_int32_t	resid;
	u_int8_t	fcp_rsp[8];
	u_int8_t	sense_data[32];
} __packed;

/* completion */
#define QLA_IOCB_STATUS_COMPLETE	0x0000
#define QLA_IOCB_STATUS_DMA_ERROR	0x0002
#define QLA_IOCB_STATUS_RESET		0x0004
#define QLA_IOCB_STATUS_ABORTED		0x0005
#define QLA_IOCB_STATUS_TIMEOUT		0x0006
#define QLA_IOCB_STATUS_DATA_OVERRUN	0x0007
#define QLA_IOCB_STATUS_DATA_UNDERRUN	0x0015
#define QLA_IOCB_STATUS_QUEUE_FULL	0x001C
#define QLA_IOCB_STATUS_PORT_UNAVAIL	0x0028
#define QLA_IOCB_STATUS_PORT_LOGGED_OUT 0x0029
#define QLA_IOCB_STATUS_PORT_CHANGED	0x002A
#define QLA_IOCB_STATUS_PORT_BUSY	0x002B

#define QLA_SCSI_STATUS_FCP_LEN_VALID	0x0100
#define QLA_SCSI_STATUS_SENSE_VALID	0x0200
#define QLA_SCSI_STATUS_RESID_OVER	0x0400
#define QLA_SCSI_STATUS_RESID_UNDER	0x0800


struct qla_iocb_marker {
	u_int8_t	entry_type;	/* QLA_IOCB_MARKER */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	handle;
	u_int8_t	reserved;
	u_int8_t	target;
	u_int8_t	modifier;
	u_int8_t	vp_index;
	u_int16_t	marker_flags;
	u_int16_t	lun;
	u_int8_t	reserved2[48];
} __packed;

struct qla_iocb_status_cont {
	u_int8_t	entry_type;	/* QLA_IOCB_STATUS_CONT */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int8_t	sense[44];
} __packed;

struct qla_iocb_req34 {
	u_int8_t	entry_type;	/* QLA_IOCB_CMD_TYPE_3 or 4 */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	req_handle;
	u_int16_t	req_target;
	u_int16_t	req_scclun;
	u_int16_t	req_flags;
	u_int16_t	req_reserved;
	u_int16_t	req_time;
	u_int16_t	req_seg_count;
	u_int8_t	req_cdb[16];
	u_int32_t	req_totalcnt;
	union {
		struct qla_iocb_seg req3_segs[2];
		struct {
			u_int16_t req4_seg_type;
			u_int32_t req4_seg_base;
			u_int64_t req4_seg_addr;
			u_int8_t  req4_reserved[10];
		} __packed __aligned(4) req4;
	} 		req_type;
} __packed __aligned(4);

