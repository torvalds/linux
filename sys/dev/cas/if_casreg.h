/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * Copyright (c) 2008 Marius Strobl <marius@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: gemreg.h,v 1.8 2005/12/11 12:21:26 christos Exp
 *	from: FreeBSD: if_gemreg.h 174987 2007-12-30 01:32:03Z marius
 *
 * $FreeBSD$
 */

#ifndef	_IF_CASREG_H
#define	_IF_CASREG_H

/*
 * register definitions for Sun Cassini/Cassini+ and National Semiconductor
 * DP83065 Saturn Gigabit Ethernet controllers
 */

/* global resources */
#define	CAS_CAW			0x0004	/* core arbitration weight */
#define	CAS_INF_BURST		0x0008	/* infinite burst enable */
#define	CAS_STATUS		0x000c	/* interrupt status */
#define	CAS_INTMASK		0x0010	/* interrupt mask */
#define	CAS_CLEAR_ALIAS		0x0014	/* clear mask alias */
#define	CAS_STATUS_ALIAS	0x001c	/* interrupt status alias */
#define	CAS_ERROR_STATUS	0x1000	/* PCI error status */
#define	CAS_ERROR_MASK		0x1004	/* PCI error mask */
#define	CAS_BIM_CONF		0x1008	/* BIM configuration */
#define	CAS_BIM_DIAG		0x100c	/* BIM diagnostic */
#define	CAS_RESET		0x1010	/* software reset */
#define	CAS_BIM_LDEV_OEN	0x1020	/* BIM local device output enable */
#define	CAS_BIM_BUF_ADDR	0x1024	/* BIM buffer address */
#define	CAS_BIM_BUF_DATA_LO	0x1028	/* BIM buffer data low */
#define	CAS_BIM_BUF_DATA_HI	0x102c	/* BIM buffer data high */
#define	CAS_BIM_RAM_BIST	0x1030	/* BIM RAM BIST control/status */
#define	CAS_PROBE_MUX_SELECT	0x1034	/* PROBE MUX SELECT */
#define	CAS_INTMASK2		0x1038	/* interrupt mask 2 for INTB */
#define	CAS_STATUS2		0x103c	/* interrupt status 2 for INTB */
#define	CAS_CLEAR_ALIAS2	0x1040	/* clear mask alias 2 for INTB */
#define	CAS_STATUS_ALIAS2	0x1044	/* interrupt status alias 2 for INTB */
#define	CAS_INTMASK3		0x1048	/* interrupt mask 3 for INTC */
#define	CAS_STATUS3		0x104c	/* interrupt status 3 for INTC */
#define	CAS_CLEAR_ALIAS3	0x1050	/* clear mask alias 3 for INTC */
#define	CAS_STATUS_ALIAS3	0x1054	/* interrupt status alias 3 for INTC */
#define	CAS_INTMASK4		0x1058	/* interrupt mask 4 for INTD */
#define	CAS_STATUS4		0x105c	/* interrupt status 4 for INTD */
#define	CAS_CLEAR_ALIAS4	0x1060	/* clear mask alias 4 for INTD */
#define	CAS_STATUS_ALIAS4	0x1064	/* interrupt status alias 4 for INTD */
#define	CAS_SATURN_PCFG		0x106c	/* internal MACPHY pin configuration */

#define	CAS_CAW_RX_WGHT_MASK	0x00000003	/* RX DMA factor for... */
#define	CAS_CAW_RX_WGHT_SHFT	0		/* ...weighted round robin */
#define	CAS_CAW_TX_WGHT_MASK	0x0000000c	/* RX DMA factor for... */
#define	CAS_CAW_TX_WGHT_SHFT	2		/* ...weighted round robin */
#define	CAS_CAW_RR_DIS		0x00000010	/* weighted round robin dis. */

#define	CAS_INF_BURST_EN	0x00000001	/* Allow bursts > cachline. */

/*
 * shared interrupt bits for CAS_STATUS, CAS_INTMASK, CAS_CLEAR_ALIAS and
 * CAS_STATUS_ALIAS
 * Bits 0-9 of CAS_STATUS auto-clear when read.  CAS_CLEAR_ALIAS specifies
 * which of bits 0-9 auto-clear when reading CAS_STATUS_ALIAS.
 */
#define	CAS_INTR_TX_INT_ME	0x00000001	/* Frame w/ INT_ME set sent. */
#define	CAS_INTR_TX_ALL		0x00000002	/* TX frames trans. to FIFO. */
#define	CAS_INTR_TX_DONE	0x00000004	/* Any TX frame transferred. */
#define	CAS_INTR_TX_TAG_ERR	0x00000008	/* TX FIFO tag corrupted. */
#define	CAS_INTR_RX_DONE	0x00000010	/* >=1 RX frames transferred. */
#define	CAS_INTR_RX_BUF_NA	0x00000020	/* RX buffer not available */
#define	CAS_INTR_RX_TAG_ERR	0x00000040	/* RX FIFO tag corrupted. */
#define	CAS_INTR_RX_COMP_FULL	0x00000080	/* RX completion ring full */
#define	CAS_INTR_RX_BUF_AEMPTY	0x00000100	/* RX desc. ring almost empty */
#define	CAS_INTR_RX_COMP_AFULL	0x00000200	/* RX cmpl. ring almost full */
#define	CAS_INTR_RX_LEN_MMATCH	0x00000400	/* length field mismatch */
#define	CAS_INTR_SUMMARY	0x00001000	/* summary interrupt bit */
#define	CAS_INTR_PCS_INT	0x00002000	/* PCS interrupt */
#define	CAS_INTR_TX_MAC_INT	0x00004000	/* TX MAC interrupt */
#define	CAS_INTR_RX_MAC_INT	0x00008000	/* RX MAC interrupt */
#define	CAS_INTR_MAC_CTRL_INT	0x00010000	/* MAC control interrupt */
#define	CAS_INTR_MIF		0x00020000	/* MIF interrupt */
#define	CAS_INTR_PCI_ERROR_INT	0x00040000	/* PCI error interrupt */

#define	CAS_STATUS_TX_COMP3_MASK	0xfff80000	/* TX completion 3 */
#define	CAS_STATUS_TX_COMP3_SHFT	19

/* CAS_ERROR_STATUS and CAS_ERROR_MASK PCI error bits */
#define	CAS_ERROR_DTRTO		0x00000002	/* delayed trans. timeout */
#define	CAS_ERROR_OTHER		0x00000004	/* errors (see PCIR_STATUS) */
#define	CAS_ERROR_DMAW_ZERO	0x00000008	/* zero count DMA write */
#define	CAS_ERROR_DMAR_ZERO	0x00000010	/* zero count DMA read */
#define	CAS_ERROR_RTRTO		0x00000020	/* 255 retries exceeded */

#define	CAS_BIM_CONF_BD64_DIS	0x00000004	/* 64-bit mode disable */
#define	CAS_BIM_CONF_M66EN	0x00000008	/* PCI clock is 66MHz (ro). */
#define	CAS_BIM_CONF_BUS32_WIDE	0x00000010	/* PCI bus is 32-bit (ro). */
#define	CAS_BIM_CONF_DPAR_EN	0x00000020	/* parity error intr. enable */
#define	CAS_BIM_CONF_RMA_EN	0x00000040	/* master abort intr. enable */
#define	CAS_BIM_CONF_RTA_EN	0x00000080	/* target abort intr. enable */
#define	CAS_BIM_CONF_DIS_BIM	0x00000200	/* Stop PCI DMA transactions. */
#define	CAS_BIM_CONF_BIM_DIS	0x00000400	/* BIM was stopped (ro). */
#define	CAS_BIM_CONF_BLOCK_PERR	0x00000800	/* Block PERR# to PCI bus. */

#define	CAS_BIM_DIAG_BRST_SM	0x0000007f	/* burst ctrl. state machine */
#define	CAS_BIM_DIAG_MSTR_SM	0x3fffff00

#define	CAS_RESET_TX		0x00000001	/* Reset TX DMA engine. */
#define	CAS_RESET_RX		0x00000002	/* Reset RX DMA engine. */
#define	CAS_RESET_RSTOUT	0x00000004	/* Force PCI RSTOUT#. */
#define	CAS_RESET_PCS_DIS	0x00000008	/* PCS reset disable */
#define	CAS_RESET_BREQ_SM	0x00007f00	/* breq state machine */
#define	CAS_RESET_PCIARB	0x00070000	/* PCI arbitration state */
#define	CAS_RESET_RDPCI		0x00300000	/* read PCI state */
#define	CAS_RESET_RDARB		0x00c00000	/* read arbitration state */
#define	CAS_RESET_WRPCI		0x06000000	/* write PCI state */
#define	CAS_RESET_WRARB		0x38000000	/* write arbitration state */

#define	CAS_BIM_LDEV_OEN_PAD	0x00000001	/* addr. bus, RW and OE */
#define	CAS_BIM_LDEV_OEN_PROM	0x00000002	/* PROM chip select */
#define	CAS_BIM_LDEV_OEN_EXT	0x00000004	/* secondary local bus device */
#define	CAS_BIM_LDEV_OEN_SOFT_0	0x00000008	/* soft. progr. ctrl. bit 0 */
#define	CAS_BIM_LDEV_OEN_SOFT_1	0x00000010	/* soft. progr. ctrl. bit 1 */
#define	CAS_BIM_LDEV_OEN_HWRST	0x00000020	/* hw. reset (Cassini+ only) */

#define	CAS_BIM_BUF_ADDR_INDEX	0x0000003f	/* buffer entry index */
#define	CAS_BIM_BUF_ADDR_RDWR	0x00000040	/* 0: read, 1: write access */

#define	CAS_BIM_RAM_BIST_START	0x00000001	/* Start BIST on read buffer. */
#define	CAS_BIM_RAM_BIST_SUM	0x00000004	/* read buffer pass summary */
#define	CAS_BIM_RAM_BIST_LO	0x00000010	/* read buf. low bank passes */
#define	CAS_BIM_RAM_BIST_HI	0x00000020	/* read buf. high bank passes */

