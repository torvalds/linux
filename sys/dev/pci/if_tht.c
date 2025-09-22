/*	$OpenBSD: if_tht.c,v 1.149 2024/09/04 07:54:52 mglocker Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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
 * Driver for the Tehuti TN30xx multi port 10Gb Ethernet chipsets,
 * see http://www.tehutinetworks.net/.
 *
 * This driver was made possible because Tehuti networks provided
 * hardware and documentation. Thanks!
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/time.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#ifdef THT_DEBUG
#define THT_D_FIFO		(1<<0)
#define THT_D_TX		(1<<1)
#define THT_D_RX		(1<<2)
#define THT_D_INTR		(1<<3)

int thtdebug = THT_D_TX | THT_D_RX | THT_D_INTR;

#define DPRINTF(l, f...)	do { if (thtdebug & (l)) printf(f); } while (0)
#else
#define DPRINTF(l, f...)
#endif

/* registers */

#define THT_PCI_BAR		0x10

#define _Q(_q)			((_q) * 4)

/* General Configuration */
#define THT_REG_END_SEL		0x5448 /* PCI Endian Select */
#define THT_REG_CLKPLL		0x5000
#define  THT_REG_CLKPLL_PLLLK		(1<<9) /* PLL is locked */
#define  THT_REG_CLKPLL_RSTEND		(1<<8) /* Reset ended */
#define  THT_REG_CLKPLL_TXF_DIS		(1<<3) /* TX Free disabled */
#define  THT_REG_CLKPLL_VNT_STOP	(1<<2) /* VENETO Stop */
#define  THT_REG_CLKPLL_PLLRST		(1<<1) /* PLL Reset */
#define  THT_REG_CLKPLL_SFTRST		(1<<0) /* Software Reset */
/* Descriptors and FIFO Registers */
#define THT_REG_TXT_CFG0(_q)	(0x4040 + _Q(_q)) /* CFG0 TX Task queues */
#define THT_REG_RXF_CFG0(_q)	(0x4050 + _Q(_q)) /* CFG0 RX Free queues */
#define THT_REG_RXD_CFG0(_q)	(0x4060 + _Q(_q)) /* CFG0 RX DSC queues */
#define THT_REG_TXF_CFG0(_q)	(0x4070 + _Q(_q)) /* CFG0 TX Free queues */
#define THT_REG_TXT_CFG1(_q)	(0x4000 + _Q(_q)) /* CFG1 TX Task queues */
#define THT_REG_RXF_CFG1(_q)	(0x4010 + _Q(_q)) /* CFG1 RX Free queues */
#define THT_REG_RXD_CFG1(_q)	(0x4020 + _Q(_q)) /* CFG1 RX DSC queues */
#define THT_REG_TXF_CFG1(_q)	(0x4030 + _Q(_q)) /* CFG1 TX Free queues */
#define THT_REG_TXT_RPTR(_q)	(0x40c0 + _Q(_q)) /* TX Task read ptr */
#define THT_REG_RXF_RPTR(_q)	(0x40d0 + _Q(_q)) /* RX Free read ptr */
#define THT_REG_RXD_RPTR(_q)	(0x40e0 + _Q(_q)) /* RX DSC read ptr */
#define THT_REG_TXF_RPTR(_q)	(0x40f0 + _Q(_q)) /* TX Free read ptr */
#define THT_REG_TXT_WPTR(_q)	(0x4080 + _Q(_q)) /* TX Task write ptr */
#define THT_REG_RXF_WPTR(_q)	(0x4090 + _Q(_q)) /* RX Free write ptr */
#define THT_REG_RXD_WPTR(_q)	(0x40a0 + _Q(_q)) /* RX DSC write ptr */
#define THT_REG_TXF_WPTR(_q)	(0x40b0 + _Q(_q)) /* TX Free write ptr */
#define THT_REG_HTB_ADDR	0x4100 /* HTB Addressing Mechanism enable */
#define THT_REG_HTB_ADDR_HI	0x4110 /* High HTB Address */
#define THT_REG_HTB_ST_TMR	0x3290 /* HTB Timer */
#define THT_REG_RDINTCM(_q)	(0x5120 + _Q(_q)) /* RX DSC Intr Coalescing */
#define  THT_REG_RDINTCM_PKT_TH(_c)	((_c)<<20) /* pkt count threshold */
#define  THT_REG_RDINTCM_RXF_TH(_c)	((_c)<<16) /* rxf intr req thresh */
#define  THT_REG_RDINTCM_COAL_RC	(1<<15) /* coalescing timer recharge */
#define  THT_REG_RDINTCM_COAL(_c)	(_c) /* coalescing timer */
#define THT_REG_TDINTCM(_q)	(0x5130 + _Q(_q)) /* TX DSC Intr Coalescing */
#define  THT_REG_TDINTCM_PKT_TH(_c)	((_c)<<20) /* pkt count threshold */
#define  THT_REG_TDINTCM_COAL_RC	(1<<15) /* coalescing timer recharge */
#define  THT_REG_TDINTCM_COAL(_c)	(_c) /* coalescing timer */
/* 10G Ethernet MAC */
#define THT_REG_10G_REV		0x6000 /* Revision */
#define THT_REG_10G_SCR		0x6004 /* Scratch */
#define THT_REG_10G_CTL		0x6008 /* Control/Status */
#define  THT_REG_10G_CTL_CMD_FRAME_EN	(1<<13) /* cmd frame enable */
#define  THT_REG_10G_CTL_SW_RESET	(1<<12) /* sw reset */
#define  THT_REG_10G_CTL_STATS_AUTO_CLR	(1<<11) /* auto clear statistics */
#define  THT_REG_10G_CTL_LOOPBACK	(1<<10) /* enable loopback */
#define  THT_REG_10G_CTL_TX_ADDR_INS	(1<<9) /* set mac on tx */
#define  THT_REG_10G_CTL_PAUSE_IGNORE	(1<<8) /* ignore pause */
#define  THT_REG_10G_CTL_PAUSE_FWD	(1<<7) /* forward pause */
#define  THT_REG_10G_CTL_CRC_FWD	(1<<6) /* crc forward */
#define  THT_REG_10G_CTL_PAD		(1<<5) /* frame padding */
#define  THT_REG_10G_CTL_PROMISC	(1<<4) /* promiscuous mode */
#define  THT_REG_10G_CTL_WAN_MODE	(1<<3) /* WAN mode */
#define  THT_REG_10G_CTL_RX_EN		(1<<1) /* RX enable */
#define  THT_REG_10G_CTL_TX_EN		(1<<0) /* TX enable */
#define THT_REG_10G_FRM_LEN	0x6014 /* Frame Length */
#define THT_REG_10G_PAUSE	0x6018 /* Pause Quanta */
#define THT_REG_10G_RX_SEC	0x601c /* RX Section */
#define THT_REG_10G_TX_SEC	0x6020 /* TX Section */
#define  THT_REG_10G_SEC_AVAIL(_t)	(_t) /* section available thresh*/
#define  THT_REG_10G_SEC_EMPTY(_t)	((_t)<<16) /* section empty avail */
#define THT_REG_10G_RFIFO_AEF	0x6024 /* RX FIFO Almost Empty/Full */
#define THT_REG_10G_TFIFO_AEF	0x6028 /* TX FIFO Almost Empty/Full */
#define  THT_REG_10G_FIFO_AE(_t)	(_t) /* almost empty */
#define  THT_REG_10G_FIFO_AF(_t)	((_t)<<16) /* almost full */
#define THT_REG_10G_SM_STAT	0x6030 /* MDIO Status */
#define THT_REG_10G_SM_CMD	0x6034 /* MDIO Command */
#define THT_REG_10G_SM_DAT	0x6038 /* MDIO Data */
#define THT_REG_10G_SM_ADD	0x603c /* MDIO Address */
#define THT_REG_10G_STAT	0x6040 /* Status */
/* Statistic Counters */
/* XXX todo */
/* Status Registers */
#define THT_REG_MAC_LNK_STAT	0x0200 /* Link Status */
#define  THT_REG_MAC_LNK_STAT_DIS	(1<<4) /* Mac Stats read disable */
#define  THT_REG_MAC_LNK_STAT_LINK	(1<<2) /* Link State */
#define  THT_REG_MAC_LNK_STAT_REM_FAULT	(1<<1) /* Remote Fault */
#define  THT_REG_MAC_LNK_STAT_LOC_FAULT	(1<<0) /* Local Fault */
/* Interrupt Registers */
#define THT_REG_ISR		0x5100 /* Interrupt Status */
#define THT_REG_ISR_LINKCHG(_p)		(1<<(27+(_p))) /* link changed */
#define THT_REG_ISR_GPIO		(1<<26) /* GPIO */
#define THT_REG_ISR_RFRSH		(1<<25) /* DDR Refresh */
#define THT_REG_ISR_SWI			(1<<23) /* software interrupt */
#define THT_REG_ISR_RXF(_q)		(1<<(19+(_q))) /* rx free fifo */
#define THT_REG_ISR_TXF(_q)		(1<<(15+(_q))) /* tx free fifo */
#define THT_REG_ISR_RXD(_q)		(1<<(11+(_q))) /* rx desc fifo */
#define THT_REG_ISR_TMR(_t)		(1<<(6+(_t))) /* timer */
#define THT_REG_ISR_VNT			(1<<5) /* optistrata */
#define THT_REG_ISR_RxFL		(1<<4) /* RX Full */
#define THT_REG_ISR_TR			(1<<2) /* table read */
#define THT_REG_ISR_PCIE_LNK_INT	(1<<1) /* pcie link fail */
#define THT_REG_ISR_GPLE_CLR		(1<<0) /* pcie timeout */
#define THT_FMT_ISR		"\020" "\035LINKCHG1" "\034LINKCHG0" \
				    "\033GPIO" "\032RFRSH" "\030SWI" \
				    "\027RXF3" "\026RXF2" "\025RXF1" \
				    "\024RXF0" "\023TXF3" "\022TXF2" \
				    "\021TXF1" "\020TXF0" "\017RXD3" \
				    "\016RXD2" "\015RXD1" "\014RXD0" \
				    "\012TMR3" "\011TMR2" "\010TMR1" \
				    "\007TMR0" "\006VNT" "\005RxFL" \
				    "\003TR" "\002PCI_LNK_INT" \
				    "\001GPLE_CLR"
