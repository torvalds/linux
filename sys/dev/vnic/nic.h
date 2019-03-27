/*
 * Copyright (C) 2015 Cavium Inc.
 * All rights reserved.
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
 *
 */

#ifndef NIC_H
#define	NIC_H

/* PCI vendor ID */
#define PCI_VENDOR_ID_CAVIUM			0x177D
/* PCI device IDs */
#define	PCI_DEVICE_ID_THUNDER_NIC_PF		0xA01E
#define	PCI_DEVICE_ID_THUNDER_PASS1_NIC_VF	0x0011
#define	PCI_DEVICE_ID_THUNDER_NIC_VF		0xA034
#define	PCI_DEVICE_ID_THUNDER_BGX		0xA026

/* PCI BAR nos */
#define	PCI_CFG_REG_BAR_NUM		0
#define	PCI_MSIX_REG_BAR_NUM		4

/* PCI revision IDs */
#define	PCI_REVID_PASS2			8

/* NIC SRIOV VF count */
#define	MAX_NUM_VFS_SUPPORTED		128
#define	DEFAULT_NUM_VF_ENABLED		8

#define	NIC_TNS_BYPASS_MODE		0
#define	NIC_TNS_MODE			1

/* NIC priv flags */
#define	NIC_SRIOV_ENABLED		(1 << 0)
#define	NIC_TNS_ENABLED			(1 << 1)

/* ARM64TODO */
#if 0
/* VNIC HW optimiation features */
#define VNIC_RSS_SUPPORT
#define VNIC_MULTI_QSET_SUPPORT
#endif

/* Min/Max packet size */
#define	NIC_HW_MIN_FRS			64
#define	NIC_HW_MAX_FRS			9200 /* 9216 max packet including FCS */

/* Max pkinds */
#define	NIC_MAX_PKIND			16

/*
 * Rx Channels */
/* Receive channel configuration in TNS bypass mode
 * Below is configuration in TNS bypass mode
 * BGX0-LMAC0-CHAN0 - VNIC CHAN0
 * BGX0-LMAC1-CHAN0 - VNIC CHAN16
 * ...
 * BGX1-LMAC0-CHAN0 - VNIC CHAN128
 * ...
 * BGX1-LMAC3-CHAN0 - VNIC CHAN174
 */
#define	NIC_INTF_COUNT			2  /* Interfaces btw VNIC and TNS/BGX */
#define	NIC_CHANS_PER_INF		128
#define	NIC_MAX_CHANS			(NIC_INTF_COUNT * NIC_CHANS_PER_INF)
#define	NIC_CPI_COUNT			2048 /* No of channel parse indices */

/* TNS bypass mode: 1-1 mapping between VNIC and BGX:LMAC */
#define	NIC_MAX_BGX			MAX_BGX_PER_CN88XX
#define	NIC_CPI_PER_BGX			(NIC_CPI_COUNT / NIC_MAX_BGX)
#define	NIC_MAX_CPI_PER_LMAC		64 /* Max when CPI_ALG is IP diffserv */
#define	NIC_RSSI_PER_BGX		(NIC_RSSI_COUNT / NIC_MAX_BGX)

/* Tx scheduling */
#define	NIC_MAX_TL4			1024
#define	NIC_MAX_TL4_SHAPERS		256 /* 1 shaper for 4 TL4s */
#define	NIC_MAX_TL3			256
#define	NIC_MAX_TL3_SHAPERS		64  /* 1 shaper for 4 TL3s */
#define	NIC_MAX_TL2			64
#define	NIC_MAX_TL2_SHAPERS		2  /* 1 shaper for 32 TL2s */
#define	NIC_MAX_TL1			2

/* TNS bypass mode */
#define	NIC_TL2_PER_BGX			32
#define	NIC_TL4_PER_BGX			(NIC_MAX_TL4 / NIC_MAX_BGX)
#define	NIC_TL4_PER_LMAC		(NIC_MAX_TL4 / NIC_CHANS_PER_INF)

