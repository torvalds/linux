/*	$OpenBSD: if_xgereg.h,v 1.5 2016/04/28 00:11:56 dlg Exp $	*/
/*	$NetBSD: if_xgereg.h,v 1.1 2005/09/09 10:30:27 ragge Exp $	*/

/*
 * Copyright (c) 2004, SUNET, Swedish University Computer Network.
 * All rights reserved.
 *
 * Written by Anders Magnusson for SUNET, Swedish University Computer Network.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      SUNET, Swedish University Computer Network.
 * 4. The name of SUNET may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SUNET ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL SUNET
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Defines for the Neterion Xframe adapter.
 */

/* PCI address space */
#define	XGE_PIF_BAR	0x10
#define	XGE_TXP_BAR	0x18

/* PIF register address calculation */
#define	DCSRB(x) (0x0000+(x))	/* 10GbE Device Control and Status Registers */
#define	PCIXB(x) (0x0800+(x))	/* PCI-X Interface Functional Registers */
#define	TDMAB(x) (0x1000+(x))	/* Transmit DMA Functional Registers */
#define	RDMAB(x) (0x1800+(x))	/* Receive DMA Functional Registers */
#define	MACRB(x) (0x2000+(x))	/* MAC functional registers */
#define	RLDRB(x) (0x2800+(x))	/* RLDRAM memory controller */
#define	XGXSB(x) (0x3000+(x))	/* XGXS functional Registers */

/*
 * Control and Status Registers
 */
#define	GENERAL_INT_STATUS	DCSRB(0x0000)
#define	GENERAL_INT_MASK	DCSRB(0x0008)
#define	SW_RESET		DCSRB(0x0100)
#define	 XGXS_RESET(x)		((uint64_t)(x) << 32)
#define	ADAPTER_STATUS		DCSRB(0x0108)
#define	 TDMA_READY		(1ULL<<63)
#define	 RDMA_READY		(1ULL<<62)
#define	 PFC_READY		(1ULL<<61)
#define	 TMAC_BUF_EMPTY		(1ULL<<60)
#define	 PIC_QUIESCENT		(1ULL<<58)
#define	 RMAC_REMOTE_FAULT	(1ULL<<57)
#define	 RMAC_LOCAL_FAULT	(1ULL<<56)
#define	 MC_DRAM_READY		(1ULL<<39)
#define	 MC_QUEUES_READY	(1ULL<<38)
#define	 RIC_RUNNING		(1ULL<<37)
#define	 M_PLL_LOCK		(1ULL<<33)
#define	 P_PLL_LOCK		(1ULL<<32)
#define	ADAPTER_CONTROL		DCSRB(0x0110)
#define	 ADAPTER_EN		(1ULL<<56)
#define	 EOI_TX_ON		(1ULL<<48)
#define	 LED_ON			(1ULL<<40)
#define	 WAIT_INT_EN		(1ULL<<15)
#define	 ECC_ENABLE_N		(1ULL<<8)

/* for debug of ADAPTER_STATUS */
#define	QUIESCENT (TDMA_READY|RDMA_READY|PFC_READY|TMAC_BUF_EMPTY|\
	PIC_QUIESCENT|MC_DRAM_READY|MC_QUEUES_READY|M_PLL_LOCK|P_PLL_LOCK)
#define	QUIESCENT_BMSK	\
	"\177\20b\x3fTDMA_READY\0b\x3eRDMA_READY\0b\x3dPFC_READY\0" \
	"b\x3cTMAC_BUF_EMPTY\0b\x3aPIC_QUIESCENT\0\x39RMAC_REMOTE_FAULT\0" \
	"b\x38RMAC_LOCAL_FAULT\0b\x27MC_DRAM_READY\0b\x26MC_QUEUES_READY\0" \
	"b\x21M_PLL_LOCK\0b\x20P_PLL_LOCK"

/*
 * PCI-X registers
 */
