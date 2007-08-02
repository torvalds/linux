/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Toshiba Corporation
 */
#ifndef __ASM_TX3927_H
#define __ASM_TX3927_H

#include <asm/jmr3927/txx927.h>

#define TX3927_SDRAMC_REG	0xfffe8000
#define TX3927_ROMC_REG		0xfffe9000
#define TX3927_DMA_REG		0xfffeb000
#define TX3927_IRC_REG		0xfffec000
#define TX3927_PCIC_REG		0xfffed000
#define TX3927_CCFG_REG		0xfffee000
#define TX3927_NR_TMR	3
#define TX3927_TMR_REG(ch)	(0xfffef000 + (ch) * 0x100)
#define TX3927_NR_SIO	2
#define TX3927_SIO_REG(ch)	(0xfffef300 + (ch) * 0x100)
#define TX3927_PIO_REG		0xfffef500

struct tx3927_sdramc_reg {
	volatile unsigned long cr[8];
	volatile unsigned long tr[3];
	volatile unsigned long cmd;
	volatile unsigned long smrs[2];
};

struct tx3927_romc_reg {
	volatile unsigned long cr[8];
};

struct tx3927_dma_reg {
	struct tx3927_dma_ch_reg {
		volatile unsigned long cha;
		volatile unsigned long sar;
		volatile unsigned long dar;
		volatile unsigned long cntr;
		volatile unsigned long sair;
		volatile unsigned long dair;
		volatile unsigned long ccr;
		volatile unsigned long csr;
	} ch[4];
	volatile unsigned long dbr[8];
	volatile unsigned long tdhr;
	volatile unsigned long mcr;
	volatile unsigned long unused0;
};

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN
#define endian_def_s2(e1,e2)	\
	volatile unsigned short e1,e2
#define endian_def_sb2(e1,e2,e3)	\
	volatile unsigned short e1;volatile unsigned char e2,e3
#define endian_def_b2s(e1,e2,e3)	\
	volatile unsigned char e1,e2;volatile unsigned short e3
#define endian_def_b4(e1,e2,e3,e4)	\
	volatile unsigned char e1,e2,e3,e4
#else
#define endian_def_s2(e1,e2)	\
	volatile unsigned short e2,e1
#define endian_def_sb2(e1,e2,e3)	\
	volatile unsigned char e3,e2;volatile unsigned short e1
#define endian_def_b2s(e1,e2,e3)	\
	volatile unsigned short e3;volatile unsigned char e2,e1
#define endian_def_b4(e1,e2,e3,e4)	\
	volatile unsigned char e4,e3,e2,e1
#endif

struct tx3927_pcic_reg {
	endian_def_s2(did, vid);
	endian_def_s2(pcistat, pcicmd);
	endian_def_b4(cc, scc, rpli, rid);
	endian_def_b4(unused0, ht, mlt, cls);
	volatile unsigned long ioba;		/* +10 */
	volatile unsigned long mba;
	volatile unsigned long unused1[5];
	endian_def_s2(svid, ssvid);
	volatile unsigned long unused2;		/* +30 */
	endian_def_sb2(unused3, unused4, capptr);
	volatile unsigned long unused5;
	endian_def_b4(ml, mg, ip, il);
	volatile unsigned long unused6;		/* +40 */
	volatile unsigned long istat;
	volatile unsigned long iim;
	volatile unsigned long rrt;
	volatile unsigned long unused7[3];		/* +50 */
	volatile unsigned long ipbmma;
	volatile unsigned long ipbioma;		/* +60 */
	volatile unsigned long ilbmma;
	volatile unsigned long ilbioma;
	volatile unsigned long unused8[9];
	volatile unsigned long tc;		/* +90 */
	volatile unsigned long tstat;
	volatile unsigned long tim;
	volatile unsigned long tccmd;
	volatile unsigned long pcirrt;		/* +a0 */
	volatile unsigned long pcirrt_cmd;
	volatile unsigned long pcirrdt;
	volatile unsigned long unused9[3];
	volatile unsigned long tlboap;
	volatile unsigned long tlbiap;
	volatile unsigned long tlbmma;		/* +c0 */
	volatile unsigned long tlbioma;
	volatile unsigned long sc_msg;
	volatile unsigned long sc_be;
	volatile unsigned long tbl;		/* +d0 */
	volatile unsigned long unused10[3];
	volatile unsigned long pwmng;		/* +e0 */
	volatile unsigned long pwmngs;
	volatile unsigned long unused11[6];
	volatile unsigned long req_trace;		/* +100 */
	volatile unsigned long pbapmc;
	volatile unsigned long pbapms;
	volatile unsigned long pbapmim;
	volatile unsigned long bm;		/* +110 */
	volatile unsigned long cpcibrs;
	volatile unsigned long cpcibgs;
	volatile unsigned long pbacs;
	volatile unsigned long iobas;		/* +120 */
	volatile unsigned long mbas;
	volatile unsigned long lbc;
	volatile unsigned long lbstat;
	volatile unsigned long lbim;		/* +130 */
	volatile unsigned long pcistatim;
	volatile unsigned long ica;
	volatile unsigned long icd;
	volatile unsigned long iiadp;		/* +140 */
	volatile unsigned long iscdp;
	volatile unsigned long mmas;
	volatile unsigned long iomas;
	volatile unsigned long ipciaddr;		/* +150 */
	volatile unsigned long ipcidata;
	volatile unsigned long ipcibe;
};