#define	CAS_PROBE_MUX_SELECT_LO	0x0000000f	/* P_A[7:0] */
#define	CAS_PROBE_MUX_SELECT_HI	0x000000f0	/* P_A[15:8] */
#define	CAS_PROBE_MUX_SELECT_SB	0x000000f0	/* txdma_wr address and size */
#define	CAS_PROBE_MUX_SELECT_EN	0xf0000000	/* enable probe on P_A[15:0] */

/*
 * interrupt bits for CAS_INTMASK[2-4], CAS_STATUS[2-4], CAS_CLEAR_ALIAS[2-4]
 * and CAS_STATUS_ALIAS[2-4].
 * CAS_STATUS[2-4] auto-clear when read.  CAS_CLEAR_ALIAS[2-4] specifies which
 * of bits 0-9 auto-clear when reading the corresponding CAS_STATUS_ALIAS[2-4].
 */
#define	CAS_INTRN_RX_DONE	0x00000001	/* >=1 RX frames transferred. */
#define	CAS_INTRN_RX_COMP_FULL	0x00000002	/* RX completion ring full */
#define	CAS_INTRN_RX_COMP_AFULL	0x00000004	/* RX cmpl. ring almost full */
#define	CAS_INTRN_RX_BUF_NA	0x00000008	/* RX buffer not available */
#define	CAS_INTRN_RX_BUF_AEMPTY	0x00000010	/* RX desc. ring almost empty */

/* INTn enable bit for CAS_INTMASK[2-4] */
#define	CAS_INTMASKN_EN		0x00000080	/* INT[B-D] enable */

#define	CAS_SATURN_PCFG_TLA	0x00000001	/* PHY activity LED */
#define	CAS_SATURN_PCFG_FLA	0x00000002	/* PHY 10MBit/sec LED */
#define	CAS_SATURN_PCFG_CLA	0x00000004	/* PHY 100MBit/sec LED */
#define	CAS_SATURN_PCFG_LLA	0x00000008	/* PHY 1000MBit/sec LED */
#define	CAS_SATURN_PCFG_RLA	0x00000010	/* PHY full-duplex LED */
#define	CAS_SATURN_PCFG_PDS	0x00000020	/* PHY debug mode */
#define	CAS_SATURN_PCFG_MTP	0x00000080	/* test point select */
#define	CAS_SATURN_PCFG_GMO	0x00000100	/* GMII observe */
#define	CAS_SATURN_PCFG_FSI	0x00000200	/* freeze GMII/SERDES */
#define	CAS_SATURN_PCFG_LAD	0x00000800	/* MAC LED control active low */

/* TX DMA registers */
#define	CAS_TX_CONF		0x2004	/* TX configuration */
#define	CAS_TX_FIFO_WR		0x2014	/* FIFO write pointer */
#define	CAS_TX_FIFO_SDWR	0x2018	/* FIFO shadow write pointer */
#define	CAS_TX_FIFO_RD		0x201c	/* FIFO read pointer */
#define	CAS_TX_FIFO_SDRD	0x2020	/* FIFO shadow read pointer */
#define	CAS_TX_FIFO_PKT_CNT	0x2024	/* FIFO packet counter */
#define	CAS_TX_SM1		0x2028	/* TX state machine 1 */
#define	CAS_TX_SM2		0x202c	/* TX state machine 2 */
#define	CAS_TX_DATA_PTR_LO	0x2030	/* TX data pointer low */
#define	CAS_TX_DATA_PTR_HI	0x2034	/* TX data pointer high */
#define	CAS_TX_KICK1		0x2038	/* TX kick 1 */
#define	CAS_TX_KICK2		0x203c	/* TX kick 2 */
#define	CAS_TX_KICK3		0x2040	/* TX kick 3 */
#define	CAS_TX_KICK4		0x2044	/* TX kick 4 */
#define	CAS_TX_COMP1		0x2048	/* TX completion 1 */
#define	CAS_TX_COMP2		0x204c	/* TX completion 2 */
#define	CAS_TX_COMP3		0x2050	/* TX completion 3 */
#define	CAS_TX_COMP4		0x2054	/* TX completion 4 */
#define	CAS_TX_COMPWB_BASE_LO	0x2058	/* TX completion writeback base low */
#define	CAS_TX_COMPWB_BASE_HI	0x205c	/* TX completion writeback base high */
#define	CAS_TX_DESC1_BASE_LO	0x2060	/* TX descriptor ring 1 base low */
#define	CAS_TX_DESC1_BASE_HI	0x2064	/* TX descriptor ring 1 base high */
#define	CAS_TX_DESC2_BASE_LO	0x2068	/* TX descriptor ring 2 base low */
#define	CAS_TX_DESC2_BASE_HI	0x206c	/* TX descriptor ring 2 base high */
#define	CAS_TX_DESC3_BASE_LO	0x2070	/* TX descriptor ring 2 base low */
#define	CAS_TX_DESC3_BASE_HI	0x2074	/* TX descriptor ring 2 base high */
#define	CAS_TX_DESC4_BASE_LO	0x2078	/* TX descriptor ring 2 base low */
#define	CAS_TX_DESC4_BASE_HI	0x207c	/* TX descriptor ring 2 base high */
#define	CAS_TX_MAXBURST1	0x2080	/* TX MaxBurst 1 */
#define	CAS_TX_MAXBURST2	0x2084	/* TX MaxBurst 2 */
#define	CAS_TX_MAXBURST3	0x2088	/* TX MaxBurst 3 */
#define	CAS_TX_MAXBURST4	0x208c	/* TX MaxBurst 4 */
#define	CAS_TX_FIFO_ADDR	0x2104	/* TX FIFO address */
#define	CAS_TX_FIFO_TAG		0x2108	/* TX FIFO tag */
#define	CAS_TX_FIFO_DATA_LO	0x210c	/* TX FIFO data low */
#define	CAS_TX_FIFO_DATA_HI_T1	0x2110	/* TX FIFO data highT1 */
#define	CAS_TX_FIFO_DATA_HI_T0	0x2114	/* TX FIFO data highT0 */
#define	CAS_TX_FIFO_SIZE	0x2118	/* TX FIFO size in 64 byte multiples */
#define	CAS_TX_RAM_BIST		0x211c	/* TX RAM BIST control/status */

#define	CAS_TX_CONF_TXDMA_EN	0x00000001	/* TX DMA enable */
#define	CAS_TX_CONF_FIFO_PIO	0x00000002	/* Allow TX FIFO PIO access. */
#define	CAS_TX_CONF_DESC1_MASK	0x0000003c	/* TX descriptor ring 1 size */
#define	CAS_TX_CONF_DESC1_SHFT	2
#define	CAS_TX_CONF_DESC2_MASK	0x000003c0	/* TX descriptor ring 2 size */
#define	CAS_TX_CONF_DESC2_SHFT	6
#define	CAS_TX_CONF_DESC3_MASK	0x00003c00	/* TX descriptor ring 3 size */
#define	CAS_TX_CONF_DESC3_SHFT	10
#define	CAS_TX_CONF_DESC4_MASK	0x0003c000	/* TX descriptor ring 4 size */
#define	CAS_TX_CONF_DESC4_SHFT	14
#define	CAS_TX_CONF_PACED	0x00100000	/* ALL intr. on FIFO empty */
#define	CAS_TX_CONF_RDPP_DIS	0x01000000	/* Should always be set. */
#define	CAS_TX_CONF_COMPWB_Q1	0x02000000	/* Completion writeback... */
#define	CAS_TX_CONF_COMPWB_Q2	0x04000000	/* ...happens at the end... */
#define	CAS_TX_CONF_COMPWB_Q3	0x08000000	/* ...of every packet in... */
#define	CAS_TX_CONF_COMPWB_Q4	0x10000000	/* ...queue n. */
#define	CAS_TX_CONF_PICWB_DIS	0x20000000	/* pre-intr. compl. W/B dis. */
#define	CAS_TX_CONF_CTX_MASK	0xc0000000	/* test port selection */
#define	CAS_TX_CONF_CTX_SHFT	30

#define	CAS_TX_COMPWB_ALIGN	2048		/* TX compl. W/B alignment */

#define	CAS_TX_DESC_ALIGN	2048		/* TX descriptor alignment */

/* descriptor ring size bits for both CAS_TX_CONF and CAS_RX_CONF */
#define	CAS_DESC_32		0x0		/* 32 descriptors */
#define	CAS_DESC_64		0x1		/* 64 descriptors */
#define	CAS_DESC_128		0x2		/* 128 descriptors */
#define	CAS_DESC_256		0x3		/* 256 descriptors */
#define	CAS_DESC_512		0x4		/* 512 descriptors */
#define	CAS_DESC_1K		0x5		/* 1k descriptors */
#define	CAS_DESC_2K		0x6		/* 2k descriptors */
#define	CAS_DESC_4K		0x7		/* 4k descriptors */
#define	CAS_DESC_8K		0x8		/* 8k descriptors */

#define	CAS_TX_SM1_CHAIN	0x000003ff	/* chaining state machine */
#define	CAS_TX_SM1_CKSUM	0x00000c00	/* checksum state machine */
#define	CAS_TX_SM1_TX_FIFO_LOAD	0x0003f000	/* TX FIFO load state machine */
#define	CAS_TX_SM1_TX_FIFO_UNLD	0x003c0000	/* TX FIFO unload state mach. */
#define	CAS_TX_SM1_CACHE_CTRL	0x03c00000	/* cache control state mach. */
#define	CAS_TX_SM1_CBQARB	0x03c00000	/* CBQ arbiter state machine */

#define	CAS_TX_SM2_COMPWB	0x00000007	/* compl. WB state machine */
#define	CAS_TX_SM2_SUB_LOAD	0x00000038	/* sub load state machine */
#define	CAS_TX_SM2_KICK		0x000000c0	/* kick state machine */