#define THT_REG_ISR_GTI		0x5080 /* GTI Interrupt Status */
#define THT_REG_IMR		0x5110 /* Interrupt Mask */
#define THT_REG_IMR_LINKCHG(_p)		(1<<(27+(_p))) /* link changed */
#define THT_REG_IMR_GPIO		(1<<26) /* GPIO */
#define THT_REG_IMR_RFRSH		(1<<25) /* DDR Refresh */
#define THT_REG_IMR_SWI			(1<<23) /* software interrupt */
#define THT_REG_IMR_RXF(_q)		(1<<(19+(_q))) /* rx free fifo */
#define THT_REG_IMR_TXF(_q)		(1<<(15+(_q))) /* tx free fifo */
#define THT_REG_IMR_RXD(_q)		(1<<(11+(_q))) /* rx desc fifo */
#define THT_REG_IMR_TMR(_t)		(1<<(6+(_t))) /* timer */
#define THT_REG_IMR_VNT			(1<<5) /* optistrata */
#define THT_REG_IMR_RxFL		(1<<4) /* RX Full */
#define THT_REG_IMR_TR			(1<<2) /* table read */
#define THT_REG_IMR_PCIE_LNK_INT	(1<<1) /* pcie link fail */
#define THT_REG_IMR_GPLE_CLR		(1<<0) /* pcie timeout */
#define THT_REG_IMR_GTI		0x5090 /* GTI Interrupt Mask */
#define THT_REG_ISR_MSK		0x5140 /* ISR Masked */
/* Global Counters */
/* XXX todo */
/* DDR2 SDRAM Controller Registers */
/* XXX TBD */
/* EEPROM Registers */
/* XXX todo */
/* Init arbitration and status registers */
#define THT_REG_INIT_SEMAPHORE	0x5170 /* Init Semaphore */
#define THT_REG_INIT_STATUS	0x5180 /* Init Status */
/* PCI Credits Registers */
/* XXX todo */
/* TX Arbitration Registers */
#define THT_REG_TXTSK_PR(_q)	(0x41b0 + _Q(_q)) /* TX Queue Priority */
/* RX Part Registers */
#define THT_REG_RX_FLT		0x1240 /* RX Filter Configuration */
#define  THT_REG_RX_FLT_ATXER		(1<<15) /* accept with xfer err */
#define  THT_REG_RX_FLT_ATRM		(1<<14) /* accept with term err */
#define  THT_REG_RX_FLT_AFTSQ		(1<<13) /* accept with fault seq */
#define  THT_REG_RX_FLT_OSEN		(1<<12) /* enable pkts */
#define  THT_REG_RX_FLT_APHER		(1<<11) /* accept with phy err */
#define  THT_REG_RX_FLT_TXFC		(1<<10) /* TX flow control */
#define  THT_REG_RX_FLT_FDA		(1<<8) /* filter direct address */
#define  THT_REG_RX_FLT_AOF		(1<<7) /* accept overflow frame */
#define  THT_REG_RX_FLT_ACF		(1<<6) /* accept control frame */
#define  THT_REG_RX_FLT_ARUNT		(1<<5) /* accept runt */
#define  THT_REG_RX_FLT_ACRC		(1<<4) /* accept crc error */
#define  THT_REG_RX_FLT_AM		(1<<3) /* accept multicast */
#define  THT_REG_RX_FLT_AB		(1<<2) /* accept broadcast */
#define  THT_REG_RX_FLT_PRM_MASK	0x3 /* promiscuous mode */
#define  THT_REG_RX_FLT_PRM_NORMAL	0x0 /* normal mode */
#define  THT_REG_RX_FLT_PRM_ALL		0x1 /* pass all incoming frames */
#define THT_REG_RX_MAX_FRAME	0x12c0 /* Max Frame Size */
#define THT_REG_RX_UNC_MAC0	0x1250 /* MAC Address low word */
#define THT_REG_RX_UNC_MAC1	0x1260 /* MAC Address mid word */
#define THT_REG_RX_UNC_MAC2	0x1270 /* MAC Address high word */
#define THT_REG_RX_MAC_MCST0(_m) (0x1a80 + (_m)*8)
#define THT_REG_RX_MAC_MCST1(_m) (0x1a84 + (_m)*8)
#define  THT_REG_RX_MAC_MCST_CNT	15
#define THT_REG_RX_MCST_HASH	0x1a00 /* imperfect multicast filter hash */
#define  THT_REG_RX_MCST_HASH_SIZE	(256 / NBBY)
/* OptiStrata Debug Registers */
#define THT_REG_VPC		0x2300 /* Program Counter */
#define THT_REG_VLI		0x2310 /* Last Interrupt */
#define THT_REG_VIC		0x2320 /* Interrupts Count */
#define THT_REG_VTMR		0x2330 /* Timer */
#define THT_REG_VGLB		0x2340 /* Global */
/* SW Reset Registers */
#define THT_REG_RST_PRT		0x7000 /* Reset Port */
#define  THT_REG_RST_PRT_ACTIVE		0x1 /* port reset is active */
#define THT_REG_DIS_PRT		0x7010 /* Disable Port */
#define THT_REG_RST_QU_0	0x7020 /* Reset Queue 0 */
#define THT_REG_RST_QU_1	0x7028 /* Reset Queue 1 */
#define THT_REG_DIS_QU_0	0x7030 /* Disable Queue 0 */
#define THT_REG_DIS_QU_1	0x7038 /* Disable Queue 1 */

#define THT_PORT_SIZE		0x8000
#define THT_PORT_REGION(_p)	((_p) * THT_PORT_SIZE)
#define THT_NQUEUES		4

#define THT_FIFO_ALIGN		4096
#define THT_FIFO_SIZE_4k	0x0
#define THT_FIFO_SIZE_8k	0x1
#define THT_FIFO_SIZE_16k	0x2
#define THT_FIFO_SIZE_32k	0x3
#define THT_FIFO_SIZE(_r)	(4096 * (1<<(_r)))
#define THT_FIFO_GAP		8 /* keep 8 bytes between ptrs */
#define THT_FIFO_PTR_MASK	0x00007ff8 /* rptr/wptr mask */

#define THT_FIFO_DESC_LEN	208 /* a descriptor can't be bigger than this */

#define THT_IMR_DOWN(_p)	(THT_REG_IMR_LINKCHG(_p))
#define THT_IMR_UP(_p)		(THT_REG_IMR_LINKCHG(_p) | \
				    THT_REG_IMR_RXF(0) | THT_REG_IMR_TXF(0) | \
				    THT_REG_IMR_RXD(0))

/* hardware structures (we're using the 64 bit variants) */

/* physical buffer descriptor */
struct tht_pbd {
	u_int32_t		addr_lo;
	u_int32_t		addr_hi;
	u_int32_t		len;
} __packed;
#define THT_PBD_PKTLEN		(64 * 1024)

/* rx free fifo */
struct tht_rx_free {
	u_int16_t		bc; /* buffer count (0:4) */
	u_int16_t		type;

	u_int64_t		uid;

	/* followed by a pdb list */
} __packed;
#define THT_RXF_TYPE		1
#define THT_RXF_1ST_PDB_LEN	128
#define THT_RXF_SGL_LEN		((THT_FIFO_DESC_LEN - \
				    sizeof(struct tht_rx_free)) / \
				    sizeof(struct tht_pbd))
#define THT_RXF_PKT_NUM		128