/* Interrupt control registers */
#define	PIC_INT_STATUS		PCIXB(0)
#define	PIC_INT_MASK		PCIXB(0x008)
#define	TXPIC_INT_MASK		PCIXB(0x018)
#define	RXPIC_INT_MASK		PCIXB(0x030)
#define	FLASH_INT_MASK		PCIXB(0x048)
#define	MDIO_INT_MASK		PCIXB(0x060)
#define	IIC_INT_MASK		PCIXB(0x078)
#define	GPIO_INT_MASK		PCIXB(0x098)
#define	TX_TRAFFIC_INT		PCIXB(0x0e0)
#define	TX_TRAFFIC_MASK		PCIXB(0x0e8)
#define	RX_TRAFFIC_INT		PCIXB(0x0f0)
#define	RX_TRAFFIC_MASK		PCIXB(0x0f8)
#define	PIC_CONTROL		PCIXB(0x100)

/* Byte swapping for little-endian */
#define	SWAPPER_CTRL		PCIXB(0x108)
#define	 PIF_R_FE		(1ULL<<63)
#define	 PIF_R_SE		(1ULL<<62)
#define	 PIF_W_FE		(1ULL<<55)
#define	 PIF_W_SE		(1ULL<<54)
#define	 TxP_FE			(1ULL<<47)
#define	 TxP_SE			(1ULL<<46)
#define	 TxD_R_FE		(1ULL<<45)
#define	 TxD_R_SE		(1ULL<<44)
#define	 TxD_W_FE		(1ULL<<43)
#define	 TxD_W_SE		(1ULL<<42)
#define	 TxF_R_FE		(1ULL<<41)
#define	 TxF_R_SE		(1ULL<<40)
#define	 RxD_R_FE		(1ULL<<31)
#define	 RxD_R_SE		(1ULL<<30)
#define	 RxD_W_FE		(1ULL<<29)
#define	 RxD_W_SE		(1ULL<<28)
#define	 RxF_W_FE		(1ULL<<27)
#define	 RxF_W_SE		(1ULL<<26)
#define	 XMSI_FE		(1ULL<<23)
#define	 XMSI_SE		(1ULL<<22)
#define	 STATS_FE		(1ULL<<15)
#define	 STATS_SE		(1ULL<<14)

/* Diagnostic register to check byte-swapping conf */
#define	PIF_RD_SWAPPER_Fb	PCIXB(0x110)
#define	 SWAPPER_MAGIC		0x0123456789abcdefULL

#define XMSI_ADDRESS		PCIXB(0x160)

/* Stats registers */
#define	STAT_CFG		PCIXB(0x1d0)
#define	STAT_ADDR		PCIXB(0x1d8)

/* DTE-XGXS Interface */
#define	MDIO_CONTROL		PCIXB(0x1e0)
#define	DTX_CONTROL		PCIXB(0x1e8)
#define	I2C_CONTROL		PCIXB(0x1f0)
#define	GPIO_CONTROL		PCIXB(0x1f8)

/*
 * Transmit DMA registers.
 */
#define	TXDMA_INT_MASK		TDMAB(0x008)
#define	PFC_ERR_MASK		TDMAB(0x018)
#define	TDA_ERR_MASK		TDMAB(0x030)
#define	PCC_ERR_MASK		TDMAB(0x048)
#define	TTI_ERR_MASK		TDMAB(0x060)
#define	LSO_ERR_MASK		TDMAB(0x078)
#define	TPA_ERR_MASK		TDMAB(0x090)
#define	SM_ERR_MASK		TDMAB(0x0a8)

/* Transmit FIFO config */
#define	TX_FIFO_P0		TDMAB(0x0108)
#define	TX_FIFO_P1		TDMAB(0x0110)
#define	TX_FIFO_P2		TDMAB(0x0118)
#define	TX_FIFO_P3		TDMAB(0x0120)
#define	 TX_FIFO_ENABLE		(1ULL<<63)
#define	 TX_FIFO_NUM0(x)	((uint64_t)(x) << 56)
#define	 TX_FIFO_LEN0(x)	((uint64_t)((x)-1) << 32)	
#define	 TX_FIFO_NUM1(x)	((uint64_t)(x) << 24)
#define	 TX_FIFO_LEN1(x)	((uint64_t)((x)-1) << 0)	

