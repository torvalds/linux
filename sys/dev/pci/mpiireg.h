/*	$OpenBSD: mpiireg.h,v 1.15 2019/12/29 09:34:43 jmatthew Exp $	*/
/*
 * Copyright (c) 2010 Mike Belopuhov
 * Copyright (c) 2009 James Giannoules
 * Copyright (c) 2005 - 2010 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2005 - 2010 Marco Peereboom <marco@openbsd.org>
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

#define MPII_DOORBELL			(0x00)
/* doorbell read bits */
#define MPII_DOORBELL_STATE		(0xf<<28) /* ioc state */
#define  MPII_DOORBELL_STATE_RESET	(0x0<<28)
#define  MPII_DOORBELL_STATE_READY	(0x1<<28)
#define  MPII_DOORBELL_STATE_OPER	(0x2<<28)
#define  MPII_DOORBELL_STATE_FAULT	(0x4<<28)
#define  MPII_DOORBELL_INUSE		(0x1<<27) /* doorbell used */
#define MPII_DOORBELL_WHOINIT		(0x7<<24) /* last to reset ioc */
#define  MPII_DOORBELL_WHOINIT_NOONE	(0x0<<24) /* not initialized */
#define  MPII_DOORBELL_WHOINIT_SYSBIOS	(0x1<<24) /* system bios */
#define  MPII_DOORBELL_WHOINIT_ROMBIOS	(0x2<<24) /* rom bios */
#define  MPII_DOORBELL_WHOINIT_PCIPEER	(0x3<<24) /* pci peer */
#define  MPII_DOORBELL_WHOINIT_DRIVER	(0x4<<24) /* host driver */
#define  MPII_DOORBELL_WHOINIT_MANUFACT	(0x5<<24) /* manufacturing */
#define MPII_DOORBELL_FAULT		(0xffff<<0) /* fault code */
/* doorbell write bits */
#define MPII_DOORBELL_FUNCTION_SHIFT	(24)
#define MPII_DOORBELL_FUNCTION_MASK	(0xff << MPII_DOORBELL_FUNCTION_SHIFT)
#define MPII_DOORBELL_FUNCTION(x)	\
    (((x) << MPII_DOORBELL_FUNCTION_SHIFT) & MPII_DOORBELL_FUNCTION_MASK)
#define MPII_DOORBELL_DWORDS_SHIFT	16
#define MPII_DOORBELL_DWORDS_MASK	(0xff << MPII_DOORBELL_DWORDS_SHIFT)
#define MPII_DOORBELL_DWORDS(x)		\
    (((x) << MPII_DOORBELL_DWORDS_SHIFT) & MPII_DOORBELL_DWORDS_MASK)
#define MPII_DOORBELL_DATA_MASK		(0xffff)

#define MPII_WRITESEQ			(0x04)
#define  MPII_WRITESEQ_KEY_VALUE_MASK	(0x0000000f) /* key value */
#define  MPII_WRITESEQ_FLUSH		(0x00)
#define  MPII_WRITESEQ_1		(0x0f)
#define  MPII_WRITESEQ_2		(0x04)
#define  MPII_WRITESEQ_3		(0x0b)
#define  MPII_WRITESEQ_4		(0x02)
#define  MPII_WRITESEQ_5		(0x07)
#define  MPII_WRITESEQ_6		(0x0d)

#define MPII_HOSTDIAG			(0x08)
#define  MPII_HOSTDIAG_BDS_MASK		(0x00001800) /* boot device select */
#define   MPII_HOSTDIAG_BDS_DEFAULT	(0<<11)	/* default address map, flash */
#define   MPII_HOSTDIAG_BDS_HCDW	(1<<11)	/* host code and data window */
#define  MPII_HOSTDIAG_CLEARFBS		(1<<10) /* clear flash bad sig */
#define  MPII_HOSTDIAG_FORCE_HCB_ONBOOT (1<<9)	/* force host controlled boot */
#define  MPII_HOSTDIAG_HCB_MODE		(1<<8)	/* host controlled boot mode */
#define  MPII_HOSTDIAG_DWRE		(1<<7)	/* diag reg write enabled */
#define  MPII_HOSTDIAG_FBS		(1<<6)	/* flash bad sig */
#define  MPII_HOSTDIAG_RESET_HIST	(1<<5)	/* reset history */
#define  MPII_HOSTDIAG_DIAGWR_EN	(1<<4)	/* diagnostic write enabled */
#define  MPII_HOSTDIAG_RESET_ADAPTER	(1<<2)	/* reset adapter */
#define  MPII_HOSTDIAG_HOLD_IOC_RESET	(1<<1)	/* hold ioc in reset */
#define  MPII_HOSTDIAG_DIAGMEM_EN	(1<<0)	/* diag mem enable */

#define MPII_DIAGRWDATA			(0x10)

#define MPII_DIAGRWADDRLOW		(0x14)

#define MPII_DIAGRWADDRHIGH		(0x18)

#define MPII_INTR_STATUS		(0x30)
#define  MPII_INTR_STATUS_SYS2IOCDB	(1<<31) /* ioc written to by host */
#define  MPII_INTR_STATUS_RESET		(1<<30) /* physical ioc reset */
#define  MPII_INTR_STATUS_REPLY		(1<<3)	/* reply message interrupt */
#define  MPII_INTR_STATUS_IOC2SYSDB	(1<<0)	/* ioc write to doorbell */

#define MPII_INTR_MASK			(0x34)
#define  MPII_INTR_MASK_RESET		(1<<30) /* ioc reset intr mask */
#define  MPII_INTR_MASK_REPLY		(1<<3)	/* reply message intr mask */
#define  MPII_INTR_MASK_DOORBELL	(1<<0)	/* doorbell interrupt mask */

#define MPII_DCR_DATA			(0x38)

#define MPII_DCR_ADDRESS		(0x3c)

#define MPII_REPLY_FREE_HOST_INDEX	(0x48)

#define MPII_REPLY_POST_HOST_INDEX	(0x6c)

#define MPII_HCB_SIZE			(0x74)

#define MPII_HCB_ADDRESS_LOW		(0x78)
#define MPII_HCB_ADDRESS_HIGH		(0x7c)

#define MPII_REQ_DESCR_POST_LOW		(0xc0)
#define MPII_REQ_DESCR_POST_HIGH	(0xc4)

/*
 * Scatter Gather Lists
 */

#define MPII_SGE_FL_LAST		(0x1<<31) /* last element in segment */
#define MPII_SGE_FL_EOB			(0x1<<30) /* last element of buffer */
#define MPII_SGE_FL_TYPE		(0x3<<28) /* element type */
 #define MPII_SGE_FL_TYPE_SIMPLE	(0x1<<28) /* simple element */
 #define MPII_SGE_FL_TYPE_CHAIN		(0x3<<28) /* chain element */
 #define MPII_SGE_FL_TYPE_XACTCTX	(0x0<<28) /* transaction context */
#define MPII_SGE_FL_LOCAL		(0x1<<27) /* local address */
#define MPII_SGE_FL_DIR			(0x1<<26) /* direction */
 #define MPII_SGE_FL_DIR_OUT		(0x1<<26)
 #define MPII_SGE_FL_DIR_IN		(0x0<<26)
#define MPII_SGE_FL_SIZE		(0x1<<25) /* address size */
 #define MPII_SGE_FL_SIZE_32		(0x0<<25)
 #define MPII_SGE_FL_SIZE_64		(0x1<<25)
#define MPII_SGE_FL_EOL			(0x1<<24) /* end of list */

struct mpii_sge {
	u_int32_t		sg_hdr;
	u_int32_t		sg_addr_lo;
	u_int32_t		sg_addr_hi;
} __packed __aligned(4);

/*
 * SAS3 (IEEE) Scatter Gather Lists
 */

#define MPII_IEEE_SGE_ADDR_MASK		(0x03)
#define MPII_IEEE_SGE_ADDR_SYSTEM	(0x00)
#define MPII_IEEE_SGE_ADDR_IOCDDR	(0x01)
#define MPII_IEEE_SGE_ADDR_IOCPLB	(0x02)
#define MPII_IEEE_SGE_ADDR_IOCPLBNTA	(0x03)
#define MPII_IEEE_SGE_END_OF_LIST	(0x40)
#define MPII_IEEE_SGE_CHAIN_ELEMENT	(0x80)