/* rx descriptor */
struct tht_rx_desc {
	u_int32_t		flags;
#define THT_RXD_FLAGS_BC(_f)		((_f) & 0x1f) /* buffer count */
#define THT_RXD_FLAGS_RXFQ(_f)		(((_f)>>8) & 0x3) /* rxf queue id */
#define THT_RXD_FLAGS_TO		(1<<15)
#define THT_RXD_FLAGS_TYPE(_f)		(((_f)>>16) & 0xf) /* desc type */
#define THT_RXD_FLAGS_OVF		(1<<21) /* overflow error */
#define THT_RXD_FLAGS_RUNT		(1<<22) /* runt error */
#define THT_RXD_FLAGS_CRC		(1<<23) /* crc error */
#define THT_RXD_FLAGS_UDPCS		(1<<24) /* udp checksum error */
#define THT_RXD_FLAGS_TCPCS		(1<<25) /* tcp checksum error */
#define THT_RXD_FLAGS_IPCS		(1<<26) /* ip checksum error */
#define THT_RXD_FLAGS_PKT_ID		0x70000000
#define THT_RXD_FLAGS_PKT_ID_NONIP	0x00000000
#define THT_RXD_FLAGS_PKT_ID_TCP4	0x10000000
#define THT_RXD_FLAGS_PKT_ID_UDP4	0x20000000
#define THT_RXD_FLAGS_PKT_ID_IPV4	0x30000000
#define THT_RXD_FLAGS_PKT_ID_TCP6	0x50000000
#define THT_RXD_FLAGS_PKT_ID_UDP6	0x60000000
#define THT_RXD_FLAGS_PKT_ID_IPV6	0x70000000
#define THT_RXD_FLAGS_VTAG		(1<<31)
	u_int16_t		len;
	u_int16_t		vlan;
#define THT_RXD_VLAN_ID(_v)		((_v) & 0xfff)
#define THT_RXD_VLAN_CFI		(1<<12)
#define THT_RXD_VLAN_PRI(_v)		((_v) & 0x7) >> 13)

	u_int64_t		uid;
} __packed;
#define THT_RXD_TYPE		2

/* rx descriptor type 3: data chain instruction */
struct tht_rx_desc_dc {
	/* preceded by tht_rx_desc */

	u_int16_t		cd_offset;
	u_int16_t		flags;

	u_int8_t		data[4];
} __packed;
#define THT_RXD_TYPE_DC		3

/* rx descriptor type 4: rss (recv side scaling) information */
struct tht_rx_desc_rss {
	/* preceded by tht_rx_desc */

	u_int8_t		rss_hft;
	u_int8_t		rss_type;
	u_int8_t		rss_tcpu;
	u_int8_t		reserved;

	u_int32_t		rss_hash;
} __packed;
#define THT_RXD_TYPE_RSS	4

/* tx task fifo */
struct tht_tx_task {
	u_int32_t		flags;
#define THT_TXT_FLAGS_BC(_f)	(_f) /* buffer count */
#define THT_TXT_FLAGS_UDPCS	(1<<5) /* udp checksum */
#define THT_TXT_FLAGS_TCPCS	(1<<6) /* tcp checksum */
#define THT_TXT_FLAGS_IPCS	(1<<7) /* ip checksum */
#define THT_TXT_FLAGS_VTAG	(1<<8) /* insert vlan tag */
#define THT_TXT_FLAGS_LGSND	(1<<9) /* tcp large send enabled */
#define THT_TXT_FLAGS_FRAG	(1<<10) /* ip fragmentation enabled */
#define THT_TXT_FLAGS_CFI	(1<<12) /* canonical format indicator */
#define THT_TXT_FLAGS_PRIO(_f)	((_f)<<13) /* vlan priority */
#define THT_TXT_FLAGS_VLAN(_f)	((_f)<<20) /* vlan id */
	u_int16_t		mss_mtu;
	u_int16_t		len;

	u_int64_t		uid;

	/* followed by a pbd list */
} __packed;
#define THT_TXT_TYPE		(3<<16)
#define THT_TXT_SGL_LEN		((THT_FIFO_DESC_LEN - \
				    sizeof(struct tht_tx_task)) / \
				    sizeof(struct tht_pbd))
#define THT_TXT_PKT_NUM		128

/* tx free fifo */
struct tht_tx_free {
	u_int32_t		status;

	u_int64_t		uid;

	u_int32_t		pad;
} __packed;

/* pci controller autoconf glue */

struct thtc_softc {
	struct device		sc_dev;

	bus_dma_tag_t		sc_dmat;

	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
	bus_size_t		sc_mems;
	void			*sc_ih;
};

int			thtc_match(struct device *, void *, void *);
void			thtc_attach(struct device *, struct device *, void *);
int			thtc_print(void *, const char *);

const struct cfattach thtc_ca = {
	sizeof(struct thtc_softc), thtc_match, thtc_attach
};

struct cfdriver thtc_cd = {
	NULL, "thtc", DV_DULL
};

/* glue between the controller and the port */

struct tht_attach_args {
	int			taa_port;

	struct pci_attach_args	*taa_pa;
};

/* tht itself */

struct tht_dmamem {
	bus_dmamap_t		tdm_map;
	bus_dma_segment_t	tdm_seg;
	size_t			tdm_size;
	caddr_t			tdm_kva;
};
#define THT_DMA_MAP(_tdm)	((_tdm)->tdm_map)
#define THT_DMA_DVA(_tdm)	((_tdm)->tdm_map->dm_segs[0].ds_addr)
#define THT_DMA_KVA(_tdm)	((void *)(_tdm)->tdm_kva)

struct tht_fifo_desc {
	bus_size_t		tfd_cfg0;
	bus_size_t		tfd_cfg1;
	bus_size_t		tfd_rptr;
	bus_size_t		tfd_wptr;
	u_int32_t		tfd_size;
	int			tfd_write;
};
#define THT_FIFO_PRE_SYNC(_d)	((_d)->tfd_write ? \
				    BUS_DMASYNC_PREWRITE : \
				    BUS_DMASYNC_PREREAD)
#define THT_FIFO_POST_SYNC(_d)	((_d)->tfd_write ? \
				    BUS_DMASYNC_POSTWRITE : \
				    BUS_DMASYNC_POSTREAD)

struct tht_fifo {
	struct tht_fifo_desc	*tf_desc;
	struct tht_dmamem	*tf_mem;
	int			tf_len;
	int			tf_rptr;
	int			tf_wptr;
	int			tf_ready;
};

struct tht_pkt {
	u_int64_t		tp_id;

	bus_dmamap_t		tp_dmap;
	struct mbuf		*tp_m;

	TAILQ_ENTRY(tht_pkt)	tp_link;
};

struct tht_pkt_list {
	struct tht_pkt		*tpl_pkts;
	TAILQ_HEAD(, tht_pkt)	tpl_free;
	TAILQ_HEAD(, tht_pkt)	tpl_used;
};

struct tht_softc {
	struct device		sc_dev;
	struct thtc_softc	*sc_thtc;
	int			sc_port;

	bus_space_handle_t	sc_memh;

	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
	struct timeval		sc_mediacheck;

	u_int16_t		sc_lladdr[3];

	struct tht_pkt_list	sc_tx_list;
	struct tht_pkt_list	sc_rx_list;

	struct tht_fifo		sc_txt;
	struct tht_fifo		sc_rxf;
	struct tht_fifo		sc_rxd;
	struct tht_fifo		sc_txf;

	u_int32_t		sc_imr;

	struct rwlock		sc_lock;
};

int			tht_match(struct device *, void *, void *);
void			tht_attach(struct device *, struct device *, void *);
void			tht_mountroot(struct device *);
int			tht_intr(void *);

const struct cfattach tht_ca = {
	sizeof(struct tht_softc), tht_match, tht_attach
};

struct cfdriver tht_cd = {
	NULL, "tht", DV_IFNET
};

/* pkts */
int			tht_pkt_alloc(struct tht_softc *,
			    struct tht_pkt_list *, int, int);
void			tht_pkt_free(struct tht_softc *,
			    struct tht_pkt_list *);
void			tht_pkt_put(struct tht_pkt_list *, struct tht_pkt *);
struct tht_pkt 		*tht_pkt_get(struct tht_pkt_list *);
struct tht_pkt		*tht_pkt_used(struct tht_pkt_list *);

/* fifos */

struct tht_fifo_desc tht_txt_desc = {
	THT_REG_TXT_CFG0(0),
	THT_REG_TXT_CFG1(0),
	THT_REG_TXT_RPTR(0),
	THT_REG_TXT_WPTR(0),
	THT_FIFO_SIZE_16k,
	1
};

struct tht_fifo_desc tht_rxf_desc = {
	THT_REG_RXF_CFG0(0),
	THT_REG_RXF_CFG1(0),
	THT_REG_RXF_RPTR(0),
	THT_REG_RXF_WPTR(0),
	THT_FIFO_SIZE_16k,
	1
};

