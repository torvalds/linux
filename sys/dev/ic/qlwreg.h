/*	$OpenBSD: qlwreg.h,v 1.7 2014/03/15 21:49:47 kettenis Exp $ */

/*
 * Copyright (c) 2013, 2014 Jonathan Matthew <jmatthew@openbsd.org>
 * Copyright (c) 2014 Mark Kettenis <kettenis@openbsd.org>
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
#define QLW_CODE_ORG			0x1000

/* interrupt types */
#define QLW_INT_TYPE_MBOX		1
#define QLW_INT_TYPE_ASYNC		2
#define QLW_INT_TYPE_IO			3
#define QLW_INT_TYPE_OTHER		4

/* ISP registers */
#define QLW_CFG0			0x04
#define QLW_CFG1			0x06
#define QLW_INT_CTRL			0x08
#define QLW_INT_STATUS			0x0a
#define QLW_SEMA			0x0c
#define QLW_NVRAM			0x0e
#define QLW_FLASH_BIOS_DATA		0x10
#define QLW_FLASH_BIOS_ADDR		0x12
#define QLW_CDMA_CFG			0x20
#define QLW_DDMA_CFG			0x40
#define QLW_MBOX_BASE_PCI		0x70
#define QLW_MBOX_BASE_SBUS		0x80
#define QLW_CDMA_CFG_1080		0x80
#define QLW_DDMA_CFG_1080		0xa0
#define QLW_HOST_CMD_CTRL_PCI		0xc0
#define QLW_GPIO_DATA			0xcc
#define QLW_GPIO_ENABLE			0xce

#define QLW_HOST_CMD_CTRL_SBUS		0x440

#define QLW_REQ_IN			0x08
#define QLW_REQ_OUT			0x08
#define QLW_RESP_IN			0x0a
#define QLW_RESP_OUT			0x0a

/* QLW_CFG1 */
#define QLW_BURST_ENABLE		0x0004
#define QLW_PCI_FIFO_16			0x0010
#define QLW_PCI_FIFO_32			0x0020
#define QLW_PCI_FIFO_64			0x0030
#define QLW_PCI_FIFO_128		0x0040
#define QLW_PCI_FIFO_MASK		0x0070
#define QLW_SBUS_FIFO_64		0x0003
#define QLW_SBUS_FIFO_32		0x0002
#define QLW_SBUS_FIFO_16		0x0001
#define QLW_SBUS_FIFO_8			0x0000
#define QLW_SBUS_FIFO_MASK		0x0003
#define QLW_SBUS_BURST_8		0x0008
#define QLW_DMA_BANK			0x0300

/* QLW_INT_CTRL */
#define QLW_RESET			0x0001

/* QLW_INT_STATUS */
#define QLW_INT_REQ			0x0002
#define QLW_RISC_INT_REQ		0x0004

/* QLW_SEMA */
#define QLW_SEMA_STATUS			0x0002
#define QLW_SEMA_LOCK			0x0001

/* QLW_NVRAM */
#define QLW_NVRAM_DATA_IN		0x0008
#define QLW_NVRAM_DATA_OUT		0x0004
#define QLW_NVRAM_CHIP_SEL		0x0002
#define QLW_NVRAM_CLOCK			0x0001
#define QLW_NVRAM_CMD_READ		6

/* QLW_CDMA_CFG and QLW_DDMA_CFG */
#define QLW_DMA_BURST_ENABLE		0x0002

/* QLW_HOST_CMD_CTRL write */
#define QLW_HOST_CMD_SHIFT		12
#define QLW_HOST_CMD_NOP		0x0
#define QLW_HOST_CMD_RESET		0x1
#define QLW_HOST_CMD_PAUSE		0x2
#define QLW_HOST_CMD_RELEASE		0x3
#define QLW_HOST_CMD_MASK_PARITY	0x4
#define QLW_HOST_CMD_SET_HOST_INT	0x5
#define QLW_HOST_CMD_CLR_HOST_INT	0x6
#define QLW_HOST_CMD_CLR_RISC_INT	0x7
#define QLW_HOST_CMD_BIOS		0x9
#define QLW_HOST_CMD_ENABLE_PARITY	0xa
#define QLW_HOST_CMD_PARITY_ERROR	0xe

/* QLA_HOST_CMD_CTRL read */
#define QLA_HOST_STATUS_HOST_INT	0x0080
#define QLA_HOST_STATUS_RISC_RESET	0x0040
#define QLA_HOST_STATUS_RISC_PAUSE	0x0020
#define QLA_HOST_STATUS_RISC_EXT	0x0010

