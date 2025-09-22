/*	$OpenBSD: if_bwfm_pci.h,v 1.9 2022/11/08 18:28:10 kettenis Exp $	*/
/*
 * Copyright (c) 2010-2016 Broadcom Corporation
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

/* Registers */
#define BWFM_PCI_BAR0_WINDOW			0x80
#define BWFM_PCI_BAR0_REG_SIZE			0x1000

#define BWFM_PCI_ARMCR4REG_BANKIDX		0x40
#define BWFM_PCI_ARMCR4REG_BANKPDA		0x4C

#define BWFM_PCI_REG_SBMBX			0x98

#define BWFM_PCI_PCIE2REG_INTMASK		0x24
#define BWFM_PCI_PCIE2REG_MAILBOXINT		0x48
#define BWFM_PCI_PCIE2REG_MAILBOXMASK		0x4C
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_FN0_0	0x0100
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_FN0_1	0x0200
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H0_DB0	0x10000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H0_DB1	0x20000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H1_DB0	0x40000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H1_DB1	0x80000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H2_DB0	0x100000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H2_DB1	0x200000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H3_DB0	0x400000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H3_DB1	0x800000
#define  BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H_DB		\
		(BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H0_DB0 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H0_DB1 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H1_DB0 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H1_DB1 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H2_DB0 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H2_DB1 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H3_DB0 |	\
		 BWFM_PCI_PCIE2REG_MAILBOXMASK_INT_D2H3_DB1)
#define BWFM_PCI_PCIE2REG_CONFIGADDR		0x120
#define BWFM_PCI_PCIE2REG_CONFIGDATA		0x124
#define BWFM_PCI_PCIE2REG_H2D_MAILBOX_0		0x140
#define BWFM_PCI_PCIE2REG_H2D_MAILBOX_1		0x144

#define BWFM_PCI_64_PCIE2REG_INTMASK		0xC14
#define BWFM_PCI_64_PCIE2REG_MAILBOXINT		0xC30
#define BWFM_PCI_64_PCIE2REG_MAILBOXMASK	0xC34
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H0_DB0	1
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H0_DB1	2
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H1_DB0	4
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H1_DB1	8
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H2_DB0	0x10
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H2_DB1	0x20
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H3_DB0	0x40
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H3_DB1	0x80
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H4_DB0	0x100
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H4_DB1	0x200
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H5_DB0	0x400
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H5_DB1	0x800
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H6_DB0	0x1000
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H6_DB1	0x2000
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H7_DB0	0x4000
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H7_DB1	0x8000
#define  BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H_DB			\
		(BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H0_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H0_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H1_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H1_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H2_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H2_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H3_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H3_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H4_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H4_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H5_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H5_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H6_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H6_DB1 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H7_DB0 |	\
		 BWFM_PCI_64_PCIE2REG_MAILBOXMASK_INT_D2H7_DB1)
#define BWFM_PCI_64_PCIE2REG_H2D_MAILBOX_0	0xA20
#define BWFM_PCI_64_PCIE2REG_H2D_MAILBOX_1	0xA24

#define BWFM_PCI_CFGREG_STATUS_CMD		0x004
#define BWFM_PCI_CFGREG_PM_CSR			0x04C
#define BWFM_PCI_CFGREG_MSI_CAP			0x058
#define BWFM_PCI_CFGREG_MSI_ADDR_L		0x05C
#define BWFM_PCI_CFGREG_MSI_ADDR_H		0x060
#define BWFM_PCI_CFGREG_MSI_DATA		0x064
#define BWFM_PCI_CFGREG_LINK_STATUS_CTRL	0x0BC
#define  BWFM_PCI_CFGREG_LINK_STATUS_CTRL_ASPM_ENAB	0x3
#define BWFM_PCI_CFGREG_LINK_STATUS_CTRL2	0x0DC
#define BWFM_PCI_CFGREG_RBAR_CTRL		0x228
#define BWFM_PCI_CFGREG_PML1_SUB_CTRL1		0x248
#define BWFM_PCI_CFGREG_REG_BAR2_CONFIG		0x4E0
#define BWFM_PCI_CFGREG_REG_BAR3_CONFIG		0x4F4

#define BWFM_RAMSIZE				0x6c
#define  BWFM_RAMSIZE_MAGIC			0x534d4152	/* SMAR */