struct tht_fifo_desc tht_rxd_desc = {
	THT_REG_RXD_CFG0(0),
	THT_REG_RXD_CFG1(0),
	THT_REG_RXD_RPTR(0),
	THT_REG_RXD_WPTR(0),
	THT_FIFO_SIZE_16k,
	0
};

struct tht_fifo_desc tht_txf_desc = {
	THT_REG_TXF_CFG0(0),
	THT_REG_TXF_CFG1(0),
	THT_REG_TXF_RPTR(0),
	THT_REG_TXF_WPTR(0),
	THT_FIFO_SIZE_4k,
	0
};

int			tht_fifo_alloc(struct tht_softc *, struct tht_fifo *,
			    struct tht_fifo_desc *);
void			tht_fifo_free(struct tht_softc *, struct tht_fifo *);

size_t			tht_fifo_readable(struct tht_softc *,
			    struct tht_fifo *);
size_t			tht_fifo_writable(struct tht_softc *,
			    struct tht_fifo *);
void			tht_fifo_pre(struct tht_softc *,
			    struct tht_fifo *);
void			tht_fifo_read(struct tht_softc *, struct tht_fifo *,
			    void *, size_t);
void			tht_fifo_write(struct tht_softc *, struct tht_fifo *,
			    void *, size_t);
void			tht_fifo_write_dmap(struct tht_softc *,
			    struct tht_fifo *, bus_dmamap_t);
void			tht_fifo_write_pad(struct tht_softc *,
			    struct tht_fifo *, int);
void			tht_fifo_post(struct tht_softc *,
			    struct tht_fifo *);

/* port operations */
void			tht_lladdr_read(struct tht_softc *);
void			tht_lladdr_write(struct tht_softc *);
int			tht_sw_reset(struct tht_softc *);
int			tht_fw_load(struct tht_softc *);
void			tht_link_state(struct tht_softc *);

/* interface operations */
int			tht_ioctl(struct ifnet *, u_long, caddr_t);
void			tht_watchdog(struct ifnet *);
void			tht_start(struct ifnet *);
int			tht_load_pkt(struct tht_softc *, struct tht_pkt *,
			    struct mbuf *);
void			tht_txf(struct tht_softc *sc);

void			tht_rxf_fill(struct tht_softc *, int);
void			tht_rxf_drain(struct tht_softc *);
void			tht_rxd(struct tht_softc *);

void			tht_up(struct tht_softc *);
void			tht_iff(struct tht_softc *);
void			tht_down(struct tht_softc *);

/* ifmedia operations */
int			tht_media_change(struct ifnet *);
void			tht_media_status(struct ifnet *, struct ifmediareq *);

/* wrapper around dma memory */
struct tht_dmamem	*tht_dmamem_alloc(struct tht_softc *, bus_size_t,
			    bus_size_t);
void			tht_dmamem_free(struct tht_softc *,
			    struct tht_dmamem *);

/* bus space operations */
u_int32_t		tht_read(struct tht_softc *, bus_size_t);
void			tht_write(struct tht_softc *, bus_size_t, u_int32_t);
void			tht_write_region(struct tht_softc *, bus_size_t,
			    void *, size_t);
int			tht_wait_eq(struct tht_softc *, bus_size_t, u_int32_t,
			    u_int32_t, int);
int			tht_wait_ne(struct tht_softc *, bus_size_t, u_int32_t,
			    u_int32_t, int);

#define tht_set(_s, _r, _b)		tht_write((_s), (_r), \
					    tht_read((_s), (_r)) | (_b))
#define tht_clr(_s, _r, _b)		tht_write((_s), (_r), \
					    tht_read((_s), (_r)) & ~(_b))
#define tht_wait_set(_s, _r, _b, _t)	tht_wait_eq((_s), (_r), \
					    (_b), (_b), (_t))


/* misc */
#define DEVNAME(_sc)	((_sc)->sc_dev.dv_xname)
#define LWORDS(_b)	(((_b) + 7) >> 3)


struct thtc_device {
	pci_vendor_id_t		td_vendor;
	pci_vendor_id_t		td_product;
	u_int			td_nports;
};

const struct thtc_device *thtc_lookup(struct pci_attach_args *);

static const struct thtc_device thtc_devices[] = {
	{ PCI_VENDOR_TEHUTI,	PCI_PRODUCT_TEHUTI_TN3009, 1 },
	{ PCI_VENDOR_TEHUTI,	PCI_PRODUCT_TEHUTI_TN3010, 1 },
	{ PCI_VENDOR_TEHUTI,	PCI_PRODUCT_TEHUTI_TN3014, 2 }
};

const struct thtc_device *
thtc_lookup(struct pci_attach_args *pa)
{
	int				i;
	const struct thtc_device	*td;

	for (i = 0; i < nitems(thtc_devices); i++) {
		td = &thtc_devices[i];
		if (td->td_vendor == PCI_VENDOR(pa->pa_id) &&
		    td->td_product == PCI_PRODUCT(pa->pa_id))
			return (td);
	}

	return (NULL);
}

int
thtc_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args		*pa = aux;

	if (thtc_lookup(pa) != NULL)
		return (1);

	return (0);
}

void
thtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct thtc_softc		*sc = (struct thtc_softc *)self;
	struct pci_attach_args		*pa = aux;
	pcireg_t			memtype;
	const struct thtc_device	*td;
	struct tht_attach_args		taa;
	pci_intr_handle_t		ih;
	int				i;

	bzero(&taa, sizeof(taa));
	td = thtc_lookup(pa);

	sc->sc_dmat = pa->pa_dmat;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, THT_PCI_BAR);
	if (pci_mapreg_map(pa, THT_PCI_BAR, memtype, 0, &sc->sc_memt,
	    &sc->sc_memh, NULL, &sc->sc_mems, 0) != 0) {
		printf(": unable to map host registers\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto unmap;
	}

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih,
	    IPL_NET, tht_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		return;
	}
	printf(": %s\n", pci_intr_string(pa->pa_pc, ih));

	taa.taa_pa = pa;
	for (i = 0; i < td->td_nports; i++) {
		taa.taa_port = i;

		config_found(self, &taa, thtc_print);
	}

	return;

unmap:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
	sc->sc_mems = 0;
}

int
thtc_print(void *aux, const char *pnp)
{
	struct tht_attach_args		*taa = aux;

	if (pnp != NULL)
		printf("\"%s\" at %s", tht_cd.cd_name, pnp);

	printf(" port %d", taa->taa_port);

	return (UNCONF);
}

int
tht_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
tht_attach(struct device *parent, struct device *self, void *aux)
{
	struct thtc_softc		*csc = (struct thtc_softc *)parent;
	struct tht_softc		*sc = (struct tht_softc *)self;
	struct tht_attach_args		*taa = aux;
	struct ifnet			*ifp;

	sc->sc_thtc = csc;
	sc->sc_port = taa->taa_port;
	sc->sc_imr = THT_IMR_DOWN(sc->sc_port);
	rw_init(&sc->sc_lock, "thtioc");

	if (bus_space_subregion(csc->sc_memt, csc->sc_memh,
	    THT_PORT_REGION(sc->sc_port), THT_PORT_SIZE,
	    &sc->sc_memh) != 0) {
		printf(": unable to map port registers\n");
		return;
	}

	if (tht_sw_reset(sc) != 0) {
		printf(": unable to reset port\n");
		/* bus_space(9) says we dont have to free subregions */
		return;
	}

	tht_lladdr_read(sc);
	bcopy(sc->sc_lladdr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_ioctl = tht_ioctl;
	ifp->if_start = tht_start;
	ifp->if_watchdog = tht_watchdog;
	ifp->if_hardmtu = MCLBYTES - ETHER_HDR_LEN - ETHER_CRC_LEN; /* XXX */
	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, 400);

	ifmedia_init(&sc->sc_media, 0, tht_media_change, tht_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	printf(": address %s\n", ether_sprintf(sc->sc_ac.ac_enaddr));

	config_mountroot(self, tht_mountroot);
}

void
tht_mountroot(struct device *self)
{
	struct tht_softc		*sc = (struct tht_softc *)self;

	if (tht_fifo_alloc(sc, &sc->sc_txt, &tht_txt_desc) != 0)
		return;

	if (tht_fw_load(sc) != 0)
		printf("%s: firmware load failed\n", DEVNAME(sc));

	tht_sw_reset(sc);

	tht_fifo_free(sc, &sc->sc_txt);

	tht_link_state(sc);
	tht_write(sc, THT_REG_IMR, sc->sc_imr);
}

int
tht_intr(void *arg)
{
	struct thtc_softc		*thtc = arg;
	struct tht_softc		*sc = arg;
        struct device			*d;
	struct ifnet			*ifp;
	u_int32_t			isr;
	int				rv = 0;

	for (d = TAILQ_NEXT(&thtc->sc_dev, dv_list); d != NULL;
	    d = TAILQ_NEXT(d, dv_list)) {
		sc = (struct tht_softc *)d;

		isr = tht_read(sc, THT_REG_ISR);
		if (isr == 0x0) {
			tht_write(sc, THT_REG_IMR, sc->sc_imr);
			continue;
		}
		rv = 1;

		DPRINTF(THT_D_INTR, "%s: isr: 0x%b\n", DEVNAME(sc), isr, THT_FMT_ISR);

		if (ISSET(isr, THT_REG_ISR_LINKCHG(0) | THT_REG_ISR_LINKCHG(1)))
			tht_link_state(sc);

		ifp = &sc->sc_ac.ac_if;
		if (ifp->if_flags & IFF_RUNNING) {
			if (ISSET(isr, THT_REG_ISR_RXD(0)))
				tht_rxd(sc);

			if (ISSET(isr, THT_REG_ISR_RXF(0)))
				tht_rxf_fill(sc, 0);

			if (ISSET(isr, THT_REG_ISR_TXF(0)))
				tht_txf(sc);

			tht_start(ifp);
		}
		tht_write(sc, THT_REG_IMR, sc->sc_imr);
	}
	return (rv);
}

int
tht_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct tht_softc		*sc = ifp->if_softc;
	struct ifreq			*ifr = (struct ifreq *)addr;
	int				s, error = 0;

	rw_enter_write(&sc->sc_lock);
	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				tht_up(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				tht_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error =  ether_ioctl(ifp, &sc->sc_ac, cmd, addr);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			tht_iff(sc);
		error = 0;
	}

	splx(s);
	rw_exit_write(&sc->sc_lock);

	return (error);
}