/* QLW_MBIX_BASE (reg 0) read */
#define QLW_MBOX_HAS_STATUS		0x4000
#define QLW_MBOX_COMPLETE		0x4000
#define QLW_MBOX_INVALID		0x4001
#define QLW_ASYNC_BUS_RESET		0x8001
#define QLW_ASYNC_SYSTEM_ERROR		0x8002
#define QLW_ASYNC_REQ_XFER_ERROR	0x8003
#define QLW_ASYNC_RSP_XFER_ERROR	0x8004
#define QLW_ASYNC_SCSI_CMD_COMPLETE	0x8020
#define QLW_ASYNC_CTIO_COMPLETE		0x8021

/* QLW_MBOX_BASE (reg 0) write */
#define QLW_MBOX_NOP			0x0000
#define QLW_MBOX_LOAD_RAM		0x0001
#define QLW_MBOX_EXEC_FIRMWARE		0x0002
#define QLW_MBOX_WRITE_RAM_WORD		0x0004
#define QLW_MBOX_REGISTER_TEST		0x0006
#define QLW_MBOX_VERIFY_CSUM		0x0007
#define QLW_MBOX_ABOUT_FIRMWARE		0x0008
#define QLW_MBOX_INIT_REQ_QUEUE		0x0010
#define QLW_MBOX_INIT_RSP_QUEUE		0x0011
#define QLW_MBOX_BUS_RESET		0x0018
#define QLW_MBOX_GET_FIRMWARE_STATUS	0x001F
#define QLW_MBOX_SET_INITIATOR_ID	0x0030
#define QLW_MBOX_SET_SELECTION_TIMEOUT	0x0031
#define QLW_MBOX_SET_RETRY_COUNT	0x0032
#define QLW_MBOX_SET_TAG_AGE_LIMIT	0x0033
#define QLW_MBOX_SET_CLOCK_RATE		0x0034
#define QLW_MBOX_SET_ACTIVE_NEGATION	0x0035
#define QLW_MBOX_SET_ASYNC_DATA_SETUP	0x0036
#define QLW_MBOX_SET_PCI_CONTROL	0x0037
#define QLW_MBOX_SET_TARGET_PARAMETERS	0x0038
#define QLW_MBOX_SET_DEVICE_QUEUE	0x0039
#define QLW_MBOX_SET_SYSTEM_PARAMETER	0x0045
#define QLW_MBOX_SET_FIRMWARE_FEATURES	0x004a
#define QLW_MBOX_INIT_REQ_QUEUE_A64	0x0052
#define QLW_MBOX_INIT_RSP_QUEUE_A64	0x0053
#define QLW_MBOX_SET_DATA_OVERRUN_RECOVERY	0x005a

/* mailbox operation register bitfields */
#define QLW_MBOX_ABOUT_FIRMWARE_IN	0x0001
#define QLW_MBOX_ABOUT_FIRMWARE_OUT	0x004f
#define QLW_MBOX_INIT_FIRMWARE_IN	0x00fd

#define QLW_FW_FEATURE_FAST_POSTING	0x0001
#define QLW_FW_FEATURE_LVD_NOTIFY	0x0002

/* nvram layout */
struct qlw_nvram_target {
	u_int8_t	parameter;
	u_int8_t	execution_throttle;
	u_int8_t	sync_period;
	u_int8_t	flags;
	u_int8_t	reserved[2];
} __packed;

struct qlw_nvram_1040 {
	u_int8_t	id[4];
	u_int8_t	nvram_version;
	u_int8_t	config1;
	u_int8_t	reset_delay;
	u_int8_t	retry_count;
	u_int8_t	retry_delay;
	u_int8_t	config2;
	u_int8_t	tag_age_limit;
	u_int8_t	flags1;
	u_int16_t	selection_timeout;
	u_int16_t	max_queue_depth;
	u_int8_t	flags2;
	u_int8_t	reserved_0[5];
	u_int8_t	flags3;
	u_int8_t	reserved_1[5];
	struct qlw_nvram_target target[16];
	u_int8_t	reserved_2[3];
	u_int8_t	checksum;
} __packed;

struct qlw_nvram_bus {
	u_int8_t	config1;
	u_int8_t	reset_delay;
	u_int8_t	retry_count;
	u_int8_t	retry_delay;
	u_int8_t	config2;
	u_int8_t	reserved_0;
	u_int16_t	selection_timeout;
	u_int16_t	max_queue_depth;
	u_int8_t	reserved_1[6];
	struct qlw_nvram_target target[16];
} __packed;