/* NIC VF Interrupts */
#define	NICVF_INTR_CQ			0
#define	NICVF_INTR_SQ			1
#define	NICVF_INTR_RBDR			2
#define	NICVF_INTR_PKT_DROP		3
#define	NICVF_INTR_TCP_TIMER		4
#define	NICVF_INTR_MBOX			5
#define	NICVF_INTR_QS_ERR		6

#define	NICVF_INTR_CQ_SHIFT		0
#define	NICVF_INTR_SQ_SHIFT		8
#define	NICVF_INTR_RBDR_SHIFT		16
#define	NICVF_INTR_PKT_DROP_SHIFT	20
#define	NICVF_INTR_TCP_TIMER_SHIFT	21
#define	NICVF_INTR_MBOX_SHIFT		22
#define	NICVF_INTR_QS_ERR_SHIFT		23

#define	NICVF_INTR_CQ_MASK		(0xFF << NICVF_INTR_CQ_SHIFT)
#define	NICVF_INTR_SQ_MASK		(0xFF << NICVF_INTR_SQ_SHIFT)
#define	NICVF_INTR_RBDR_MASK		(0x03 << NICVF_INTR_RBDR_SHIFT)
#define	NICVF_INTR_PKT_DROP_MASK	(1 << NICVF_INTR_PKT_DROP_SHIFT)
#define	NICVF_INTR_TCP_TIMER_MASK	(1 << NICVF_INTR_TCP_TIMER_SHIFT)
#define	NICVF_INTR_MBOX_MASK		(1 << NICVF_INTR_MBOX_SHIFT)
#define	NICVF_INTR_QS_ERR_MASK		(1 << NICVF_INTR_QS_ERR_SHIFT)

/* MSI-X interrupts */
#define	NIC_PF_MSIX_VECTORS		10
#define	NIC_VF_MSIX_VECTORS		20

#define	NIC_PF_INTR_ID_ECC0_SBE		0
#define	NIC_PF_INTR_ID_ECC0_DBE		1
#define	NIC_PF_INTR_ID_ECC1_SBE		2
#define	NIC_PF_INTR_ID_ECC1_DBE		3
#define	NIC_PF_INTR_ID_ECC2_SBE		4
#define	NIC_PF_INTR_ID_ECC2_DBE		5
#define	NIC_PF_INTR_ID_ECC3_SBE		6
#define	NIC_PF_INTR_ID_ECC3_DBE		7
#define	NIC_PF_INTR_ID_MBOX0		8
#define	NIC_PF_INTR_ID_MBOX1		9

struct msix_entry {
	struct resource *	irq_res;
	void *			handle;
};

/*
 * Global timer for CQ timer thresh interrupts
 * Calculated for SCLK of 700Mhz
 * value written should be a 1/16th of what is expected
 *
 * 1 tick per 0.05usec = value of 2.2
 * This 10% would be covered in CQ timer thresh value
 */
#define NICPF_CLK_PER_INT_TICK		2

/*
 * Time to wait before we decide that a SQ is stuck.
 *
 * Since both pkt rx and tx notifications are done with same CQ,
 * when packets are being received at very high rate (eg: L2 forwarding)
 * then freeing transmitted skbs will be delayed and watchdog
 * will kick in, resetting interface. Hence keeping this value high.
 */
#define	NICVF_TX_TIMEOUT		(50 * HZ)

#define	NIC_RSSI_COUNT			4096 /* Total no of RSS indices */
#define	NIC_MAX_RSS_HASH_BITS		8
#define	NIC_MAX_RSS_IDR_TBL_SIZE	(1 << NIC_MAX_RSS_HASH_BITS)
#define	RSS_HASH_KEY_SIZE		5 /* 320 bit key */

struct nicvf_rss_info {
	boolean_t enable;
#define	RSS_L2_EXTENDED_HASH_ENA	(1UL << 0)
#define	RSS_IP_HASH_ENA			(1UL << 1)
#define	RSS_TCP_HASH_ENA		(1UL << 2)
#define	RSS_TCP_SYN_DIS			(1UL << 3)
#define	RSS_UDP_HASH_ENA		(1UL << 4)
#define	RSS_L4_EXTENDED_HASH_ENA	(1UL << 5)
#define	RSS_ROCE_ENA			(1UL << 6)
#define	RSS_L3_BI_DIRECTION_ENA		(1UL << 7)
#define	RSS_L4_BI_DIRECTION_ENA		(1UL << 8)
	uint64_t cfg;
	uint8_t  hash_bits;
	uint16_t rss_size;
	uint8_t  ind_tbl[NIC_MAX_RSS_IDR_TBL_SIZE];
	uint64_t key[RSS_HASH_KEY_SIZE];
};

