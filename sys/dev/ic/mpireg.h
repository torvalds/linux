/*	$OpenBSD: mpireg.h,v 1.45 2014/03/25 05:41:44 dlg Exp $ */

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
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

/*
 * System Interface Register Set
 */

#define MPI_DOORBELL		0x00
/* doorbell read bits */
#define  MPI_DOORBELL_STATE		(0xf<<28) /* ioc state */
#define  MPI_DOORBELL_STATE_RESET	(0x0<<28)
#define  MPI_DOORBELL_STATE_READY	(0x1<<28)
#define  MPI_DOORBELL_STATE_OPER	(0x2<<28)
#define  MPI_DOORBELL_STATE_FAULT	(0x4<<28)
#define  MPI_DOORBELL_INUSE		(0x1<<27) /* doorbell used */
#define  MPI_DOORBELL_WHOINIT		(0x7<<24) /* last to reset ioc */
#define  MPI_DOORBELL_WHOINIT_NOONE	(0x0<<24) /* not initialized */
#define  MPI_DOORBELL_WHOINIT_SYSBIOS	(0x1<<24) /* system bios */
#define  MPI_DOORBELL_WHOINIT_ROMBIOS	(0x2<<24) /* rom bios */
#define  MPI_DOORBELL_WHOINIT_PCIPEER	(0x3<<24) /* pci peer */
#define  MPI_DOORBELL_WHOINIT_DRIVER	(0x4<<24) /* host driver */
#define  MPI_DOORBELL_WHOINIT_MANUFACT	(0x5<<24) /* manufacturing */
#define  MPI_DOORBELL_FAULT		(0xffff<<0) /* fault code */
#define  MPI_DOORBELL_FAULT_REQ_PCIPAR	0x8111 /* req msg pci parity err */
#define  MPI_DOORBELL_FAULT_REQ_PCIBUS	0x8112 /* req msg pci bus err */
#define  MPI_DOORBELL_FAULT_REP_PCIPAR	0x8113 /* reply msg pci parity err */
#define  MPI_DOORBELL_FAULT_REP_PCIBUS	0x8114 /* reply msg pci bus err */
#define  MPI_DOORBELL_FAULT_SND_PCIPAR	0x8115 /* data send pci parity err */
#define  MPI_DOORBELL_FAULT_SND_PCIBUS	0x8116 /* data send pci bus err */
#define  MPI_DOORBELL_FAULT_RCV_PCIPAR	0x8117 /* data recv pci parity err */
#define  MPI_DOORBELL_FAULT_RCV_PCIBUS	0x8118 /* data recv pci bus err */
/* doorbell write bits */
#define  MPI_DOORBELL_FUNCTION_SHIFT	24
#define  MPI_DOORBELL_FUNCTION_MASK	(0xff << MPI_DOORBELL_FUNCTION_SHIFT)
#define  MPI_DOORBELL_FUNCTION(x)	\
    (((x) << MPI_DOORBELL_FUNCTION_SHIFT) & MPI_DOORBELL_FUNCTION_MASK)
#define  MPI_DOORBELL_DWORDS_SHIFT	16
#define  MPI_DOORBELL_DWORDS_MASK	(0xff << MPI_DOORBELL_DWORDS_SHIFT)
#define  MPI_DOORBELL_DWORDS(x)		\
    (((x) << MPI_DOORBELL_DWORDS_SHIFT) & MPI_DOORBELL_DWORDS_MASK)
#define  MPI_DOORBELL_DATA_MASK		0xffff

#define MPI_WRITESEQ		0x04
#define  MPI_WRITESEQ_VALUE		0x0000000f /* key value */
#define  MPI_WRITESEQ_1			0x04
#define  MPI_WRITESEQ_2			0x0b
#define  MPI_WRITESEQ_3			0x02
#define  MPI_WRITESEQ_4			0x07
#define  MPI_WRITESEQ_5			0x0d

#define MPI_HOSTDIAG		0x08
#define  MPI_HOSTDIAG_CLEARFBS		(1<<10) /* clear flash bad sig */
#define  MPI_HOSTDIAG_POICB		(1<<9) /* prevent ioc boot */
#define  MPI_HOSTDIAG_DWRE		(1<<7) /* diag reg write enabled */
#define  MPI_HOSTDIAG_FBS		(1<<6) /* flash bad sig */
#define  MPI_HOSTDIAG_RESET_HIST	(1<<5) /* reset history */
#define  MPI_HOSTDIAG_DIAGWR_EN		(1<<4) /* diagnostic write enabled */
#define  MPI_HOSTDIAG_RESET_ADAPTER	(1<<2) /* reset adapter */
#define  MPI_HOSTDIAG_DISABLE_ARM	(1<<1) /* disable arm */
#define  MPI_HOSTDIAG_DIAGMEM_EN	(1<<0) /* diag mem enable */

#define MPI_TESTBASE		0x0c

#define MPI_DIAGRWDATA		0x10

#define MPI_DIAGRWADDR		0x18

#define MPI_INTR_STATUS		0x30
#define  MPI_INTR_STATUS_IOCDOORBELL	(1<<31) /* ioc doorbell status */
#define  MPI_INTR_STATUS_REPLY		(1<<3) /* reply message interrupt */
#define  MPI_INTR_STATUS_DOORBELL	(1<<0) /* doorbell interrupt */

#define MPI_INTR_MASK		0x34
#define  MPI_INTR_MASK_REPLY		(1<<3) /* reply message intr mask */
#define  MPI_INTR_MASK_DOORBELL		(1<<0) /* doorbell interrupt mask */

#define MPI_REQ_QUEUE		0x40

#define MPI_REPLY_QUEUE		0x44
#define  MPI_REPLY_QUEUE_ADDRESS	(1<<31) /* address reply */
#define  MPI_REPLY_QUEUE_ADDRESS_MASK	0x7fffffff
#define  MPI_REPLY_QUEUE_TYPE_MASK	(3<<29)
#define  MPI_REPLY_QUEUE_TYPE_INIT	(0<<29) /* scsi initiator reply */
#define  MPI_REPLY_QUEUE_TYPE_TARGET	(1<<29) /* scsi target reply */
#define  MPI_REPLY_QUEUE_TYPE_LAN	(2<<29) /* lan reply */
#define  MPI_REPLY_QUEUE_CONTEXT	0x1fffffff /* not address and type */

#define MPI_PRIREQ_QUEUE	0x48

/*
 * Scatter Gather Lists
 */

#define MPI_SGE_FL_LAST			(0x1<<31) /* last element in segment */
#define MPI_SGE_FL_EOB			(0x1<<30) /* last element of buffer */
#define MPI_SGE_FL_TYPE			(0x3<<28) /* element type */
#define  MPI_SGE_FL_TYPE_SIMPLE		(0x1<<28) /* simple element */
#define  MPI_SGE_FL_TYPE_CHAIN		(0x3<<28) /* chain element */
#define  MPI_SGE_FL_TYPE_XACTCTX	(0x0<<28) /* transaction context */
#define MPI_SGE_FL_LOCAL		(0x1<<27) /* local address */
#define MPI_SGE_FL_DIR			(0x1<<26) /* direction */
#define  MPI_SGE_FL_DIR_OUT		(0x1<<26)
#define  MPI_SGE_FL_DIR_IN		(0x0<<26)
#define MPI_SGE_FL_SIZE			(0x1<<25) /* address size */
#define  MPI_SGE_FL_SIZE_32		(0x0<<25)
#define  MPI_SGE_FL_SIZE_64		(0x1<<25)
#define MPI_SGE_FL_EOL			(0x1<<24) /* end of list */
#define MPI_SGE_FLAGS_IOC_TO_HOST	(0x00)
#define MPI_SGE_FLAGS_HOST_TO_IOC	(0x04)

struct mpi_sge {
	u_int32_t		sg_hdr;
	u_int32_t		sg_addr_lo;
	u_int32_t		sg_addr_hi;
} __packed __aligned(4);

struct mpi_fw_tce {
	u_int8_t		reserved1;
	u_int8_t		context_size;
	u_int8_t		details_length;
	u_int8_t		flags;

	u_int32_t		reserved2;

	u_int32_t		image_offset;

	u_int32_t		image_size;
} __packed __aligned(4);

/*
 * Messages
 */

/* functions */
#define MPI_FUNCTION_SCSI_IO_REQUEST			(0x00)
#define MPI_FUNCTION_SCSI_TASK_MGMT			(0x01)
#define MPI_FUNCTION_IOC_INIT				(0x02)
#define MPI_FUNCTION_IOC_FACTS				(0x03)
#define MPI_FUNCTION_CONFIG				(0x04)
#define MPI_FUNCTION_PORT_FACTS				(0x05)
#define MPI_FUNCTION_PORT_ENABLE			(0x06)
#define MPI_FUNCTION_EVENT_NOTIFICATION			(0x07)
#define MPI_FUNCTION_EVENT_ACK				(0x08)
#define MPI_FUNCTION_FW_DOWNLOAD			(0x09)
#define MPI_FUNCTION_TARGET_CMD_BUFFER_POST		(0x0A)
#define MPI_FUNCTION_TARGET_ASSIST			(0x0B)
#define MPI_FUNCTION_TARGET_STATUS_SEND			(0x0C)
#define MPI_FUNCTION_TARGET_MODE_ABORT			(0x0D)
#define MPI_FUNCTION_TARGET_FC_BUF_POST_LINK_SRVC	(0x0E) /* obsolete */
#define MPI_FUNCTION_TARGET_FC_RSP_LINK_SRVC		(0x0F) /* obsolete */
#define MPI_FUNCTION_TARGET_FC_EX_SEND_LINK_SRVC	(0x10) /* obsolete */
#define MPI_FUNCTION_TARGET_FC_ABORT			(0x11) /* obsolete */
#define MPI_FUNCTION_FC_LINK_SRVC_BUF_POST		(0x0E)
#define MPI_FUNCTION_FC_LINK_SRVC_RSP			(0x0F)
#define MPI_FUNCTION_FC_EX_LINK_SRVC_SEND		(0x10)
#define MPI_FUNCTION_FC_ABORT				(0x11)
#define MPI_FUNCTION_FW_UPLOAD				(0x12)
#define MPI_FUNCTION_FC_COMMON_TRANSPORT_SEND		(0x13)
#define MPI_FUNCTION_FC_PRIMITIVE_SEND			(0x14)