void
tht_up(struct tht_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		return;
	}

	if (tht_pkt_alloc(sc, &sc->sc_tx_list, THT_TXT_PKT_NUM,
	    THT_TXT_SGL_LEN) != 0)
		return;
	if (tht_pkt_alloc(sc, &sc->sc_rx_list, THT_RXF_PKT_NUM,
	    THT_RXF_SGL_LEN) != 0)
		goto free_tx_list;

	if (tht_fifo_alloc(sc, &sc->sc_txt, &tht_txt_desc) != 0)
		goto free_rx_list;
	if (tht_fifo_alloc(sc, &sc->sc_rxf, &tht_rxf_desc) != 0)
		goto free_txt;
	if (tht_fifo_alloc(sc, &sc->sc_rxd, &tht_rxd_desc) != 0)
		goto free_rxf;
	if (tht_fifo_alloc(sc, &sc->sc_txf, &tht_txf_desc) != 0)
		goto free_rxd;

	tht_write(sc, THT_REG_10G_FRM_LEN, MCLBYTES - ETHER_ALIGN);
	tht_write(sc, THT_REG_10G_PAUSE, 0x96);
	tht_write(sc, THT_REG_10G_RX_SEC, THT_REG_10G_SEC_AVAIL(0x10) |
	    THT_REG_10G_SEC_EMPTY(0x80));
	tht_write(sc, THT_REG_10G_TX_SEC, THT_REG_10G_SEC_AVAIL(0x10) |
	    THT_REG_10G_SEC_EMPTY(0xe0));
	tht_write(sc, THT_REG_10G_RFIFO_AEF, THT_REG_10G_FIFO_AE(0x0) |
	    THT_REG_10G_FIFO_AF(0x0));
	tht_write(sc, THT_REG_10G_TFIFO_AEF, THT_REG_10G_FIFO_AE(0x0) |
	    THT_REG_10G_FIFO_AF(0x0));
	tht_write(sc, THT_REG_10G_CTL, THT_REG_10G_CTL_TX_EN |
	    THT_REG_10G_CTL_RX_EN | THT_REG_10G_CTL_PAD |
	    THT_REG_10G_CTL_PROMISC);

	tht_write(sc, THT_REG_VGLB, 0);

	tht_write(sc, THT_REG_RX_MAX_FRAME, MCLBYTES - ETHER_ALIGN);

	tht_write(sc, THT_REG_RDINTCM(0), THT_REG_RDINTCM_PKT_TH(12) |
	    THT_REG_RDINTCM_RXF_TH(4) | THT_REG_RDINTCM_COAL_RC |
	    THT_REG_RDINTCM_COAL(0x20));
	tht_write(sc, THT_REG_TDINTCM(0), THT_REG_TDINTCM_PKT_TH(12) |
	    THT_REG_TDINTCM_COAL_RC | THT_REG_TDINTCM_COAL(0x20));

	bcopy(sc->sc_ac.ac_enaddr, sc->sc_lladdr, ETHER_ADDR_LEN);
	tht_lladdr_write(sc);

	/* populate rxf fifo */
	tht_rxf_fill(sc, 1);

	/* program promiscuous mode and multicast filters */
	tht_iff(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	
	/* enable interrupts */
	sc->sc_imr = THT_IMR_UP(sc->sc_port);
	tht_write(sc, THT_REG_IMR, sc->sc_imr);

	return;

free_rxd:
	tht_fifo_free(sc, &sc->sc_rxd);
free_rxf:
	tht_fifo_free(sc, &sc->sc_rxf);
free_txt:
	tht_fifo_free(sc, &sc->sc_txt);

	tht_sw_reset(sc);

free_rx_list:
	tht_pkt_free(sc, &sc->sc_rx_list);
free_tx_list:
	tht_pkt_free(sc, &sc->sc_tx_list);
}

void
tht_iff(struct tht_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	struct ether_multi		*enm;
	struct ether_multistep		step;
	u_int32_t			rxf;
	u_int8_t			imf[THT_REG_RX_MCST_HASH_SIZE];
	u_int8_t			hash;
	int				i;

	ifp->if_flags &= ~IFF_ALLMULTI;

	rxf = THT_REG_RX_FLT_OSEN | THT_REG_RX_FLT_AM | THT_REG_RX_FLT_AB;
	for (i = 0; i < THT_REG_RX_MAC_MCST_CNT; i++) {
		tht_write(sc, THT_REG_RX_MAC_MCST0(i), 0);
		tht_write(sc, THT_REG_RX_MAC_MCST1(i), 0);
	}
	memset(imf, 0x00, sizeof(imf));

	if (ifp->if_flags & IFF_PROMISC) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxf |= THT_REG_RX_FLT_PRM_ALL;
	} else if (sc->sc_ac.ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		memset(imf, 0xff, sizeof(imf));
	} else {
		ETHER_FIRST_MULTI(step, &sc->sc_ac, enm);

#if 0
		/* fill the perfect multicast filters */
		for (i = 0; i < THT_REG_RX_MAC_MCST_CNT; i++) {
			if (enm == NULL)
				break;

			tht_write(sc, THT_REG_RX_MAC_MCST0(i),
			    (enm->enm_addrlo[0] << 0) |
			    (enm->enm_addrlo[1] << 8) |
			    (enm->enm_addrlo[2] << 16) |
			    (enm->enm_addrlo[3] << 24));
			tht_write(sc, THT_REG_RX_MAC_MCST1(i),
			    (enm->enm_addrlo[4] << 0) |
			    (enm->enm_addrlo[5] << 8));

			ETHER_NEXT_MULTI(step, enm);
		}
#endif

		/* fill the imperfect multicast filter with what's left */
		while (enm != NULL) {
			hash = 0x00;
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				hash ^= enm->enm_addrlo[i];
			setbit(imf, hash);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	tht_write_region(sc, THT_REG_RX_MCST_HASH, imf, sizeof(imf));
	tht_write(sc, THT_REG_RX_FLT, rxf);
}

void
tht_down(struct tht_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		return;
	}

	ifp->if_flags &= ~(IFF_RUNNING | IFF_ALLMULTI);
	ifq_clr_oactive(&ifp->if_snd);

	while (tht_fifo_writable(sc, &sc->sc_txt) < sc->sc_txt.tf_len &&
	    tht_fifo_readable(sc, &sc->sc_txf) > 0)
		tsleep_nsec(sc, 0, "thtdown", SEC_TO_NSEC(1));

	sc->sc_imr = THT_IMR_DOWN(sc->sc_port);
	tht_write(sc, THT_REG_IMR, sc->sc_imr);

	tht_sw_reset(sc);

	tht_fifo_free(sc, &sc->sc_txf);
	tht_fifo_free(sc, &sc->sc_rxd);
	tht_fifo_free(sc, &sc->sc_rxf);
	tht_fifo_free(sc, &sc->sc_txt);

	/* free mbufs that were on the rxf fifo */
	tht_rxf_drain(sc);

	tht_pkt_free(sc, &sc->sc_rx_list);
	tht_pkt_free(sc, &sc->sc_tx_list);
}