/* Transmit interrupts */
#define	TTI_COMMAND_MEM		TDMAB(0x150)
#define	 TTI_CMD_MEM_WE		(1ULL<<56)
#define	 TTI_CMD_MEM_STROBE	(1ULL<<48)
#define	TTI_DATA1_MEM		TDMAB(0x158)
#define	 TX_TIMER_VAL(x)	((uint64_t)(x) << 32)
#define	 TX_TIMER_AC		(1ULL<<25)
#define	 TX_TIMER_CI		(1ULL<<24)
#define	 TX_URNG_A(x)		((uint64_t)(x) << 16)
#define	 TX_URNG_B(x)		((uint64_t)(x) << 8)
#define	 TX_URNG_C(x)		((uint64_t)(x) << 0)
#define	TTI_DATA2_MEM		TDMAB(0x160)
#define	 TX_UFC_A(x)		((uint64_t)(x) << 48)
#define	 TX_UFC_B(x)		((uint64_t)(x) << 32)
#define	 TX_UFC_C(x)		((uint64_t)(x) << 16)
#define	 TX_UFC_D(x)		((uint64_t)(x) << 0)


/* Transmit protocol assist */
#define	TX_PA_CFG		TDMAB(0x0168)
#define	 TX_PA_CFG_IFR		(1ULL<<62)	/* Ignore frame error */
#define	 TX_PA_CFG_ISO		(1ULL<<61)	/* Ignore snap OUI */
#define	 TX_PA_CFG_ILC		(1ULL<<60)	/* Ignore LLC ctrl */
#define	 TX_PA_CFG_ILE		(1ULL<<57)	/* Ignore L2 error */

/*
 * Transmit descriptor list (TxDL) pointer and control.
 * There may be up to 8192 TxDL's per FIFO, but with a NIC total
 * of 8192. The TxDL's are located in the NIC memory.
 * Each TxDL can have up to 256 Transmit descriptors (TxD)
 * that are located in host memory.
 *
 * The txdl struct fields must be written in order.
 */
#ifdef notdef  /* Use bus_space stuff instead */
struct txdl {
	uint64_t txdl_pointer;	/* address of TxD's */
	uint64_t txdl_control;
};
#endif
#define	TXDLOFF1(x)	(16*(x))	/* byte offset in txdl for list */
#define	TXDLOFF2(x)	(16*(x)+8)	/* byte offset in txdl for list */
#define	TXDL_NUMTXD(x)	((uint64_t)(x) << 56)	/* # of TxD's in the list */
#define	TXDL_LGC_FIRST	(1ULL << 49)	/* First special list */
#define	TXDL_LGC_LAST	(1ULL << 48)	/* Last special list */
#define	TXDL_SFF	(1ULL << 40)	/* List is a special function list */
#define	TXDL_PAR	0		/* Pointer address register */
#define	TXDL_LCR	8		/* List control register */

struct txd {
	uint64_t txd_control1;
	uint64_t txd_control2;
	uint64_t txd_bufaddr;
	uint64_t txd_hostctrl;
};
#define	TXD_CTL1_OWN	(1ULL << 56)	/* Owner, 0 == host, 1 == NIC */
#define	TXD_CTL1_GCF	(1ULL << 41)	/* First frame or LSO */
#define	TXD_CTL1_GCL	(1ULL << 40)	/* Last frame or LSO */
#define	TXD_CTL1_LSO	(1ULL << 33)	/* LSO should be performed */
#define	TXD_CTL1_COF	(1ULL << 32)	/* UDP Checksum over fragments */
#define	TXD_CTL1_MSS(x)	((uint64_t)(x) << 16)