struct tx3927_ccfg_reg {
	volatile unsigned long ccfg;
	volatile unsigned long crir;
	volatile unsigned long pcfg;
	volatile unsigned long tear;
	volatile unsigned long pdcr;
};

/*
 * SDRAMC
 */

/*
 * ROMC
 */

/*
 * DMA
 */
/* bits for MCR */
#define TX3927_DMA_MCR_EIS(ch)	(0x10000000<<(ch))
#define TX3927_DMA_MCR_DIS(ch)	(0x01000000<<(ch))
#define TX3927_DMA_MCR_RSFIF	0x00000080
#define TX3927_DMA_MCR_FIFUM(ch)	(0x00000008<<(ch))
#define TX3927_DMA_MCR_LE	0x00000004
#define TX3927_DMA_MCR_RPRT	0x00000002
#define TX3927_DMA_MCR_MSTEN	0x00000001

/* bits for CCRn */
#define TX3927_DMA_CCR_DBINH	0x04000000
#define TX3927_DMA_CCR_SBINH	0x02000000
#define TX3927_DMA_CCR_CHRST	0x01000000
#define TX3927_DMA_CCR_RVBYTE	0x00800000
#define TX3927_DMA_CCR_ACKPOL	0x00400000
#define TX3927_DMA_CCR_REQPL	0x00200000
#define TX3927_DMA_CCR_EGREQ	0x00100000
#define TX3927_DMA_CCR_CHDN	0x00080000
#define TX3927_DMA_CCR_DNCTL	0x00060000
#define TX3927_DMA_CCR_EXTRQ	0x00010000
#define TX3927_DMA_CCR_INTRQD	0x0000e000
#define TX3927_DMA_CCR_INTENE	0x00001000
#define TX3927_DMA_CCR_INTENC	0x00000800
#define TX3927_DMA_CCR_INTENT	0x00000400
#define TX3927_DMA_CCR_CHNEN	0x00000200
#define TX3927_DMA_CCR_XFACT	0x00000100
#define TX3927_DMA_CCR_SNOP	0x00000080
#define TX3927_DMA_CCR_DSTINC	0x00000040
#define TX3927_DMA_CCR_SRCINC	0x00000020
#define TX3927_DMA_CCR_XFSZ(order)	(((order) << 2) & 0x0000001c)
#define TX3927_DMA_CCR_XFSZ_1W	TX3927_DMA_CCR_XFSZ(2)
#define TX3927_DMA_CCR_XFSZ_4W	TX3927_DMA_CCR_XFSZ(4)
#define TX3927_DMA_CCR_XFSZ_8W	TX3927_DMA_CCR_XFSZ(5)
#define TX3927_DMA_CCR_XFSZ_16W	TX3927_DMA_CCR_XFSZ(6)
#define TX3927_DMA_CCR_XFSZ_32W	TX3927_DMA_CCR_XFSZ(7)
#define TX3927_DMA_CCR_MEMIO	0x00000002
#define TX3927_DMA_CCR_ONEAD	0x00000001

/* bits for CSRn */
#define TX3927_DMA_CSR_CHNACT	0x00000100
#define TX3927_DMA_CSR_ABCHC	0x00000080
#define TX3927_DMA_CSR_NCHNC	0x00000040
#define TX3927_DMA_CSR_NTRNFC	0x00000020
#define TX3927_DMA_CSR_EXTDN	0x00000010
#define TX3927_DMA_CSR_CFERR	0x00000008
#define TX3927_DMA_CSR_CHERR	0x00000004
#define TX3927_DMA_CSR_DESERR	0x00000002
#define TX3927_DMA_CSR_SORERR	0x00000001

/*
 * IRC
 */
#define TX3927_IR_INT0	0
#define TX3927_IR_INT1	1
#define TX3927_IR_INT2	2
#define TX3927_IR_INT3	3
#define TX3927_IR_INT4	4
#define TX3927_IR_INT5	5
#define TX3927_IR_SIO0	6
#define TX3927_IR_SIO1	7
#define TX3927_IR_SIO(ch)	(6 + (ch))
#define TX3927_IR_DMA	8
#define TX3927_IR_PIO	9
#define TX3927_IR_PCI	10
#define TX3927_IR_TMR0	13
#define TX3927_IR_TMR1	14
#define TX3927_IR_TMR2	15
#define TX3927_NUM_IR	16

/*
 * PCIC
 */
/* bits for PCICMD */
/* see PCI_COMMAND_XXX in linux/pci.h */

/* bits for PCISTAT */
/* see PCI_STATUS_XXX in linux/pci.h */
#define PCI_STATUS_NEW_CAP	0x0010

