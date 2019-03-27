/*	$OpenBSD: if_txpreg.h,v 1.35 2003/06/04 19:36:33 deraadt Exp $ */
/*	$FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Aaron Campbell <aaron@monkey.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Typhoon registers.
 */
#define	TXP_SRR				0x00	/* soft reset register */
#define	TXP_ISR				0x04	/* interrupt status register */
#define	TXP_IER				0x08	/* interrupt enable register */
#define	TXP_IMR				0x0c	/* interrupt mask register */
#define	TXP_SIR				0x10	/* self interrupt register */
#define	TXP_H2A_7			0x14	/* host->arm comm 7 */
#define	TXP_H2A_6			0x18	/* host->arm comm 6 */
#define	TXP_H2A_5			0x1c	/* host->arm comm 5 */
#define	TXP_H2A_4			0x20	/* host->arm comm 4 */
#define	TXP_H2A_3			0x24	/* host->arm comm 3 */
#define	TXP_H2A_2			0x28	/* host->arm comm 2 */
#define	TXP_H2A_1			0x2c	/* host->arm comm 1 */
#define	TXP_H2A_0			0x30	/* host->arm comm 0 */
#define	TXP_A2H_3			0x34	/* arm->host comm 3 */
#define	TXP_A2H_2			0x38	/* arm->host comm 2 */
#define	TXP_A2H_1			0x3c	/* arm->host comm 1 */
#define	TXP_A2H_0			0x40	/* arm->host comm 0 */

/*
 * interrupt bits (IMR, ISR, IER)
 */
#define	TXP_INT_RESERVED	0xffff0000
#define	TXP_INT_A2H_7		0x00008000	/* arm->host comm 7 */
#define	TXP_INT_A2H_6		0x00004000	/* arm->host comm 6 */
#define	TXP_INT_A2H_5		0x00002000	/* arm->host comm 5 */
#define	TXP_INT_A2H_4		0x00001000	/* arm->host comm 4 */
#define	TXP_INT_SELF		0x00000800	/* self interrupt */
#define	TXP_INT_PCI_TABORT	0x00000400	/* pci target abort */
#define	TXP_INT_PCI_MABORT	0x00000200	/* pci master abort */
#define	TXP_INT_DMA3		0x00000100	/* dma3 done */
#define	TXP_INT_DMA2		0x00000080	/* dma2 done */
#define	TXP_INT_DMA1		0x00000040	/* dma1 done */
#define	TXP_INT_DMA0		0x00000020	/* dma0 done */
#define	TXP_INT_A2H_3		0x00000010	/* arm->host comm 3 */
#define	TXP_INT_A2H_2		0x00000008	/* arm->host comm 2 */
#define	TXP_INT_A2H_1		0x00000004	/* arm->host comm 1 */
#define	TXP_INT_A2H_0		0x00000002	/* arm->host comm 0 */
#define	TXP_INT_LATCH		0x00000001	/* interrupt latch */

/*
 * Controller periodically generates TXP_INT_A2H_3 interrupt so
 * we don't want to see them in interrupt handler.
 */
#define	TXP_INTRS		0xFFFFFFEF
#define	TXP_INTR_ALL		0xFFFFFFFF
#define	TXP_INTR_NONE		0x00000000

/*
 * soft reset register (SRR)
 */
#define	TXP_SRR_ALL		0x0000007f	/* full reset */

/*
 * Typhoon boot commands.
 */
#define	TXP_BOOTCMD_NULL			0x00
#define	TXP_BOOTCMD_WAKEUP			0xfa
#define	TXP_BOOTCMD_DOWNLOAD_COMPLETE		0xfb
#define	TXP_BOOTCMD_SEGMENT_AVAILABLE		0xfc
#define	TXP_BOOTCMD_RUNTIME_IMAGE		0xfd
#define	TXP_BOOTCMD_REGISTER_BOOT_RECORD	0xff

/*
 * Typhoon runtime commands.
 */
