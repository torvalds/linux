/*	$OpenBSD: igc_base.h,v 1.4 2024/05/13 01:15:51 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_BASE_H_
#define _IGC_BASE_H_

/* Forward declaration */
struct igc_hw;

int		igc_init_hw_base(struct igc_hw *hw);
void		igc_power_down_phy_copper_base(struct igc_hw *hw);
int		igc_acquire_phy_base(struct igc_hw *hw);
void		igc_release_phy_base(struct igc_hw *hw);

/* Transmit Descriptor - Advanced */
union igc_adv_tx_desc {
	struct {
		uint64_t buffer_addr;	/* Address of descriptor's data buf */
		uint32_t cmd_type_len;
		uint32_t olinfo_status;
	} read;
	struct {
		uint64_t rsvd;		/* Reserved */
		uint32_t nxtseq_seed;
		uint32_t status;
	} wb;
};

/* Context descriptors */
struct igc_adv_tx_context_desc {
	uint32_t vlan_macip_lens;
	union {
		uint32_t launch_time;
		uint32_t seqnum_seed;
	};
	uint32_t type_tucmd_mlhl;
	uint32_t mss_l4len_idx;
};

/* Adv Transmit Descriptor Config Masks */
#define IGC_ADVTXD_DTALEN_MASK	0x0000FFFF
#define IGC_ADVTXD_DTYP_CTXT	0x00200000 /* Advanced Context Descriptor */
#define IGC_ADVTXD_DTYP_DATA	0x00300000 /* Advanced Data Descriptor */
#define IGC_ADVTXD_DCMD_EOP	0x01000000 /* End of Packet */
#define IGC_ADVTXD_DCMD_IFCS	0x02000000 /* Insert FCS (Ethernet CRC) */
#define IGC_ADVTXD_DCMD_RS	0x08000000 /* Report Status */
#define IGC_ADVTXD_DCMD_DDTYP_ISCSI	0x10000000 /* DDP hdr type or iSCSI */
#define IGC_ADVTXD_DCMD_DEXT	0x20000000 /* Descriptor extension (1=Adv) */
#define IGC_ADVTXD_DCMD_VLE	0x40000000 /* VLAN pkt enable */
#define IGC_ADVTXD_DCMD_TSE	0x80000000 /* TCP Seg enable */
#define IGC_ADVTXD_MAC_LINKSEC	0x00040000 /* Apply LinkSec on pkt */
#define IGC_ADVTXD_MAC_TSTAMP	0x00080000 /* IEEE1588 Timestamp pkt */
#define IGC_ADVTXD_STAT_SN_CRC	0x00000002 /* NXTSEQ/SEED prsnt in WB */
#define IGC_ADVTXD_IDX_SHIFT	4 /* Adv desc Index shift */
#define IGC_ADVTXD_POPTS_ISCO_1ST	0x00000000 /* 1st TSO of iSCSI PDU */
#define IGC_ADVTXD_POPTS_ISCO_MDL	0x00000800 /* Middle TSO of iSCSI PDU */
#define IGC_ADVTXD_POPTS_ISCO_LAST	0x00001000 /* Last TSO of iSCSI PDU */
/* 1st & Last TSO-full iSCSI PDU*/
#define IGC_ADVTXD_POPTS_ISCO_FULL	0x00001800
#define IGC_ADVTXD_POPTS_IPSEC	0x00000400 /* IPSec offload request */
#define IGC_ADVTXD_PAYLEN_SHIFT	14 /* Adv desc PAYLEN shift */
#define IGC_ADVTXD_PAYLEN_MASK	0xFFFFD000 /* Adv desc PAYLEN shift */

/* Advanced Transmit Context Descriptor Config */
#define IGC_ADVTXD_MACLEN_SHIFT		9 /* Adv ctxt desc mac len shift */
#define IGC_ADVTXD_VLAN_SHIFT		16 /* Adv ctxt vlan tag shift */
#define IGC_ADVTXD_TUCMD_IPV4		0x00000400 /* IP Packet Type: 1=IPv4 */
#define IGC_ADVTXD_TUCMD_IPV6		0x00000000 /* IP Packet Type: 0=IPv6 */
#define IGC_ADVTXD_TUCMD_L4T_UDP	0x00000000 /* L4 Packet TYPE of UDP */
#define IGC_ADVTXD_TUCMD_L4T_TCP	0x00000800 /* L4 Packet TYPE of TCP */
#define IGC_ADVTXD_TUCMD_L4T_SCTP	0x00001000 /* L4 Packet TYPE of SCTP */
#define IGC_ADVTXD_TUCMD_IPSEC_TYPE_ESP	0x00002000 /* IPSec Type ESP */
/* IPSec Encrypt Enable for ESP */
#define IGC_ADVTXD_TUCMD_IPSEC_ENCRYPT_EN	0x00004000
/* Req requires Markers and CRC */
#define IGC_ADVTXD_TUCMD_MKRREQ		0x00002000
#define IGC_ADVTXD_L4LEN_SHIFT		8 	/* Adv ctxt L4LEN shift */
#define IGC_ADVTXD_MSS_SHIFT		16	/* Adv ctxt MSS shift */
/* Adv ctxt IPSec SA IDX mask */
#define IGC_ADVTXD_IPSEC_SA_INDEX_MASK	0x000000FF
/* Adv ctxt IPSec ESP len mask */
#define IGC_ADVTXD_IPSEC_ESP_LEN_MASK	0x000000FF

#define IGC_RAR_ENTRIES_BASE		16

/* Receive Descriptor - Advanced */
union igc_adv_rx_desc {
	struct {
		uint64_t pkt_addr; /* Packet buffer address */
		uint64_t hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			union {
				uint32_t data;
				struct {
					uint16_t pkt_info; /* Pkt type */
					/* Split Header, header buffer len */
					uint16_t hdr_info;
				} hs_rss;
			} lo_dword;
			union {
				uint32_t rss; /* RSS hash */
				struct {
					uint16_t ip_id; /* IP id */
					uint16_t csum; /* Packet checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			uint32_t status_error; /* ext status/error */
			uint16_t length; /* Packet length */
			uint16_t vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

/* Additional Transmit Descriptor Control definitions */
#define IGC_TXDCTL_QUEUE_ENABLE	0x02000000 /* Ena specific Tx Queue */

/* Additional Receive Descriptor Control definitions */
#define IGC_RXDCTL_QUEUE_ENABLE	0x02000000 /* Ena specific Rx Queue */

/* SRRCTL bit definitions */
#define IGC_SRRCTL_BSIZEPKT_SHIFT	10	/* Shift _right_ */
#define IGC_SRRCTL_BSIZEHDRSIZE_SHIFT	2	/* Shift _left_ */
#define IGC_SRRCTL_DESCTYPE_ADV_ONEBUF	0x02000000

#endif /* _IGC_BASE_H_ */