void
tht_start(struct ifnet *ifp)
{
	struct tht_softc		*sc = ifp->if_softc;
	struct tht_pkt			*pkt;
	struct tht_tx_task		txt;
	u_int32_t			flags;
	struct mbuf			*m;
	int				bc;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	if (ifq_is_oactive(&ifp->if_snd))
		return;
	if (ifq_empty(&ifp->if_snd))
		return;

	if (tht_fifo_writable(sc, &sc->sc_txt) <= THT_FIFO_DESC_LEN)
		return;

	bzero(&txt, sizeof(txt));

	tht_fifo_pre(sc, &sc->sc_txt);

	do {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		pkt = tht_pkt_get(&sc->sc_tx_list);
		if (pkt == NULL) {
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		ifq_deq_commit(&ifp->if_snd, m);
		if (tht_load_pkt(sc, pkt, m) != 0) {
			m_freem(m);
			tht_pkt_put(&sc->sc_tx_list, pkt);
			ifp->if_oerrors++;
			break;
		}
		/* thou shalt not use m after this point, only pkt->tp_m */

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, pkt->tp_m, BPF_DIRECTION_OUT);
#endif

		bc = sizeof(txt) +
		    sizeof(struct tht_pbd) * pkt->tp_dmap->dm_nsegs;

		flags = THT_TXT_TYPE | LWORDS(bc);
		txt.flags = htole32(flags);
		txt.len = htole16(pkt->tp_m->m_pkthdr.len);
		txt.uid = pkt->tp_id;

		DPRINTF(THT_D_TX, "%s: txt uid 0x%llx flags 0x%08x len %d\n",
		    DEVNAME(sc), pkt->tp_id, flags, pkt->tp_m->m_pkthdr.len);

		tht_fifo_write(sc, &sc->sc_txt, &txt, sizeof(txt));
		tht_fifo_write_dmap(sc, &sc->sc_txt, pkt->tp_dmap);
		tht_fifo_write_pad(sc, &sc->sc_txt, bc);

		bus_dmamap_sync(sc->sc_thtc->sc_dmat, pkt->tp_dmap, 0,
		    pkt->tp_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	} while (sc->sc_txt.tf_ready > THT_FIFO_DESC_LEN);

	tht_fifo_post(sc, &sc->sc_txt);
}

int
tht_load_pkt(struct tht_softc *sc, struct tht_pkt *pkt, struct mbuf *m)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	bus_dmamap_t			dmap = pkt->tp_dmap;
	struct mbuf			*m0 = NULL;

	switch(bus_dmamap_load_mbuf(dmat, dmap, m, BUS_DMA_NOWAIT)) {
	case 0:
		pkt->tp_m = m;
		break;

	case EFBIG: /* mbuf chain is too fragmented */
		MGETHDR(m0, M_DONTWAIT, MT_DATA);
		if (m0 == NULL)
			return (ENOBUFS);
		if (m->m_pkthdr.len > MHLEN) {
			MCLGET(m0, M_DONTWAIT);
			if (!(m0->m_flags & M_EXT)) {
				m_freem(m0);
				return (ENOBUFS);
			}
		}
		m_copydata(m, 0, m->m_pkthdr.len, mtod(m0, caddr_t));
		m0->m_pkthdr.len = m0->m_len = m->m_pkthdr.len;
		if (bus_dmamap_load_mbuf(dmat, dmap, m0, BUS_DMA_NOWAIT)) {
                        m_freem(m0);
			return (ENOBUFS);
                }

		m_freem(m);
		pkt->tp_m = m0;
		break;

	default:
		return (ENOBUFS);
	}

	return (0);
}

void
tht_txf(struct tht_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	bus_dmamap_t			dmap;
	struct tht_tx_free		txf;
	struct tht_pkt			*pkt;

	if (tht_fifo_readable(sc, &sc->sc_txf) < sizeof(txf))
		return;

	tht_fifo_pre(sc, &sc->sc_txf);

	do {
		tht_fifo_read(sc, &sc->sc_txf, &txf, sizeof(txf));

		DPRINTF(THT_D_TX, "%s: txf uid 0x%llx\n", DEVNAME(sc), txf.uid);

		pkt = &sc->sc_tx_list.tpl_pkts[txf.uid];
		dmap = pkt->tp_dmap;

		bus_dmamap_sync(dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dmat, dmap);

		m_freem(pkt->tp_m);

		tht_pkt_put(&sc->sc_tx_list, pkt);

	} while (sc->sc_txf.tf_ready >= sizeof(txf));

	ifq_clr_oactive(&ifp->if_snd);

	tht_fifo_post(sc, &sc->sc_txf);
}

void
tht_rxf_fill(struct tht_softc *sc, int wait)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	bus_dmamap_t			dmap;
	struct tht_rx_free		rxf;
	struct tht_pkt			*pkt;
	struct mbuf			*m;
	int				bc;

	if (tht_fifo_writable(sc, &sc->sc_rxf) <= THT_FIFO_DESC_LEN)
		return;

	tht_fifo_pre(sc, &sc->sc_rxf);

	for (;;) {
		if ((pkt = tht_pkt_get(&sc->sc_rx_list)) == NULL)
			goto done;

		MGETHDR(m, wait ? M_WAIT : M_DONTWAIT, MT_DATA);
		if (m == NULL)
			goto put_pkt;

		MCLGET(m, wait ? M_WAIT : M_DONTWAIT);
		if (!ISSET(m->m_flags, M_EXT))
			goto free_m;

		m->m_data += ETHER_ALIGN;
		m->m_len = m->m_pkthdr.len = MCLBYTES - ETHER_ALIGN;

		dmap = pkt->tp_dmap;
		if (bus_dmamap_load_mbuf(dmat, dmap, m,
		    wait ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT) != 0)
			goto free_m;

		pkt->tp_m = m;

		bc = sizeof(rxf) + sizeof(struct tht_pbd) * dmap->dm_nsegs;

		rxf.bc = htole16(LWORDS(bc));
		rxf.type = htole16(THT_RXF_TYPE);
		rxf.uid = pkt->tp_id;

		tht_fifo_write(sc, &sc->sc_rxf, &rxf, sizeof(rxf));
		tht_fifo_write_dmap(sc, &sc->sc_rxf, dmap);
		tht_fifo_write_pad(sc, &sc->sc_rxf, bc);

		bus_dmamap_sync(dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_PREREAD);

		if (sc->sc_rxf.tf_ready <= THT_FIFO_DESC_LEN)
			goto done;
	}

free_m:
	m_freem(m);
put_pkt:
	tht_pkt_put(&sc->sc_rx_list, pkt);
done:
	tht_fifo_post(sc, &sc->sc_rxf);
}

void
tht_rxf_drain(struct tht_softc *sc)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	bus_dmamap_t			dmap;
	struct tht_pkt			*pkt;

	while ((pkt = tht_pkt_used(&sc->sc_rx_list)) != NULL) {
		dmap = pkt->tp_dmap;

		bus_dmamap_sync(dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(dmat, dmap);

		m_freem(pkt->tp_m);

		tht_pkt_put(&sc->sc_rx_list, pkt);
	}
}

void
tht_rxd(struct tht_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	bus_dmamap_t			dmap;
	struct tht_rx_desc		rxd;
	struct tht_pkt			*pkt;
	struct mbuf			*m;
	struct mbuf_list		ml = MBUF_LIST_INITIALIZER();
	int				bc;
	u_int32_t			flags;

	if (tht_fifo_readable(sc, &sc->sc_rxd) < sizeof(rxd))
		return;

	tht_fifo_pre(sc, &sc->sc_rxd);

	do {
		tht_fifo_read(sc, &sc->sc_rxd, &rxd, sizeof(rxd));

		flags = letoh32(rxd.flags);
		bc = THT_RXD_FLAGS_BC(flags) * 8;
		bc -= sizeof(rxd);
		pkt = &sc->sc_rx_list.tpl_pkts[rxd.uid];

		dmap = pkt->tp_dmap;

		bus_dmamap_sync(dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(dmat, dmap);

		m = pkt->tp_m;
		m->m_pkthdr.len = m->m_len = letoh16(rxd.len);

		/* XXX process type 3 rx descriptors */

		ml_enqueue(&ml, m);

		tht_pkt_put(&sc->sc_rx_list, pkt);

		while (bc > 0) {
			static u_int32_t pad;

			tht_fifo_read(sc, &sc->sc_rxd, &pad, sizeof(pad));
			bc -= sizeof(pad);
		}
	} while (sc->sc_rxd.tf_ready >= sizeof(rxd));

	tht_fifo_post(sc, &sc->sc_rxd);

	if_input(ifp, &ml);

	/* put more pkts on the fifo */
	tht_rxf_fill(sc, 0);
}

void
tht_watchdog(struct ifnet *ifp)
{
	/* do nothing */
}

int
tht_media_change(struct ifnet *ifp)
{
	/* ignore */
	return (0);
}

void
tht_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct tht_softc		*sc = ifp->if_softc;

	imr->ifm_active = IFM_ETHER | IFM_AUTO;
	imr->ifm_status = IFM_AVALID;

	tht_link_state(sc);

	if (LINK_STATE_IS_UP(ifp->if_link_state))
		imr->ifm_status |= IFM_ACTIVE;
}

