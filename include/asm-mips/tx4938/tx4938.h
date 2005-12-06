/*
 * linux/include/asm-mips/tx4938/tx4938.h
 * Definitions for TX4937/TX4938
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#ifndef __ASM_TX_BOARDS_TX4938_H
#define __ASM_TX_BOARDS_TX4938_H

#include <asm/tx4938/tx4938_mips.h>

#define tx4938_read_nfmc(addr) (*(volatile unsigned int *)(addr))
#define tx4938_write_nfmc(b,addr) (*(volatile unsigned int *)(addr)) = (b)

#define TX4938_NR_IRQ_LOCAL     TX4938_IRQ_PIC_BEG

#define TX4938_IRQ_IRC_PCIC     (TX4938_NR_IRQ_LOCAL + TX4938_IR_PCIC)
#define TX4938_IRQ_IRC_PCIERR   (TX4938_NR_IRQ_LOCAL + TX4938_IR_PCIERR)

#define TX4938_PCIIO_0 0x10000000
#define TX4938_PCIIO_1 0x01010000
#define TX4938_PCIMEM_0 0x08000000
#define TX4938_PCIMEM_1 0x11000000

#define TX4938_PCIIO_SIZE_0 0x01000000
#define TX4938_PCIIO_SIZE_1 0x00010000
#define TX4938_PCIMEM_SIZE_0 0x08000000
#define TX4938_PCIMEM_SIZE_1 0x00010000

#define TX4938_REG_BASE	0xff1f0000 /* == TX4937_REG_BASE */
#define TX4938_REG_SIZE	0x00010000 /* == TX4937_REG_SIZE */

/* NDFMC, SRAMC, PCIC1, SPIC: TX4938 only */
#define TX4938_NDFMC_REG	(TX4938_REG_BASE + 0x5000)
#define TX4938_SRAMC_REG	(TX4938_REG_BASE + 0x6000)
#define TX4938_PCIC1_REG	(TX4938_REG_BASE + 0x7000)
#define TX4938_SDRAMC_REG	(TX4938_REG_BASE + 0x8000)
#define TX4938_EBUSC_REG	(TX4938_REG_BASE + 0x9000)
#define TX4938_DMA_REG(ch)	(TX4938_REG_BASE + 0xb000 + (ch) * 0x800)
#define TX4938_PCIC_REG		(TX4938_REG_BASE + 0xd000)
#define TX4938_CCFG_REG		(TX4938_REG_BASE + 0xe000)
#define TX4938_NR_TMR	3
#define TX4938_TMR_REG(ch)	((TX4938_REG_BASE + 0xf000) + (ch) * 0x100)
#define TX4938_NR_SIO	2
#define TX4938_SIO_REG(ch)	((TX4938_REG_BASE + 0xf300) + (ch) * 0x100)
#define TX4938_PIO_REG		(TX4938_REG_BASE + 0xf500)
#define TX4938_IRC_REG		(TX4938_REG_BASE + 0xf600)
#define TX4938_ACLC_REG		(TX4938_REG_BASE + 0xf700)
#define TX4938_SPI_REG		(TX4938_REG_BASE + 0xf800)

#ifndef _LANGUAGE_ASSEMBLY
#include <asm/byteorder.h>

#define TX4938_MKA(x) ((u32)( ((u32)(TX4938_REG_BASE)) | ((u32)(x)) ))

#define TX4938_RD08( reg      )   (*(vu08*)(reg))
#define TX4938_WR08( reg, val )  ((*(vu08*)(reg))=(val))

#define TX4938_RD16( reg      )   (*(vu16*)(reg))
#define TX4938_WR16( reg, val )  ((*(vu16*)(reg))=(val))

#define TX4938_RD32( reg      )   (*(vu32*)(reg))
#define TX4938_WR32( reg, val )  ((*(vu32*)(reg))=(val))

#define TX4938_RD64( reg      )   (*(vu64*)(reg))
#define TX4938_WR64( reg, val )  ((*(vu64*)(reg))=(val))

#define TX4938_RD( reg      ) TX4938_RD32( reg )
#define TX4938_WR( reg, val ) TX4938_WR32( reg, val )

#endif /* !__ASSEMBLY__ */

#ifdef __ASSEMBLY__
#define _CONST64(c)	c
#else
#define _CONST64(c)	c##ull

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN
#define endian_def_l2(e1,e2)	\
	volatile unsigned long e1,e2
#define endian_def_s2(e1,e2)	\
	volatile unsigned short e1,e2
#define endian_def_sb2(e1,e2,e3)	\
	volatile unsigned short e1;volatile unsigned char e2,e3