enum rx_stats_reg_offset {
	RX_OCTS = 0x0,
	RX_UCAST = 0x1,
	RX_BCAST = 0x2,
	RX_MCAST = 0x3,
	RX_RED = 0x4,
	RX_RED_OCTS = 0x5,
	RX_ORUN = 0x6,
	RX_ORUN_OCTS = 0x7,
	RX_FCS = 0x8,
	RX_L2ERR = 0x9,
	RX_DRP_BCAST = 0xa,
	RX_DRP_MCAST = 0xb,
	RX_DRP_L3BCAST = 0xc,
	RX_DRP_L3MCAST = 0xd,
	RX_STATS_ENUM_LAST,
};

enum tx_stats_reg_offset {
	TX_OCTS = 0x0,
	TX_UCAST = 0x1,
	TX_BCAST = 0x2,
	TX_MCAST = 0x3,
	TX_DROP = 0x4,
	TX_STATS_ENUM_LAST,
};

struct nicvf_hw_stats {
	uint64_t rx_bytes;
	uint64_t rx_ucast_frames;
	uint64_t rx_bcast_frames;
	uint64_t rx_mcast_frames;
	uint64_t rx_fcs_errors;
	uint64_t rx_l2_errors;
	uint64_t rx_drop_red;
	uint64_t rx_drop_red_bytes;
	uint64_t rx_drop_overrun;
	uint64_t rx_drop_overrun_bytes;
	uint64_t rx_drop_bcast;
	uint64_t rx_drop_mcast;
	uint64_t rx_drop_l3_bcast;
	uint64_t rx_drop_l3_mcast;
	uint64_t rx_bgx_truncated_pkts;
	uint64_t rx_jabber_errs;
	uint64_t rx_fcs_errs;
	uint64_t rx_bgx_errs;
	uint64_t rx_prel2_errs;
	uint64_t rx_l2_hdr_malformed;
	uint64_t rx_oversize;
	uint64_t rx_undersize;
	uint64_t rx_l2_len_mismatch;
	uint64_t rx_l2_pclp;
	uint64_t rx_ip_ver_errs;
	uint64_t rx_ip_csum_errs;
	uint64_t rx_ip_hdr_malformed;
	uint64_t rx_ip_payload_malformed;
	uint64_t rx_ip_ttl_errs;
	uint64_t rx_l3_pclp;
	uint64_t rx_l4_malformed;
	uint64_t rx_l4_csum_errs;
	uint64_t rx_udp_len_errs;
	uint64_t rx_l4_port_errs;
	uint64_t rx_tcp_flag_errs;
	uint64_t rx_tcp_offset_errs;
	uint64_t rx_l4_pclp;
	uint64_t rx_truncated_pkts;

	uint64_t tx_bytes_ok;
	uint64_t tx_ucast_frames_ok;
	uint64_t tx_bcast_frames_ok;
	uint64_t tx_mcast_frames_ok;
	uint64_t tx_drops;
};

struct nicvf_drv_stats {
	/* Rx */
	uint64_t rx_frames_ok;
	uint64_t rx_frames_64;
	uint64_t rx_frames_127;
	uint64_t rx_frames_255;
	uint64_t rx_frames_511;
	uint64_t rx_frames_1023;
	uint64_t rx_frames_1518;
	uint64_t rx_frames_jumbo;
	uint64_t rx_drops;

	/* Tx */
	uint64_t tx_frames_ok;
	uint64_t tx_drops;
	uint64_t tx_tso;
	uint64_t txq_stop;
	uint64_t txq_wake;
};

struct nicvf {
	struct nicvf		*pnicvf;
	device_t		dev;

