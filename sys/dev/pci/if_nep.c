/*	$OpenBSD: if_nep.c,v 1.35 2024/05/24 06:02:56 jsg Exp $	*/
/*
 * Copyright (c) 2014, 2015 Mark Kettenis
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/pool.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#endif

/*
 * The virtualization features make this a really complex device.  For
 * now we try to keep things simple and use one logical device per
 * port, using port numbers as logical device numbers.
 */

#define PIO		0x000000
#define FZC_PIO		0x080000
#define FZC_MAC		0x180000
#define FZC_IPP		0x280000
#define FFLP		0x300000
#define FZC_FFLP	0x380000
#define ZCP		0x500000
#define FZC_ZCP		0x580000
#define DMC		0x600000
#define FZC_DMC		0x680000
#define TXC		0x700000
#define FZC_TXC		0x780000
#define PIO_LDSV	0x800000
#define PIO_IMASK0	0xa00000
#define PIO_IMASK1	0xb00000

#define RST_CTL			(FZC_PIO + 0x00038)
#define SYS_ERR_MASK		(FZC_PIO + 0x00090)
#define SYS_ERR_STAT		(FZC_PIO + 0x00098)

#define LDN_RXDMA(chan)		(0 + (chan))
#define LDN_TXDMA(chan)		(32 + (chan))
#define LDN_MIF			63
#define LDN_MAC(port)		(64 + (port))
#define LDN_SYSERR		68

#define LDSV0(ldg)		(PIO_LDSV + 0x00000 + (ldg) * 0x02000)
#define LDSV1(ldg)		(PIO_LDSV + 0x00008 + (ldg) * 0x02000)
#define LDSV2(ldg)		(PIO_LDSV + 0x00010 + (ldg) * 0x02000)
#define LDGIMGN(ldg)		(PIO_LDSV + 0x00018 + (ldg) * 0x02000)
#define  LDGIMGN_ARM		(1ULL << 31)
#define  LDGIMGN_TIMER		(63ULL << 0)

#define LD_IM0(idx)		(PIO_IMASK0 + 0x00000 + (idx) * 0x02000)
#define  LD_IM0_LDF_MASK	(3ULL << 0)
#define LD_IM1(idx)		(PIO_IMASK1 + 0x00000 + (idx - 64) * 0x02000)
#define  LD_IM1_LDF_MASK	(3ULL << 0)

#define SID(ldg)		(FZC_PIO + 0x10200 + (ldg) * 0x00008)
#define LDG_NUM(ldn)		(FZC_PIO + 0x20000 + (ldn) * 0x00008)

#define ipp_port(port)		(((port & 0x1) << 1) | (port & 0x2) >> 1)
#define IPP_CFIG(port)		(FZC_IPP + 0x00000 + ipp_port(port) * 0x04000)
#define  IPP_CFIG_SOFT_RST		(1ULL << 31)
#define  IPP_CFIG_DFIFO_PIO_W		(1ULL << 5)
#define  IPP_CFIG_IPP_ENABLE		(1ULL << 0)
#define IPP_INT_STAT(port)	(FZC_IPP + 0x00040 + ipp_port(port) * 0x04000)
#define IPP_MSK(port)		(FZC_IPP + 0x00048 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_RD1(port)	(FZC_IPP + 0x000c0 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_RD2(port)	(FZC_IPP + 0x000c8 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_RD3(port)	(FZC_IPP + 0x000d0 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_RD4(port)	(FZC_IPP + 0x000d8 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_RD5(port)	(FZC_IPP + 0x000e0 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_WR1(port)	(FZC_IPP + 0x000e8 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_WR2(port)	(FZC_IPP + 0x000f0 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_WR3(port)	(FZC_IPP + 0x000f8 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_WR4(port)	(FZC_IPP + 0x00100 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_WR5(port)	(FZC_IPP + 0x00108 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_RD_PTR(port)	(FZC_IPP + 0x00110 + ipp_port(port) * 0x04000)
#define IPP_DFIFO_WR_PTR(port)	(FZC_IPP + 0x00118 + ipp_port(port) * 0x04000)

#define IPP_NIU_DFIFO_ENTRIES		1024
#define	IPP_P0_P1_DFIFO_ENTRIES		2048
#define IPP_P2_P3_DFIFO_ENTRIES		1024

#define ZCP_CFIG		(FZC_ZCP + 0x00000)
#define ZCP_INT_STAT		(FZC_ZCP + 0x00008)
#define ZCP_INT_MASK		(FZC_ZCP + 0x00010)

#define TXC_DMA_MAX(chan)	(FZC_TXC + 0x00000 + (chan) * 0x01000)
#define TXC_CONTROL		(FZC_TXC + 0x20000)
#define  TXC_CONTROL_TXC_ENABLED	(1ULL << 4)
#define TXC_PORT_DMA(port)	(FZC_TXC + 0x20028 + (port) * 0x00100)
#define TXC_PKT_STUFFED(port)	(FZC_TXC + 0x20030 + (port) * 0x00100)
#define TXC_PKT_XMIT(port)	(FZC_TXC + 0x20038 + (port) * 0x00100)
#define TXC_INT_STAT_DBG	(FZC_TXC + 0x20420)
#define TXC_INT_STAT		(FZC_TXC + 0x20428)
#define TXC_INT_MASK		(FZC_TXC + 0x20430)
#define  TXC_INT_MASK_PORT_INT_MASK(port) (0x3fULL << ((port) * 8))

#define XTXMAC_SW_RST(port)	(FZC_MAC + 0x00000 + (port) * 0x06000)
#define  XTXMAC_SW_RST_REG_RST		(1ULL << 1)
#define  XTXMAC_SW_RST_SOFT_RST		(1ULL << 0)
#define XRXMAC_SW_RST(port)	(FZC_MAC + 0x00008 + (port) * 0x06000)
#define  XRXMAC_SW_RST_REG_RST		(1ULL << 1)
#define  XRXMAC_SW_RST_SOFT_RST		(1ULL << 0)
#define XTXMAC_STATUS(port)	(FZC_MAC + 0x00020 + (port) * 0x06000)
#define XRXMAC_STATUS(port)	(FZC_MAC + 0x00028 + (port) * 0x06000)
#define XTXMAC_STAT_MSK(port)	(FZC_MAC + 0x00040 + (port) * 0x06000)
#define XRXMAC_STAT_MSK(port)	(FZC_MAC + 0x00048 + (port) * 0x06000)
#define XMAC_CONFIG(port)	(FZC_MAC + 0x00060 + (port) * 0x06000)
#define  XMAC_CONFIG_SEL_CLK_25MHZ	(1ULL << 31)
#define  XMAC_CONFIG_1G_PCS_BYPASS	(1ULL << 30)
#define  XMAC_CONFIG_MODE_MASK		(3ULL << 27)
#define  XMAC_CONFIG_MODE_XGMII		(0ULL << 27)
#define  XMAC_CONFIG_MODE_GMII		(1ULL << 27)
#define  XMAC_CONFIG_MODE_MII		(2ULL << 27)
#define  XMAC_CONFIG_LFS_DISABLE	(1ULL << 26)
#define  XMAC_CONFIG_LOOPBACK		(1ULL << 25)
#define  XMAC_CONFIG_TX_OUTPUT_EN	(1ULL << 24)
#define  XMAC_CONFIG_SEL_POR_CLK_SRC	(1ULL << 23)
#define  XMAC_CONFIG_HASH_FILTER_EN	(1ULL << 15)
#define  XMAC_CONFIG_PROMISCUOUS_GROUP	(1ULL << 10)
#define  XMAC_CONFIG_PROMISCUOUS	(1ULL << 9)
#define  XMAC_CONFIG_RX_MAC_ENABLE	(1ULL << 8)
#define  XMAC_CONFIG_ALWAYS_NO_CRC	(1ULL << 3)
#define  XMAC_CONFIG_VAR_MIN_IPG_EN	(1ULL << 2)
#define  XMAC_CONFIG_STRETCH_MODE	(1ULL << 1)
#define  XMAC_CONFIG_TX_ENABLE		(1ULL << 0)

#define XMAC_IPG(port)		(FZC_MAC + 0x00080 + (port) * 0x06000)
#define  XMAC_IPG_IPG_VALUE1_MASK	(0xffULL << 8)
#define  XMAC_IPG_IPG_VALUE1_12		(10ULL << 8)
#define  XMAC_IPG_IPG_VALUE_MASK	(0x07ULL << 0)
#define  XMAC_IPG_IPG_VALUE_12_15	(3ULL << 0)

#define XMAC_MIN(port)		(FZC_MAC + 0x00088 + (port) * 0x06000)
#define  XMAC_MIN_RX_MIN_PKT_SIZE_MASK	(0x3ffULL << 20)
#define  XMAC_MIN_RX_MIN_PKT_SIZE_SHIFT	20
#define  XMAC_MIN_TX_MIN_PKT_SIZE_MASK	(0x3ffULL << 0)
#define  XMAC_MIN_TX_MIN_PKT_SIZE_SHIFT	0
#define XMAC_MAX(port)		(FZC_MAC + 0x00090 + (port) * 0x06000)

#define XMAC_ADDR0(port)	(FZC_MAC + 0x000a0 + (port) * 0x06000)
#define XMAC_ADDR1(port)	(FZC_MAC + 0x000a8 + (port) * 0x06000)
#define XMAC_ADDR2(port)	(FZC_MAC + 0x000b0 + (port) * 0x06000)

#define XMAC_ADDR_CMPEN(port)	(FZC_MAC + 0x00208 + (port) * 0x06000)

#define XMAC_ADD_FILT0(port)	(FZC_MAC + 0x00818 + (port) * 0x06000)
#define XMAC_ADD_FILT1(port)	(FZC_MAC + 0x00820 + (port) * 0x06000)
#define XMAC_ADD_FILT2(port)	(FZC_MAC + 0x00828 + (port) * 0x06000)
#define XMAC_ADD_FILT12_MASK(port) (FZC_MAC + 0x00830 + (port) * 0x06000)
#define XMAC_ADD_FILT00_MASK(port) (FZC_MAC + 0x00838 + (port) * 0x06000)

#define XMAC_HASH_TBL0(port)	(FZC_MAC + 0x00840 + (port) * 0x06000)
#define XMAC_HASH_TBL(port, i)	(XMAC_HASH_TBL0(port) + (i) * 0x00008)

