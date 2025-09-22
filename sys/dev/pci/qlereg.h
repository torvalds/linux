/*	$OpenBSD: qlereg.h,v 1.9 2014/04/20 09:49:23 jmatthew Exp $ */

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
#define QLE_2400_CODE_ORG		0x00100000

/* interrupt types */
#define QLE_INT_TYPE_MBOX		1
#define QLE_INT_TYPE_ASYNC		2
#define QLE_INT_TYPE_IO			3
#define QLE_INT_TYPE_OTHER		4

/* 24xx interrupt status codes */
#define QLE_24XX_INT_ROM_MBOX		0x01
#define QLE_24XX_INT_ROM_MBOX_FAIL	0x02
#define QLE_24XX_INT_MBOX		0x10
#define QLE_24XX_INT_MBOX_FAIL		0x11
#define QLE_24XX_INT_ASYNC		0x12
#define QLE_24XX_INT_RSPQ		0x13

/* ISP registers */
#define QLE_FLASH_NVRAM_ADDR		0x000
#define QLE_FLASH_NVRAM_DATA		0x004
#define QLE_CTRL_STATUS			0x008
#define QLE_INT_CTRL			0x00C
#define QLE_INT_STATUS			0x010
#define QLE_REQ_IN			0x01C
#define QLE_REQ_OUT			0x020
#define QLE_RESP_IN			0x024
#define QLE_RESP_OUT			0x028
#define QLE_PRI_REQ_IN			0x02C
#define QLE_PRI_REQ_OUT			0x030
#define QLE_RISC_STATUS			0x044
#define QLE_HOST_CMD_CTRL		0x048
#define QLE_GPIO_DATA			0x04C
#define QLE_GPIO_ENABLE			0x050
#define QLE_HOST_SEMAPHORE		0x058

/* mailbox base moves around between generations */
#define QLE_MBOX_BASE_24XX		0x080

/* QLE_CTRL_STATUS */
#define QLE_CTRL_DMA_ACTIVE		0x00020000
#define QLE_CTRL_DMA_SHUTDOWN		0x00010000
#define QLE_CTRL_RESET			0x00000001

/* QLE_INT_STATUS */
#define QLE_RISC_INT_REQ		0x00000008

/* QLE_INT_CTRL */
#define QLE_INT_CTRL_ENABLE		0x00000008

/* QLE_RISC_STATUS */
#define QLE_INT_INFO_SHIFT		16
#define QLE_RISC_HOST_INT_REQ		0x00008000
#define QLE_RISC_PAUSED			0x00000100
#define QLE_INT_STATUS_MASK		0x000000FF

/* QLE_HOST_CMD_CTRL write */
#define QLE_HOST_CMD_SHIFT		28
#define QLE_HOST_CMD_NOP		0x0
#define QLE_HOST_CMD_RESET		0x1
#define QLE_HOST_CMD_CLEAR_RESET	0x2
#define QLE_HOST_CMD_PAUSE		0x3
#define QLE_HOST_CMD_RELEASE		0x4
#define QLE_HOST_CMD_SET_HOST_INT	0x5
#define QLE_HOST_CMD_CLR_HOST_INT	0x6
#define QLE_HOST_CMD_CLR_RISC_INT	0xA

/* QLE_HOST_CMD_CTRL read */
#define QLE_HOST_STATUS_ERROR_SHIFT	12
#define QLE_HOST_STATUS_HOST_INT	0x00000040
#define QLE_HOST_STATUS_RISC_RESET	0x00000020

/* QLE_HOST_SEMAPHORE */
#define QLE_HOST_SEMAPHORE_LOCK		0x00000001


