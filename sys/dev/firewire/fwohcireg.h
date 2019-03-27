/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $FreeBSD$
 *
 */
#define		PCI_CBMEM		PCIR_BAR(0)

#define		FW_VENDORID_NATSEMI	0x100B
#define		FW_VENDORID_NEC		0x1033
#define		FW_VENDORID_SIS		0x1039
#define		FW_VENDORID_TI		0x104c
#define		FW_VENDORID_SONY	0x104d
#define		FW_VENDORID_VIA		0x1106
#define		FW_VENDORID_RICOH	0x1180
#define		FW_VENDORID_APPLE	0x106b
#define		FW_VENDORID_LUCENT	0x11c1
#define		FW_VENDORID_INTEL	0x8086
#define		FW_VENDORID_ADAPTEC	0x9004
#define		FW_VENDORID_SUN		0x108e

#define		FW_DEVICE_CS4210	(0x000f << 16)
#define		FW_DEVICE_UPD861	(0x0063 << 16)
#define		FW_DEVICE_UPD871	(0x00ce << 16)
#define		FW_DEVICE_UPD72870	(0x00cd << 16)
#define		FW_DEVICE_UPD72873	(0x00e7 << 16)
#define		FW_DEVICE_UPD72874	(0x00f2 << 16)
#define		FW_DEVICE_TITSB22	(0x8009 << 16)
#define		FW_DEVICE_TITSB23	(0x8019 << 16)
#define		FW_DEVICE_TITSB26	(0x8020 << 16)
#define		FW_DEVICE_TITSB43	(0x8021 << 16)
#define		FW_DEVICE_TITSB43A	(0x8023 << 16)
#define		FW_DEVICE_TITSB43AB23	(0x8024 << 16)
#define		FW_DEVICE_TITSB82AA2	(0x8025 << 16)
#define		FW_DEVICE_TITSB43AB21	(0x8026 << 16)
#define		FW_DEVICE_TIPCI4410A	(0x8017 << 16)
#define		FW_DEVICE_TIPCI4450	(0x8011 << 16)
#define		FW_DEVICE_TIPCI4451	(0x8027 << 16)
#define		FW_DEVICE_CXD1947	(0x8009 << 16)
#define		FW_DEVICE_CXD3222	(0x8039 << 16)
#define		FW_DEVICE_VT6306	(0x3044 << 16)
#define		FW_DEVICE_R5C551	(0x0551 << 16)
#define		FW_DEVICE_R5C552	(0x0552 << 16)
#define		FW_DEVICE_PANGEA	(0x0030 << 16)
#define		FW_DEVICE_UNINORTH2	(0x0031 << 16)
#define		FW_DEVICE_AIC5800	(0x5800 << 16)
#define		FW_DEVICE_FW322		(0x5811 << 16)
#define		FW_DEVICE_7007		(0x7007 << 16)
#define		FW_DEVICE_82372FB	(0x7605 << 16)
#define		FW_DEVICE_PCIO2FW	(0x1102 << 16)

#define PCI_INTERFACE_OHCI	0x10

#define FW_OHCI_BASE_REG	0x10

#define		OHCI_DMA_ITCH		0x20
#define		OHCI_DMA_IRCH		0x20

#define		OHCI_MAX_DMA_CH		(0x4 + OHCI_DMA_ITCH + OHCI_DMA_IRCH)


typedef uint32_t 	fwohcireg_t;

/* for PCI */
#if BYTE_ORDER == BIG_ENDIAN
#define FWOHCI_DMA_WRITE(x, y)	((x) = htole32(y))
#define FWOHCI_DMA_READ(x)	le32toh(x)
#define FWOHCI_DMA_SET(x, y)	((x) |= htole32(y))
#define FWOHCI_DMA_CLEAR(x, y)	((x) &= htole32(~(y)))
#else
#define FWOHCI_DMA_WRITE(x, y)	((x) = (y))
#define FWOHCI_DMA_READ(x)	(x)
#define FWOHCI_DMA_SET(x, y)	((x) |= (y))
#define FWOHCI_DMA_CLEAR(x, y)	((x) &= ~(y))
#endif