#define MPI_FUNCTION_RAID_ACTION			(0x15)
#define MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH		(0x16)

#define MPI_FUNCTION_TOOLBOX				(0x17)

#define MPI_FUNCTION_SCSI_ENCLOSURE_PROCESSOR		(0x18)

#define MPI_FUNCTION_MAILBOX				(0x19)

#define MPI_FUNCTION_LAN_SEND				(0x20)
#define MPI_FUNCTION_LAN_RECEIVE			(0x21)
#define MPI_FUNCTION_LAN_RESET				(0x22)

#define MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET		(0x40)
#define MPI_FUNCTION_IO_UNIT_RESET			(0x41)
#define MPI_FUNCTION_HANDSHAKE				(0x42)
#define MPI_FUNCTION_REPLY_FRAME_REMOVAL		(0x43)

/* reply flags */
#define MPI_REP_FLAGS_CONT		(1<<7) /* continuation reply */

#define MPI_REP_IOCSTATUS_AVAIL		(1<<15) /* logging info available */
#define MPI_REP_IOCSTATUS		(0x7fff) /* status */

/* Common IOCStatus values for all replies */
#define  MPI_IOCSTATUS_SUCCESS				(0x0000)
#define  MPI_IOCSTATUS_INVALID_FUNCTION			(0x0001)
#define  MPI_IOCSTATUS_BUSY				(0x0002)
#define  MPI_IOCSTATUS_INVALID_SGL			(0x0003)
#define  MPI_IOCSTATUS_INTERNAL_ERROR			(0x0004)
#define  MPI_IOCSTATUS_RESERVED				(0x0005)
#define  MPI_IOCSTATUS_INSUFFICIENT_RESOURCES		(0x0006)
#define  MPI_IOCSTATUS_INVALID_FIELD			(0x0007)
#define  MPI_IOCSTATUS_INVALID_STATE			(0x0008)
#define  MPI_IOCSTATUS_OP_STATE_NOT_SUPPORTED		(0x0009)
/* Config IOCStatus values */
#define  MPI_IOCSTATUS_CONFIG_INVALID_ACTION		(0x0020)
#define  MPI_IOCSTATUS_CONFIG_INVALID_TYPE		(0x0021)
#define  MPI_IOCSTATUS_CONFIG_INVALID_PAGE		(0x0022)
#define  MPI_IOCSTATUS_CONFIG_INVALID_DATA		(0x0023)
#define  MPI_IOCSTATUS_CONFIG_NO_DEFAULTS		(0x0024)
#define  MPI_IOCSTATUS_CONFIG_CANT_COMMIT		(0x0025)
/* SCSIIO Reply (SPI & FCP) initiator values */
#define  MPI_IOCSTATUS_SCSI_RECOVERED_ERROR		(0x0040)
#define  MPI_IOCSTATUS_SCSI_INVALID_BUS			(0x0041)
#define  MPI_IOCSTATUS_SCSI_INVALID_TARGETID		(0x0042)
#define  MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE		(0x0043)
#define  MPI_IOCSTATUS_SCSI_DATA_OVERRUN		(0x0044)
#define  MPI_IOCSTATUS_SCSI_DATA_UNDERRUN		(0x0045)
#define  MPI_IOCSTATUS_SCSI_IO_DATA_ERROR		(0x0046)
#define  MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR		(0x0047)
#define  MPI_IOCSTATUS_SCSI_TASK_TERMINATED		(0x0048)
#define  MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH		(0x0049)
#define  MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED		(0x004A)
#define  MPI_IOCSTATUS_SCSI_IOC_TERMINATED		(0x004B)
#define  MPI_IOCSTATUS_SCSI_EXT_TERMINATED		(0x004C)
/* For use by SCSI Initiator and SCSI Target end-to-end data protection */
#define  MPI_IOCSTATUS_EEDP_GUARD_ERROR			(0x004D)
#define  MPI_IOCSTATUS_EEDP_REF_TAG_ERROR		(0x004E)
#define  MPI_IOCSTATUS_EEDP_APP_TAG_ERROR		(0x004F)
/* SCSI (SPI & FCP) target values */
#define  MPI_IOCSTATUS_TARGET_PRIORITY_IO		(0x0060)
#define  MPI_IOCSTATUS_TARGET_INVALID_PORT		(0x0061)
#define  MPI_IOCSTATUS_TARGET_INVALID_IOCINDEX		(0x0062) /* obsolete */
#define  MPI_IOCSTATUS_TARGET_INVALID_IO_INDEX		(0x0062)
#define  MPI_IOCSTATUS_TARGET_ABORTED			(0x0063)
#define  MPI_IOCSTATUS_TARGET_NO_CONN_RETRYABLE		(0x0064)
#define  MPI_IOCSTATUS_TARGET_NO_CONNECTION		(0x0065)
#define  MPI_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH	(0x006A)
#define  MPI_IOCSTATUS_TARGET_STS_DATA_NOT_SENT		(0x006B)
#define  MPI_IOCSTATUS_TARGET_DATA_OFFSET_ERROR		(0x006D)
#define  MPI_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA	(0x006E)
#define  MPI_IOCSTATUS_TARGET_IU_TOO_SHORT		(0x006F)
/* Additional FCP target values */
#define  MPI_IOCSTATUS_TARGET_FC_ABORTED		(0x0066) /* obsolete */
#define  MPI_IOCSTATUS_TARGET_FC_RX_ID_INVALID		(0x0067) /* obsolete */
#define  MPI_IOCSTATUS_TARGET_FC_DID_INVALID		(0x0068) /* obsolete */
#define  MPI_IOCSTATUS_TARGET_FC_NODE_LOGGED_OUT	(0x0069) /* obsolete */
/* Fibre Channel Direct Access values */
#define  MPI_IOCSTATUS_FC_ABORTED			(0x0066)
#define  MPI_IOCSTATUS_FC_RX_ID_INVALID			(0x0067)
#define  MPI_IOCSTATUS_FC_DID_INVALID			(0x0068)
#define  MPI_IOCSTATUS_FC_NODE_LOGGED_OUT		(0x0069)
#define  MPI_IOCSTATUS_FC_EXCHANGE_CANCELED		(0x006C)
/* LAN values */
#define  MPI_IOCSTATUS_LAN_DEVICE_NOT_FOUND		(0x0080)
#define  MPI_IOCSTATUS_LAN_DEVICE_FAILURE		(0x0081)
#define  MPI_IOCSTATUS_LAN_TRANSMIT_ERROR		(0x0082)
#define  MPI_IOCSTATUS_LAN_TRANSMIT_ABORTED		(0x0083)
#define  MPI_IOCSTATUS_LAN_RECEIVE_ERROR		(0x0084)
#define  MPI_IOCSTATUS_LAN_RECEIVE_ABORTED		(0x0085)
#define  MPI_IOCSTATUS_LAN_PARTIAL_PACKET		(0x0086)
#define  MPI_IOCSTATUS_LAN_CANCELED			(0x0087)
/* Serial Attached SCSI values */
#define  MPI_IOCSTATUS_SAS_SMP_REQUEST_FAILED		(0x0090)
#define  MPI_IOCSTATUS_SAS_SMP_DATA_OVERRUN		(0x0091)
/* Inband values */
#define  MPI_IOCSTATUS_INBAND_ABORTED			(0x0098)
#define  MPI_IOCSTATUS_INBAND_NO_CONNECTION		(0x0099)
/* Diagnostic Tools values */
#define  MPI_IOCSTATUS_DIAGNOSTIC_RELEASED		(0x00A0)

#define MPI_REP_IOCLOGINFO_TYPE		(0xf<<28) /* logging info type */
#define MPI_REP_IOCLOGINFO_TYPE_NONE	(0x0<<28)
#define MPI_REP_IOCLOGINFO_TYPE_SCSI	(0x1<<28)
#define MPI_REP_IOCLOGINFO_TYPE_FC	(0x2<<28)
#define MPI_REP_IOCLOGINFO_TYPE_SAS	(0x3<<28)
#define MPI_REP_IOCLOGINFO_TYPE_ISCSI	(0x4<<28)
#define MPI_REP_IOCLOGINFO_DATA		(0x0fffffff) /* logging info data */