#define	TXD_CTL2_INTLST	(1ULL << 16)	/* Per-list interrupt */
#define	TXD_CTL2_UTIL	(1ULL << 17)	/* Utilization interrupt */
#define	TXD_CTL2_CIPv4	(1ULL << 58)	/* Calculate IPv4 header checksum */
#define	TXD_CTL2_CTCP	(1ULL << 57)	/* Calculate TCP checksum */
#define	TXD_CTL2_CUDP	(1ULL << 56)	/* Calculate UDP checksum */
#define	TXD_CTL2_VLANE	(1ULL << 48)	/* Enable VLAN tag insertion */
#define	TXD_CTL2_VLANT(x) ((uint64_t)(x) << 32)

/*
 * Receive DMA registers
 */
/* Receive interrupt registers */
#define	RXDMA_INT_MASK		RDMAB(0x008)
#define	RDA_ERR_MASK		RDMAB(0x018)
#define	RC_ERR_MASK		RDMAB(0x030)
#define	PRC_PCIX_ERR_MASK	RDMAB(0x048)
#define	RPA_ERR_MASK		RDMAB(0x060)
#define	RTI_ERR_MASK		RDMAB(0x078)

#define	RX_QUEUE_PRIORITY	RDMAB(0x100)
#define	RX_W_ROUND_ROBIN_0	RDMAB(0x108)
#define	RX_W_ROUND_ROBIN_1	RDMAB(0x110)
#define	RX_W_ROUND_ROBIN_2	RDMAB(0x118)
#define	RX_W_ROUND_ROBIN_3	RDMAB(0x120)
#define	RX_W_ROUND_ROBIN_4	RDMAB(0x128)
#define	PRC_RXD0_0		RDMAB(0x130)
#define	PRC_CTRL_0		RDMAB(0x170)
#define	 RC_IN_SVC		(1ULL << 56)
#define	 RING_MODE_1		(0ULL << 48)
#define	 RING_MODE_3		(1ULL << 48)
#define	 RING_MODE_5		(2ULL << 48)
#define	 RC_NO_SNOOP_D		(1ULL << 41)
#define	 RC_NO_SNOOP_B		(1ULL << 40)
#define	PRC_ALARM_ACTION	RDMAB(0x1b0)
#define	RTI_COMMAND_MEM		RDMAB(0x1b8)
#define	 RTI_CMD_MEM_WE		(1ULL << 56)
#define	 RTI_CMD_MEM_STROBE	(1ULL << 48)
#define	RTI_DATA1_MEM		RDMAB(0x1c0)
#define	 RX_TIMER_VAL(x)	((uint64_t)(x) << 32)
#define	 RX_TIMER_AC		(1ULL << 25)
#define	 RX_URNG_A(x)		((uint64_t)(x) << 16)
#define	 RX_URNG_B(x)		((uint64_t)(x) << 8)
#define	 RX_URNG_C(x)		((uint64_t)(x) << 0)
#define	RTI_DATA2_MEM		RDMAB(0x1c8)
#define	 RX_UFC_A(x)		((uint64_t)(x) << 48)
#define	 RX_UFC_B(x)		((uint64_t)(x) << 32)
#define	 RX_UFC_C(x)		((uint64_t)(x) << 16)
#define	 RX_UFC_D(x)		((uint64_t)(x) << 0)
#define	RX_PA_CFG		RDMAB(0x1d0)
#define	 IGNORE_FRAME_ERROR	(1ULL << 62)
#define	 IGNORE_SNAP_OUI	(1ULL << 61)
#define	 IGNORE_LLC_CTRL	(1ULL << 60)
#define	 SCATTER_MODE		(1ULL << 57)
#define	 STRIP_VLAN_TAG		(1ULL << 48)

/*
 * Receive descriptor (RxD) format.
 * There are three formats of receive descriptors, 1, 3 and 5 buffer format.
 */
#define	RX_MODE_1 1
#define	RX_MODE_3 3
#define	RX_MODE_5 5