#define	CAS_TX_RAM_BIST_START	0x00000001	/* Start RAM BIST process. */
#define	CAS_TX_RAM_BIST_SUMMARY	0x00000002	/* All RAM okay */
#define	CAS_TX_RAM_BIST_RAM32B	0x00000004	/* RAM32B okay */
#define	CAS_TX_RAM_BIST_RAM33B	0x00000008	/* RAM33B okay */
#define	CAS_TX_RAM_BIST_RAM32A	0x00000010	/* RAM32A okay */
#define	CAS_TX_RAM_BIST_RAM33A	0x00000020	/* RAM33A okay */
#define	CAS_TX_RAM_BIST_SM	0x000001c0	/* RAM BIST state machine */

/* RX DMA registers */
#define	CAS_RX_CONF		0x4000	/* RX configuration */
#define	CAS_RX_PSZ		0x4004	/* RX page size */
#define	CAS_RX_FIFO_WR		0x4008	/* RX FIFO write pointer */
#define	CAS_RX_FIFO_RD		0x400c	/* RX FIFO read pointer */
#define	CAS_RX_IPP_WR		0x4010	/* RX IPP FIFO write pointer */
#define	CAS_RX_IPP_SDWR		0x4014	/* RX IPP FIFO shadow write pointer */
#define	CAS_RX_IPP_RD		0x4018	/* RX IPP FIFO read pointer */
#define	CAS_RX_DEBUG		0x401c	/* RX debug */
#define	CAS_RX_PTHRS		0x4020	/* RX PAUSE threshold */
#define	CAS_RX_KICK		0x4024	/* RX kick */
#define	CAS_RX_DESC_BASE_LO	0x4028	/* RX descriptor ring base low */
#define	CAS_RX_DESC_BASE_HI	0x402c	/* RX descriptor ring base high */
#define	CAS_RX_COMP_BASE_LO	0x4030	/* RX completion ring base low */
#define	CAS_RX_COMP_BASE_HI	0x4034	/* RX completion ring base high */
#define	CAS_RX_COMP		0x4038	/* RX completion */
#define	CAS_RX_COMP_HEAD	0x403c	/* RX completion head */
#define	CAS_RX_COMP_TAIL	0x4040	/* RX completion tail */
#define	CAS_RX_BLANK		0x4044	/* RX blanking for ISR read */
#define	CAS_RX_AEMPTY_THRS	0x4048	/* RX almost empty threshold */
#define	CAS_RX_RED		0x4048	/* RX random early detection enable */
#define	CAS_RX_FF		0x4050	/* RX FIFO fullness */
#define	CAS_RX_IPP_PKT_CNT	0x4054	/* RX IPP packet counter */
#define	CAS_RX_WORKING_DMA_LO	0x4058	/* RX working DMA pointer low */
#define	CAS_RX_WORKING_DMA_HI	0x405c	/* RX working DMA pointer high */
#define	CAS_RX_BIST		0x4060	/* RX BIST */
#define	CAS_RX_CTRL_FIFO_WR	0x4064	/* RX control FIFO write pointer */
#define	CAS_RX_CTRL_FIFO_RD	0x4068	/* RX control FIFO read pointer */
#define	CAS_RX_BLANK_ALIAS	0x406c	/* RX blanking for ISR read alias */
#define	CAS_RX_FIFO_ADDR	0x4080	/* RX FIFO address */
#define	CAS_RX_FIFO_TAG		0x4084	/* RX FIFO tag */
#define	CAS_RX_FIFO_DATA_LO	0x4088	/* RX FIFO data low */
#define	CAS_RX_FIFO_DATA_HI_T0	0x408c	/* RX FIFO data highT0 */
#define	CAS_RX_FIFO_DATA_HI_T1	0x4090	/* RX FIFO data highT1 */
#define	CAS_RX_CTRL_FIFO	0x4094	/* RX control FIFO and batching FIFO */
#define	CAS_RX_CTRL_FIFO_LO	0x4098	/* RX control FIFO data low */
#define	CAS_RX_CTRL_FIFO_MD	0x409c	/* RX control FIFO data mid */
#define	CAS_RX_CTRL_FIFO_HI	0x4100	/* RX control FIFO data high, flowID */
#define	CAS_RX_IPP_ADDR		0x4104	/* RX IPP FIFO address */
#define	CAS_RX_IPP_TAG		0x4108	/* RX IPP FIFO tag */
#define	CAS_RX_IPP_DATA_LO	0x410c	/* RX IPP FIFO data low */
#define	CAS_RX_IPP_DATA_HI_T0	0x4110	/* RX IPP FIFO data highT0 */
#define	CAS_RX_IPP_DATA_HI_T1	0x4114	/* RX IPP FIFO data highT1 */
#define	CAS_RX_HDR_PAGE_LO	0x4118	/* RX header page pointer low */
#define	CAS_RX_HDR_PAGE_HIGH	0x411c	/* RX header page pointer high */
#define	CAS_RX_MTU_PAGE_LO	0x4120	/* RX MTU page pointer low */
#define	CAS_RX_MTU_PAGE_HIGH	0x4124	/* RX MTU page pointer high */
#define	CAS_RX_REAS_DMA_ADDR	0x4128	/* RX reassembly DMA table address */
#define	CAS_RX_REAS_DMA_DATA_LO	0x412c	/* RX reassembly DMA table data low */
#define	CAS_RX_REAS_DMA_DATA_MD	0x4130	/* RX reassembly DMA table data mid */
#define	CAS_RX_REAS_DMA_DATA_HI	0x4134	/* RX reassembly DMA table data high */
/* The rest of the RX DMA registers are Cassini+/Saturn only. */
#define	CAS_RX_DESC2_BASE_LO	0x4200	/* RX descriptor ring 2 base low */
#define	CAS_RX_DESC2_BASE_HI	0x4204	/* RX descriptor ring 2 base high */
#define	CAS_RX_COMP2_BASE_LO	0x4208	/* RX completion ring 2 base low */
#define	CAS_RX_COMP2_BASE_HI	0x420c	/* RX completion ring 2 base high */
#define	CAS_RX_COMP3_BASE_LO	0x4210	/* RX completion ring 3 base low */
#define	CAS_RX_COMP3_BASE_HI	0x4214	/* RX completion ring 3 base high */
#define	CAS_RX_COMP4_BASE_LO	0x4218	/* RX completion ring 4 base low */
#define	CAS_RX_COMP4_BASE_HI	0x421c	/* RX completion ring 4 base high */
#define	CAS_RX_KICK2		0x4220	/* RX kick 2 */
#define	CAS_RX_COMP2		0x4224	/* RX completion 2 */
#define	CAS_RX_COMP_HEAD2	0x4228	/* RX completion head 2 */
#define	CAS_RX_COMP_TAIL2	0x422c	/* RX completion tail 2 */
#define	CAS_RX_COMP_HEAD3	0x4230	/* RX completion head 3 */
#define	CAS_RX_COMP_TAIL3	0x4234	/* RX completion tail 3 */
#define	CAS_RX_COMP_HEAD4	0x4238	/* RX completion head 4 */
#define	CAS_RX_COMP_TAIL4	0x423c	/* RX completion tail 4 */
#define	CAS_RX_AEMPTY_THRS2	0x4048	/* RX almost empty threshold 2 */

#define	CAS_RX_CONF_RXDMA_EN	0x00000001	/* RX DMA enable */
#define	CAS_RX_CONF_DESC_MASK	0x0000001e	/* RX descriptor ring size */
#define	CAS_RX_CONF_DESC_SHFT	1
#define	CAS_RX_CONF_COMP_MASK	0x000001e0	/* RX complition ring size */
#define	CAS_RX_CONF_COMP_SHFT	5
#define	CAS_RX_CONF_BATCH_DIS	0x00000200	/* descriptor batching dis. */
#define	CAS_RX_CONF_SOFF_MASK	0x00001c00	/* swivel offset */
#define	CAS_RX_CONF_SOFF_SHFT	10
/* The RX descriptor ring 2 is Cassini+/Saturn only. */
#define	CAS_RX_CONF_DESC2_MASK	0x000f0000	/* RX descriptor ring 2 size */
#define	CAS_RX_CONF_DESC2_SHFT	16

#define	CAS_RX_CONF_COMP_128	0x0		/* 128 descriptors */
#define	CAS_RX_CONF_COMP_256	0x1		/* 256 descriptors */
#define	CAS_RX_CONF_COMP_512	0x2		/* 512 descriptors */
#define	CAS_RX_CONF_COMP_1K	0x3		/* 1k descriptors */
#define	CAS_RX_CONF_COMP_2K	0x4		/* 2k descriptors */
#define	CAS_RX_CONF_COMP_4K	0x5		/* 4k descriptors */
#define	CAS_RX_CONF_COMP_8K	0x6		/* 8k descriptors */
#define	CAS_RX_CONF_COMP_16K	0x7		/* 16k descriptors */
#define	CAS_RX_CONF_COMP_32K	0x8		/* 32k descriptors */

#define	CAS_RX_PSZ_MASK		0x00000003	/* RX page size */
#define	CAS_RX_PSZ_SHFT		0
#define	CAS_RX_PSZ_MB_CNT_MASK	0x00007800	/* number of MTU buffers */
#define	CAS_RX_PSZ_MB_CNT_SHFT	11
#define	CAS_RX_PSZ_MB_STRD_MASK	0x18000000	/* MTU buffer stride */
#define	CAS_RX_PSZ_MB_STRD_SHFT	27
#define	CAS_RX_PSZ_MB_OFF_MASK	0xc0000000	/* MTU buffer offset */
#define	CAS_RX_PSZ_MB_OFF_SHFT	30