#define endian_def_b2s(e1,e2,e3)	\
	volatile unsigned char e1,e2;volatile unsigned short e3
#define endian_def_b4(e1,e2,e3,e4)	\
	volatile unsigned char e1,e2,e3,e4
#else
#define endian_def_l2(e1,e2)	\
	volatile unsigned long e2,e1
#define endian_def_s2(e1,e2)	\
	volatile unsigned short e2,e1
#define endian_def_sb2(e1,e2,e3)	\
	volatile unsigned char e3,e2;volatile unsigned short e1
#define endian_def_b2s(e1,e2,e3)	\
	volatile unsigned short e3;volatile unsigned char e2,e1
#define endian_def_b4(e1,e2,e3,e4)	\
	volatile unsigned char e4,e3,e2,e1
#endif


struct tx4938_sdramc_reg {
	volatile unsigned long long cr[4];
	volatile unsigned long long unused0[4];
	volatile unsigned long long tr;
	volatile unsigned long long unused1[2];
	volatile unsigned long long cmd;
	volatile unsigned long long sfcmd;
};

struct tx4938_ebusc_reg {
	volatile unsigned long long cr[8];
};

struct tx4938_dma_reg {
	struct tx4938_dma_ch_reg {
		volatile unsigned long long cha;
		volatile unsigned long long sar;
		volatile unsigned long long dar;
		endian_def_l2(unused0, cntr);
		endian_def_l2(unused1, sair);
		endian_def_l2(unused2, dair);
		endian_def_l2(unused3, ccr);
		endian_def_l2(unused4, csr);
	} ch[4];
	volatile unsigned long long dbr[8];
	volatile unsigned long long tdhr;
	volatile unsigned long long midr;
	endian_def_l2(unused0, mcr);
};

struct tx4938_pcic_reg {
	volatile unsigned long pciid;
	volatile unsigned long pcistatus;
	volatile unsigned long pciccrev;
	volatile unsigned long pcicfg1;
	volatile unsigned long p2gm0plbase;		/* +10 */
	volatile unsigned long p2gm0pubase;
	volatile unsigned long p2gm1plbase;
	volatile unsigned long p2gm1pubase;
	volatile unsigned long p2gm2pbase;		/* +20 */
	volatile unsigned long p2giopbase;
	volatile unsigned long unused0;
	volatile unsigned long pcisid;
	volatile unsigned long unused1;		/* +30 */
	volatile unsigned long pcicapptr;
	volatile unsigned long unused2;
	volatile unsigned long pcicfg2;
	volatile unsigned long g2ptocnt;		/* +40 */
	volatile unsigned long unused3[15];
	volatile unsigned long g2pstatus;		/* +80 */
	volatile unsigned long g2pmask;
	volatile unsigned long pcisstatus;
	volatile unsigned long pcimask;
	volatile unsigned long p2gcfg;		/* +90 */
	volatile unsigned long p2gstatus;
	volatile unsigned long p2gmask;
	volatile unsigned long p2gccmd;
	volatile unsigned long unused4[24];		/* +a0 */
	volatile unsigned long pbareqport;		/* +100 */
	volatile unsigned long pbacfg;
	volatile unsigned long pbastatus;
	volatile unsigned long pbamask;
	volatile unsigned long pbabm;		/* +110 */
	volatile unsigned long pbacreq;
	volatile unsigned long pbacgnt;
	volatile unsigned long pbacstate;
	volatile unsigned long long g2pmgbase[3];		/* +120 */
	volatile unsigned long long g2piogbase;
	volatile unsigned long g2pmmask[3];		/* +140 */
	volatile unsigned long g2piomask;
	volatile unsigned long long g2pmpbase[3];		/* +150 */
	volatile unsigned long long g2piopbase;
	volatile unsigned long pciccfg;		/* +170 */
	volatile unsigned long pcicstatus;
	volatile unsigned long pcicmask;
	volatile unsigned long unused5;
	volatile unsigned long long p2gmgbase[3];		/* +180 */
	volatile unsigned long long p2giogbase;
	volatile unsigned long g2pcfgadrs;		/* +1a0 */
	volatile unsigned long g2pcfgdata;
	volatile unsigned long unused6[8];
	volatile unsigned long g2pintack;
	volatile unsigned long g2pspc;
	volatile unsigned long unused7[12];		/* +1d0 */
	volatile unsigned long long pdmca;		/* +200 */
	volatile unsigned long long pdmga;
	volatile unsigned long long pdmpa;
	volatile unsigned long long pdmctr;
	volatile unsigned long long pdmcfg;		/* +220 */
	volatile unsigned long long pdmsts;
};