struct mpii_ieee_sge {
	u_int64_t		sg_addr;
	u_int32_t		sg_len;
	u_int16_t		_reserved;
	u_int8_t		sg_next_chain_offset;
	u_int8_t		sg_flags;
} __packed __aligned(8);

struct mpii_fw_tce {
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
#define MPII_FUNCTION_SCSI_IO_REQUEST			(0x00)
#define MPII_FUNCTION_SCSI_TASK_MGMT			(0x01)
#define MPII_FUNCTION_IOC_INIT				(0x02)
#define MPII_FUNCTION_IOC_FACTS				(0x03)
#define MPII_FUNCTION_CONFIG				(0x04)
#define MPII_FUNCTION_PORT_FACTS			(0x05)
#define MPII_FUNCTION_PORT_ENABLE			(0x06)
#define MPII_FUNCTION_EVENT_NOTIFICATION		(0x07)
#define MPII_FUNCTION_EVENT_ACK				(0x08)
#define MPII_FUNCTION_FW_DOWNLOAD			(0x09)
#define MPII_FUNCTION_TARGET_CMD_BUFFER_POST		(0x0a)
#define MPII_FUNCTION_TARGET_ASSIST			(0x0b)
#define MPII_FUNCTION_TARGET_STATUS_SEND		(0x0c)
#define MPII_FUNCTION_TARGET_MODE_ABORT			(0x0d)
#define MPII_FUNCTION_FW_UPLOAD				(0x12)

#define MPII_FUNCTION_RAID_ACTION			(0x15)
#define MPII_FUNCTION_RAID_SCSI_IO_PASSTHROUGH		(0x16)

#define MPII_FUNCTION_TOOLBOX				(0x17)

#define MPII_FUNCTION_SCSI_ENCLOSURE_PROCESSOR		(0x18)

#define MPII_FUNCTION_SMP_PASSTHROUGH			(0x1a)
#define MPII_FUNCTION_SAS_IO_UNIT_CONTROL		(0x1b)
#define MPII_FUNCTION_SATA_PASSTHROUGH			(0x1c)

#define MPII_FUNCTION_DIAG_BUFFER_POST			(0x1d)
#define MPII_FUNCTION_DIAG_RELEASE			(0x1e)

#define MPII_FUNCTION_TARGET_CMD_BUF_BASE_POST		(0x24)
#define MPII_FUNCTION_TARGET_CMD_BUF_LIST_POST		(0x25)

#define MPII_FUNCTION_IOC_MESSAGE_UNIT_RESET		(0x40)
#define MPII_FUNCTION_IO_UNIT_RESET			(0x41)
#define MPII_FUNCTION_HANDSHAKE				(0x42)

/* Common IOCStatus values for all replies */
#define MPII_IOCSTATUS_MASK				(0x7fff)
#define  MPII_IOCSTATUS_SUCCESS				(0x0000)
#define  MPII_IOCSTATUS_INVALID_FUNCTION		(0x0001)
#define  MPII_IOCSTATUS_BUSY				(0x0002)
#define  MPII_IOCSTATUS_INVALID_SGL			(0x0003)
#define  MPII_IOCSTATUS_INTERNAL_ERROR			(0x0004)
#define  MPII_IOCSTATUS_INVALID_VPID			(0x0005)
#define  MPII_IOCSTATUS_INSUFFICIENT_RESOURCES		(0x0006)
#define  MPII_IOCSTATUS_INVALID_FIELD			(0x0007)
#define  MPII_IOCSTATUS_INVALID_STATE			(0x0008)
#define  MPII_IOCSTATUS_OP_STATE_NOT_SUPPORTED		(0x0009)
/* Config IOCStatus values */
#define  MPII_IOCSTATUS_CONFIG_INVALID_ACTION		(0x0020)
#define  MPII_IOCSTATUS_CONFIG_INVALID_TYPE		(0x0021)
#define  MPII_IOCSTATUS_CONFIG_INVALID_PAGE		(0x0022)
#define  MPII_IOCSTATUS_CONFIG_INVALID_DATA		(0x0023)
#define  MPII_IOCSTATUS_CONFIG_NO_DEFAULTS		(0x0024)
#define  MPII_IOCSTATUS_CONFIG_CANT_COMMIT		(0x0025)
/* SCSIIO Reply initiator values */
#define  MPII_IOCSTATUS_SCSI_RECOVERED_ERROR		(0x0040)
#define  MPII_IOCSTATUS_SCSI_INVALID_DEVHANDLE		(0x0042)
#define  MPII_IOCSTATUS_SCSI_DEVICE_NOT_THERE		(0x0043)
#define  MPII_IOCSTATUS_SCSI_DATA_OVERRUN		(0x0044)
#define  MPII_IOCSTATUS_SCSI_DATA_UNDERRUN		(0x0045)
#define  MPII_IOCSTATUS_SCSI_IO_DATA_ERROR		(0x0046)
#define  MPII_IOCSTATUS_SCSI_PROTOCOL_ERROR		(0x0047)
#define  MPII_IOCSTATUS_SCSI_TASK_TERMINATED		(0x0048)
#define  MPII_IOCSTATUS_SCSI_RESIDUAL_MISMATCH		(0x0049)
#define  MPII_IOCSTATUS_SCSI_TASK_MGMT_FAILED		(0x004a)
#define  MPII_IOCSTATUS_SCSI_IOC_TERMINATED		(0x004b)
#define  MPII_IOCSTATUS_SCSI_EXT_TERMINATED		(0x004c)
/* For use by SCSI Initiator and SCSI Target end-to-end data protection */
#define  MPII_IOCSTATUS_EEDP_GUARD_ERROR		(0x004d)
#define  MPII_IOCSTATUS_EEDP_REF_TAG_ERROR		(0x004e)
#define  MPII_IOCSTATUS_EEDP_APP_TAG_ERROR		(0x004f)
/* SCSI (SPI & FCP) target values */
#define  MPII_IOCSTATUS_TARGET_INVALID_IO_INDEX		(0x0062)
#define  MPII_IOCSTATUS_TARGET_ABORTED			(0x0063)
#define  MPII_IOCSTATUS_TARGET_NO_CONN_RETRYABLE	(0x0064)
#define  MPII_IOCSTATUS_TARGET_NO_CONNECTION		(0x0065)
#define  MPII_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH	(0x006a)
#define  MPII_IOCSTATUS_TARGET_DATA_OFFSET_ERROR	(0x006d)
#define  MPII_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA	(0x006e)
#define  MPII_IOCSTATUS_TARGET_IU_TOO_SHORT		(0x006f)
#define  MPII_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT		(0x0070)
#define  MPII_IOCSTATUS_TARGET_NAK_RECEIVED		(0x0071)
/* Serial Attached SCSI values */
#define  MPII_IOCSTATUS_SAS_SMP_REQUEST_FAILED		(0x0090)
#define  MPII_IOCSTATUS_SAS_SMP_DATA_OVERRUN		(0x0091)
/* Diagnostic Tools values */
#define  MPII_IOCSTATUS_DIAGNOSTIC_RELEASED		(0x00a0)

#define MPII_REP_IOCLOGINFO_TYPE			(0xf<<28)
#define MPII_REP_IOCLOGINFO_TYPE_NONE			(0x0<<28)
#define MPII_REP_IOCLOGINFO_TYPE_SCSI			(0x1<<28)
#define MPII_REP_IOCLOGINFO_TYPE_FC			(0x2<<28)
#define MPII_REP_IOCLOGINFO_TYPE_SAS			(0x3<<28)
#define MPII_REP_IOCLOGINFO_TYPE_ISCSI			(0x4<<28)
#define MPII_REP_IOCLOGINFO_DATA			(0x0fffffff)

/* event notification types */
#define MPII_EVENT_NONE					(0x00)
#define MPII_EVENT_LOG_DATA				(0x01)
#define MPII_EVENT_STATE_CHANGE				(0x02)
#define MPII_EVENT_HARD_RESET_RECEIVED			(0x05)
#define MPII_EVENT_EVENT_CHANGE				(0x0a)
#define MPII_EVENT_TASK_SET_FULL			(0x0e)
#define MPII_EVENT_SAS_DEVICE_STATUS_CHANGE		(0x0f)
#define MPII_EVENT_IR_OPERATION_STATUS			(0x14)
#define MPII_EVENT_SAS_DISCOVERY			(0x16)
#define MPII_EVENT_SAS_BROADCAST_PRIMITIVE		(0x17)
#define MPII_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE	(0x18)
#define MPII_EVENT_SAS_INIT_TABLE_OVERFLOW		(0x19)
#define MPII_EVENT_SAS_TOPOLOGY_CHANGE_LIST		(0x1c)
#define MPII_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE	(0x1d)
#define MPII_EVENT_IR_VOLUME				(0x1e)
#define MPII_EVENT_IR_PHYSICAL_DISK			(0x1f)
#define MPII_EVENT_IR_CONFIGURATION_CHANGE_LIST		(0x20)
#define MPII_EVENT_LOG_ENTRY_ADDED			(0x21)

/* messages */

#define MPII_WHOINIT_NOONE				(0x00)
#define MPII_WHOINIT_SYSTEM_BIOS			(0x01)
#define MPII_WHOINIT_ROM_BIOS				(0x02)
#define MPII_WHOINIT_PCI_PEER				(0x03)
#define MPII_WHOINIT_HOST_DRIVER			(0x04)
#define MPII_WHOINIT_MANUFACTURER			(0x05)

/* default messages */

struct mpii_msg_request {
	u_int8_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved3;
	u_int8_t		reserved4;
	u_int8_t		reserved5;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved6;
} __packed __aligned(4);

struct mpii_msg_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_if;
	u_int16_t		reserved4;