/* event notification types */
#define MPI_EVENT_NONE					0x00
#define MPI_EVENT_LOG_DATA				0x01
#define MPI_EVENT_STATE_CHANGE				0x02
#define MPI_EVENT_UNIT_ATTENTION			0x03
#define MPI_EVENT_IOC_BUS_RESET				0x04
#define MPI_EVENT_EXT_BUS_RESET				0x05
#define MPI_EVENT_RESCAN				0x06
#define MPI_EVENT_LINK_STATUS_CHANGE			0x07
#define MPI_EVENT_LOOP_STATE_CHANGE			0x08
#define MPI_EVENT_LOGOUT				0x09
#define MPI_EVENT_EVENT_CHANGE				0x0a
#define MPI_EVENT_INTEGRATED_RAID			0x0b
#define MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE		0x0c
#define MPI_EVENT_ON_BUS_TIMER_EXPIRED			0x0d
#define MPI_EVENT_QUEUE_FULL				0x0e
#define MPI_EVENT_SAS_DEVICE_STATUS_CHANGE		0x0f
#define MPI_EVENT_SAS_SES				0x10
#define MPI_EVENT_PERSISTENT_TABLE_FULL			0x11
#define MPI_EVENT_SAS_PHY_LINK_STATUS			0x12
#define MPI_EVENT_SAS_DISCOVERY_ERROR			0x13
#define MPI_EVENT_IR_RESYNC_UPDATE			0x14
#define MPI_EVENT_IR2					0x15
#define MPI_EVENT_SAS_DISCOVERY				0x16
#define MPI_EVENT_LOG_ENTRY_ADDED			0x21

/* messages */

#define MPI_WHOINIT_NOONE		0x00
#define MPI_WHOINIT_SYSTEM_BIOS		0x01
#define MPI_WHOINIT_ROM_BIOS		0x02
#define MPI_WHOINIT_PCI_PEER		0x03
#define MPI_WHOINIT_HOST_DRIVER		0x04
#define MPI_WHOINIT_MANUFACTURER	0x05

/* page address fields */
#define MPI_PAGE_ADDRESS_FC_BTID	(1<<24)	/* Bus Target ID */

/* default messages */

struct mpi_msg_request {
	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		reserved5;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;
} __packed __aligned(4);

struct mpi_msg_reply {
	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		reserved5;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int8_t		reserved6;
	u_int8_t		reserved7;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

/* ioc init */

struct mpi_msg_iocinit_request {
	u_int8_t		whoinit;
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		flags;
#define MPI_IOCINIT_F_DISCARD_FW			(1<<0)
#define MPI_IOCINIT_F_ENABLE_HOST_FIFO			(1<<1)
#define MPI_IOCINIT_F_HOST_PG_BUF_PERSIST		(1<<2)
	u_int8_t		max_devices;
	u_int8_t		max_buses;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reply_frame_size;
	u_int16_t		reserved2;

	u_int32_t		host_mfa_hi_addr;

	u_int32_t		sense_buffer_hi_addr;

	u_int32_t		reply_fifo_host_signalling_addr;

	struct mpi_sge		host_page_buffer_sge;

	u_int8_t		msg_version_min;
	u_int8_t		msg_version_maj;

	u_int8_t		hdr_version_unit;
	u_int8_t		hdr_version_dev;
} __packed __aligned(4);

struct mpi_msg_iocinit_reply {
	u_int8_t		whoinit;
	u_int8_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		flags;
	u_int8_t		max_devices;
	u_int8_t		max_buses;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved2;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);


/* ioc facts */
struct mpi_msg_iocfacts_request {
	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		reserved5;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;
} __packed __aligned(4);

struct mpi_msg_iocfacts_reply {
	u_int8_t		msg_version_min;
	u_int8_t		msg_version_maj;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		header_version_min;
	u_int8_t		header_version_maj;
	u_int8_t		ioc_number;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		ioc_exceptions;
#define MPI_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL	(1<<0)
#define MPI_IOCFACTS_EXCEPT_RAID_CONFIG_INVALID		(1<<1)
#define MPI_IOCFACTS_EXCEPT_FW_CHECKSUM_FAIL		(1<<2)
#define MPI_IOCFACTS_EXCEPT_PERSISTENT_TABLE_FULL	(1<<3)
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int8_t		max_chain_depth;
	u_int8_t		whoinit;
	u_int8_t		block_size;
	u_int8_t		flags;
#define MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT		(1<<0)
#define MPI_IOCFACTS_FLAGS_REPLY_FIFO_HOST_SIGNAL	(1<<1)
#define MPI_IOCFACTS_FLAGS_HOST_PAGE_BUFFER_PERSISTENT	(1<<2)

	u_int16_t		reply_queue_depth;
	u_int16_t		request_frame_size;

	u_int16_t		reserved1;
	u_int16_t		product_id;	/* product id */

	u_int32_t		current_host_mfa_hi_addr;

	u_int16_t		global_credits;
	u_int8_t		number_of_ports;
	u_int8_t		event_state;

	u_int32_t		current_sense_buffer_hi_addr;

	u_int16_t		current_reply_frame_size;
	u_int8_t		max_devices;
	u_int8_t		max_buses;

	u_int32_t		fw_image_size;

	u_int32_t		ioc_capabilities;
#define MPI_IOCFACTS_CAPABILITY_HIGH_PRI_Q		(1<<0)
#define MPI_IOCFACTS_CAPABILITY_REPLY_HOST_SIGNAL	(1<<1)
#define MPI_IOCFACTS_CAPABILITY_QUEUE_FULL_HANDLING	(1<<2)
#define MPI_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER	(1<<3)
#define MPI_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER		(1<<4)
#define MPI_IOCFACTS_CAPABILITY_EXTENDED_BUFFER		(1<<5)
#define MPI_IOCFACTS_CAPABILITY_EEDP			(1<<6)
#define MPI_IOCFACTS_CAPABILITY_BIDIRECTIONAL		(1<<7)
#define MPI_IOCFACTS_CAPABILITY_MULTICAST		(1<<8)
#define MPI_IOCFACTS_CAPABILITY_SCSIIO32		(1<<9)
#define MPI_IOCFACTS_CAPABILITY_NO_SCSIIO16		(1<<10)

	u_int8_t		fw_version_dev;
	u_int8_t		fw_version_unit;
	u_int8_t		fw_version_min;
	u_int8_t		fw_version_maj;

	u_int16_t		hi_priority_queue_depth;
	u_int16_t		reserved2;

	struct mpi_sge		host_page_buffer_sge;

	u_int32_t		reply_fifo_host_signalling_addr;
} __packed __aligned(4);

struct mpi_msg_portfacts_request {
	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		port_number;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

} __packed __aligned(4);

struct mpi_msg_portfacts_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		port_number;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved3;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int8_t		reserved4;
	u_int8_t		port_type;
#define MPI_PORTFACTS_PORTTYPE_INACTIVE			0x00
#define MPI_PORTFACTS_PORTTYPE_SCSI			0x01
#define MPI_PORTFACTS_PORTTYPE_FC			0x10
#define MPI_PORTFACTS_PORTTYPE_ISCSI			0x20
#define MPI_PORTFACTS_PORTTYPE_SAS			0x30

	u_int16_t		max_devices;

	u_int16_t		port_scsi_id;
	u_int16_t		protocol_flags;
#define MPI_PORTFACTS_PROTOCOL_LOGBUSADDR		(1<<0)
#define MPI_PORTFACTS_PROTOCOL_LAN			(1<<1)
#define MPI_PORTFACTS_PROTOCOL_TARGET			(1<<2)
#define MPI_PORTFACTS_PROTOCOL_INITIATOR		(1<<3)

	u_int16_t		max_posted_cmd_buffers;
	u_int16_t		max_persistent_ids;

	u_int16_t		max_lan_buckets;
	u_int16_t		reserved5;

	u_int32_t		reserved6;
} __packed __aligned(4);

struct mpi_msg_portenable_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		port_number;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;
} __packed __aligned(4);

struct mpi_msg_portenable_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		port_number;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved3;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

struct mpi_msg_event_request {
	u_int8_t		event_switch;
#define MPI_EVENT_SWITCH_ON				(0x01)
#define MPI_EVENT_SWITCH_OFF				(0x00)
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int32_t		msg_context;
} __packed __aligned(4);

struct mpi_msg_event_reply {
	u_int16_t		data_length;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved1;
	u_int8_t		ack_required;
#define MPI_EVENT_ACK_REQUIRED				(0x01)
	u_int8_t		msg_flags;
#define MPI_EVENT_FLAGS_REPLY_KEPT			(1<<7)

	u_int32_t		msg_context;

	u_int16_t		reserved2;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		event;

	u_int32_t		event_context;

	/* event data follows */
} __packed __aligned(4);

struct mpi_evt_change {
	u_int8_t		event_state;
	u_int8_t		reserved[3];
} __packed __aligned(4);

struct mpi_evt_link_status_change {
	u_int8_t		state;
#define MPI_EVT_LINK_STATUS_CHANGE_OFFLINE		0x00
#define MPI_EVT_LINK_STATUS_CHANGE_ACTIVE		0x01
	u_int8_t		_reserved1[3];

	u_int8_t		_reserved2[1];
	u_int8_t		port;
	u_int8_t		_reserved3[2];
} __packed __aligned(4);

struct mpi_evt_loop_status_change {
	u_int8_t		character4;
	u_int8_t		character3;
	u_int8_t		type;
#define MPI_EVT_LOOP_STATUS_CHANGE_TYPE_LIP		0x01
#define MPI_EVT_LOOP_STATUS_CHANGE_TYPE_LPE		0x02
#define MPI_EVT_LOOP_STATUS_CHANGE_TYPE_LPB		0x03
	u_int8_t		_reserved1[1];