struct tx4938_aclc_reg {
	volatile unsigned long acctlen;
	volatile unsigned long acctldis;
	volatile unsigned long acregacc;
	volatile unsigned long unused0;
	volatile unsigned long acintsts;
	volatile unsigned long acintmsts;
	volatile unsigned long acinten;
	volatile unsigned long acintdis;
	volatile unsigned long acsemaph;
	volatile unsigned long unused1[7];
	volatile unsigned long acgpidat;
	volatile unsigned long acgpodat;
	volatile unsigned long acslten;
	volatile unsigned long acsltdis;
	volatile unsigned long acfifosts;
	volatile unsigned long unused2[11];
	volatile unsigned long acdmasts;
	volatile unsigned long acdmasel;
	volatile unsigned long unused3[6];
	volatile unsigned long acaudodat;
	volatile unsigned long acsurrdat;
	volatile unsigned long accentdat;
	volatile unsigned long aclfedat;
	volatile unsigned long acaudiat;
	volatile unsigned long unused4;
	volatile unsigned long acmodoat;
	volatile unsigned long acmodidat;
	volatile unsigned long unused5[15];
	volatile unsigned long acrevid;
};


struct tx4938_tmr_reg {
	volatile unsigned long tcr;
	volatile unsigned long tisr;
	volatile unsigned long cpra;
	volatile unsigned long cprb;
	volatile unsigned long itmr;
	volatile unsigned long unused0[3];
	volatile unsigned long ccdr;
	volatile unsigned long unused1[3];
	volatile unsigned long pgmr;
	volatile unsigned long unused2[3];
	volatile unsigned long wtmr;
	volatile unsigned long unused3[43];
	volatile unsigned long trr;
};

struct tx4938_sio_reg {
	volatile unsigned long lcr;
	volatile unsigned long dicr;
	volatile unsigned long disr;
	volatile unsigned long cisr;
	volatile unsigned long fcr;
	volatile unsigned long flcr;
	volatile unsigned long bgr;
	volatile unsigned long tfifo;
	volatile unsigned long rfifo;
};

struct tx4938_pio_reg {
	volatile unsigned long dout;
	volatile unsigned long din;
	volatile unsigned long dir;
	volatile unsigned long od;
	volatile unsigned long flag[2];
	volatile unsigned long pol;
	volatile unsigned long intc;
	volatile unsigned long maskcpu;
	volatile unsigned long maskext;
};
struct tx4938_irc_reg {
	volatile unsigned long cer;
	volatile unsigned long cr[2];
	volatile unsigned long unused0;
	volatile unsigned long ilr[8];
	volatile unsigned long unused1[4];
	volatile unsigned long imr;
	volatile unsigned long unused2[7];
	volatile unsigned long scr;
	volatile unsigned long unused3[7];
	volatile unsigned long ssr;
	volatile unsigned long unused4[7];
	volatile unsigned long csr;
};

struct tx4938_ndfmc_reg {
	endian_def_l2(unused0, dtr);
	endian_def_l2(unused1, mcr);
	endian_def_l2(unused2, sr);
	endian_def_l2(unused3, isr);
	endian_def_l2(unused4, imr);
	endian_def_l2(unused5, spr);
	endian_def_l2(unused6, rstr);
};

struct tx4938_spi_reg {
	volatile unsigned long mcr;
	volatile unsigned long cr0;
	volatile unsigned long cr1;
	volatile unsigned long fs;
	volatile unsigned long unused1;
	volatile unsigned long sr;
	volatile unsigned long dr;
	volatile unsigned long unused2;
};

struct tx4938_sramc_reg {
	volatile unsigned long long cr;
};

struct tx4938_ccfg_reg {
	volatile unsigned long long ccfg;
	volatile unsigned long long crir;
	volatile unsigned long long pcfg;
	volatile unsigned long long tear;
	volatile unsigned long long clkctr;
	volatile unsigned long long unused0;
	volatile unsigned long long garbc;
	volatile unsigned long long unused1;
	volatile unsigned long long unused2;
	volatile unsigned long long ramp;
	volatile unsigned long long unused3;
	volatile unsigned long long jmpadr;
};

#undef endian_def_l2
#undef endian_def_s2
#undef endian_def_sb2
#undef endian_def_b2s
#undef endian_def_b4

#endif /* __ASSEMBLY__ */

/*
 * NDFMC
 */

/* NDFMCR : NDFMC Mode Control */
#define TX4938_NDFMCR_WE	0x80
#define TX4938_NDFMCR_ECC_ALL	0x60
#define TX4938_NDFMCR_ECC_RESET	0x60
#define TX4938_NDFMCR_ECC_READ	0x40
#define TX4938_NDFMCR_ECC_ON	0x20
#define TX4938_NDFMCR_ECC_OFF	0x00
#define TX4938_NDFMCR_CE	0x10
#define TX4938_NDFMCR_BSPRT	0x04
#define TX4938_NDFMCR_ALE	0x02
#define TX4938_NDFMCR_CLE	0x01