#define	CAS_RX_PSZ_2K		0x0		/* page size 2Kbyte */
#define	CAS_RX_PSZ_4K		0x1		/* page size 4Kbyte */
#define	CAS_RX_PSZ_8K		0x2		/* page size 8Kbyte */
#define	CAS_RX_PSZ_16K		0x3		/* page size 16Kbyte*/

#define	CAS_RX_PSZ_MB_STRD_1K	0x0		/* MTU buffer stride 1Kbyte */
#define	CAS_RX_PSZ_MB_STRD_2K	0x1		/* MTU buffer stride 2Kbyte */
#define	CAS_RX_PSZ_MB_STRD_4K	0x2		/* MTU buffer stride 4Kbyte */
#define	CAS_RX_PSZ_MB_STRD_8K	0x3		/* MTU buffer stride 8Kbyte */

#define	CAS_RX_PSZ_MB_OFF_0	0x0		/* MTU buf. offset 0 bytes */
#define	CAS_RX_PSZ_MB_OFF_64	0x1		/* MTU buf. offset 64 bytes */
#define	CAS_RX_PSZ_MB_OFF_96	0x2		/* MTU buf. offset 96 bytes */
#define	CAS_RX_PSZ_MB_OFF_128	0x3		/* MTU buf. offset 128 bytes */

#define	CAS_RX_DESC_ALIGN	8192		/* RX descriptor alignment */

#define	CAS_RX_COMP_ALIGN	8192		/* RX complition alignment */

/* The RX PAUSE thresholds are specified in multiples of 64 bytes. */
#define	CAS_RX_PTHRS_XOFF_MASK	0x000001ff	/* XOFF PAUSE */
#define	CAS_RX_PTHRS_XOFF_SHFT	0
#define	CAS_RX_PTHRS_XON_MASK	0x001ff000	/* XON PAUSE */
#define	CAS_RX_PTHRS_XON_SHFT	12

/*
 * CAS_RX_BLANK and CAS_RX_BLANK_ALIAS bits
 * CAS_RX_BLANK is loaded each time CAS_STATUS is read and CAS_RX_BLANK_ALIAS
 * is read each time CAS_STATUS_ALIAS is read.  The blanking time is specified
 * in multiples of 512 core ticks (which runs at 125MHz).
 */
#define	CAS_RX_BLANK_PKTS_MASK	0x000001ff	/* RX blanking packets */
#define	CAS_RX_BLANK_PKTS_SHFT	0
#define	CAS_RX_BLANK_TIME_MASK	0x3ffff000	/* RX blanking time */
#define	CAS_RX_BLANK_TIME_SHFT	12

/* CAS_RX_AEMPTY_THRS and CAS_RX_AEMPTY_THRS2 bits */
#define	CAS_RX_AEMPTY_THRS_MASK	0x00001fff	/* RX_BUF_AEMPTY threshold */
#define	CAS_RX_AEMPTY_THRS_SHFT	0
#define	CAS_RX_AEMPTY_COMP_MASK	0x0fffe000	/* RX_COMP_AFULL threshold */
#define	CAS_RX_AEMPTY_COMP_SHFT	13

/* The RX random early detection probability is in 12.5% granularity. */
#define	CAS_RX_RED_4K_6K_MASK	0x000000ff	/* 4K < FIFO threshold < 6K */
#define	CAS_RX_RED_4K_6K_SHFT	0
#define	CAS_RX_RED_6K_8K_MASK	0x0000ff00	/* 6K < FIFO threshold < 8K */
#define	CAS_RX_RED_6K_8K_SHFT	8
#define	CAS_RX_RED_8K_10K_MASK	0x00ff0000	/* 8K < FIFO threshold < 10K */
#define	CAS_RX_RED_8K_10K_SHFT	16
#define	CAS_RX_RED_10K_12K_MASK	0xff000000	/* 10K < FIFO threshold < 12K */
#define	CAS_RX_RED_10K_12K_SHFT	24

/* CAS_RX_FF_IPP_MASK and CAS_RX_FF_FIFO_MASK are in 8 bytes granularity. */
#define	CAS_RX_FF_PKT_MASK	0x000000ff	/* # of packets in RX FIFO */
#define	CAS_RX_FF_PKT_SHFT	0
#define	CAS_RX_FF_IPP_MASK	0x0007ff00	/* IPP FIFO level */
#define	CAS_RX_FF_IPP_SHFT	8
#define	CAS_RX_FF_FIFO_MASK	0x3ff80000	/* RX FIFO level */
#define	CAS_RX_FF_FIFO_SHFT	19

#define	CAS_RX_BIST_START	0x00000001	/* Start BIST process. */
#define	CAS_RX_BIST_SUMMARY	0x00000002	/* All okay */
#define	CAS_RX_BIST_SM		0x00007800	/* BIST state machine */
#define	CAS_RX_BIST_REAS_27	0x00008000	/* Reas 27 okay */
#define	CAS_RX_BIST_REAS_26B	0x00010000	/* Reas 26B okay */
#define	CAS_RX_BIST_REAS_26A	0x00020000	/* Reas 26A okay */
#define	CAS_RX_BIST_CTRL_33	0x00040000	/* Control FIFO 33 okay */
#define	CAS_RX_BIST_CTRL_32	0x00080000	/* Control FIFO 32 okay */
#define	CAS_RX_BIST_IPP_33C	0x00100000	/* IPP 33C okay */
#define	CAS_RX_BIST_IPP_32C	0x00200000	/* IPP 32C okay */
#define	CAS_RX_BIST_IPP_33B	0x00400000	/* IPP 33B okay */
#define	CAS_RX_BIST_IPP_32B	0x00800000	/* IPP 32B okay */
#define	CAS_RX_BIST_IPP_33A	0x01000000	/* IPP 33A okay */
#define	CAS_RX_BIST_IPP_32A	0x02000000	/* IPP 32A okay */
#define	CAS_RX_BIST_33C		0x04000000	/* 33C okay */
#define	CAS_RX_BIST_32C		0x08000000	/* 32C okay */
#define	CAS_RX_BIST_33B		0x10000000	/* 33B okay */
#define	CAS_RX_BIST_32B		0x20000000	/* 32B okay */
#define	CAS_RX_BIST_33A		0x40000000	/* 33A okay */
#define	CAS_RX_BIST_32A		0x80000000	/* 32A okay */

#define	CAS_RX_REAS_DMA_ADDR_LC	0x0000003f	/* reas. table location sel. */

/* header parser registers */
#define	CAS_HP_CONF		0x4140	/* HP configuration */
#define	CAS_HP_IR_ADDR		0x4144	/* HP instruction RAM address */
#define	CAS_HP_IR_DATA_LO	0x4148	/* HP instruction RAM data low */
#define	CAS_HP_IR_DATA_MD	0x414c	/* HP instruction RAM data mid */
#define	CAS_HP_IR_DATA_HI	0x4150	/* HP instruction RAM data high */
#define	CAS_HP_DR_FDB		0x4154	/* HP data RAM and flow DB address */
#define	CAS_HP_DR_DATA		0x4158	/* HP data RAM data */
#define	CAS_HP_FLOW_DB1		0x415c	/* HP flow database 1 */
#define	CAS_HP_FLOW_DB2		0x4160	/* HP flow database 2 */
#define	CAS_HP_FLOW_DB3		0x4164	/* HP flow database 3 */
#define	CAS_HP_FLOW_DB4		0x4168	/* HP flow database 4 */
#define	CAS_HP_FLOW_DB5		0x416c	/* HP flow database 5 */
#define	CAS_HP_FLOW_DB6		0x4170	/* HP flow database 6 */
#define	CAS_HP_FLOW_DB7		0x4174	/* HP flow database 7 */
#define	CAS_HP_FLOW_DB8		0x4178	/* HP flow database 8 */
#define	CAS_HP_FLOW_DB9		0x417c	/* HP flow database 9 */
#define	CAS_HP_FLOW_DB10	0x4180	/* HP flow database 10 */
#define	CAS_HP_FLOW_DB11	0x4184	/* HP flow database 11 */
#define	CAS_HP_FLOW_DB12	0x4188	/* HP flow database 12 */
#define	CAS_HP_SM		0x418c	/* HP state machine */
#define	CAS_HP_STATUS1		0x4190	/* HP status 1 */
#define	CAS_HP_STATUS2		0x4194	/* HP status 2 */
#define	CAS_HP_STATUS3		0x4198	/* HP status 3 */
#define	CAS_HP_RAM_BIST		0x419c	/* HP RAM BIST */

#define	CAS_HP_CONF_PARSE_EN	0x00000001	/* header parsing enable */
#define	CAS_HP_CONF_NCPU_MASK	0x000000fc	/* #CPUs (0x0: 64) */
#define	CAS_HP_CONF_NCPU_SHFT	2
#define	CAS_HP_CONF_SINC_DIS	0x00000100	/* SYN inc. seq. number dis. */
#define	CAS_HP_CONF_TPT_MASK	0x000ffe00	/* TCP payload threshold */
#define	CAS_HP_CONF_TPT_SHFT	9

#define	CAS_HP_DR_FDB_DR_MASK	0x0000001f	/* data RAM location sel. */
#define	CAS_HP_DR_FDB_DR_SHFT	0
#define	CAS_HP_DR_FDB_FDB_MASK	0x00003f00	/* flow DB location sel. */
#define	CAS_HP_DR_FDB_FDB_SHFT	8

#define	CAS_HP_STATUS1_OP_MASK	0x00000007	/* HRP opcode */
#define	CAS_HP_STATUS1_OP_SHFT	0
#define	CAS_HP_STATUS1_LB_MASK	0x000001f8	/* load balancing CPU number */
#define	CAS_HP_STATUS1_LB_SHFT	3
#define	CAS_HP_STATUS1_L3O_MASK	0x0000fe00	/* layer 3 offset */
#define	CAS_HP_STATUS1_L3O_SHFT	9
#define	CAS_HP_STATUS1_SAP_MASK	0xffff0000	/* ethertype */
#define	CAS_HP_STATUS1_SAP_SHFT	16