#define XMAC_HOST_INFO0(port)	(FZC_MAC + 0x00900 + (port) * 0x06000)
#define XMAC_HOST_INFO(port, i)	(XMAC_HOST_INFO0(port) + (i) * 0x00008)

#define RXMAC_BT_CNT(port)	(FZC_MAC + 0x00100 + (port) * 0x06000)

#define TXMAC_FRM_CNT(port)	(FZC_MAC + 0x00170 + (port) * 0x06000)
#define TXMAC_BYTE_CNT(port)	(FZC_MAC + 0x00178 + (port) * 0x06000)

#define LINK_FAULT_CNT(port)	(FZC_MAC + 0x00180 + (port) * 0x06000)
#define XMAC_SM_REG(port)	(FZC_MAC + 0x001a8 + (port) * 0x06000)

#define TXMAC_SW_RST(port)	(FZC_MAC + 0x0c000 + ((port) - 2) * 0x04000)
#define  TXMAC_SW_RST_SW_RST		(1ULL << 0)
#define RXMAC_SW_RST(port)	(FZC_MAC + 0x0c008 + ((port) - 2) * 0x04000)
#define  RXMAC_SW_RST_SW_RST		(1ULL << 0)
#define TXMAC_CONFIG(port)	(FZC_MAC + 0x0c060 + ((port) - 2) * 0x04000)
#define  TXMAC_CONFIG_TX_ENABLE		(1ULL << 0)
#define RXMAC_CONFIG(port)	(FZC_MAC + 0x0c068 + ((port) - 2) * 0x04000)
#define  RXMAC_CONFIG_ERROR_CHK_DIS	(1ULL << 7)
#define  RXMAC_CONFIG_ADDR_FILTER_EN	(1ULL << 6)
#define  RXMAC_CONFIG_HASH_FILTER_EN	(1ULL << 5)
#define  RXMAC_CONFIG_PROMISCUOUS_GROUP	(1ULL << 4)
#define  RXMAC_CONFIG_PROMISCUOUS	(1ULL << 3)
#define  RXMAC_CONFIG_STRIP_FCS		(1ULL << 2)
#define  RXMAC_CONFIG_STRIP_PAD		(1ULL << 1)
#define  RXMAC_CONFIG_RX_ENABLE		(1ULL << 0)
#define MAC_XIF_CONFIG(port)	(FZC_MAC + 0x0c078 + ((port) - 2) * 0x04000)
#define  MAC_XIF_CONFIG_SEL_CLK_25MHZ	(1ULL << 7)
#define  MAC_XIF_CONFIG_GMII_MODE	(1ULL << 3)
#define  MAC_XIF_CONFIG_LOOPBACK	(1ULL << 1)
#define  MAC_XIF_CONFIG_TX_OUTPUT_EN	(1ULL << 0)
#define BMAC_MIN(port)		(FZC_MAC + 0x0c0a0 + ((port) - 2) * 0x04000)
#define BMAC_MAX(port)		(FZC_MAC + 0x0c0a8 + ((port) - 2) * 0x04000)
#define  BMAC_MAX_BURST_SHIFT		16
#define MAC_PA_SIZE(port)	(FZC_MAC + 0x0c0b0 + ((port) - 2) * 0x04000)
#define MAC_CTRL_TYPE(port)	(FZC_MAC + 0x0c0b8 + ((port) - 2) * 0x04000)
#define BMAC_ADDR0(port)	(FZC_MAC + 0x0c100 + ((port) - 2) * 0x04000)
#define BMAC_ADDR1(port)	(FZC_MAC + 0x0c108 + ((port) - 2) * 0x04000)
#define BMAC_ADDR2(port)	(FZC_MAC + 0x0c110 + ((port) - 2) * 0x04000)

#define MAC_ADDR_FILT0(port)	(FZC_MAC + 0x0c298 + ((port) - 2) * 0x04000)
#define MAC_ADDR_FILT1(port)	(FZC_MAC + 0x0c2a0 + ((port) - 2) * 0x04000)
#define MAC_ADDR_FILT2(port)	(FZC_MAC + 0x0c2a8 + ((port) - 2) * 0x04000)
#define MAC_ADDR_FILT12_MASK(port) (FZC_MAC + 0x0c2b0 + ((port) - 2) * 0x04000)
#define MAC_ADDR_FILT00_MASK(port) (FZC_MAC + 0x0c2b8 + ((port) - 2) * 0x04000)

#define MAC_HASH_TBL0(port)	(FZC_MAC + 0x0c2c0 + ((port) - 2) * 0x04000)
#define MAC_HASH_TBL(port, i)	(MAC_HASH_TBL0(port) + (i) * 0x00008)

#define RXMAC_FRM_CNT(port)	(FZC_MAC + 0x0c370 + ((port) - 2) * 0x04000)
#define BMAC_ALTAD_CMPEN(port)	(FZC_MAC + 0x0c3f8 + ((port) - 2) * 0x04000)

#define BMAC_HOST_INFO0(port)	(FZC_MAC + 0x0c400 + ((port) - 2) * 0x04000)
#define BMAC_HOST_INFO(port, i)	(BMAC_HOST_INFO0(port) + (i) * 0x00008)

#define PCS_PORT_OFFSET(port)	((port < 2) ? ((port) * 0x06000) : \
					(0x02000 + (port) * 0x4000))
#define PCS_MII_CTL(port)	(FZC_MAC + 0x04000 + PCS_PORT_OFFSET(port))
#define  PCS_MII_CTL_RESET		(1ULL << 15)
#define PCS_DPATH_MODE(port)	(FZC_MAC + 0x040a0 + PCS_PORT_OFFSET(port))
#define  PCS_DPATH_MODE_MII		(1ULL << 1)

#define MIF_FRAME_OUTPUT	(FZC_MAC + 0x16018)
#define  MIF_FRAME_DATA			0xffff
#define  MIF_FRAME_TA0			(1ULL << 16)
#define  MIF_FRAME_TA1			(1ULL << 17)
#define  MIF_FRAME_REG_SHIFT		18
#define  MIF_FRAME_PHY_SHIFT		23
#define  MIF_FRAME_READ			0x60020000
#define  MIF_FRAME_WRITE		0x50020000
#define MIF_CONFIG		(FZC_MAC + 0x16020)
#define  MIF_CONFIG_INDIRECT_MODE	(1ULL << 15)

#define DEF_PT0_RDC		(FZC_DMC + 0x00008)
#define DEF_PT_RDC(port)	(DEF_PT0_RDC + (port) * 0x00008)
#define RDC_TBL(tbl, i)		(FZC_ZCP + 0x10000 + (tbl * 16 + i) * 0x00008)

#define RX_LOG_PAGE_VLD(chan)	(FZC_DMC + 0x20000 + (chan) * 0x00040)
#define  RX_LOG_PAGE_VLD_PAGE0		(1ULL << 0)
#define  RX_LOG_PAGE_VLD_PAGE1		(1ULL << 1)
#define  RX_LOG_PAGE_VLD_FUNC_SHIFT	2
#define RX_LOG_MASK1(chan)	(FZC_DMC + 0x20008 + (chan) * 0x00040)
#define RX_LOG_VALUE1(chan)	(FZC_DMC + 0x20010 + (chan) * 0x00040)
#define RX_LOG_MASK2(chan)	(FZC_DMC + 0x20018 + (chan) * 0x00040)
#define RX_LOG_VALUE2(chan)	(FZC_DMC + 0x20020 + (chan) * 0x00040)
#define RX_LOG_PAGE_RELO1(chan)	(FZC_DMC + 0x20028 + (chan) * 0x00040)
#define RX_LOG_PAGE_RELO2(chan)	(FZC_DMC + 0x20030 + (chan) * 0x00040)
#define RX_LOG_PAGE_HDL(chan)	(FZC_DMC + 0x20038 + (chan) * 0x00040)

#define RXDMA_CFIG1(chan)	(DMC + 0x00000 + (chan) * 0x00200)
#define  RXDMA_CFIG1_EN			(1ULL << 31)
#define  RXDMA_CFIG1_RST		(1ULL << 30)
#define  RXDMA_CFIG1_QST		(1ULL << 29)
#define RXDMA_CFIG2(chan)	(DMC + 0x00008 + (chan) * 0x00200)
#define  RXDMA_CFIG2_OFFSET_MASK	(3ULL << 2)
#define  RXDMA_CFIG2_OFFSET_0		(0ULL << 2)
#define  RXDMA_CFIG2_OFFSET_64		(1ULL << 2)
#define  RXDMA_CFIG2_OFFSET_128		(2ULL << 2)
#define  RXDMA_CFIG2_FULL_HDR		(1ULL << 0)

#define RBR_CFIG_A(chan)	(DMC + 0x00010 + (chan) * 0x00200)
#define  RBR_CFIG_A_LEN_SHIFT		48
#define RBR_CFIG_B(chan)	(DMC + 0x00018 + (chan) * 0x00200)
#define  RBR_CFIG_B_BLKSIZE_MASK	(3ULL << 24)
#define  RBR_CFIG_B_BLKSIZE_4K		(0ULL << 24)
#define  RBR_CFIG_B_BLKSIZE_8K		(1ULL << 24)
#define  RBR_CFIG_B_BLKSIZE_16K		(2ULL << 24)
#define  RBR_CFIG_B_BLKSIZE_32K		(3ULL << 24)
#define  RBR_CFIG_B_VLD2		(1ULL << 23)
#define  RBR_CFIG_B_BUFSZ2_MASK		(3ULL << 16)
#define  RBR_CFIG_B_BUFSZ2_2K		(0ULL << 16)
#define  RBR_CFIG_B_BUFSZ2_4K		(1ULL << 16)
#define  RBR_CFIG_B_BUFSZ2_8K		(2ULL << 16)
#define  RBR_CFIG_B_BUFSZ2_16K		(3ULL << 16)
#define  RBR_CFIG_B_VLD1		(1ULL << 15)
#define  RBR_CFIG_B_BUFSZ1_MASK		(3ULL << 8)
#define  RBR_CFIG_B_BUFSZ1_1K		(0ULL << 8)
#define  RBR_CFIG_B_BUFSZ1_2K		(1ULL << 8)
#define  RBR_CFIG_B_BUFSZ1_4K		(2ULL << 8)
#define  RBR_CFIG_B_BUFSZ1_8K		(3ULL << 8)
#define  RBR_CFIG_B_VLD0		(1ULL << 7)
#define  RBR_CFIG_B_BUFSZ0_MASK		(3ULL << 0)
#define  RBR_CFIG_B_BUFSZ0_256		(0ULL << 0)
#define  RBR_CFIG_B_BUFSZ0_512		(1ULL << 0)
#define  RBR_CFIG_B_BUFSZ0_1K		(2ULL << 0)
#define  RBR_CFIG_B_BUFSZ0_2K		(3ULL << 0)
#define RBR_KICK(chan)		(DMC + 0x00020 + (chan) * 0x00200)
#define RBR_STAT(chan)		(DMC + 0x00028 + (chan) * 0x00200)
#define RBR_HDH(chan)		(DMC + 0x00030 + (chan) * 0x00200)
#define RBR_HDL(chan)		(DMC + 0x00038 + (chan) * 0x00200)
#define RCRCFIG_A(chan)		(DMC + 0x00040 + (chan) * 0x00200)
#define  RCRCFIG_A_LEN_SHIFT		48
#define RCRCFIG_B(chan)		(DMC + 0x00048 + (chan) * 0x00200)
#define  RCRCFIG_B_PTHRES_SHIFT		16
#define  RCRCFIG_B_ENTOUT		(1ULL << 15)
#define RCRSTAT_A(chan)		(DMC + 0x00050 + (chan) * 0x00200)
#define RCRSTAT_B(chan)		(DMC + 0x00058 + (chan) * 0x00200)
#define RCRSTAT_C(chan)		(DMC + 0x00060 + (chan) * 0x00200)