#define	TXP_CMD_GLOBAL_RESET			0x00
#define	TXP_CMD_TX_ENABLE			0x01
#define	TXP_CMD_TX_DISABLE			0x02
#define	TXP_CMD_RX_ENABLE			0x03
#define	TXP_CMD_RX_DISABLE			0x04
#define	TXP_CMD_RX_FILTER_WRITE			0x05
#define	TXP_CMD_RX_FILTER_READ			0x06
#define	TXP_CMD_READ_STATISTICS			0x07
#define	TXP_CMD_CYCLE_STATISTICS		0x08
#define	TXP_CMD_CLEAR_STATISTICS		0x09
#define	TXP_CMD_MEMORY_READ			0x0a
#define	TXP_CMD_MEMORY_WRITE_SINGLE		0x0b
#define	TXP_CMD_VARIABLE_SECTION_READ		0x0c
#define	TXP_CMD_VARIABLE_SECTION_WRITE		0x0d
#define	TXP_CMD_STATIC_SECTION_READ		0x0e
#define	TXP_CMD_STATIC_SECTION_WRITE		0x0f
#define	TXP_CMD_IMAGE_SECTION_PROGRAM		0x10
#define	TXP_CMD_NVRAM_PAGE_READ			0x11
#define	TXP_CMD_NVRAM_PAGE_WRITE		0x12
#define	TXP_CMD_XCVR_SELECT			0x13
#define	TXP_CMD_TEST_MUX			0x14
#define	TXP_CMD_PHYLOOPBACK_ENABLE		0x15
#define	TXP_CMD_PHYLOOPBACK_DISABLE		0x16
#define	TXP_CMD_MAC_CONTROL_READ		0x17
#define	TXP_CMD_MAC_CONTROL_WRITE		0x18
#define	TXP_CMD_MAX_PKT_SIZE_READ		0x19
#define	TXP_CMD_MAX_PKT_SIZE_WRITE		0x1a
#define	TXP_CMD_MEDIA_STATUS_READ		0x1b
#define	TXP_CMD_MEDIA_STATUS_WRITE		0x1c
#define	TXP_CMD_NETWORK_DIAGS_READ		0x1d
#define	TXP_CMD_NETWORK_DIAGS_WRITE		0x1e
#define	TXP_CMD_PHY_MGMT_READ			0x1f
#define	TXP_CMD_PHY_MGMT_WRITE			0x20
#define	TXP_CMD_VARIABLE_PARAMETER_READ		0x21
#define	TXP_CMD_VARIABLE_PARAMETER_WRITE	0x22
#define	TXP_CMD_GOTO_SLEEP			0x23
#define	TXP_CMD_FIREWALL_CONTROL		0x24
#define	TXP_CMD_MCAST_HASH_MASK_WRITE		0x25
#define	TXP_CMD_STATION_ADDRESS_WRITE		0x26
#define	TXP_CMD_STATION_ADDRESS_READ		0x27
#define	TXP_CMD_STATION_MASK_WRITE		0x28
#define	TXP_CMD_STATION_MASK_READ		0x29
#define	TXP_CMD_VLAN_ETHER_TYPE_READ		0x2a
#define	TXP_CMD_VLAN_ETHER_TYPE_WRITE		0x2b
#define	TXP_CMD_VLAN_MASK_READ			0x2c
#define	TXP_CMD_VLAN_MASK_WRITE			0x2d
#define	TXP_CMD_BCAST_THROTTLE_WRITE		0x2e
#define	TXP_CMD_BCAST_THROTTLE_READ		0x2f
#define	TXP_CMD_DHCP_PREVENT_WRITE		0x30
#define	TXP_CMD_DHCP_PREVENT_READ		0x31
#define	TXP_CMD_RECV_BUFFER_CONTROL		0x32
#define	TXP_CMD_SOFTWARE_RESET			0x33
#define	TXP_CMD_CREATE_SA			0x34
#define	TXP_CMD_DELETE_SA			0x35
#define	TXP_CMD_ENABLE_RX_IP_OPTION		0x36
#define	TXP_CMD_RANDOM_NUMBER_CONTROL		0x37
#define	TXP_CMD_RANDOM_NUMBER_READ		0x38
#define	TXP_CMD_MATRIX_TABLE_MODE_WRITE		0x39
#define	TXP_CMD_MATRIX_DETAIL_READ		0x3a
#define	TXP_CMD_FILTER_ARRAY_READ		0x3b
#define	TXP_CMD_FILTER_DETAIL_READ		0x3c
#define	TXP_CMD_FILTER_TABLE_MODE_WRITE		0x3d
#define	TXP_CMD_FILTER_TCL_WRITE		0x3e
#define	TXP_CMD_FILTER_TBL_READ			0x3f
#define	TXP_CMD_VERSIONS_READ			0x43
#define	TXP_CMD_FILTER_DEFINE			0x45
#define	TXP_CMD_ADD_WAKEUP_PKT			0x46
#define	TXP_CMD_ADD_SLEEP_PKT			0x47
#define	TXP_CMD_ENABLE_SLEEP_EVENTS		0x48
#define	TXP_CMD_ENABLE_WAKEUP_EVENTS		0x49
#define	TXP_CMD_GET_IP_ADDRESS			0x4a
#define	TXP_CMD_READ_PCI_REG			0x4c
#define	TXP_CMD_WRITE_PCI_REG			0x4d
#define	TXP_CMD_OFFLOAD_READ			0x4e
#define	TXP_CMD_OFFLOAD_WRITE			0x4f
#define	TXP_CMD_HELLO_RESPONSE			0x57
#define	TXP_CMD_ENABLE_RX_FILTER		0x58
#define	TXP_CMD_RX_FILTER_CAPABILITY		0x59
#define	TXP_CMD_HALT				0x5d
#define	TXP_CMD_READ_IPSEC_INFO			0x54
#define	TXP_CMD_GET_IPSEC_ENABLE		0x67
#define	TXP_CMD_INVALID				0xffff