/* NDFMCR : NDFMC Status */
#define TX4938_NDFSR_BUSY	0x80

/* NDFMCR : NDFMC Reset */
#define TX4938_NDFRSTR_RST	0x01

/*
 * IRC
 */

#define TX4938_IR_ECCERR	0
#define TX4938_IR_WTOERR	1
#define TX4938_NUM_IR_INT	6
#define TX4938_IR_INT(n)	(2 + (n))
#define TX4938_NUM_IR_SIO	2
#define TX4938_IR_SIO(n)	(8 + (n))
#define TX4938_NUM_IR_DMA	4
#define TX4938_IR_DMA(ch,n)	((ch ? 27 : 10) + (n)) /* 10-13,27-30 */
#define TX4938_IR_PIO	14
#define TX4938_IR_PDMAC	15
#define TX4938_IR_PCIC	16
#define TX4938_NUM_IR_TMR	3
#define TX4938_IR_TMR(n)	(17 + (n))
#define TX4938_IR_NDFMC	21
#define TX4938_IR_PCIERR	22
#define TX4938_IR_PCIPME	23
#define TX4938_IR_ACLC	24
#define TX4938_IR_ACLCPME	25
#define TX4938_IR_PCIC1	26
#define TX4938_IR_SPI	31
#define TX4938_NUM_IR	32
/* multiplex */
#define TX4938_IR_ETH0	TX4938_IR_INT(4)
#define TX4938_IR_ETH1	TX4938_IR_INT(3)

/*
 * CCFG
 */
/* CCFG : Chip Configuration */
#define TX4938_CCFG_WDRST	_CONST64(0x0000020000000000)
#define TX4938_CCFG_WDREXEN	_CONST64(0x0000010000000000)
#define TX4938_CCFG_BCFG_MASK	_CONST64(0x000000ff00000000)
#define TX4938_CCFG_TINTDIS	0x01000000
#define TX4938_CCFG_PCI66	0x00800000
#define TX4938_CCFG_PCIMODE	0x00400000
#define TX4938_CCFG_PCI1_66	0x00200000
#define TX4938_CCFG_DIVMODE_MASK	0x001e0000
#define TX4938_CCFG_DIVMODE_2	(0x4 << 17)
#define TX4938_CCFG_DIVMODE_2_5	(0xf << 17)
#define TX4938_CCFG_DIVMODE_3	(0x5 << 17)
#define TX4938_CCFG_DIVMODE_4	(0x6 << 17)
#define TX4938_CCFG_DIVMODE_4_5	(0xd << 17)
#define TX4938_CCFG_DIVMODE_8	(0x0 << 17)
#define TX4938_CCFG_DIVMODE_10	(0xb << 17)
#define TX4938_CCFG_DIVMODE_12	(0x1 << 17)
#define TX4938_CCFG_DIVMODE_16	(0x2 << 17)
#define TX4938_CCFG_DIVMODE_18	(0x9 << 17)
#define TX4938_CCFG_BEOW	0x00010000
#define TX4938_CCFG_WR	0x00008000
#define TX4938_CCFG_TOE	0x00004000
#define TX4938_CCFG_PCIXARB	0x00002000
#define TX4938_CCFG_PCIDIVMODE_MASK	0x00001c00
#define TX4938_CCFG_PCIDIVMODE_4	(0x1 << 10)
#define TX4938_CCFG_PCIDIVMODE_4_5	(0x3 << 10)
#define TX4938_CCFG_PCIDIVMODE_5	(0x5 << 10)
#define TX4938_CCFG_PCIDIVMODE_5_5	(0x7 << 10)
#define TX4938_CCFG_PCIDIVMODE_8	(0x0 << 10)
#define TX4938_CCFG_PCIDIVMODE_9	(0x2 << 10)
#define TX4938_CCFG_PCIDIVMODE_10	(0x4 << 10)
#define TX4938_CCFG_PCIDIVMODE_11	(0x6 << 10)
#define TX4938_CCFG_PCI1DMD	0x00000100
#define TX4938_CCFG_SYSSP_MASK	0x000000c0
#define TX4938_CCFG_ENDIAN	0x00000004
#define TX4938_CCFG_HALT	0x00000002
#define TX4938_CCFG_ACEHOLD	0x00000001