	u_int16_t		reserved5;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

/* ioc init */

struct mpii_msg_iocinit_request {
	u_int8_t		whoinit;
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved4;

	u_int8_t		msg_version_min;
	u_int8_t		msg_version_maj;
	u_int8_t		hdr_version_unit;
	u_int8_t		hdr_version_dev;

	u_int32_t		reserved5;

	u_int32_t		reserved6;

	u_int16_t		reserved7;
	u_int16_t		system_request_frame_size;

	u_int16_t		reply_descriptor_post_queue_depth;
	u_int16_t		reply_free_queue_depth;

	u_int32_t		sense_buffer_address_high;

	u_int32_t		system_reply_address_high;

	u_int32_t		system_request_frame_base_address_lo;
	u_int32_t		system_request_frame_base_address_hi;

	u_int32_t		reply_descriptor_post_queue_address_lo;
	u_int32_t		reply_descriptor_post_queue_address_hi;

	u_int32_t		reply_free_queue_address_lo;
	u_int32_t		reply_free_queue_address_hi;

	u_int64_t		timestamp;
} __packed __aligned(4);

struct mpii_msg_iocinit_reply {
	u_int8_t		whoinit;
	u_int8_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved4;

	u_int16_t		reserved5;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

struct mpii_msg_iocfacts_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved4;
} __packed __aligned(4);

struct mpii_msg_iocfacts_reply {
	u_int8_t		msg_version_min;
	u_int8_t		msg_version_maj;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		header_version_dev;
	u_int8_t		header_version_unit;
	u_int8_t		ioc_number;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved1;

	u_int16_t		ioc_exceptions;
#define MPII_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL	(1<<0)
#define MPII_IOCFACTS_EXCEPT_RAID_CONFIG_INVALID	(1<<1)
#define MPII_IOCFACTS_EXCEPT_FW_CHECKSUM_FAIL		(1<<2)
#define MPII_IOCFACTS_EXCEPT_MANUFACT_CHECKSUM_FAIL	(1<<3)
#define MPII_IOCFACTS_EXCEPT_METADATA_UNSUPPORTED	(1<<4)
#define MPII_IOCFACTS_EXCEPT_IR_FOREIGN_CONFIG_MAC	(1<<8)
	/* XXX JPG BOOT_STATUS in bits[7:5] */
	/* XXX JPG all these #defines need to be fixed up */
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int8_t		max_chain_depth;
	u_int8_t		whoinit;
	u_int8_t		number_of_ports;
	u_int8_t		reserved2;

	u_int16_t		request_credit;
	u_int16_t		product_id;

	u_int32_t		ioc_capabilities;
#define MPII_IOCFACTS_CAPABILITY_EVENT_REPLAY           (1<<13)
#define MPII_IOCFACTS_CAPABILITY_INTEGRATED_RAID        (1<<12)
#define MPII_IOCFACTS_CAPABILITY_TLR                    (1<<11)
#define MPII_IOCFACTS_CAPABILITY_MULTICAST              (1<<8)
#define MPII_IOCFACTS_CAPABILITY_BIDIRECTIONAL_TARGET   (1<<7)
#define MPII_IOCFACTS_CAPABILITY_EEDP                   (1<<6)
#define MPII_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER        (1<<4)
#define MPII_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER      (1<<3)
#define MPII_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING (1<<2)

	u_int8_t		fw_version_dev;
	u_int8_t		fw_version_unit;
	u_int8_t		fw_version_min;
	u_int8_t		fw_version_maj;

	u_int16_t		ioc_request_frame_size;
	u_int16_t		ioc_max_chain_seg_size;

	u_int16_t		max_initiators;
	u_int16_t		max_targets;

	u_int16_t		max_sas_expanders;
	u_int16_t		max_enclosures;

	u_int16_t		protocol_flags;
	u_int16_t		high_priority_credit;

	u_int16_t		max_reply_descriptor_post_queue_depth;
	u_int8_t		reply_frame_size;
	u_int8_t		max_volumes;

	u_int16_t		max_dev_handle;
	u_int16_t		max_persistent_entries;

	u_int32_t		reserved4;
} __packed __aligned(4);

struct mpii_msg_portfacts_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		port_number;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;
} __packed __aligned(4);

struct mpii_msg_portfacts_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		port_number;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int16_t		reserved4;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int8_t		reserved5;
	u_int8_t		port_type;
#define MPII_PORTFACTS_PORTTYPE_INACTIVE		(0x00)
#define MPII_PORTFACTS_PORTTYPE_FC			(0x10)
#define MPII_PORTFACTS_PORTTYPE_ISCSI			(0x20)
#define MPII_PORTFACTS_PORTTYPE_SAS_PHYSICAL		(0x30)
#define MPII_PORTFACTS_PORTTYPE_SAS_VIRTUAL		(0x31)
#define MPII_PORTFACTS_PORTTYPE_TRI_MODE		(0x40)
	u_int16_t		reserved6;

	u_int16_t		max_posted_cmd_buffers;
	u_int16_t		reserved7;
} __packed __aligned(4);

struct mpii_msg_portenable_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved2;
	u_int8_t		port_flags;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved4;
} __packed __aligned(4);

struct mpii_msg_portenable_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		reserved2;
	u_int8_t		port_flags;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved4;

	u_int16_t		reserved5;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

struct mpii_msg_event_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		reserved2;
	u_int8_t		reserved3;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved4;

	u_int32_t		reserved5;

	u_int32_t		reserved6;

	u_int32_t		event_masks[4];

	u_int16_t		sas_broadcase_primitive_masks;
	u_int16_t		reserved7;

	u_int32_t		reserved8;
} __packed __aligned(4);

struct mpii_msg_event_reply {
	u_int16_t		event_data_length;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved1;
	u_int8_t		ack_required;
#define MPII_EVENT_ACK_REQUIRED				(0x01)
	u_int8_t		msg_flags;
#define MPII_EVENT_FLAGS_REPLY_KEPT			(1<<7)

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved2;

	u_int16_t		reserved3;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int16_t		event;
	u_int16_t		reserved4;

	u_int32_t		event_context;

	/* event data follows */
} __packed __aligned(4);