int
tht_fifo_alloc(struct tht_softc *sc, struct tht_fifo *tf,
    struct tht_fifo_desc *tfd)
{
	u_int64_t			dva;

	tf->tf_len = THT_FIFO_SIZE(tfd->tfd_size);
	tf->tf_mem = tht_dmamem_alloc(sc, tf->tf_len, THT_FIFO_ALIGN);
	if (tf->tf_mem == NULL)
		return (1);

	tf->tf_desc = tfd;
	tf->tf_rptr = tf->tf_wptr = 0;

	bus_dmamap_sync(sc->sc_thtc->sc_dmat, THT_DMA_MAP(tf->tf_mem),
	    0, tf->tf_len, THT_FIFO_PRE_SYNC(tfd));

	dva = THT_DMA_DVA(tf->tf_mem);
	tht_write(sc, tfd->tfd_cfg0, (u_int32_t)dva | tfd->tfd_size);
	tht_write(sc, tfd->tfd_cfg1, (u_int32_t)(dva >> 32));

	return (0);
}

void
tht_fifo_free(struct tht_softc *sc, struct tht_fifo *tf)
{
	bus_dmamap_sync(sc->sc_thtc->sc_dmat, THT_DMA_MAP(tf->tf_mem),
	    0, tf->tf_len, THT_FIFO_POST_SYNC(tf->tf_desc));
	tht_dmamem_free(sc, tf->tf_mem);
}

size_t
tht_fifo_readable(struct tht_softc *sc, struct tht_fifo *tf)
{
	tf->tf_wptr = tht_read(sc, tf->tf_desc->tfd_wptr);
	tf->tf_wptr &= THT_FIFO_PTR_MASK;
	tf->tf_ready = tf->tf_wptr - tf->tf_rptr;
	if (tf->tf_ready < 0)
		tf->tf_ready += tf->tf_len;

	DPRINTF(THT_D_FIFO, "%s: fifo rdable wptr: %d rptr: %d ready: %d\n",
	    DEVNAME(sc), tf->tf_wptr, tf->tf_rptr, tf->tf_ready);

	return (tf->tf_ready);
}

size_t
tht_fifo_writable(struct tht_softc *sc, struct tht_fifo *tf)
{
	tf->tf_rptr = tht_read(sc, tf->tf_desc->tfd_rptr);
	tf->tf_rptr &= THT_FIFO_PTR_MASK;
	tf->tf_ready = tf->tf_rptr - tf->tf_wptr;
	if (tf->tf_ready <= 0)
		tf->tf_ready += tf->tf_len;

	DPRINTF(THT_D_FIFO, "%s: fifo wrable wptr: %d rptr: %d ready: %d\n",
	    DEVNAME(sc), tf->tf_wptr, tf->tf_rptr, tf->tf_ready);

	return (tf->tf_ready);
}

void
tht_fifo_pre(struct tht_softc *sc, struct tht_fifo *tf)
{
	bus_dmamap_sync(sc->sc_thtc->sc_dmat, THT_DMA_MAP(tf->tf_mem),
	    0, tf->tf_len, THT_FIFO_POST_SYNC(tf->tf_desc));
}

void
tht_fifo_read(struct tht_softc *sc, struct tht_fifo *tf,
    void *buf, size_t buflen)
{
	u_int8_t			*fifo = THT_DMA_KVA(tf->tf_mem);
	u_int8_t			*desc = buf;
	size_t				len;

	tf->tf_ready -= buflen;

	len = tf->tf_len - tf->tf_rptr;

	if (len < buflen) {
		memcpy(desc, fifo + tf->tf_rptr, len);

		buflen -= len;
		desc += len;

		tf->tf_rptr = 0;
	}

	memcpy(desc, fifo + tf->tf_rptr, buflen);
	tf->tf_rptr += buflen;

	DPRINTF(THT_D_FIFO, "%s: fifo rd wptr: %d rptr: %d ready: %d\n",
	    DEVNAME(sc), tf->tf_wptr, tf->tf_rptr, tf->tf_ready);
}

void
tht_fifo_write(struct tht_softc *sc, struct tht_fifo *tf,
    void *buf, size_t buflen)
{
	u_int8_t			*fifo = THT_DMA_KVA(tf->tf_mem);
	u_int8_t			*desc = buf;
	size_t				len;

	tf->tf_ready -= buflen;

	len = tf->tf_len - tf->tf_wptr;

	if (len < buflen) {
		memcpy(fifo + tf->tf_wptr, desc, len);

		buflen -= len;
		desc += len;

		tf->tf_wptr = 0;
	}

	memcpy(fifo + tf->tf_wptr, desc, buflen);
	tf->tf_wptr += buflen;
	tf->tf_wptr %= tf->tf_len;

	DPRINTF(THT_D_FIFO, "%s: fifo wr wptr: %d rptr: %d ready: %d\n",
	    DEVNAME(sc), tf->tf_wptr, tf->tf_rptr, tf->tf_ready);
}

void
tht_fifo_write_dmap(struct tht_softc *sc, struct tht_fifo *tf,
    bus_dmamap_t dmap)
{
	struct tht_pbd			pbd;
	u_int64_t			dva;
	int				i;

	for (i = 0; i < dmap->dm_nsegs; i++) {
		dva = dmap->dm_segs[i].ds_addr;

		pbd.addr_lo = htole32(dva);
		pbd.addr_hi = htole32(dva >> 32);
		pbd.len = htole32(dmap->dm_segs[i].ds_len);

		tht_fifo_write(sc, tf, &pbd, sizeof(pbd));
	}
}

void
tht_fifo_write_pad(struct tht_softc *sc, struct tht_fifo *tf, int bc)
{
	static const u_int32_t pad = 0x0;

	/* this assumes you'll only ever be writing multiples of 4 bytes */
	if (bc % 8)
		tht_fifo_write(sc, tf, (void *)&pad, sizeof(pad));
}

void
tht_fifo_post(struct tht_softc *sc, struct tht_fifo *tf)
{
	bus_dmamap_sync(sc->sc_thtc->sc_dmat, THT_DMA_MAP(tf->tf_mem),
	    0, tf->tf_len, THT_FIFO_PRE_SYNC(tf->tf_desc));
	if (tf->tf_desc->tfd_write)
		tht_write(sc, tf->tf_desc->tfd_wptr, tf->tf_wptr);
	else
		tht_write(sc, tf->tf_desc->tfd_rptr, tf->tf_rptr);

	DPRINTF(THT_D_FIFO, "%s: fifo post wptr: %d rptr: %d\n", DEVNAME(sc),
	    tf->tf_wptr, tf->tf_rptr);
}

static const bus_size_t tht_mac_regs[3] = {
    THT_REG_RX_UNC_MAC2, THT_REG_RX_UNC_MAC1, THT_REG_RX_UNC_MAC0
};

void
tht_lladdr_read(struct tht_softc *sc)
{
	int				i;

	for (i = 0; i < nitems(tht_mac_regs); i++)
		sc->sc_lladdr[i] = betoh16(tht_read(sc, tht_mac_regs[i]));
}

void
tht_lladdr_write(struct tht_softc *sc)
{
	int				i;

	for (i = 0; i < nitems(tht_mac_regs); i++)
		tht_write(sc, tht_mac_regs[i], htobe16(sc->sc_lladdr[i]));
}

#define tht_swrst_set(_s, _r) tht_write((_s), (_r), 0x1)
#define tht_swrst_clr(_s, _r) tht_write((_s), (_r), 0x0)
int
tht_sw_reset(struct tht_softc *sc)
{
	int				i;

	/* this follows SW Reset process in 8.8 of the doco */

	/* 1. disable rx */
	tht_clr(sc, THT_REG_RX_FLT, THT_REG_RX_FLT_OSEN);

	/* 2. initiate port disable */
	tht_swrst_set(sc, THT_REG_DIS_PRT);

	/* 3. initiate queue disable */
	tht_swrst_set(sc, THT_REG_DIS_QU_0);
	tht_swrst_set(sc, THT_REG_DIS_QU_1);

	/* 4. wait for successful finish of previous tasks */
	if (!tht_wait_set(sc, THT_REG_RST_PRT, THT_REG_RST_PRT_ACTIVE, 1000))
		return (1);

	/* 5. Reset interrupt registers */
	tht_write(sc, THT_REG_IMR, 0x0); /* 5.a */
	tht_read(sc, THT_REG_ISR); /* 5.b */
	for (i = 0; i < THT_NQUEUES; i++) {
		tht_write(sc, THT_REG_RDINTCM(i), 0x0); /* 5.c/5.d */
		tht_write(sc, THT_REG_TDINTCM(i), 0x0); /* 5.e */
	}

	/* 6. initiate queue reset */
	tht_swrst_set(sc, THT_REG_RST_QU_0);
	tht_swrst_set(sc, THT_REG_RST_QU_1);

	/* 7. initiate port reset */
	tht_swrst_set(sc, THT_REG_RST_PRT);

	/* 8. clear txt/rxf/rxd/txf read and write ptrs */
	for (i = 0; i < THT_NQUEUES; i++) {
		tht_write(sc, THT_REG_TXT_RPTR(i), 0);
		tht_write(sc, THT_REG_RXF_RPTR(i), 0);
		tht_write(sc, THT_REG_RXD_RPTR(i), 0);
		tht_write(sc, THT_REG_TXF_RPTR(i), 0);

		tht_write(sc, THT_REG_TXT_WPTR(i), 0);
		tht_write(sc, THT_REG_RXF_WPTR(i), 0);
		tht_write(sc, THT_REG_RXD_WPTR(i), 0);
		tht_write(sc, THT_REG_TXF_WPTR(i), 0);
	}

	/* 9. unset port disable */
	tht_swrst_clr(sc, THT_REG_DIS_PRT);

	/* 10. unset queue disable */
	tht_swrst_clr(sc, THT_REG_DIS_QU_0);
	tht_swrst_clr(sc, THT_REG_DIS_QU_1);

	/* 11. unset queue reset */
	tht_swrst_clr(sc, THT_REG_RST_QU_0);
	tht_swrst_clr(sc, THT_REG_RST_QU_1);

	/* 12. unset port reset */
	tht_swrst_clr(sc, THT_REG_RST_PRT);

	/* 13. enable rx */
	tht_set(sc, THT_REG_RX_FLT, THT_REG_RX_FLT_OSEN);

	return (0);
}