	u_int8_t		_reserved2[1];
	u_int8_t		port;
	u_int8_t		_reserved3[2];
} __packed __aligned(4);

struct mpi_evt_logout {
	u_int32_t		n_portid;

	u_int8_t		alias_index;
	u_int8_t		port;
	u_int8_t		_reserved[2];
} __packed __aligned(4);

struct mpi_evt_sas_phy {
	u_int8_t		phy_num;
	u_int8_t		link_rates;
#define MPI_EVT_SASPHY_LINK_CUR(x)			(((x) & 0xf0) >> 4)
#define MPI_EVT_SASPHY_LINK_PREV(x)			((x) & 0x0f)
#define MPI_EVT_SASPHY_LINK_ENABLED			0x0
#define MPI_EVT_SASPHY_LINK_DISABLED			0x1
#define MPI_EVT_SASPHY_LINK_NEGFAIL			0x2
#define MPI_EVT_SASPHY_LINK_SATAOOB			0x3
#define MPI_EVT_SASPHY_LINK_1_5GBPS			0x8
#define MPI_EVT_SASPHY_LINK_3_0GBPS			0x9
	u_int16_t		dev_handle;

	u_int64_t		sas_addr;
} __packed __aligned(4);

struct mpi_evt_sas_change {
	u_int8_t		target;
	u_int8_t		bus;
	u_int8_t		reason;
#define MPI_EVT_SASCH_REASON_ADDED			0x03
#define MPI_EVT_SASCH_REASON_NOT_RESPONDING		0x04
#define MPI_EVT_SASCH_REASON_SMART_DATA			0x05
#define MPI_EVT_SASCH_REASON_NO_PERSIST_ADDED		0x06
#define MPI_EVT_SASCH_REASON_UNSUPPORTED		0x07
#define MPI_EVT_SASCH_REASON_INTERNAL_RESET		0x08
	u_int8_t		reserved1;

	u_int8_t		asc;
	u_int8_t		ascq;
	u_int16_t		dev_handle;

	u_int32_t		device_info;
#define MPI_EVT_SASCH_INFO_ATAPI			(1<<13)
#define MPI_EVT_SASCH_INFO_LSI				(1<<12)
#define MPI_EVT_SASCH_INFO_DIRECT_ATTACHED		(1<<11)
#define MPI_EVT_SASCH_INFO_SSP				(1<<10)
#define MPI_EVT_SASCH_INFO_STP				(1<<9)
#define MPI_EVT_SASCH_INFO_SMP				(1<<8)
#define MPI_EVT_SASCH_INFO_SATA				(1<<7)
#define MPI_EVT_SASCH_INFO_SSP_INITIATOR		(1<<6)
#define MPI_EVT_SASCH_INFO_STP_INITIATOR		(1<<5)
#define MPI_EVT_SASCH_INFO_SMP_INITIATOR		(1<<4)
#define MPI_EVT_SASCH_INFO_SATA_HOST			(1<<3)
#define MPI_EVT_SASCH_INFO_TYPE_MASK			0x7
#define MPI_EVT_SASCH_INFO_TYPE_NONE			0x0
#define MPI_EVT_SASCH_INFO_TYPE_END			0x1
#define MPI_EVT_SASCH_INFO_TYPE_EDGE			0x2
#define MPI_EVT_SASCH_INFO_TYPE_FANOUT			0x3

	u_int16_t		parent_dev_handle;
	u_int8_t		phy_num;
	u_int8_t		reserved2;

	u_int64_t		sas_addr;
} __packed __aligned(4);

struct mpi_msg_eventack_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int32_t		event;

	u_int32_t		event_context;
} __packed __aligned(4);

struct mpi_msg_eventack_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved3;
	u_int32_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

struct mpi_msg_fwupload_request {
	u_int8_t		image_type;
#define MPI_FWUPLOAD_IMAGETYPE_IOC_FW			(0x00)
#define MPI_FWUPLOAD_IMAGETYPE_NV_FW			(0x01)
#define MPI_FWUPLOAD_IMAGETYPE_MPI_NV_FW		(0x02)
#define MPI_FWUPLOAD_IMAGETYPE_NV_DATA			(0x03)
#define MPI_FWUPLOAD_IMAGETYPE_BOOT			(0x04)
#define MPI_FWUPLOAD_IMAGETYPE_NV_BACKUP		(0x05)
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	struct mpi_fw_tce	tce;

	/* followed by an sgl */
} __packed __aligned(4);

struct mpi_msg_fwupload_reply {
	u_int8_t		image_type;
	u_int8_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved3;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		actual_image_size;
} __packed __aligned(4);

struct mpi_msg_scsi_io {
	u_int8_t		target_id;
	u_int8_t		bus;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		cdb_length;
	u_int8_t		sense_buf_len;
	u_int8_t		reserved1;
	u_int8_t		msg_flags;
#define MPI_SCSIIO_EEDP					0xf0
#define MPI_SCSIIO_CMD_DATA_DIR				(1<<2)
#define MPI_SCSIIO_SENSE_BUF_LOC			(1<<1)
#define MPI_SCSIIO_SENSE_BUF_ADDR_WIDTH			(1<<0)
#define  MPI_SCSIIO_SENSE_BUF_ADDR_WIDTH_32		(0<<0)
#define  MPI_SCSIIO_SENSE_BUF_ADDR_WIDTH_64		(1<<0)

	u_int32_t		msg_context;

	u_int16_t		lun[4];

	u_int8_t		reserved2;
	u_int8_t		tagging;
#define MPI_SCSIIO_ATTR_SIMPLE_Q			(0x0)
#define MPI_SCSIIO_ATTR_HEAD_OF_Q			(0x1)
#define MPI_SCSIIO_ATTR_ORDERED_Q			(0x2)
#define MPI_SCSIIO_ATTR_ACA_Q				(0x4)
#define MPI_SCSIIO_ATTR_UNTAGGED			(0x5)
#define MPI_SCSIIO_ATTR_NO_DISCONNECT			(0x7)
	u_int8_t		reserved3;
	u_int8_t		direction;
#define MPI_SCSIIO_DIR_NONE				(0x0)
#define MPI_SCSIIO_DIR_WRITE				(0x1)
#define MPI_SCSIIO_DIR_READ				(0x2)

#define MPI_CDB_LEN					16
	u_int8_t		cdb[MPI_CDB_LEN];

	u_int32_t		data_length;

	u_int32_t		sense_buf_low_addr;

	/* followed by an sgl */
} __packed __aligned(4);

struct mpi_msg_scsi_io_error {
	u_int8_t		target_id;
	u_int8_t		bus;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		cdb_length;
	u_int8_t		sense_buf_len;
	u_int8_t		reserved1;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int8_t		scsi_status;
#if notyet
#define MPI_SCSIIO_ERR_STATUS_SUCCESS
#define MPI_SCSIIO_ERR_STATUS_CHECK_COND
#define MPI_SCSIIO_ERR_STATUS_BUSY
#define MPI_SCSIIO_ERR_STATUS_INTERMEDIATE
#define MPI_SCSIIO_ERR_STATUS_INTERMEDIATE_CONDMET
#define MPI_SCSIIO_ERR_STATUS_RESERVATION_CONFLICT
#define MPI_SCSIIO_ERR_STATUS_CMD_TERM
#define MPI_SCSIIO_ERR_STATUS_TASK_SET_FULL
#define MPI_SCSIIO_ERR_STATUS_ACA_ACTIVE
#endif
	u_int8_t		scsi_state;
#define MPI_SCSIIO_ERR_STATE_AUTOSENSE_VALID		(1<<0)
#define MPI_SCSIIO_ERR_STATE_AUTOSENSE_FAILED		(1<<2)
#define MPI_SCSIIO_ERR_STATE_NO_SCSI_STATUS		(1<<3)
#define MPI_SCSIIO_ERR_STATE_TERMINATED			(1<<4)
#define MPI_SCSIIO_ERR_STATE_RESPONSE_INFO_VALID	(1<<5)
#define MPI_SCSIIO_ERR_STATE_QUEUE_TAG_REJECTED		(1<<6)
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		transfer_count;

	u_int32_t		sense_count;

	u_int32_t		response_info;

	u_int16_t		tag;
	u_int16_t		reserved2;
} __packed __aligned(4);

struct mpi_msg_scsi_task_request {
	u_int8_t		target_id;
	u_int8_t		bus;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved1;
	u_int8_t		task_type;
#define MPI_MSG_SCSI_TASK_TYPE_ABORT_TASK		(0x01)
#define MPI_MSG_SCSI_TASK_TYPE_ABRT_TASK_SET		(0x02)
#define MPI_MSG_SCSI_TASK_TYPE_TARGET_RESET		(0x03)
#define MPI_MSG_SCSI_TASK_TYPE_RESET_BUS		(0x04)
#define MPI_MSG_SCSI_TASK_TYPE_LOGICAL_UNIT_RESET	(0x05)
	u_int8_t		reserved2;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		lun[4];

	u_int32_t		reserved3[7]; /* wtf? */

	u_int32_t		target_msg_context;
} __packed __aligned(4);

struct mpi_msg_scsi_task_reply {
	u_int8_t		target_id;
	u_int8_t		bus;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		response_code;
	u_int8_t		task_type;
	u_int8_t		reserved1;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved2;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		termination_count;
} __packed __aligned(4);