struct mpii_msg_eventack_request {
	u_int16_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int16_t		event;
	u_int16_t		reserved4;

	u_int32_t		event_context;
} __packed __aligned(4);

struct mpii_msg_eventack_reply {
	u_int16_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int16_t		reserved4;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

struct mpii_msg_fwupload_request {
	u_int8_t		image_type;
#define MPII_FWUPLOAD_IMAGETYPE_IOC_FW			(0x00)
#define MPII_FWUPLOAD_IMAGETYPE_NV_FW			(0x01)
#define MPII_FWUPLOAD_IMAGETYPE_NV_BACKUP		(0x05)
#define MPII_FWUPLOAD_IMAGETYPE_NV_MANUFACTURING	(0x06)
#define MPII_FWUPLOAD_IMAGETYPE_NV_CONFIG_1		(0x07)
#define MPII_FWUPLOAD_IMAGETYPE_NV_CONFIG_2		(0x08)
#define MPII_FWUPLOAD_IMAGETYPE_NV_MEGARAID		(0x09)
#define MPII_FWUPLOAD_IMAGETYPE_NV_COMPLETE		(0x0a)
#define MPII_FWUPLOAD_IMAGETYPE_COMMON_BOOT_BLOCK	(0x0b)
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int32_t		reserved4;

	u_int32_t		reserved5;

	struct mpii_fw_tce	tce;

	/* followed by an sgl */
} __packed __aligned(4);

struct mpii_msg_fwupload_reply {
	u_int8_t		image_type;
	u_int8_t		reserved1;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		reserved2[3];
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int16_t		reserved4;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		actual_image_size;
} __packed __aligned(4);

struct mpii_msg_scsi_io {
	u_int16_t		dev_handle;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int32_t		sense_buffer_low_address;

	u_int16_t		sgl_flags;
	u_int8_t		sense_buffer_length;
	u_int8_t		reserved4;

	u_int8_t		sgl_offset0;
	u_int8_t		sgl_offset1;
	u_int8_t		sgl_offset2;
	u_int8_t		sgl_offset3;

	u_int32_t		skip_count;

	u_int32_t		data_length;

	u_int32_t		bidirectional_data_length;

	u_int16_t		io_flags;
	u_int16_t		eedp_flags;

	u_int32_t		eedp_block_size;

	u_int32_t		secondary_reference_tag;

	u_int16_t		secondary_application_tag;
	u_int16_t		application_tag_translation_mask;

	u_int16_t		lun[4];

/* the following 16 bits are defined in MPI2 as the control field */
	u_int8_t		reserved5;
	u_int8_t		tagging;
#define MPII_SCSIIO_ATTR_SIMPLE_Q			(0x0)
#define MPII_SCSIIO_ATTR_HEAD_OF_Q			(0x1)
#define MPII_SCSIIO_ATTR_ORDERED_Q			(0x2)
#define MPII_SCSIIO_ATTR_ACA_Q				(0x4)
#define MPII_SCSIIO_ATTR_UNTAGGED			(0x5)
#define MPII_SCSIIO_ATTR_NO_DISCONNECT			(0x7)
	u_int8_t		reserved6;
	u_int8_t		direction;
#define MPII_SCSIIO_DIR_NONE				(0x0)
#define MPII_SCSIIO_DIR_WRITE				(0x1)
#define MPII_SCSIIO_DIR_READ				(0x2)

#define	MPII_CDB_LEN					(32)
	u_int8_t		cdb[MPII_CDB_LEN];

	/* followed by an sgl */
} __packed __aligned(4);

struct mpii_msg_scsi_io_error {
	u_int16_t		dev_handle;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		reserved1;
	u_int8_t		reserved2;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int8_t		scsi_status;
#define MPII_SCSIIO_STATUS_GOOD				(0x00)
#define MPII_SCSIIO_STATUS_CHECK_COND			(0x02)
#define MPII_SCSIIO_STATUS_COND_MET			(0x04)
#define MPII_SCSIIO_STATUS_BUSY				(0x08)
#define MPII_SCSIIO_STATUS_INTERMEDIATE			(0x10)
#define MPII_SCSIIO_STATUS_INTERMEDIATE_CONDMET		(0x14)
#define MPII_SCSIIO_STATUS_RESERVATION_CONFLICT		(0x18)
#define MPII_SCSIIO_STATUS_CMD_TERM			(0x22)
#define MPII_SCSIIO_STATUS_TASK_SET_FULL		(0x28)
#define MPII_SCSIIO_STATUS_ACA_ACTIVE			(0x30)
#define MPII_SCSIIO_STATUS_TASK_ABORTED			(0x40)
	u_int8_t		scsi_state;
#define MPII_SCSIIO_STATE_AUTOSENSE_VALID		(1<<0)
#define MPII_SCSIIO_STATE_AUTOSENSE_FAILED		(1<<1)
#define MPII_SCSIIO_STATE_NO_SCSI_STATUS		(1<<2)
#define MPII_SCSIIO_STATE_TERMINATED			(1<<3)
#define MPII_SCSIIO_STATE_RESPONSE_INFO_VALID		(1<<4)
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		transfer_count;

	u_int32_t		sense_count;

	u_int32_t		response_info;

	u_int16_t		task_tag;
	u_int16_t		reserved4;

	u_int32_t		bidirectional_transfer_count;

	u_int32_t		reserved5;

	u_int32_t		reserved6;
} __packed __aligned(4);

struct mpii_request_descr {
	u_int8_t		request_flags;
#define MPII_REQ_DESCR_TYPE_MASK			(0x0e)
#define MPII_REQ_DESCR_SCSI_IO				(0x00)
#define MPII_REQ_DESCR_SCSI_TARGET			(0x02)
#define MPII_REQ_DESCR_HIGH_PRIORITY			(0x06)
#define MPII_REQ_DESCR_DEFAULT				(0x08)
	u_int8_t		vf_id;
	u_int16_t		smid;

	u_int16_t		lmid;
	u_int16_t		dev_handle;
} __packed __aligned(8);

struct mpii_reply_descr {
	u_int8_t		reply_flags;
#define MPII_REPLY_DESCR_TYPE_MASK			(0x0f)
#define MPII_REPLY_DESCR_SCSI_IO_SUCCESS		(0x00)
#define MPII_REPLY_DESCR_ADDRESS_REPLY			(0x01)
#define MPII_REPLY_DESCR_TARGET_ASSIST_SUCCESS		(0x02)
#define MPII_REPLY_DESCR_TARGET_COMMAND_BUFFER		(0x03)
#define MPII_REPLY_DESCR_UNUSED				(0x0f)
	u_int8_t		vf_id;
	u_int16_t		smid;

	union {
		u_int32_t	data;
		u_int32_t	frame_addr;	/* Address Reply */
	};
} __packed __aligned(8);

struct mpii_request_header {
	u_int16_t		function_dependent1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		function_dependent2;
	u_int8_t		function_dependent3;
	u_int8_t		message_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved;
} __packed __aligned(4);

struct mpii_msg_scsi_task_request {
	u_int16_t		dev_handle;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int8_t		reserved1;
	u_int8_t		task_type;
#define MPII_SCSI_TASK_ABORT_TASK			(0x01)
#define MPII_SCSI_TASK_ABRT_TASK_SET			(0x02)
#define MPII_SCSI_TASK_TARGET_RESET			(0x03)
#define MPII_SCSI_TASK_RESET_BUS			(0x04)
#define MPII_SCSI_TASK_LOGICAL_UNIT_RESET		(0x05)
	u_int8_t		reserved2;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved3;

	u_int16_t		lun[4];

	u_int32_t		reserved4[7];

	u_int16_t		task_mid;
	u_int16_t		reserved5;
} __packed __aligned(4);

struct mpii_msg_scsi_task_reply {
	u_int16_t		dev_handle;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int8_t		response_code;
	u_int8_t		task_type;
	u_int8_t		reserved1;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved2;