#define RX_DMA_ENT_MSK(chan)	(DMC + 0x00068 + (chan) * 0x00200)
#define  RX_DMA_ENT_MSK_RBR_EMPTY	(1ULL << 3)
#define RX_DMA_CTL_STAT(chan)	(DMC + 0x00070 + (chan) * 0x00200)
#define  RX_DMA_CTL_STAT_MEX		(1ULL << 47)
#define  RX_DMA_CTL_STAT_RCRTHRES	(1ULL << 46)
#define  RX_DMA_CTL_STAT_RCRTO		(1ULL << 45)
#define  RX_DMA_CTL_STAT_RBR_EMPTY	(1ULL << 35)
#define  RX_DMA_CTL_STAT_PTRREAD_SHIFT	16
#define RX_DMA_CTL_STAT_DBG(chan) (DMC + 0x00098 + (chan) * 0x00200)

#define TX_LOG_PAGE_VLD(chan)	(FZC_DMC + 0x40000 + (chan) * 0x00200)
#define  TX_LOG_PAGE_VLD_PAGE0		(1ULL << 0)
#define  TX_LOG_PAGE_VLD_PAGE1		(1ULL << 1)
#define  TX_LOG_PAGE_VLD_FUNC_SHIFT	2
#define TX_LOG_MASK1(chan)	(FZC_DMC + 0x40008 + (chan) * 0x00200)
#define TX_LOG_VALUE1(chan)	(FZC_DMC + 0x40010 + (chan) * 0x00200)
#define TX_LOG_MASK2(chan)	(FZC_DMC + 0x40018 + (chan) * 0x00200)
#define TX_LOG_VALUE2(chan)	(FZC_DMC + 0x40020 + (chan) * 0x00200)
#define TX_LOG_PAGE_RELO1(chan)	(FZC_DMC + 0x40028 + (chan) * 0x00200)
#define TX_LOG_PAGE_RELO2(chan)	(FZC_DMC + 0x40030 + (chan) * 0x00200)
#define TX_LOG_PAGE_HDL(chan)	(FZC_DMC + 0x40038 + (chan) * 0x00200)

#define TX_RNG_CFIG(chan)	(DMC + 0x40000 + (chan) * 0x00200)
#define  TX_RNG_CFIG_LEN_SHIFT		48
#define TX_RING_HDL(chan)	(DMC + 0x40010 + (chan) * 0x00200)
#define TX_RING_KICK(chan)	(DMC + 0x40018 + (chan) * 0x00200)
#define  TX_RING_KICK_WRAP		(1ULL << 19)
#define TX_ENT_MSK(chan)	(DMC + 0x40020 + (chan) * 0x00200)
#define TX_CS(chan)		(DMC + 0x40028 + (chan) * 0x00200)
#define  TX_CS_PKT_CNT_MASK		(0xfffULL << 48)
#define  TX_CS_PKT_CNT_SHIFT		48
#define  TX_CS_RST			(1ULL << 31)
#define  TX_CS_STOP_N_GO		(1ULL << 28)
#define  TX_CS_SNG_STATE		(1ULL << 27)
#define TDMC_INTR_DBG(chan)	(DMC + 0x40060 + (chan) * 0x00200)
#define TXDMA_MBH(chan)		(DMC + 0x40030 + (chan) * 0x00200)
#define TXDMA_MBL(chan)		(DMC + 0x40038 + (chan) * 0x00200)
#define TX_RNG_ERR_LOGH(chan)	(DMC + 0x40048 + (chan) * 0x00200)
#define TX_RNG_ERR_LOGL(chan)	(DMC + 0x40050 + (chan) * 0x00200)

#define RXD_MULTI		(1ULL << 63)
#define RXD_L2_LEN_MASK		(0x3fffULL << 40)
#define RXD_L2_LEN_SHIFT	40
#define RXD_PKT_BUF_ADDR_MASK	0x3fffffffffULL
#define RXD_PKT_BUF_ADDR_SHIFT	6

struct nep_block {
	bus_dmamap_t	nb_map;
	void		*nb_block;
};

#define NEP_NRBDESC	256
#define NEP_NRCDESC	512

#define TXD_SOP			(1ULL << 63)
#define TXD_MARK		(1ULL << 62)
#define TXD_NUM_PTR_SHIFT	58
#define TXD_TR_LEN_SHIFT	44

struct nep_txbuf_hdr {
	uint64_t	nh_flags;
	uint64_t	nh_reserved;
};

struct nep_buf {
	bus_dmamap_t	nb_map;
	struct mbuf	*nb_m;
};

#define NEP_NTXDESC	256
#define NEP_NTXSEGS	15

struct nep_dmamem {
	bus_dmamap_t		ndm_map;
	bus_dma_segment_t	ndm_seg;
	size_t			ndm_size;
	caddr_t			ndm_kva;
};
#define NEP_DMA_MAP(_ndm)	((_ndm)->ndm_map)
#define NEP_DMA_LEN(_ndm)	((_ndm)->ndm_size)
#define NEP_DMA_DVA(_ndm)	((_ndm)->ndm_map->dm_segs[0].ds_addr)
#define NEP_DMA_KVA(_ndm)	((void *)(_ndm)->ndm_kva);

struct pool *nep_block_pool;

struct nep_softc {
	struct device		sc_dev;
	struct arpcom		sc_ac;
#define sc_lladdr	sc_ac.ac_enaddr
	struct mii_data		sc_mii;
#define sc_media	sc_mii.mii_media

	bus_dma_tag_t		sc_dmat;
	bus_space_tag_t		sc_memt;
	bus_space_handle_t 	sc_memh;
	bus_size_t		sc_mems;
	void			*sc_ih;

	int			sc_port;

	struct nep_dmamem	*sc_txring;
	struct nep_buf		*sc_txbuf;
	uint64_t		*sc_txdesc;
	int			sc_tx_prod;
	int			sc_tx_cnt;
	int			sc_tx_cons;

	uint64_t		sc_wrap;
	uint16_t		sc_pkt_cnt;

	struct nep_dmamem	*sc_rbring;
	struct nep_block	*sc_rb;
	uint32_t		*sc_rbdesc;
	struct if_rxring	sc_rx_ring;
	int			sc_rx_prod;
	struct nep_dmamem	*sc_rcring;
	uint64_t		*sc_rcdesc;
	int			sc_rx_cons;

	struct nep_dmamem	*sc_rxmbox;

	struct timeout		sc_tick;
};

int	nep_match(struct device *, void *, void *);
void	nep_attach(struct device *, struct device *, void *);

const struct cfattach nep_ca = {
	sizeof(struct nep_softc), nep_match, nep_attach
};

struct cfdriver nep_cd = {
	NULL, "nep", DV_DULL
};

static u_int	nep_mextfree_idx;

int	nep_pci_enaddr(struct nep_softc *, struct pci_attach_args *);

uint64_t nep_read(struct nep_softc *, uint32_t);
void	nep_write(struct nep_softc *, uint32_t, uint64_t);
int	nep_mii_readreg(struct device *, int, int);
void	nep_mii_writereg(struct device *, int, int, int);
void	nep_mii_statchg(struct device *);
void	nep_xmac_mii_statchg(struct nep_softc *);
void	nep_bmac_mii_statchg(struct nep_softc *);
int	nep_media_change(struct ifnet *);
void	nep_media_status(struct ifnet *, struct ifmediareq *);
int	nep_intr(void *);

void	nep_rx_proc(struct nep_softc *);
void	nep_extfree(caddr_t, u_int, void *);
void	nep_tx_proc(struct nep_softc *);

void	nep_init_ipp(struct nep_softc *);
void	nep_ipp_clear_dfifo(struct nep_softc *, uint64_t);
void	nep_init_rx_mac(struct nep_softc *);
void	nep_init_rx_xmac(struct nep_softc *);
void	nep_init_rx_bmac(struct nep_softc *);
void	nep_init_rx_channel(struct nep_softc *, int);
void	nep_init_tx_mac(struct nep_softc *);
void	nep_init_tx_xmac(struct nep_softc *);
void	nep_init_tx_bmac(struct nep_softc *);
void	nep_init_tx_channel(struct nep_softc *, int);
void	nep_enable_rx_mac(struct nep_softc *);
void	nep_disable_rx_mac(struct nep_softc *);
void	nep_stop_dma(struct nep_softc *);

void	nep_fill_rx_ring(struct nep_softc *);

void	nep_up(struct nep_softc *);
void	nep_down(struct nep_softc *);
void	nep_iff(struct nep_softc *);
int	nep_encap(struct nep_softc *, struct mbuf **, int *);

void	nep_start(struct ifnet *);
void	nep_watchdog(struct ifnet *);
void	nep_tick(void *);
int	nep_ioctl(struct ifnet *, u_long, caddr_t);

struct nep_dmamem *nep_dmamem_alloc(struct nep_softc *, size_t);
void	nep_dmamem_free(struct nep_softc *, struct nep_dmamem *);