struct mpi_msg_raid_action_request {
	u_int8_t		action;
#define MPI_MSG_RAID_ACTION_STATUS			(0x00)
#define MPI_MSG_RAID_ACTION_INDICATOR_STRUCT		(0x01)
#define MPI_MSG_RAID_ACTION_CREATE_VOLUME		(0x02)
#define MPI_MSG_RAID_ACTION_DELETE_VOLUME		(0x03)
#define MPI_MSG_RAID_ACTION_DISABLE_VOLUME		(0x04)
#define MPI_MSG_RAID_ACTION_ENABLE_VOLUME		(0x05)
#define MPI_MSG_RAID_ACTION_QUIESCE_PHYSIO		(0x06)
#define MPI_MSG_RAID_ACTION_ENABLE_PHYSIO		(0x07)
#define MPI_MSG_RAID_ACTION_CH_VOL_SETTINGS		(0x08)
#define MPI_MSG_RAID_ACTION_PHYSDISK_OFFLINE		(0x0a)
#define MPI_MSG_RAID_ACTION_PHYSDISK_ONLINE		(0x0b)
#define MPI_MSG_RAID_ACTION_CH_PHYSDISK_SETTINGS	(0x0c)
#define MPI_MSG_RAID_ACTION_CREATE_PHYSDISK		(0x0d)
#define MPI_MSG_RAID_ACTION_DELETE_PHYSDISK		(0x0e)
#define MPI_MSG_RAID_ACTION_PHYSDISK_FAIL		(0x0f)
#define MPI_MSG_RAID_ACTION_ACTIVATE_VOLUME		(0x11)
#define MPI_MSG_RAID_ACTION_DEACTIVATE_VOLUME		(0x12)
#define MPI_MSG_RAID_ACTION_SET_RESYNC_RATE		(0x13)
#define MPI_MSG_RAID_ACTION_SET_SCRUB_RATE		(0x14)
#define MPI_MSG_RAID_ACTION_DEVICE_FW_UPDATE_MODE	(0x15)
#define MPI_MSG_RAID_ACTION_SET_VOL_NAME		(0x16)
	u_int8_t		_reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		vol_id;
	u_int8_t		vol_bus;
	u_int8_t		phys_disk_num;
	u_int8_t		message_flags;

	u_int32_t		msg_context;

	u_int32_t		_reserved2;

	u_int32_t		data_word;
	u_int32_t		data_sge;
} __packed __aligned(4);

struct mpi_msg_raid_action_reply {
	u_int8_t		action;
	u_int8_t		_reserved1;
	u_int8_t		message_length;
	u_int8_t		function;

	u_int8_t		vol_id;
	u_int8_t		vol_bus;
	u_int8_t		phys_disk_num;
	u_int8_t		message_flags;

	u_int32_t		message_context;

	u_int16_t		action_status;
#define MPI_RAID_ACTION_STATUS_OK			(0x0000)
#define MPI_RAID_ACTION_STATUS_INVALID			(0x0001)
#define MPI_RAID_ACTION_STATUS_FAILURE			(0x0002)
#define MPI_RAID_ACTION_STATUS_IN_PROGRESS		(0x0004)
	u_int16_t		ioc_status;

	u_int32_t		ioc_log_info;

	u_int32_t		volume_status;

	u_int32_t		action_data;
} __packed __aligned(4);

struct mpi_cfg_hdr {
	u_int8_t		page_version;
	u_int8_t		page_length;
	u_int8_t		page_number;
	u_int8_t		page_type;
#define MPI_CONFIG_REQ_PAGE_TYPE_ATTRIBUTE		(0xf0)
#define MPI_CONFIG_REQ_PAGE_TYPE_MASK			(0x0f)
#define MPI_CONFIG_REQ_PAGE_TYPE_IO_UNIT		(0x00)
#define MPI_CONFIG_REQ_PAGE_TYPE_IOC			(0x01)
#define MPI_CONFIG_REQ_PAGE_TYPE_BIOS			(0x02)
#define MPI_CONFIG_REQ_PAGE_TYPE_SCSI_SPI_PORT		(0x03)
#define MPI_CONFIG_REQ_PAGE_TYPE_SCSI_SPI_DEV		(0x04)
#define MPI_CONFIG_REQ_PAGE_TYPE_FC_PORT		(0x05)
#define MPI_CONFIG_REQ_PAGE_TYPE_FC_DEV			(0x06)
#define MPI_CONFIG_REQ_PAGE_TYPE_LAN			(0x07)
#define MPI_CONFIG_REQ_PAGE_TYPE_RAID_VOL		(0x08)
#define MPI_CONFIG_REQ_PAGE_TYPE_MANUFACTURING		(0x09)
#define MPI_CONFIG_REQ_PAGE_TYPE_RAID_PD		(0x0A)
#define MPI_CONFIG_REQ_PAGE_TYPE_INBAND			(0x0B)
#define MPI_CONFIG_REQ_PAGE_TYPE_EXTENDED		(0x0F)
} __packed __aligned(4);

struct mpi_ecfg_hdr {
	u_int8_t		page_version;
	u_int8_t		reserved1;
	u_int8_t		page_number;
	u_int8_t		page_type;

	u_int16_t		ext_page_length;
	u_int8_t		ext_page_type;
	u_int8_t		reserved2;
} __packed __aligned(4);

struct mpi_msg_config_request {
	u_int8_t		action;
#define MPI_CONFIG_REQ_ACTION_PAGE_HEADER		(0x00)
#define MPI_CONFIG_REQ_ACTION_PAGE_READ_CURRENT		(0x01)
#define MPI_CONFIG_REQ_ACTION_PAGE_WRITE_CURRENT	(0x02)
#define MPI_CONFIG_REQ_ACTION_PAGE_DEFAULT		(0x03)
#define MPI_CONFIG_REQ_ACTION_PAGE_WRITE_NVRAM		(0x04)
#define MPI_CONFIG_REQ_ACTION_PAGE_READ_DEFAULT		(0x05)
#define MPI_CONFIG_REQ_ACTION_PAGE_READ_NVRAM		(0x06)
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		ext_page_len;
	u_int8_t		ext_page_type;
#define MPI_CONFIG_REQ_EXTPAGE_TYPE_SAS_IO_UNIT		(0x10)
#define MPI_CONFIG_REQ_EXTPAGE_TYPE_SAS_EXPANDER	(0x11)
#define MPI_CONFIG_REQ_EXTPAGE_TYPE_SAS_DEVICE		(0x12)
#define MPI_CONFIG_REQ_EXTPAGE_TYPE_SAS_PHY		(0x13)
#define MPI_CONFIG_REQ_EXTPAGE_TYPE_LOG			(0x14)
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int32_t		reserved2[2];

	struct mpi_cfg_hdr	config_header;

	u_int32_t		page_address;
/* XXX lots of defns here */

	struct mpi_sge		page_buffer;
} __packed __aligned(4);

struct mpi_msg_config_reply {
	u_int8_t		action;
	u_int8_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		ext_page_length;
	u_int8_t		ext_page_type;
	u_int8_t		msg_flags;

	u_int32_t		msg_context;

	u_int16_t		reserved2;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	struct mpi_cfg_hdr	config_header;
} __packed __aligned(4);

struct mpi_cfg_spi_port_pg0 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		capabilities1;
#define MPI_CFG_SPI_PORT_0_CAPABILITIES_PACKETIZED	(1<<0)
#define MPI_CFG_SPI_PORT_0_CAPABILITIES_DT		(1<<1)
#define MPI_CFG_SPI_PORT_0_CAPABILITIES_QAS		(1<<2)
	u_int8_t		min_period;
	u_int8_t		max_offset;
	u_int8_t		capabilities2;
#define MPI_CFG_SPI_PORT_0_CAPABILITIES_IDP		(1<<3)
#define MPI_CFG_SPI_PORT_0_CAPABILITIES_WIDTH		(1<<5)
#define  MPI_CFG_SPI_PORT_0_CAPABILITIES_WIDTH_NARROW	(0<<5)
#define  MPI_CFG_SPI_PORT_0_CAPABILITIES_WIDTH_WIDE	(1<<5)
#define MPI_CFG_SPI_PORT_0_CAPABILITIES_AIP		(1<<7)

	u_int8_t		signalling_type;
#define MPI_CFG_SPI_PORT_0_SIGNAL_HVD			(0x1)
#define MPI_CFG_SPI_PORT_0_SIGNAL_SE			(0x2)
#define MPI_CFG_SPI_PORT_0_SIGNAL_LVD			(0x3)
	u_int16_t		reserved;
	u_int8_t		connected_id;
#define  MPI_CFG_SPI_PORT_0_CONNECTEDID_BUSFREE		(0xfe)
#define  MPI_CFG_SPI_PORT_0_CONNECTEDID_UNKNOWN		(0xff)
} __packed __aligned(4);

struct mpi_cfg_spi_port_pg1 {
	struct mpi_cfg_hdr	config_header;

	/* configuration */
	u_int8_t		port_scsi_id;
	u_int8_t		reserved1;
	u_int16_t		port_resp_ids;

	u_int32_t		on_bus_timer_value;

	u_int8_t		target_config;
#define MPI_CFG_SPI_PORT_1_TARGCFG_TARGET_ONLY		(0x01)
#define MPI_CFG_SPI_PORT_1_TARGCFG_INIT_TARGET		(0x02)
	u_int8_t		reserved2;
	u_int16_t		id_config;
} __packed __aligned(4);

