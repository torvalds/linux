/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 */
#ifndef __ASM_TX4927_TX4927_PCI_H
#define __ASM_TX4927_TX4927_PCI_H

#define TX4927_CCFG_TOE 0x00004000
#define TX4927_CCFG_TINTDIS	0x01000000

#define TX4927_PCIMEM      0x08000000
#define TX4927_PCIMEM_SIZE 0x08000000
#define TX4927_PCIIO       0x16000000
#define TX4927_PCIIO_SIZE  0x01000000

#define TX4927_SDRAMC_REG       0xff1f8000
#define TX4927_EBUSC_REG        0xff1f9000
#define TX4927_PCIC_REG         0xff1fd000
#define TX4927_CCFG_REG         0xff1fe000
#define TX4927_IRC_REG          0xff1ff600
#define TX4927_NR_TMR	3
#define TX4927_TMR_REG(ch)	(0xff1ff000 + (ch) * 0x100)
#define TX4927_CE3      0x17f00000      /* 1M */
#define TX4927_PCIRESET_ADDR    0xbc00f006
#define TX4927_PCI_CLK_ADDR     (KSEG1 + TX4927_CE3 + 0x00040020)

#define TX4927_IMSTAT_ADDR(n)   (KSEG1 + TX4927_CE3 + 0x0004001a + (n))
#define tx4927_imstat_ptr(n)    \
        ((volatile unsigned char *)TX4927_IMSTAT_ADDR(n))

/* bits for ISTAT3/IMASK3/IMSTAT3 */
#define TX4927_INT3B_PCID       0
#define TX4927_INT3B_PCIC       1
#define TX4927_INT3B_PCIB       2
#define TX4927_INT3B_PCIA       3
#define TX4927_INT3F_PCID       (1 << TX4927_INT3B_PCID)
#define TX4927_INT3F_PCIC       (1 << TX4927_INT3B_PCIC)
#define TX4927_INT3F_PCIB       (1 << TX4927_INT3B_PCIB)
#define TX4927_INT3F_PCIA       (1 << TX4927_INT3B_PCIA)

/* bits for PCI_CLK (S6) */
#define TX4927_PCI_CLK_HOST     0x80
#define TX4927_PCI_CLK_MASK     (0x0f << 3)
#define TX4927_PCI_CLK_33       (0x01 << 3)
#define TX4927_PCI_CLK_25       (0x04 << 3)
#define TX4927_PCI_CLK_66       (0x09 << 3)
#define TX4927_PCI_CLK_50       (0x0c << 3)
#define TX4927_PCI_CLK_ACK      0x04
#define TX4927_PCI_CLK_ACE      0x02
#define TX4927_PCI_CLK_ENDIAN   0x01
#define TX4927_NR_IRQ_LOCAL     TX4927_IRQ_PIC_BEG
#define TX4927_NR_IRQ_IRC       32      /* On-Chip IRC */

#define TX4927_IR_PCIC  	16
#define TX4927_IR_PCIERR        22
#define TX4927_IR_PCIPMA        23
#define TX4927_IRQ_IRC_PCIC     (TX4927_NR_IRQ_LOCAL + TX4927_IR_PCIC)
#define TX4927_IRQ_IRC_PCIERR   (TX4927_NR_IRQ_LOCAL + TX4927_IR_PCIERR)
#define TX4927_IRQ_IOC1         (TX4927_NR_IRQ_LOCAL + TX4927_NR_IRQ_IRC)
#define TX4927_IRQ_IOC_PCID     (TX4927_IRQ_IOC1 + TX4927_INT3B_PCID)
#define TX4927_IRQ_IOC_PCIC     (TX4927_IRQ_IOC1 + TX4927_INT3B_PCIC)
#define TX4927_IRQ_IOC_PCIB     (TX4927_IRQ_IOC1 + TX4927_INT3B_PCIB)
#define TX4927_IRQ_IOC_PCIA     (TX4927_IRQ_IOC1 + TX4927_INT3B_PCIA)

#ifdef _LANGUAGE_ASSEMBLY
#define _CONST64(c)     c
#else
#define _CONST64(c)     c##ull

#include <asm/byteorder.h>

#define tx4927_pcireset_ptr     \
        ((volatile unsigned char *)TX4927_PCIRESET_ADDR)
#define tx4927_pci_clk_ptr      \
        ((volatile unsigned char *)TX4927_PCI_CLK_ADDR)