/* QLE_MBOX_BASE (reg 0) read */
#define QLE_MBOX_HAS_STATUS		0x4000
#define QLE_MBOX_COMPLETE		0x4000
#define QLE_MBOX_INVALID		0x4001
#define QLE_MBOX_INTF_ERROR		0x4002
#define QLE_MBOX_TEST_FAILED		0x4003
#define QLE_MBOX_CMD_ERROR		0x4005
#define QLE_MBOX_CMD_PARAM		0x4006
#define QLE_MBOX_LINK_DOWN		0x400B
#define QLE_MBOX_DIAG_ERROR		0x400C
#define QLE_MBOX_CSUM_ERROR		0x4010
#define QLE_ASYNC_SYSTEM_ERROR		0x8002
#define QLE_ASYNC_REQ_XFER_ERROR	0x8003
#define QLE_ASYNC_RSP_XFER_ERROR	0x8004
#define QLE_ASYNC_LIP_OCCURRED		0x8010
#define QLE_ASYNC_LOOP_UP		0x8011
#define QLE_ASYNC_LOOP_DOWN		0x8012
#define QLE_ASYNC_LIP_RESET		0x8013
#define QLE_ASYNC_PORT_DB_CHANGE	0x8014
#define QLE_ASYNC_CHANGE_NOTIFY		0x8015
#define QLE_ASYNC_LIP_F8		0x8016
#define QLE_ASYNC_LOOP_INIT_ERROR	0x8017
#define QLE_ASYNC_POINT_TO_POINT	0x8030
#define QLE_ASYNC_ZIO_RESP_UPDATE	0x8040
#define QLE_ASYNC_RECV_ERROR		0x8048
#define QLE_ASYNC_LOGIN_RJT_SENT	0x8049


/* QLE_MBOX_BASE (reg 0) write */
#define QLE_MBOX_NOP			0x0000
#define QLE_MBOX_EXEC_FIRMWARE		0x0002
#define QLE_MBOX_REGISTER_TEST		0x0006
#define QLE_MBOX_VERIFY_CSUM		0x0007
#define QLE_MBOX_ABOUT_FIRMWARE		0x0008
#define QLE_MBOX_LOAD_RISC_RAM		0x000B
#define QLE_MBOX_INIT_RISC_RAM		0x000E
#define QLE_MBOX_READ_RISC_RAM		0x000F
#define QLE_MBOX_GET_IO_STATUS		0x0012
#define QLE_MBOX_STOP_FIRMWARE		0x0014
#define QLE_MBOX_GET_ID			0x0020
#define QLE_MBOX_SET_FIRMWARE_OPTIONS	0x0038
#define QLE_MBOX_PLOGO			0x0056
#define QLE_MBOX_DATA_RATE		0x005D
#define QLE_MBOX_INIT_FIRMWARE		0x0060
#define QLE_MBOX_GET_INIT_CB		0x0061
#define QLE_MBOX_GET_FC_AL_POS		0x0063
#define QLE_MBOX_GET_PORT_DB		0x0064
#define QLE_MBOX_GET_FIRMWARE_STATE	0x0069
#define QLE_MBOX_GET_PORT_NAME		0x006A
#define QLE_MBOX_GET_LINK_STATUS	0x006B
#define QLE_MBOX_SEND_CHANGE_REQ	0x0070
#define QLE_MBOX_LINK_INIT		0x0072
#define QLE_MBOX_GET_PORT_NAME_LIST	0x0075

#define QLE_MBOX_COUNT			32

/* nvram layout */
struct qle_nvram {
	u_int8_t	id[4];
	u_int16_t	nvram_version;
	u_int16_t	reserved_0;

	u_int16_t	version;
	u_int16_t	reserved_1;
	u_int16_t	frame_payload_size;
	u_int16_t	execution_throttle;
	u_int16_t	exchg_count;
	u_int16_t	hard_address;

	u_int64_t	port_name;
	u_int64_t	node_name;
	
	u_int16_t	login_retry;
	u_int16_t	link_down_on_nos;
	u_int16_t	int_delay_timer;
	u_int16_t	login_timeout;

	u_int32_t	fwoptions1;
	u_int32_t	fwoptions2;
	u_int32_t	fwoptions3;

	u_int16_t	serial_options[4];

	u_int16_t	reserved_2[96];

	u_int32_t	host_p;

	u_int64_t	alt_port_name;
	u_int64_t	alt_node_name;

	u_int64_t	boot_port_name;
	u_int16_t	boot_lun;
	u_int16_t	reserved_3;

	u_int64_t	alt1_boot_port_name;
	u_int16_t	alt1_boot_lun;
	u_int16_t	reserved_4;

	u_int64_t	alt2_boot_port_name;
	u_int16_t	alt2_boot_lun;
	u_int16_t	reserved_5;

	u_int64_t	alt3_boot_port_name;
	u_int16_t	alt3_boot_lun;
	u_int16_t	reserved_6;