/* PCFG : Pin Configuration */
#define TX4938_PCFG_ETH0_SEL	_CONST64(0x8000000000000000)
#define TX4938_PCFG_ETH1_SEL	_CONST64(0x4000000000000000)
#define TX4938_PCFG_ATA_SEL	_CONST64(0x2000000000000000)
#define TX4938_PCFG_ISA_SEL	_CONST64(0x1000000000000000)
#define TX4938_PCFG_SPI_SEL	_CONST64(0x0800000000000000)
#define TX4938_PCFG_NDF_SEL	_CONST64(0x0400000000000000)
#define TX4938_PCFG_SDCLKDLY_MASK	0x30000000
#define TX4938_PCFG_SDCLKDLY(d)	((d)<<28)
#define TX4938_PCFG_SYSCLKEN	0x08000000
#define TX4938_PCFG_SDCLKEN_ALL	0x07800000
#define TX4938_PCFG_SDCLKEN(ch)	(0x00800000<<(ch))
#define TX4938_PCFG_PCICLKEN_ALL	0x003f0000
#define TX4938_PCFG_PCICLKEN(ch)	(0x00010000<<(ch))
#define TX4938_PCFG_SEL2	0x00000200
#define TX4938_PCFG_SEL1	0x00000100
#define TX4938_PCFG_DMASEL_ALL	0x0000000f
#define TX4938_PCFG_DMASEL0_DRQ0	0x00000000
#define TX4938_PCFG_DMASEL0_SIO1	0x00000001
#define TX4938_PCFG_DMASEL1_DRQ1	0x00000000
#define TX4938_PCFG_DMASEL1_SIO1	0x00000002
#define TX4938_PCFG_DMASEL2_DRQ2	0x00000000
#define TX4938_PCFG_DMASEL2_SIO0	0x00000004
#define TX4938_PCFG_DMASEL3_DRQ3	0x00000000
#define TX4938_PCFG_DMASEL3_SIO0	0x00000008

/* CLKCTR : Clock Control */
#define TX4938_CLKCTR_NDFCKD	_CONST64(0x0001000000000000)
#define TX4938_CLKCTR_NDFRST	_CONST64(0x0000000100000000)
#define TX4938_CLKCTR_ETH1CKD	0x80000000
#define TX4938_CLKCTR_ETH0CKD	0x40000000
#define TX4938_CLKCTR_SPICKD	0x20000000
#define TX4938_CLKCTR_SRAMCKD	0x10000000
#define TX4938_CLKCTR_PCIC1CKD	0x08000000
#define TX4938_CLKCTR_DMA1CKD	0x04000000
#define TX4938_CLKCTR_ACLCKD	0x02000000
#define TX4938_CLKCTR_PIOCKD	0x01000000
#define TX4938_CLKCTR_DMACKD	0x00800000
#define TX4938_CLKCTR_PCICKD	0x00400000
#define TX4938_CLKCTR_TM0CKD	0x00100000
#define TX4938_CLKCTR_TM1CKD	0x00080000
#define TX4938_CLKCTR_TM2CKD	0x00040000
#define TX4938_CLKCTR_SIO0CKD	0x00020000
#define TX4938_CLKCTR_SIO1CKD	0x00010000
#define TX4938_CLKCTR_ETH1RST	0x00008000
#define TX4938_CLKCTR_ETH0RST	0x00004000
#define TX4938_CLKCTR_SPIRST	0x00002000
#define TX4938_CLKCTR_SRAMRST	0x00001000
#define TX4938_CLKCTR_PCIC1RST	0x00000800
#define TX4938_CLKCTR_DMA1RST	0x00000400
#define TX4938_CLKCTR_ACLRST	0x00000200
#define TX4938_CLKCTR_PIORST	0x00000100
#define TX4938_CLKCTR_DMARST	0x00000080
#define TX4938_CLKCTR_PCIRST	0x00000040
#define TX4938_CLKCTR_TM0RST	0x00000010
#define TX4938_CLKCTR_TM1RST	0x00000008
#define TX4938_CLKCTR_TM2RST	0x00000004
#define TX4938_CLKCTR_SIO0RST	0x00000002
#define TX4938_CLKCTR_SIO1RST	0x00000001

/* bits for G2PSTATUS/G2PMASK */
#define TX4938_PCIC_G2PSTATUS_ALL	0x00000003
#define TX4938_PCIC_G2PSTATUS_TTOE	0x00000002
#define TX4938_PCIC_G2PSTATUS_RTOE	0x00000001

/* bits for PCIMASK (see also PCI_STATUS_XXX in linux/pci.h */
#define TX4938_PCIC_PCISTATUS_ALL	0x0000f900