	struct ifnet *		ifp;
	struct sx		core_sx;
	struct ifmedia		if_media;
	uint32_t		if_flags;

	uint8_t			hwaddr[ETHER_ADDR_LEN];
	uint8_t			vf_id;
	uint8_t			node;
	boolean_t		tns_mode:1;
	boolean_t		sqs_mode:1;
	bool			loopback_supported:1;
	struct nicvf_rss_info	rss_info;
	uint16_t		mtu;
	struct queue_set	*qs;
	uint8_t			rx_queues;
	uint8_t			tx_queues;
	uint8_t			max_queues;
	struct resource		*reg_base;
	boolean_t		link_up;
	boolean_t		hw_tso;
	uint8_t			duplex;
	uint32_t		speed;
	uint8_t			cpi_alg;
	/* Interrupt coalescing settings */
	uint32_t		cq_coalesce_usecs;

	uint32_t		msg_enable;
	struct nicvf_hw_stats	hw_stats;
	struct nicvf_drv_stats	drv_stats;
	struct bgx_stats	bgx_stats;

	/* Interface statistics */
	struct callout		stats_callout;
	struct mtx		stats_mtx;

	/* MSI-X  */
	boolean_t		msix_enabled;
	uint8_t			num_vec;
	struct msix_entry	msix_entries[NIC_VF_MSIX_VECTORS];
	struct resource *	msix_table_res;
	char			irq_name[NIC_VF_MSIX_VECTORS][20];
	boolean_t		irq_allocated[NIC_VF_MSIX_VECTORS];

	/* VF <-> PF mailbox communication */
	boolean_t		pf_acked;
	boolean_t		pf_nacked;
} __aligned(CACHE_LINE_SIZE);

/*
 * PF <--> VF Mailbox communication
 * Eight 64bit registers are shared between PF and VF.
 * Separate set for each VF.
 * Writing '1' into last register mbx7 means end of message.
 */

/* PF <--> VF mailbox communication */
#define	NIC_PF_VF_MAILBOX_SIZE		2
#define	NIC_MBOX_MSG_TIMEOUT		2000 /* ms */

/* Mailbox message types */
#define	NIC_MBOX_MSG_READY		0x01	/* Is PF ready to rcv msgs */
#define	NIC_MBOX_MSG_ACK		0x02	/* ACK the message received */
#define	NIC_MBOX_MSG_NACK		0x03	/* NACK the message received */
#define	NIC_MBOX_MSG_QS_CFG		0x04	/* Configure Qset */
#define	NIC_MBOX_MSG_RQ_CFG		0x05	/* Configure receive queue */
#define	NIC_MBOX_MSG_SQ_CFG		0x06	/* Configure Send queue */
#define	NIC_MBOX_MSG_RQ_DROP_CFG	0x07	/* Configure receive queue */
#define	NIC_MBOX_MSG_SET_MAC		0x08	/* Add MAC ID to DMAC filter */
#define	NIC_MBOX_MSG_SET_MAX_FRS	0x09	/* Set max frame size */
#define	NIC_MBOX_MSG_CPI_CFG		0x0A	/* Config CPI, RSSI */
#define	NIC_MBOX_MSG_RSS_SIZE		0x0B	/* Get RSS indir_tbl size */
#define	NIC_MBOX_MSG_RSS_CFG		0x0C	/* Config RSS table */
#define	NIC_MBOX_MSG_RSS_CFG_CONT	0x0D	/* RSS config continuation */
#define	NIC_MBOX_MSG_RQ_BP_CFG		0x0E	/* RQ backpressure config */
#define	NIC_MBOX_MSG_RQ_SW_SYNC		0x0F	/* Flush inflight pkts to RQ */
#define	NIC_MBOX_MSG_BGX_STATS		0x10	/* Get stats from BGX */
#define	NIC_MBOX_MSG_BGX_LINK_CHANGE	0x11	/* BGX:LMAC link status */
#define	NIC_MBOX_MSG_ALLOC_SQS		0x12	/* Allocate secondary Qset */
#define	NIC_MBOX_MSG_NICVF_PTR		0x13	/* Send nicvf ptr to PF */
#define	NIC_MBOX_MSG_PNICVF_PTR		0x14	/* Get primary qset nicvf ptr */
#define	NIC_MBOX_MSG_SNICVF_PTR		0x15	/* Send sqet nicvf ptr to PVF */
#define	NIC_MBOX_MSG_LOOPBACK		0x16	/* Set interface in loopback */
#define	NIC_MBOX_MSG_CFG_DONE		0xF0	/* VF configuration done */
#define	NIC_MBOX_MSG_SHUTDOWN		0xF1	/* VF is being shutdown */