struct fwohcidb {
	union {
		struct {
			uint32_t cmd;
			uint32_t addr;
			uint32_t depend;
			uint32_t res;
		} desc;
		uint32_t immed[4];
	} db;
#define OHCI_STATUS_SHIFT	16
#define OHCI_COUNT_MASK		0xffff
#define OHCI_OUTPUT_MORE	(0 << 28)
#define OHCI_OUTPUT_LAST	(1 << 28)
#define OHCI_INPUT_MORE		(2 << 28)
#define OHCI_INPUT_LAST		(3 << 28)
#define OHCI_STORE_QUAD		(4 << 28)
#define OHCI_LOAD_QUAD		(5 << 28)
#define OHCI_NOP		(6 << 28)
#define OHCI_STOP		(7 << 28)
#define OHCI_STORE		(8 << 28)
#define OHCI_CMD_MASK		(0xf << 28)

#define	OHCI_UPDATE		(1 << 27)

#define OHCI_KEY_ST0		(0 << 24)
#define OHCI_KEY_ST1		(1 << 24)
#define OHCI_KEY_ST2		(2 << 24)
#define OHCI_KEY_ST3		(3 << 24)
#define OHCI_KEY_REGS		(5 << 24)
#define OHCI_KEY_SYS		(6 << 24)
#define OHCI_KEY_DEVICE		(7 << 24)
#define OHCI_KEY_MASK		(7 << 24)

#define OHCI_INTERRUPT_NEVER	(0 << 20)
#define OHCI_INTERRUPT_TRUE	(1 << 20)
#define OHCI_INTERRUPT_FALSE	(2 << 20)
#define OHCI_INTERRUPT_ALWAYS	(3 << 20)

#define OHCI_BRANCH_NEVER	(0 << 18)
#define OHCI_BRANCH_TRUE	(1 << 18)
#define OHCI_BRANCH_FALSE	(2 << 18)
#define OHCI_BRANCH_ALWAYS	(3 << 18)
#define OHCI_BRANCH_MASK	(3 << 18)

#define OHCI_WAIT_NEVER		(0 << 16)
#define OHCI_WAIT_TRUE		(1 << 16)
#define OHCI_WAIT_FALSE		(2 << 16)
#define OHCI_WAIT_ALWAYS	(3 << 16)
};

#define OHCI_SPD_S100 0x4
#define OHCI_SPD_S200 0x1
#define OHCI_SPD_S400 0x2


#define FWOHCIEV_NOSTAT 0
#define FWOHCIEV_LONGP 2
#define FWOHCIEV_MISSACK 3
#define FWOHCIEV_UNDRRUN 4
#define FWOHCIEV_OVRRUN 5
#define FWOHCIEV_DESCERR 6
#define FWOHCIEV_DTRDERR 7
#define FWOHCIEV_DTWRERR 8
#define FWOHCIEV_BUSRST 9
#define FWOHCIEV_TIMEOUT 0xa
#define FWOHCIEV_TCODERR 0xb
#define FWOHCIEV_UNKNOWN 0xe
#define FWOHCIEV_FLUSHED 0xf
#define FWOHCIEV_ACKCOMPL 0x11
#define FWOHCIEV_ACKPEND 0x12
#define FWOHCIEV_ACKBSX 0x14
#define FWOHCIEV_ACKBSA 0x15
#define FWOHCIEV_ACKBSB 0x16
#define FWOHCIEV_ACKTARD 0x1b
#define FWOHCIEV_ACKDERR 0x1d
#define FWOHCIEV_ACKTERR 0x1e

#define FWOHCIEV_MASK 0x1f

struct ohci_dma {
	fwohcireg_t	cntl;

#define	OHCI_CNTL_CYCMATCH_S	(0x1 << 31)

#define	OHCI_CNTL_BUFFIL	(0x1 << 31)
#define	OHCI_CNTL_ISOHDR	(0x1 << 30)
#define	OHCI_CNTL_CYCMATCH_R	(0x1 << 29)
#define	OHCI_CNTL_MULTICH	(0x1 << 28)

#define	OHCI_CNTL_DMA_RUN	(0x1 << 15)
#define	OHCI_CNTL_DMA_WAKE	(0x1 << 12)
#define	OHCI_CNTL_DMA_DEAD	(0x1 << 11)
#define	OHCI_CNTL_DMA_ACTIVE	(0x1 << 10)
#define	OHCI_CNTL_DMA_BT	(0x1 << 8)
#define	OHCI_CNTL_DMA_BAD	(0x1 << 7)
#define	OHCI_CNTL_DMA_STAT	(0xff)