/* bits for TC */
#define TX3927_PCIC_TC_OF16E	0x00000020
#define TX3927_PCIC_TC_IF8E	0x00000010
#define TX3927_PCIC_TC_OF8E	0x00000008

/* bits for IOBA/MBA */
/* see PCI_BASE_ADDRESS_XXX in linux/pci.h */

/* bits for PBAPMC */
#define TX3927_PCIC_PBAPMC_RPBA	0x00000004
#define TX3927_PCIC_PBAPMC_PBAEN	0x00000002
#define TX3927_PCIC_PBAPMC_BMCEN	0x00000001

/* bits for LBSTAT/LBIM */
#define TX3927_PCIC_LBIM_ALL	0x0000003e

/* bits for PCISTATIM (see also PCI_STATUS_XXX in linux/pci.h */
#define TX3927_PCIC_PCISTATIM_ALL	0x0000f900

/* bits for LBC */
#define TX3927_PCIC_LBC_IBSE	0x00004000
#define TX3927_PCIC_LBC_TIBSE	0x00002000
#define TX3927_PCIC_LBC_TMFBSE	0x00001000
#define TX3927_PCIC_LBC_HRST	0x00000800
#define TX3927_PCIC_LBC_SRST	0x00000400
#define TX3927_PCIC_LBC_EPCAD	0x00000200
#define TX3927_PCIC_LBC_MSDSE	0x00000100
#define TX3927_PCIC_LBC_CRR	0x00000080
#define TX3927_PCIC_LBC_ILMDE	0x00000040
#define TX3927_PCIC_LBC_ILIDE	0x00000020

#define TX3927_PCIC_IDSEL_AD_TO_SLOT(ad)	((ad) - 11)
#define TX3927_PCIC_MAX_DEVNU	TX3927_PCIC_IDSEL_AD_TO_SLOT(32)

/*
 * CCFG
 */
/* CCFG : Chip Configuration */
#define TX3927_CCFG_TLBOFF	0x00020000
#define TX3927_CCFG_BEOW	0x00010000
#define TX3927_CCFG_WR	0x00008000
#define TX3927_CCFG_TOE	0x00004000
#define TX3927_CCFG_PCIXARB	0x00002000
#define TX3927_CCFG_PCI3	0x00001000
#define TX3927_CCFG_PSNP	0x00000800
#define TX3927_CCFG_PPRI	0x00000400
#define TX3927_CCFG_PLLM	0x00000030
#define TX3927_CCFG_ENDIAN	0x00000004
#define TX3927_CCFG_HALT	0x00000002
#define TX3927_CCFG_ACEHOLD	0x00000001

/* PCFG : Pin Configuration */
#define TX3927_PCFG_SYSCLKEN	0x08000000
#define TX3927_PCFG_SDRCLKEN_ALL	0x07c00000
#define TX3927_PCFG_SDRCLKEN(ch)	(0x00400000<<(ch))
#define TX3927_PCFG_PCICLKEN_ALL	0x003c0000
#define TX3927_PCFG_PCICLKEN(ch)	(0x00040000<<(ch))
#define TX3927_PCFG_SELALL	0x0003ffff
#define TX3927_PCFG_SELCS	0x00020000
#define TX3927_PCFG_SELDSF	0x00010000
#define TX3927_PCFG_SELSIOC_ALL	0x0000c000
#define TX3927_PCFG_SELSIOC(ch)	(0x00004000<<(ch))
#define TX3927_PCFG_SELSIO_ALL	0x00003000
#define TX3927_PCFG_SELSIO(ch)	(0x00001000<<(ch))
#define TX3927_PCFG_SELTMR_ALL	0x00000e00
#define TX3927_PCFG_SELTMR(ch)	(0x00000200<<(ch))
#define TX3927_PCFG_SELDONE	0x00000100
#define TX3927_PCFG_INTDMA_ALL	0x000000f0
#define TX3927_PCFG_INTDMA(ch)	(0x00000010<<(ch))
#define TX3927_PCFG_SELDMA_ALL	0x0000000f
#define TX3927_PCFG_SELDMA(ch)	(0x00000001<<(ch))

#define tx3927_sdramcptr	((struct tx3927_sdramc_reg *)TX3927_SDRAMC_REG)
#define tx3927_romcptr		((struct tx3927_romc_reg *)TX3927_ROMC_REG)
#define tx3927_dmaptr		((struct tx3927_dma_reg *)TX3927_DMA_REG)
#define tx3927_pcicptr		((struct tx3927_pcic_reg *)TX3927_PCIC_REG)
#define tx3927_ccfgptr		((struct tx3927_ccfg_reg *)TX3927_CCFG_REG)
#define tx3927_tmrptr(ch)	((struct txx927_tmr_reg *)TX3927_TMR_REG(ch))
#define tx3927_sioptr(ch)	((struct txx927_sio_reg *)TX3927_SIO_REG(ch))
#define tx3927_pioptr		((struct txx927_pio_reg *)TX3927_PIO_REG)

#endif /* __ASM_TX3927_H */