struct qlw_nvram_1080 {
	u_int8_t	id[4];
	u_int8_t	nvram_version;
	u_int8_t	flags1;
	u_int16_t	flags2;
	u_int8_t	reserved_0[8];
	u_int8_t	isp_config;
	u_int8_t	termination;
	u_int16_t	isp_parameter;
	u_int16_t	fw_features;
	u_int16_t	reserved_1;
	struct qlw_nvram_bus bus[2];
	u_int8_t	reserved_2[2];
	u_int16_t	subsystem_vendor_id;
	u_int16_t	subsystem_device_id;
	u_int8_t	reserved_3;
	u_int8_t	checksum;
} __packed;

struct qlw_nvram {
	u_int8_t	id[4];
	u_int8_t	nvram_version;
	u_int8_t	data[251];
};

#define QLW_TARGET_PPR		0x0020
#define QLW_TARGET_ASYNC	0x0040
#define QLW_TARGET_NARROW	0x0080
#define QLW_TARGET_RENEG	0x0100
#define QLW_TARGET_QFRZ		0x0200
#define QLW_TARGET_ARQ		0x0400
#define QLW_TARGET_TAGS		0x0800
#define QLW_TARGET_SYNC		0x1000
#define QLW_TARGET_WIDE		0x2000
#define QLW_TARGET_PARITY	0x4000
#define QLW_TARGET_DISC		0x8000
#define QLW_TARGET_SAFE		0xc500
#define QLW_TARGET_DEFAULT	0xfd00

#define QLW_IOCB_CMD_HEAD_OF_QUEUE	0x0002
#define QLW_IOCB_CMD_ORDERED_QUEUE	0x0004
#define QLW_IOCB_CMD_SIMPLE_QUEUE	0x0008
#define QLW_IOCB_CMD_NO_DATA		0x0000
#define QLW_IOCB_CMD_READ_DATA		0x0020
#define QLW_IOCB_CMD_WRITE_DATA		0x0040
#define QLW_IOCB_CMD_NO_FAST_POST	0x0080

struct qlw_iocb_hdr {
	u_int8_t	entry_type;
	u_int8_t	entry_count;
	u_int8_t	seqno;
	u_int8_t	flags;
} __packed;

#define QLW_IOCB_SEGS_PER_CMD		4
#define QLW_IOCB_SEGS_PER_CONT		7

struct qlw_iocb_seg {
	u_int32_t	seg_addr;
	u_int32_t	seg_len;
} __packed;

/* IOCB types */
#define QLW_IOCB_CMD_TYPE_0		0x01
#define QLW_IOCB_CONT_TYPE_0		0x02
#define QLW_IOCB_STATUS			0x03
#define QLW_IOCB_MARKER			0x04

struct qlw_iocb_req0 {
	struct qlw_iocb_hdr hdr;	/* QLW_IOCB_REQ_TYPE0 */

	u_int32_t	handle;
	u_int16_t	device;
	u_int16_t	ccblen;
	u_int16_t	flags;
	u_int16_t	reserved;
	u_int16_t	timeout;
	u_int16_t	seg_count;
	u_int8_t	cdb[12];
	struct qlw_iocb_seg segs[4];
} __packed;

struct qlw_iocb_cont0 {
	struct qlw_iocb_hdr hdr;	/* QLW_IOCB_CONT_TYPE_0 */

	u_int32_t	reserved;
	struct qlw_iocb_seg segs[7];
} __packed;

struct qlw_iocb_status {
	struct qlw_iocb_hdr hdr;

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
#define QLW_IOCB_STATUS_COMPLETE	0x0000
#define QLW_IOCB_STATUS_INCOMPLETE	0x0001
#define QLW_IOCB_STATUS_DMA_ERROR	0x0002
#define QLW_IOCB_STATUS_RESET		0x0004
#define QLW_IOCB_STATUS_ABORTED		0x0005
#define QLW_IOCB_STATUS_TIMEOUT		0x0006
#define QLW_IOCB_STATUS_DATA_OVERRUN	0x0007
#define QLW_IOCB_STATUS_DATA_UNDERRUN	0x0015
#define QLW_IOCB_STATUS_QUEUE_FULL	0x001c
#define QLW_IOCB_STATUS_WIDE_FAILED	0x001f
#define QLW_IOCB_STATUS_SYNCXFER_FAILED	0x0020

#define QLW_STATE_GOT_BUS		0x0100
#define QLW_STATE_GOT_TARGET		0x0200

#define QLW_SCSI_STATUS_SENSE_VALID	0x0200

struct qlw_iocb_marker {
	struct qlw_iocb_hdr hdr;	/* QLW_IOCB_MARKER */

	u_int32_t	handle;
	u_int16_t	device;
	u_int16_t	modifier;
	u_int8_t	reserved2[52];

} __packed;

#define QLW_IOCB_MARKER_SYNC_ALL	2