struct rxd1 {
	uint64_t rxd_hcontrol;
	uint64_t rxd_control1;
	uint64_t rxd_control2;  
	uint64_t rxd_buf0;
};

/* 4k struct for 5 buffer mode */
#define	NDESC_1BUFMODE		127	/* # desc/page for 5-buffer mode */
struct rxd1_4k {
	struct rxd1 r4_rxd[NDESC_1BUFMODE];
	uint64_t pad[3];
	uint64_t r4_next; /* phys address of next 4k buffer */
};

struct rxd3 {
	uint64_t rxd_hcontrol;
	uint64_t rxd_control1;
	uint64_t rxd_control2;
	uint64_t rxd_buf0;
	uint64_t rxd_buf1;      
	uint64_t rxd_buf2;      
};

struct rxd5 {
	uint64_t rxd_control3;
	uint64_t rxd_control1;
	uint64_t rxd_control2;
	uint64_t rxd_buf0;
	uint64_t rxd_buf1;
	uint64_t rxd_buf2;
	uint64_t rxd_buf3;
	uint64_t rxd_buf4;
};

/* 4k struct for 5 buffer mode */
#define	NDESC_5BUFMODE		63	/* # desc/page for 5-buffer mode */
#define	XGE_PAGE		4096	/* page size used for receive */
struct rxd5_4k {
	struct rxd5 r4_rxd[NDESC_5BUFMODE];
	uint64_t pad[7];
	uint64_t r4_next; /* phys address of next 4k buffer */
};

#define	RXD_MKCTL3(h,bs3,bs4)	\
	(((uint64_t)(h) << 32) | ((uint64_t)(bs3) << 16) | (uint64_t)(bs4))
#define	RXD_MKCTL2(bs0,bs1,bs2)	\
	(((uint64_t)(bs0) << 48) | ((uint64_t)(bs1) << 32) | \
	((uint64_t)(bs2) << 16))

#define	RXD_CTL2_BUF0SIZ(x)	(((x) >> 48) & 0xffff)
#define	RXD_CTL2_BUF1SIZ(x)	(((x) >> 32) & 0xffff)
#define	RXD_CTL2_BUF2SIZ(x)	(((x) >> 16) & 0xffff)
#define	RXD_CTL3_BUF3SIZ(x)	(((x) >> 16) & 0xffff)
#define	RXD_CTL3_BUF4SIZ(x)	((x) & 0xffff)
#define	RXD_CTL1_OWN		(1ULL << 56)
#define	RXD_CTL1_XCODE(x)	(((x) >> 48) & 0xf)	/* Status bits */
#define	 RXD_CTL1_X_OK		0
#define	 RXD_CTL1_X_PERR	1	/* Parity error */
#define	 RXD_CTL1_X_ABORT	2	/* Abort during xfer */
#define	 RXD_CTL1_X_PA		3	/* Parity error and abort */
#define	 RXD_CTL1_X_RDA		4	/* RDA failure */
#define	 RXD_CTL1_X_UP		5	/* Unknown protocol */
#define	 RXD_CTL1_X_FI		6	/* Frame integrity (FCS) error */
#define	 RXD_CTL1_X_BSZ		7	/* Buffer size error */
#define	 RXD_CTL1_X_ECC		8	/* Internal ECC */
#define	 RXD_CTL1_X_UNK		15	/* Unknown error */
#define	RXD_CTL1_PROTOS(x)	(((x) >> 32) & 0xff)
#define	 RXD_CTL1_P_VLAN	0x80	/* VLAN tagged */
#define	 RXD_CTL1_P_MSK		0x60	/* Mask for frame type */
#define	  RXD_CTL1_P_DIX	0x00
#define	  RXD_CTL1_P_LLC	0x20
#define	  RXD_CTL1_P_SNAP	0x40
#define	  RXD_CTL1_P_IPX	0x60
#define	 RXD_CTL1_P_IPv4	0x10
#define	 RXD_CTL1_P_IPv6	0x08
#define	 RXD_CTL1_P_IPFRAG	0x04
#define	 RXD_CTL1_P_TCP		0x02
#define	 RXD_CTL1_P_UDP		0x01
#define	RXD_CTL1_L3CSUM(x)	(((x) >> 16) & 0xffff)
#define	RXD_CTL1_L4CSUM(x)	((x) & 0xffff)
#define	RXD_CTL2_VLANTAG(x)	((x) & 0xffff)