	fwohcireg_t	cntl_clr;
	fwohcireg_t	dummy0;
	fwohcireg_t	cmd;
	fwohcireg_t	match;
	fwohcireg_t	dummy1;
	fwohcireg_t	dummy2;
	fwohcireg_t	dummy3;
};

struct ohci_itdma {
	fwohcireg_t	cntl;
	fwohcireg_t	cntl_clr;
	fwohcireg_t	dummy0;
	fwohcireg_t	cmd;
};

struct ohci_registers {
	fwohcireg_t	ver;		/* Version No. 0x0 */
	fwohcireg_t	guid;		/* GUID_ROM No. 0x4 */
	fwohcireg_t	retry;		/* AT retries 0x8 */
#define FWOHCI_RETRY	0x8
	fwohcireg_t	csr_data;	/* CSR data   0xc */
	fwohcireg_t	csr_cmp;	/* CSR compare 0x10 */
	fwohcireg_t	csr_cntl;	/* CSR compare 0x14 */
	fwohcireg_t	rom_hdr;	/* config ROM ptr. 0x18 */
	fwohcireg_t	bus_id;		/* BUS_ID 0x1c */
	fwohcireg_t	bus_opt;	/* BUS option 0x20 */
#define	FWOHCIGUID_H	0x24
#define	FWOHCIGUID_L	0x28
	fwohcireg_t	guid_hi;	/* GUID hi 0x24 */
	fwohcireg_t	guid_lo;	/* GUID lo 0x28 */
	fwohcireg_t	dummy0[2];	/* dummy 0x2c-0x30 */
	fwohcireg_t	config_rom;	/* config ROM map 0x34 */
	fwohcireg_t	post_wr_lo;	/* post write addr lo 0x38 */
	fwohcireg_t	post_wr_hi;	/* post write addr hi 0x3c */
	fwohcireg_t	vendor;		/* vendor ID 0x40 */
	fwohcireg_t	dummy1[3];	/* dummy 0x44-0x4c */
	fwohcireg_t	hcc_cntl_set;	/* HCC control set 0x50 */
	fwohcireg_t	hcc_cntl_clr;	/* HCC control clr 0x54 */
#define	OHCI_HCC_BIBIV	(1U << 31)	/* BIBimage Valid */
#define	OHCI_HCC_BIGEND	(1 << 30)	/* noByteSwapData */
#define	OHCI_HCC_PRPHY	(1 << 23)	/* programPhyEnable */
#define	OHCI_HCC_PHYEN	(1 << 22)	/* aPhyEnhanceEnable */
#define	OHCI_HCC_LPS	(1 << 19)	/* LPS */
#define	OHCI_HCC_POSTWR	(1 << 18)	/* postedWriteEnable */
#define	OHCI_HCC_LINKEN	(1 << 17)	/* linkEnable */
#define	OHCI_HCC_RESET	(1 << 16)	/* softReset */
	fwohcireg_t	dummy2[2];	/* dummy 0x58-0x5c */
	fwohcireg_t	dummy3[1];	/* dummy 0x60 */
	fwohcireg_t	sid_buf;	/* self id buffer 0x64 */
	fwohcireg_t	sid_cnt;	/* self id count 0x68 */
	fwohcireg_t	dummy4[1];	/* dummy 0x6c */
	fwohcireg_t	ir_mask_hi_set;	/* ir mask hi set 0x70 */
	fwohcireg_t	ir_mask_hi_clr;	/* ir mask hi set 0x74 */
	fwohcireg_t	ir_mask_lo_set;	/* ir mask hi set 0x78 */
	fwohcireg_t	ir_mask_lo_clr;	/* ir mask hi set 0x7c */
#define	FWOHCI_INTSTAT		0x80
#define	FWOHCI_INTSTATCLR	0x84
#define	FWOHCI_INTMASK		0x88
#define	FWOHCI_INTMASKCLR	0x8c
	fwohcireg_t	int_stat;   /*       0x80 */
	fwohcireg_t	int_clear;  /*       0x84 */
	fwohcireg_t	int_mask;   /*       0x88 */
	fwohcireg_t	int_mask_clear;   /*       0x8c */
	fwohcireg_t	it_int_stat;   /*       0x90 */
	fwohcireg_t	it_int_clear;  /*       0x94 */
	fwohcireg_t	it_int_mask;   /*       0x98 */
	fwohcireg_t	it_mask_clear;   /*       0x9c */
	fwohcireg_t	ir_int_stat;   /*       0xa0 */
	fwohcireg_t	ir_int_clear;  /*       0xa4 */
	fwohcireg_t	ir_int_mask;   /*       0xa8 */
	fwohcireg_t	ir_mask_clear;   /*       0xac */
	fwohcireg_t	dummy5[11];	/* dummy 0xb0-d8 */
	fwohcireg_t	fairness;   /* fairness control      0xdc */
	fwohcireg_t	link_cntl;		/* Chip control 0xe0*/
	fwohcireg_t	link_cntl_clr;	/* Chip control clear 0xe4*/
#define FWOHCI_NODEID	0xe8
	fwohcireg_t	node;		/* Node ID 0xe8 */
#define	OHCI_NODE_VALID	(1U << 31)
#define	OHCI_NODE_ROOT	(1 << 30)

#define	OHCI_ASYSRCBUS	1