/*
 * SUNW,pcie-neptune: 4x1G onboard on T5140/T5240
 * SUNW,pcie-qgc: 4x1G, "Sun Quad GbE UTP x8 PCI Express Card"
 * SUNW,pcie-qgc-pem: 4x1G, "Sun Quad GbE UTP x8 PCIe ExpressModule"
 * SUNW,pcie-2xgf: 2x10G, "Sun Dual 10GbE XFP PCI Express Card"
 * SUNW,pcie-2xgf-pem: 2x10G, "Sun Dual 10GbE XFP PCIe ExpressModule"
 */
int
nep_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SUN &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_SUN_NEPTUNE)
		return (1);
	return (0);
}

void
nep_attach(struct device *parent, struct device *self, void *aux)
{
	struct nep_softc *sc = (struct nep_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mii_data *mii = &sc->sc_mii;
	pcireg_t memtype;
	uint64_t val;

	if (nep_mextfree_idx == 0)
		nep_mextfree_idx = mextfree_register(nep_extfree);

	sc->sc_dmat = pa->pa_dmat;

	memtype = PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT;
	if (pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,
	    &sc->sc_memt, &sc->sc_memh, NULL, &sc->sc_mems, 0)) {
		printf(": can't map registers\n");
		return;
	}

	if (pci_intr_map_msi(pa, &ih) && pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
		return;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih =  pci_intr_establish(pa->pa_pc, ih, IPL_NET,
	    nep_intr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
		return;
	}

	printf(": %s", intrstr);

	sc->sc_port = pa->pa_function;

	nep_write(sc, SID(sc->sc_port), pa->pa_function << 5);
	nep_write(sc, LDG_NUM(LDN_RXDMA(sc->sc_port)), sc->sc_port);
	nep_write(sc, LDG_NUM(LDN_TXDMA(sc->sc_port)), sc->sc_port);
	nep_write(sc, LDG_NUM(LDN_MAC(sc->sc_port)), sc->sc_port);

	/* Port 0 gets the MIF and error interrupts. */
	if (sc->sc_port == 0) {
		nep_write(sc, LDG_NUM(LDN_MIF), sc->sc_port);
		nep_write(sc, LDG_NUM(LDN_SYSERR), sc->sc_port);
		nep_write(sc, ZCP_INT_MASK, 0);
	}

#ifdef __sparc64__
	if (OF_getprop(PCITAG_NODE(pa->pa_tag), "local-mac-address",
	    sc->sc_lladdr, ETHER_ADDR_LEN) <= 0)
#endif
		nep_pci_enaddr(sc, pa);

	printf(", address %s\n", ether_sprintf(sc->sc_lladdr));

	if (nep_block_pool == NULL) {
		nep_block_pool = malloc(sizeof(*nep_block_pool),
		    M_DEVBUF, M_WAITOK);
		if (nep_block_pool == NULL) {
			printf("%s: unable to allocate block pool\n",
			    sc->sc_dev.dv_xname);
			return;
		}
		pool_init(nep_block_pool, PAGE_SIZE, 0, IPL_NET, 0,
		    "nepblk", NULL);
	}

	val = nep_read(sc, MIF_CONFIG);
	val &= ~MIF_CONFIG_INDIRECT_MODE;
	nep_write(sc, MIF_CONFIG, val);

	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, sizeof(ifp->if_xname));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = nep_ioctl;
	ifp->if_start = nep_start;
	ifp->if_watchdog = nep_watchdog;

	mii->mii_ifp = ifp;
	mii->mii_readreg = nep_mii_readreg;
	mii->mii_writereg = nep_mii_writereg;
	mii->mii_statchg = nep_mii_statchg;

	ifmedia_init(&sc->sc_media, 0, nep_media_change, nep_media_status);

	/*
	 * The PHYs are wired up in reverse order on the 4x1G (RGMII)
	 * configuration.
	 */
	mii_attach(&sc->sc_dev, mii, 0xffffffff, MII_PHY_ANY,
	    sc->sc_port ^ 0x3, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_tick, nep_tick, sc);

	/* Enable the MIF and error interrupts. */
	if (sc->sc_port == 0) {
		nep_write(sc, LD_IM0(LDN_MIF), 0);
		nep_write(sc, LD_IM1(LDN_SYSERR), 0);
	}
}

#define PROMHDR_PTR_DATA	0x18
#define PROMDATA_PTR_VPD	0x08
#define PROMDATA_LEN		0x10
#define PROMDATA_TYPE		0x14

static const uint8_t nep_promhdr[] = { 0x55, 0xaa };
static const uint8_t nep_promdat[] = {
	'P', 'C', 'I', 'R',
	PCI_VENDOR_SUN & 0xff, PCI_VENDOR_SUN >> 8,
	PCI_PRODUCT_SUN_NEPTUNE & 0xff, PCI_PRODUCT_SUN_NEPTUNE >> 8
};

int
nep_pci_enaddr(struct nep_softc *sc, struct pci_attach_args *pa)
{
	struct pci_vpd_largeres *res;
	struct pci_vpd *vpd;
	bus_space_handle_t romh;
	bus_space_tag_t romt;
	bus_size_t romsize = 0;
	u_int8_t buf[32], *desc;
	pcireg_t address;
	int dataoff, vpdoff, len;
	int off = 0;
	int rv = -1;

	if (pci_mapreg_map(pa, PCI_ROM_REG, PCI_MAPREG_TYPE_MEM, 0,
	    &romt, &romh, 0, &romsize, 0))
		return (-1);

	address = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	address |= PCI_ROM_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, address);

	while (off < romsize) {
		bus_space_read_region_1(romt, romh, off, buf, sizeof(buf));
		if (memcmp(buf, nep_promhdr, sizeof(nep_promhdr)))
			goto fail;

		dataoff =
		    buf[PROMHDR_PTR_DATA] | (buf[PROMHDR_PTR_DATA + 1] << 8);
		if (dataoff < 0x1c)
			goto fail;
		dataoff += off;

		bus_space_read_region_1(romt, romh, dataoff, buf, sizeof(buf));
		if (memcmp(buf, nep_promdat, sizeof(nep_promdat)))
			goto fail;

		if (buf[PROMDATA_TYPE] == 1)
		    break;

		len = buf[PROMDATA_LEN] | (buf[PROMDATA_LEN + 1] << 8);
		off += len * 512;
	}

	vpdoff = buf[PROMDATA_PTR_VPD] | (buf[PROMDATA_PTR_VPD + 1] << 8);
	if (vpdoff < 0x1c)
		goto fail;
	vpdoff += off;

next:
	bus_space_read_region_1(romt, romh, vpdoff, buf, sizeof(buf));
	if (!PCI_VPDRES_ISLARGE(buf[0]))
		goto fail;

	res = (struct pci_vpd_largeres *)buf;
	vpdoff += sizeof(*res);

	len = ((res->vpdres_len_msb << 8) + res->vpdres_len_lsb);
	switch(PCI_VPDRES_LARGE_NAME(res->vpdres_byte0)) {
	case PCI_VPDRES_TYPE_IDENTIFIER_STRING:
		/* Skip identifier string. */
		vpdoff += len;
		goto next;

	case PCI_VPDRES_TYPE_VPD:
		while (len > 0) {
			bus_space_read_region_1(romt, romh, vpdoff,
			     buf, sizeof(buf));

			vpd = (struct pci_vpd *)buf;
			vpdoff += sizeof(*vpd) + vpd->vpd_len;
			len -= sizeof(*vpd) + vpd->vpd_len;

			/*
			 * We're looking for an "Enhanced" VPD...
			 */
			if (vpd->vpd_key0 != 'Z')
				continue;

			desc = buf + sizeof(*vpd);

			/* 
			 * ...which is an instance property...
			 */
			if (desc[0] != 'I')
				continue;
			desc += 3;

			/* 
			 * ...that's a byte array with the proper
			 * length for a MAC address...
			 */
			if (desc[0] != 'B' || desc[1] != ETHER_ADDR_LEN)
				continue;
			desc += 2;

			/*
			 * ...named "local-mac-address".
			 */
			if (strcmp(desc, "local-mac-address") != 0)
				continue;
			desc += strlen("local-mac-address") + 1;
					
			memcpy(sc->sc_ac.ac_enaddr, desc, ETHER_ADDR_LEN);
			sc->sc_ac.ac_enaddr[5] += pa->pa_function;
			rv = 0;
		}
		break;

	default:
		goto fail;
	}

 fail:
	if (romsize != 0)
		bus_space_unmap(romt, romh, romsize);

	address = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_ROM_REG);
	address &= ~PCI_ROM_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_ROM_REG, address);

	return (rv);
}

uint64_t
nep_read(struct nep_softc *sc, uint32_t reg)
{
	return (bus_space_read_8(sc->sc_memt, sc->sc_memh, reg));
}

void
nep_write(struct nep_softc *sc, uint32_t reg, uint64_t value)
{
	bus_space_write_8(sc->sc_memt, sc->sc_memh, reg, value);
}

int
nep_mii_readreg(struct device *self, int phy, int reg)
{
	struct nep_softc *sc = (struct nep_softc *)self;
	uint64_t frame;
	int n;

	frame = MIF_FRAME_READ;
	frame |= (reg << MIF_FRAME_REG_SHIFT) | (phy << MIF_FRAME_PHY_SHIFT);
	nep_write(sc, MIF_FRAME_OUTPUT, frame);
	for (n = 0; n < 1000; n++) {
		delay(10);
		frame = nep_read(sc, MIF_FRAME_OUTPUT);
		if (frame & MIF_FRAME_TA0)
			return (frame & MIF_FRAME_DATA);
	}

	printf("%s: %s timeout\n", sc->sc_dev.dv_xname, __func__);
	return (0);
}

void
nep_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct nep_softc *sc = (struct nep_softc *)self;
	uint64_t frame;
	int n;

	frame = MIF_FRAME_WRITE;
	frame |= (reg << MIF_FRAME_REG_SHIFT) | (phy << MIF_FRAME_PHY_SHIFT);
	frame |= (val & MIF_FRAME_DATA);
	nep_write(sc, MIF_FRAME_OUTPUT, frame);
	for (n = 0; n < 1000; n++) {
		delay(10);
		frame = nep_read(sc, MIF_FRAME_OUTPUT);
		if (frame & MIF_FRAME_TA0)
			return;
	}

	printf("%s: %s timeout\n", sc->sc_dev.dv_xname, __func__);
	return;
}