	u_int16_t		reserved3;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	u_int32_t		termination_count;
} __packed __aligned(4);

struct mpii_msg_sas_oper_request {
	u_int8_t		operation;
#define MPII_SAS_OP_CLEAR_PERSISTENT		(0x02)
#define MPII_SAS_OP_PHY_LINK_RESET		(0x06)
#define MPII_SAS_OP_PHY_HARD_RESET		(0x07)
#define MPII_SAS_OP_PHY_CLEAR_ERROR_LOG		(0x08)
#define MPII_SAS_OP_SEND_PRIMITIVE		(0x0a)
#define MPII_SAS_OP_FORCE_FULL_DISCOVERY	(0x0b)
#define MPII_SAS_OP_TRANSMIT_PORT_SELECT	(0x0c)
#define MPII_SAS_OP_REMOVE_DEVICE		(0x0d)
#define MPII_SAS_OP_LOOKUP_MAPPING		(0x0e)
#define MPII_SAS_OP_SET_IOC_PARAM		(0x0f)
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		dev_handle;
	u_int8_t		ioc_param;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved2;

	u_int16_t		reserved3;
	u_int8_t		phy_num;
	u_int8_t		prim_flags;

	u_int32_t		primitive;

	u_int8_t		lookup_method;
#define MPII_SAS_LOOKUP_METHOD_SAS_ADDR		(0x01)
#define MPII_SAS_LOOKUP_METHOD_SAS_ENCL		(0x02)
#define MPII_SAS_LOOKUP_METHOD_SAS_DEVNAME	(0x03)
	u_int8_t		reserved4;
	u_int16_t		slot_num;

	u_int64_t		lookup_addr;

	u_int32_t		ioc_param_value;

	u_int64_t		reserved5;
} __packed __aligned(4);

struct mpii_msg_sas_oper_reply {
	u_int8_t		operation;
	u_int8_t		reserved1;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		dev_handle;
	u_int8_t		ioc_param;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved2;

	u_int16_t		reserved3;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;
} __packed __aligned(4);

struct mpii_msg_raid_action_request {
	u_int8_t	action;
#define MPII_RAID_ACTION_CHANGE_VOL_WRITE_CACHE	(0x17)
	u_int8_t	reserved1;
	u_int8_t	chain_offset;
	u_int8_t	function;

	u_int16_t	vol_dev_handle;
	u_int8_t	phys_disk_num;
	u_int8_t	msg_flags;

	u_int8_t	vp_id;
	u_int8_t	vf_if;
	u_int16_t	reserved2;

	u_int32_t	reserved3;

	u_int32_t	action_data;
#define MPII_RAID_VOL_WRITE_CACHE_MASK			(0x03)
#define MPII_RAID_VOL_WRITE_CACHE_DISABLE		(0x01)
#define MPII_RAID_VOL_WRITE_CACHE_ENABLE		(0x02)

	struct mpii_sge	action_sge;
} __packed __aligned(4);

struct mpii_msg_raid_action_reply {
	u_int8_t	action;
	u_int8_t	reserved1;
	u_int8_t	chain_offset;
	u_int8_t	function;

	u_int16_t	vol_dev_handle;
	u_int8_t	phys_disk_num;
	u_int8_t	msg_flags;

	u_int8_t	vp_id;
	u_int8_t	vf_if;
	u_int16_t	reserved2;

	u_int16_t	reserved3;
	u_int16_t	ioc_status;

	u_int32_t	action_data[5];
} __packed __aligned(4);

struct mpii_cfg_hdr {
	u_int8_t		page_version;
	u_int8_t		page_length;
	u_int8_t		page_number;
	u_int8_t		page_type;
#define MPII_CONFIG_REQ_PAGE_TYPE_ATTRIBUTE		(0xf0)
#define MPI2_CONFIG_PAGEATTR_READ_ONLY			(0x00)
#define MPI2_CONFIG_PAGEATTR_CHANGEABLE			(0x10)
#define MPI2_CONFIG_PAGEATTR_PERSISTENT			(0x20)

#define MPII_CONFIG_REQ_PAGE_TYPE_MASK			(0x0f)
#define MPII_CONFIG_REQ_PAGE_TYPE_IO_UNIT		(0x00)
#define MPII_CONFIG_REQ_PAGE_TYPE_IOC			(0x01)
#define MPII_CONFIG_REQ_PAGE_TYPE_BIOS			(0x02)
#define MPII_CONFIG_REQ_PAGE_TYPE_RAID_VOL		(0x08)
#define MPII_CONFIG_REQ_PAGE_TYPE_MANUFACTURING		(0x09)
#define MPII_CONFIG_REQ_PAGE_TYPE_RAID_PD		(0x0a)
#define MPII_CONFIG_REQ_PAGE_TYPE_EXTENDED		(0x0f)
} __packed __aligned(4);

struct mpii_ecfg_hdr {
	u_int8_t		page_version;
	u_int8_t		reserved1;
	u_int8_t		page_number;
	u_int8_t		page_type;

	u_int16_t		ext_page_length;
	u_int8_t		ext_page_type;
#define MPII_CONFIG_REQ_PAGE_TYPE_SAS_DEVICE		(0x12)
#define MPII_CONFIG_REQ_PAGE_TYPE_RAID_CONFIG		(0x16)
#define MPII_CONFIG_REQ_PAGE_TYPE_DRIVER_MAPPING	(0x17)
	u_int8_t		reserved2;
} __packed __aligned(4);

/* config page address formats */
#define MPII_PGAD_SAS_DEVICE_FORM_MASK			(0xf0000000)
#define MPII_PGAD_SAS_DEVICE_FORM_GET_NEXT_HANDLE	(0x00000000)
#define MPII_PGAD_SAS_DEVICE_FORM_HANDLE		(0x20000000)

#define MPII_PGAD_SAS_DEVICE_HANDLE_MASK		(0x0000ffff)

struct mpii_msg_config_request {
	u_int8_t		action;
#define MPII_CONFIG_REQ_ACTION_PAGE_HEADER		(0x00)
#define MPII_CONFIG_REQ_ACTION_PAGE_READ_CURRENT	(0x01)
#define MPII_CONFIG_REQ_ACTION_PAGE_WRITE_CURRENT	(0x02)
#define MPII_CONFIG_REQ_ACTION_PAGE_DEFAULT		(0x03)
#define MPII_CONFIG_REQ_ACTION_PAGE_WRITE_NVRAM		(0x04)
#define MPII_CONFIG_REQ_ACTION_PAGE_READ_DEFAULT	(0x05)
#define MPII_CONFIG_REQ_ACTION_PAGE_READ_NVRAM		(0x06)
	u_int8_t		sgl_flags;
	u_int8_t		chain_offset;
	u_int8_t		function;

	u_int16_t		ext_page_len;
	u_int8_t		ext_page_type;
#define MPII_CONFIG_REQ_EXTPAGE_TYPE_SAS_IO_UNIT	(0x10)
#define MPII_CONFIG_REQ_EXTPAGE_TYPE_SAS_EXPANDER	(0x11)
#define MPII_CONFIG_REQ_EXTPAGE_TYPE_SAS_DEVICE		(0x12)
#define MPII_CONFIG_REQ_EXTPAGE_TYPE_SAS_PHY		(0x13)
#define MPII_CONFIG_REQ_EXTPAGE_TYPE_LOG		(0x14)
#define MPI2_CONFIG_EXTPAGETYPE_ENCLOSURE		(0x15)
#define MPI2_CONFIG_EXTPAGETYPE_RAID_CONFIG		(0x16)
#define MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING		(0x17)
#define MPI2_CONFIG_EXTPAGETYPE_SAS_PORT		(0x18)
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved1;

	u_int32_t		reserved2[2];

	struct mpii_cfg_hdr	config_header;

	u_int32_t		page_address;
/* XXX lots of defns here */

	struct mpii_sge		page_buffer;
} __packed __aligned(4);

struct mpii_msg_config_reply {
	u_int8_t		action;
	u_int8_t		sgl_flags;
	u_int8_t		msg_length;
	u_int8_t		function;

	u_int16_t		ext_page_length;
	u_int8_t		ext_page_type;
	u_int8_t		msg_flags;

	u_int8_t		vp_id;
	u_int8_t		vf_id;
	u_int16_t		reserved1;