struct tx4927_sdramc_reg {
        volatile unsigned long long cr[4];
        volatile unsigned long long unused0[4];
        volatile unsigned long long tr;
        volatile unsigned long long unused1[2];
        volatile unsigned long long cmd;
};

struct tx4927_ebusc_reg {
        volatile unsigned long long cr[8];
};

struct tx4927_ccfg_reg {
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
};

struct tx4927_pcic_reg {
        volatile unsigned long pciid;
        volatile unsigned long pcistatus;
        volatile unsigned long pciccrev;
        volatile unsigned long pcicfg1;
        volatile unsigned long p2gm0plbase;             /* +10 */
        volatile unsigned long p2gm0pubase;
        volatile unsigned long p2gm1plbase;
        volatile unsigned long p2gm1pubase;
        volatile unsigned long p2gm2pbase;              /* +20 */
        volatile unsigned long p2giopbase;
        volatile unsigned long unused0;
        volatile unsigned long pcisid;
        volatile unsigned long unused1;         /* +30 */
        volatile unsigned long pcicapptr;
        volatile unsigned long unused2;
        volatile unsigned long pcicfg2;
        volatile unsigned long g2ptocnt;                /* +40 */
        volatile unsigned long unused3[15];
        volatile unsigned long g2pstatus;               /* +80 */
        volatile unsigned long g2pmask;
        volatile unsigned long pcisstatus;
        volatile unsigned long pcimask;
        volatile unsigned long p2gcfg;          /* +90 */
        volatile unsigned long p2gstatus;
        volatile unsigned long p2gmask;
        volatile unsigned long p2gccmd;
        volatile unsigned long unused4[24];             /* +a0 */
        volatile unsigned long pbareqport;              /* +100 */
        volatile unsigned long pbacfg;
        volatile unsigned long pbastatus;
        volatile unsigned long pbamask;
        volatile unsigned long pbabm;           /* +110 */
        volatile unsigned long pbacreq;
        volatile unsigned long pbacgnt;
        volatile unsigned long pbacstate;
        volatile unsigned long long g2pmgbase[3];               /* +120 */
        volatile unsigned long long g2piogbase;
        volatile unsigned long g2pmmask[3];             /* +140 */
        volatile unsigned long g2piomask;
        volatile unsigned long long g2pmpbase[3];               /* +150 */
        volatile unsigned long long g2piopbase;
        volatile unsigned long pciccfg;         /* +170 */
        volatile unsigned long pcicstatus;
        volatile unsigned long pcicmask;
        volatile unsigned long unused5;
        volatile unsigned long long p2gmgbase[3];               /* +180 */
        volatile unsigned long long p2giogbase;
        volatile unsigned long g2pcfgadrs;              /* +1a0 */
        volatile unsigned long g2pcfgdata;
        volatile unsigned long unused6[8];
        volatile unsigned long g2pintack;
        volatile unsigned long g2pspc;
        volatile unsigned long unused7[12];             /* +1d0 */
        volatile unsigned long long pdmca;              /* +200 */
        volatile unsigned long long pdmga;
        volatile unsigned long long pdmpa;
        volatile unsigned long long pdmcut;
        volatile unsigned long long pdmcnt;             /* +220 */
        volatile unsigned long long pdmsts;
        volatile unsigned long long unused8[2];
        volatile unsigned long long pdmdb[4];           /* +240 */
        volatile unsigned long long pdmtdh;             /* +260 */
        volatile unsigned long long pdmdms;
};

#endif /* _LANGUAGE_ASSEMBLY */

/*
 * PCIC
 */

/* bits for G2PSTATUS/G2PMASK */
#define TX4927_PCIC_G2PSTATUS_ALL       0x00000003
#define TX4927_PCIC_G2PSTATUS_TTOE      0x00000002
#define TX4927_PCIC_G2PSTATUS_RTOE      0x00000001

/* bits for PCIMASK (see also PCI_STATUS_XXX in linux/pci.h */
#define TX4927_PCIC_PCISTATUS_ALL       0x0000f900

/* bits for PBACFG */
#define TX4927_PCIC_PBACFG_RPBA 0x00000004
#define TX4927_PCIC_PBACFG_PBAEN        0x00000002
#define TX4927_PCIC_PBACFG_BMCEN        0x00000001

/* bits for G2PMnGBASE */
#define TX4927_PCIC_G2PMnGBASE_BSDIS    _CONST64(0x0000002000000000)
#define TX4927_PCIC_G2PMnGBASE_ECHG     _CONST64(0x0000001000000000)