void
nep_mii_statchg(struct device *dev)
{
	struct nep_softc *sc = (struct nep_softc *)dev;

	if (sc->sc_port < 2)
		nep_xmac_mii_statchg(sc);
	else
		nep_bmac_mii_statchg(sc);
}

void
nep_xmac_mii_statchg(struct nep_softc *sc)
{
	struct mii_data *mii = &sc->sc_mii;
	uint64_t val;

	val = nep_read(sc, XMAC_CONFIG(sc->sc_port));

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
		val |= XMAC_CONFIG_SEL_CLK_25MHZ;
	else
		val &= ~XMAC_CONFIG_SEL_CLK_25MHZ;

	val |= XMAC_CONFIG_1G_PCS_BYPASS;

	val &= ~XMAC_CONFIG_MODE_MASK;
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX)
		val |= XMAC_CONFIG_MODE_GMII;
	else
		val |= XMAC_CONFIG_MODE_MII;

	val |= XMAC_CONFIG_LFS_DISABLE;

	if (mii->mii_media_active & IFM_LOOP)
		val |= XMAC_CONFIG_LOOPBACK;
	else
		val &= ~XMAC_CONFIG_LOOPBACK;

	val |= XMAC_CONFIG_TX_OUTPUT_EN;

	nep_write(sc, XMAC_CONFIG(sc->sc_port), val);
}

void
nep_bmac_mii_statchg(struct nep_softc *sc)
{
	struct mii_data *mii = &sc->sc_mii;
	uint64_t val;

	val = nep_read(sc, MAC_XIF_CONFIG(sc->sc_port));

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
		val |= MAC_XIF_CONFIG_SEL_CLK_25MHZ;
	else
		val &= MAC_XIF_CONFIG_SEL_CLK_25MHZ;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX)
		val |= MAC_XIF_CONFIG_GMII_MODE;
	else
		val &= ~MAC_XIF_CONFIG_GMII_MODE;

	if (mii->mii_media_active & IFM_LOOP)
		val |= MAC_XIF_CONFIG_LOOPBACK;
	else
		val &= ~MAC_XIF_CONFIG_LOOPBACK;

	val |= MAC_XIF_CONFIG_TX_OUTPUT_EN;

	nep_write(sc, MAC_XIF_CONFIG(sc->sc_port), val);
}

int
nep_media_change(struct ifnet *ifp)
{
	struct nep_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
nep_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nep_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

int
nep_intr(void *arg)
{
	struct nep_softc *sc = arg;
	uint64_t sv0, sv1, sv2;
	int rearm = 0;

	sv0 = nep_read(sc, LDSV0(sc->sc_port));
	sv1 = nep_read(sc, LDSV1(sc->sc_port));
	sv2 = nep_read(sc, LDSV2(sc->sc_port));

	if ((sv0 | sv1 | sv2) == 0)
		return (0);

	if (sv0 & (1ULL << LDN_TXDMA(sc->sc_port))) {
		nep_tx_proc(sc);
		rearm = 1;
	}

	if (sv0 & (1ULL << LDN_RXDMA(sc->sc_port))) {
		nep_rx_proc(sc);
		rearm = 1;
	}

	if (rearm)
		nep_write(sc, LDGIMGN(sc->sc_port), LDGIMGN_ARM | 2);
	else
		printf("%s: %s %llx %llx %llx\n", sc->sc_dev.dv_xname,
		    __func__, sv0, sv1, sv2);

	return (1);
}

void
nep_rx_proc(struct nep_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	uint64_t val;
	uint16_t count;
	uint16_t pktread = 0, ptrread = 0;
	uint64_t rxd;
	uint64_t addr;
	bus_addr_t page;
	bus_size_t off;
	char *block;
	struct mbuf *m;
	int idx, len, i;

	val = nep_read(sc, RX_DMA_CTL_STAT(sc->sc_port));
	nep_write(sc, RX_DMA_CTL_STAT(sc->sc_port),
	    RX_DMA_CTL_STAT_RCRTHRES | RX_DMA_CTL_STAT_RCRTO);

	bus_dmamap_sync(sc->sc_dmat, NEP_DMA_MAP(sc->sc_rcring), 0,
	    NEP_DMA_LEN(sc->sc_rcring), BUS_DMASYNC_POSTREAD);
	
	count = nep_read(sc, RCRSTAT_A(sc->sc_port));
	while (count > 0) {
		idx = sc->sc_rx_cons;
		KASSERT(idx < NEP_NRCDESC);

		rxd = letoh64(sc->sc_rcdesc[idx]);

		addr = (rxd & RXD_PKT_BUF_ADDR_MASK) << RXD_PKT_BUF_ADDR_SHIFT;
		len = (rxd & RXD_L2_LEN_MASK) >> RXD_L2_LEN_SHIFT;
		page = addr & ~PAGE_MASK;
		off = addr & PAGE_MASK;
		block = NULL;
		for (i = 0; i < NEP_NRBDESC; i++) {
			if (sc->sc_rb[i].nb_block &&
			    sc->sc_rb[i].nb_map->dm_segs[0].ds_addr == page) {
				block = sc->sc_rb[i].nb_block;
				break;
			}
		}
		if (block == NULL) {
			m = NULL;
		} else {
			bus_dmamap_unload(sc->sc_dmat, sc->sc_rb[i].nb_map);
			sc->sc_rb[i].nb_block = NULL;

			MGETHDR(m, M_DONTWAIT, MT_DATA);
		}

		if (m == NULL) {
			ifp->if_ierrors++;
		} else {
			MEXTADD(m, block + off, PAGE_SIZE, M_EXTWR,
			    nep_mextfree_idx, block);
			m->m_pkthdr.len = m->m_len = len;
			m->m_data += ETHER_ALIGN;

			ml_enqueue(&ml, m);
		}

		if_rxr_put(&sc->sc_rx_ring, 1);
		if ((rxd & RXD_MULTI) == 0) {
			count--;
			pktread++;
		}
		ptrread++;
		sc->sc_rx_cons++;
		if (sc->sc_rx_cons >= NEP_NRCDESC)
			sc->sc_rx_cons = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, NEP_DMA_MAP(sc->sc_rcring), 0,
	    NEP_DMA_LEN(sc->sc_rcring), BUS_DMASYNC_PREREAD);

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	nep_fill_rx_ring(sc);

	val = pktread | (ptrread << RX_DMA_CTL_STAT_PTRREAD_SHIFT);
	val |= RX_DMA_CTL_STAT_MEX;
	nep_write(sc, RX_DMA_CTL_STAT(sc->sc_port), val);
}

void
nep_extfree(caddr_t buf, u_int size, void *arg)
{
	pool_put(nep_block_pool, arg);
}

void
nep_tx_proc(struct nep_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct nep_buf *txb;
	uint64_t val;
	uint16_t pkt_cnt, count;
	int idx;

	val = nep_read(sc, TX_CS(sc->sc_port));
	pkt_cnt = (val & TX_CS_PKT_CNT_MASK) >> TX_CS_PKT_CNT_SHIFT;
	count = (pkt_cnt - sc->sc_pkt_cnt);
	count &= (TX_CS_PKT_CNT_MASK >> TX_CS_PKT_CNT_SHIFT);
	sc->sc_pkt_cnt = pkt_cnt;

	while (count > 0) {
		idx = sc->sc_tx_cons;
		KASSERT(idx < NEP_NTXDESC);

		txb = &sc->sc_txbuf[idx];
		if (txb->nb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->nb_map, 0,
			    txb->nb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->nb_map);

			m_freem(txb->nb_m);
			txb->nb_m = NULL;
			count--;
		}

		ifq_clr_oactive(&ifp->if_snd);

		sc->sc_tx_cnt--;
		sc->sc_tx_cons++;
		if (sc->sc_tx_cons >= NEP_NTXDESC)
			sc->sc_tx_cons = 0;
	}

	if (sc->sc_tx_cnt == 0)
		ifp->if_timer = 0;
}