	u_int16_t		reserved2;
	u_int16_t		ioc_status;

	u_int32_t		ioc_loginfo;

	struct mpii_cfg_hdr	config_header;
} __packed __aligned(4);

struct mpii_cfg_manufacturing_pg0 {
	struct mpii_cfg_hdr	config_header;

	char			chip_name[16];
	char			chip_revision[8];
	char			board_name[16];
	char			board_assembly[16];
	char			board_tracer_number[16];
} __packed __aligned(4);

struct mpii_cfg_ioc_pg1 {
	struct mpii_cfg_hdr     config_header;

	u_int32_t       flags;

	u_int32_t       coalescing_timeout;
#define	MPII_CFG_IOC_1_REPLY_COALESCING			(1<<0)

	u_int8_t        coalescing_depth;
	u_int8_t        pci_slot_num;
	u_int8_t        pci_bus_num;
	u_int8_t        pci_domain_segment;

	u_int32_t       reserved1;

	u_int32_t       reserved2;
} __packed __aligned(4);

struct mpii_cfg_ioc_pg3 {
	struct mpii_cfg_hdr	config_header;

	u_int8_t		no_phys_disks;
	u_int8_t		reserved[3];

	/* followed by a list of mpii_cfg_raid_physdisk structs */
} __packed __aligned(4);

struct mpii_cfg_ioc_pg8 {
	struct mpii_cfg_hdr	config_header;

	u_int8_t		num_devs_per_enclosure;
	u_int8_t		reserved1;
	u_int16_t		reserved2;

	u_int16_t		max_persistent_entries;
	u_int16_t		max_num_physical_mapped_ids;

	u_int16_t		flags;
#define	MPII_IOC_PG8_FLAGS_DA_START_SLOT_1		(1<<5)
#define MPII_IOC_PG8_FLAGS_RESERVED_TARGETID_0		(1<<4)
#define MPII_IOC_PG8_FLAGS_MAPPING_MODE_MASK		(0x0000000e)
#define MPII_IOC_PG8_FLAGS_DEVICE_PERSISTENCE_MAPPING	(0<<1)
#define MPII_IOC_PG8_FLAGS_ENCLOSURE_SLOT_MAPPING	(1<<1)
#define MPII_IOC_PG8_FLAGS_DISABLE_PERSISTENT_MAPPING	(1<<0)
#define	MPII_IOC_PG8_FLAGS_ENABLE_PERSISTENT_MAPPING	(0<<0)
	u_int16_t		reserved3;

	u_int16_t		ir_volume_mapping_flags;
#define	MPII_IOC_PG8_IRFLAGS_VOLUME_MAPPING_MODE_MASK	(0x00000003)
#define	MPII_IOC_PG8_IRFLAGS_LOW_VOLUME_MAPPING		(0<<0)
#define	MPII_IOC_PG8_IRFLAGS_HIGH_VOLUME_MAPPING	(1<<0)
	u_int16_t		reserved4;

	u_int32_t		reserved5;
} __packed __aligned(4);

struct mpii_cfg_raid_physdisk {
	u_int8_t		phys_disk_id;
	u_int8_t		phys_disk_bus;
	u_int8_t		phys_disk_ioc;
	u_int8_t		phys_disk_num;
} __packed __aligned(4);

struct mpii_cfg_fc_port_pg0 {
	struct mpii_cfg_hdr	config_header;

	u_int32_t		flags;

	u_int8_t		mpii_port_nr;
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

struct mpii_cfg_fc_port_pg1 {
	struct mpii_cfg_hdr	config_header;

	u_int32_t		flags;

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

struct mpii_cfg_fc_device_pg0 {
	struct mpii_cfg_hdr	config_header;

	u_int64_t		wwnn;

	u_int64_t		wwpn;

	u_int32_t		port_id;

	u_int8_t		protocol;
	u_int8_t		flags;
	u_int16_t		bb_credit;

	u_int16_t		max_rx_frame_size;
	u_int8_t		adisc_hard_alpa;
	u_int8_t		port_nr;

	u_int8_t		fc_ph_low_version;
	u_int8_t		fc_ph_high_version;
	u_int8_t		current_target_id;
	u_int8_t		current_bus;
} __packed __aligned(4);

#define MPII_CFG_RAID_VOL_ADDR_HANDLE		(1<<28)

struct mpii_cfg_raid_vol_pg0 {
	struct mpii_cfg_hdr	config_header;

	u_int16_t		volume_handle;
	u_int8_t		volume_state;
#define MPII_CFG_RAID_VOL_0_STATE_MISSING		(0x00)
#define MPII_CFG_RAID_VOL_0_STATE_FAILED		(0x01)
#define MPII_CFG_RAID_VOL_0_STATE_INITIALIZING		(0x02)
#define MPII_CFG_RAID_VOL_0_STATE_ONLINE		(0x03)
#define MPII_CFG_RAID_VOL_0_STATE_DEGRADED		(0x04)
#define MPII_CFG_RAID_VOL_0_STATE_OPTIMAL		(0x05)
	u_int8_t		volume_type;
#define MPII_CFG_RAID_VOL_0_TYPE_RAID0			(0x00)
#define MPII_CFG_RAID_VOL_0_TYPE_RAID1E			(0x01)
#define MPII_CFG_RAID_VOL_0_TYPE_RAID1			(0x02)
#define MPII_CFG_RAID_VOL_0_TYPE_RAID10			(0x05)
#define MPII_CFG_RAID_VOL_0_TYPE_UNKNOWN		(0xff)

	u_int32_t		volume_status;
#define MPII_CFG_RAID_VOL_0_STATUS_SCRUB		(1<<20)
#define MPII_CFG_RAID_VOL_0_STATUS_RESYNC		(1<<16)

	u_int16_t		volume_settings;
#define MPII_CFG_RAID_VOL_0_SETTINGS_CACHE_MASK		(0x3<<0)
#define MPII_CFG_RAID_VOL_0_SETTINGS_CACHE_UNCHANGED	(0x0<<0)
#define MPII_CFG_RAID_VOL_0_SETTINGS_CACHE_DISABLED	(0x1<<0)
#define MPII_CFG_RAID_VOL_0_SETTINGS_CACHE_ENABLED	(0x2<<0)

	u_int8_t		hot_spare_pool;
	u_int8_t		reserved1;

	u_int64_t		max_lba;

	u_int32_t		stripe_size;

	u_int16_t		block_size;
	u_int16_t		reserved2;

	u_int8_t		phys_disk_types;
	u_int8_t		resync_rate;
	u_int16_t		data_scrub_rate;

	u_int8_t		num_phys_disks;
	u_int16_t		reserved3;
	u_int8_t		inactive_status;
#define MPII_CFG_RAID_VOL_0_INACTIVE_UNKNOWN		(0x00)
#define MPII_CFG_RAID_VOL_0_INACTIVE_STALE_META		(0x01)
#define MPII_CFG_RAID_VOL_0_INACTIVE_FOREIGN_VOL	(0x02)
#define MPII_CFG_RAID_VOL_0_INACTIVE_NO_RESOURCES	(0x03)
#define MPII_CFG_RAID_VOL_0_INACTIVE_CLONED_VOL		(0x04)
#define MPII_CFG_RAID_VOL_0_INACTIVE_INSUF_META		(0x05)

	/* followed by a list of mpii_cfg_raid_vol_pg0_physdisk structs */
} __packed __aligned(4);

struct mpii_cfg_raid_vol_pg0_physdisk {
	u_int8_t		raid_set_num;
	u_int8_t		phys_disk_map;
	u_int8_t		phys_disk_num;
	u_int8_t		reserved;
} __packed __aligned(4);

struct mpii_cfg_raid_vol_pg1 {
	struct mpii_cfg_hdr	config_header;

	u_int8_t		volume_id;
	u_int8_t		volume_bus;
	u_int8_t		volume_ioc;
	u_int8_t		reserved1;

	u_int8_t		guid[24];

	u_int8_t		name[16];

	u_int64_t		wwid;

	u_int32_t		reserved2;