struct mpi_cfg_spi_port_pg2 {
	struct mpi_cfg_hdr	config_header;

	u_int32_t		port_flags;
#define MPI_CFG_SPI_PORT_2_PORT_FLAGS_SCAN_HI2LOW	(1<<0)
#define MPI_CFG_SPI_PORT_2_PORT_FLAGS_AVOID_RESET	(1<<2)
#define MPI_CFG_SPI_PORT_2_PORT_FLAGS_ALT_CHS		(1<<3)
#define MPI_CFG_SPI_PORT_2_PORT_FLAGS_TERM_DISABLED	(1<<4)
#define MPI_CFG_SPI_PORT_2_PORT_FLAGS_DV_CTL		(0x3<<5)
#define  MPI_CFG_SPI_PORT_2_PORT_FLAGS_DV_HOST_BE	(0x0<<5)
#define  MPI_CFG_SPI_PORT_2_PORT_FLAGS_DV_HOST_B	(0x1<<5)
#define  MPI_CFG_SPI_PORT_2_PORT_FLAGS_DV_HOST_NONE	(0x3<<5)

	u_int32_t		port_settings;
#define MPI_CFG_SPI_PORT_2_PORT_SET_HOST_ID		(0x7<<0)
#define MPI_CFG_SPI_PORT_2_PORT_SET_INIT_HBA		(0x3<<4)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_INIT_HBA_DISABLED	(0x0<<4)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_INIT_HBA_BIOS	(0x1<<4)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_INIT_HBA_OS	(0x2<<4)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_INIT_HBA_BIOS_OS	(0x3<<4)
#define MPI_CFG_SPI_PORT_2_PORT_SET_REMOVABLE		(0x3<<6)
#define MPI_CFG_SPI_PORT_2_PORT_SET_SPINUP_DELAY	(0xf<<8)
#define MPI_CFG_SPI_PORT_2_PORT_SET_SYNC		(0x3<<12)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_NEG_SUPPORTED	(0x0<<12)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_NEG_NONE		(0x1<<12)
#define  MPI_CFG_SPI_PORT_2_PORT_SET_NEG_ALL		(0x3<<12)

	struct {
		u_int8_t		timeout;
		u_int8_t		sync_factor;
		u_int16_t		device_flags;
#define MPI_CFG_SPI_PORT_2_DEV_FLAG_DISCONNECT_EN	(1<<0)
#define MPI_CFG_SPI_PORT_2_DEV_FLAG_SCAN_ID_EN		(1<<1)
#define MPI_CFG_SPI_PORT_2_DEV_FLAG_SCAN_LUN_EN		(1<<2)
#define MPI_CFG_SPI_PORT_2_DEV_FLAG_TAQ_Q_EN		(1<<3)
#define MPI_CFG_SPI_PORT_2_DEV_FLAG_WIDE_DIS		(1<<4)
#define MPI_CFG_SPI_PORT_2_DEV_FLAG_BOOT_CHOICE		(1<<5)
	} __packed		device_settings[16];
};

struct mpi_cfg_spi_dev_pg0 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		neg_params1;
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_PACKETIZED		(1<<0)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_DUALXFERS		(1<<1)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_QAS			(1<<2)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_HOLD_MCS		(1<<3)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_WR_FLOW		(1<<4)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_RD_STRM		(1<<5)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_RTI			(1<<6)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_PCOMP_EN		(1<<7)
	u_int8_t		neg_period;
	u_int8_t		neg_offset;
	u_int8_t		neg_params2;
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_IDP_EN		(1<<3)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_WIDTH		(1<<5)
#define  MPI_CFG_SPI_DEV_0_NEGPARAMS_WIDTH_NARROW	(0<<5)
#define  MPI_CFG_SPI_DEV_0_NEGPARAMS_WIDTH_WIDE		(1<<5)
#define MPI_CFG_SPI_DEV_0_NEGPARAMS_AIP			(1<<7)

	u_int32_t		information;
#define MPI_CFG_SPI_DEV_0_INFO_NEG_OCCURRED		(1<<0)
#define MPI_CFG_SPI_DEV_0_INFO_SDTR_REJECTED		(1<<1)
#define MPI_CFG_SPI_DEV_0_INFO_WDTR_REJECTED		(1<<2)
#define MPI_CFG_SPI_DEV_0_INFO_PPR_REJECTED		(1<<3)
} __packed __aligned(4);

struct mpi_cfg_spi_dev_pg1 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		req_params1;
#define MPI_CFG_SPI_DEV_1_REQPARAMS_PACKETIZED		(1<<0)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_DUALXFERS		(1<<1)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_QAS			(1<<2)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_HOLD_MCS		(1<<3)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_WR_FLOW		(1<<4)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_RD_STRM		(1<<5)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_RTI			(1<<6)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_PCOMP_EN		(1<<7)
	u_int8_t		req_period;
	u_int8_t		req_offset;
	u_int8_t		req_params2;
#define MPI_CFG_SPI_DEV_1_REQPARAMS_IDP_EN		(1<<3)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_WIDTH		(1<<5)
#define  MPI_CFG_SPI_DEV_1_REQPARAMS_WIDTH_NARROW	(0<<5)
#define  MPI_CFG_SPI_DEV_1_REQPARAMS_WIDTH_WIDE		(1<<5)
#define MPI_CFG_SPI_DEV_1_REQPARAMS_AIP			(1<<7)

	u_int32_t		reserved;

	u_int32_t		configuration;
#define MPI_CFG_SPI_DEV_1_CONF_WDTR_DISALLOWED		(1<<1)
#define MPI_CFG_SPI_DEV_1_CONF_SDTR_DISALLOWED		(1<<2)
#define MPI_CFG_SPI_DEV_1_CONF_EXTPARAMS		(1<<3)
#define MPI_CFG_SPI_DEV_1_CONF_FORCE_PPR		(1<<4)
} __packed __aligned(4);

struct mpi_cfg_spi_dev_pg2 {
	struct mpi_cfg_hdr	config_header;

	u_int32_t		domain_validation;
#define MPI_CFG_SPI_DEV_2_DV_ISI_ENABLED		(1<<4)
#define MPI_CFG_SPI_DEV_2_DV_SECONDARY_DRV_EN		(1<<5)
#define MPI_CFG_SPI_DEV_2_DV_SLEW_RATE_CTL		(0x7<<7)
#define MPI_CFG_SPI_DEV_2_DV_PRIMARY_DRV_STRENGTH	(0x7<<10)
#define MPI_CFG_SPI_DEV_2_DV_XCLKH_ST			(1<<28)
#define MPI_CFG_SPI_DEV_2_DV_XCLKS_ST			(1<<29)
#define MPI_CFG_SPI_DEV_2_DV_XCLKH_DT			(1<<30)
#define MPI_CFG_SPI_DEV_2_DV_XCLKS_DT			(1<<31)

	u_int32_t		parity_pipe_select;
#define MPI_CFG_SPI_DEV_2_PARITY_PIPE_SELECT		(0x3)

	u_int32_t		data_pipe_select;
#define MPI_CFG_SPI_DEV_2_DATA_PIPE_SELECT(x)		(0x3<<((x)*2))

} __packed __aligned(4);

struct mpi_cfg_spi_dev_pg3 {
	struct mpi_cfg_hdr	config_header;

	u_int16_t		msg_reject_count;
	u_int16_t		phase_error_count;

	u_int16_t		parity_error_count;
	u_int16_t		reserved;
} __packed __aligned(4);

struct mpi_cfg_manufacturing_pg0 {
	struct mpi_cfg_hdr	config_header;

	char			chip_name[16];
	char			chip_revision[8];
	char			board_name[16];
	char			board_assembly[16];
	char			board_tracer_number[16];
} __packed __aligned(4);

struct mpi_cfg_ioc_pg1 {
	struct mpi_cfg_hdr	config_header;

	u_int32_t		flags;
#define MPI_CFG_IOC_1_REPLY_COALESCING			(1<<0)
#define MPI_CFG_IOC_1_CTX_REPLY_DISABLE			(1<<4)

	u_int32_t		coalescing_timeout;

	u_int8_t		coalescing_depth;
	u_int8_t		pci_slot_num;
	u_int8_t		_reserved[2];
} __packed __aligned(4);

struct mpi_cfg_ioc_pg2 {
	struct mpi_cfg_hdr	config_header;

	u_int32_t		capabilities;
#define MPI_CFG_IOC_2_CAPABILITIES_IS			(1<<0)
#define MPI_CFG_IOC_2_CAPABILITIES_IME			(1<<1)
#define MPI_CFG_IOC_2_CAPABILITIES_IM			(1<<2)
#define  MPI_CFG_IOC_2_CAPABILITIES_RAID		( \
    MPI_CFG_IOC_2_CAPABILITIES_IS | MPI_CFG_IOC_2_CAPABILITIES_IME | \
    MPI_CFG_IOC_2_CAPABILITIES_IM)
#define MPI_CFG_IOC_2_CAPABILITIES_SES			(1<<29)
#define MPI_CFG_IOC_2_CAPABILITIES_SAFTE		(1<<30)
#define MPI_CFG_IOC_2_CAPABILITIES_XCHANNEL		(1<<31)

	u_int8_t		active_vols;
	u_int8_t		max_vols;
	u_int8_t		active_physdisks;
	u_int8_t		max_physdisks;

	/* followed by a list of mpi_cfg_raid_vol structs */
} __packed __aligned(4);