#define	TXP_FRAGMENT		0x0000
#define	TXP_TXFRAME		0x0001
#define	TXP_COMMAND		0x0002
#define	TXP_OPTION		0x0003
#define	TXP_RECEIVE		0x0004
#define	TXP_RESPONSE		0x0005

#define	TXP_TYPE_IPSEC		0x0000
#define	TXP_TYPE_TCPSEGMENT	0x0001

#define	TXP_PFLAG_NOCRC		0x0000
#define	TXP_PFLAG_IPCKSUM	0x0001
#define	TXP_PFLAG_TCPCKSUM	0x0002
#define	TXP_PFLAG_TCPSEGMENT	0x0004
#define	TXP_PFLAG_INSERTVLAN	0x0008
#define	TXP_PFLAG_IPSEC		0x0010
#define	TXP_PFLAG_PRIORITY	0x0020
#define	TXP_PFLAG_UDPCKSUM	0x0040
#define	TXP_PFLAG_PADFRAME	0x0080

#define	TXP_MISC_FIRSTDESC	0x0000
#define	TXP_MISC_LASTDESC	0x0001

#define	TXP_ERR_INTERNAL	0x0000
#define	TXP_ERR_FIFOUNDERRUN	0x0001
#define	TXP_ERR_BADSSD		0x0002
#define	TXP_ERR_RUNT		0x0003
#define	TXP_ERR_CRC		0x0004
#define	TXP_ERR_OVERSIZE	0x0005
#define	TXP_ERR_ALIGNMENT	0x0006
#define	TXP_ERR_DRIBBLEBIT	0x0007

#define	TXP_PROTO_UNKNOWN	0x0000
#define	TXP_PROTO_IP		0x0001
#define	TXP_PROTO_IPX		0x0002
#define	TXP_PROTO_RESERVED	0x0003

#define	TXP_STAT_PROTO		0x0001
#define	TXP_STAT_VLAN		0x0002
#define	TXP_STAT_IPFRAGMENT	0x0004
#define	TXP_STAT_IPSEC		0x0008
#define	TXP_STAT_IPCKSUMBAD	0x0010
#define	TXP_STAT_TCPCKSUMBAD	0x0020
#define	TXP_STAT_UDPCKSUMBAD	0x0040
#define	TXP_STAT_IPCKSUMGOOD	0x0080
#define	TXP_STAT_TCPCKSUMGOOD	0x0100
#define	TXP_STAT_UDPCKSUMGOOD	0x0200

struct txp_tx_desc {
	uint8_t			tx_flags;	/* type/descriptor flags */
	uint8_t			tx_numdesc;	/* number of descriptors */
	uint16_t		tx_totlen;	/* total packet length */
	uint32_t		tx_addrlo;	/* virt addr low word */
	uint32_t		tx_addrhi;	/* virt addr high word */
	uint32_t		tx_pflags;	/* processing flags */
};
#define	TX_FLAGS_TYPE_M		0x07		/* type mask */
#define	TX_FLAGS_TYPE_FRAG	0x00		/* type: fragment */
#define	TX_FLAGS_TYPE_DATA	0x01		/* type: data frame */
#define	TX_FLAGS_TYPE_CMD	0x02		/* type: command frame */
#define	TX_FLAGS_TYPE_OPT	0x03		/* type: options */
#define	TX_FLAGS_TYPE_RX	0x04		/* type: command */
#define	TX_FLAGS_TYPE_RESP	0x05		/* type: response */
#define	TX_FLAGS_RESP		0x40		/* response requested */
#define	TX_FLAGS_VALID		0x80		/* valid descriptor */

#define	TX_PFLAGS_DNAC		0x00000001	/* do not add crc */
#define	TX_PFLAGS_IPCKSUM	0x00000002	/* ip checksum */
#define	TX_PFLAGS_TCPCKSUM	0x00000004	/* tcp checksum */
#define	TX_PFLAGS_TCPSEG	0x00000008	/* tcp segmentation */
#define	TX_PFLAGS_VLAN		0x00000010	/* insert vlan */
#define	TX_PFLAGS_IPSEC		0x00000020	/* perform ipsec */
#define	TX_PFLAGS_PRIO		0x00000040	/* priority field valid */
#define	TX_PFLAGS_UDPCKSUM	0x00000080	/* udp checksum */
#define	TX_PFLAGS_PADFRAME	0x00000100	/* pad frame */
#define	TX_PFLAGS_VLANTAG_M	0x0ffff000	/* vlan tag mask */
#define	TX_PFLAGS_VLANPRI_M	0x00700000	/* vlan priority mask */
#define	TX_PFLAGS_VLANTAG_S	12		/* amount to shift tag */