#define	CAS_HP_STATUS2_TSZ_MASK	0x0000ffff	/* TCP payload size */
#define	CAS_HP_STATUS2_TSZ_SHFT	0
#define	CAS_HP_STATUS2_TO_MASK	0x007f0000	/* TCP payload offset */
#define	CAS_HP_STATUS2_TO_SHFT	16
#define	CAS_HP_STATUS2_FID_MASK	0x1f800000	/* flow ID */
#define	CAS_HP_STATUS2_FID_SHFT	23
#define	CAS_HP_STATUS2_AR2_MASK	0xe0000000	/* accu_R2[6:4] */
#define	CAS_HP_STATUS2_AR2_SHFT	29

#define	CAS_HP_STATUS3_TCP_NCHK	0x00000001	/* TCP no payload check */
#define	CAS_HP_STATUS3_TCP_CHK	0x00000002	/* TCP payload check */
#define	CAS_HP_STATUS3_SYN_FLAG	0x00000004	/* SYN flag */
#define	CAS_HP_STATUS3_TCP_FLAG	0x00000008	/* TCP flag check */
#define	CAS_HP_STATUS3_CTRL_PF	0x00000010	/* control packet flag */
#define	CAS_HP_STATUS3_NASSIST	0x00000020	/* no assist */
#define	CAS_HP_STATUS3_MASK_PT	0x00000040	/* Mask payload threshold. */
#define	CAS_HP_STATUS3_FRC_TPC	0x00000080	/* Force TCP payload check. */
#define	CAS_HP_STATUS3_MASK_DLZ	0x00000100	/* Mask data length equal 0. */
#define	CAS_HP_STATUS3_FRC_TNPC	0x00000200	/* Force TCP no payload chk. */
#define	CAS_HP_STATUS3_JMBHS_EN	0x00000400	/* jumbo header split enable */
#define	CAS_HP_STATUS3_BWO_REAS	0x00000800	/* batching w/o reassembly */
#define	CAS_HP_STATUS3_FRC_DROP	0x00001000	/* force drop */
#define	CAS_HP_STATUS3_AR1_MASK	0x000fe000	/* accu_R1 */
#define	CAS_HP_STATUS3_AR1_SHFT	13
#define	CAS_HP_STATUS3_CSO_MASK	0x07f00000	/* checksum start offset */
#define	CAS_HP_STATUS3_CSO_SHFT	19
#define	CAS_HP_STATUS3_AR2_MASK	0xf0000000	/* accu_R2[3:0] */
#define	CAS_HP_STATUS3_AR2_SHFT	28

#define	CAS_HP_RAM_BIST_START	0x00000001	/* Start RAM BIST process. */
#define	CAS_HP_RAM_BIST_SUMMARY	0x00000002	/* all RAM okay */
#define	CAS_HP_RAM_BIST_TCPSEQ	0x00020000	/* TCP seqeunce RAM okay */
#define	CAS_HP_RAM_BIST_FID31	0x00040000	/* flow ID RAM3 bank 1 okay */
#define	CAS_HP_RAM_BIST_FID21	0x00080000	/* flow ID RAM2 bank 1 okay */
#define	CAS_HP_RAM_BIST_FID11	0x00100000	/* flow ID RAM1 bank 1 okay */
#define	CAS_HP_RAM_BIST_FID01	0x00200000	/* flow ID RAM0 bank 1 okay */
#define	CAS_HP_RAM_BIST_FID30	0x00400000	/* flow ID RAM3 bank 0 okay */
#define	CAS_HP_RAM_BIST_FID20	0x00800000	/* flow ID RAM2 bank 0 okay */
#define	CAS_HP_RAM_BIST_FID10	0x01000000	/* flow ID RAM1 bank 0 okay */
#define	CAS_HP_RAM_BIST_FID00	0x02000000	/* flow ID RAM0 bank 0 okay */
#define	CAS_HP_RAM_BIST_AGE1	0x04000000	/* aging RAM1 okay */
#define	CAS_HP_RAM_BIST_AGE0	0x08000000	/* aging RAM0 okay */
#define	CAS_HP_RAM_BIST_IR2	0x10000000	/* instruction RAM2 okay */
#define	CAS_HP_RAM_BIST_IR1	0x20000000	/* instruction RAM1 okay */
#define	CAS_HP_RAM_BIST_IR0	0x40000000	/* instruction RAM0 okay */
#define	CAS_HP_RAM_BIST_DR	0x80000000	/* data RAM okay */

/* MAC registers */
#define	CAS_MAC_TXRESET		0x6000	/* TX MAC software reset command */
#define	CAS_MAC_RXRESET		0x6004	/* RX MAC software reset command */
#define	CAS_MAC_SPC		0x6008	/* send PAUSE command */
#define	CAS_MAC_TX_STATUS	0x6010	/* TX MAC status */
#define	CAS_MAC_RX_STATUS	0x6014	/* RX MAC status */
#define	CAS_MAC_CTRL_STATUS	0x6018	/* MAC control status */
#define	CAS_MAC_TX_MASK		0x6020	/* TX MAC mask */
#define	CAS_MAC_RX_MASK		0x6024	/* RX MAC mask */
#define	CAS_MAC_CTRL_MASK	0x6028	/* MAC control mask */
#define	CAS_MAC_TX_CONF		0x6030	/* TX MAC configuration */
#define	CAS_MAC_RX_CONF		0x6034	/* RX MAC configuration */
#define	CAS_MAC_CTRL_CONF	0x6038	/* MAC control configuration */
#define	CAS_MAC_XIF_CONF	0x603c	/* XIF configuration */
#define	CAS_MAC_IPG0		0x6040	/* inter packet gap 0 */
#define	CAS_MAC_IPG1		0x6044	/* inter packet gap 1 */
#define	CAS_MAC_IPG2		0x6048	/* inter packet gap 2 */
#define	CAS_MAC_SLOT_TIME	0x604c	/* slot time */
#define	CAS_MAC_MIN_FRAME	0x6050	/* minimum frame size */
#define	CAS_MAC_MAX_BF		0x6054	/* maximum bust and frame size */
#define	CAS_MAC_PREAMBLE_LEN	0x6058	/* PA size */
#define	CAS_MAC_JAM_SIZE	0x605c	/* jam size */
#define	CAS_MAC_ATTEMPT_LIMIT	0x6060	/* attempt limit */
#define	CAS_MAC_CTRL_TYPE	0x6064	/* MAC control type */
#define	CAS_MAC_ADDR0		0x6080	/* MAC address 0 */
#define	CAS_MAC_ADDR1		0x6084	/* MAC address 1 */
#define	CAS_MAC_ADDR2		0x6088	/* MAC address 2 */
#define	CAS_MAC_ADDR3		0x608c	/* MAC address 3 */
#define	CAS_MAC_ADDR4		0x6090	/* MAC address 4 */
#define	CAS_MAC_ADDR5		0x6094	/* MAC address 5 */
#define	CAS_MAC_ADDR6		0x6098	/* MAC address 6 */
#define	CAS_MAC_ADDR7		0x609c	/* MAC address 7 */
#define	CAS_MAC_ADDR8		0x60a0	/* MAC address 8 */
#define	CAS_MAC_ADDR9		0x60a4	/* MAC address 9 */
#define	CAS_MAC_ADDR10		0x60a8	/* MAC address 10 */
#define	CAS_MAC_ADDR11		0x60ac	/* MAC address 11 */
#define	CAS_MAC_ADDR12		0x60b0	/* MAC address 12 */
#define	CAS_MAC_ADDR13		0x60b4	/* MAC address 13 */
#define	CAS_MAC_ADDR14		0x60b8	/* MAC address 14 */
#define	CAS_MAC_ADDR15		0x60bc	/* MAC address 15 */
#define	CAS_MAC_ADDR16		0x60c0	/* MAC address 16 */
#define	CAS_MAC_ADDR17		0x60c4	/* MAC address 17 */
#define	CAS_MAC_ADDR18		0x60c8	/* MAC address 18 */
#define	CAS_MAC_ADDR19		0x60cc	/* MAC address 19 */
#define	CAS_MAC_ADDR20		0x60d0	/* MAC address 20 */
#define	CAS_MAC_ADDR21		0x60d4	/* MAC address 21 */
#define	CAS_MAC_ADDR22		0x60d8	/* MAC address 22 */
#define	CAS_MAC_ADDR23		0x60dc	/* MAC address 23 */
#define	CAS_MAC_ADDR24		0x60e0	/* MAC address 24 */
#define	CAS_MAC_ADDR25		0x60e4	/* MAC address 25 */
#define	CAS_MAC_ADDR26		0x60e8	/* MAC address 26 */
#define	CAS_MAC_ADDR27		0x60ec	/* MAC address 27 */
#define	CAS_MAC_ADDR28		0x60f0	/* MAC address 28 */
#define	CAS_MAC_ADDR29		0x60f4	/* MAC address 29 */
#define	CAS_MAC_ADDR30		0x60f8	/* MAC address 30 */
#define	CAS_MAC_ADDR31		0x60fc	/* MAC address 31 */
#define	CAS_MAC_ADDR32		0x6100	/* MAC address 32 */
#define	CAS_MAC_ADDR33		0x6104	/* MAC address 33 */
#define	CAS_MAC_ADDR34		0x6108	/* MAC address 34 */
#define	CAS_MAC_ADDR35		0x610c	/* MAC address 35 */
#define	CAS_MAC_ADDR36		0x6110	/* MAC address 36 */
#define	CAS_MAC_ADDR37		0x6114	/* MAC address 37 */
#define	CAS_MAC_ADDR38		0x6118	/* MAC address 38 */
#define	CAS_MAC_ADDR39		0x611c	/* MAC address 39 */
#define	CAS_MAC_ADDR40		0x6120	/* MAC address 40 */
#define	CAS_MAC_ADDR41		0x6124	/* MAC address 41 */
#define	CAS_MAC_ADDR42		0x6128	/* MAC address 42 */
#define	CAS_MAC_ADDR43		0x612c	/* MAC address 43 */
#define	CAS_MAC_ADDR44		0x6130	/* MAC address 44 */
#define	CAS_MAC_AFILTER0	0x614c	/* address filter 0 */
#define	CAS_MAC_AFILTER1	0x6150	/* address filter 1 */
#define	CAS_MAC_AFILTER2	0x6154	/* address filter 2 */
#define	CAS_MAC_AFILTER_MASK1_2	0x6158	/* address filter 2 & 1 mask*/
#define	CAS_MAC_AFILTER_MASK0	0x615c	/* address filter 0 mask */
#define	CAS_MAC_HASH0		0x6160	/* hash table 0 */
#define	CAS_MAC_HASH1		0x6164	/* hash table 1 */
#define	CAS_MAC_HASH2		0x6168	/* hash table 2 */
#define	CAS_MAC_HASH3		0x616c	/* hash table 3 */
#define	CAS_MAC_HASH4		0x6170	/* hash table 4 */
#define	CAS_MAC_HASH5		0x6174	/* hash table 5 */
#define	CAS_MAC_HASH6		0x6178	/* hash table 6 */
#define	CAS_MAC_HASH7		0x617c	/* hash table 7 */
#define	CAS_MAC_HASH8		0x6180	/* hash table 8 */
#define	CAS_MAC_HASH9		0x6184	/* hash table 9 */
#define	CAS_MAC_HASH10		0x6188	/* hash table 10 */
#define	CAS_MAC_HASH11		0x618c	/* hash table 11 */
#define	CAS_MAC_HASH12		0x6190	/* hash table 12 */
#define	CAS_MAC_HASH13		0x6194	/* hash table 13 */
#define	CAS_MAC_HASH14		0x6198	/* hash table 14 */
#define	CAS_MAC_HASH15		0x619c	/* hash table 15 */
#define	CAS_MAC_NORM_COLL_CNT	0x61a0	/* normal collision counter */
#define	CAS_MAC_FIRST_COLL_CNT	0x61a4	/* 1st attempt suc. collision counter */
#define	CAS_MAC_EXCESS_COLL_CNT	0x61a8	/* excess collision counter */
#define	CAS_MAC_LATE_COLL_CNT	0x61ac	/* late collision counter */
#define	CAS_MAC_DEFER_TMR_CNT	0x61b0	/* defer timer */
#define	CAS_MAC_PEAK_ATTEMPTS	0x61b4	/* peak attempts */
#define	CAS_MAC_RX_FRAME_COUNT	0x61b8	/* receive frame counter */
#define	CAS_MAC_RX_LEN_ERR_CNT	0x61bc	/* length error counter */
#define	CAS_MAC_RX_ALIGN_ERR	0x61c0	/* alignment error counter */
#define	CAS_MAC_RX_CRC_ERR_CNT	0x61c4	/* FCS error counter */
#define	CAS_MAC_RX_CODE_VIOL	0x61c8	/* RX code violation error counter */
#define	CAS_MAC_RANDOM_SEED	0x61cc	/* random number seed */
#define	CAS_MAC_MAC_STATE	0x61d0	/* MAC state machine */