void
nep_init_ipp(struct nep_softc *sc)
{
	uint64_t val;
	int num_entries;
	int n, i;

	if (sc->sc_port < 2)
		num_entries = IPP_P0_P1_DFIFO_ENTRIES;
	else
		num_entries = IPP_P2_P3_DFIFO_ENTRIES;

	for (i = 0; i < num_entries; i++)
		nep_ipp_clear_dfifo(sc, i);

	(void)nep_read(sc, IPP_INT_STAT(sc->sc_port));
	(void)nep_read(sc, IPP_INT_STAT(sc->sc_port));

	val = nep_read(sc, IPP_CFIG(sc->sc_port));
	val |= IPP_CFIG_SOFT_RST;
	nep_write(sc, IPP_CFIG(sc->sc_port), val);
	n = 1000;
	while (--n) {
		val = nep_read(sc, IPP_CFIG(sc->sc_port));
		if ((val & IPP_CFIG_SOFT_RST) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting IPP\n");

	val = nep_read(sc, IPP_CFIG(sc->sc_port));
	val |= IPP_CFIG_IPP_ENABLE;
	nep_write(sc, IPP_CFIG(sc->sc_port), val);

	nep_write(sc, IPP_MSK(sc->sc_port), 0);
}

void
nep_ipp_clear_dfifo(struct nep_softc *sc, uint64_t addr)
{
	uint64_t val;

	val = nep_read(sc, IPP_CFIG(sc->sc_port));
	val |= IPP_CFIG_DFIFO_PIO_W;
	nep_write(sc, IPP_CFIG(sc->sc_port), val);

	nep_write(sc, IPP_DFIFO_WR_PTR(sc->sc_port), addr);
	nep_write(sc, IPP_DFIFO_WR1(sc->sc_port), 0);
	nep_write(sc, IPP_DFIFO_WR2(sc->sc_port), 0);
	nep_write(sc, IPP_DFIFO_WR3(sc->sc_port), 0);
	nep_write(sc, IPP_DFIFO_WR4(sc->sc_port), 0);
	nep_write(sc, IPP_DFIFO_WR5(sc->sc_port), 0);

	val &= ~IPP_CFIG_DFIFO_PIO_W;
	nep_write(sc, IPP_CFIG(sc->sc_port), val);

	nep_write(sc, IPP_DFIFO_RD_PTR(sc->sc_port), addr);
	(void)nep_read(sc, IPP_DFIFO_RD1(sc->sc_port));
	(void)nep_read(sc, IPP_DFIFO_RD2(sc->sc_port));
	(void)nep_read(sc, IPP_DFIFO_RD3(sc->sc_port));
	(void)nep_read(sc, IPP_DFIFO_RD4(sc->sc_port));
	(void)nep_read(sc, IPP_DFIFO_RD5(sc->sc_port));
}

void
nep_init_rx_mac(struct nep_softc *sc)
{
	if (sc->sc_port < 2)
		nep_init_rx_xmac(sc);
	else
		nep_init_rx_bmac(sc);
}

void
nep_init_rx_xmac(struct nep_softc *sc)
{
	uint64_t addr0, addr1, addr2;
	uint64_t val;
	int n, i;

	nep_write(sc, XRXMAC_SW_RST(sc->sc_port),
	    XRXMAC_SW_RST_REG_RST | XRXMAC_SW_RST_SOFT_RST);
	n = 1000;
	while (--n) {
		val = nep_read(sc, XRXMAC_SW_RST(sc->sc_port));
		if ((val & (XRXMAC_SW_RST_REG_RST |
		    XRXMAC_SW_RST_SOFT_RST)) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting Rx MAC\n");

	addr0 = (sc->sc_lladdr[4] << 8) | sc->sc_lladdr[5];
	addr1 = (sc->sc_lladdr[2] << 8) | sc->sc_lladdr[3];
	addr2 = (sc->sc_lladdr[0] << 8) | sc->sc_lladdr[1];
	nep_write(sc, XMAC_ADDR0(sc->sc_port), addr0);
	nep_write(sc, XMAC_ADDR1(sc->sc_port), addr1);
	nep_write(sc, XMAC_ADDR2(sc->sc_port), addr2);

	nep_write(sc, XMAC_ADDR_CMPEN(sc->sc_port), 0);

	nep_write(sc, XMAC_ADD_FILT0(sc->sc_port), 0);
	nep_write(sc, XMAC_ADD_FILT1(sc->sc_port), 0);
	nep_write(sc, XMAC_ADD_FILT2(sc->sc_port), 0);
	nep_write(sc, XMAC_ADD_FILT12_MASK(sc->sc_port), 0);
	nep_write(sc, XMAC_ADD_FILT00_MASK(sc->sc_port), 0);

	for (i = 0; i < 16; i++)
		nep_write(sc, XMAC_HASH_TBL(sc->sc_port, i), 0);

	for (i = 0; i < 20; i++)
		nep_write(sc, XMAC_HOST_INFO(sc->sc_port, i), sc->sc_port);
}

void
nep_init_rx_bmac(struct nep_softc *sc)
{
	uint64_t addr0, addr1, addr2;
	uint64_t val;
	int n, i;

	nep_write(sc, RXMAC_SW_RST(sc->sc_port), RXMAC_SW_RST_SW_RST);
	n = 1000;
	while (--n) {
		val = nep_read(sc, RXMAC_SW_RST(sc->sc_port));
		if ((val & RXMAC_SW_RST_SW_RST) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting Rx MAC\n");

	val = nep_read(sc, RXMAC_CONFIG(sc->sc_port));
	val &= ~RXMAC_CONFIG_ERROR_CHK_DIS;
	val &= ~RXMAC_CONFIG_PROMISCUOUS;
	val &= ~RXMAC_CONFIG_PROMISCUOUS_GROUP;
	val &= ~RXMAC_CONFIG_ADDR_FILTER_EN;
	val &= ~RXMAC_CONFIG_HASH_FILTER_EN;
	val &= ~RXMAC_CONFIG_STRIP_FCS;
	val &= ~RXMAC_CONFIG_STRIP_PAD;
	val &= ~RXMAC_CONFIG_RX_ENABLE;
	nep_write(sc, RXMAC_CONFIG(sc->sc_port), val);

	addr0 = (sc->sc_lladdr[4] << 8) | sc->sc_lladdr[5];
	addr1 = (sc->sc_lladdr[2] << 8) | sc->sc_lladdr[3];
	addr2 = (sc->sc_lladdr[0] << 8) | sc->sc_lladdr[1];
	nep_write(sc, BMAC_ADDR0(sc->sc_port), addr0);
	nep_write(sc, BMAC_ADDR1(sc->sc_port), addr1);
	nep_write(sc, BMAC_ADDR2(sc->sc_port), addr2);

	nep_write(sc, BMAC_ALTAD_CMPEN(sc->sc_port), 1);

	nep_write(sc, MAC_ADDR_FILT0(sc->sc_port), 0);
	nep_write(sc, MAC_ADDR_FILT1(sc->sc_port), 0);
	nep_write(sc, MAC_ADDR_FILT2(sc->sc_port), 0);
	nep_write(sc, MAC_ADDR_FILT12_MASK(sc->sc_port), 0);
	nep_write(sc, MAC_ADDR_FILT00_MASK(sc->sc_port), 0);

	for (i = 0; i < 16; i++)
		nep_write(sc, MAC_HASH_TBL(sc->sc_port, i), 0);

	for (i = 0; i < 9; i++)
		nep_write(sc, BMAC_HOST_INFO(sc->sc_port, i), sc->sc_port);
}

void
nep_init_rx_channel(struct nep_softc *sc, int chan)
{
	uint64_t val;
	int i, n;

	val = nep_read(sc, RXDMA_CFIG1(chan));
	val &= ~RXDMA_CFIG1_EN;
	val |= RXDMA_CFIG1_RST;
	nep_write(sc, RXDMA_CFIG1(chan), RXDMA_CFIG1_RST);

	n = 1000;
	while (--n) {
		val = nep_read(sc, RXDMA_CFIG1(chan));
		if ((val & RXDMA_CFIG1_RST) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting Rx DMA\n");

	nep_write(sc, RX_LOG_MASK1(chan), 0);
	nep_write(sc, RX_LOG_VALUE1(chan), 0);
	nep_write(sc, RX_LOG_MASK2(chan), 0);
	nep_write(sc, RX_LOG_VALUE2(chan), 0);
	nep_write(sc, RX_LOG_PAGE_RELO1(chan), 0);
	nep_write(sc, RX_LOG_PAGE_RELO2(chan), 0);
	nep_write(sc, RX_LOG_PAGE_HDL(chan), 0);
	nep_write(sc, RX_LOG_PAGE_VLD(chan),
	    (sc->sc_port << RX_LOG_PAGE_VLD_FUNC_SHIFT) |
	    RX_LOG_PAGE_VLD_PAGE0 | RX_LOG_PAGE_VLD_PAGE1);

	nep_write(sc, RX_DMA_ENT_MSK(chan), RX_DMA_ENT_MSK_RBR_EMPTY);
	nep_write(sc, RX_DMA_CTL_STAT(chan), RX_DMA_CTL_STAT_MEX);

	val = NEP_DMA_DVA(sc->sc_rxmbox) >> 32;
	nep_write(sc, RXDMA_CFIG1(chan), val);

	val = NEP_DMA_DVA(sc->sc_rxmbox) & 0xffffffc0;
	nep_write(sc, RXDMA_CFIG2(chan), val);

	val = NEP_DMA_DVA(sc->sc_rbring);
	val |= (uint64_t)NEP_NRBDESC << RBR_CFIG_A_LEN_SHIFT;
	nep_write(sc, RBR_CFIG_A(chan), val);

	val = RBR_CFIG_B_BLKSIZE_8K;
	val |= RBR_CFIG_B_BUFSZ1_8K | RBR_CFIG_B_VLD1;
	nep_write(sc, RBR_CFIG_B(chan), val);

	nep_write(sc, RBR_KICK(chan), 0);

	val = NEP_DMA_DVA(sc->sc_rcring);
	val |= (uint64_t)NEP_NRCDESC << RCRCFIG_A_LEN_SHIFT;
	nep_write(sc, RCRCFIG_A(chan), val);

	val = 8 | RCRCFIG_B_ENTOUT;
	val |= (16 << RCRCFIG_B_PTHRES_SHIFT);
	nep_write(sc, RCRCFIG_B(chan), val);

	nep_write(sc, DEF_PT_RDC(sc->sc_port), chan);
	for (i = 0; i < 16; i++)
		nep_write(sc, RDC_TBL(sc->sc_port, i), chan);
}

void
nep_init_tx_mac(struct nep_softc *sc)
{
	if (sc->sc_port < 2)
		nep_init_tx_xmac(sc);
	else
		nep_init_tx_bmac(sc);
}

void
nep_init_tx_xmac(struct nep_softc *sc)
{
	uint64_t val;
	int n;

	nep_write(sc, XTXMAC_SW_RST(sc->sc_port),
	    XTXMAC_SW_RST_REG_RST | XTXMAC_SW_RST_SOFT_RST);
	n = 1000;
	while (--n) {
		val = nep_read(sc, XTXMAC_SW_RST(sc->sc_port));
		if ((val & (XTXMAC_SW_RST_REG_RST |
		    XTXMAC_SW_RST_SOFT_RST)) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting Tx MAC\n");

	val = nep_read(sc, XMAC_CONFIG(sc->sc_port));
	val &= ~XMAC_CONFIG_ALWAYS_NO_CRC;
	val &= ~XMAC_CONFIG_VAR_MIN_IPG_EN;
	val &= ~XMAC_CONFIG_STRETCH_MODE;
	val &= ~XMAC_CONFIG_TX_ENABLE;
	nep_write(sc, XMAC_CONFIG(sc->sc_port), val);

	val = nep_read(sc, XMAC_IPG(sc->sc_port));
	val &= ~XMAC_IPG_IPG_VALUE1_MASK;	/* MII/GMII mode */
	val |= XMAC_IPG_IPG_VALUE1_12;
	val &= ~XMAC_IPG_IPG_VALUE_MASK;	/* XGMII mode */
	val |= XMAC_IPG_IPG_VALUE_12_15;
	nep_write(sc, XMAC_IPG(sc->sc_port), val);

	val = nep_read(sc, XMAC_MIN(sc->sc_port));
	val &= ~XMAC_MIN_RX_MIN_PKT_SIZE_MASK;
	val &= ~XMAC_MIN_TX_MIN_PKT_SIZE_MASK;
	val |= (64 << XMAC_MIN_RX_MIN_PKT_SIZE_SHIFT);
	val |= (64 << XMAC_MIN_TX_MIN_PKT_SIZE_SHIFT);
	nep_write(sc, XMAC_MIN(sc->sc_port), val);
	nep_write(sc, XMAC_MAX(sc->sc_port), ETHER_MAX_LEN);

	nep_write(sc, TXMAC_FRM_CNT(sc->sc_port), 0);
	nep_write(sc, TXMAC_BYTE_CNT(sc->sc_port), 0);
}

void
nep_init_tx_bmac(struct nep_softc *sc)
{
	uint64_t val;
	int n;

	nep_write(sc, TXMAC_SW_RST(sc->sc_port), TXMAC_SW_RST_SW_RST);
	n = 1000;
	while (--n) {
		val = nep_read(sc, TXMAC_SW_RST(sc->sc_port));
		if ((val & TXMAC_SW_RST_SW_RST) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting Tx MAC\n");

	nep_write(sc, BMAC_MIN(sc->sc_port), 0x40);
	nep_write(sc, BMAC_MAX(sc->sc_port), ETHER_MAX_LEN |
	    (ETHER_MAX_LEN << BMAC_MAX_BURST_SHIFT));
	nep_write(sc, MAC_CTRL_TYPE(sc->sc_port), 0x8808);
	nep_write(sc, MAC_PA_SIZE(sc->sc_port), 0x7);
}

void
nep_init_tx_channel(struct nep_softc *sc, int chan)
{
	uint64_t val;
	int n;

	val = nep_read(sc, TXC_CONTROL);
	val |= TXC_CONTROL_TXC_ENABLED;
	val |= (1ULL << sc->sc_port);
	nep_write(sc, TXC_CONTROL, val);

	nep_write(sc, TXC_PORT_DMA(sc->sc_port), 1ULL << chan);

	val = nep_read(sc, TXC_INT_MASK);
	val &= ~TXC_INT_MASK_PORT_INT_MASK(sc->sc_port);
	nep_write(sc, TXC_INT_MASK, val);

	val = nep_read(sc, TX_CS(chan));
	val |= TX_CS_RST;
	nep_write(sc, TX_CS(chan), val);

	n = 1000;
	while (--n) {
		val = nep_read(sc, TX_CS(chan));
		if ((val & TX_CS_RST) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting Tx DMA\n");

	nep_write(sc, TX_LOG_MASK1(chan), 0);
	nep_write(sc, TX_LOG_VALUE1(chan), 0);
	nep_write(sc, TX_LOG_MASK2(chan), 0);
	nep_write(sc, TX_LOG_VALUE2(chan), 0);
	nep_write(sc, TX_LOG_PAGE_RELO1(chan), 0);
	nep_write(sc, TX_LOG_PAGE_RELO2(chan), 0);
	nep_write(sc, TX_LOG_PAGE_HDL(chan), 0);
	nep_write(sc, TX_LOG_PAGE_VLD(chan),
	    (sc->sc_port << TX_LOG_PAGE_VLD_FUNC_SHIFT) |
	    TX_LOG_PAGE_VLD_PAGE0 | TX_LOG_PAGE_VLD_PAGE1);

	nep_write(sc, TX_RING_KICK(chan), 0);

	nep_write(sc, TXC_DMA_MAX(chan), ETHER_MAX_LEN + 64);
	nep_write(sc, TX_ENT_MSK(chan), 0);

	val = NEP_DMA_DVA(sc->sc_txring);
	val |= (NEP_DMA_LEN(sc->sc_txring) / 64) << TX_RNG_CFIG_LEN_SHIFT;
	nep_write(sc, TX_RNG_CFIG(chan), val);

	nep_write(sc, TX_CS(chan), 0);
}

void
nep_enable_rx_mac(struct nep_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint64_t val;

	if (sc->sc_port < 2) {
		val = nep_read(sc, XMAC_CONFIG(sc->sc_port));
		val &= ~XMAC_CONFIG_PROMISCUOUS;
		val &= ~XMAC_CONFIG_PROMISCUOUS_GROUP;
		val &= ~XMAC_CONFIG_HASH_FILTER_EN;
		if (ifp->if_flags & IFF_PROMISC)
			val |= XMAC_CONFIG_PROMISCUOUS;
		if (ifp->if_flags & IFF_ALLMULTI)
			val |= XMAC_CONFIG_PROMISCUOUS_GROUP;
		else
			val |= XMAC_CONFIG_HASH_FILTER_EN;
		val |= XMAC_CONFIG_RX_MAC_ENABLE;
		nep_write(sc, XMAC_CONFIG(sc->sc_port), val);
	} else {
		val = nep_read(sc, RXMAC_CONFIG(sc->sc_port));
		val &= ~RXMAC_CONFIG_PROMISCUOUS;
		val &= ~RXMAC_CONFIG_PROMISCUOUS_GROUP;
		val &= ~RXMAC_CONFIG_HASH_FILTER_EN;
		if (ifp->if_flags & IFF_PROMISC)
			val |= RXMAC_CONFIG_PROMISCUOUS;
		if (ifp->if_flags & IFF_ALLMULTI)
			val |= RXMAC_CONFIG_PROMISCUOUS_GROUP;
		else
			val |= RXMAC_CONFIG_HASH_FILTER_EN;
		val |= RXMAC_CONFIG_RX_ENABLE;
		nep_write(sc, RXMAC_CONFIG(sc->sc_port), val);
	}
}

void
nep_disable_rx_mac(struct nep_softc *sc)
{
	uint64_t val;

	if (sc->sc_port < 2) {
		val = nep_read(sc, XMAC_CONFIG(sc->sc_port));
		val &= ~XMAC_CONFIG_RX_MAC_ENABLE;
		nep_write(sc, XMAC_CONFIG(sc->sc_port), val);
	} else {
		val = nep_read(sc, RXMAC_CONFIG(sc->sc_port));
		val &= ~RXMAC_CONFIG_RX_ENABLE;
		nep_write(sc, RXMAC_CONFIG(sc->sc_port), val);
	}
}

void
nep_stop_dma(struct nep_softc *sc)
{
	uint64_t val;
	int n;

	val = nep_read(sc, TX_CS(sc->sc_port));
	val |= TX_CS_STOP_N_GO;
	nep_write(sc, TX_CS(sc->sc_port), val);

	n = 1000;
	while (--n) {
		val = nep_read(sc, TX_CS(sc->sc_port));
		if (val & TX_CS_SNG_STATE)
			break;
	}
	if (n == 0)
		printf("timeout stopping Tx DMA\n");

	val = nep_read(sc, RXDMA_CFIG1(sc->sc_port));
	val &= ~RXDMA_CFIG1_EN;
	nep_write(sc, RXDMA_CFIG1(sc->sc_port), val);

	n = 1000;
	while (--n) {
		val = nep_read(sc, RXDMA_CFIG1(sc->sc_port));
		if (val & RXDMA_CFIG1_QST)
			break;
	}
	if (n == 0)
		printf("timeout stopping Rx DMA\n");
}

void
nep_up(struct nep_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct nep_block *rb;
	struct nep_buf *txb;
	uint64_t val;
	int i, n;

	/* Allocate Rx block descriptor ring. */
	sc->sc_rbring = nep_dmamem_alloc(sc, NEP_NRBDESC * sizeof(uint32_t));
	if (sc->sc_rbring == NULL)
		return;
	sc->sc_rbdesc = NEP_DMA_KVA(sc->sc_rbring);

	sc->sc_rb = malloc(sizeof(struct nep_block) * NEP_NRBDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < NEP_NRBDESC; i++) {
		rb = &sc->sc_rb[i];
		bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE, 0,
		    BUS_DMA_WAITOK, &rb->nb_map);
		rb->nb_block = NULL;
	}

	sc->sc_rx_prod = 0;
	if_rxr_init(&sc->sc_rx_ring, 16, NEP_NRBDESC);
	
	/* Allocate Rx completion descriptor ring. */
	sc->sc_rcring = nep_dmamem_alloc(sc, NEP_NRCDESC * sizeof(uint64_t));
	if (sc->sc_rcring == NULL)
		goto free_rbring;
	sc->sc_rcdesc = NEP_DMA_KVA(sc->sc_rcring);

	sc->sc_rx_cons = 0;

	/* Allocate Rx mailbox. */
	sc->sc_rxmbox = nep_dmamem_alloc(sc, 64);
	if (sc->sc_rxmbox == NULL)
		goto free_rcring;

	/* Allocate Tx descriptor ring. */
	sc->sc_txring = nep_dmamem_alloc(sc, NEP_NTXDESC * sizeof(uint64_t));
	if (sc->sc_txring == NULL)
		goto free_rxmbox;
	sc->sc_txdesc = NEP_DMA_KVA(sc->sc_txring);

	sc->sc_txbuf = malloc(sizeof(struct nep_buf) * NEP_NTXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < NEP_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, NEP_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->nb_map);
		txb->nb_m = NULL;
	}

	sc->sc_tx_prod = sc->sc_tx_cons = 0;
	sc->sc_tx_cnt = 0;
	sc->sc_wrap = 0;
	sc->sc_pkt_cnt = 0;

	if (sc->sc_port < 2) {
		/* Disable the POR loopback clock source. */
		val = nep_read(sc, XMAC_CONFIG(sc->sc_port));
		val &= ~XMAC_CONFIG_SEL_POR_CLK_SRC;
		nep_write(sc, XMAC_CONFIG(sc->sc_port), val);
	}

	nep_write(sc, PCS_DPATH_MODE(sc->sc_port), PCS_DPATH_MODE_MII);
	val = nep_read(sc, PCS_MII_CTL(sc->sc_port));
	val |= PCS_MII_CTL_RESET;
	nep_write(sc, PCS_MII_CTL(sc->sc_port), val);
	n = 1000;
	while (--n) {
		val = nep_read(sc, PCS_MII_CTL(sc->sc_port));
		if ((val & PCS_MII_CTL_RESET) == 0)
			break;
	}
	if (n == 0)
		printf("timeout resetting PCS\n");

	nep_init_rx_mac(sc);
	nep_init_rx_channel(sc, sc->sc_port);
	nep_init_ipp(sc);

	nep_init_tx_mac(sc);
	nep_init_tx_channel(sc, sc->sc_port);

	nep_fill_rx_ring(sc);

	nep_enable_rx_mac(sc);
	if (sc->sc_port < 2) {
		val = nep_read(sc, XMAC_CONFIG(sc->sc_port));
		val |= XMAC_CONFIG_TX_ENABLE;
		nep_write(sc, XMAC_CONFIG(sc->sc_port), val);
	} else {
		val = nep_read(sc, TXMAC_CONFIG(sc->sc_port));
		val |= TXMAC_CONFIG_TX_ENABLE;
		nep_write(sc, TXMAC_CONFIG(sc->sc_port), val);
	}

	val = nep_read(sc, RXDMA_CFIG1(sc->sc_port));
	val |= RXDMA_CFIG1_EN;
	nep_write(sc, RXDMA_CFIG1(sc->sc_port), val);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Enable interrupts. */
	nep_write(sc, LD_IM1(LDN_MAC(sc->sc_port)), 0);
	nep_write(sc, LD_IM0(LDN_RXDMA(sc->sc_port)), 0);
	nep_write(sc, LD_IM0(LDN_TXDMA(sc->sc_port)), 0);
	nep_write(sc, LDGIMGN(sc->sc_port), LDGIMGN_ARM | 2);

	timeout_add_sec(&sc->sc_tick, 1);

	return;

free_rxmbox:
	nep_dmamem_free(sc, sc->sc_rxmbox);
free_rcring:
	nep_dmamem_free(sc, sc->sc_rcring);
free_rbring:
	nep_dmamem_free(sc, sc->sc_rbring);
}

void
nep_down(struct nep_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct nep_buf *txb;
	struct nep_block *rb;
	uint64_t val;
	int i;

	timeout_del(&sc->sc_tick);

	/* Disable interrupts. */
	nep_write(sc, LD_IM1(LDN_MAC(sc->sc_port)), 1);
	nep_write(sc, LD_IM0(LDN_RXDMA(sc->sc_port)), 1);
	nep_write(sc, LD_IM0(LDN_TXDMA(sc->sc_port)), 1);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	nep_disable_rx_mac(sc);

	val = nep_read(sc, IPP_CFIG(sc->sc_port));
	val &= ~IPP_CFIG_IPP_ENABLE;
	nep_write(sc, IPP_CFIG(sc->sc_port), val);

	nep_stop_dma(sc);

	for (i = 0; i < NEP_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		if (txb->nb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->nb_map, 0,
			    txb->nb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->nb_map);
			m_freem(txb->nb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, txb->nb_map);
	}

	nep_dmamem_free(sc, sc->sc_txring);
	free(sc->sc_txbuf, M_DEVBUF, sizeof(struct nep_buf) * NEP_NTXDESC);

	nep_dmamem_free(sc, sc->sc_rxmbox);
	nep_dmamem_free(sc, sc->sc_rcring);

	for (i = 0; i < NEP_NRBDESC; i++) {
		rb = &sc->sc_rb[i];
		if (rb->nb_block) {
			bus_dmamap_sync(sc->sc_dmat, rb->nb_map, 0,
			    rb->nb_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rb->nb_map);
			pool_put(nep_block_pool, rb->nb_block);
		}
		bus_dmamap_destroy(sc->sc_dmat, rb->nb_map);
	}

	nep_dmamem_free(sc, sc->sc_rbring);
	free(sc->sc_rb, M_DEVBUF, sizeof(struct nep_block) * NEP_NRBDESC);
}

void
nep_iff(struct nep_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc, hash[16];
	int i;

	nep_disable_rx_mac(sc);

	ifp->if_flags &= ~IFF_ALLMULTI;
	memset(hash, 0, sizeof(hash));

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
                        crc = ether_crc32_le(enm->enm_addrlo,
                            ETHER_ADDR_LEN);

                        crc >>= 24;
                        hash[crc >> 4] |= 1 << (15 - (crc & 15));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	for (i = 0; i < nitems(hash); i++) {
		if (sc->sc_port < 2)
			nep_write(sc, XMAC_HASH_TBL(sc->sc_port, i), hash[i]);
		else
			nep_write(sc, MAC_HASH_TBL(sc->sc_port, i), hash[i]);
	}

	nep_enable_rx_mac(sc);
}

int
nep_encap(struct nep_softc *sc, struct mbuf **m0, int *idx)
{
	struct mbuf *m = *m0;
	struct nep_txbuf_hdr *nh;
	uint64_t txd;
	bus_dmamap_t map;
	int cur, frag, i;
	int len, pad;
	int err;

	/*
	 * MAC does not support padding of transmit packets that are
	 * fewer than 60 bytes.
	 */
	if (m->m_pkthdr.len < (ETHER_MIN_LEN - ETHER_CRC_LEN)) {
		struct mbuf *n;
		int padlen;

		padlen = (ETHER_MIN_LEN - ETHER_CRC_LEN) - m->m_pkthdr.len;
		MGET(n, M_DONTWAIT, MT_DATA);
		if (n == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
		memset(mtod(n, caddr_t), 0, padlen);
		n->m_len = padlen;
		m_cat(m, n);
		m->m_pkthdr.len += padlen;
	}

	if (m_leadingspace(m) < 16)
		pad = 0;
	else
		pad = mtod(m, u_long) % 16;
	len = m->m_pkthdr.len + pad;
	M_PREPEND(m, sizeof(*nh) + pad, M_DONTWAIT);
	if (m == NULL)
		return (ENOBUFS);
	nh = mtod(m, struct nep_txbuf_hdr *);
	nh->nh_flags = htole64((len << 16) | (pad / 2));
	nh->nh_reserved = 0;

	cur = frag = *idx;
	map = sc->sc_txbuf[cur].nb_map;

	err = bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT);
	if (err) {
		/* XXX defrag */
		m_freem(m);
		return (ENOBUFS);
	}

	/* Sync the DMA map. */
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	txd = TXD_SOP | TXD_MARK;
	txd |= ((uint64_t)map->dm_nsegs << TXD_NUM_PTR_SHIFT);
	for (i = 0; i < map->dm_nsegs; i++) {
		txd |= ((uint64_t)map->dm_segs[i].ds_len << TXD_TR_LEN_SHIFT);
		txd |= map->dm_segs[i].ds_addr;
		sc->sc_txdesc[frag] = htole64(txd);
		txd = 0;

		bus_dmamap_sync(sc->sc_dmat, NEP_DMA_MAP(sc->sc_txring),
		    frag * sizeof(txd), sizeof(txd), BUS_DMASYNC_PREWRITE);

		cur = frag++;
		if (frag >= NEP_NTXDESC)
			frag = 0;
		KASSERT(frag != sc->sc_tx_cons);
	}

	KASSERT(sc->sc_txbuf[cur].nb_m == NULL);
	sc->sc_txbuf[*idx].nb_map = sc->sc_txbuf[cur].nb_map;
	sc->sc_txbuf[cur].nb_map = map;
	sc->sc_txbuf[cur].nb_m = m;

	if (frag < *idx)
		sc->sc_wrap ^= TX_RING_KICK_WRAP;
	nep_write(sc, TX_RING_KICK(sc->sc_port), sc->sc_wrap | (frag << 3));

	sc->sc_tx_cnt += map->dm_nsegs;
	*idx = frag;

	m_adj(m, sizeof(*nh) + pad);
	*m0 = m;

	return (0);
}

void
nep_start(struct ifnet *ifp)
{
	struct nep_softc *sc = (struct nep_softc *)ifp->if_softc;
	struct mbuf *m;
	int idx;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	if (ifq_is_oactive(&ifp->if_snd))
		return;
	if (ifq_empty(&ifp->if_snd))
		return;

	idx = sc->sc_tx_prod;
	for (;;) {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		if (sc->sc_tx_cnt >= (NEP_NTXDESC - NEP_NTXSEGS)) {
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* Now we are committed to transmit the packet. */
		ifq_deq_commit(&ifp->if_snd, m);

		if (nep_encap(sc, &m, &idx))
			break;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (sc->sc_tx_prod != idx) {
		sc->sc_tx_prod = idx;

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;
	}
}

void
nep_watchdog(struct ifnet *ifp)
{
	printf("%s\n", __func__);
}

void
nep_tick(void *arg)
{
	struct nep_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

int
nep_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct nep_softc *sc = (struct nep_softc *)ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				nep_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				nep_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			nep_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
nep_fill_rx_ring(struct nep_softc *sc)
{
	struct nep_block *rb;
	void *block;
	uint64_t val;
	u_int slots;
	int desc, err;
	int count = 0;

	desc = sc->sc_rx_prod;
	slots = if_rxr_get(&sc->sc_rx_ring, NEP_NRBDESC);
	while (slots > 0) {
		rb = &sc->sc_rb[desc];

		block = pool_get(nep_block_pool, PR_NOWAIT);
		if (block == NULL)
			break;
		err = bus_dmamap_load(sc->sc_dmat, rb->nb_map, block,
		     PAGE_SIZE, NULL, BUS_DMA_NOWAIT);
		if (err) {
			pool_put(nep_block_pool, block);
			break;
		}
		rb->nb_block = block;
		sc->sc_rbdesc[desc++] = 
		    htole32(rb->nb_map->dm_segs[0].ds_addr >> 12);
		count++;
		slots--;
		if (desc >= NEP_NRBDESC)
			desc = 0;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);
	if (count > 0) {
		nep_write(sc, RBR_KICK(sc->sc_port), count);
		val = nep_read(sc, RX_DMA_CTL_STAT(sc->sc_port));
		val |= RX_DMA_CTL_STAT_RBR_EMPTY;
		nep_write(sc, RX_DMA_CTL_STAT(sc->sc_port), val);
		sc->sc_rx_prod = desc;
	}
}

struct nep_dmamem *
nep_dmamem_alloc(struct nep_softc *sc, size_t size)
{
	struct nep_dmamem *m;
	int nsegs;

	m = malloc(sizeof(*m), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL)
		return (NULL);

	m->ndm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &m->ndm_map) != 0)
		goto qdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &m->ndm_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &m->ndm_seg, nsegs, size, &m->ndm_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, m->ndm_map, m->ndm_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (m);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, m->ndm_kva, m->ndm_size);
free:
	bus_dmamem_free(sc->sc_dmat, &m->ndm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, m->ndm_map);
qdmfree:
	free(m, M_DEVBUF, sizeof(*m));

	return (NULL);
}

void
nep_dmamem_free(struct nep_softc *sc, struct nep_dmamem *m)
{
	bus_dmamap_unload(sc->sc_dmat, m->ndm_map);
	bus_dmamem_unmap(sc->sc_dmat, m->ndm_kva, m->ndm_size);
	bus_dmamem_free(sc->sc_dmat, &m->ndm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, m->ndm_map);
	free(m, M_DEVBUF, sizeof(*m));
}