/*
 * MAC Configuration/Status
 */
#define	MAC_INT_STATUS		MACRB(0x000)
#define	 MAC_TMAC_INT		(1ULL<<63)
#define	 MAC_RMAC_INT		(1ULL<<62)
#define	MAC_INT_MASK		MACRB(0x008)
#define	MAC_TMAC_ERR_MASK	MACRB(0x018)
#define	MAC_RMAC_ERR_REG	MACRB(0x028)
#define	 RMAC_LINK_STATE_CHANGE_INT (1ULL<<32)
#define	MAC_RMAC_ERR_MASK	MACRB(0x030)

#define	MAC_CFG			MACRB(0x0100)
#define	 TMAC_EN		(1ULL<<63)
#define	 RMAC_EN		(1ULL<<62)
#define	 UTILZATION_CALC_SEL	(1ULL<<61)
#define	 TMAC_LOOPBACK		(1ULL<<60)
#define	 TMAC_APPEND_PAD	(1ULL<<59)
#define	 RMAC_STRIP_FCS		(1ULL<<58)
#define	 RMAC_STRIP_PAD		(1ULL<<57)
#define	 RMAC_PROM_EN		(1ULL<<56)
#define	 RMAC_DISCARD_PFRM	(1ULL<<55)
#define	 RMAC_BCAST_EN		(1ULL<<54)
#define	 RMAC_ALL_ADDR_EN	(1ULL<<53)
#define	RMAC_MAX_PYLD_LEN	MACRB(0x0110)
#define	 RMAC_PYLD_LEN(x)	((uint64_t)(x) << 48)
#define	RMAC_CFG_KEY		MACRB(0x0120)
#define	 RMAC_KEY_VALUE		(0x4c0dULL<<48)
#define	RMAC_ADDR_CMD_MEM	MACRB(0x0128)
#define	 RMAC_ADDR_CMD_MEM_WE	(1ULL<<56)
#define	 RMAC_ADDR_CMD_MEM_STR	(1ULL<<48)
#define	 RMAC_ADDR_CMD_MEM_OFF(x) ((uint64_t)(x) << 32)
#define	MAX_MCAST_ADDR		64	/* slots in mcast table */
#define	RMAC_ADDR_DATA0_MEM	MACRB(0x0130)
#define	RMAC_ADDR_DATA1_MEM	MACRB(0x0138)
#define	RMAC_PAUSE_CFG		MACRB(0x150)
#define	 RMAC_PAUSE_GEN_EN	(1ULL<<63)
#define	 RMAC_PAUSE_RCV_EN	(1ULL<<62)

/*
 * RLDRAM registers.
 */
#define	MC_INT_MASK		RLDRB(0x008)
#define	MC_ERR_MASK		RLDRB(0x018)

#define	RX_QUEUE_CFG		RLDRB(0x100)
#define	 MC_QUEUE(q,s)		((uint64_t)(s)<<(56-(q*8)))
#define	MC_RLDRAM_MRS		RLDRB(0x108)
#define	 MC_QUEUE_SIZE_ENABLE	(1ULL<<24)
#define	 MC_RLDRAM_MRS_ENABLE	(1ULL<<16)

/*
 * XGXS registers.
 */
/* XGXS control/statue */
#define	XGXS_INT_MASK		XGXSB(0x008)
#define	XGXS_TXGXS_ERR_MASK	XGXSB(0x018)
#define	XGXS_RXGXS_ERR_MASK	XGXSB(0x030)
#define	XGXS_CFG		XGXSB(0x0100)