#define	CAS_MAC_SPC_TIME_MASK	0x0000ffff	/* PAUSE time value */
#define	CAS_MAC_SPC_TIME_SHFT	0
#define	CAS_MAC_SPC_SEND	0x00010000	/* Send PAUSE frame. */

/* CAS_MAC_TX_STATUS and CAS_MAC_TX_MASK register bits */
#define	CAS_MAC_TX_FRAME_XMTD	0x00000001	/* Frame transmitted. */
#define	CAS_MAC_TX_UNDERRUN	0x00000002	/* TX data starvation */
#define	CAS_MAC_TX_MAX_PKT_ERR	0x00000004	/* frame > CAS_MAC_MAX_FRAME */
#define	CAS_MAC_TX_NCC_EXP	0x00000008	/* normal coll. counter wrap */
#define	CAS_MAC_TX_ECC_EXP	0x00000010	/* excess coll. counter wrap */
#define	CAS_MAC_TX_LCC_EXP	0x00000020	/* late coll. counter wrap */
#define	CAS_MAC_TX_FCC_EXP	0x00000040	/* 1st coll. counter wrap */
#define	CAS_MAC_TX_DEFER_EXP	0x00000080	/* defer timer wrap */
#define	CAS_MAC_TX_PEAK_EXP	0x00000100	/* peak attempts counter wrap */

/* CAS_MAC_RX_STATUS and CAS_MAC_RX_MASK register bits */
#define	CAS_MAC_RX_FRAME_RCVD	0x00000001	/* Frame received. */
#define	CAS_MAC_RX_OVERFLOW	0x00000002	/* RX FIFO overflow */
#define	CAS_MAC_RX_FRAME_EXP	0x00000004	/* RX frame counter wrap */
#define	CAS_MAC_RX_ALIGN_EXP	0x00000008	/* alignment error cntr. wrap */
#define	CAS_MAC_RX_CRC_EXP	0x00000010	/* CRC error counter wrap */
#define	CAS_MAC_RX_LEN_EXP	0x00000020	/* length error counter wrap */
#define	CAS_MAC_RX_VIOL_EXP	0x00000040	/* code violation cntr. wrap */

/* CAS_MAC_CTRL_STATUS and CAS_MAC_CTRL_MASK register bits */
#define	CAS_MAC_CTRL_PAUSE_RCVD	0x00000001	/* PAUSE received. */
#define	CAS_MAC_CTRL_PAUSE	0x00000002	/* PAUSE state entered. */
#define	CAS_MAC_CTRL_NON_PAUSE	0x00000004	/* PAUSE state left. */

#define	CAS_MAC_CTRL_STATUS_PT_MASK	0xffff0000	/* PAUSE time */
#define	CAS_MAC_CTRL_STATUS_PT_SHFT	16

#define	CAS_MAC_TX_CONF_EN	0x00000001	/* TX enable */
#define	CAS_MAC_TX_CONF_ICARR	0x00000002	/* Ignore carrier sense. */
#define	CAS_MAC_TX_CONF_ICOLLIS	0x00000004	/* Ignore collisions. */
#define	CAS_MAC_TX_CONF_EN_IPG0	0x00000008	/* extend RX-to-TX IPG */
#define	CAS_MAC_TX_CONF_NGU	0x00000010	/* Never give up. */
#define	CAS_MAC_TX_CONF_NGUL	0x00000020	/* never give up limit */
#define	CAS_MAC_TX_CONF_NBOFF	0x00000040	/* Disable backoff algorithm. */
#define	CAS_MAC_TX_CONF_SDOWN	0x00000080	/* CSMA/CD slow down */
#define	CAS_MAC_TX_CONF_NO_FCS	0x00000100	/* Don't generate FCS. */
#define	CAS_MAC_TX_CONF_CARR	0x00000200	/* carrier extension enable */

#define	CAS_MAC_RX_CONF_EN	0x00000001	/* RX enable */
#define	CAS_MAC_RX_CONF_STRPPAD	0x00000002	/* Must not be set. */
#define	CAS_MAC_RX_CONF_STRPFCS	0x00000004	/* Strip FCS bytes. */
#define	CAS_MAC_RX_CONF_PROMISC	0x00000008	/* promiscuous mode enable */
#define	CAS_MAC_RX_CONF_PGRP	0x00000010	/* promiscuous group mode en. */
#define	CAS_MAC_RX_CONF_HFILTER	0x00000020	/* hash filter enable */
#define	CAS_MAC_RX_CONF_AFILTER	0x00000040	/* address filter enable */
#define	CAS_MAC_RX_CONF_DIS_DOE	0x00000080	/* disable discard on error */
#define	CAS_MAC_RX_CONF_CARR	0x00000100	/* carrier extension enable */

#define	CAS_MAC_CTRL_CONF_TXP	0x00000001	/* send PAUSE enable */
#define	CAS_MAC_CTRL_CONF_RXP	0x00000002	/* receive PAUSE enable */
#define	CAS_MAC_CTRL_CONF_PASSP	0x00000004	/* Pass PAUSE up to RX DMA. */

#define	CAS_MAC_XIF_CONF_TX_OE	0x00000001	/* MII TX output drivers en. */
#define	CAS_MAC_XIF_CONF_ILBK	0x00000002	/* MII internal loopback en. */
#define	CAS_MAC_XIF_CONF_NOECHO	0x00000004	/* Disable echo. */
#define	CAS_MAC_XIF_CONF_GMII	0x00000008	/* GMII (vs. MII) mode enable */
#define	CAS_MAC_XIF_CONF_BUF_OE	0x00000010	/* MII_BUF_OE enable */
#define	CAS_MAC_XIF_CONF_LNKLED	0x00000020	/* Force LINKLED# active. */
#define	CAS_MAC_XIF_CONF_FDXLED	0x00000040	/* Force FDPLXLED# active. */

/*
 * The value of CAS_MAC_SLOT_TIME specifies the PAUSE time unit and depends
 * on whether carrier extension is enabled.
 */
#define	CAS_MAC_SLOT_TIME_CARR	0x200		/* slot time for carr. ext. */
#define	CAS_MAC_SLOT_TIME_NORM	0x40		/* slot time otherwise */

#define	CAS_MAC_MAX_BF_FRM_MASK	0x00007fff	/* maximum frame size */
#define	CAS_MAC_MAX_BF_FRM_SHFT	0
#define	CAS_MAC_MAX_BF_BST_MASK	0x3fff0000	/* maximum burst size */
#define	CAS_MAC_MAX_BF_BST_SHFT	16

/*
 * MIF registers
 * The bit-bang registers use the low bit only.
 */
