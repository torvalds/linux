/*	$OpenBSD: if_txpreg.h,v 1.39 2017/07/12 14:36:57 mikeb Exp $ */

/*
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

#define	TXP_PCI_LOMEM			0x14	/* pci conf, memory map BAR */
#define	TXP_PCI_LOIO			0x10	/* pci conf, IO map BAR */

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
 * soft reset register (SRR)
 */
#define	TXP_SRR_ALL		0x0000007f	/* full reset */

/*
 * Typhoon boot commands.
 */
#define	TXP_BOOTCMD_NULL			0x00
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

#define	TXP_PFLAG_NOCRC		0x0001
#define	TXP_PFLAG_IPCKSUM	0x0002
#define	TXP_PFLAG_TCPCKSUM	0x0004
#define	TXP_PFLAG_TCPSEGMENT	0x0008
#define	TXP_PFLAG_INSERTVLAN	0x0010
#define	TXP_PFLAG_IPSEC		0x0020
#define	TXP_PFLAG_PRIORITY	0x0040
#define	TXP_PFLAG_UDPCKSUM	0x0080
#define	TXP_PFLAG_PADFRAME	0x0100

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
	volatile u_int8_t	tx_flags;	/* type/descriptor flags */
	volatile u_int8_t	tx_numdesc;	/* number of descriptors */
	volatile u_int16_t	tx_totlen;	/* total packet length */
	volatile u_int32_t	tx_addrlo;	/* virt addr low word */
	volatile u_int32_t	tx_addrhi;	/* virt addr high word */
	volatile u_int32_t	tx_pflags;	/* processing flags */
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
	volatile u_int8_t	rx_flags;	/* type/descriptor flags */
	volatile u_int8_t	rx_numdesc;	/* number of descriptors */
	volatile u_int16_t	rx_len;		/* frame length */
	volatile u_int32_t	rx_vaddrlo;	/* virtual address, lo word */
	volatile u_int32_t	rx_vaddrhi;	/* virtual address, hi word */
	volatile u_int32_t	rx_stat;	/* status */
	volatile u_int16_t	rx_filter;	/* filter status */
	volatile u_int16_t	rx_hash;	/* hash status */
	volatile u_int32_t	rx_vlan;	/* vlan tag/priority */
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
	volatile u_int32_t	rb_paddrlo;
	volatile u_int32_t	rb_paddrhi;
	volatile u_int32_t	rb_vaddrlo;
	volatile u_int32_t	rb_vaddrhi;
};

/* Extension descriptor */
struct txp_ext_desc {
	volatile u_int32_t	ext_1;
	volatile u_int32_t	ext_2;
	volatile u_int32_t	ext_3;
	volatile u_int32_t	ext_4;
};

struct txp_cmd_desc {
	volatile u_int8_t	cmd_flags;
	volatile u_int8_t	cmd_numdesc;
	volatile u_int16_t	cmd_id;
	volatile u_int16_t	cmd_seq;
	volatile u_int16_t	cmd_par1;
	volatile u_int32_t	cmd_par2;
	volatile u_int32_t	cmd_par3;
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
	volatile u_int8_t	rsp_flags;
	volatile u_int8_t	rsp_numdesc;
	volatile u_int16_t	rsp_id;
	volatile u_int16_t	rsp_seq;
	volatile u_int16_t	rsp_par1;
	volatile u_int32_t	rsp_par2;
	volatile u_int32_t	rsp_par3;
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
	volatile u_int8_t	frag_flags;	/* type/descriptor flags */
	volatile u_int8_t	frag_rsvd1;
	volatile u_int16_t	frag_len;	/* bytes in this fragment */
	volatile u_int32_t	frag_addrlo;	/* phys addr low word */
	volatile u_int32_t	frag_addrhi;	/* phys addr high word */
	volatile u_int32_t	frag_rsvd2;
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
	u_int8_t		opt_desctype:3,
				opt_rsvd:1,
				opt_type:4;

	u_int8_t		opt_num;
	u_int16_t		opt_dep1;
	u_int32_t		opt_dep2;
	u_int32_t		opt_dep3;
	u_int32_t		opt_dep4;
};

struct txp_ipsec_desc {
	u_int8_t		ipsec_desctpe:3,
				ipsec_rsvd:1,
				ipsec_type:4;

	u_int8_t		ipsec_num;
	u_int16_t		ipsec_flags;
	u_int16_t		ipsec_ah1;
	u_int16_t		ipsec_esp1;
	u_int16_t		ipsec_ah2;
	u_int16_t		ipsec_esp2;
	u_int32_t		ipsec_rsvd1;
};

struct txp_tcpseg_desc {
	u_int8_t		tcpseg_desctype:3,
				tcpseg_rsvd:1,
				tcpseg_type:4;

	u_int8_t		tcpseg_num;

	u_int16_t		tcpseg_mss:12,
				tcpseg_misc:4;