	u_int32_t	efi_param;

	u_int8_t	reset_delay;
	u_int8_t	reserved_7;
	u_int16_t	reserved_8;

	u_int16_t	boot_id_num;
	u_int16_t	reserved_9;

	u_int16_t	max_luns_per_target;
	u_int16_t	reserved_10;

	u_int16_t	port_down_retry_count;
	u_int16_t	link_down_timeout;

	u_int16_t	fcode_param;
	u_int16_t	reserved_11[3];

	u_int8_t	prev_drv_ver_major;
	u_int8_t	prev_drv_ver_submajor;
	u_int8_t	prev_drv_ver_minor;
	u_int8_t	prev_drv_ver_subminor;

	u_int16_t	prev_bios_ver_major;
	u_int16_t	prev_bios_ver_minor;

	u_int16_t	prev_efi_ver_major;
	u_int16_t	prev_efi_ver_minor;

	u_int16_t	prev_fw_ver_major;
	u_int8_t	prev_fw_ver_minor;
	u_int8_t	prev_fw_ver_subminor;

	u_int16_t	reserved_12[56];

	u_int8_t	model_namep[16];

	u_int16_t	reserved_13[2];

	u_int16_t	pcie_table_sig;
	u_int16_t	pcie_table_offset;
	u_int16_t	subsystem_vendor_id;
	u_int16_t	subsystem_device_id;

	u_int32_t	checksum;
} __packed __aligned(4);

/* init firmware control block */
#define QLE_ICB_VERSION			1

#define QLE_ICB_FW1_HARD_ADDR		0x0001
#define QLE_ICB_FW1_FAIRNESS		0x0002
#define QLE_ICB_FW1_FULL_DUPLEX		0x0004
#define QLE_ICB_FW1_TARGET_MODE		0x0010
#define QLE_ICB_FW1_DISABLE_INITIATOR	0x0020
#define QLE_ICB_FW1_DISABLE_INIT_LIP	0x0200
#define QLE_ICB_FW1_DESC_LOOP_ID	0x0400
#define QLE_ICB_FW1_PREV_LOOP_ID	0x0800
#define QLE_ICB_FW1_LOGIN_AFTER_LIP	0x2000
#define QLE_ICB_FW1_NAME_OPTION		0x4000

#define QLE_ICB_FW2_LOOP_ONLY		0x0000
#define QLE_ICB_FW2_PTP_ONLY		0x0010
#define QLE_ICB_FW2_LOOP_PTP		0x0020
#define QLE_ICB_FW2_ZIO_DISABLED	0x0000
#define QLE_ICB_FW2_ZIO5_ENABLED	0x0005
#define QLE_ICB_FW2_ZIO6_ENABLED	0x0006
#define QLE_ICB_FW2_HARD_ADDR_ONLY	0x0080

#define QLE_ICB_FW3_SOFT_ID_ONLY	0x0002
#define QLE_ICB_FW3_FCP_RSP_12_0	0x0010
#define QLE_ICB_FW3_FCP_RSP_24_0	0x0020
#define QLE_ICB_FW3_FCP_RSP_32_BYTES	0x0030
#define QLE_ICB_FW3_ENABLE_OOO		0x0040
#define QLE_ICB_FW3_NO_AUTO_PLOGI	0x0080
#define QLE_ICB_FW3_ENABLE_OOO_RDY	0x0200
#define QLE_ICB_FW3_1GBPS		0x0000
#define QLE_ICB_FW3_2GBPS		0x2000
#define QLE_ICB_FW3_AUTONEG		0x4000
#define QLE_ICB_FW3_4GBPS		0x6000
#define QLE_ICB_FW3_50_OHMS		0x8000