/* bits for PBACFG */
#define TX4938_PCIC_PBACFG_FIXPA	0x00000008
#define TX4938_PCIC_PBACFG_RPBA	0x00000004
#define TX4938_PCIC_PBACFG_PBAEN	0x00000002
#define TX4938_PCIC_PBACFG_BMCEN	0x00000001

/* bits for G2PMnGBASE */
#define TX4938_PCIC_G2PMnGBASE_BSDIS	_CONST64(0x0000002000000000)
#define TX4938_PCIC_G2PMnGBASE_ECHG	_CONST64(0x0000001000000000)

/* bits for G2PIOGBASE */
#define TX4938_PCIC_G2PIOGBASE_BSDIS	_CONST64(0x0000002000000000)
#define TX4938_PCIC_G2PIOGBASE_ECHG	_CONST64(0x0000001000000000)

/* bits for PCICSTATUS/PCICMASK */
#define TX4938_PCIC_PCICSTATUS_ALL	0x000007b8
#define TX4938_PCIC_PCICSTATUS_PME	0x00000400
#define TX4938_PCIC_PCICSTATUS_TLB	0x00000200
#define TX4938_PCIC_PCICSTATUS_NIB	0x00000100
#define TX4938_PCIC_PCICSTATUS_ZIB	0x00000080
#define TX4938_PCIC_PCICSTATUS_PERR	0x00000020
#define TX4938_PCIC_PCICSTATUS_SERR	0x00000010
#define TX4938_PCIC_PCICSTATUS_GBE	0x00000008
#define TX4938_PCIC_PCICSTATUS_IWB	0x00000002
#define TX4938_PCIC_PCICSTATUS_E2PDONE	0x00000001

/* bits for PCICCFG */
#define TX4938_PCIC_PCICCFG_GBWC_MASK	0x0fff0000
#define TX4938_PCIC_PCICCFG_HRST	0x00000800
#define TX4938_PCIC_PCICCFG_SRST	0x00000400
#define TX4938_PCIC_PCICCFG_IRBER	0x00000200
#define TX4938_PCIC_PCICCFG_G2PMEN(ch)	(0x00000100>>(ch))
#define TX4938_PCIC_PCICCFG_G2PM0EN	0x00000100
#define TX4938_PCIC_PCICCFG_G2PM1EN	0x00000080
#define TX4938_PCIC_PCICCFG_G2PM2EN	0x00000040
#define TX4938_PCIC_PCICCFG_G2PIOEN	0x00000020
#define TX4938_PCIC_PCICCFG_TCAR	0x00000010
#define TX4938_PCIC_PCICCFG_ICAEN	0x00000008

/* bits for P2GMnGBASE */
#define TX4938_PCIC_P2GMnGBASE_TMEMEN	_CONST64(0x0000004000000000)
#define TX4938_PCIC_P2GMnGBASE_TBSDIS	_CONST64(0x0000002000000000)
#define TX4938_PCIC_P2GMnGBASE_TECHG	_CONST64(0x0000001000000000)

/* bits for P2GIOGBASE */
#define TX4938_PCIC_P2GIOGBASE_TIOEN	_CONST64(0x0000004000000000)
#define TX4938_PCIC_P2GIOGBASE_TBSDIS	_CONST64(0x0000002000000000)
#define TX4938_PCIC_P2GIOGBASE_TECHG	_CONST64(0x0000001000000000)

#define TX4938_PCIC_IDSEL_AD_TO_SLOT(ad)	((ad) - 11)
#define TX4938_PCIC_MAX_DEVNU	TX4938_PCIC_IDSEL_AD_TO_SLOT(32)

/* bits for PDMCFG */
#define TX4938_PCIC_PDMCFG_RSTFIFO	0x00200000
#define TX4938_PCIC_PDMCFG_EXFER	0x00100000
#define TX4938_PCIC_PDMCFG_REQDLY_MASK	0x00003800
#define TX4938_PCIC_PDMCFG_REQDLY_NONE	(0 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_16	(1 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_32	(2 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_64	(3 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_128	(4 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_256	(5 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_512	(6 << 11)
#define TX4938_PCIC_PDMCFG_REQDLY_1024	(7 << 11)
#define TX4938_PCIC_PDMCFG_ERRIE	0x00000400
#define TX4938_PCIC_PDMCFG_NCCMPIE	0x00000200
#define TX4938_PCIC_PDMCFG_NTCMPIE	0x00000100
#define TX4938_PCIC_PDMCFG_CHNEN	0x00000080
#define TX4938_PCIC_PDMCFG_XFRACT	0x00000040
#define TX4938_PCIC_PDMCFG_BSWAP	0x00000020
#define TX4938_PCIC_PDMCFG_XFRSIZE_MASK	0x0000000c
#define TX4938_PCIC_PDMCFG_XFRSIZE_1DW	0x00000000
#define TX4938_PCIC_PDMCFG_XFRSIZE_1QW	0x00000004
#define TX4938_PCIC_PDMCFG_XFRSIZE_4QW	0x00000008
#define TX4938_PCIC_PDMCFG_XFRDIRC	0x00000002
#define TX4938_PCIC_PDMCFG_CHRST	0x00000001