	fwohcireg_t	phy_access;	/* PHY cntl 0xec */
#define	PHYDEV_RDDONE		(1<<31)
#define	PHYDEV_RDCMD		(1<<15)
#define	PHYDEV_WRCMD		(1<<14)
#define	PHYDEV_REGADDR		8
#define	PHYDEV_WRDATA		0
#define	PHYDEV_RDADDR		24
#define	PHYDEV_RDDATA		16

	fwohcireg_t	cycle_timer;	/* Cycle Timer 0xf0 */
	fwohcireg_t	dummy6[3];	/* dummy 0xf4-fc */
	fwohcireg_t	areq_hi;	/* Async req. filter hi 0x100 */
	fwohcireg_t	areq_hi_clr;	/* Async req. filter hi 0x104 */
	fwohcireg_t	areq_lo;	/* Async req. filter lo 0x108 */
	fwohcireg_t	areq_lo_clr;	/* Async req. filter lo 0x10c */
	fwohcireg_t	preq_hi;	/* Async req. filter hi 0x110 */
	fwohcireg_t	preq_hi_clr;	/* Async req. filter hi 0x114 */
	fwohcireg_t	preq_lo;	/* Async req. filter lo 0x118 */
	fwohcireg_t	preq_lo_clr;	/* Async req. filter lo 0x11c */

	fwohcireg_t	pys_upper;	/* Physical Upper bound 0x120 */

	fwohcireg_t	dummy7[23];	/* dummy 0x124-0x17c */

	/*       0x180, 0x184, 0x188, 0x18c */
	/*       0x190, 0x194, 0x198, 0x19c */
	/*       0x1a0, 0x1a4, 0x1a8, 0x1ac */
	/*       0x1b0, 0x1b4, 0x1b8, 0x1bc */
	/*       0x1c0, 0x1c4, 0x1c8, 0x1cc */
	/*       0x1d0, 0x1d4, 0x1d8, 0x1dc */
	/*       0x1e0, 0x1e4, 0x1e8, 0x1ec */
	/*       0x1f0, 0x1f4, 0x1f8, 0x1fc */
	struct ohci_dma dma_ch[0x4];

	/*       0x200, 0x204, 0x208, 0x20c */
	/*       0x210, 0x204, 0x208, 0x20c */
	struct ohci_itdma dma_itch[0x20];