struct txp_rx_desc {
	uint8_t			rx_flags;	/* type/descriptor flags */
	uint8_t			rx_numdesc;	/* number of descriptors */
	uint16_t		rx_len;		/* frame length */
	uint32_t		rx_vaddrlo;	/* virtual address, lo word */
	uint32_t		rx_vaddrhi;	/* virtual address, hi word */
	uint32_t		rx_stat;	/* status */
	uint16_t		rx_filter;	/* filter status */
	uint16_t		rx_hash;	/* hash status */
	uint32_t		rx_vlan;	/* vlan tag/priority */
};

/* txp_rx_desc.rx_flags */
#define	RX_FLAGS_TYPE_M		0x07		/* type mask */
#define	RX_FLAGS_TYPE_FRAG	0x00		/* type: fragment */
#define	RX_FLAGS_TYPE_DATA	0x01		/* type: data frame */
#define	RX_FLAGS_TYPE_CMD	0x02		/* type: command frame */
#define	RX_FLAGS_TYPE_OPT	0x03		/* type: options */
#define	RX_FLAGS_TYPE_RX	0x04		/* type: command */
#define	RX_FLAGS_TYPE_RESP	0x05		/* type: response */
#define	RX_FLAGS_RCV_TYPE_M	0x18		/* rcvtype mask */
#define	RX_FLAGS_RCV_TYPE_RX	0x00		/* rcvtype: receive */
#define	RX_FLAGS_RCV_TYPE_RSP	0x08		/* rcvtype: response */
#define	RX_FLAGS_ERROR		0x40		/* error in packet */

/* txp_rx_desc.rx_stat (if rx_flags & RX_FLAGS_ERROR bit set) */
#define	RX_ERROR_ADAPTER	0x00000000	/* adapter internal error */
#define	RX_ERROR_FIFO		0x00000001	/* fifo underrun */
#define	RX_ERROR_BADSSD		0x00000002	/* bad ssd */
#define	RX_ERROR_RUNT		0x00000003	/* runt packet */
#define	RX_ERROR_CRC		0x00000004	/* bad crc */
#define	RX_ERROR_OVERSIZE	0x00000005	/* oversized packet */
#define	RX_ERROR_ALIGN		0x00000006	/* alignment error */
#define	RX_ERROR_DRIBBLE	0x00000007	/* dribble bit */
#define	RX_ERROR_MASK		0x07

/* txp_rx_desc.rx_stat (if rx_flags & RX_FLAGS_ERROR not bit set) */
#define	RX_STAT_PROTO_M		0x00000003	/* protocol mask */
#define	RX_STAT_PROTO_UK	0x00000000	/* unknown protocol */
#define	RX_STAT_PROTO_IPX	0x00000001	/* IPX */
#define	RX_STAT_PROTO_IP	0x00000002	/* IP */
#define	RX_STAT_PROTO_RSV	0x00000003	/* reserved */
#define	RX_STAT_VLAN		0x00000004	/* vlan tag (in rxd) */
#define	RX_STAT_IPFRAG		0x00000008	/* fragment, ipsec not done */
#define	RX_STAT_IPSEC		0x00000010	/* ipsec decoded packet */
#define	RX_STAT_IPCKSUMBAD	0x00000020	/* ip checksum failed */
#define	RX_STAT_UDPCKSUMBAD	0x00000040	/* udp checksum failed */
#define	RX_STAT_TCPCKSUMBAD	0x00000080	/* tcp checksum failed */
#define	RX_STAT_IPCKSUMGOOD	0x00000100	/* ip checksum succeeded */
#define	RX_STAT_UDPCKSUMGOOD	0x00000200	/* udp checksum succeeded */
#define	RX_STAT_TCPCKSUMGOOD	0x00000400	/* tcp checksum succeeded */


struct txp_rxbuf_desc {
	uint32_t		rb_paddrlo;
	uint32_t		rb_paddrhi;
	uint32_t		rb_vaddrlo;
	uint32_t		rb_vaddrhi;
};

/* Extension descriptor */
struct txp_ext_desc {
	uint32_t		ext_1;
	uint32_t		ext_2;
	uint32_t		ext_3;
	uint32_t		ext_4;
};

