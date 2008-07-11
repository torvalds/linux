/*
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
#ifndef __ASM_TXX9_TX4938_H
#define __ASM_TXX9_TX4938_H

/* some controllers are compatible with 4927 */
#include <asm/txx9/tx4927.h>

#define tx4938_read_nfmc(addr) (*(volatile unsigned int *)(addr))
#define tx4938_write_nfmc(b, addr) (*(volatile unsigned int *)(addr)) = (b)

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

#define _CONST64(c)	c##ull

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN
#define endian_def_l2(e1, e2)	\
	volatile unsigned long e1, e2
#define endian_def_s2(e1, e2)	\
	volatile unsigned short e1, e2
#define endian_def_sb2(e1, e2, e3)	\
	volatile unsigned short e1;volatile unsigned char e2, e3
#define endian_def_b2s(e1, e2, e3)	\
	volatile unsigned char e1, e2;volatile unsigned short e3
#define endian_def_b4(e1, e2, e3, e4)	\
	volatile unsigned char e1, e2, e3, e4
#else
#define endian_def_l2(e1, e2)	\
	volatile unsigned long e2, e1
#define endian_def_s2(e1, e2)	\
	volatile unsigned short e2, e1
#define endian_def_sb2(e1, e2, e3)	\
	volatile unsigned char e3, e2;volatile unsigned short e1
#define endian_def_b2s(e1, e2, e3)	\
	volatile unsigned short e3;volatile unsigned char e2, e1
#define endian_def_b4(e1, e2, e3, e4)	\
	volatile unsigned char e4, e3, e2, e1
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
	u64 ccfg;
	u64 crir;
	u64 pcfg;
	u64 toea;
	u64 clkctr;
	u64 unused0;
	u64 garbc;
	u64 unused1;
	u64 unused2;
	u64 ramp;
	u64 unused3;
	u64 jmpadr;
};

#undef endian_def_l2
#undef endian_def_s2
#undef endian_def_sb2
#undef endian_def_b2s
#undef endian_def_b4

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
#define TX4938_IR_DMA(ch, n)	((ch ? 27 : 10) + (n)) /* 10-13, 27-30 */
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

#define TX4938_IRC_INT	2	/* IP[2] in Status register */

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
#define TX4938_CCFG_PCIARB	0x00002000
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

#define tx4938_sdramcptr	((struct tx4938_sdramc_reg *)TX4938_SDRAMC_REG)
#define tx4938_ebuscptr         ((struct tx4938_ebusc_reg *)TX4938_EBUSC_REG)
#define tx4938_dmaptr(ch)	((struct tx4938_dma_reg *)TX4938_DMA_REG(ch))
#define tx4938_ndfmcptr		((struct tx4938_ndfmc_reg *)TX4938_NDFMC_REG)
#define tx4938_pcicptr		tx4927_pcicptr
#define tx4938_pcic1ptr \
		((struct tx4927_pcic_reg __iomem *)TX4938_PCIC1_REG)
#define tx4938_ccfgptr \
		((struct tx4938_ccfg_reg __iomem *)TX4938_CCFG_REG)
#define tx4938_sioptr(ch)	((struct tx4938_sio_reg *)TX4938_SIO_REG(ch))
#define tx4938_pioptr		((struct txx9_pio_reg __iomem *)TX4938_PIO_REG)
#define tx4938_aclcptr		((struct tx4938_aclc_reg *)TX4938_ACLC_REG)
#define tx4938_spiptr		((struct tx4938_spi_reg *)TX4938_SPI_REG)
#define tx4938_sramcptr		((struct tx4938_sramc_reg *)TX4938_SRAMC_REG)


#define TX4938_REV_PCODE()	\
	((__u32)__raw_readq(&tx4938_ccfgptr->crir) >> 16)

#define tx4938_ccfg_clear(bits)	tx4927_ccfg_clear(bits)
#define tx4938_ccfg_set(bits)	tx4927_ccfg_set(bits)
#define tx4938_ccfg_change(change, new)	tx4927_ccfg_change(change, new)

#define TX4938_SDRAMC_BA(ch)	((tx4938_sdramcptr->cr[ch] >> 49) << 21)
#define TX4938_SDRAMC_SIZE(ch)	(((tx4938_sdramcptr->cr[ch] >> 33) + 1) << 21)

#define TX4938_EBUSC_CR(ch)	__raw_readq(&tx4938_ebuscptr->cr[(ch)])
#define TX4938_EBUSC_BA(ch)	((tx4938_ebuscptr->cr[ch] >> 48) << 20)
#define TX4938_EBUSC_SIZE(ch)	\
	(0x00100000 << ((unsigned long)(tx4938_ebuscptr->cr[ch] >> 8) & 0xf))

int tx4938_report_pciclk(void);
void tx4938_report_pci1clk(void);
int tx4938_pciclk66_setup(void);
struct pci_dev;
int tx4938_pcic1_map_irq(const struct pci_dev *dev, u8 slot);
void tx4938_irq_init(void);

#endif