struct qle_init_cb {
	u_int16_t	icb_version;
	u_int16_t	icb_reserved;
	u_int16_t	icb_max_frame_len;
	u_int16_t	icb_exec_throttle;
	u_int16_t	icb_exchange_count;
	u_int16_t	icb_hardaddr;
	u_int64_t	icb_portname;
	u_int64_t	icb_nodename;
	u_int16_t	icb_resp_in;
	u_int16_t	icb_req_out;
	u_int16_t	icb_login_retry;
	u_int16_t	icb_pri_req_out;
	u_int16_t	icb_resp_queue_len;
	u_int16_t	icb_req_queue_len;
	u_int16_t	icb_link_down_nos;
	u_int16_t	icb_pri_req_queue_len;
	u_int32_t	icb_req_queue_addr_lo;
	u_int32_t	icb_req_queue_addr_hi;
	u_int32_t	icb_resp_queue_addr_lo;
	u_int32_t	icb_resp_queue_addr_hi;
	u_int32_t	icb_pri_req_queue_addr_lo;
	u_int32_t	icb_pri_req_queue_addr_hi;
	u_int8_t	icb_reserved2[8];
	u_int16_t	icb_atio_queue_in;
	u_int16_t	icb_atio_queue_len;
	u_int64_t	icb_atio_queue_addr;
	u_int16_t	icb_int_delay;
	u_int16_t	icb_login_timeout;
	u_int32_t	icb_fwoptions1;
	u_int32_t	icb_fwoptions2;
	u_int32_t	icb_fwoptions3;
	u_int8_t	icb_reserved3[24];
} __packed __aligned(4);

#define QLE_FW_OPTION1_ASYNC_LIP_F8	0x0001
#define QLE_FW_OPTION1_ASYNC_LIP_RESET	0x0002
#define QLE_FW_OPTION1_SYNC_LOSS_LIP	0x0010
#define QLE_FW_OPTION1_ASYNC_LIP_ERROR	0x0080
#define QLE_FW_OPTION1_ASYNC_LOGIN_RJT	0x0800

#define QLE_FW_OPTION3_EMERG_IOCB	0x0001
#define QLE_FW_OPTION3_ASYNC_RND_ERROR	0x0002

/* topology types returned from QLE_MBOX_GET_LOOP_ID */
#define QLE_TOPO_NL_PORT		0
#define QLE_TOPO_FL_PORT		1
#define QLE_TOPO_N_PORT			2
#define QLE_TOPO_F_PORT			3
#define QLE_TOPO_N_PORT_NO_TARGET	4


struct qle_get_port_db {
	u_int16_t	flags;
	u_int8_t	current_login_state;
	u_int8_t	stable_login_state;
	u_int8_t	adisc_addr[3];
	u_int8_t	reserved;
	u_int8_t	port_id[3];
	u_int8_t	sequence_id;
	u_int16_t	retry_timer;
	u_int16_t	nport_handle;
	u_int16_t	recv_data_size;
	u_int16_t	reserved2;
	u_int16_t	prli_svc_word0;
	u_int16_t	prli_svc_word3;
	u_int64_t	port_name;
	u_int64_t	node_name;
	u_int8_t	reserved3[24];
} __packed __aligned(4);

struct qle_port_name_list {
	u_int64_t	port_name;
	u_int16_t	loopid;
	u_int16_t	reserved;
} __packed;

#define QLE_SVC3_TARGET_ROLE		0x0010

/* fabric name server commands */
#define QLE_SNS_GA_NXT			0x0100
#define QLE_SNS_GID_FT			0x0171
#define QLE_SNS_RFT_ID			0x0217

#define QLE_FC4_SCSI			8

#define	QLE_LS_REJECT			0x8001
#define QLE_LS_ACCEPT			0x8002

struct qle_ct_cmd_hdr {
	u_int8_t	ct_revision;
	u_int8_t	ct_id[3];
	u_int8_t	ct_gs_type;
	u_int8_t	ct_gs_subtype;
	u_int8_t	ct_gs_options;
	u_int8_t	ct_gs_reserved;
} __packed __aligned(4);

struct qle_ct_ga_nxt_req {
	struct qle_ct_cmd_hdr header;
	u_int16_t	subcmd;
	u_int16_t	max_word;
	u_int32_t	reserved3;
	u_int32_t	port_id;
} __packed __aligned(4);

struct qle_ct_ga_nxt_resp {
	struct qle_ct_cmd_hdr header;
	u_int16_t	response;
	u_int16_t	residual;
	u_int8_t	fragment_id;
	u_int8_t	reason_code;
	u_int8_t	explanation_code;
	u_int8_t	vendor_unique;