struct txp_cmd_desc {
	uint8_t			cmd_flags;
	uint8_t			cmd_numdesc;
	uint16_t		cmd_id;
	uint16_t		cmd_seq;
	uint16_t		cmd_par1;
	uint32_t		cmd_par2;
	uint32_t		cmd_par3;
};
#define	CMD_FLAGS_TYPE_M	0x07		/* type mask */
#define	CMD_FLAGS_TYPE_FRAG	0x00		/* type: fragment */
#define	CMD_FLAGS_TYPE_DATA	0x01		/* type: data frame */
#define	CMD_FLAGS_TYPE_CMD	0x02		/* type: command frame */
#define	CMD_FLAGS_TYPE_OPT	0x03		/* type: options */
#define	CMD_FLAGS_TYPE_RX	0x04		/* type: command */
#define	CMD_FLAGS_TYPE_RESP	0x05		/* type: response */
#define	CMD_FLAGS_RESP		0x40		/* response requested */
#define	CMD_FLAGS_VALID		0x80		/* valid descriptor */

struct txp_rsp_desc {
	uint8_t			rsp_flags;
	uint8_t			rsp_numdesc;
	uint16_t		rsp_id;
	uint16_t		rsp_seq;
	uint16_t		rsp_par1;
	uint32_t		rsp_par2;
	uint32_t		rsp_par3;
};
#define	RSP_FLAGS_TYPE_M	0x07		/* type mask */
#define	RSP_FLAGS_TYPE_FRAG	0x00		/* type: fragment */
#define	RSP_FLAGS_TYPE_DATA	0x01		/* type: data frame */
#define	RSP_FLAGS_TYPE_CMD	0x02		/* type: command frame */
#define	RSP_FLAGS_TYPE_OPT	0x03		/* type: options */
#define	RSP_FLAGS_TYPE_RX	0x04		/* type: command */
#define	RSP_FLAGS_TYPE_RESP	0x05		/* type: response */
#define	RSP_FLAGS_ERROR		0x40		/* response error */

struct txp_frag_desc {
	uint8_t			frag_flags;	/* type/descriptor flags */
	uint8_t			frag_rsvd1;
	uint16_t		frag_len;	/* bytes in this fragment */
	uint32_t		frag_addrlo;	/* phys addr low word */
	uint32_t		frag_addrhi;	/* phys addr high word */
	uint32_t		frag_rsvd2;
};
#define	FRAG_FLAGS_TYPE_M	0x07		/* type mask */
#define	FRAG_FLAGS_TYPE_FRAG	0x00		/* type: fragment */
#define	FRAG_FLAGS_TYPE_DATA	0x01		/* type: data frame */
#define	FRAG_FLAGS_TYPE_CMD	0x02		/* type: command frame */
#define	FRAG_FLAGS_TYPE_OPT	0x03		/* type: options */
#define	FRAG_FLAGS_TYPE_RX	0x04		/* type: command */
#define	FRAG_FLAGS_TYPE_RESP	0x05		/* type: response */
#define	FRAG_FLAGS_VALID	0x80		/* valid descriptor */

struct txp_opt_desc {
	uint8_t			opt_desctype:3,
				opt_rsvd:1,
				opt_type:4;

	uint8_t			opt_num;
	uint16_t		opt_dep1;
	uint32_t		opt_dep2;
	uint32_t		opt_dep3;
	uint32_t		opt_dep4;
};

struct txp_ipsec_desc {
	uint8_t			ipsec_desctpe:3,
				ipsec_rsvd:1,
				ipsec_type:4;

	uint8_t			ipsec_num;
	uint16_t		ipsec_flags;
	uint16_t		ipsec_ah1;
	uint16_t		ipsec_esp1;
	uint16_t		ipsec_ah2;
	uint16_t		ipsec_esp2;
	uint32_t		ipsec_rsvd1;
};

struct txp_tcpseg_desc {
	uint8_t			tcpseg_type;
	uint8_t			tcpseg_num;
	uint16_t		tcpseg_mss;
	uint32_t		tcpseg_respaddr;
	uint32_t		tcpseg_txbytes;
	uint32_t		tcpseg_lss;
};
#define	TCPSEG_DESC_TYPE_M	0x07		/* type mask */
#define	TCPSEG_DESC_TYPE_FRAG	0x00		/* type: fragment */
#define	TCPSEG_DESC_TYPE_DATA	0x01		/* type: data frame */
#define	TCPSEG_DESC_TYPE_CMD	0x02		/* type: command frame */
#define	TCPSEG_DESC_TYPE_OPT	0x03		/* type: options */
#define	TCPSEG_DESC_TYPE_RX	0x04		/* type: command */
#define	TCPSEG_DESC_TYPE_RESP	0x05		/* type: response */
#define	TCPSEG_OPT_IPSEC	0x00
#define	TCPSEG_OPT_TSO		0x10
#define	TCPSEG_MSS_MASK		0x0FFF
#define	TCPSEG_MSS_FIRST	0x1000
#define	TCPSEG_MSS_LAST		0x2000

/*
 * Transceiver types
 */
#define	TXP_XCVR_10_HDX		0
#define	TXP_XCVR_10_FDX		1
#define	TXP_XCVR_100_HDX	2
#define	TXP_XCVR_100_FDX	3
#define	TXP_XCVR_AUTO		4