int
tht_fw_load(struct tht_softc *sc)
{
	u_int8_t			*fw, *buf;
	size_t				fwlen, wrlen;
	int				error = 1, msecs, ret;

	if (loadfirmware("tht", &fw, &fwlen) != 0)
		return (1);

	if ((fwlen % 8) != 0)
		goto err;

	buf = fw;
	while (fwlen > 0) {
		while (tht_fifo_writable(sc, &sc->sc_txt) <= THT_FIFO_GAP) {
			ret = tsleep_nsec(sc, PCATCH, "thtfw",
			    MSEC_TO_NSEC(10));
			if (ret == EINTR)
				goto err;
		}

		wrlen = MIN(sc->sc_txt.tf_ready - THT_FIFO_GAP, fwlen);
		tht_fifo_pre(sc, &sc->sc_txt);
		tht_fifo_write(sc, &sc->sc_txt, buf, wrlen);
		tht_fifo_post(sc, &sc->sc_txt);

		fwlen -= wrlen;
		buf += wrlen;
	}

	for (msecs = 0; msecs < 2000; msecs += 10) {
		if (tht_read(sc, THT_REG_INIT_STATUS) != 0) {
			error = 0;
			break;
		}
		ret = tsleep_nsec(sc, PCATCH, "thtinit", MSEC_TO_NSEC(10));
		if (ret == EINTR)
			goto err;
	}

	tht_write(sc, THT_REG_INIT_SEMAPHORE, 0x1);

err:
	free(fw, M_DEVBUF, fwlen);
	return (error);
}

void
tht_link_state(struct tht_softc *sc)
{
	static const struct timeval	interval = { 0, 10000 };
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	int				link_state = LINK_STATE_DOWN;

	if (!ratecheck(&sc->sc_mediacheck, &interval))
		return;

	if (tht_read(sc, THT_REG_MAC_LNK_STAT) & THT_REG_MAC_LNK_STAT_LINK)
		link_state = LINK_STATE_FULL_DUPLEX;

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}

	if (LINK_STATE_IS_UP(ifp->if_link_state))
		ifp->if_baudrate = IF_Gbps(10);
	else
		ifp->if_baudrate = 0;
}

u_int32_t
tht_read(struct tht_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_thtc->sc_memt, sc->sc_memh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_thtc->sc_memt, sc->sc_memh, r));
}

void
tht_write(struct tht_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_thtc->sc_memt, sc->sc_memh, r, v);
	bus_space_barrier(sc->sc_thtc->sc_memt, sc->sc_memh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

void
tht_write_region(struct tht_softc *sc, bus_size_t r, void *buf, size_t len)
{
	bus_space_write_raw_region_4(sc->sc_thtc->sc_memt, sc->sc_memh, r,
	    buf, len);
	bus_space_barrier(sc->sc_thtc->sc_memt, sc->sc_memh, r, len,
	    BUS_SPACE_BARRIER_WRITE);
}

int
tht_wait_eq(struct tht_softc *sc, bus_size_t r, u_int32_t m, u_int32_t v,
    int timeout)
{
	while ((tht_read(sc, r) & m) != v) {
		if (timeout == 0)
			return (0);

		delay(1000);
		timeout--;
	}

	return (1);
}

int
tht_wait_ne(struct tht_softc *sc, bus_size_t r, u_int32_t m, u_int32_t v,
    int timeout)
{
	while ((tht_read(sc, r) & m) == v) {
		if (timeout == 0)
			return (0);

		delay(1000);
		timeout--;
	}

	return (1);
}

struct tht_dmamem *
tht_dmamem_alloc(struct tht_softc *sc, bus_size_t size, bus_size_t align)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	struct tht_dmamem		*tdm;
	int				nsegs;

	tdm = malloc(sizeof(struct tht_dmamem), M_DEVBUF, M_WAITOK | M_ZERO);
	tdm->tdm_size = size;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &tdm->tdm_map) != 0)
		goto tdmfree;

	if (bus_dmamem_alloc(dmat, size, align, 0, &tdm->tdm_seg, 1, &nsegs,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(dmat, &tdm->tdm_seg, nsegs, size, &tdm->tdm_kva,
	    BUS_DMA_WAITOK) != 0)
		goto free;

	if (bus_dmamap_load(dmat, tdm->tdm_map, tdm->tdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (tdm);

unmap:
	bus_dmamem_unmap(dmat, tdm->tdm_kva, size);
free:
	bus_dmamem_free(dmat, &tdm->tdm_seg, 1);
destroy:
	bus_dmamap_destroy(dmat, tdm->tdm_map);
tdmfree:
	free(tdm, M_DEVBUF, 0);

	return (NULL);
}

void
tht_dmamem_free(struct tht_softc *sc, struct tht_dmamem *tdm)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;

	bus_dmamap_unload(dmat, tdm->tdm_map);
	bus_dmamem_unmap(dmat, tdm->tdm_kva, tdm->tdm_size);
	bus_dmamem_free(dmat, &tdm->tdm_seg, 1);
	bus_dmamap_destroy(dmat, tdm->tdm_map);
	free(tdm, M_DEVBUF, 0);
}

int
tht_pkt_alloc(struct tht_softc *sc, struct tht_pkt_list *tpl, int npkts,
    int nsegs)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	struct tht_pkt			*pkt;
	int				i;

	tpl->tpl_pkts = mallocarray(npkts, sizeof(struct tht_pkt),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	TAILQ_INIT(&tpl->tpl_free);
	TAILQ_INIT(&tpl->tpl_used);
	for (i = 0; i < npkts; i++) {
		pkt = &tpl->tpl_pkts[i];

		pkt->tp_id = i;
		if (bus_dmamap_create(dmat, THT_PBD_PKTLEN, nsegs,
		    THT_PBD_PKTLEN, 0, BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW,
		    &pkt->tp_dmap) != 0) {
			tht_pkt_free(sc, tpl);
			return (1);
		}

		TAILQ_INSERT_TAIL(&tpl->tpl_free, pkt, tp_link);
	}

	return (0);
}

void
tht_pkt_free(struct tht_softc *sc, struct tht_pkt_list *tpl)
{
	bus_dma_tag_t			dmat = sc->sc_thtc->sc_dmat;
	struct tht_pkt			*pkt;

	while ((pkt = tht_pkt_get(tpl)) != NULL)
		bus_dmamap_destroy(dmat, pkt->tp_dmap);
	free(tpl->tpl_pkts, M_DEVBUF, 0);
	tpl->tpl_pkts = NULL;
}

void
tht_pkt_put(struct tht_pkt_list *tpl, struct tht_pkt *pkt)
{
	TAILQ_REMOVE(&tpl->tpl_used, pkt, tp_link);
	TAILQ_INSERT_TAIL(&tpl->tpl_free, pkt, tp_link);
}

struct tht_pkt *
tht_pkt_get(struct tht_pkt_list *tpl)
{
	struct tht_pkt			*pkt;

	pkt = TAILQ_FIRST(&tpl->tpl_free);
	if (pkt != NULL) {
		TAILQ_REMOVE(&tpl->tpl_free, pkt, tp_link);
		TAILQ_INSERT_TAIL(&tpl->tpl_used, pkt, tp_link);

	}

	return (pkt);
}

struct tht_pkt *
tht_pkt_used(struct tht_pkt_list *tpl)
{
	return (TAILQ_FIRST(&tpl->tpl_used));
}