#define	CAS_MIF_BB_CLOCK	0x6200	/* MIF bit-bang clock */
#define	CAS_MIF_BB_DATA		0x6204	/* MIF bit-bang data */
#define	CAS_MIF_BB_OUTPUT_EN	0x6208	/* MIF bit-bang output enable */
#define	CAS_MIF_FRAME		0x620c	/* MIF frame/output */
#define	CAS_MIF_CONF		0x6210	/* MIF configuration */
#define	CAS_MIF_MASK		0x6214	/* MIF mask */
#define	CAS_MIF_STATUS		0x6218	/* MIF status */
#define	CAS_MIF_SM		0x621c	/* MIF state machine */

#define	CAS_MIF_FRAME_DATA	0x0000ffff	/* instruction payload */
#define	CAS_MIF_FRAME_TA_LSB	0x00010000	/* turn around LSB */
#define	CAS_MIF_FRAME_TA_MSB	0x00020000	/* turn around MSB */
#define	CAS_MIF_FRAME_REG_MASK	0x007c0000	/* register address */
#define	CAS_MIF_FRAME_REG_SHFT	18
#define	CAS_MIF_FRAME_PHY_MASK	0x0f800000	/* PHY address */
#define	CAS_MIF_FRAME_PHY_SHFT	23
#define	CAS_MIF_FRAME_OP_WRITE	0x10000000	/* write opcode */
#define	CAS_MIF_FRAME_OP_READ	0x20000000	/* read opcode */
#define	CAS_MIF_FRAME_OP_MASK						\
	(CAS_MIF_FRAME_OP_WRITE | CAS_MIF_FRAME_OP_READ)
#define	CAS_MIF_FRAME_ST	0x40000000	/* start of frame */
#define	CAS_MIF_FRAME_ST_MASK	0xc0000000	/* start of frame */

#define	CAS_MIF_FRAME_READ						\
	(CAS_MIF_FRAME_TA_MSB | CAS_MIF_FRAME_OP_READ | CAS_MIF_FRAME_ST)
#define	CAS_MIF_FRAME_WRITE						\
	(CAS_MIF_FRAME_TA_MSB | CAS_MIF_FRAME_OP_WRITE | CAS_MIF_FRAME_ST)

#define	CAS_MIF_CONF_PHY_SELECT	0x00000001	/* PHY select, 0: MDIO_0 */
#define	CAS_MIF_CONF_POLL_EN	0x00000002	/* polling mechanism enable */
#define	CAS_MIF_CONF_BB_MODE	0x00000004	/* bit-bang mode enable */
#define	CAS_MIF_CONF_PREG_MASK	0x000000f8	/* polled register */
#define	CAS_MIF_CONF_PREG_SHFT	3
#define	CAS_MIF_CONF_MDI0	0x00000100	/* MDIO_0 data/attached */
#define	CAS_MIF_CONF_MDI1	0x00000200	/* MDIO_1 data/attached */
#define	CAS_MIF_CONF_PPHY_MASK	0x00007c00	/* polled PHY */
#define	CAS_MIF_CONF_PPHY_SHFT	10

/* CAS_MIF_MASK and CAS_MIF_STATUS bits */
#define	CAS_MIF_POLL_STATUS_MASK	0x0000ffff	/* polling status */
#define	CAS_MIF_POLL_STATUS_SHFT	0
#define	CAS_MIF_POLL_DATA_MASK		0xffff0000	/* polling data */
#define	CAS_MIF_POLL_DATA_SHFT		8

#define	CAS_MIF_SM_CTRL_MASK	0x00000007	/* ctrl. state machine state */
#define	CAS_MIF_SM_CTRL_SHFT	0
#define	CAS_MIF_SM_EXEC_MASK	0x00000060	/* exec. state machine state */

/* PCS/Serialink registers */
#define	CAS_PCS_CTRL		0x9000	/* PCS MII control (PCS "BMCR") */
#define	CAS_PCS_STATUS		0x9004	/* PCS MII status (PCS "BMSR") */
#define	CAS_PCS_ANAR		0x9008	/* PCS MII advertisement */
#define	CAS_PCS_ANLPAR		0x900c	/* PCS MII link partner ability */
#define	CAS_PCS_CONF		0x9010	/* PCS configuration */
#define	CAS_PCS_SM		0x9014	/* PCS state machine */
#define	CAS_PCS_INTR_STATUS	0x9018	/* PCS interrupt status */
#define	CAS_PCS_DATAPATH	0x9050	/* datapath mode */
#define	CAS_PCS_SERDES_CTRL	0x9054	/* SERDES control */
#define	CAS_PCS_OUTPUT_SELECT	0x9058	/* shared output select */
#define	CAS_PCS_SERDES_STATUS	0x905c	/* SERDES state */
#define	CAS_PCS_PKT_CNT		0x9060	/* PCS packet counter */

#define	CAS_PCS_CTRL_1000M	0x00000040	/* 1000Mbps speed select */
#define	CAS_PCS_CTRL_COLL_TEST	0x00000080	/* collision test */
#define	CAS_PCS_CTRL_FDX	0x00000100	/* full-duplex, always 0 */
#define	CAS_PCS_CTRL_RANEG	0x00000200	/* restart auto-negotiation */
#define	CAS_PCS_CTRL_ISOLATE	0x00000400	/* isolate PHY from MII */
#define	CAS_PCS_CTRL_POWERDOWN	0x00000800	/* power down */
#define	CAS_PCS_CTRL_ANEG_EN	0x00001000	/* auto-negotiation enable */
#define	CAS_PCS_CTRL_10_100M	0x00002000	/* 10/100Mbps speed select */
#define	CAS_PCS_CTRL_RESET	0x00008000	/* Reset PCS. */

#define	CAS_PCS_STATUS_EXTCAP	0x00000001	/* extended capability */
#define	CAS_PCS_STATUS_JABBER	0x00000002	/* jabber condition detected */
#define	CAS_PCS_STATUS_LINK	0x00000004	/* link status */
#define	CAS_PCS_STATUS_ANEG_ABL	0x00000008	/* auto-negotiation ability */
#define	CAS_PCS_STATUS_REM_FLT	0x00000010	/* remote fault detected */
#define	CAS_PCS_STATUS_ANEG_CPT	0x00000020	/* auto-negotiate complete */
#define	CAS_PCS_STATUS_EXTENDED	0x00000100	/* extended status */

/* CAS_PCS_ANAR and CAS_PCS_ANLPAR register bits */
#define	CAS_PCS_ANEG_FDX	0x00000020	/* full-duplex */
#define	CAS_PCS_ANEG_HDX	0x00000040	/* half-duplex */
#define	CAS_PCS_ANEG_PAUSE	0x00000080	/* symmetric PAUSE */
#define	CAS_PCS_ANEG_ASM_DIR	0x00000100	/* asymmetric PAUSE */
#define	CAS_PCS_ANEG_RFLT_FAIL	0x00001000	/* remote fault - fail */
#define	CAS_PCS_ANEG_RFLT_OFF	0x00002000	/* remote fault - off-line */
#define	CAS_PCS_ANEG_RFLT_MASK						\
	(CAS_PCS_ANEG_RFLT_FAIL | CAS_PCS_ANEG_RFLT_OFF)
#define	CAS_PCS_ANEG_ACK	0x00004000	/* acknowledge */
#define	CAS_PCS_ANEG_NEXT_PAGE	0x00008000	/* next page */

#define	CAS_PCS_CONF_EN		0x00000001	/* Enable PCS. */
#define	CAS_PCS_CONF_SDO	0x00000002	/* signal detect override */
#define	CAS_PCS_CONF_SDL	0x00000004	/* signal detect active-low */
#define	CAS_PCS_CONF_JS_NORM	0x00000000	/* jitter study - normal op. */
#define	CAS_PCS_CONF_JS_HF	0x00000008	/* jitter study - HF test */
#define	CAS_PCS_CONF_JS_LF	0x00000010	/* jitter study - LF test */
#define	CAS_PCS_CONF_JS_MASK	(CAS_PCS_CONF_JS_HF | CAS_PCS_CONF_JS_LF)
#define	CAS_PCS_CONF_ANEG_TO	0x00000020	/* auto-neg. timer override */

#define	CAS_PCS_SM_TX_CTRL_MASK	0x0000000f	/* TX control state */
#define	CAS_PCS_SM_TX_CTRL_SHFT	0
#define	CAS_PCS_SM_RX_CTRL_MASK	0x000000f0	/* RX control state */
#define	CAS_PCS_SM_RX_CTRL_SHFT	4
#define	CAS_PCS_SM_WSYNC_MASK	0x00000700	/* word sync. state */
#define	CAS_PCS_SM_WSYNC_SHFT	8
#define	CAS_PCS_SM_SEQ_MASK	0x00001800	/* sequence detection state */
#define	CAS_PCS_SM_SEQ_SHFT	11
#define	CAS_PCS_SM_LINK_UP	0x00016000
#define	CAS_PCS_SM_LINK_MASK	0x0001e000	/* link configuration state */
#define	CAS_PCS_SM_LINK_SHFT	13
#define	CAS_PCS_SM_LOSS_C	0x00100000	/* link-loss due to C codes */
#define	CAS_PCS_SM_LOSS_SYNC	0x00200000	/* link-loss due to sync-loss */
#define	CAS_PCS_SM_LOS		0x00400000	/* loss of signal */
#define	CAS_PCS_SM_NLINK_BREAK	0x01000000	/* no link due to breaklink */
#define	CAS_PCS_SM_NLINK_SERDES	0x02000000	/* no link due to SERDES */
#define	CAS_PCS_SM_NLINK_C	0x04000000	/* no link due to bad C codes */
#define	CAS_PCS_SM_NLINK_SYNC	0x08000000	/* no link due to word sync. */
#define	CAS_PCS_SM_NLINK_WAIT_C	0x10000000	/* no link, waiting for ack. */
#define	CAS_PCS_SM_NLINK_NIDLE	0x20000000	/* no link due to no idle */

/*
 * CAS_PCS_INTR_STATUS has no corresponding mask register.  It can only
 * be masked with CAS_INTR_PCS_INT.
 */