	u_int32_t	port_type_id;
	u_int64_t	port_name;
	u_int8_t	sym_port_name_len;
	u_int8_t	sym_port_name[255];
	u_int64_t	node_name;
	u_int8_t	sym_node_name_len;
	u_int8_t	sym_node_name[255];
	u_int64_t	initial_assoc;
	u_int8_t	node_ip_addr[16];
	u_int32_t	cos;
	u_int32_t	fc4_types[8];
	u_int8_t	ip_addr[16];
	u_int64_t	fabric_port_name;
	u_int32_t	hard_address;
} __packed __aligned(4);

struct qle_ct_rft_id_req {
	struct qle_ct_cmd_hdr header;
	u_int16_t	subcmd;
	u_int16_t	max_word;
	u_int32_t	reserved3;
	u_int32_t	port_id;
	u_int32_t	fc4_types[8];
} __packed __aligned(4);

struct qle_ct_rft_id_resp {
	struct qle_ct_cmd_hdr header;
	u_int16_t	response;
	u_int16_t	residual;
	u_int8_t	fragment_id;
	u_int8_t	reason_code;
	u_int8_t	explanation_code;
	u_int8_t	vendor_unique;
} __packed __aligned(4);

/* available handle ranges */
#define QLE_MIN_HANDLE			0x81
#define QLE_MAX_HANDLE			0x7EF

#define QLE_F_PORT_HANDLE		0x7FE
#define QLE_FABRIC_CTRL_HANDLE		0x7FD
#define QLE_SNS_HANDLE			0x7FC
#define QLE_IP_BCAST_HANDLE		0xFFF

/* IOCB types */
#define QLE_IOCB_STATUS			0x03
#define QLE_IOCB_MARKER			0x04
#define QLE_IOCB_STATUS_CONT		0x10
#define QLE_IOCB_CMD_TYPE_7		0x18
#define QLE_IOCB_CT_PASSTHROUGH		0x29
#define QLE_IOCB_MAILBOX		0x39
#define QLE_IOCB_CMD_TYPE_6		0x48
#define QLE_IOCB_PLOGX			0x52

#define QLE_REQ_FLAG_CONT		0x01
#define QLE_REQ_FLAG_FULL		0x02
#define QLE_REQ_FLAG_BAD_HDR		0x04
#define QLE_REQ_FLAG_BAD_PKT		0x08

#define QLE_RESP_FLAG_INVALID_COUNT	0x10
#define QLE_RESP_FLAG_INVALID_ORDER	0x20
#define QLE_RESP_FLAG_DMA_ERR		0x40
#define QLE_RESP_FLAG_RESERVED		0x80

#define QLE_IOCB_CTRL_FLAG_WRITE	0x0001
#define QLE_IOCB_CTRL_FLAG_READ		0x0002
#define QLE_IOCB_CTRL_FLAG_EXT_SEG	0x0004

#define QLE_IOCB_SEGS_PER_CMD		2

#define QLE_IOCB_MARKER_SYNC_ALL	2

struct qle_iocb_seg {
	u_int32_t	seg_addr_lo;
	u_int32_t	seg_addr_hi;
	u_int32_t	seg_len;
} __packed __aligned(4);

struct qle_iocb_status {
	u_int8_t	entry_type;	/* QLE_IOCB_STATUS */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	handle;
	u_int16_t	completion;
	u_int16_t	ox_id;
	u_int32_t	resid;
	u_int16_t	reserved;
	u_int16_t	state_flags;
	u_int16_t	reserved2;
	u_int16_t	scsi_status;
	u_int32_t	fcp_rsp_resid;
	u_int32_t	fcp_sense_len;

	u_int32_t	fcp_rsp_len;
	u_int8_t	data[28];
} __packed __aligned(64);

/* completion */
#define QLE_IOCB_STATUS_COMPLETE	0x0000
#define QLE_IOCB_STATUS_DMA_ERROR	0x0002
#define QLE_IOCB_STATUS_RESET		0x0004
#define QLE_IOCB_STATUS_ABORTED		0x0005
#define QLE_IOCB_STATUS_TIMEOUT		0x0006
#define QLE_IOCB_STATUS_DATA_OVERRUN	0x0007
#define QLE_IOCB_STATUS_DATA_UNDERRUN	0x0015
#define QLE_IOCB_STATUS_QUEUE_FULL	0x001C
#define QLE_IOCB_STATUS_PORT_UNAVAIL	0x0028
#define QLE_IOCB_STATUS_PORT_LOGGED_OUT 0x0029
#define QLE_IOCB_STATUS_PORT_CHANGED	0x002A
#define QLE_IOCB_STATUS_PORT_BUSY	0x002B