/* bits for PDMSTS */
#define TX4938_PCIC_PDMSTS_REQCNT_MASK	0x3f000000
#define TX4938_PCIC_PDMSTS_FIFOCNT_MASK	0x00f00000
#define TX4938_PCIC_PDMSTS_FIFOWP_MASK	0x000c0000
#define TX4938_PCIC_PDMSTS_FIFORP_MASK	0x00030000
#define TX4938_PCIC_PDMSTS_ERRINT	0x00000800
#define TX4938_PCIC_PDMSTS_DONEINT	0x00000400
#define TX4938_PCIC_PDMSTS_CHNEN	0x00000200
#define TX4938_PCIC_PDMSTS_XFRACT	0x00000100
#define TX4938_PCIC_PDMSTS_ACCMP	0x00000080
#define TX4938_PCIC_PDMSTS_NCCMP	0x00000040
#define TX4938_PCIC_PDMSTS_NTCMP	0x00000020
#define TX4938_PCIC_PDMSTS_CFGERR	0x00000008
#define TX4938_PCIC_PDMSTS_PCIERR	0x00000004
#define TX4938_PCIC_PDMSTS_CHNERR	0x00000002
#define TX4938_PCIC_PDMSTS_DATAERR	0x00000001
#define TX4938_PCIC_PDMSTS_ALL_CMP	0x000000e0
#define TX4938_PCIC_PDMSTS_ALL_ERR	0x0000000f

/*
 * DMA
 */
/* bits for MCR */
#define TX4938_DMA_MCR_EIS(ch)	(0x10000000<<(ch))
#define TX4938_DMA_MCR_DIS(ch)	(0x01000000<<(ch))
#define TX4938_DMA_MCR_RSFIF	0x00000080
#define TX4938_DMA_MCR_FIFUM(ch)	(0x00000008<<(ch))
#define TX4938_DMA_MCR_RPRT	0x00000002
#define TX4938_DMA_MCR_MSTEN	0x00000001

/* bits for CCRn */
#define TX4938_DMA_CCR_IMMCHN	0x20000000
#define TX4938_DMA_CCR_USEXFSZ	0x10000000
#define TX4938_DMA_CCR_LE	0x08000000
#define TX4938_DMA_CCR_DBINH	0x04000000
#define TX4938_DMA_CCR_SBINH	0x02000000
#define TX4938_DMA_CCR_CHRST	0x01000000
#define TX4938_DMA_CCR_RVBYTE	0x00800000
#define TX4938_DMA_CCR_ACKPOL	0x00400000
#define TX4938_DMA_CCR_REQPL	0x00200000
#define TX4938_DMA_CCR_EGREQ	0x00100000
#define TX4938_DMA_CCR_CHDN	0x00080000
#define TX4938_DMA_CCR_DNCTL	0x00060000
#define TX4938_DMA_CCR_EXTRQ	0x00010000
#define TX4938_DMA_CCR_INTRQD	0x0000e000
#define TX4938_DMA_CCR_INTENE	0x00001000
#define TX4938_DMA_CCR_INTENC	0x00000800
#define TX4938_DMA_CCR_INTENT	0x00000400
#define TX4938_DMA_CCR_CHNEN	0x00000200
#define TX4938_DMA_CCR_XFACT	0x00000100
#define TX4938_DMA_CCR_SMPCHN	0x00000020
#define TX4938_DMA_CCR_XFSZ(order)	(((order) << 2) & 0x0000001c)
#define TX4938_DMA_CCR_XFSZ_1W	TX4938_DMA_CCR_XFSZ(2)
#define TX4938_DMA_CCR_XFSZ_2W	TX4938_DMA_CCR_XFSZ(3)
#define TX4938_DMA_CCR_XFSZ_4W	TX4938_DMA_CCR_XFSZ(4)
#define TX4938_DMA_CCR_XFSZ_8W	TX4938_DMA_CCR_XFSZ(5)
#define TX4938_DMA_CCR_XFSZ_16W	TX4938_DMA_CCR_XFSZ(6)
#define TX4938_DMA_CCR_XFSZ_32W	TX4938_DMA_CCR_XFSZ(7)
#define TX4938_DMA_CCR_MEMIO	0x00000002
#define TX4938_DMA_CCR_SNGAD	0x00000001