#define BWFM_SHARED_INFO			0x000
#define  BWFM_SHARED_INFO_MIN_VERSION			5
#define  BWFM_SHARED_INFO_MAX_VERSION			7
#define  BWFM_SHARED_INFO_VERSION_MASK			0x00FF
#define  BWFM_SHARED_INFO_DMA_INDEX			0x10000
#define  BWFM_SHARED_INFO_DMA_2B_IDX			0x100000
#define  BWFM_SHARED_INFO_USE_MAILBOX			0x2000000
#define  BWFM_SHARED_INFO_TIMESTAMP_DB0			0x8000000
#define  BWFM_SHARED_INFO_HOSTRDY_DB1			0x10000000
#define  BWFM_SHARED_INFO_NO_OOB_DW			0x20000000
#define  BWFM_SHARED_INFO_INBAND_DS			0x40000000
#define  BWFM_SHARED_INFO_SHARED_DAR			0x80000000
#define BWFM_SHARED_CONSOLE_ADDR		0x14
#define BWFM_SHARED_MAX_RXBUFPOST		0x22
#define  BWFM_SHARED_MAX_RXBUFPOST_DEFAULT		255
#define BWFM_SHARED_RX_DATAOFFSET		0x24
#define BWFM_SHARED_HTOD_MB_DATA_ADDR		0x28
#define  BWFM_PCI_H2D_HOST_D3_INFORM			0x00000001
#define  BWFM_PCI_H2D_HOST_DS_ACK			0x00000002
#define  BWFM_PCI_H2D_HOST_D0_INFORM_IN_USE		0x00000008
#define  BWFM_PCI_H2D_HOST_D0_INFORM			0x00000010
#define BWFM_SHARED_DTOH_MB_DATA_ADDR		0x2c
#define  BWFM_PCI_D2H_DEV_D3_ACK			0x00000001
#define  BWFM_PCI_D2H_DEV_DS_ENTER_REQ			0x00000002
#define  BWFM_PCI_D2H_DEV_DS_EXIT_NOTE			0x00000004
#define  BWFM_PCI_D2H_DEV_FWHALT			0x10000000
#define BWFM_SHARED_RING_INFO_ADDR		0x30
#define BWFM_SHARED_DMA_SCRATCH_LEN		0x34
#define BWFM_SHARED_DMA_SCRATCH_ADDR_LOW	0x38
#define BWFM_SHARED_DMA_SCRATCH_ADDR_HIGH	0x3c
#define BWFM_SHARED_DMA_RINGUPD_LEN		0x40
#define BWFM_SHARED_DMA_RINGUPD_ADDR_LOW	0x44
#define BWFM_SHARED_DMA_RINGUPD_ADDR_HIGH	0x48
#define BWFM_SHARED_HOST_CAP			0x54
#define  BWFM_SHARED_HOST_CAP_H2D_ENABLE_HOSTRDY	0x00000400
#define  BWFM_SHARED_HOST_CAP_DS_NO_OOB_DW		0x00001000
#define  BWFM_SHARED_HOST_CAP_H2D_DAR			0x00010000
#define BWFM_SHARED_HOST_CAP2			0x70

#define BWFM_RING_MAX_ITEM			0x04
#define BWFM_RING_LEN_ITEMS			0x06
#define BWFM_RING_MEM_BASE_ADDR_LOW		0x08
#define BWFM_RING_MEM_BASE_ADDR_HIGH		0x0c
#define BWFM_RING_MEM_SZ			16

#define BWFM_CONSOLE_BUFADDR			0x08
#define BWFM_CONSOLE_BUFSIZE			0x0c
#define BWFM_CONSOLE_WRITEIDX			0x10

#define BWFM_RANDOM_SEED_MAGIC			0xfeedc0de
#define BWFM_RANDOM_SEED_LENGTH			0x100

struct bwfm_pci_random_seed_footer {
	uint32_t		length;
	uint32_t		magic;
};

struct bwfm_pci_ringinfo {
	uint32_t		ringmem;
	uint32_t		h2d_w_idx_ptr;
	uint32_t		h2d_r_idx_ptr;
	uint32_t		d2h_w_idx_ptr;
	uint32_t		d2h_r_idx_ptr;
	uint32_t		h2d_w_idx_hostaddr_low;
	uint32_t		h2d_w_idx_hostaddr_high;
	uint32_t		h2d_r_idx_hostaddr_low;
	uint32_t		h2d_r_idx_hostaddr_high;
	uint32_t		d2h_w_idx_hostaddr_low;
	uint32_t		d2h_w_idx_hostaddr_high;
	uint32_t		d2h_r_idx_hostaddr_low;
	uint32_t		d2h_r_idx_hostaddr_high;
	uint16_t		max_flowrings;
	uint16_t		max_submissionrings;
	uint16_t		max_completionrings;
};

/* Msgbuf defines */
#define MSGBUF_IOCTL_RESP_TIMEOUT		2000 /* msecs */
#define MSGBUF_IOCTL_REQ_PKTID			0xFFFE
#define MSGBUF_MAX_PKT_SIZE			2048
#define MSGBUF_MAX_CTL_PKT_SIZE			8192