#define QLE_SCSI_STATUS_FCP_LEN_VALID	0x0100
#define QLE_SCSI_STATUS_SENSE_VALID	0x0200
#define QLE_SCSI_STATUS_RESID_OVER	0x0400
#define QLE_SCSI_STATUS_RESID_UNDER	0x0800


struct qle_iocb_marker {
	u_int8_t	entry_type;	/* QLE_IOCB_MARKER */
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
} __packed __aligned(64);

struct qle_iocb_status_cont {
	u_int8_t	entry_type;	/* QLE_IOCB_STATUS_CONT */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int8_t	sense[44];
} __packed __aligned(64);

struct qle_iocb_req6 {
	u_int8_t	entry_type;	/* QLE_IOCB_CMD_TYPE_6 */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	req_handle;
	u_int16_t	req_nport_handle;
	u_int16_t	req_timeout;
	u_int16_t	req_data_seg_count;
	u_int16_t	req_resp_seg_count;

	u_int16_t	req_fcp_lun[4];

	u_int16_t	req_ctrl_flags;
	u_int16_t	req_fcp_cmnd_len;

	u_int32_t	req_fcp_cmnd_addr_lo;
	u_int32_t	req_fcp_cmnd_addr_hi;

	u_int32_t	req_resp_seg_addr_lo;
	u_int32_t	req_resp_seg_addr_hi;

	u_int32_t	req_data_len;

	u_int32_t	req_target_id;
	struct qle_iocb_seg req_data_seg;
} __packed __aligned(64);

struct qle_fcp_cmnd {
	u_int16_t	fcp_lun[4];
	u_int8_t	fcp_crn;
	u_int8_t	fcp_task_attr;
	u_int8_t	fcp_task_mgmt;
	u_int8_t	fcp_add_cdb_len;
	
	u_int8_t	fcp_cdb[52];
	/* 64 bytes total */
} __packed __aligned(64);

struct qle_iocb_ct_passthrough {
	u_int8_t	entry_type;	/* QLE_IOCB_CT_PASSTHROUGH */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	req_handle;
	u_int16_t	req_status;
	u_int16_t	req_nport_handle;
	u_int16_t	req_dsd_count;
	u_int8_t	req_vp_index;
	u_int8_t	req_reserved;
	u_int16_t	req_timeout;
	u_int16_t	req_reserved2;
	u_int16_t	req_resp_dsd_count;
	u_int16_t	req_reserved3[5];
	u_int32_t	req_resp_byte_count;
	u_int32_t	req_cmd_byte_count;
	struct qle_iocb_seg req_cmd_seg;
	struct qle_iocb_seg req_resp_seg;
} __packed __aligned(64);

#define QLE_PLOGX_LOGIN			0x0000
#define QLE_PLOGX_LOGIN_COND		0x0010

#define QLE_PLOGX_LOGOUT		0x0008
#define QLE_PLOGX_LOGOUT_IMPLICIT	0x0010
#define QLE_PLOGX_LOGOUT_ALL		0x0020
#define QLE_PLOGX_LOGOUT_EXPLICIT	0x0040
#define QLE_PLOGX_LOGOUT_FREE_HANDLE	0x0080

#define QLE_PLOGX_PORT_UNAVAILABLE	0x28
#define QLE_PLOGX_PORT_LOGGED_OUT	0x29
#define QLE_PLOGX_ERROR			0x31

#define QLE_PLOGX_ERROR_PORT_ID_USED	0x1A
#define QLE_PLOGX_ERROR_HANDLE_USED	0x1B
#define QLE_PLOGX_ERROR_NO_HANDLE	0x1C

struct qle_iocb_plogx {
	u_int8_t	entry_type;	/* QLE_IOCB_PLOGX */
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;

	u_int32_t	req_handle;
	u_int16_t	req_status;
	u_int16_t	req_nport_handle;
	u_int16_t	req_flags;
	u_int8_t	req_vp_index;
	u_int8_t	req_reserved;
	u_int16_t	req_port_id_lo;
	u_int8_t	req_port_id_hi;
	u_int8_t	req_rspsize;
	u_int32_t	req_ioparms[11];
} __packed __aligned(64);