struct nic_cfg_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint8_t		node_id;
	boolean_t	tns_mode:1;
	boolean_t	sqs_mode:1;
	boolean_t	loopback_supported:1;
	uint8_t	mac_addr[ETHER_ADDR_LEN];
};

/* Qset configuration */
struct qs_cfg_msg {
	uint8_t		msg;
	uint8_t		num;
	uint8_t		sqs_count;
	uint64_t	cfg;
};

/* Receive queue configuration */
struct rq_cfg_msg {
	uint8_t		msg;
	uint8_t		qs_num;
	uint8_t		rq_num;
	uint64_t	cfg;
};

/* Send queue configuration */
struct sq_cfg_msg {
	uint8_t		msg;
	uint8_t		qs_num;
	uint8_t		sq_num;
	boolean_t	sqs_mode;
	uint64_t	cfg;
};

/* Set VF's MAC address */
struct set_mac_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint8_t		mac_addr[ETHER_ADDR_LEN];
};

/* Set Maximum frame size */
struct set_frs_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint16_t	max_frs;
};

/* Set CPI algorithm type */
struct cpi_cfg_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint8_t		rq_cnt;
	uint8_t		cpi_alg;
};

/* Get RSS table size */
struct rss_sz_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint16_t	ind_tbl_size;
};

/* Set RSS configuration */
struct rss_cfg_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint8_t		hash_bits;
	uint8_t		tbl_len;
	uint8_t		tbl_offset;
#define	RSS_IND_TBL_LEN_PER_MBX_MSG	8
	uint8_t		ind_tbl[RSS_IND_TBL_LEN_PER_MBX_MSG];
};

struct bgx_stats_msg {
	uint8_t		msg;
	uint8_t		vf_id;
	uint8_t		rx;
	uint8_t		idx;
	uint64_t	stats;
};

/* Physical interface link status */
struct bgx_link_status {
	uint8_t		msg;
	uint8_t		link_up;
	uint8_t		duplex;
	uint32_t	speed;
};

/* Set interface in loopback mode */
struct set_loopback {
	uint8_t		msg;
	uint8_t		vf_id;
	boolean_t	enable;
};

/* 128 bit shared memory between PF and each VF */
union nic_mbx {
	struct {
		uint8_t msg;
	} msg;
	struct nic_cfg_msg	nic_cfg;
	struct qs_cfg_msg	qs;
	struct rq_cfg_msg	rq;
	struct sq_cfg_msg	sq;
	struct set_mac_msg	mac;
	struct set_frs_msg	frs;
	struct cpi_cfg_msg	cpi_cfg;
	struct rss_sz_msg	rss_size;
	struct rss_cfg_msg	rss_cfg;
	struct bgx_stats_msg	bgx_stats;
	struct bgx_link_status	link_status;
	struct set_loopback	lbk;
};

#define	NIC_NODE_ID_MASK	0x03
#define	NIC_NODE_ID_SHIFT	44

static __inline int
nic_get_node_id(struct resource *res)
{
	pci_addr_t addr;

	addr = rman_get_start(res);
	return ((addr >> NIC_NODE_ID_SHIFT) & NIC_NODE_ID_MASK);
}

static __inline boolean_t
pass1_silicon(device_t dev)
{

	/* Check if the chip revision is < Pass2 */
	return (pci_get_revid(dev) < PCI_REVID_PASS2);
}

int nicvf_send_msg_to_pf(struct nicvf *vf, union nic_mbx *mbx);

#endif /* NIC_H */