#define	CAS_PCS_INTR_LINK	0x00000004	/* link status change */

#define	CAS_PCS_DATAPATH_MII	0x00000001	/* GMII/MII and MAC loopback */
#define	CAS_PCS_DATAPATH_SERDES	0x00000002	/* SERDES via 10-bit */

#define	CAS_PCS_SERDES_CTRL_LBK	0x00000001	/* loopback at 10-bit enable */
#define	CAS_PCS_SERDES_CTRL_ESD	0x00000002	/* En. sync char. detection. */
#define	CAS_PCS_SERDES_CTRL_LR	0x00000004	/* Lock to reference clock. */

#define	CAS_PCS_SERDES_STATUS_T	0x00000000	/* Undergoing test. */
#define	CAS_PCS_SERDES_STATUS_L	0x00000001	/* Waiting 500us w/ lockrefn. */
#define	CAS_PCS_SERDES_STATUS_C	0x00000002	/* Waiting for comma detect. */
#define	CAS_PCS_SERDES_STATUS_S	0x00000003	/* Receive data is sync. */

#define	CAS_PCS_PKT_CNT_TX_MASK	0x000007ff	/* TX packets */
#define	CAS_PCS_PKT_CNT_TX_SHFT	0
#define	CAS_PCS_PKT_CNT_RX_MASK	0x07ff0000	/* RX packets */
#define	CAS_PCS_PKT_CNT_RX_SHFT	16

/*
 * PCI expansion ROM runtime access
 * Cassinis and Saturn map a 1MB space for the PCI expansion ROM as the
 * second half of the first register bank, although they only support up
 * to 64KB ROMs.
 */
#define	CAS_PCI_ROM_OFFSET	0x100000
#define	CAS_PCI_ROM_SIZE	0x10000

/* secondary local bus device */
#define	CAS_SEC_LBDEV_OFFSET	0x180000
#define	CAS_SEC_LBDE_SIZE	0x7ffff

/* wired PHY addresses */
#define	CAS_PHYAD_INTERNAL	1
#define	CAS_PHYAD_EXTERNAL	0

/* wired RX FIFO size in bytes */
#define	CAS_RX_FIFO_SIZE	16 * 1024

/*
 * descriptor ring structures
 */
struct cas_desc {
	uint64_t	cd_flags;
	uint64_t	cd_buf_ptr;
};

/*
 * transmit flags
 * CAS_TD_CKSUM_START_MASK, CAS_TD_CKSUM_STUFF_MASK, CAS_TD_CKSUM_EN and
 * CAS_TD_INT_ME only need to be set in 1st descriptor of a frame.
 */
#define	CAS_TD_BUF_LEN_MASK	0x0000000000003fffULL	/* buffer length */
#define	CAS_TD_BUF_LEN_SHFT	0
#define	CAS_TD_CKSUM_START_MASK	0x00000000001f8000ULL	/* checksum start... */
#define	CAS_TD_CKSUM_START_SHFT	15			/* ...offset */
#define	CAS_TD_CKSUM_STUFF_MASK	0x000000001fe00000ULL	/* checksum stuff... */
#define	CAS_TD_CKSUM_STUFF_SHFT	21			/* ...offset */
#define	CAS_TD_CKSUM_EN		0x0000000020000000ULL	/* checksum enable */
#define	CAS_TD_END_OF_FRAME	0x0000000040000000ULL	/* last desc. of pkt. */
#define	CAS_TD_START_OF_FRAME	0x0000000080000000ULL	/* 1st desc. of pkt. */
#define	CAS_TD_INT_ME		0x0000000100000000ULL	/* intr. when in FIFO */
#define	CAS_TD_NO_CRC		0x0000000200000000ULL	/* Don't insert CRC. */

/* receive flags */
#define	CAS_RD_BUF_INDEX_MASK	0x0000000000003fffULL	/* data buffer index */
#define	CAS_RD_BUF_INDEX_SHFT	0

/*
 * receive completion ring structure
 */
struct cas_rx_comp {
	uint64_t	crc_word1;
	uint64_t	crc_word2;
	uint64_t	crc_word3;
	uint64_t	crc_word4;
};

#define	CAS_RC1_DATA_SIZE_MASK	0x0000000007ffe000ULL	/* pkt. data length */
#define	CAS_RC1_DATA_SIZE_SHFT	13
#define	CAS_RC1_DATA_OFF_MASK	0x000001fff8000000ULL	/* data buffer offset */
#define	CAS_RC1_DATA_OFF_SHFT	27
#define	CAS_RC1_DATA_INDEX_MASK	0x007ffe0000000000ULL	/* data buffer index */
#define	CAS_RC1_DATA_INDEX_SHFT	41
#define	CAS_RC1_SKIP_MASK	0x0180000000000000ULL	/* entries to skip */
#define	CAS_RC1_SKIP_SHFT	55
#define	CAS_RC1_RELEASE_NEXT	0x0200000000000000ULL	/* last in reas. buf. */
#define	CAS_RC1_SPLIT_PKT	0x0400000000000000ULL	/* used 2 reas. buf. */
#define	CAS_RC1_RELEASE_FLOW	0x0800000000000000ULL	/* last pkt. of flow */
#define	CAS_RC1_RELEASE_DATA	0x1000000000000000ULL	/* reas. buf. full */
#define	CAS_RC1_RELEASE_HDR	0x2000000000000000ULL	/* header buf. full */
#define	CAS_RC1_TYPE_HW		0x0000000000000000ULL	/* owned by hardware */
#define	CAS_RC1_TYPE_RSFB	0x4000000000000000ULL	/* stale flow buf... */
#define	CAS_RC1_TYPE_RNRP	0x8000000000000000ULL	/* non-reas. pkt... */
#define	CAS_RC1_TYPE_RFP	0xc000000000000000ULL	/* flow packet... */
#define	CAS_RC1_TYPE_MASK	CAS_RC1_TYPE_RFP	/* ...release */
#define	CAS_RC1_TYPE_SHFT	62

#define	CAS_RC2_NEXT_INDEX_MASK	0x00000007ffe00000ULL	/* next buf. of pkt. */
#define	CAS_RC2_NEXT_INDEX_SHFT	21
#define	CAS_RC2_HDR_SIZE_MASK	0x00000ff800000000ULL	/* header length */
#define	CAS_RC2_HDR_SIZE_SHFT	35
#define	CAS_RC2_HDR_OFF_MASK	0x0003f00000000000ULL	/* header buf. offset */
#define	CAS_RC2_HDR_OFF_SHFT	44
#define	CAS_RC2_HDR_INDEX_MASK	0xfffc000000000000ULL	/* header buf. index */
#define	CAS_RC2_HDR_INDEX_SHFT	50

#define	CAS_RC3_SMALL_PKT	0x0000000000000001ULL	/* pkt. <= 256 - SOFF */
#define	CAS_RC3_JUMBO_PKT	0x0000000000000002ULL	/* pkt. > 1522 bytes */
#define	CAS_RC3_JMBHS_EN	0x0000000000000004ULL	/* jmb. hdr. spl. en. */
#define	CAS_RC3_CSO_MASK	0x000000000007f000ULL	/* checksum start... */
#define	CAS_RC3_CSO_SHFT	12			/* ...offset */
#define	CAS_RC3_FLOWID_MASK	0x0000000001f80000ULL	/* flow ID of pkt. */
#define	CAS_RC3_FLOWID_SHFT	19
#define	CAS_RC3_OP_MASK		0x000000000e000000ULL	/* opcode */
#define	CAS_RC3_OP_SHFT		25
#define	CAS_RC3_FRC_FLAG	0x0000000010000000ULL	/* op. 2 batch. lkhd. */
#define	CAS_RC3_NASSIST		0x0000000020000000ULL	/* no assist */
#define	CAS_RC3_LB_MASK		0x000001f800000000ULL	/* load balancing key */
#define	CAS_RC3_LB_SHFT		35
#define	CAS_RC3_L3HO_MASK	0x0000fe0000000000ULL	/* layer 3 hdr. off. */
#define	CAS_RC3_L3HO_SHFT	41
#define	CAS_RC3_PLUS_ENC_PKT	0x0000020000000000ULL	/* IPsec AH/ESP pkt. */
#define	CAS_RC3_PLUS_L3HO_MASK	0x0000fc0000000000ULL	/* layer 3 hdr. off. */
#define	CAS_RC3_PLUS_L3HO_SHFT	42
#define	CAS_RC3_SAP_MASK	0xffff000000000000ULL	/* ethertype */
#define	CAS_RC3_SAP_SHFT	48

#define	CAS_RC4_TCP_CSUM_MASK	0x000000000000ffffULL	/* TCP checksum */
#define	CAS_RC4_TCP_CSUM_SHFT	0
#define	CAS_RC4_PKT_LEN_MASK	0x000000003fff0000ULL	/* entire pkt. length */
#define	CAS_RC4_PKT_LEN_SHFT	16
#define	CAS_RC4_PAM_MASK	0x00000003c0000000ULL	/* mcast. addr. match */
#define	CAS_RC4_PAM_SHFT	30
#define	CAS_RC4_ZERO		0x0000080000000000ULL	/* owned by software */
#define	CAS_RC4_HASH_VAL_MASK	0x0ffff00000000000ULL	/* mcast. addr. hash */
#define	CAS_RC4_HASH_VAL_SHFT	44
#define	CAS_RC4_HASH_PASS	0x1000000000000000ULL	/* passed hash filter */
#define	CAS_RC4_BAD		0x4000000000000000ULL	/* CRC error */
#define	CAS_RC4_LEN_MMATCH	0x8000000000000000ULL	/* length field mism. */

#define	CAS_GET(reg, bits)	(((reg) & (bits ## _MASK)) >> (bits ## _SHFT))
#define	CAS_SET(val, bits)	(((val) << (bits ## _SHFT)) & (bits ## _MASK))

#endif