struct mpi_cfg_raid_vol {
	u_int8_t		vol_id;
	u_int8_t		vol_bus;
	u_int8_t		vol_ioc;
	u_int8_t		vol_page;

	u_int8_t		vol_type;
#define MPI_CFG_RAID_TYPE_RAID_IS			(0x00)
#define MPI_CFG_RAID_TYPE_RAID_IME			(0x01)
#define MPI_CFG_RAID_TYPE_RAID_IM			(0x02)
#define MPI_CFG_RAID_TYPE_RAID_5			(0x03)
#define MPI_CFG_RAID_TYPE_RAID_6			(0x04)
#define MPI_CFG_RAID_TYPE_RAID_10			(0x05)
#define MPI_CFG_RAID_TYPE_RAID_50			(0x06)
	u_int8_t		flags;
#define MPI_CFG_RAID_VOL_INACTIVE	(1<<3)
	u_int16_t		reserved;
} __packed __aligned(4);

struct mpi_cfg_ioc_pg3 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		no_phys_disks;
	u_int8_t		reserved[3];

	/* followed by a list of mpi_cfg_raid_physdisk structs */
} __packed __aligned(4);

struct mpi_cfg_raid_physdisk {
	u_int8_t		phys_disk_id;
	u_int8_t		phys_disk_bus;
	u_int8_t		phys_disk_ioc;
	u_int8_t		phys_disk_num;
} __packed __aligned(4);

struct mpi_cfg_fc_port_pg0 {
	struct mpi_cfg_hdr	config_header;

	u_int32_t		flags;

	u_int8_t		mpi_port_nr;
	u_int8_t		link_type;
	u_int8_t		port_state;
	u_int8_t		reserved1;

	u_int32_t		port_id;

	u_int64_t		wwnn;

	u_int64_t		wwpn;

	u_int32_t		supported_service_class;

	u_int32_t		supported_speeds;

	u_int32_t		current_speed;

	u_int32_t		max_frame_size;

	u_int64_t		fabric_wwnn;

	u_int64_t		fabric_wwpn;

	u_int32_t		discovered_port_count;

	u_int32_t		max_initiators;

	u_int8_t		max_aliases_supported;
	u_int8_t		max_hard_aliases_supported;
	u_int8_t		num_current_aliases;
	u_int8_t		reserved2;
} __packed __aligned(4);

struct mpi_cfg_fc_port_pg1 {
	struct mpi_cfg_hdr	config_header;

	u_int32_t		flags;
#define MPI_CFG_FC_PORT_0_FLAGS_MAP_BY_D_ID		(1<<0)
#define MPI_CFG_FC_PORT_0_FLAGS_MAINTAIN_LOGINS		(1<<1)
#define MPI_CFG_FC_PORT_0_FLAGS_PLOGI_AFTER_LOGO	(1<<2)
#define MPI_CFG_FC_PORT_0_FLAGS_SUPPRESS_PROT_REG	(1<<3)
#define MPI_CFG_FC_PORT_0_FLAGS_MASK_RR_TOV_UNITS	(0x7<<4)
#define MPI_CFG_FC_PORT_0_FLAGS_MASK_RR_TOV_UNIT_NONE		(0x0<<4)
#define MPI_CFG_FC_PORT_0_FLAGS_MASK_RR_TOV_UNIT_0_001_SEC	(0x1<<4)
#define MPI_CFG_FC_PORT_0_FLAGS_MASK_RR_TOV_UNIT_0_1_SEC	(0x3<<4)
#define MPI_CFG_FC_PORT_0_FLAGS_MASK_RR_TOV_UNIT_10_SEC		(0x5<<4)
#define MPI_CFG_FC_PORT_0_FLAGS_TGT_LARGE_CDB_EN	(1<<7)
#define MPI_CFG_FC_PORT_0_FLAGS_SOFT_ALPA_FALLBACK	(1<<21)
#define MPI_CFG_FC_PORT_0_FLAGS_PORT_OFFLINE		(1<<22)
#define MPI_CFG_FC_PORT_0_FLAGS_TGT_MODE_OXID		(1<<23)
#define MPI_CFG_FC_PORT_0_FLAGS_VERBOSE_RESCAN		(1<<24)
#define MPI_CFG_FC_PORT_0_FLAGS_FORCE_NOSEEPROM_WWNS	(1<<25)
#define MPI_CFG_FC_PORT_0_FLAGS_IMMEDIATE_ERROR		(1<<26)
#define MPI_CFG_FC_PORT_0_FLAGS_EXT_FCP_STATUS_EN	(1<<27)
#define MPI_CFG_FC_PORT_0_FLAGS_REQ_PROT_LOG_BUS_ADDR	(1<<28)
#define MPI_CFG_FC_PORT_0_FLAGS_REQ_PROT_LAN		(1<<29)
#define MPI_CFG_FC_PORT_0_FLAGS_REQ_PROT_TARGET		(1<<30)
#define MPI_CFG_FC_PORT_0_FLAGS_REQ_PROT_INITIATOR	(1<<31)

	u_int64_t		noseepromwwnn;

	u_int64_t		noseepromwwpn;

	u_int8_t		hard_alpa;
	u_int8_t		link_config;
	u_int8_t		topology_config;
	u_int8_t		alt_connector;

	u_int8_t		num_req_aliases;
	u_int8_t		rr_tov;
	u_int8_t		initiator_dev_to;
	u_int8_t		initiator_lo_pend_to;
} __packed __aligned(4);

struct mpi_cfg_fc_device_pg0 {
	struct mpi_cfg_hdr	config_header;

	u_int64_t		wwnn;

	u_int64_t		wwpn;

	u_int32_t		port_id;

	u_int8_t		protocol;
	u_int8_t		flags;
#define MPI_CFG_FC_DEV_0_FLAGS_BUSADDR_VALID		(1<<0)
#define MPI_CFG_FC_DEV_0_FLAGS_PLOGI_INVALID		(1<<1)
#define MPI_CFG_FC_DEV_0_FLAGS_PRLI_INVALID		(1<<2)
	u_int16_t		bb_credit;

	u_int16_t		max_rx_frame_size;
	u_int8_t		adisc_hard_alpa;
	u_int8_t		port_nr;

	u_int8_t		fc_ph_low_version;
	u_int8_t		fc_ph_high_version;
	u_int8_t		current_target_id;
	u_int8_t		current_bus;
} __packed __aligned(4);

struct mpi_raid_settings {
	u_int16_t		volume_settings;
#define MPI_CFG_RAID_VOL_0_SETTINGS_WRITE_CACHE_EN	(1<<0)
#define MPI_CFG_RAID_VOL_0_SETTINGS_OFFLINE_SMART_ERR	(1<<1)
#define MPI_CFG_RAID_VOL_0_SETTINGS_OFFLINE_SMART	(1<<2)
#define MPI_CFG_RAID_VOL_0_SETTINGS_AUTO_SWAP		(1<<3)
#define MPI_CFG_RAID_VOL_0_SETTINGS_HI_PRI_RESYNC	(1<<4)
#define MPI_CFG_RAID_VOL_0_SETTINGS_PROD_SUFFIX		(1<<5)
#define MPI_CFG_RAID_VOL_0_SETTINGS_FAST_SCRUB		(1<<6) /* obsolete */
#define MPI_CFG_RAID_VOL_0_SETTINGS_DEFAULTS		(1<<15)
	u_int8_t		hot_spare_pool;
	u_int8_t		reserved2;
} __packed __aligned(4);

struct mpi_cfg_raid_vol_pg0 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		volume_id;
	u_int8_t		volume_bus;
	u_int8_t		volume_ioc;
	u_int8_t		volume_type;

	u_int8_t		volume_status;
#define MPI_CFG_RAID_VOL_0_STATUS_ENABLED		(1<<0)
#define MPI_CFG_RAID_VOL_0_STATUS_QUIESCED		(1<<1)
#define MPI_CFG_RAID_VOL_0_STATUS_RESYNCING		(1<<2)
#define MPI_CFG_RAID_VOL_0_STATUS_ACTIVE		(1<<3)
#define MPI_CFG_RAID_VOL_0_STATUS_BADBLOCK_FULL		(1<<4)
	u_int8_t		volume_state;
#define MPI_CFG_RAID_VOL_0_STATE_OPTIMAL		(0x00)
#define MPI_CFG_RAID_VOL_0_STATE_DEGRADED		(0x01)
#define MPI_CFG_RAID_VOL_0_STATE_FAILED			(0x02)
#define MPI_CFG_RAID_VOL_0_STATE_MISSING		(0x03)
	u_int16_t		_reserved1;

	struct mpi_raid_settings settings;

	u_int32_t		max_lba;

	u_int32_t		_reserved2;

	u_int32_t		stripe_size;

	u_int32_t		_reserved3;

	u_int32_t		_reserved4;

	u_int8_t		num_phys_disks;
	u_int8_t		data_scrub_rate;
	u_int8_t		resync_rate;
	u_int8_t		inactive_status;
#define MPI_CFG_RAID_VOL_0_INACTIVE_UNKNOWN		(0x00)
#define MPI_CFG_RAID_VOL_0_INACTIVE_STALE_META		(0x01)
#define MPI_CFG_RAID_VOL_0_INACTIVE_FOREIGN_VOL		(0x02)
#define MPI_CFG_RAID_VOL_0_INACTIVE_NO_RESOURCES	(0x03)
#define MPI_CFG_RAID_VOL_0_INACTIVE_CLONED_VOL		(0x04)
#define MPI_CFG_RAID_VOL_0_INACTIVE_INSUF_META		(0x05)

	/* followed by a list of mpi_cfg_raid_vol_pg0_physdisk structs */
} __packed __aligned(4);