	/*       0x400, 0x404, 0x408, 0x40c */
	/*       0x410, 0x404, 0x408, 0x40c */
	struct ohci_dma dma_irch[0x20];
};

#ifndef _STANDALONE
struct fwohcidb_tr {
	STAILQ_ENTRY(fwohcidb_tr) link;
	struct fw_xfer *xfer;
	struct fwohcidb *db;
	bus_dmamap_t dma_map;
	caddr_t buf;
	bus_addr_t bus_addr;
	int dbcnt;
};
#endif

/*
 * OHCI info structure.
 */
struct fwohci_txpkthdr {
	union {
		uint32_t ld[4];
		struct {
#if BYTE_ORDER == BIG_ENDIAN
			uint32_t spd:16, /* XXX include reserved field */
				 :8,
				 tcode:4,
				 :4;
#else
			uint32_t :4,
				 tcode:4,
				 :8,
				 spd:16; /* XXX include reserved fields */
#endif
		}common;
		struct {
#if BYTE_ORDER == BIG_ENDIAN
			uint32_t :8,
				 srcbus:1,
				 :4,
				 spd:3,
				 tlrt:8,
				 tcode:4,
				 :4;
#else
			uint32_t :4,
				 tcode:4,
				 tlrt:8,
				 spd:3,
				 :4,
				 srcbus:1,
				 :8;
#endif
			BIT16x2(dst, );
		} asycomm;
		struct {
#if BYTE_ORDER == BIG_ENDIAN
			uint32_t :13,
			         spd:3,
				 chtag:8,
				 tcode:4,
				 sy:4;
#else
			uint32_t sy:4,
				 tcode:4,
				 chtag:8,
			         spd:3,
				 :13;
#endif
			BIT16x2(len, );
		} stream;
	} mode;
};

struct fwohci_trailer {
#if BYTE_ORDER == BIG_ENDIAN
	uint32_t stat:16,
		 time:16;
#else
	uint32_t time:16,
		 stat:16;
#endif
};

#define	OHCI_CNTL_CYCSRC	(0x1 << 22)
#define	OHCI_CNTL_CYCMTR	(0x1 << 21)
#define	OHCI_CNTL_CYCTIMER	(0x1 << 20)
#define	OHCI_CNTL_PHYPKT	(0x1 << 10)
#define	OHCI_CNTL_SID		(0x1 << 9)

/*
 * defined in OHCI 1.1
 * chapter 6.1
 */
#define OHCI_INT_DMA_ATRQ	(0x1 << 0)
#define OHCI_INT_DMA_ATRS	(0x1 << 1)
#define OHCI_INT_DMA_ARRQ	(0x1 << 2)
#define OHCI_INT_DMA_ARRS	(0x1 << 3)
#define OHCI_INT_DMA_PRRQ	(0x1 << 4)
#define OHCI_INT_DMA_PRRS	(0x1 << 5)
#define OHCI_INT_DMA_IT 	(0x1 << 6)
#define OHCI_INT_DMA_IR 	(0x1 << 7)
#define OHCI_INT_PW_ERR 	(0x1 << 8)
#define OHCI_INT_LR_ERR 	(0x1 << 9)
#define OHCI_INT_PHY_SID	(0x1 << 16)
#define OHCI_INT_PHY_BUS_R	(0x1 << 17)
#define OHCI_INT_REG_FAIL	(0x1 << 18)
#define OHCI_INT_PHY_INT	(0x1 << 19)
#define OHCI_INT_CYC_START	(0x1 << 20)
#define OHCI_INT_CYC_64SECOND	(0x1 << 21)
#define OHCI_INT_CYC_LOST	(0x1 << 22)
#define OHCI_INT_CYC_ERR	(0x1 << 23)
#define OHCI_INT_ERR		(0x1 << 24)
#define OHCI_INT_CYC_LONG	(0x1 << 25)
#define OHCI_INT_PHY_REG	(0x1 << 26)
#define OHCI_INT_EN		(0x1 << 31)

#define IP_CHANNELS             0x0234
#define FWOHCI_MAXREC		2048

#define	OHCI_ISORA		0x02
#define	OHCI_ISORB		0x04

#define FWOHCITCODE_PHY		0xe