	u_int32_t		tcpseg_respaddr;
	u_int32_t		tcpseg_txbytes;
	u_int32_t		tcpseg_lss;
};

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
	volatile u_int32_t	br_hostvar_lo;		/* host ring pointer */
	volatile u_int32_t	br_hostvar_hi;
	volatile u_int32_t	br_txlopri_lo;		/* tx low pri ring */
	volatile u_int32_t	br_txlopri_hi;
	volatile u_int32_t	br_txlopri_siz;
	volatile u_int32_t	br_txhipri_lo;		/* tx high pri ring */
	volatile u_int32_t	br_txhipri_hi;
	volatile u_int32_t	br_txhipri_siz;
	volatile u_int32_t	br_rxlopri_lo;		/* rx low pri ring */
	volatile u_int32_t	br_rxlopri_hi;
	volatile u_int32_t	br_rxlopri_siz;
	volatile u_int32_t	br_rxbuf_lo;		/* rx buffer ring */
	volatile u_int32_t	br_rxbuf_hi;
	volatile u_int32_t	br_rxbuf_siz;
	volatile u_int32_t	br_cmd_lo;		/* command ring */
	volatile u_int32_t	br_cmd_hi;
	volatile u_int32_t	br_cmd_siz;
	volatile u_int32_t	br_resp_lo;		/* response ring */
	volatile u_int32_t	br_resp_hi;
	volatile u_int32_t	br_resp_siz;
	volatile u_int32_t	br_zero_lo;		/* zero word */
	volatile u_int32_t	br_zero_hi;
	volatile u_int32_t	br_rxhipri_lo;		/* rx high pri ring */
	volatile u_int32_t	br_rxhipri_hi;
	volatile u_int32_t	br_rxhipri_siz;
};

/*
 * hostvar structure (shared with typhoon)
 */
struct txp_hostvar {
	volatile u_int32_t	hv_rx_hi_read_idx;	/* host->arm */
	volatile u_int32_t	hv_rx_lo_read_idx;	/* host->arm */
	volatile u_int32_t	hv_rx_buf_write_idx;	/* host->arm */
	volatile u_int32_t	hv_resp_read_idx;	/* host->arm */
	volatile u_int32_t	hv_tx_lo_desc_read_idx;	/* arm->host */
	volatile u_int32_t	hv_tx_hi_desc_read_idx;	/* arm->host */
	volatile u_int32_t	hv_rx_lo_write_idx;	/* arm->host */
	volatile u_int32_t	hv_rx_buf_read_idx;	/* arm->host */
	volatile u_int32_t	hv_cmd_read_idx;	/* arm->host */
	volatile u_int32_t	hv_resp_write_idx;	/* arm->host */
	volatile u_int32_t	hv_rx_hi_write_idx;	/* arm->host */
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

struct txp_dma_alloc {
	u_int64_t		dma_paddr;
	caddr_t			dma_vaddr;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	int			dma_nseg;
};

struct txp_cmd_ring {
	struct txp_cmd_desc	*base;
	u_int32_t		lastwrite;
	u_int32_t		size;
};

struct txp_rsp_ring {
	struct txp_rsp_desc	*base;
	u_int32_t		lastwrite;
	u_int32_t		size;
};

struct txp_tx_ring {
	struct txp_tx_desc	*r_desc;	/* base address of descs */
	u_int32_t		r_reg;		/* register to activate */
	u_int32_t		r_prod;		/* producer */
	u_int32_t		r_cons;		/* consumer */
	u_int32_t		r_cnt;		/* # descs in use */
	volatile u_int32_t	*r_off;		/* hostvar index pointer */
};

struct txp_swdesc {
	struct mbuf *		sd_mbuf;
	bus_dmamap_t		sd_map;
};

struct txp_rx_ring {
	struct txp_rx_desc	*r_desc;	/* base address of descs */
	volatile u_int32_t	*r_roff;	/* hv read offset ptr */
	volatile u_int32_t	*r_woff;	/* hv write offset ptr */
};

struct txp_softc {
	struct device		sc_dev;		/* base device */
	struct arpcom		sc_arpcom;	/* ethernet common */
	struct txp_hostvar	*sc_hostvar;
	struct txp_boot_record	*sc_boot;
	bus_space_handle_t	sc_bh;		/* bus handle (regs) */
	bus_space_tag_t		sc_bt;		/* bus tag (regs) */
	bus_dma_tag_t		sc_dmat;	/* dma tag */
	struct txp_cmd_ring	sc_cmdring;
	struct txp_rsp_ring	sc_rspring;
	struct txp_swdesc	sc_txd[TX_ENTRIES];
	void *			sc_ih;
	struct timeout		sc_tick;
	struct ifmedia		sc_ifmedia;
	struct txp_tx_ring	sc_txhir, sc_txlor;
	struct txp_rxbuf_desc	*sc_rxbufs;
	struct txp_rx_ring	sc_rxhir, sc_rxlor;
	u_int16_t		sc_xcvr;
	u_int16_t		sc_seq;
	struct txp_dma_alloc	sc_boot_dma, sc_host_dma, sc_zero_dma;
	struct txp_dma_alloc	sc_rxhiring_dma, sc_rxloring_dma;
	struct txp_dma_alloc	sc_txhiring_dma, sc_txloring_dma;
	struct txp_dma_alloc	sc_cmdring_dma, sc_rspring_dma;
	struct txp_dma_alloc	sc_rxbufring_dma;
	int			sc_cold;
	u_int32_t		sc_rx_capability, sc_tx_capability;
};

#define	TXP_DEVNAME(sc)		((sc)->sc_cold ? "" : (sc)->sc_dev.dv_xname)

struct txp_fw_file_header {
	u_int8_t	magicid[8];	/* TYPHOON\0 */
	u_int32_t	version;
	u_int32_t	nsections;
	u_int32_t	addr;
	u_int32_t	hmac[5];
};

struct txp_fw_section_header {
	u_int32_t	nbytes;
	u_int16_t	cksum;
	u_int16_t	reserved;
	u_int32_t	addr;
};

#define	TXP_MAX_SEGLEN	0xffff
#define	TXP_MAX_PKTLEN	0x0800

#define	TXP_MAXTXSEGS	16

#define	WRITE_REG(sc,reg,val) \
    bus_space_write_4((sc)->sc_bt, (sc)->sc_bh, reg, val)
#define	READ_REG(sc,reg) \
    bus_space_read_4((sc)->sc_bt, (sc)->sc_bh, reg)