#define MSGBUF_TYPE_GEN_STATUS			0x1
#define MSGBUF_TYPE_RING_STATUS			0x2
#define MSGBUF_TYPE_FLOW_RING_CREATE		0x3
#define MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT	0x4
#define MSGBUF_TYPE_FLOW_RING_DELETE		0x5
#define MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT	0x6
#define MSGBUF_TYPE_FLOW_RING_FLUSH		0x7
#define MSGBUF_TYPE_FLOW_RING_FLUSH_CMPLT	0x8
#define MSGBUF_TYPE_IOCTLPTR_REQ		0x9
#define MSGBUF_TYPE_IOCTLPTR_REQ_ACK		0xA
#define MSGBUF_TYPE_IOCTLRESP_BUF_POST		0xB
#define MSGBUF_TYPE_IOCTL_CMPLT			0xC
#define MSGBUF_TYPE_EVENT_BUF_POST		0xD
#define MSGBUF_TYPE_WL_EVENT			0xE
#define MSGBUF_TYPE_TX_POST			0xF
#define MSGBUF_TYPE_TX_STATUS			0x10
#define MSGBUF_TYPE_RXBUF_POST			0x11
#define MSGBUF_TYPE_RX_CMPLT			0x12
#define MSGBUF_TYPE_LPBK_DMAXFER		0x13
#define MSGBUF_TYPE_LPBK_DMAXFER_CMPLT		0x14
#define MSGBUF_TYPE_H2D_MAILBOX_DATA		0x23
#define MSGBUF_TYPE_D2H_MAILBOX_DATA		0x24

struct msgbuf_common_hdr {
	uint8_t			msgtype;
	uint8_t			ifidx;
	uint8_t			flags;
	uint8_t			rsvd0;
	uint32_t		request_id;
};

struct msgbuf_buf_addr {
	uint32_t		low_addr;
	uint32_t		high_addr;
};

struct msgbuf_ioctl_req_hdr {
	struct msgbuf_common_hdr	msg;
	uint32_t			cmd;
	uint16_t			trans_id;
	uint16_t			input_buf_len;
	uint16_t			output_buf_len;
	uint16_t			rsvd0[3];
	struct msgbuf_buf_addr		req_buf_addr;
	uint32_t			rsvd1[2];
};

struct msgbuf_tx_msghdr {
	struct msgbuf_common_hdr	msg;
	uint8_t				txhdr[ETHER_HDR_LEN];
	uint8_t				flags;
#define BWFM_MSGBUF_PKT_FLAGS_FRAME_802_3	(1 << 0)
#define BWFM_MSGBUF_PKT_FLAGS_PRIO_SHIFT	5
	uint8_t				seg_cnt;
	struct msgbuf_buf_addr		metadata_buf_addr;
	struct msgbuf_buf_addr		data_buf_addr;
	uint16_t			metadata_buf_len;
	uint16_t			data_len;
	uint32_t			rsvd0;
};

struct msgbuf_rx_bufpost {
	struct msgbuf_common_hdr	msg;
	uint16_t			metadata_buf_len;
	uint16_t			data_buf_len;
	uint32_t			rsvd0;
	struct msgbuf_buf_addr		metadata_buf_addr;
	struct msgbuf_buf_addr		data_buf_addr;
};

struct msgbuf_rx_ioctl_resp_or_event {
	struct msgbuf_common_hdr	msg;
	uint16_t			host_buf_len;
	uint16_t			rsvd0[3];
	struct msgbuf_buf_addr		host_buf_addr;
	uint32_t			rsvd1[4];
};

struct msgbuf_completion_hdr {
	uint16_t			status;
	uint16_t			flow_ring_id;
};

struct msgbuf_rx_event {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint16_t			event_data_len;
	uint16_t			seqnum;
	uint16_t			rsvd0[4];
};

struct msgbuf_ioctl_resp_hdr {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint16_t			resp_len;
	uint16_t			trans_id;
	uint32_t			cmd;
	uint32_t			rsvd0;
};

struct msgbuf_tx_status {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint16_t			metadata_len;
	uint16_t			tx_status;
};

struct msgbuf_rx_complete {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint16_t			metadata_len;
	uint16_t			data_len;
	uint16_t			data_offset;
	uint16_t			flags;
	uint32_t			rx_status_0;
	uint32_t			rx_status_1;
	uint32_t			rsvd0;
};

struct msgbuf_tx_flowring_create_req {
	struct msgbuf_common_hdr	msg;
	uint8_t				da[ETHER_ADDR_LEN];
	uint8_t				sa[ETHER_ADDR_LEN];
	uint8_t				tid;
	uint8_t				if_flags;
	uint16_t			flow_ring_id;
	uint8_t				tc;
	uint8_t				priority;
	uint16_t			int_vector;
	uint16_t			max_items;
	uint16_t			len_item;
	struct msgbuf_buf_addr		flow_ring_addr;
};

struct msgbuf_tx_flowring_delete_req {
	struct msgbuf_common_hdr	msg;
	uint16_t			flow_ring_id;
	uint16_t			reason;
	uint32_t			rsvd0[7];
};

struct msgbuf_flowring_create_resp {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint32_t			rsvd0[3];
};

struct msgbuf_flowring_delete_resp {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint32_t			rsvd0[3];
};

struct msgbuf_flowring_flush_resp {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint32_t			rsvd0[3];
};

struct msgbuf_h2d_mailbox_data {
	struct msgbuf_common_hdr	msg;
	uint32_t			data;
	uint32_t			rsvd0[7];
};

struct msgbuf_d2h_mailbox_data {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	uint32_t			data;
	uint32_t			rsvd0[2];
};