#define TXP_MEDIA_CRC		0x0004	/* crc strip disable */
#define	TXP_MEDIA_CD		0x0010	/* collision detection */
#define	TXP_MEDIA_CS		0x0020	/* carrier sense */
#define	TXP_MEDIA_POL		0x0400	/* polarity reversed */
#define	TXP_MEDIA_NOLINK	0x0800	/* 0 = link, 1 = no link */

/*
 * receive filter bits (par1 to TXP_CMD_RX_FILTER_{READ|WRITE}
 */
#define	TXP_RXFILT_DIRECT	0x0001	/* directed packets */
#define	TXP_RXFILT_ALLMULTI	0x0002	/* all multicast packets */
#define	TXP_RXFILT_BROADCAST	0x0004	/* broadcast packets */
#define	TXP_RXFILT_PROMISC	0x0008	/* promiscuous mode */
#define	TXP_RXFILT_HASHMULTI	0x0010	/* use multicast filter */

/*
 * boot record (pointers to rings)
 */
struct txp_boot_record {
	uint32_t		br_hostvar_lo;		/* host ring pointer */
	uint32_t		br_hostvar_hi;
	uint32_t		br_txlopri_lo;		/* tx low pri ring */
	uint32_t		br_txlopri_hi;
	uint32_t		br_txlopri_siz;
	uint32_t		br_txhipri_lo;		/* tx high pri ring */
	uint32_t		br_txhipri_hi;
	uint32_t		br_txhipri_siz;
	uint32_t		br_rxlopri_lo;		/* rx low pri ring */
	uint32_t		br_rxlopri_hi;
	uint32_t		br_rxlopri_siz;
	uint32_t		br_rxbuf_lo;		/* rx buffer ring */
	uint32_t		br_rxbuf_hi;
	uint32_t		br_rxbuf_siz;
	uint32_t		br_cmd_lo;		/* command ring */
	uint32_t		br_cmd_hi;
	uint32_t		br_cmd_siz;
	uint32_t		br_resp_lo;		/* response ring */
	uint32_t		br_resp_hi;
	uint32_t		br_resp_siz;
	uint32_t		br_zero_lo;		/* zero word */
	uint32_t		br_zero_hi;
	uint32_t		br_rxhipri_lo;		/* rx high pri ring */
	uint32_t		br_rxhipri_hi;
	uint32_t		br_rxhipri_siz;
};

/*
 * hostvar structure (shared with typhoon)
 */
struct txp_hostvar {
	uint32_t		hv_rx_hi_read_idx;	/* host->arm */
	uint32_t		hv_rx_lo_read_idx;	/* host->arm */
	uint32_t		hv_rx_buf_write_idx;	/* host->arm */
	uint32_t		hv_resp_read_idx;	/* host->arm */
	uint32_t		hv_tx_lo_desc_read_idx;	/* arm->host */
	uint32_t		hv_tx_hi_desc_read_idx;	/* arm->host */
	uint32_t		hv_rx_lo_write_idx;	/* arm->host */
	uint32_t		hv_rx_buf_read_idx;	/* arm->host */
	uint32_t		hv_cmd_read_idx;	/* arm->host */
	uint32_t		hv_resp_write_idx;	/* arm->host */
	uint32_t		hv_rx_hi_write_idx;	/* arm->host */
};

/*
 * TYPHOON status register state (in TXP_A2H_0)
 */
#define	STAT_ROM_CODE			0x00000001
#define	STAT_ROM_EEPROM_LOAD		0x00000002
#define	STAT_WAITING_FOR_BOOT		0x00000007
#define	STAT_RUNNING			0x00000009
#define	STAT_WAITING_FOR_HOST_REQUEST	0x0000000d
#define	STAT_WAITING_FOR_SEGMENT	0x00000010
#define	STAT_SLEEPING			0x00000011
#define	STAT_HALTED			0x00000014

#define	TX_ENTRIES			256
#define	RX_ENTRIES			128
#define	RXBUF_ENTRIES			256
#define	CMD_ENTRIES			32
#define	RSP_ENTRIES			32

#define	OFFLOAD_TCPCKSUM		0x00000002	/* tcp checksum */
#define	OFFLOAD_UDPCKSUM		0x00000004	/* udp checksum */
#define	OFFLOAD_IPCKSUM			0x00000008	/* ip checksum */
#define	OFFLOAD_IPSEC			0x00000010	/* ipsec enable */
#define	OFFLOAD_BCAST			0x00000020	/* broadcast throttle */
#define	OFFLOAD_DHCP			0x00000040	/* dhcp prevention */
#define	OFFLOAD_VLAN			0x00000080	/* vlan enable */
#define	OFFLOAD_FILTER			0x00000100	/* filter enable */
#define	OFFLOAD_TCPSEG			0x00000200	/* tcp segmentation */
#define	OFFLOAD_MASK			0xfffffffe	/* mask off low bit */