	u_int32_t		reserved3;
} __packed __aligned(4);

#define MPII_CFG_RAID_PHYS_DISK_ADDR_NUMBER		(1<<28)

struct mpii_cfg_raid_physdisk_pg0 {
	struct mpii_cfg_hdr	config_header;

	u_int16_t		dev_handle;
	u_int8_t		reserved1;
	u_int8_t		phys_disk_num;

	u_int8_t		enc_id;
	u_int8_t		enc_bus;
	u_int8_t		hot_spare_pool;
	u_int8_t		enc_type;
#define MPII_CFG_RAID_PHYDISK_0_ENCTYPE_NONE		(0x0)
#define MPII_CFG_RAID_PHYDISK_0_ENCTYPE_SAFTE		(0x1)
#define MPII_CFG_RAID_PHYDISK_0_ENCTYPE_SES		(0x2)

	u_int32_t		reserved2;

	u_int8_t		vendor_id[8];

	u_int8_t		product_id[16];

	u_int8_t		product_rev[4];

	u_int8_t		serial[32];

	u_int32_t		reserved3;

	u_int8_t		phys_disk_state;
#define MPII_CFG_RAID_PHYDISK_0_STATE_NOTCONFIGURED	(0x00)
#define MPII_CFG_RAID_PHYDISK_0_STATE_NOTCOMPATIBLE	(0x01)
#define MPII_CFG_RAID_PHYDISK_0_STATE_OFFLINE		(0x02)
#define MPII_CFG_RAID_PHYDISK_0_STATE_ONLINE		(0x03)
#define MPII_CFG_RAID_PHYDISK_0_STATE_HOTSPARE		(0x04)
#define MPII_CFG_RAID_PHYDISK_0_STATE_DEGRADED		(0x05)
#define MPII_CFG_RAID_PHYDISK_0_STATE_REBUILDING	(0x06)
#define MPII_CFG_RAID_PHYDISK_0_STATE_OPTIMAL		(0x07)
	u_int8_t		offline_reason;
#define MPII_CFG_RAID_PHYDISK_0_OFFLINE_MISSING		(0x01)
#define MPII_CFG_RAID_PHYDISK_0_OFFLINE_FAILED		(0x03)
#define MPII_CFG_RAID_PHYDISK_0_OFFLINE_INITIALIZING	(0x04)
#define MPII_CFG_RAID_PHYDISK_0_OFFLINE_REQUESTED	(0x05)
#define MPII_CFG_RAID_PHYDISK_0_OFFLINE_FAILEDREQ	(0x06)
#define MPII_CFG_RAID_PHYDISK_0_OFFLINE_OTHER		(0xff)

	u_int8_t		incompat_reason;
	u_int8_t		phys_disk_attrs;

	u_int32_t		phys_disk_status;
#define MPII_CFG_RAID_PHYDISK_0_STATUS_OUTOFSYNC	(1<<0)
#define MPII_CFG_RAID_PHYDISK_0_STATUS_QUIESCED		(1<<1)

	u_int64_t		dev_max_lba;

	u_int64_t		host_max_lba;

	u_int64_t		coerced_max_lba;

	u_int16_t		block_size;
	u_int16_t		reserved4;

	u_int32_t		reserved5;
} __packed __aligned(4);

struct mpii_cfg_raid_physdisk_pg1 {
	struct mpii_cfg_hdr	config_header;

	u_int8_t		num_phys_disk_paths;
	u_int8_t		phys_disk_num;
	u_int16_t		reserved1;

	u_int32_t		reserved2;

	/* followed by mpii_cfg_raid_physdisk_path structs */
} __packed __aligned(4);

struct mpii_cfg_raid_physdisk_path {
	u_int8_t		phys_disk_id;
	u_int8_t		phys_disk_bus;
	u_int16_t		reserved1;

	u_int64_t		wwwid;

	u_int64_t		owner_wwid;

	u_int8_t		ownder_id;
	u_int8_t		reserved2;
	u_int16_t		flags;
#define MPII_CFG_RAID_PHYDISK_PATH_INVALID	(1<<0)
#define MPII_CFG_RAID_PHYDISK_PATH_BROKEN	(1<<1)
} __packed __aligned(4);

#define MPII_CFG_SAS_DEV_ADDR_NEXT		(0<<28)
#define MPII_CFG_SAS_DEV_ADDR_BUS		(1<<28)
#define MPII_CFG_SAS_DEV_ADDR_HANDLE		(2<<28)

struct mpii_cfg_sas_dev_pg0 {
	struct mpii_ecfg_hdr	config_header;

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
#define MPII_CFG_SAS_DEV_0_DEVINFO_TYPE			(0x7)
#define MPII_CFG_SAS_DEV_0_DEVINFO_TYPE_NONE		(0x0)
#define MPII_CFG_SAS_DEV_0_DEVINFO_TYPE_END		(0x1)
#define MPII_CFG_SAS_DEV_0_DEVINFO_TYPE_EDGE_EXPANDER	(0x2)
#define MPII_CFG_SAS_DEV_0_DEVINFO_TYPE_FANOUT_EXPANDER	(0x3)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SATA_HOST		(1<<3)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SMP_INITIATOR	(1<<4)
#define MPII_CFG_SAS_DEV_0_DEVINFO_STP_INITIATOR	(1<<5)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SSP_INITIATOR	(1<<6)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SATA_DEVICE		(1<<7)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SMP_TARGET		(1<<8)
#define MPII_CFG_SAS_DEV_0_DEVINFO_STP_TARGET		(1<<9)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SSP_TARGET		(1<<10)
#define MPII_CFG_SAS_DEV_0_DEVINFO_DIRECT_ATTACHED	(1<<11)
#define MPII_CFG_SAS_DEV_0_DEVINFO_LSI_DEVICE		(1<<12)
#define MPII_CFG_SAS_DEV_0_DEVINFO_ATAPI_DEVICE		(1<<13)
#define MPII_CFG_SAS_DEV_0_DEVINFO_SEP_DEVICE		(1<<14)

	u_int16_t		flags;
#define MPII_CFG_SAS_DEV_0_FLAGS_DEV_PRESENT		(1<<0)
#define MPII_CFG_SAS_DEV_0_FLAGS_DEV_MAPPED		(1<<1)
#define MPII_CFG_SAS_DEV_0_FLAGS_DEV_MAPPED_PERSISTENT	(1<<2)
#define MPII_CFG_SAS_DEV_0_FLAGS_SATA_PORT_SELECTOR	(1<<3)
#define MPII_CFG_SAS_DEV_0_FLAGS_SATA_FUA		(1<<4)
#define MPII_CFG_SAS_DEV_0_FLAGS_SATA_NCQ		(1<<5)
#define MPII_CFG_SAS_DEV_0_FLAGS_SATA_SMART		(1<<6)
#define MPII_CFG_SAS_DEV_0_FLAGS_SATA_LBA48		(1<<7)
#define MPII_CFG_SAS_DEV_0_FLAGS_UNSUPPORTED		(1<<8)
#define MPII_CFG_SAS_DEV_0_FLAGS_SATA_SETTINGS		(1<<9)
	u_int8_t		physical_port;
	u_int8_t		max_port_conn;

	u_int64_t		device_name;

	u_int8_t		port_groups;
	u_int8_t		dma_group;
	u_int8_t		ctrl_group;
	u_int8_t		reserved1;

	u_int64_t		reserved2;
} __packed __aligned(4);

#define MPII_CFG_RAID_CONFIG_ACTIVE_CONFIG		(2<<28)

struct mpii_cfg_raid_config_pg0 {
	struct	mpii_ecfg_hdr	config_header;

	u_int8_t		num_hot_spares;
	u_int8_t		num_phys_disks;
	u_int8_t		num_volumes;
	u_int8_t		config_num;

	u_int32_t		flags;
#define MPII_CFG_RAID_CONFIG_0_FLAGS_NATIVE		(0<<0)
#define MPII_CFG_RAID_CONFIG_0_FLAGS_FOREIGN		(1<<0)

	u_int32_t		config_guid[6];

	u_int32_t		reserved1;