struct mpi_cfg_raid_vol_pg0_physdisk {
	u_int16_t		reserved;
	u_int8_t		phys_disk_map;
	u_int8_t		phys_disk_num;
} __packed __aligned(4);

struct mpi_cfg_raid_vol_pg1 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		volume_id;
	u_int8_t		volume_bus;
	u_int8_t		volume_ioc;
	u_int8_t		reserved1;

	u_int8_t		guid[24];

	u_int8_t		name[32];

	u_int64_t		wwid;

	u_int32_t		reserved2;

	u_int32_t		reserved3;
} __packed __aligned(4);

struct mpi_cfg_raid_physdisk_pg0 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		phys_disk_id;
	u_int8_t		phys_disk_bus;
	u_int8_t		phys_disk_ioc;
	u_int8_t		phys_disk_num;

	u_int8_t		enc_id;
	u_int8_t		enc_bus;
	u_int8_t		hot_spare_pool;
	u_int8_t		enc_type;
#define MPI_CFG_RAID_PHYDISK_0_ENCTYPE_NONE		(0x0)
#define MPI_CFG_RAID_PHYDISK_0_ENCTYPE_SAFTE		(0x1)
#define MPI_CFG_RAID_PHYDISK_0_ENCTYPE_SES		(0x2)

	u_int32_t		reserved1;

	u_int8_t		ext_disk_id[8];

	u_int8_t		disk_id[16];

	u_int8_t		vendor_id[8];

	u_int8_t		product_id[16];

	u_int8_t		product_rev[4];

	u_int8_t		info[32];

	u_int8_t		phys_disk_status;
#define MPI_CFG_RAID_PHYDISK_0_STATUS_OUTOFSYNC		(1<<0)
#define MPI_CFG_RAID_PHYDISK_0_STATUS_QUIESCED		(1<<1)
	u_int8_t		phys_disk_state;
#define MPI_CFG_RAID_PHYDISK_0_STATE_ONLINE		(0x00)
#define MPI_CFG_RAID_PHYDISK_0_STATE_MISSING		(0x01)
#define MPI_CFG_RAID_PHYDISK_0_STATE_INCOMPAT		(0x02)
#define MPI_CFG_RAID_PHYDISK_0_STATE_FAILED		(0x03)
#define MPI_CFG_RAID_PHYDISK_0_STATE_INIT		(0x04)
#define MPI_CFG_RAID_PHYDISK_0_STATE_OFFLINE		(0x05)
#define MPI_CFG_RAID_PHYDISK_0_STATE_HOSTFAIL		(0x06)
#define MPI_CFG_RAID_PHYDISK_0_STATE_OTHER		(0xff)
	u_int16_t		reserved2;

	u_int32_t		max_lba;

	u_int8_t		error_cdb_byte;
	u_int8_t		error_sense_key;
	u_int16_t		reserved3;

	u_int16_t		error_count;
	u_int8_t		error_asc;
	u_int8_t		error_ascq;

	u_int16_t		smart_count;
	u_int8_t		smart_asc;
	u_int8_t		smart_ascq;
} __packed __aligned(4);

struct mpi_cfg_raid_physdisk_pg1 {
	struct mpi_cfg_hdr	config_header;

	u_int8_t		num_phys_disk_paths;
	u_int8_t		phys_disk_num;
	u_int16_t		reserved1;

	u_int32_t		reserved2;

	/* followed by mpi_cfg_raid_physdisk_path structs */
} __packed __aligned(4);

struct mpi_cfg_raid_physdisk_path {
	u_int8_t		phys_disk_id;
	u_int8_t		phys_disk_bus;
	u_int16_t		reserved1;

	u_int64_t		wwwid;

	u_int64_t		owner_wwid;

	u_int8_t		ownder_id;
	u_int8_t		reserved2;
	u_int16_t		flags;
#define MPI_CFG_RAID_PHYDISK_PATH_INVALID		(1<<0)
#define MPI_CFG_RAID_PHYDISK_PATH_BROKEN		(1<<1)
} __packed __aligned(4);

struct mpi_cfg_sas_iou_pg0 {
	struct mpi_ecfg_hdr	config_header;

	u_int16_t		nvdata_version_default;
	u_int16_t		nvdata_version_persistent;

	u_int8_t		num_phys;
	u_int8_t		_reserved1[3];

	/* followed by mpi_cfg_sas_iou_pg0_phy structs */
} __packed __aligned(4);

struct mpi_cfg_sas_iou_pg0_phy {
	u_int8_t		port;
	u_int8_t		port_flags;
	u_int8_t		phy_flags;
	u_int8_t		negotiated_link_rate;

	u_int32_t		controller_phy_dev_info;

	u_int16_t		attached_dev_handle;
	u_int16_t		controller_dev_handle;

	u_int32_t		discovery_status;
} __packed __aligned(4);

struct mpi_cfg_sas_iou_pg1 {
	struct mpi_ecfg_hdr	config_header;

	u_int16_t		control_flags;
	u_int16_t		max_sata_targets;

	u_int16_t		additional_control_flags;
	u_int16_t		_reserved1;

	u_int8_t		num_phys;
	u_int8_t		max_sata_q_depth;
	u_int8_t		report_dev_missing_delay;
	u_int8_t		io_dev_missing_delay;

	/* followed by mpi_cfg_sas_iou_pg1_phy structs */
} __packed __aligned(4);

struct mpi_cfg_sas_iou_pg1_phy {
	u_int8_t		port;
	u_int8_t		port_flags;
	u_int8_t		phy_flags;
	u_int8_t		max_min_link_rate;

	u_int32_t		controller_phy_dev_info;

	u_int16_t		max_target_port_connect_time;
	u_int16_t		_reserved1;
} __packed __aligned(4);

#define MPI_CFG_SAS_DEV_ADDR_NEXT		(0<<28)
#define MPI_CFG_SAS_DEV_ADDR_BUS		(1<<28)
#define MPI_CFG_SAS_DEV_ADDR_HANDLE		(2<<28)

struct mpi_cfg_sas_dev_pg0 {
	struct mpi_ecfg_hdr	config_header;

	u_int16_t		slot;
	u_int16_t		enc_handle;

	u_int64_t		sas_addr;

	u_int16_t		parent_dev_handle;
	u_int8_t		phy_num;
	u_int8_t		access_status;

	u_int16_t		dev_handle;
	u_int8_t		target;
	u_int8_t		bus;

	u_int32_t		device_info;
#define MPI_CFG_SAS_DEV_0_DEVINFO_TYPE			(0x7)
#define MPI_CFG_SAS_DEV_0_DEVINFO_TYPE_NONE		(0x0)
#define MPI_CFG_SAS_DEV_0_DEVINFO_TYPE_END		(0x1)
#define MPI_CFG_SAS_DEV_0_DEVINFO_TYPE_EDGE_EXPANDER	(0x2)
#define MPI_CFG_SAS_DEV_0_DEVINFO_TYPE_FANOUT_EXPANDER	(0x3)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SATA_HOST		(1<<3)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SMP_INITIATOR		(1<<4)
#define MPI_CFG_SAS_DEV_0_DEVINFO_STP_INITIATOR		(1<<5)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SSP_INITIATOR		(1<<6)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SATA_DEVICE		(1<<7)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SMP_TARGET		(1<<8)
#define MPI_CFG_SAS_DEV_0_DEVINFO_STP_TARGET		(1<<9)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SSP_TARGET		(1<<10)
#define MPI_CFG_SAS_DEV_0_DEVINFO_DIRECT_ATTACHED	(1<<11)
#define MPI_CFG_SAS_DEV_0_DEVINFO_LSI_DEVICE		(1<<12)
#define MPI_CFG_SAS_DEV_0_DEVINFO_ATAPI_DEVICE		(1<<13)
#define MPI_CFG_SAS_DEV_0_DEVINFO_SEP_DEVICE		(1<<14)

	u_int16_t		flags;
#define MPI_CFG_SAS_DEV_0_FLAGS_DEV_PRESENT		(1<<0)
#define MPI_CFG_SAS_DEV_0_FLAGS_DEV_MAPPED		(1<<1)
#define MPI_CFG_SAS_DEV_0_FLAGS_DEV_MAPPED_PERSISTENT	(1<<2)
#define MPI_CFG_SAS_DEV_0_FLAGS_SATA_PORT_SELECTOR	(1<<3)
#define MPI_CFG_SAS_DEV_0_FLAGS_SATA_FUA		(1<<4)
#define MPI_CFG_SAS_DEV_0_FLAGS_SATA_NCQ		(1<<5)
#define MPI_CFG_SAS_DEV_0_FLAGS_SATA_SMART		(1<<6)
#define MPI_CFG_SAS_DEV_0_FLAGS_SATA_LBA48		(1<<7)
#define MPI_CFG_SAS_DEV_0_FLAGS_UNSUPPORTED		(1<<8)
#define MPI_CFG_SAS_DEV_0_FLAGS_SATA_SETTINGS		(1<<9)
	u_int8_t		physical_port;
	u_int8_t		reserved;
} __packed __aligned(4);