/*
 * Macros for converting array indices to offsets within the descriptor
 * arrays.  The chip operates on offsets, but it's much easier for us
 * to operate on indices.  Assumes descriptor entries are 16 bytes.
 */
#define	TXP_IDX2OFFSET(idx)	((idx) << 4)
#define	TXP_OFFSET2IDX(off)	((off) >> 4)

struct txp_cmd_ring {
	struct txp_cmd_desc	*base;
	uint32_t		lastwrite;
	uint32_t		size;
};

struct txp_rsp_ring {
	struct txp_rsp_desc	*base;
	uint32_t		lastwrite;
	uint32_t		size;
};

struct txp_tx_ring {
	struct txp_tx_desc	*r_desc;	/* base address of descs */
	bus_dma_tag_t		r_tag;
	bus_dmamap_t		r_map;
	uint32_t		r_reg;		/* register to activate */
	uint32_t		r_prod;		/* producer */
	uint32_t		r_cons;		/* consumer */
	uint32_t		r_cnt;		/* # descs in use */
	uint32_t		*r_off;		/* hostvar index pointer */
};

struct txp_swdesc {
	struct mbuf 		*sd_mbuf;
	bus_dmamap_t		sd_map;
};

struct txp_rx_swdesc {
	TAILQ_ENTRY(txp_rx_swdesc)	sd_next;
	struct mbuf 		*sd_mbuf;
	bus_dmamap_t		sd_map;
};

struct txp_rx_ring {
	struct txp_rx_desc	*r_desc;	/* base address of descs */
	bus_dma_tag_t		r_tag;
	bus_dmamap_t		r_map;
	uint32_t		*r_roff;	/* hv read offset ptr */
	uint32_t		*r_woff;	/* hv write offset ptr */
};

struct txp_ldata {
	struct txp_boot_record	*txp_boot;
	bus_addr_t		txp_boot_paddr;
	struct txp_hostvar	*txp_hostvar;
	bus_addr_t		txp_hostvar_paddr;
	struct txp_tx_desc	*txp_txhiring;
	bus_addr_t		txp_txhiring_paddr;
	struct txp_tx_desc	*txp_txloring;
	bus_addr_t		txp_txloring_paddr;
	struct txp_rxbuf_desc	*txp_rxbufs;
	bus_addr_t		txp_rxbufs_paddr;
	struct txp_rx_desc	*txp_rxhiring;
	bus_addr_t		txp_rxhiring_paddr;
	struct txp_rx_desc	*txp_rxloring;
	bus_addr_t		txp_rxloring_paddr;
	struct txp_cmd_desc	*txp_cmdring;
	bus_addr_t		txp_cmdring_paddr;
	struct txp_rsp_desc	*txp_rspring;
	bus_addr_t		txp_rspring_paddr;
	uint32_t		*txp_zero;
	bus_addr_t		txp_zero_paddr;
};

struct txp_chain_data {
	bus_dma_tag_t		txp_parent_tag;
	bus_dma_tag_t		txp_boot_tag;
	bus_dmamap_t		txp_boot_map;
	bus_dma_tag_t		txp_hostvar_tag;
	bus_dmamap_t		txp_hostvar_map;
	bus_dma_tag_t		txp_txhiring_tag;
	bus_dmamap_t		txp_txhiring_map;
	bus_dma_tag_t		txp_txloring_tag;
	bus_dmamap_t		txp_txloring_map;
	bus_dma_tag_t		txp_tx_tag;
	bus_dma_tag_t		txp_rx_tag;
	bus_dma_tag_t		txp_rxbufs_tag;
	bus_dmamap_t		txp_rxbufs_map;
	bus_dma_tag_t		txp_rxhiring_tag;
	bus_dmamap_t		txp_rxhiring_map;
	bus_dma_tag_t		txp_rxloring_tag;
	bus_dmamap_t		txp_rxloring_map;
	bus_dma_tag_t		txp_cmdring_tag;
	bus_dmamap_t		txp_cmdring_map;
	bus_dma_tag_t		txp_rspring_tag;
	bus_dmamap_t		txp_rspring_map;
	bus_dma_tag_t		txp_zero_tag;
	bus_dmamap_t		txp_zero_map;
};

struct txp_hw_stats {
	uint32_t		tx_frames;
	uint64_t		tx_bytes;
	uint32_t		tx_deferred;
	uint32_t		tx_late_colls;
	uint32_t		tx_colls;
	uint32_t		tx_carrier_lost;
	uint32_t		tx_multi_colls;
	uint32_t		tx_excess_colls;
	uint32_t		tx_fifo_underruns;
	uint32_t		tx_mcast_oflows;
	uint32_t		tx_filtered;
	uint32_t		rx_frames;
	uint64_t		rx_bytes;
	uint32_t		rx_fifo_oflows;
	uint32_t		rx_badssd;
	uint32_t		rx_crcerrs;
	uint32_t		rx_lenerrs;
	uint32_t		rx_bcast_frames;
	uint32_t		rx_mcast_frames;
	uint32_t		rx_oflows;
	uint32_t		rx_filtered;
};