	u_int8_t		num_elements;
	u_int8_t		reserved2[3];

	/* followed by struct mpii_raid_config_element structs */
} __packed __aligned(4);

struct mpii_raid_config_element {
	u_int16_t		element_flags;
#define MPII_RAID_CONFIG_ELEMENT_FLAG_VOLUME		(0x0)
#define MPII_RAID_CONFIG_ELEMENT_FLAG_VOLUME_PHYS_DISK	(0x1)
#define	MPII_RAID_CONFIG_ELEMENT_FLAG_HSP_PHYS_DISK	(0x2)
#define MPII_RAID_CONFIG_ELEMENT_ONLINE_CE_PHYS_DISK	(0x3)
	u_int16_t		vol_dev_handle;

	u_int8_t		hot_spare_pool;
	u_int8_t		phys_disk_num;
	u_int16_t		phys_disk_dev_handle;
} __packed __aligned(4);

struct mpii_cfg_dpm_pg0 {
	struct mpii_ecfg_hdr	config_header;
#define MPII_DPM_ADDRESS_FORM_MASK			(0xf0000000)
#define MPII_DPM_ADDRESS_FORM_ENTRY_RANGE		(0x00000000)
#define MPII_DPM_ADDRESS_ENTRY_COUNT_MASK		(0x0fff0000)
#define MPII_DPM_ADDRESS_ENTRY_COUNT_SHIFT		(16)
#define MPII_DPM_ADDRESS_START_ENTRY_MASK		(0x0000ffff)

	/* followed by struct mpii_dpm_entry structs */
} __packed __aligned(4);

struct mpii_dpm_entry {
	u_int64_t		physical_identifier;

	u_int16_t		mapping_information;
	u_int16_t		device_index;

	u_int32_t		physical_bits_mapping;

	u_int32_t		reserved1;
} __packed __aligned(4);

struct mpii_evt_sas_discovery {
	u_int8_t		flags;
#define	MPII_EVENT_SAS_DISC_FLAGS_DEV_CHANGE_MASK	(1<<1)
#define MPII_EVENT_SAS_DISC_FLAGS_DEV_CHANGE_NO_CHANGE	(0<<1)
#define MPII_EVENT_SAS_DISC_FLAGS_DEV_CHANGE_CHANGE	(1<<1)
#define MPII_EVENT_SAS_DISC_FLAGS_DISC_IN_PROG_MASK	(1<<0)
#define MPII_EVENT_SAS_DISC_FLAGS_DISC_NOT_IN_PROGRESS	(1<<0)
#define MPII_EVENT_SAS_DISC_FLAGS_DISC_IN_PROGRESS	(0<<0)
	u_int8_t		reason_code;
#define MPII_EVENT_SAS_DISC_REASON_CODE_STARTED		(0x01)
#define	MPII_EVENT_SAS_DISC_REASON_CODE_COMPLETED	(0x02)
	u_int8_t		physical_port;
	u_int8_t		reserved1;

	u_int32_t		discovery_status;
} __packed __aligned(4);

struct mpii_evt_ir_status {
	u_int16_t		vol_dev_handle;
	u_int16_t		reserved1;

	u_int8_t		operation;
#define MPII_EVENT_IR_RAIDOP_RESYNC			(0x00)
#define MPII_EVENT_IR_RAIDOP_OCE			(0x01)
#define MPII_EVENT_IR_RAIDOP_CONS_CHECK			(0x02)
#define MPII_EVENT_IR_RAIDOP_BG_INIT			(0x03)
#define MPII_EVENT_IR_RAIDOP_MAKE_CONS			(0x04)
	u_int8_t		percent;
	u_int16_t		reserved2;

	u_int32_t		reserved3;
};

struct mpii_evt_ir_volume {
	u_int16_t		vol_dev_handle;
	u_int8_t		reason_code;
#define MPII_EVENT_IR_VOL_RC_SETTINGS_CHANGED		(0x01)
#define MPII_EVENT_IR_VOL_RC_STATUS_CHANGED		(0x02)
#define MPII_EVENT_IR_VOL_RC_STATE_CHANGED		(0x03)
	u_int8_t		reserved1;

	u_int32_t		new_value;
	u_int32_t		prev_value;
} __packed __aligned(4);

struct mpii_evt_ir_physical_disk {
	u_int16_t		reserved1;
	u_int8_t		reason_code;
#define MPII_EVENT_IR_PD_RC_SETTINGS_CHANGED		(0x01)
#define MPII_EVENT_IR_PD_RC_STATUS_FLAGS_CHANGED	(0x02)
#define MPII_EVENT_IR_PD_RC_STATUS_CHANGED		(0x03)
	u_int8_t		phys_disk_num;

	u_int16_t		phys_disk_dev_handle;
	u_int16_t		reserved2;

	u_int16_t		slot;
	u_int16_t		enclosure_handle;

	u_int32_t		new_value;
	u_int32_t		previous_value;
} __packed __aligned(4);

struct mpii_evt_sas_tcl {
	u_int16_t		enclosure_handle;
	u_int16_t		expander_handle;

	u_int8_t		num_phys;
	u_int8_t		reserved1[3];

	u_int8_t		num_entries;
	u_int8_t		start_phy_num;
	u_int8_t		expn_status;
#define	MPII_EVENT_SAS_TOPO_ES_ADDED			(0x01)
#define MPII_EVENT_SAS_TOPO_ES_NOT_RESPONDING		(0x02)
#define MPII_EVENT_SAS_TOPO_ES_RESPONDING		(0x03)
#define MPII_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING	(0x04)
	u_int8_t		physical_port;

	/* followed by num_entries number of struct mpii_evt_phy_entry */
} __packed __aligned(4);

struct mpii_evt_phy_entry {
	u_int16_t		dev_handle;
	u_int8_t		link_rate;
	u_int8_t		phy_status;
#define MPII_EVENT_SAS_TOPO_PS_RC_MASK			(0x0f)
#define MPII_EVENT_SAS_TOPO_PS_RC_ADDED			(0x01)
#define MPII_EVENT_SAS_TOPO_PS_RC_MISSING		(0x02)
} __packed __aligned(4);

struct mpii_evt_ir_cfg_change_list {
	u_int8_t		num_elements;
	u_int16_t		reserved;
	u_int8_t		config_num;

	u_int32_t		flags;
#define MPII_EVT_IR_CFG_CHANGE_LIST_FOREIGN		(0x1)

	/* followed by num_elements struct mpii_evt_ir_cfg_elements */
} __packed __aligned(4);

struct mpii_evt_ir_cfg_element {
	u_int16_t		element_flags;
#define MPII_EVT_IR_CFG_ELEMENT_TYPE_MASK		(0xf)
#define MPII_EVT_IR_CFG_ELEMENT_TYPE_VOLUME		(0x0)
#define MPII_EVT_IR_CFG_ELEMENT_TYPE_VOLUME_DISK	(0x1)
#define MPII_EVT_IR_CFG_ELEMENT_TYPE_HOT_SPARE		(0x2)
	u_int16_t		vol_dev_handle;

	u_int8_t		reason_code;
#define MPII_EVT_IR_CFG_ELEMENT_RC_ADDED		(0x01)
#define MPII_EVT_IR_CFG_ELEMENT_RC_REMOVED		(0x02)
#define MPII_EVT_IR_CFG_ELEMENT_RC_NO_CHANGE		(0x03)
#define MPII_EVT_IR_CFG_ELEMENT_RC_HIDE			(0x04)
#define MPII_EVT_IR_CFG_ELEMENT_RC_UNHIDE		(0x05)
#define MPII_EVT_IR_CFG_ELEMENT_RC_VOLUME_CREATED	(0x06)
#define MPII_EVT_IR_CFG_ELEMENT_RC_VOLUME_DELETED	(0x07)
#define MPII_EVT_IR_CFG_ELEMENT_RC_PD_CREATED		(0x08)
#define MPII_EVT_IR_CFG_ELEMENT_RC_PD_DELETED		(0x09)
	u_int8_t		phys_disk_num;
	u_int16_t		phys_disk_dev_handle;
} __packed __aligned(4);