/* bits for CSRn */
#define TX4938_DMA_CSR_CHNEN	0x00000400
#define TX4938_DMA_CSR_STLXFER	0x00000200
#define TX4938_DMA_CSR_CHNACT	0x00000100
#define TX4938_DMA_CSR_ABCHC	0x00000080
#define TX4938_DMA_CSR_NCHNC	0x00000040
#define TX4938_DMA_CSR_NTRNFC	0x00000020
#define TX4938_DMA_CSR_EXTDN	0x00000010
#define TX4938_DMA_CSR_CFERR	0x00000008
#define TX4938_DMA_CSR_CHERR	0x00000004
#define TX4938_DMA_CSR_DESERR	0x00000002
#define TX4938_DMA_CSR_SORERR	0x00000001

/* TX4938 Interrupt Controller (32-bit registers) */
#define TX4938_IRC_BASE                 0xf510
#define TX4938_IRC_IRFLAG0              0xf510
#define TX4938_IRC_IRFLAG1              0xf514
#define TX4938_IRC_IRPOL                0xf518
#define TX4938_IRC_IRRCNT               0xf51c
#define TX4938_IRC_IRMASKINT            0xf520
#define TX4938_IRC_IRMASKEXT            0xf524
#define TX4938_IRC_IRDEN                0xf600
#define TX4938_IRC_IRDM0                0xf604
#define TX4938_IRC_IRDM1                0xf608
#define TX4938_IRC_IRLVL0               0xf610
#define TX4938_IRC_IRLVL1               0xf614
#define TX4938_IRC_IRLVL2               0xf618
#define TX4938_IRC_IRLVL3               0xf61c
#define TX4938_IRC_IRLVL4               0xf620
#define TX4938_IRC_IRLVL5               0xf624
#define TX4938_IRC_IRLVL6               0xf628
#define TX4938_IRC_IRLVL7               0xf62c
#define TX4938_IRC_IRMSK                0xf640
#define TX4938_IRC_IREDC                0xf660
#define TX4938_IRC_IRPND                0xf680
#define TX4938_IRC_IRCS                 0xf6a0
#define TX4938_IRC_LIMIT                0xf6ff


#ifndef __ASSEMBLY__

#define tx4938_sdramcptr	((struct tx4938_sdramc_reg *)TX4938_SDRAMC_REG)
#define tx4938_ebuscptr         ((struct tx4938_ebusc_reg *)TX4938_EBUSC_REG)
#define tx4938_dmaptr(ch)	((struct tx4938_dma_reg *)TX4938_DMA_REG(ch))
#define tx4938_ndfmcptr		((struct tx4938_ndfmc_reg *)TX4938_NDFMC_REG)
#define tx4938_ircptr		((struct tx4938_irc_reg *)TX4938_IRC_REG)
#define tx4938_pcicptr		((struct tx4938_pcic_reg *)TX4938_PCIC_REG)
#define tx4938_pcic1ptr		((struct tx4938_pcic_reg *)TX4938_PCIC1_REG)
#define tx4938_ccfgptr		((struct tx4938_ccfg_reg *)TX4938_CCFG_REG)
#define tx4938_tmrptr(ch)	((struct tx4938_tmr_reg *)TX4938_TMR_REG(ch))
#define tx4938_sioptr(ch)	((struct tx4938_sio_reg *)TX4938_SIO_REG(ch))
#define tx4938_pioptr		((struct tx4938_pio_reg *)TX4938_PIO_REG)
#define tx4938_aclcptr		((struct tx4938_aclc_reg *)TX4938_ACLC_REG)
#define tx4938_spiptr		((struct tx4938_spi_reg *)TX4938_SPI_REG)
#define tx4938_sramcptr		((struct tx4938_sramc_reg *)TX4938_SRAMC_REG)


#define TX4938_REV_MAJ_MIN()	((unsigned long)tx4938_ccfgptr->crir & 0x00ff)
#define TX4938_REV_PCODE()	((unsigned long)tx4938_ccfgptr->crir >> 16)

#define TX4938_SDRAMC_BA(ch)	((tx4938_sdramcptr->cr[ch] >> 49) << 21)
#define TX4938_SDRAMC_SIZE(ch)	(((tx4938_sdramcptr->cr[ch] >> 33) + 1) << 21)

#define TX4938_EBUSC_BA(ch)	((tx4938_ebuscptr->cr[ch] >> 48) << 20)
#define TX4938_EBUSC_SIZE(ch)	\
	(0x00100000 << ((unsigned long)(tx4938_ebuscptr->cr[ch] >> 8) & 0xf))


#endif /* !__ASSEMBLY__ */

#endif