struct txp_softc {
	struct ifnet		*sc_ifp;
	device_t		sc_dev;
	struct txp_hostvar	*sc_hostvar;
	struct txp_boot_record	*sc_boot;
	struct resource		*sc_res;
	int			sc_res_id;
	int			sc_res_type;
	struct resource		*sc_irq;
	void			*sc_intrhand;
	struct txp_chain_data	sc_cdata;
	struct txp_ldata	sc_ldata;
	int			sc_rxbufprod;
	int			sc_process_limit;
	struct txp_cmd_ring	sc_cmdring;
	struct txp_rsp_ring	sc_rspring;
	struct callout		sc_tick;
	struct ifmedia		sc_ifmedia;
	struct txp_hw_stats	sc_ostats;
	struct txp_hw_stats	sc_stats;
	struct txp_tx_ring	sc_txhir, sc_txlor;
	struct txp_swdesc	sc_txd[TX_ENTRIES];
	struct txp_rxbuf_desc	*sc_rxbufs;
	struct txp_rx_ring	sc_rxhir, sc_rxlor;
	uint16_t		sc_xcvr;
	uint16_t		sc_seq;
	int			sc_watchdog_timer;
	int			sc_if_flags;
	int			sc_flags;
#define	TXP_FLAG_DETACH		0x4000
#define	TXP_FLAG_LINK		0x8000
	TAILQ_HEAD(, txp_rx_swdesc)	sc_free_list;
	TAILQ_HEAD(, txp_rx_swdesc)	sc_busy_list;
	struct task		sc_int_task;
	struct taskqueue	*sc_tq;
	struct mtx		sc_mtx;
};

struct txp_fw_file_header {
	uint8_t		magicid[8];	/* TYPHOON\0 */
	uint32_t	version;
	uint32_t	nsections;
	uint32_t	addr;
	uint32_t	hmac[5];
};

struct txp_fw_section_header {
	uint32_t	nbytes;
	uint16_t	cksum;
	uint16_t	reserved;
	uint32_t	addr;
};

#define	TXP_MAX_SEGLEN	0xffff
#define	TXP_MAX_PKTLEN	(ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN)

#define	WRITE_REG(sc, reg, val)		bus_write_4((sc)->sc_res, reg, val)
#define	READ_REG(sc, reg)		bus_read_4((sc)->sc_res, reg)
#define	TXP_BARRIER(sc, o, l, f)	bus_barrier((sc)->sc_res, (o), (l), (f))

#define	TXP_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	TXP_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	TXP_LOCK_ASSERT(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)

#define	TXP_MAXTXSEGS		16
#define	TXP_RXBUF_ALIGN		(sizeof(uint32_t))

#define	TXP_PROC_MIN		16
#define	TXP_PROC_MAX		RX_ENTRIES
#define	TXP_PROC_DEFAULT	(RX_ENTRIES / 2)

#define	TXP_ADDR_HI(x)		((uint64_t)(x) >> 32)
#define	TXP_ADDR_LO(x)		((uint64_t)(x) & 0xffffffff)

/*
 * 3Com PCI vendor ID.
 */
#define	TXP_VENDORID_3COM		0x10B7

/*
 * 3cR990 device IDs
 */
#define TXP_DEVICEID_3CR990_TX_95	0x9902
#define TXP_DEVICEID_3CR990_TX_97	0x9903
#define TXP_DEVICEID_3CR990B_TXM	0x9904
#define TXP_DEVICEID_3CR990_SRV_95	0x9908
#define TXP_DEVICEID_3CR990_SRV_97	0x9909
#define TXP_DEVICEID_3CR990B_SRV	0x990A

struct txp_type {
	uint16_t		txp_vid;
	uint16_t		txp_did;
	char			*txp_name;
};

#define	TXP_TIMEOUT	10000
#define	TXP_CMD_NOWAIT	0
#define	TXP_CMD_WAIT	1
#define	TXP_TX_TIMEOUT	5

/*
 * Each frame requires one frame descriptor and one or more
 * fragment descriptors. If TSO is used frame descriptor block
 * requires one or two option frame descriptors depending on
 * number of framents. Therefore we will consume three
 * additional descriptors at most to use TSO for a frame and
 * one reserved descriptor in order not to full Tx descriptor
 * ring.
 */
#define	TXP_TXD_RESERVED	4

#define	TXP_DESC_INC(x, y)	((x) = ((x) + 1) % (y))