/* bits for G2PIOGBASE */
#define TX4927_PCIC_G2PIOGBASE_BSDIS    _CONST64(0x0000002000000000)
#define TX4927_PCIC_G2PIOGBASE_ECHG     _CONST64(0x0000001000000000)

/* bits for PCICSTATUS/PCICMASK */
#define TX4927_PCIC_PCICSTATUS_ALL      0x000007dc

/* bits for PCICCFG */
#define TX4927_PCIC_PCICCFG_LBWC_MASK   0x0fff0000
#define TX4927_PCIC_PCICCFG_HRST        0x00000800
#define TX4927_PCIC_PCICCFG_SRST        0x00000400
#define TX4927_PCIC_PCICCFG_IRBER       0x00000200
#define TX4927_PCIC_PCICCFG_IMSE0       0x00000100
#define TX4927_PCIC_PCICCFG_IMSE1       0x00000080
#define TX4927_PCIC_PCICCFG_IMSE2       0x00000040
#define TX4927_PCIC_PCICCFG_IISE        0x00000020
#define TX4927_PCIC_PCICCFG_ATR 0x00000010
#define TX4927_PCIC_PCICCFG_ICAE        0x00000008

/* bits for P2GMnGBASE */
#define TX4927_PCIC_P2GMnGBASE_TMEMEN   _CONST64(0x0000004000000000)
#define TX4927_PCIC_P2GMnGBASE_TBSDIS   _CONST64(0x0000002000000000)
#define TX4927_PCIC_P2GMnGBASE_TECHG    _CONST64(0x0000001000000000)

/* bits for P2GIOGBASE */
#define TX4927_PCIC_P2GIOGBASE_TIOEN    _CONST64(0x0000004000000000)
#define TX4927_PCIC_P2GIOGBASE_TBSDIS   _CONST64(0x0000002000000000)
#define TX4927_PCIC_P2GIOGBASE_TECHG    _CONST64(0x0000001000000000)

#define TX4927_PCIC_IDSEL_AD_TO_SLOT(ad)        ((ad) - 11)
#define TX4927_PCIC_MAX_DEVNU   TX4927_PCIC_IDSEL_AD_TO_SLOT(32)

/*
 * CCFG
 */
/* CCFG : Chip Configuration */
#define TX4927_CCFG_PCI66       0x00800000
#define TX4927_CCFG_PCIMIDE     0x00400000
#define TX4927_CCFG_PCIXARB     0x00002000
#define TX4927_CCFG_PCIDIVMODE_MASK     0x00001800
#define TX4927_CCFG_PCIDIVMODE_2_5      0x00000000
#define TX4927_CCFG_PCIDIVMODE_3        0x00000800
#define TX4927_CCFG_PCIDIVMODE_5        0x00001000
#define TX4927_CCFG_PCIDIVMODE_6        0x00001800

#define TX4937_CCFG_PCIDIVMODE_MASK	0x00001c00
#define TX4937_CCFG_PCIDIVMODE_8	0x00000000
#define TX4937_CCFG_PCIDIVMODE_4	0x00000400
#define TX4937_CCFG_PCIDIVMODE_9 	0x00000800
#define TX4937_CCFG_PCIDIVMODE_4_5	0x00000c00
#define TX4937_CCFG_PCIDIVMODE_10	0x00001000
#define TX4937_CCFG_PCIDIVMODE_5	0x00001400
#define TX4937_CCFG_PCIDIVMODE_11	0x00001800
#define TX4937_CCFG_PCIDIVMODE_5_5	0x00001c00

/* PCFG : Pin Configuration */
#define TX4927_PCFG_PCICLKEN_ALL        0x003f0000
#define TX4927_PCFG_PCICLKEN(ch)        (0x00010000<<(ch))

/* CLKCTR : Clock Control */
#define TX4927_CLKCTR_PCICKD    0x00400000
#define TX4927_CLKCTR_PCIRST    0x00000040


#ifndef _LANGUAGE_ASSEMBLY

#define tx4927_sdramcptr        ((struct tx4927_sdramc_reg *)TX4927_SDRAMC_REG)
#define tx4927_pcicptr          ((struct tx4927_pcic_reg *)TX4927_PCIC_REG)
#define tx4927_ccfgptr          ((struct tx4927_ccfg_reg *)TX4927_CCFG_REG)
#define tx4927_ebuscptr         ((struct tx4927_ebusc_reg *)TX4927_EBUSC_REG)

#endif /* _LANGUAGE_ASSEMBLY */

#endif /* __ASM_TX4927_TX4927_PCI_H */
