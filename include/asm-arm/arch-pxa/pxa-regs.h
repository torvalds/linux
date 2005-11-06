/*
 *  linux/include/asm-arm/arch-pxa/pxa-regs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PXA_REGS_H
#define __PXA_REGS_H

#include <linux/config.h>

/*
 * PXA Chip selects
 */

#define PXA_CS0_PHYS	0x00000000
#define PXA_CS1_PHYS	0x04000000
#define PXA_CS2_PHYS	0x08000000
#define PXA_CS3_PHYS	0x0C000000
#define PXA_CS4_PHYS	0x10000000
#define PXA_CS5_PHYS	0x14000000


/*
 * Personal Computer Memory Card International Association (PCMCIA) sockets
 */

#define PCMCIAPrtSp	0x04000000	/* PCMCIA Partition Space [byte]   */
#define PCMCIASp	(4*PCMCIAPrtSp)	/* PCMCIA Space [byte]             */
#define PCMCIAIOSp	PCMCIAPrtSp	/* PCMCIA I/O Space [byte]         */
#define PCMCIAAttrSp	PCMCIAPrtSp	/* PCMCIA Attribute Space [byte]   */
#define PCMCIAMemSp	PCMCIAPrtSp	/* PCMCIA Memory Space [byte]      */

#define PCMCIA0Sp	PCMCIASp	/* PCMCIA 0 Space [byte]           */
#define PCMCIA0IOSp	PCMCIAIOSp	/* PCMCIA 0 I/O Space [byte]       */
#define PCMCIA0AttrSp	PCMCIAAttrSp	/* PCMCIA 0 Attribute Space [byte] */
#define PCMCIA0MemSp	PCMCIAMemSp	/* PCMCIA 0 Memory Space [byte]    */

#define PCMCIA1Sp	PCMCIASp	/* PCMCIA 1 Space [byte]           */
#define PCMCIA1IOSp	PCMCIAIOSp	/* PCMCIA 1 I/O Space [byte]       */
#define PCMCIA1AttrSp	PCMCIAAttrSp	/* PCMCIA 1 Attribute Space [byte] */
#define PCMCIA1MemSp	PCMCIAMemSp	/* PCMCIA 1 Memory Space [byte]    */

#define _PCMCIA(Nb)	        	/* PCMCIA [0..1]                   */ \
                	(0x20000000 + (Nb)*PCMCIASp)
#define _PCMCIAIO(Nb)	_PCMCIA (Nb)	/* PCMCIA I/O [0..1]               */
#define _PCMCIAAttr(Nb)	        	/* PCMCIA Attribute [0..1]         */ \
                	(_PCMCIA (Nb) + 2*PCMCIAPrtSp)
#define _PCMCIAMem(Nb)	        	/* PCMCIA Memory [0..1]            */ \
                	(_PCMCIA (Nb) + 3*PCMCIAPrtSp)

#define _PCMCIA0	_PCMCIA (0)	/* PCMCIA 0                        */
#define _PCMCIA0IO	_PCMCIAIO (0)	/* PCMCIA 0 I/O                    */
#define _PCMCIA0Attr	_PCMCIAAttr (0)	/* PCMCIA 0 Attribute              */
#define _PCMCIA0Mem	_PCMCIAMem (0)	/* PCMCIA 0 Memory                 */

#define _PCMCIA1	_PCMCIA (1)	/* PCMCIA 1                        */
#define _PCMCIA1IO	_PCMCIAIO (1)	/* PCMCIA 1 I/O                    */
#define _PCMCIA1Attr	_PCMCIAAttr (1)	/* PCMCIA 1 Attribute              */
#define _PCMCIA1Mem	_PCMCIAMem (1)	/* PCMCIA 1 Memory                 */



/*
 * DMA Controller
 */

#define DCSR0		__REG(0x40000000)  /* DMA Control / Status Register for Channel 0 */
#define DCSR1		__REG(0x40000004)  /* DMA Control / Status Register for Channel 1 */
#define DCSR2		__REG(0x40000008)  /* DMA Control / Status Register for Channel 2 */
#define DCSR3		__REG(0x4000000c)  /* DMA Control / Status Register for Channel 3 */
#define DCSR4		__REG(0x40000010)  /* DMA Control / Status Register for Channel 4 */
#define DCSR5		__REG(0x40000014)  /* DMA Control / Status Register for Channel 5 */
#define DCSR6		__REG(0x40000018)  /* DMA Control / Status Register for Channel 6 */
#define DCSR7		__REG(0x4000001c)  /* DMA Control / Status Register for Channel 7 */
#define DCSR8		__REG(0x40000020)  /* DMA Control / Status Register for Channel 8 */
#define DCSR9		__REG(0x40000024)  /* DMA Control / Status Register for Channel 9 */
#define DCSR10		__REG(0x40000028)  /* DMA Control / Status Register for Channel 10 */
#define DCSR11		__REG(0x4000002c)  /* DMA Control / Status Register for Channel 11 */
#define DCSR12		__REG(0x40000030)  /* DMA Control / Status Register for Channel 12 */
#define DCSR13		__REG(0x40000034)  /* DMA Control / Status Register for Channel 13 */
#define DCSR14		__REG(0x40000038)  /* DMA Control / Status Register for Channel 14 */
#define DCSR15		__REG(0x4000003c)  /* DMA Control / Status Register for Channel 15 */

#define DCSR(x)		__REG2(0x40000000, (x) << 2)

#define DCSR_RUN	(1 << 31)	/* Run Bit (read / write) */
#define DCSR_NODESC	(1 << 30)	/* No-Descriptor Fetch (read / write) */
#define DCSR_STOPIRQEN	(1 << 29)	/* Stop Interrupt Enable (read / write) */
#ifdef CONFIG_PXA27x
#define DCSR_EORIRQEN	(1 << 28)       /* End of Receive Interrupt Enable (R/W) */
#define DCSR_EORJMPEN	(1 << 27)       /* Jump to next descriptor on EOR */
#define DCSR_EORSTOPEN	(1 << 26)       /* STOP on an EOR */
#define DCSR_SETCMPST	(1 << 25)       /* Set Descriptor Compare Status */
#define DCSR_CLRCMPST	(1 << 24)       /* Clear Descriptor Compare Status */
#define DCSR_CMPST	(1 << 10)       /* The Descriptor Compare Status */
#define DCSR_ENRINTR	(1 << 9)        /* The end of Receive */
#endif
#define DCSR_REQPEND	(1 << 8)	/* Request Pending (read-only) */
#define DCSR_STOPSTATE	(1 << 3)	/* Stop State (read-only) */
#define DCSR_ENDINTR	(1 << 2)	/* End Interrupt (read / write) */
#define DCSR_STARTINTR	(1 << 1)	/* Start Interrupt (read / write) */
#define DCSR_BUSERR	(1 << 0)	/* Bus Error Interrupt (read / write) */

#define DINT		__REG(0x400000f0)  /* DMA Interrupt Register */

#define DRCMR(n)	__REG2(0x40000100, (n)<<2)
#define DRCMR0		__REG(0x40000100)  /* Request to Channel Map Register for DREQ 0 */
#define DRCMR1		__REG(0x40000104)  /* Request to Channel Map Register for DREQ 1 */
#define DRCMR2		__REG(0x40000108)  /* Request to Channel Map Register for I2S receive Request */
#define DRCMR3		__REG(0x4000010c)  /* Request to Channel Map Register for I2S transmit Request */
#define DRCMR4		__REG(0x40000110)  /* Request to Channel Map Register for BTUART receive Request */
#define DRCMR5		__REG(0x40000114)  /* Request to Channel Map Register for BTUART transmit Request. */
#define DRCMR6		__REG(0x40000118)  /* Request to Channel Map Register for FFUART receive Request */
#define DRCMR7		__REG(0x4000011c)  /* Request to Channel Map Register for FFUART transmit Request */
#define DRCMR8		__REG(0x40000120)  /* Request to Channel Map Register for AC97 microphone Request */
#define DRCMR9		__REG(0x40000124)  /* Request to Channel Map Register for AC97 modem receive Request */
#define DRCMR10		__REG(0x40000128)  /* Request to Channel Map Register for AC97 modem transmit Request */
#define DRCMR11		__REG(0x4000012c)  /* Request to Channel Map Register for AC97 audio receive Request */
#define DRCMR12		__REG(0x40000130)  /* Request to Channel Map Register for AC97 audio transmit Request */
#define DRCMR13		__REG(0x40000134)  /* Request to Channel Map Register for SSP receive Request */
#define DRCMR14		__REG(0x40000138)  /* Request to Channel Map Register for SSP transmit Request */
#define DRCMR15		__REG(0x4000013c)  /* Request to Channel Map Register for SSP2 receive Request */
#define DRCMR16		__REG(0x40000140)  /* Request to Channel Map Register for SSP2 transmit Request */
#define DRCMR17		__REG(0x40000144)  /* Request to Channel Map Register for ICP receive Request */
#define DRCMR18		__REG(0x40000148)  /* Request to Channel Map Register for ICP transmit Request */
#define DRCMR19		__REG(0x4000014c)  /* Request to Channel Map Register for STUART receive Request */
#define DRCMR20		__REG(0x40000150)  /* Request to Channel Map Register for STUART transmit Request */
#define DRCMR21		__REG(0x40000154)  /* Request to Channel Map Register for MMC receive Request */
#define DRCMR22		__REG(0x40000158)  /* Request to Channel Map Register for MMC transmit Request */
#define DRCMR23		__REG(0x4000015c)  /* Reserved */
#define DRCMR24		__REG(0x40000160)  /* Reserved */
#define DRCMR25		__REG(0x40000164)  /* Request to Channel Map Register for USB endpoint 1 Request */
#define DRCMR26		__REG(0x40000168)  /* Request to Channel Map Register for USB endpoint 2 Request */
#define DRCMR27		__REG(0x4000016C)  /* Request to Channel Map Register for USB endpoint 3 Request */
#define DRCMR28		__REG(0x40000170)  /* Request to Channel Map Register for USB endpoint 4 Request */
#define DRCMR29		__REG(0x40000174)  /* Reserved */
#define DRCMR30		__REG(0x40000178)  /* Request to Channel Map Register for USB endpoint 6 Request */
#define DRCMR31		__REG(0x4000017C)  /* Request to Channel Map Register for USB endpoint 7 Request */
#define DRCMR32		__REG(0x40000180)  /* Request to Channel Map Register for USB endpoint 8 Request */
#define DRCMR33		__REG(0x40000184)  /* Request to Channel Map Register for USB endpoint 9 Request */
#define DRCMR34		__REG(0x40000188)  /* Reserved */
#define DRCMR35		__REG(0x4000018C)  /* Request to Channel Map Register for USB endpoint 11 Request */
#define DRCMR36		__REG(0x40000190)  /* Request to Channel Map Register for USB endpoint 12 Request */
#define DRCMR37		__REG(0x40000194)  /* Request to Channel Map Register for USB endpoint 13 Request */
#define DRCMR38		__REG(0x40000198)  /* Request to Channel Map Register for USB endpoint 14 Request */
#define DRCMR39		__REG(0x4000019C)  /* Reserved */
#define DRCMR66		__REG(0x40001108)  /* Request to Channel Map Register for SSP3 receive Request */
#define DRCMR67		__REG(0x4000110C)  /* Request to Channel Map Register for SSP3 transmit Request */
#define DRCMR68		__REG(0x40001110)  /* Request to Channel Map Register for Camera FIFO 0 Request */
#define DRCMR69		__REG(0x40001114)  /* Request to Channel Map Register for Camera FIFO 1 Request */
#define DRCMR70		__REG(0x40001118)  /* Request to Channel Map Register for Camera FIFO 2 Request */

#define DRCMRRXSADR	DRCMR2
#define DRCMRTXSADR	DRCMR3
#define DRCMRRXBTRBR	DRCMR4
#define DRCMRTXBTTHR	DRCMR5
#define DRCMRRXFFRBR	DRCMR6
#define DRCMRTXFFTHR	DRCMR7
#define DRCMRRXMCDR	DRCMR8
#define DRCMRRXMODR	DRCMR9
#define DRCMRTXMODR	DRCMR10
#define DRCMRRXPCDR	DRCMR11
#define DRCMRTXPCDR	DRCMR12
#define DRCMRRXSSDR	DRCMR13
#define DRCMRTXSSDR	DRCMR14
#define DRCMRRXSS2DR   DRCMR15
#define DRCMRTXSS2DR   DRCMR16
#define DRCMRRXICDR	DRCMR17
#define DRCMRTXICDR	DRCMR18
#define DRCMRRXSTRBR	DRCMR19
#define DRCMRTXSTTHR	DRCMR20
#define DRCMRRXMMC	DRCMR21
#define DRCMRTXMMC	DRCMR22
#define DRCMRRXSS3DR   DRCMR66
#define DRCMRTXSS3DR   DRCMR67
#define DRCMRUDC(x)	DRCMR((x) + 24)

#define DRCMR_MAPVLD	(1 << 7)	/* Map Valid (read / write) */
#define DRCMR_CHLNUM	0x1f		/* mask for Channel Number (read / write) */

#define DDADR0		__REG(0x40000200)  /* DMA Descriptor Address Register Channel 0 */
#define DSADR0		__REG(0x40000204)  /* DMA Source Address Register Channel 0 */
#define DTADR0		__REG(0x40000208)  /* DMA Target Address Register Channel 0 */
#define DCMD0		__REG(0x4000020c)  /* DMA Command Address Register Channel 0 */
#define DDADR1		__REG(0x40000210)  /* DMA Descriptor Address Register Channel 1 */
#define DSADR1		__REG(0x40000214)  /* DMA Source Address Register Channel 1 */
#define DTADR1		__REG(0x40000218)  /* DMA Target Address Register Channel 1 */
#define DCMD1		__REG(0x4000021c)  /* DMA Command Address Register Channel 1 */
#define DDADR2		__REG(0x40000220)  /* DMA Descriptor Address Register Channel 2 */
#define DSADR2		__REG(0x40000224)  /* DMA Source Address Register Channel 2 */
#define DTADR2		__REG(0x40000228)  /* DMA Target Address Register Channel 2 */
#define DCMD2		__REG(0x4000022c)  /* DMA Command Address Register Channel 2 */
#define DDADR3		__REG(0x40000230)  /* DMA Descriptor Address Register Channel 3 */
#define DSADR3		__REG(0x40000234)  /* DMA Source Address Register Channel 3 */
#define DTADR3		__REG(0x40000238)  /* DMA Target Address Register Channel 3 */
#define DCMD3		__REG(0x4000023c)  /* DMA Command Address Register Channel 3 */
#define DDADR4		__REG(0x40000240)  /* DMA Descriptor Address Register Channel 4 */
#define DSADR4		__REG(0x40000244)  /* DMA Source Address Register Channel 4 */
#define DTADR4		__REG(0x40000248)  /* DMA Target Address Register Channel 4 */
#define DCMD4		__REG(0x4000024c)  /* DMA Command Address Register Channel 4 */
#define DDADR5		__REG(0x40000250)  /* DMA Descriptor Address Register Channel 5 */
#define DSADR5		__REG(0x40000254)  /* DMA Source Address Register Channel 5 */
#define DTADR5		__REG(0x40000258)  /* DMA Target Address Register Channel 5 */
#define DCMD5		__REG(0x4000025c)  /* DMA Command Address Register Channel 5 */
#define DDADR6		__REG(0x40000260)  /* DMA Descriptor Address Register Channel 6 */
#define DSADR6		__REG(0x40000264)  /* DMA Source Address Register Channel 6 */
#define DTADR6		__REG(0x40000268)  /* DMA Target Address Register Channel 6 */
#define DCMD6		__REG(0x4000026c)  /* DMA Command Address Register Channel 6 */
#define DDADR7		__REG(0x40000270)  /* DMA Descriptor Address Register Channel 7 */
#define DSADR7		__REG(0x40000274)  /* DMA Source Address Register Channel 7 */
#define DTADR7		__REG(0x40000278)  /* DMA Target Address Register Channel 7 */
#define DCMD7		__REG(0x4000027c)  /* DMA Command Address Register Channel 7 */
#define DDADR8		__REG(0x40000280)  /* DMA Descriptor Address Register Channel 8 */
#define DSADR8		__REG(0x40000284)  /* DMA Source Address Register Channel 8 */
#define DTADR8		__REG(0x40000288)  /* DMA Target Address Register Channel 8 */
#define DCMD8		__REG(0x4000028c)  /* DMA Command Address Register Channel 8 */
#define DDADR9		__REG(0x40000290)  /* DMA Descriptor Address Register Channel 9 */
#define DSADR9		__REG(0x40000294)  /* DMA Source Address Register Channel 9 */
#define DTADR9		__REG(0x40000298)  /* DMA Target Address Register Channel 9 */
#define DCMD9		__REG(0x4000029c)  /* DMA Command Address Register Channel 9 */
#define DDADR10		__REG(0x400002a0)  /* DMA Descriptor Address Register Channel 10 */
#define DSADR10		__REG(0x400002a4)  /* DMA Source Address Register Channel 10 */
#define DTADR10		__REG(0x400002a8)  /* DMA Target Address Register Channel 10 */
#define DCMD10		__REG(0x400002ac)  /* DMA Command Address Register Channel 10 */
#define DDADR11		__REG(0x400002b0)  /* DMA Descriptor Address Register Channel 11 */
#define DSADR11		__REG(0x400002b4)  /* DMA Source Address Register Channel 11 */
#define DTADR11		__REG(0x400002b8)  /* DMA Target Address Register Channel 11 */
#define DCMD11		__REG(0x400002bc)  /* DMA Command Address Register Channel 11 */
#define DDADR12		__REG(0x400002c0)  /* DMA Descriptor Address Register Channel 12 */
#define DSADR12		__REG(0x400002c4)  /* DMA Source Address Register Channel 12 */
#define DTADR12		__REG(0x400002c8)  /* DMA Target Address Register Channel 12 */
#define DCMD12		__REG(0x400002cc)  /* DMA Command Address Register Channel 12 */
#define DDADR13		__REG(0x400002d0)  /* DMA Descriptor Address Register Channel 13 */
#define DSADR13		__REG(0x400002d4)  /* DMA Source Address Register Channel 13 */
#define DTADR13		__REG(0x400002d8)  /* DMA Target Address Register Channel 13 */
#define DCMD13		__REG(0x400002dc)  /* DMA Command Address Register Channel 13 */
#define DDADR14		__REG(0x400002e0)  /* DMA Descriptor Address Register Channel 14 */
#define DSADR14		__REG(0x400002e4)  /* DMA Source Address Register Channel 14 */
#define DTADR14		__REG(0x400002e8)  /* DMA Target Address Register Channel 14 */
#define DCMD14		__REG(0x400002ec)  /* DMA Command Address Register Channel 14 */
#define DDADR15		__REG(0x400002f0)  /* DMA Descriptor Address Register Channel 15 */
#define DSADR15		__REG(0x400002f4)  /* DMA Source Address Register Channel 15 */
#define DTADR15		__REG(0x400002f8)  /* DMA Target Address Register Channel 15 */
#define DCMD15		__REG(0x400002fc)  /* DMA Command Address Register Channel 15 */

#define DDADR(x)	__REG2(0x40000200, (x) << 4)
#define DSADR(x)	__REG2(0x40000204, (x) << 4)
#define DTADR(x)	__REG2(0x40000208, (x) << 4)
#define DCMD(x)		__REG2(0x4000020c, (x) << 4)

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor (mask) */
#define DDADR_STOP	(1 << 0)	/* Stop (read / write) */

#define DCMD_INCSRCADDR	(1 << 31)	/* Source Address Increment Setting. */
#define DCMD_INCTRGADDR	(1 << 30)	/* Target Address Increment Setting. */
#define DCMD_FLOWSRC	(1 << 29)	/* Flow Control by the source. */
#define DCMD_FLOWTRG	(1 << 28)	/* Flow Control by the target. */
#define DCMD_STARTIRQEN	(1 << 22)	/* Start Interrupt Enable */
#define DCMD_ENDIRQEN	(1 << 21)	/* End Interrupt Enable */
#define DCMD_ENDIAN	(1 << 18)	/* Device Endian-ness. */
#define DCMD_BURST8	(1 << 16)	/* 8 byte burst */
#define DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define DCMD_WIDTH1	(1 << 14)	/* 1 byte width */
#define DCMD_WIDTH2	(2 << 14)	/* 2 byte width (HalfWord) */
#define DCMD_WIDTH4	(3 << 14)	/* 4 byte width (Word) */
#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */


/*
 * UARTs
 */

/* Full Function UART (FFUART) */
#define FFUART		FFRBR
#define FFRBR		__REG(0x40100000)  /* Receive Buffer Register (read only) */
#define FFTHR		__REG(0x40100000)  /* Transmit Holding Register (write only) */
#define FFIER		__REG(0x40100004)  /* Interrupt Enable Register (read/write) */
#define FFIIR		__REG(0x40100008)  /* Interrupt ID Register (read only) */
#define FFFCR		__REG(0x40100008)  /* FIFO Control Register (write only) */
#define FFLCR		__REG(0x4010000C)  /* Line Control Register (read/write) */
#define FFMCR		__REG(0x40100010)  /* Modem Control Register (read/write) */
#define FFLSR		__REG(0x40100014)  /* Line Status Register (read only) */
#define FFMSR		__REG(0x40100018)  /* Modem Status Register (read only) */
#define FFSPR		__REG(0x4010001C)  /* Scratch Pad Register (read/write) */
#define FFISR		__REG(0x40100020)  /* Infrared Selection Register (read/write) */
#define FFDLL		__REG(0x40100000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define FFDLH		__REG(0x40100004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Bluetooth UART (BTUART) */
#define BTUART		BTRBR
#define BTRBR		__REG(0x40200000)  /* Receive Buffer Register (read only) */
#define BTTHR		__REG(0x40200000)  /* Transmit Holding Register (write only) */
#define BTIER		__REG(0x40200004)  /* Interrupt Enable Register (read/write) */
#define BTIIR		__REG(0x40200008)  /* Interrupt ID Register (read only) */
#define BTFCR		__REG(0x40200008)  /* FIFO Control Register (write only) */
#define BTLCR		__REG(0x4020000C)  /* Line Control Register (read/write) */
#define BTMCR		__REG(0x40200010)  /* Modem Control Register (read/write) */
#define BTLSR		__REG(0x40200014)  /* Line Status Register (read only) */
#define BTMSR		__REG(0x40200018)  /* Modem Status Register (read only) */
#define BTSPR		__REG(0x4020001C)  /* Scratch Pad Register (read/write) */
#define BTISR		__REG(0x40200020)  /* Infrared Selection Register (read/write) */
#define BTDLL		__REG(0x40200000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define BTDLH		__REG(0x40200004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Standard UART (STUART) */
#define STUART		STRBR
#define STRBR		__REG(0x40700000)  /* Receive Buffer Register (read only) */
#define STTHR		__REG(0x40700000)  /* Transmit Holding Register (write only) */
#define STIER		__REG(0x40700004)  /* Interrupt Enable Register (read/write) */
#define STIIR		__REG(0x40700008)  /* Interrupt ID Register (read only) */
#define STFCR		__REG(0x40700008)  /* FIFO Control Register (write only) */
#define STLCR		__REG(0x4070000C)  /* Line Control Register (read/write) */
#define STMCR		__REG(0x40700010)  /* Modem Control Register (read/write) */
#define STLSR		__REG(0x40700014)  /* Line Status Register (read only) */
#define STMSR		__REG(0x40700018)  /* Reserved */
#define STSPR		__REG(0x4070001C)  /* Scratch Pad Register (read/write) */
#define STISR		__REG(0x40700020)  /* Infrared Selection Register (read/write) */
#define STDLL		__REG(0x40700000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define STDLH		__REG(0x40700004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

/* Hardware UART (HWUART) */
#define HWUART		HWRBR
#define HWRBR		__REG(0x41600000)  /* Receive Buffer Register (read only) */
#define HWTHR		__REG(0x41600000)  /* Transmit Holding Register (write only) */
#define HWIER		__REG(0x41600004)  /* Interrupt Enable Register (read/write) */
#define HWIIR		__REG(0x41600008)  /* Interrupt ID Register (read only) */
#define HWFCR		__REG(0x41600008)  /* FIFO Control Register (write only) */
#define HWLCR		__REG(0x4160000C)  /* Line Control Register (read/write) */
#define HWMCR		__REG(0x41600010)  /* Modem Control Register (read/write) */
#define HWLSR		__REG(0x41600014)  /* Line Status Register (read only) */
#define HWMSR		__REG(0x41600018)  /* Modem Status Register (read only) */
#define HWSPR		__REG(0x4160001C)  /* Scratch Pad Register (read/write) */
#define HWISR		__REG(0x41600020)  /* Infrared Selection Register (read/write) */
#define HWFOR		__REG(0x41600024)  /* Receive FIFO Occupancy Register (read only) */
#define HWABR		__REG(0x41600028)  /* Auto-Baud Control Register (read/write) */
#define HWACR		__REG(0x4160002C)  /* Auto-Baud Count Register (read only) */
#define HWDLL		__REG(0x41600000)  /* Divisor Latch Low Register (DLAB = 1) (read/write) */
#define HWDLH		__REG(0x41600004)  /* Divisor Latch High Register (DLAB = 1) (read/write) */

#define IER_DMAE	(1 << 7)	/* DMA Requests Enable */
#define IER_UUE		(1 << 6)	/* UART Unit Enable */
#define IER_NRZE	(1 << 5)	/* NRZ coding Enable */
#define IER_RTIOE	(1 << 4)	/* Receiver Time Out Interrupt Enable */
#define IER_MIE		(1 << 3)	/* Modem Interrupt Enable */
#define IER_RLSE	(1 << 2)	/* Receiver Line Status Interrupt Enable */
#define IER_TIE		(1 << 1)	/* Transmit Data request Interrupt Enable */
#define IER_RAVIE	(1 << 0)	/* Receiver Data Available Interrupt Enable */

#define IIR_FIFOES1	(1 << 7)	/* FIFO Mode Enable Status */
#define IIR_FIFOES0	(1 << 6)	/* FIFO Mode Enable Status */
#define IIR_TOD		(1 << 3)	/* Time Out Detected */
#define IIR_IID2	(1 << 2)	/* Interrupt Source Encoded */
#define IIR_IID1	(1 << 1)	/* Interrupt Source Encoded */
#define IIR_IP		(1 << 0)	/* Interrupt Pending (active low) */

#define FCR_ITL2	(1 << 7)	/* Interrupt Trigger Level */
#define FCR_ITL1	(1 << 6)	/* Interrupt Trigger Level */
#define FCR_RESETTF	(1 << 2)	/* Reset Transmitter FIFO */
#define FCR_RESETRF	(1 << 1)	/* Reset Receiver FIFO */
#define FCR_TRFIFOE	(1 << 0)	/* Transmit and Receive FIFO Enable */
#define FCR_ITL_1	(0)
#define FCR_ITL_8	(FCR_ITL1)
#define FCR_ITL_16	(FCR_ITL2)
#define FCR_ITL_32	(FCR_ITL2|FCR_ITL1)

#define LCR_DLAB	(1 << 7)	/* Divisor Latch Access Bit */
#define LCR_SB		(1 << 6)	/* Set Break */
#define LCR_STKYP	(1 << 5)	/* Sticky Parity */
#define LCR_EPS		(1 << 4)	/* Even Parity Select */
#define LCR_PEN		(1 << 3)	/* Parity Enable */
#define LCR_STB		(1 << 2)	/* Stop Bit */
#define LCR_WLS1	(1 << 1)	/* Word Length Select */
#define LCR_WLS0	(1 << 0)	/* Word Length Select */

#define LSR_FIFOE	(1 << 7)	/* FIFO Error Status */
#define LSR_TEMT	(1 << 6)	/* Transmitter Empty */
#define LSR_TDRQ	(1 << 5)	/* Transmit Data Request */
#define LSR_BI		(1 << 4)	/* Break Interrupt */
#define LSR_FE		(1 << 3)	/* Framing Error */
#define LSR_PE		(1 << 2)	/* Parity Error */
#define LSR_OE		(1 << 1)	/* Overrun Error */
#define LSR_DR		(1 << 0)	/* Data Ready */

#define MCR_LOOP	(1 << 4)
#define MCR_OUT2	(1 << 3)	/* force MSR_DCD in loopback mode */
#define MCR_OUT1	(1 << 2)	/* force MSR_RI in loopback mode */
#define MCR_RTS		(1 << 1)	/* Request to Send */
#define MCR_DTR		(1 << 0)	/* Data Terminal Ready */

#define MSR_DCD		(1 << 7)	/* Data Carrier Detect */
#define MSR_RI		(1 << 6)	/* Ring Indicator */
#define MSR_DSR		(1 << 5)	/* Data Set Ready */
#define MSR_CTS		(1 << 4)	/* Clear To Send */
#define MSR_DDCD	(1 << 3)	/* Delta Data Carrier Detect */
#define MSR_TERI	(1 << 2)	/* Trailing Edge Ring Indicator */
#define MSR_DDSR	(1 << 1)	/* Delta Data Set Ready */
#define MSR_DCTS	(1 << 0)	/* Delta Clear To Send */

/*
 * IrSR (Infrared Selection Register)
 */
#define STISR_RXPL      (1 << 4)        /* Receive Data Polarity */
#define STISR_TXPL      (1 << 3)        /* Transmit Data Polarity */
#define STISR_XMODE     (1 << 2)        /* Transmit Pulse Width Select */
#define STISR_RCVEIR    (1 << 1)        /* Receiver SIR Enable */
#define STISR_XMITIR    (1 << 0)        /* Transmitter SIR Enable */


/*
 * I2C registers
 */

#define IBMR		__REG(0x40301680)  /* I2C Bus Monitor Register - IBMR */
#define IDBR		__REG(0x40301688)  /* I2C Data Buffer Register - IDBR */
#define ICR		__REG(0x40301690)  /* I2C Control Register - ICR */
#define ISR		__REG(0x40301698)  /* I2C Status Register - ISR */
#define ISAR		__REG(0x403016A0)  /* I2C Slave Address Register - ISAR */

#define PWRIBMR    __REG(0x40f00180)  /* Power I2C Bus Monitor Register-IBMR */
#define PWRIDBR    __REG(0x40f00188)  /* Power I2C Data Buffer Register-IDBR */
#define PWRICR __REG(0x40f00190)  /* Power I2C Control Register - ICR */
#define PWRISR __REG(0x40f00198)  /* Power I2C Status Register - ISR */
#define PWRISAR    __REG(0x40f001A0)  /*Power I2C Slave Address Register-ISAR */

#define ICR_START	(1 << 0)	   /* start bit */
#define ICR_STOP	(1 << 1)	   /* stop bit */
#define ICR_ACKNAK	(1 << 2)	   /* send ACK(0) or NAK(1) */
#define ICR_TB		(1 << 3)	   /* transfer byte bit */
#define ICR_MA		(1 << 4)	   /* master abort */
#define ICR_SCLE	(1 << 5)	   /* master clock enable */
#define ICR_IUE		(1 << 6)	   /* unit enable */
#define ICR_GCD		(1 << 7)	   /* general call disable */
#define ICR_ITEIE	(1 << 8)	   /* enable tx interrupts */
#define ICR_IRFIE	(1 << 9)	   /* enable rx interrupts */
#define ICR_BEIE	(1 << 10)	   /* enable bus error ints */
#define ICR_SSDIE	(1 << 11)	   /* slave STOP detected int enable */
#define ICR_ALDIE	(1 << 12)	   /* enable arbitration interrupt */
#define ICR_SADIE	(1 << 13)	   /* slave address detected int enable */
#define ICR_UR		(1 << 14)	   /* unit reset */

#define ISR_RWM		(1 << 0)	   /* read/write mode */
#define ISR_ACKNAK	(1 << 1)	   /* ack/nak status */
#define ISR_UB		(1 << 2)	   /* unit busy */
#define ISR_IBB		(1 << 3)	   /* bus busy */
#define ISR_SSD		(1 << 4)	   /* slave stop detected */
#define ISR_ALD		(1 << 5)	   /* arbitration loss detected */
#define ISR_ITE		(1 << 6)	   /* tx buffer empty */
#define ISR_IRF		(1 << 7)	   /* rx buffer full */
#define ISR_GCAD	(1 << 8)	   /* general call address detected */
#define ISR_SAD		(1 << 9)	   /* slave address detected */
#define ISR_BED		(1 << 10)	   /* bus error no ACK/NAK */


/*
 * Serial Audio Controller
 */

/* FIXME: This clash with SA1111 defines */
#ifndef _ASM_ARCH_SA1111

#define SACR0		__REG(0x40400000)  /* Global Control Register */
#define SACR1		__REG(0x40400004)  /* Serial Audio I 2 S/MSB-Justified Control Register */
#define SASR0		__REG(0x4040000C)  /* Serial Audio I 2 S/MSB-Justified Interface and FIFO Status Register */
#define SAIMR		__REG(0x40400014)  /* Serial Audio Interrupt Mask Register */
#define SAICR		__REG(0x40400018)  /* Serial Audio Interrupt Clear Register */
#define SADIV		__REG(0x40400060)  /* Audio Clock Divider Register. */
#define SADR		__REG(0x40400080)  /* Serial Audio Data Register (TX and RX FIFO access Register). */

#define SACR0_RFTH(x)	(x << 12)	/* Rx FIFO Interrupt or DMA Trigger Threshold */
#define SACR0_TFTH(x)	(x << 8)	/* Tx FIFO Interrupt or DMA Trigger Threshold */
#define SACR0_STRF	(1 << 5)	/* FIFO Select for EFWR Special Function */
#define SACR0_EFWR	(1 << 4)	/* Enable EFWR Function  */
#define SACR0_RST	(1 << 3)	/* FIFO, i2s Register Reset */
#define SACR0_BCKD	(1 << 2) 	/* Bit Clock Direction */
#define SACR0_ENB	(1 << 0)	/* Enable I2S Link */
#define SACR1_ENLBF	(1 << 5)	/* Enable Loopback */
#define SACR1_DRPL	(1 << 4) 	/* Disable Replaying Function */
#define SACR1_DREC	(1 << 3)	/* Disable Recording Function */
#define SACR1_AMSL	(1 << 1)	/* Specify Alternate Mode */

#define SASR0_I2SOFF	(1 << 7)	/* Controller Status */
#define SASR0_ROR	(1 << 6)	/* Rx FIFO Overrun */
#define SASR0_TUR	(1 << 5)	/* Tx FIFO Underrun */
#define SASR0_RFS	(1 << 4)	/* Rx FIFO Service Request */
#define SASR0_TFS	(1 << 3)	/* Tx FIFO Service Request */
#define SASR0_BSY	(1 << 2)	/* I2S Busy */
#define SASR0_RNE	(1 << 1)	/* Rx FIFO Not Empty */
#define SASR0_TNF	(1 << 0) 	/* Tx FIFO Not Empty */

#define SAICR_ROR	(1 << 6)	/* Clear Rx FIFO Overrun Interrupt */
#define SAICR_TUR	(1 << 5)	/* Clear Tx FIFO Underrun Interrupt */

#define SAIMR_ROR	(1 << 6)	/* Enable Rx FIFO Overrun Condition Interrupt */
#define SAIMR_TUR	(1 << 5)	/* Enable Tx FIFO Underrun Condition Interrupt */
#define SAIMR_RFS	(1 << 4)	/* Enable Rx FIFO Service Interrupt */
#define SAIMR_TFS	(1 << 3)	/* Enable Tx FIFO Service Interrupt */

#endif

/*
 * AC97 Controller registers
 */

#define POCR		__REG(0x40500000)  /* PCM Out Control Register */
#define POCR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */
#define POCR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define PICR		__REG(0x40500004)  /* PCM In Control Register */
#define PICR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */
#define PICR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define MCCR		__REG(0x40500008)  /* Mic In Control Register */
#define MCCR_FEIE	(1 << 3)	/* FIFO Error Interrupt Enable */
#define MCCR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define GCR		__REG(0x4050000C)  /* Global Control Register */
#define GCR_nDMAEN	(1 << 24)	/* non DMA Enable */
#define GCR_CDONE_IE	(1 << 19)	/* Command Done Interrupt Enable */
#define GCR_SDONE_IE	(1 << 18)	/* Status Done Interrupt Enable */
#define GCR_SECRDY_IEN	(1 << 9)	/* Secondary Ready Interrupt Enable */
#define GCR_PRIRDY_IEN	(1 << 8)	/* Primary Ready Interrupt Enable */
#define GCR_SECRES_IEN	(1 << 5)	/* Secondary Resume Interrupt Enable */
#define GCR_PRIRES_IEN	(1 << 4)	/* Primary Resume Interrupt Enable */
#define GCR_ACLINK_OFF	(1 << 3)	/* AC-link Shut Off */
#define GCR_WARM_RST	(1 << 2)	/* AC97 Warm Reset */
#define GCR_COLD_RST	(1 << 1)	/* AC'97 Cold Reset (0 = active) */
#define GCR_GIE		(1 << 0)	/* Codec GPI Interrupt Enable */

#define POSR		__REG(0x40500010)  /* PCM Out Status Register */
#define POSR_FIFOE	(1 << 4)	/* FIFO error */
#define POSR_FSR	(1 << 2)	/* FIFO Service Request */

#define PISR		__REG(0x40500014)  /* PCM In Status Register */
#define PISR_FIFOE	(1 << 4)	/* FIFO error */
#define PISR_EOC	(1 << 3)	/* DMA End-of-Chain (exclusive clear) */
#define PISR_FSR	(1 << 2)	/* FIFO Service Request */

#define MCSR		__REG(0x40500018)  /* Mic In Status Register */
#define MCSR_FIFOE	(1 << 4)	/* FIFO error */
#define MCSR_EOC	(1 << 3)	/* DMA End-of-Chain (exclusive clear) */
#define MCSR_FSR	(1 << 2)	/* FIFO Service Request */

#define GSR		__REG(0x4050001C)  /* Global Status Register */
#define GSR_CDONE	(1 << 19)	/* Command Done */
#define GSR_SDONE	(1 << 18)	/* Status Done */
#define GSR_RDCS	(1 << 15)	/* Read Completion Status */
#define GSR_BIT3SLT12	(1 << 14)	/* Bit 3 of slot 12 */
#define GSR_BIT2SLT12	(1 << 13)	/* Bit 2 of slot 12 */
#define GSR_BIT1SLT12	(1 << 12)	/* Bit 1 of slot 12 */
#define GSR_SECRES	(1 << 11)	/* Secondary Resume Interrupt */
#define GSR_PRIRES	(1 << 10)	/* Primary Resume Interrupt */
#define GSR_SCR		(1 << 9)	/* Secondary Codec Ready */
#define GSR_PCR		(1 << 8)	/*  Primary Codec Ready */
#define GSR_MCINT	(1 << 7)	/* Mic In Interrupt */
#define GSR_POINT	(1 << 6)	/* PCM Out Interrupt */
#define GSR_PIINT	(1 << 5)	/* PCM In Interrupt */
#define GSR_ACOFFD	(1 << 3)	/* AC-link Shut Off Done */
#define GSR_MOINT	(1 << 2)	/* Modem Out Interrupt */
#define GSR_MIINT	(1 << 1)	/* Modem In Interrupt */
#define GSR_GSCI	(1 << 0)	/* Codec GPI Status Change Interrupt */

#define CAR		__REG(0x40500020)  /* CODEC Access Register */
#define CAR_CAIP	(1 << 0)	/* Codec Access In Progress */

#define PCDR		__REG(0x40500040)  /* PCM FIFO Data Register */
#define MCDR		__REG(0x40500060)  /* Mic-in FIFO Data Register */

#define MOCR		__REG(0x40500100)  /* Modem Out Control Register */
#define MOCR_FEIE	(1 << 3)	/* FIFO Error */
#define MOCR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define MICR		__REG(0x40500108)  /* Modem In Control Register */
#define MICR_FEIE	(1 << 3)	/* FIFO Error */
#define MICR_FSRIE	(1 << 1)	/* FIFO Service Request Interrupt Enable */

#define MOSR		__REG(0x40500110)  /* Modem Out Status Register */
#define MOSR_FIFOE	(1 << 4)	/* FIFO error */
#define MOSR_FSR	(1 << 2)	/* FIFO Service Request */

#define MISR		__REG(0x40500118)  /* Modem In Status Register */
#define MISR_FIFOE	(1 << 4)	/* FIFO error */
#define MISR_EOC	(1 << 3)	/* DMA End-of-Chain (exclusive clear) */
#define MISR_FSR	(1 << 2)	/* FIFO Service Request */

#define MODR		__REG(0x40500140)  /* Modem FIFO Data Register */

#define PAC_REG_BASE	__REG(0x40500200)  /* Primary Audio Codec */
#define SAC_REG_BASE	__REG(0x40500300)  /* Secondary Audio Codec */
#define PMC_REG_BASE	__REG(0x40500400)  /* Primary Modem Codec */
#define SMC_REG_BASE	__REG(0x40500500)  /* Secondary Modem Codec */


/*
 * USB Device Controller
 * PXA25x and PXA27x USB device controller registers are different.
 */
#if defined(CONFIG_PXA25x)

#define UDC_RES1	__REG(0x40600004)  /* UDC Undocumented - Reserved1 */
#define UDC_RES2	__REG(0x40600008)  /* UDC Undocumented - Reserved2 */
#define UDC_RES3	__REG(0x4060000C)  /* UDC Undocumented - Reserved3 */

#define UDCCR		__REG(0x40600000)  /* UDC Control Register */
#define UDCCR_UDE	(1 << 0)	/* UDC enable */
#define UDCCR_UDA	(1 << 1)	/* UDC active */
#define UDCCR_RSM	(1 << 2)	/* Device resume */
#define UDCCR_RESIR	(1 << 3)	/* Resume interrupt request */
#define UDCCR_SUSIR	(1 << 4)	/* Suspend interrupt request */
#define UDCCR_SRM	(1 << 5)	/* Suspend/resume interrupt mask */
#define UDCCR_RSTIR	(1 << 6)	/* Reset interrupt request */
#define UDCCR_REM	(1 << 7)	/* Reset interrupt mask */

#define UDCCS0		__REG(0x40600010)  /* UDC Endpoint 0 Control/Status Register */
#define UDCCS0_OPR	(1 << 0)	/* OUT packet ready */
#define UDCCS0_IPR	(1 << 1)	/* IN packet ready */
#define UDCCS0_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS0_DRWF	(1 << 3)	/* Device remote wakeup feature */
#define UDCCS0_SST	(1 << 4)	/* Sent stall */
#define UDCCS0_FST	(1 << 5)	/* Force stall */
#define UDCCS0_RNE	(1 << 6)	/* Receive FIFO no empty */
#define UDCCS0_SA	(1 << 7)	/* Setup active */

/* Bulk IN - Endpoint 1,6,11 */
#define UDCCS1		__REG(0x40600014)  /* UDC Endpoint 1 (IN) Control/Status Register */
#define UDCCS6		__REG(0x40600028)  /* UDC Endpoint 6 (IN) Control/Status Register */
#define UDCCS11		__REG(0x4060003C)  /* UDC Endpoint 11 (IN) Control/Status Register */

#define UDCCS_BI_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_BI_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_BI_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_BI_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_BI_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BI_FST	(1 << 5)	/* Force stall */
#define UDCCS_BI_TSP	(1 << 7)	/* Transmit short packet */

/* Bulk OUT - Endpoint 2,7,12 */
#define UDCCS2		__REG(0x40600018)  /* UDC Endpoint 2 (OUT) Control/Status Register */
#define UDCCS7		__REG(0x4060002C)  /* UDC Endpoint 7 (OUT) Control/Status Register */
#define UDCCS12		__REG(0x40600040)  /* UDC Endpoint 12 (OUT) Control/Status Register */

#define UDCCS_BO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_BO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_BO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_BO_SST	(1 << 4)	/* Sent stall */
#define UDCCS_BO_FST	(1 << 5)	/* Force stall */
#define UDCCS_BO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_BO_RSP	(1 << 7)	/* Receive short packet */

/* Isochronous IN - Endpoint 3,8,13 */
#define UDCCS3		__REG(0x4060001C)  /* UDC Endpoint 3 (IN) Control/Status Register */
#define UDCCS8		__REG(0x40600030)  /* UDC Endpoint 8 (IN) Control/Status Register */
#define UDCCS13		__REG(0x40600044)  /* UDC Endpoint 13 (IN) Control/Status Register */

#define UDCCS_II_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_II_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_II_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_II_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_II_TSP	(1 << 7)	/* Transmit short packet */

/* Isochronous OUT - Endpoint 4,9,14 */
#define UDCCS4		__REG(0x40600020)  /* UDC Endpoint 4 (OUT) Control/Status Register */
#define UDCCS9		__REG(0x40600034)  /* UDC Endpoint 9 (OUT) Control/Status Register */
#define UDCCS14		__REG(0x40600048)  /* UDC Endpoint 14 (OUT) Control/Status Register */

#define UDCCS_IO_RFS	(1 << 0)	/* Receive FIFO service */
#define UDCCS_IO_RPC	(1 << 1)	/* Receive packet complete */
#define UDCCS_IO_ROF	(1 << 2)	/* Receive overflow */
#define UDCCS_IO_DME	(1 << 3)	/* DMA enable */
#define UDCCS_IO_RNE	(1 << 6)	/* Receive FIFO not empty */
#define UDCCS_IO_RSP	(1 << 7)	/* Receive short packet */

/* Interrupt IN - Endpoint 5,10,15 */
#define UDCCS5		__REG(0x40600024)  /* UDC Endpoint 5 (Interrupt) Control/Status Register */
#define UDCCS10		__REG(0x40600038)  /* UDC Endpoint 10 (Interrupt) Control/Status Register */
#define UDCCS15		__REG(0x4060004C)  /* UDC Endpoint 15 (Interrupt) Control/Status Register */

#define UDCCS_INT_TFS	(1 << 0)	/* Transmit FIFO service */
#define UDCCS_INT_TPC	(1 << 1)	/* Transmit packet complete */
#define UDCCS_INT_FTF	(1 << 2)	/* Flush Tx FIFO */
#define UDCCS_INT_TUR	(1 << 3)	/* Transmit FIFO underrun */
#define UDCCS_INT_SST	(1 << 4)	/* Sent stall */
#define UDCCS_INT_FST	(1 << 5)	/* Force stall */
#define UDCCS_INT_TSP	(1 << 7)	/* Transmit short packet */

#define UFNRH		__REG(0x40600060)  /* UDC Frame Number Register High */
#define UFNRL		__REG(0x40600064)  /* UDC Frame Number Register Low */
#define UBCR2		__REG(0x40600068)  /* UDC Byte Count Reg 2 */
#define UBCR4		__REG(0x4060006c)  /* UDC Byte Count Reg 4 */
#define UBCR7		__REG(0x40600070)  /* UDC Byte Count Reg 7 */
#define UBCR9		__REG(0x40600074)  /* UDC Byte Count Reg 9 */
#define UBCR12		__REG(0x40600078)  /* UDC Byte Count Reg 12 */
#define UBCR14		__REG(0x4060007c)  /* UDC Byte Count Reg 14 */
#define UDDR0		__REG(0x40600080)  /* UDC Endpoint 0 Data Register */
#define UDDR1		__REG(0x40600100)  /* UDC Endpoint 1 Data Register */
#define UDDR2		__REG(0x40600180)  /* UDC Endpoint 2 Data Register */
#define UDDR3		__REG(0x40600200)  /* UDC Endpoint 3 Data Register */
#define UDDR4		__REG(0x40600400)  /* UDC Endpoint 4 Data Register */
#define UDDR5		__REG(0x406000A0)  /* UDC Endpoint 5 Data Register */
#define UDDR6		__REG(0x40600600)  /* UDC Endpoint 6 Data Register */
#define UDDR7		__REG(0x40600680)  /* UDC Endpoint 7 Data Register */
#define UDDR8		__REG(0x40600700)  /* UDC Endpoint 8 Data Register */
#define UDDR9		__REG(0x40600900)  /* UDC Endpoint 9 Data Register */
#define UDDR10		__REG(0x406000C0)  /* UDC Endpoint 10 Data Register */
#define UDDR11		__REG(0x40600B00)  /* UDC Endpoint 11 Data Register */
#define UDDR12		__REG(0x40600B80)  /* UDC Endpoint 12 Data Register */
#define UDDR13		__REG(0x40600C00)  /* UDC Endpoint 13 Data Register */
#define UDDR14		__REG(0x40600E00)  /* UDC Endpoint 14 Data Register */
#define UDDR15		__REG(0x406000E0)  /* UDC Endpoint 15 Data Register */

#define UICR0		__REG(0x40600050)  /* UDC Interrupt Control Register 0 */

#define UICR0_IM0	(1 << 0)	/* Interrupt mask ep 0 */
#define UICR0_IM1	(1 << 1)	/* Interrupt mask ep 1 */
#define UICR0_IM2	(1 << 2)	/* Interrupt mask ep 2 */
#define UICR0_IM3	(1 << 3)	/* Interrupt mask ep 3 */
#define UICR0_IM4	(1 << 4)	/* Interrupt mask ep 4 */
#define UICR0_IM5	(1 << 5)	/* Interrupt mask ep 5 */
#define UICR0_IM6	(1 << 6)	/* Interrupt mask ep 6 */
#define UICR0_IM7	(1 << 7)	/* Interrupt mask ep 7 */

#define UICR1		__REG(0x40600054)  /* UDC Interrupt Control Register 1 */

#define UICR1_IM8	(1 << 0)	/* Interrupt mask ep 8 */
#define UICR1_IM9	(1 << 1)	/* Interrupt mask ep 9 */
#define UICR1_IM10	(1 << 2)	/* Interrupt mask ep 10 */
#define UICR1_IM11	(1 << 3)	/* Interrupt mask ep 11 */
#define UICR1_IM12	(1 << 4)	/* Interrupt mask ep 12 */
#define UICR1_IM13	(1 << 5)	/* Interrupt mask ep 13 */
#define UICR1_IM14	(1 << 6)	/* Interrupt mask ep 14 */
#define UICR1_IM15	(1 << 7)	/* Interrupt mask ep 15 */

#define USIR0		__REG(0x40600058)  /* UDC Status Interrupt Register 0 */

#define USIR0_IR0	(1 << 0)	/* Interrup request ep 0 */
#define USIR0_IR1	(1 << 1)	/* Interrup request ep 1 */
#define USIR0_IR2	(1 << 2)	/* Interrup request ep 2 */
#define USIR0_IR3	(1 << 3)	/* Interrup request ep 3 */
#define USIR0_IR4	(1 << 4)	/* Interrup request ep 4 */
#define USIR0_IR5	(1 << 5)	/* Interrup request ep 5 */
#define USIR0_IR6	(1 << 6)	/* Interrup request ep 6 */
#define USIR0_IR7	(1 << 7)	/* Interrup request ep 7 */

#define USIR1		__REG(0x4060005C)  /* UDC Status Interrupt Register 1 */

#define USIR1_IR8	(1 << 0)	/* Interrup request ep 8 */
#define USIR1_IR9	(1 << 1)	/* Interrup request ep 9 */
#define USIR1_IR10	(1 << 2)	/* Interrup request ep 10 */
#define USIR1_IR11	(1 << 3)	/* Interrup request ep 11 */
#define USIR1_IR12	(1 << 4)	/* Interrup request ep 12 */
#define USIR1_IR13	(1 << 5)	/* Interrup request ep 13 */
#define USIR1_IR14	(1 << 6)	/* Interrup request ep 14 */
#define USIR1_IR15	(1 << 7)	/* Interrup request ep 15 */

#elif defined(CONFIG_PXA27x)

#define UDCCR           __REG(0x40600000) /* UDC Control Register */
#define UDCCR_OEN	(1 << 31)	/* On-the-Go Enable */
#define UDCCR_AALTHNP	(1 << 30)	/* A-device Alternate Host Negotiation
					   Protocol Port Support */
#define UDCCR_AHNP	(1 << 29)	/* A-device Host Negotiation Protocol
					   Support */
#define UDCCR_BHNP	(1 << 28)	/* B-device Host Negotiation Protocol
					   Enable */
#define UDCCR_DWRE	(1 << 16)	/* Device Remote Wake-up Enable */
#define UDCCR_ACN	(0x03 << 11)	/* Active UDC configuration Number */
#define UDCCR_ACN_S	11
#define UDCCR_AIN	(0x07 << 8)	/* Active UDC interface Number */
#define UDCCR_AIN_S	8
#define UDCCR_AAISN	(0x07 << 5)	/* Active UDC Alternate Interface
					   Setting Number */
#define UDCCR_AAISN_S	5
#define UDCCR_SMAC	(1 << 4)	/* Switch Endpoint Memory to Active
					   Configuration */
#define UDCCR_EMCE	(1 << 3)	/* Endpoint Memory Configuration
					   Error */
#define UDCCR_UDR	(1 << 2)	/* UDC Resume */
#define UDCCR_UDA	(1 << 1)	/* UDC Active */
#define UDCCR_UDE	(1 << 0)	/* UDC Enable */

#define UDCICR0         __REG(0x40600004) /* UDC Interrupt Control Register0 */
#define UDCICR1         __REG(0x40600008) /* UDC Interrupt Control Register1 */
#define UDCICR_FIFOERR	(1 << 1)	/* FIFO Error interrupt for EP */
#define UDCICR_PKTCOMPL (1 << 0)	/* Packet Complete interrupt for EP */

#define UDC_INT_FIFOERROR  (0x2)
#define UDC_INT_PACKETCMP  (0x1)

#define UDCICR_INT(n,intr) (((intr) & 0x03) << (((n) & 0x0F) * 2))
#define UDCICR1_IECC	(1 << 31)	/* IntEn - Configuration Change */
#define UDCICR1_IESOF	(1 << 30)	/* IntEn - Start of Frame */
#define UDCICR1_IERU	(1 << 29)	/* IntEn - Resume */
#define UDCICR1_IESU	(1 << 28)	/* IntEn - Suspend */
#define UDCICR1_IERS	(1 << 27)	/* IntEn - Reset */

#define UDCISR0         __REG(0x4060000C) /* UDC Interrupt Status Register 0 */
#define UDCISR1         __REG(0x40600010) /* UDC Interrupt Status Register 1 */
#define UDCISR_INT(n,intr) (((intr) & 0x03) << (((n) & 0x0F) * 2))
#define UDCISR1_IECC	(1 << 31)	/* IntEn - Configuration Change */
#define UDCISR1_IESOF	(1 << 30)	/* IntEn - Start of Frame */
#define UDCISR1_IERU	(1 << 29)	/* IntEn - Resume */
#define UDCISR1_IESU	(1 << 28)	/* IntEn - Suspend */
#define UDCISR1_IERS	(1 << 27)	/* IntEn - Reset */


#define UDCFNR          __REG(0x40600014) /* UDC Frame Number Register */
#define UDCOTGICR	__REG(0x40600018) /* UDC On-The-Go interrupt control */
#define UDCOTGICR_IESF	(1 << 24)	/* OTG SET_FEATURE command recvd */
#define UDCOTGICR_IEXR	(1 << 17)	/* Extra Transciever Interrupt
					   Rising Edge Interrupt Enable */
#define UDCOTGICR_IEXF	(1 << 16)	/* Extra Transciever Interrupt
					   Falling Edge Interrupt Enable */
#define UDCOTGICR_IEVV40R (1 << 9)	/* OTG Vbus Valid 4.0V Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV40F (1 << 8)	/* OTG Vbus Valid 4.0V Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV44R (1 << 7)	/* OTG Vbus Valid 4.4V Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV44F (1 << 6)	/* OTG Vbus Valid 4.4V Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IESVR	(1 << 5)	/* OTG Session Valid Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IESVF	(1 << 4)	/* OTG Session Valid Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IESDR	(1 << 3)	/* OTG A-Device SRP Detect Rising
					   Edge Interrupt Enable */
#define UDCOTGICR_IESDF	(1 << 2)	/* OTG A-Device SRP Detect Falling
					   Edge Interrupt Enable */
#define UDCOTGICR_IEIDR	(1 << 1)	/* OTG ID Change Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEIDF	(1 << 0)	/* OTG ID Change Falling Edge
					   Interrupt Enable */

#define UP2OCR		  __REG(0x40600020)  /* USB Port 2 Output Control register */

#define UP2OCR_CPVEN	(1 << 0)	/* Charge Pump Vbus Enable */
#define UP2OCR_CPVPE	(1 << 1)	/* Charge Pump Vbus Pulse Enable */
#define UP2OCR_DPPDE	(1 << 2)	/* Host Port 2 Transceiver D+ Pull Down Enable */
#define UP2OCR_DMPDE	(1 << 3)	/* Host Port 2 Transceiver D- Pull Down Enable */
#define UP2OCR_DPPUE	(1 << 4)	/* Host Port 2 Transceiver D+ Pull Up Enable */
#define UP2OCR_DMPUE	(1 << 5)	/* Host Port 2 Transceiver D- Pull Up Enable */
#define UP2OCR_DPPUBE	(1 << 6)	/* Host Port 2 Transceiver D+ Pull Up Bypass Enable */
#define UP2OCR_DMPUBE	(1 << 7)	/* Host Port 2 Transceiver D- Pull Up Bypass Enable */
#define UP2OCR_EXSP		(1 << 8)	/* External Transceiver Speed Control */
#define UP2OCR_EXSUS	(1 << 9)	/* External Transceiver Speed Enable */
#define UP2OCR_IDON		(1 << 10)	/* OTG ID Read Enable */
#define UP2OCR_HXS		(1 << 16)	/* Host Port 2 Transceiver Output Select */
#define UP2OCR_HXOE		(1 << 17)	/* Host Port 2 Transceiver Output Enable */
#define UP2OCR_SEOS		(1 << 24)	/* Single-Ended Output Select */

#define UDCCSN(x)	__REG2(0x40600100, (x) << 2)
#define UDCCSR0         __REG(0x40600100) /* UDC Control/Status register - Endpoint 0 */
#define UDCCSR0_SA	(1 << 7)	/* Setup Active */
#define UDCCSR0_RNE	(1 << 6)	/* Receive FIFO Not Empty */
#define UDCCSR0_FST	(1 << 5)	/* Force Stall */
#define UDCCSR0_SST	(1 << 4)	/* Sent Stall */
#define UDCCSR0_DME	(1 << 3)	/* DMA Enable */
#define UDCCSR0_FTF	(1 << 2)	/* Flush Transmit FIFO */
#define UDCCSR0_IPR	(1 << 1)	/* IN Packet Ready */
#define UDCCSR0_OPC	(1 << 0)	/* OUT Packet Complete */

#define UDCCSRA         __REG(0x40600104) /* UDC Control/Status register - Endpoint A */
#define UDCCSRB         __REG(0x40600108) /* UDC Control/Status register - Endpoint B */
#define UDCCSRC         __REG(0x4060010C) /* UDC Control/Status register - Endpoint C */
#define UDCCSRD         __REG(0x40600110) /* UDC Control/Status register - Endpoint D */
#define UDCCSRE         __REG(0x40600114) /* UDC Control/Status register - Endpoint E */
#define UDCCSRF         __REG(0x40600118) /* UDC Control/Status register - Endpoint F */
#define UDCCSRG         __REG(0x4060011C) /* UDC Control/Status register - Endpoint G */
#define UDCCSRH         __REG(0x40600120) /* UDC Control/Status register - Endpoint H */
#define UDCCSRI         __REG(0x40600124) /* UDC Control/Status register - Endpoint I */
#define UDCCSRJ         __REG(0x40600128) /* UDC Control/Status register - Endpoint J */
#define UDCCSRK         __REG(0x4060012C) /* UDC Control/Status register - Endpoint K */
#define UDCCSRL         __REG(0x40600130) /* UDC Control/Status register - Endpoint L */
#define UDCCSRM         __REG(0x40600134) /* UDC Control/Status register - Endpoint M */
#define UDCCSRN         __REG(0x40600138) /* UDC Control/Status register - Endpoint N */
#define UDCCSRP         __REG(0x4060013C) /* UDC Control/Status register - Endpoint P */
#define UDCCSRQ         __REG(0x40600140) /* UDC Control/Status register - Endpoint Q */
#define UDCCSRR         __REG(0x40600144) /* UDC Control/Status register - Endpoint R */
#define UDCCSRS         __REG(0x40600148) /* UDC Control/Status register - Endpoint S */
#define UDCCSRT         __REG(0x4060014C) /* UDC Control/Status register - Endpoint T */
#define UDCCSRU         __REG(0x40600150) /* UDC Control/Status register - Endpoint U */
#define UDCCSRV         __REG(0x40600154) /* UDC Control/Status register - Endpoint V */
#define UDCCSRW         __REG(0x40600158) /* UDC Control/Status register - Endpoint W */
#define UDCCSRX         __REG(0x4060015C) /* UDC Control/Status register - Endpoint X */

#define UDCCSR_DPE	(1 << 9)	/* Data Packet Error */
#define UDCCSR_FEF	(1 << 8)	/* Flush Endpoint FIFO */
#define UDCCSR_SP	(1 << 7)	/* Short Packet Control/Status */
#define UDCCSR_BNE	(1 << 6)	/* Buffer Not Empty (IN endpoints) */
#define UDCCSR_BNF	(1 << 6)	/* Buffer Not Full (OUT endpoints) */
#define UDCCSR_FST	(1 << 5)	/* Force STALL */
#define UDCCSR_SST	(1 << 4)	/* Sent STALL */
#define UDCCSR_DME	(1 << 3)	/* DMA Enable */
#define UDCCSR_TRN	(1 << 2)	/* Tx/Rx NAK */
#define UDCCSR_PC	(1 << 1)	/* Packet Complete */
#define UDCCSR_FS	(1 << 0)	/* FIFO needs service */

#define UDCBCN(x)	__REG2(0x40600200, (x)<<2)
#define UDCBCR0         __REG(0x40600200) /* Byte Count Register - EP0 */
#define UDCBCRA         __REG(0x40600204) /* Byte Count Register - EPA */
#define UDCBCRB         __REG(0x40600208) /* Byte Count Register - EPB */
#define UDCBCRC         __REG(0x4060020C) /* Byte Count Register - EPC */
#define UDCBCRD         __REG(0x40600210) /* Byte Count Register - EPD */
#define UDCBCRE         __REG(0x40600214) /* Byte Count Register - EPE */
#define UDCBCRF         __REG(0x40600218) /* Byte Count Register - EPF */
#define UDCBCRG         __REG(0x4060021C) /* Byte Count Register - EPG */
#define UDCBCRH         __REG(0x40600220) /* Byte Count Register - EPH */
#define UDCBCRI         __REG(0x40600224) /* Byte Count Register - EPI */
#define UDCBCRJ         __REG(0x40600228) /* Byte Count Register - EPJ */
#define UDCBCRK         __REG(0x4060022C) /* Byte Count Register - EPK */
#define UDCBCRL         __REG(0x40600230) /* Byte Count Register - EPL */
#define UDCBCRM         __REG(0x40600234) /* Byte Count Register - EPM */
#define UDCBCRN         __REG(0x40600238) /* Byte Count Register - EPN */
#define UDCBCRP         __REG(0x4060023C) /* Byte Count Register - EPP */
#define UDCBCRQ         __REG(0x40600240) /* Byte Count Register - EPQ */
#define UDCBCRR         __REG(0x40600244) /* Byte Count Register - EPR */
#define UDCBCRS         __REG(0x40600248) /* Byte Count Register - EPS */
#define UDCBCRT         __REG(0x4060024C) /* Byte Count Register - EPT */
#define UDCBCRU         __REG(0x40600250) /* Byte Count Register - EPU */
#define UDCBCRV         __REG(0x40600254) /* Byte Count Register - EPV */
#define UDCBCRW         __REG(0x40600258) /* Byte Count Register - EPW */
#define UDCBCRX         __REG(0x4060025C) /* Byte Count Register - EPX */

#define UDCDN(x)	__REG2(0x40600300, (x)<<2)
#define PHYS_UDCDN(x)	(0x40600300 + ((x)<<2))
#define PUDCDN(x)	(volatile u32 *)(io_p2v(PHYS_UDCDN((x))))
#define UDCDR0          __REG(0x40600300) /* Data Register - EP0 */
#define UDCDRA          __REG(0x40600304) /* Data Register - EPA */
#define UDCDRB          __REG(0x40600308) /* Data Register - EPB */
#define UDCDRC          __REG(0x4060030C) /* Data Register - EPC */
#define UDCDRD          __REG(0x40600310) /* Data Register - EPD */
#define UDCDRE          __REG(0x40600314) /* Data Register - EPE */
#define UDCDRF          __REG(0x40600318) /* Data Register - EPF */
#define UDCDRG          __REG(0x4060031C) /* Data Register - EPG */
#define UDCDRH          __REG(0x40600320) /* Data Register - EPH */
#define UDCDRI          __REG(0x40600324) /* Data Register - EPI */
#define UDCDRJ          __REG(0x40600328) /* Data Register - EPJ */
#define UDCDRK          __REG(0x4060032C) /* Data Register - EPK */
#define UDCDRL          __REG(0x40600330) /* Data Register - EPL */
#define UDCDRM          __REG(0x40600334) /* Data Register - EPM */
#define UDCDRN          __REG(0x40600338) /* Data Register - EPN */
#define UDCDRP          __REG(0x4060033C) /* Data Register - EPP */
#define UDCDRQ          __REG(0x40600340) /* Data Register - EPQ */
#define UDCDRR          __REG(0x40600344) /* Data Register - EPR */
#define UDCDRS          __REG(0x40600348) /* Data Register - EPS */
#define UDCDRT          __REG(0x4060034C) /* Data Register - EPT */
#define UDCDRU          __REG(0x40600350) /* Data Register - EPU */
#define UDCDRV          __REG(0x40600354) /* Data Register - EPV */
#define UDCDRW          __REG(0x40600358) /* Data Register - EPW */
#define UDCDRX          __REG(0x4060035C) /* Data Register - EPX */

#define UDCCN(x)       __REG2(0x40600400, (x)<<2)
#define UDCCRA          __REG(0x40600404) /* Configuration register EPA */
#define UDCCRB          __REG(0x40600408) /* Configuration register EPB */
#define UDCCRC          __REG(0x4060040C) /* Configuration register EPC */
#define UDCCRD          __REG(0x40600410) /* Configuration register EPD */
#define UDCCRE          __REG(0x40600414) /* Configuration register EPE */
#define UDCCRF          __REG(0x40600418) /* Configuration register EPF */
#define UDCCRG          __REG(0x4060041C) /* Configuration register EPG */
#define UDCCRH          __REG(0x40600420) /* Configuration register EPH */
#define UDCCRI          __REG(0x40600424) /* Configuration register EPI */
#define UDCCRJ          __REG(0x40600428) /* Configuration register EPJ */
#define UDCCRK          __REG(0x4060042C) /* Configuration register EPK */
#define UDCCRL          __REG(0x40600430) /* Configuration register EPL */
#define UDCCRM          __REG(0x40600434) /* Configuration register EPM */
#define UDCCRN          __REG(0x40600438) /* Configuration register EPN */
#define UDCCRP          __REG(0x4060043C) /* Configuration register EPP */
#define UDCCRQ          __REG(0x40600440) /* Configuration register EPQ */
#define UDCCRR          __REG(0x40600444) /* Configuration register EPR */
#define UDCCRS          __REG(0x40600448) /* Configuration register EPS */
#define UDCCRT          __REG(0x4060044C) /* Configuration register EPT */
#define UDCCRU          __REG(0x40600450) /* Configuration register EPU */
#define UDCCRV          __REG(0x40600454) /* Configuration register EPV */
#define UDCCRW          __REG(0x40600458) /* Configuration register EPW */
#define UDCCRX          __REG(0x4060045C) /* Configuration register EPX */

#define UDCCONR_CN	(0x03 << 25)	/* Configuration Number */
#define UDCCONR_CN_S	(25)
#define UDCCONR_IN	(0x07 << 22)	/* Interface Number */
#define UDCCONR_IN_S	(22)
#define UDCCONR_AISN	(0x07 << 19)	/* Alternate Interface Number */
#define UDCCONR_AISN_S	(19)
#define UDCCONR_EN	(0x0f << 15)	/* Endpoint Number */
#define UDCCONR_EN_S	(15)
#define UDCCONR_ET	(0x03 << 13)	/* Endpoint Type: */
#define UDCCONR_ET_S	(13)
#define UDCCONR_ET_INT	(0x03 << 13)	/*   Interrupt */
#define UDCCONR_ET_BULK	(0x02 << 13)	/*   Bulk */
#define UDCCONR_ET_ISO	(0x01 << 13)	/*   Isochronous */
#define UDCCONR_ET_NU	(0x00 << 13)	/*   Not used */
#define UDCCONR_ED	(1 << 12)	/* Endpoint Direction */
#define UDCCONR_MPS	(0x3ff << 2)	/* Maximum Packet Size */
#define UDCCONR_MPS_S	(2)
#define UDCCONR_DE	(1 << 1)	/* Double Buffering Enable */
#define UDCCONR_EE	(1 << 0)	/* Endpoint Enable */


#define UDC_INT_FIFOERROR  (0x2)
#define UDC_INT_PACKETCMP  (0x1)

#define UDC_FNR_MASK     (0x7ff)

#define UDCCSR_WR_MASK   (UDCCSR_DME|UDCCSR_FST)
#define UDC_BCR_MASK    (0x3ff)
#endif

/*
 * Fast Infrared Communication Port
 */

#define FICP		__REG(0x40800000)  /* Start of FICP area */
#define ICCR0		__REG(0x40800000)  /* ICP Control Register 0 */
#define ICCR1		__REG(0x40800004)  /* ICP Control Register 1 */
#define ICCR2		__REG(0x40800008)  /* ICP Control Register 2 */
#define ICDR		__REG(0x4080000c)  /* ICP Data Register */
#define ICSR0		__REG(0x40800014)  /* ICP Status Register 0 */
#define ICSR1		__REG(0x40800018)  /* ICP Status Register 1 */

#define ICCR0_AME	(1 << 7)	/* Adress match enable */
#define ICCR0_TIE	(1 << 6)	/* Transmit FIFO interrupt enable */
#define ICCR0_RIE	(1 << 5)	/* Recieve FIFO interrupt enable */
#define ICCR0_RXE	(1 << 4)	/* Receive enable */
#define ICCR0_TXE	(1 << 3)	/* Transmit enable */
#define ICCR0_TUS	(1 << 2)	/* Transmit FIFO underrun select */
#define ICCR0_LBM	(1 << 1)	/* Loopback mode */
#define ICCR0_ITR	(1 << 0)	/* IrDA transmission */

#define ICCR2_RXP       (1 << 3)	/* Receive Pin Polarity select */
#define ICCR2_TXP       (1 << 2)	/* Transmit Pin Polarity select */
#define ICCR2_TRIG	(3 << 0)	/* Receive FIFO Trigger threshold */
#define ICCR2_TRIG_8    (0 << 0)	/* 	>= 8 bytes */
#define ICCR2_TRIG_16   (1 << 0)	/*	>= 16 bytes */
#define ICCR2_TRIG_32   (2 << 0)	/*	>= 32 bytes */

#ifdef CONFIG_PXA27x
#define ICSR0_EOC	(1 << 6)	/* DMA End of Descriptor Chain */
#endif
#define ICSR0_FRE	(1 << 5)	/* Framing error */
#define ICSR0_RFS	(1 << 4)	/* Receive FIFO service request */
#define ICSR0_TFS	(1 << 3)	/* Transnit FIFO service request */
#define ICSR0_RAB	(1 << 2)	/* Receiver abort */
#define ICSR0_TUR	(1 << 1)	/* Trunsmit FIFO underun */
#define ICSR0_EIF	(1 << 0)	/* End/Error in FIFO */

#define ICSR1_ROR	(1 << 6)	/* Receiver FIFO underrun  */
#define ICSR1_CRE	(1 << 5)	/* CRC error */
#define ICSR1_EOF	(1 << 4)	/* End of frame */
#define ICSR1_TNF	(1 << 3)	/* Transmit FIFO not full */
#define ICSR1_RNE	(1 << 2)	/* Receive FIFO not empty */
#define ICSR1_TBY	(1 << 1)	/* Tramsmiter busy flag */
#define ICSR1_RSY	(1 << 0)	/* Recevier synchronized flag */


/*
 * Real Time Clock
 */

#define RCNR		__REG(0x40900000)  /* RTC Count Register */
#define RTAR		__REG(0x40900004)  /* RTC Alarm Register */
#define RTSR		__REG(0x40900008)  /* RTC Status Register */
#define RTTR		__REG(0x4090000C)  /* RTC Timer Trim Register */
#define PIAR		__REG(0x40900038)  /* Periodic Interrupt Alarm Register */

#define RTSR_PICE	(1 << 15)	/* Periodic interrupt count enable */
#define RTSR_PIALE	(1 << 14)	/* Periodic interrupt Alarm enable */
#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */


/*
 * OS Timer & Match Registers
 */

#define OSMR0		__REG(0x40A00000)  /* */
#define OSMR1		__REG(0x40A00004)  /* */
#define OSMR2		__REG(0x40A00008)  /* */
#define OSMR3		__REG(0x40A0000C)  /* */
#define OSMR4		__REG(0x40A00080)  /* */
#define OSCR		__REG(0x40A00010)  /* OS Timer Counter Register */
#define OSCR4		__REG(0x40A00040)  /* OS Timer Counter Register */
#define OMCR4		__REG(0x40A000C0)  /* */
#define OSSR		__REG(0x40A00014)  /* OS Timer Status Register */
#define OWER		__REG(0x40A00018)  /* OS Timer Watchdog Enable Register */
#define OIER		__REG(0x40A0001C)  /* OS Timer Interrupt Enable Register */

#define OSSR_M3		(1 << 3)	/* Match status channel 3 */
#define OSSR_M2		(1 << 2)	/* Match status channel 2 */
#define OSSR_M1		(1 << 1)	/* Match status channel 1 */
#define OSSR_M0		(1 << 0)	/* Match status channel 0 */

#define OWER_WME	(1 << 0)	/* Watchdog Match Enable */

#define OIER_E3		(1 << 3)	/* Interrupt enable channel 3 */
#define OIER_E2		(1 << 2)	/* Interrupt enable channel 2 */
#define OIER_E1		(1 << 1)	/* Interrupt enable channel 1 */
#define OIER_E0		(1 << 0)	/* Interrupt enable channel 0 */


/*
 * Pulse Width Modulator
 */

#define PWM_CTRL0	__REG(0x40B00000)  /* PWM 0 Control Register */
#define PWM_PWDUTY0	__REG(0x40B00004)  /* PWM 0 Duty Cycle Register */
#define PWM_PERVAL0	__REG(0x40B00008)  /* PWM 0 Period Control Register */

#define PWM_CTRL1	__REG(0x40C00000)  /* PWM 1Control Register */
#define PWM_PWDUTY1	__REG(0x40C00004)  /* PWM 1 Duty Cycle Register */
#define PWM_PERVAL1	__REG(0x40C00008)  /* PWM 1 Period Control Register */


/*
 * Interrupt Controller
 */

#define ICIP		__REG(0x40D00000)  /* Interrupt Controller IRQ Pending Register */
#define ICMR		__REG(0x40D00004)  /* Interrupt Controller Mask Register */
#define ICLR		__REG(0x40D00008)  /* Interrupt Controller Level Register */
#define ICFP		__REG(0x40D0000C)  /* Interrupt Controller FIQ Pending Register */
#define ICPR		__REG(0x40D00010)  /* Interrupt Controller Pending Register */
#define ICCR		__REG(0x40D00014)  /* Interrupt Controller Control Register */


/*
 * General Purpose I/O
 */

#define GPLR0		__REG(0x40E00000)  /* GPIO Pin-Level Register GPIO<31:0> */
#define GPLR1		__REG(0x40E00004)  /* GPIO Pin-Level Register GPIO<63:32> */
#define GPLR2		__REG(0x40E00008)  /* GPIO Pin-Level Register GPIO<80:64> */

#define GPDR0		__REG(0x40E0000C)  /* GPIO Pin Direction Register GPIO<31:0> */
#define GPDR1		__REG(0x40E00010)  /* GPIO Pin Direction Register GPIO<63:32> */
#define GPDR2		__REG(0x40E00014)  /* GPIO Pin Direction Register GPIO<80:64> */

#define GPSR0		__REG(0x40E00018)  /* GPIO Pin Output Set Register GPIO<31:0> */
#define GPSR1		__REG(0x40E0001C)  /* GPIO Pin Output Set Register GPIO<63:32> */
#define GPSR2		__REG(0x40E00020)  /* GPIO Pin Output Set Register GPIO<80:64> */

#define GPCR0		__REG(0x40E00024)  /* GPIO Pin Output Clear Register GPIO<31:0> */
#define GPCR1		__REG(0x40E00028)  /* GPIO Pin Output Clear Register GPIO <63:32> */
#define GPCR2		__REG(0x40E0002C)  /* GPIO Pin Output Clear Register GPIO <80:64> */

#define GRER0		__REG(0x40E00030)  /* GPIO Rising-Edge Detect Register GPIO<31:0> */
#define GRER1		__REG(0x40E00034)  /* GPIO Rising-Edge Detect Register GPIO<63:32> */
#define GRER2		__REG(0x40E00038)  /* GPIO Rising-Edge Detect Register GPIO<80:64> */

#define GFER0		__REG(0x40E0003C)  /* GPIO Falling-Edge Detect Register GPIO<31:0> */
#define GFER1		__REG(0x40E00040)  /* GPIO Falling-Edge Detect Register GPIO<63:32> */
#define GFER2		__REG(0x40E00044)  /* GPIO Falling-Edge Detect Register GPIO<80:64> */

#define GEDR0		__REG(0x40E00048)  /* GPIO Edge Detect Status Register GPIO<31:0> */
#define GEDR1		__REG(0x40E0004C)  /* GPIO Edge Detect Status Register GPIO<63:32> */
#define GEDR2		__REG(0x40E00050)  /* GPIO Edge Detect Status Register GPIO<80:64> */

#define GAFR0_L		__REG(0x40E00054)  /* GPIO Alternate Function Select Register GPIO<15:0> */
#define GAFR0_U		__REG(0x40E00058)  /* GPIO Alternate Function Select Register GPIO<31:16> */
#define GAFR1_L		__REG(0x40E0005C)  /* GPIO Alternate Function Select Register GPIO<47:32> */
#define GAFR1_U		__REG(0x40E00060)  /* GPIO Alternate Function Select Register GPIO<63:48> */
#define GAFR2_L		__REG(0x40E00064)  /* GPIO Alternate Function Select Register GPIO<79:64> */
#define GAFR2_U		__REG(0x40E00068)  /* GPIO Alternate Function Select Register GPIO<95-80> */
#define GAFR3_L		__REG(0x40E0006C)  /* GPIO Alternate Function Select Register GPIO<111:96> */
#define GAFR3_U		__REG(0x40E00070)  /* GPIO Alternate Function Select Register GPIO<127:112> */

#define GPLR3		__REG(0x40E00100)  /* GPIO Pin-Level Register GPIO<127:96> */
#define GPDR3		__REG(0x40E0010C)  /* GPIO Pin Direction Register GPIO<127:96> */
#define GPSR3		__REG(0x40E00118)  /* GPIO Pin Output Set Register GPIO<127:96> */
#define GPCR3		__REG(0x40E00124)  /* GPIO Pin Output Clear Register GPIO<127:96> */
#define GRER3		__REG(0x40E00130)  /* GPIO Rising-Edge Detect Register GPIO<127:96> */
#define GFER3		__REG(0x40E0013C)  /* GPIO Falling-Edge Detect Register GPIO<127:96> */
#define GEDR3		__REG(0x40E00148)  /* GPIO Edge Detect Status Register GPIO<127:96> */

/* More handy macros.  The argument is a literal GPIO number. */

#define GPIO_bit(x)	(1 << ((x) & 0x1f))

#ifdef CONFIG_PXA27x

/* Interrupt Controller */

#define ICIP2		__REG(0x40D0009C)  /* Interrupt Controller IRQ Pending Register 2 */
#define ICMR2		__REG(0x40D000A0)  /* Interrupt Controller Mask Register 2 */
#define ICLR2		__REG(0x40D000A4)  /* Interrupt Controller Level Register 2 */
#define ICFP2		__REG(0x40D000A8)  /* Interrupt Controller FIQ Pending Register 2 */
#define ICPR2		__REG(0x40D000AC)  /* Interrupt Controller Pending Register 2 */

#define _GPLR(x)	__REG2(0x40E00000, ((x) & 0x60) >> 3)
#define _GPDR(x)	__REG2(0x40E0000C, ((x) & 0x60) >> 3)
#define _GPSR(x)	__REG2(0x40E00018, ((x) & 0x60) >> 3)
#define _GPCR(x)	__REG2(0x40E00024, ((x) & 0x60) >> 3)
#define _GRER(x)	__REG2(0x40E00030, ((x) & 0x60) >> 3)
#define _GFER(x)	__REG2(0x40E0003C, ((x) & 0x60) >> 3)
#define _GEDR(x)	__REG2(0x40E00048, ((x) & 0x60) >> 3)
#define _GAFR(x)	__REG2(0x40E00054, ((x) & 0x70) >> 2)

#define GPLR(x) 	(*((((x) & 0x7f) < 96) ? &_GPLR(x) : &GPLR3))
#define GPDR(x)		(*((((x) & 0x7f) < 96) ? &_GPDR(x) : &GPDR3))
#define GPSR(x)		(*((((x) & 0x7f) < 96) ? &_GPSR(x) : &GPSR3))
#define GPCR(x)		(*((((x) & 0x7f) < 96) ? &_GPCR(x) : &GPCR3))
#define GRER(x)		(*((((x) & 0x7f) < 96) ? &_GRER(x) : &GRER3))
#define GFER(x)		(*((((x) & 0x7f) < 96) ? &_GFER(x) : &GFER3))
#define GEDR(x)		(*((((x) & 0x7f) < 96) ? &_GEDR(x) : &GEDR3))
#define GAFR(x)		(*((((x) & 0x7f) < 96) ? &_GAFR(x) : \
			 ((((x) & 0x7f) < 112) ? &GAFR3_L : &GAFR3_U)))
#else

#define GPLR(x)		__REG2(0x40E00000, ((x) & 0x60) >> 3)
#define GPDR(x)		__REG2(0x40E0000C, ((x) & 0x60) >> 3)
#define GPSR(x)		__REG2(0x40E00018, ((x) & 0x60) >> 3)
#define GPCR(x)		__REG2(0x40E00024, ((x) & 0x60) >> 3)
#define GRER(x)		__REG2(0x40E00030, ((x) & 0x60) >> 3)
#define GFER(x)		__REG2(0x40E0003C, ((x) & 0x60) >> 3)
#define GEDR(x)		__REG2(0x40E00048, ((x) & 0x60) >> 3)
#define GAFR(x)		__REG2(0x40E00054, ((x) & 0x70) >> 2)

#endif


/* GPIO alternate function assignments */

#define GPIO1_RST		1	/* reset */
#define GPIO6_MMCCLK		6	/* MMC Clock */
#define GPIO7_48MHz		7	/* 48 MHz clock output */
#define GPIO8_MMCCS0		8	/* MMC Chip Select 0 */
#define GPIO9_MMCCS1		9	/* MMC Chip Select 1 */
#define GPIO10_RTCCLK		10	/* real time clock (1 Hz) */
#define GPIO11_3_6MHz		11	/* 3.6 MHz oscillator out */
#define GPIO12_32KHz		12	/* 32 kHz out */
#define GPIO13_MBGNT		13	/* memory controller grant */
#define GPIO14_MBREQ		14	/* alternate bus master request */
#define GPIO15_nCS_1		15	/* chip select 1 */
#define GPIO16_PWM0		16	/* PWM0 output */
#define GPIO17_PWM1		17	/* PWM1 output */
#define GPIO18_RDY		18	/* Ext. Bus Ready */
#define GPIO19_DREQ1		19	/* External DMA Request */
#define GPIO20_DREQ0		20	/* External DMA Request */
#define GPIO23_SCLK		23	/* SSP clock */
#define GPIO24_SFRM		24	/* SSP Frame */
#define GPIO25_STXD		25	/* SSP transmit */
#define GPIO26_SRXD		26	/* SSP receive */
#define GPIO27_SEXTCLK		27	/* SSP ext_clk */
#define GPIO28_BITCLK		28	/* AC97/I2S bit_clk */
#define GPIO29_SDATA_IN		29	/* AC97 Sdata_in0 / I2S Sdata_in */
#define GPIO30_SDATA_OUT	30	/* AC97/I2S Sdata_out */
#define GPIO31_SYNC		31	/* AC97/I2S sync */
#define GPIO32_SDATA_IN1	32	/* AC97 Sdata_in1 */
#define GPIO32_SYSCLK		32	/* I2S System Clock */
#define GPIO32_MMCCLK		32	/* MMC Clock (PXA270) */
#define GPIO33_nCS_5		33	/* chip select 5 */
#define GPIO34_FFRXD		34	/* FFUART receive */
#define GPIO34_MMCCS0		34	/* MMC Chip Select 0 */
#define GPIO35_FFCTS		35	/* FFUART Clear to send */
#define GPIO36_FFDCD		36	/* FFUART Data carrier detect */
#define GPIO37_FFDSR		37	/* FFUART data set ready */
#define GPIO38_FFRI		38	/* FFUART Ring Indicator */
#define GPIO39_MMCCS1		39	/* MMC Chip Select 1 */
#define GPIO39_FFTXD		39	/* FFUART transmit data */
#define GPIO40_FFDTR		40	/* FFUART data terminal Ready */
#define GPIO41_FFRTS		41	/* FFUART request to send */
#define GPIO42_BTRXD		42	/* BTUART receive data */
#define GPIO42_HWRXD		42	/* HWUART receive data */
#define GPIO43_BTTXD		43	/* BTUART transmit data */
#define GPIO43_HWTXD		43	/* HWUART transmit data */
#define GPIO44_BTCTS		44	/* BTUART clear to send */
#define GPIO44_HWCTS		44	/* HWUART clear to send */
#define GPIO45_BTRTS		45	/* BTUART request to send */
#define GPIO45_HWRTS		45	/* HWUART request to send */
#define GPIO45_AC97_SYSCLK	45	/* AC97 System Clock */
#define GPIO46_ICPRXD		46	/* ICP receive data */
#define GPIO46_STRXD		46	/* STD_UART receive data */
#define GPIO47_ICPTXD		47	/* ICP transmit data */
#define GPIO47_STTXD		47	/* STD_UART transmit data */
#define GPIO48_nPOE		48	/* Output Enable for Card Space */
#define GPIO49_nPWE		49	/* Write Enable for Card Space */
#define GPIO50_nPIOR		50	/* I/O Read for Card Space */
#define GPIO51_nPIOW		51	/* I/O Write for Card Space */
#define GPIO52_nPCE_1		52	/* Card Enable for Card Space */
#define GPIO53_nPCE_2		53	/* Card Enable for Card Space */
#define GPIO53_MMCCLK		53	/* MMC Clock */
#define GPIO54_MMCCLK		54	/* MMC Clock */
#define GPIO54_pSKTSEL		54	/* Socket Select for Card Space */
#define GPIO54_nPCE_2		54	/* Card Enable for Card Space (PXA27x) */
#define GPIO55_nPREG		55	/* Card Address bit 26 */
#define GPIO56_nPWAIT		56	/* Wait signal for Card Space */
#define GPIO57_nIOIS16		57	/* Bus Width select for I/O Card Space */
#define GPIO58_LDD_0		58	/* LCD data pin 0 */
#define GPIO59_LDD_1		59	/* LCD data pin 1 */
#define GPIO60_LDD_2		60	/* LCD data pin 2 */
#define GPIO61_LDD_3		61	/* LCD data pin 3 */
#define GPIO62_LDD_4		62	/* LCD data pin 4 */
#define GPIO63_LDD_5		63	/* LCD data pin 5 */
#define GPIO64_LDD_6		64	/* LCD data pin 6 */
#define GPIO65_LDD_7		65	/* LCD data pin 7 */
#define GPIO66_LDD_8		66	/* LCD data pin 8 */
#define GPIO66_MBREQ		66	/* alternate bus master req */
#define GPIO67_LDD_9		67	/* LCD data pin 9 */
#define GPIO67_MMCCS0		67	/* MMC Chip Select 0 */
#define GPIO68_LDD_10		68	/* LCD data pin 10 */
#define GPIO68_MMCCS1		68	/* MMC Chip Select 1 */
#define GPIO69_LDD_11		69	/* LCD data pin 11 */
#define GPIO69_MMCCLK		69	/* MMC_CLK */
#define GPIO70_LDD_12		70	/* LCD data pin 12 */
#define GPIO70_RTCCLK		70	/* Real Time clock (1 Hz) */
#define GPIO71_LDD_13		71	/* LCD data pin 13 */
#define GPIO71_3_6MHz		71	/* 3.6 MHz Oscillator clock */
#define GPIO72_LDD_14		72	/* LCD data pin 14 */
#define GPIO72_32kHz		72	/* 32 kHz clock */
#define GPIO73_LDD_15		73	/* LCD data pin 15 */
#define GPIO73_MBGNT		73	/* Memory controller grant */
#define GPIO74_LCD_FCLK		74	/* LCD Frame clock */
#define GPIO75_LCD_LCLK		75	/* LCD line clock */
#define GPIO76_LCD_PCLK		76	/* LCD Pixel clock */
#define GPIO77_LCD_ACBIAS	77	/* LCD AC Bias */
#define GPIO78_nCS_2		78	/* chip select 2 */
#define GPIO79_nCS_3		79	/* chip select 3 */
#define GPIO80_nCS_4		80	/* chip select 4 */
#define GPIO81_NSCLK		81	/* NSSP clock */
#define GPIO82_NSFRM		82	/* NSSP Frame */
#define GPIO83_NSTXD		83	/* NSSP transmit */
#define GPIO84_NSRXD		84	/* NSSP receive */
#define GPIO85_nPCE_1		85	/* Card Enable for Card Space (PXA27x) */
#define GPIO92_MMCDAT0		92	/* MMC DAT0 (PXA27x) */
#define GPIO109_MMCDAT1		109	/* MMC DAT1 (PXA27x) */
#define GPIO110_MMCDAT2		110	/* MMC DAT2 (PXA27x) */
#define GPIO110_MMCCS0		110	/* MMC Chip Select 0 (PXA27x) */
#define GPIO111_MMCDAT3		111	/* MMC DAT3 (PXA27x) */
#define GPIO111_MMCCS1		111	/* MMC Chip Select 1 (PXA27x) */
#define GPIO112_MMCCMD		112	/* MMC CMD (PXA27x) */
#define GPIO113_I2S_SYSCLK	113	/* I2S System Clock (PXA27x) */
#define GPIO113_AC97_RESET_N	113	/* AC97 NRESET on (PXA27x) */

/* GPIO alternate function mode & direction */

#define GPIO_IN			0x000
#define GPIO_OUT		0x080
#define GPIO_ALT_FN_1_IN	0x100
#define GPIO_ALT_FN_1_OUT	0x180
#define GPIO_ALT_FN_2_IN	0x200
#define GPIO_ALT_FN_2_OUT	0x280
#define GPIO_ALT_FN_3_IN	0x300
#define GPIO_ALT_FN_3_OUT	0x380
#define GPIO_MD_MASK_NR		0x07f
#define GPIO_MD_MASK_DIR	0x080
#define GPIO_MD_MASK_FN		0x300
#define GPIO_DFLT_LOW		0x400
#define GPIO_DFLT_HIGH		0x800

#define GPIO1_RTS_MD		( 1 | GPIO_ALT_FN_1_IN)
#define GPIO6_MMCCLK_MD		( 6 | GPIO_ALT_FN_1_OUT)
#define GPIO7_48MHz_MD		( 7 | GPIO_ALT_FN_1_OUT)
#define GPIO8_MMCCS0_MD		( 8 | GPIO_ALT_FN_1_OUT)
#define GPIO9_MMCCS1_MD		( 9 | GPIO_ALT_FN_1_OUT)
#define GPIO10_RTCCLK_MD	(10 | GPIO_ALT_FN_1_OUT)
#define GPIO11_3_6MHz_MD	(11 | GPIO_ALT_FN_1_OUT)
#define GPIO12_32KHz_MD		(12 | GPIO_ALT_FN_1_OUT)
#define GPIO13_MBGNT_MD		(13 | GPIO_ALT_FN_2_OUT)
#define GPIO14_MBREQ_MD		(14 | GPIO_ALT_FN_1_IN)
#define GPIO15_nCS_1_MD		(15 | GPIO_ALT_FN_2_OUT)
#define GPIO16_PWM0_MD		(16 | GPIO_ALT_FN_2_OUT)
#define GPIO17_PWM1_MD		(17 | GPIO_ALT_FN_2_OUT)
#define GPIO18_RDY_MD		(18 | GPIO_ALT_FN_1_IN)
#define GPIO19_DREQ1_MD		(19 | GPIO_ALT_FN_1_IN)
#define GPIO20_DREQ0_MD		(20 | GPIO_ALT_FN_1_IN)
#define GPIO23_SCLK_MD		(23 | GPIO_ALT_FN_2_OUT)
#define GPIO24_SFRM_MD		(24 | GPIO_ALT_FN_2_OUT)
#define GPIO25_STXD_MD		(25 | GPIO_ALT_FN_2_OUT)
#define GPIO26_SRXD_MD		(26 | GPIO_ALT_FN_1_IN)
#define GPIO27_SEXTCLK_MD	(27 | GPIO_ALT_FN_1_IN)
#define GPIO28_BITCLK_AC97_MD	(28 | GPIO_ALT_FN_1_IN)
#define GPIO28_BITCLK_IN_I2S_MD	(28 | GPIO_ALT_FN_2_IN)
#define GPIO28_BITCLK_OUT_I2S_MD	(28 | GPIO_ALT_FN_1_OUT)
#define GPIO29_SDATA_IN_AC97_MD	(29 | GPIO_ALT_FN_1_IN)
#define GPIO29_SDATA_IN_I2S_MD	(29 | GPIO_ALT_FN_2_IN)
#define GPIO30_SDATA_OUT_AC97_MD	(30 | GPIO_ALT_FN_2_OUT)
#define GPIO30_SDATA_OUT_I2S_MD	(30 | GPIO_ALT_FN_1_OUT)
#define GPIO31_SYNC_I2S_MD	(31 | GPIO_ALT_FN_1_OUT)
#define GPIO31_SYNC_AC97_MD	(31 | GPIO_ALT_FN_2_OUT)
#define GPIO32_SDATA_IN1_AC97_MD	(32 | GPIO_ALT_FN_1_IN)
#define GPIO32_SYSCLK_I2S_MD	(32 | GPIO_ALT_FN_1_OUT)
#define GPIO32_MMCCLK_MD		( 32 | GPIO_ALT_FN_2_OUT)
#define GPIO33_nCS_5_MD		(33 | GPIO_ALT_FN_2_OUT)
#define GPIO34_FFRXD_MD		(34 | GPIO_ALT_FN_1_IN)
#define GPIO34_MMCCS0_MD	(34 | GPIO_ALT_FN_2_OUT)
#define GPIO35_FFCTS_MD		(35 | GPIO_ALT_FN_1_IN)
#define GPIO36_FFDCD_MD		(36 | GPIO_ALT_FN_1_IN)
#define GPIO37_FFDSR_MD		(37 | GPIO_ALT_FN_1_IN)
#define GPIO38_FFRI_MD		(38 | GPIO_ALT_FN_1_IN)
#define GPIO39_MMCCS1_MD	(39 | GPIO_ALT_FN_1_OUT)
#define GPIO39_FFTXD_MD		(39 | GPIO_ALT_FN_2_OUT)
#define GPIO40_FFDTR_MD		(40 | GPIO_ALT_FN_2_OUT)
#define GPIO41_FFRTS_MD		(41 | GPIO_ALT_FN_2_OUT)
#define GPIO42_BTRXD_MD		(42 | GPIO_ALT_FN_1_IN)
#define GPIO42_HWRXD_MD		(42 | GPIO_ALT_FN_3_IN)
#define GPIO43_BTTXD_MD		(43 | GPIO_ALT_FN_2_OUT)
#define GPIO43_HWTXD_MD		(43 | GPIO_ALT_FN_3_OUT)
#define GPIO44_BTCTS_MD		(44 | GPIO_ALT_FN_1_IN)
#define GPIO44_HWCTS_MD		(44 | GPIO_ALT_FN_3_IN)
#define GPIO45_BTRTS_MD		(45 | GPIO_ALT_FN_2_OUT)
#define GPIO45_HWRTS_MD		(45 | GPIO_ALT_FN_3_OUT)
#define GPIO45_SYSCLK_AC97_MD		(45 | GPIO_ALT_FN_1_OUT)
#define GPIO46_ICPRXD_MD	(46 | GPIO_ALT_FN_1_IN)
#define GPIO46_STRXD_MD		(46 | GPIO_ALT_FN_2_IN)
#define GPIO47_ICPTXD_MD	(47 | GPIO_ALT_FN_2_OUT)
#define GPIO47_STTXD_MD		(47 | GPIO_ALT_FN_1_OUT)
#define GPIO48_nPOE_MD		(48 | GPIO_ALT_FN_2_OUT)
#define GPIO48_HWTXD_MD         (48 | GPIO_ALT_FN_1_OUT)
#define GPIO48_nPOE_MD          (48 | GPIO_ALT_FN_2_OUT)
#define GPIO49_HWRXD_MD		(49 | GPIO_ALT_FN_1_IN)
#define GPIO49_nPWE_MD		(49 | GPIO_ALT_FN_2_OUT)
#define GPIO50_nPIOR_MD		(50 | GPIO_ALT_FN_2_OUT)
#define GPIO50_HWCTS_MD         (50 | GPIO_ALT_FN_1_IN)
#define GPIO51_HWRTS_MD         (51 | GPIO_ALT_FN_1_OUT)
#define GPIO51_nPIOW_MD		(51 | GPIO_ALT_FN_2_OUT)
#define GPIO52_nPCE_1_MD	(52 | GPIO_ALT_FN_2_OUT)
#define GPIO53_nPCE_2_MD	(53 | GPIO_ALT_FN_2_OUT)
#define GPIO53_MMCCLK_MD	(53 | GPIO_ALT_FN_1_OUT)
#define GPIO54_MMCCLK_MD	(54 | GPIO_ALT_FN_1_OUT)
#define GPIO54_nPCE_2_MD	(54 | GPIO_ALT_FN_2_OUT)
#define GPIO54_pSKTSEL_MD	(54 | GPIO_ALT_FN_2_OUT)
#define GPIO55_nPREG_MD		(55 | GPIO_ALT_FN_2_OUT)
#define GPIO56_nPWAIT_MD	(56 | GPIO_ALT_FN_1_IN)
#define GPIO57_nIOIS16_MD	(57 | GPIO_ALT_FN_1_IN)
#define GPIO58_LDD_0_MD		(58 | GPIO_ALT_FN_2_OUT)
#define GPIO59_LDD_1_MD		(59 | GPIO_ALT_FN_2_OUT)
#define GPIO60_LDD_2_MD		(60 | GPIO_ALT_FN_2_OUT)
#define GPIO61_LDD_3_MD		(61 | GPIO_ALT_FN_2_OUT)
#define GPIO62_LDD_4_MD		(62 | GPIO_ALT_FN_2_OUT)
#define GPIO63_LDD_5_MD		(63 | GPIO_ALT_FN_2_OUT)
#define GPIO64_LDD_6_MD		(64 | GPIO_ALT_FN_2_OUT)
#define GPIO65_LDD_7_MD		(65 | GPIO_ALT_FN_2_OUT)
#define GPIO66_LDD_8_MD		(66 | GPIO_ALT_FN_2_OUT)
#define GPIO66_MBREQ_MD		(66 | GPIO_ALT_FN_1_IN)
#define GPIO67_LDD_9_MD		(67 | GPIO_ALT_FN_2_OUT)
#define GPIO67_MMCCS0_MD	(67 | GPIO_ALT_FN_1_OUT)
#define GPIO68_LDD_10_MD	(68 | GPIO_ALT_FN_2_OUT)
#define GPIO68_MMCCS1_MD	(68 | GPIO_ALT_FN_1_OUT)
#define GPIO69_LDD_11_MD	(69 | GPIO_ALT_FN_2_OUT)
#define GPIO69_MMCCLK_MD	(69 | GPIO_ALT_FN_1_OUT)
#define GPIO70_LDD_12_MD	(70 | GPIO_ALT_FN_2_OUT)
#define GPIO70_RTCCLK_MD	(70 | GPIO_ALT_FN_1_OUT)
#define GPIO71_LDD_13_MD	(71 | GPIO_ALT_FN_2_OUT)
#define GPIO71_3_6MHz_MD	(71 | GPIO_ALT_FN_1_OUT)
#define GPIO72_LDD_14_MD	(72 | GPIO_ALT_FN_2_OUT)
#define GPIO72_32kHz_MD		(72 | GPIO_ALT_FN_1_OUT)
#define GPIO73_LDD_15_MD	(73 | GPIO_ALT_FN_2_OUT)
#define GPIO73_MBGNT_MD		(73 | GPIO_ALT_FN_1_OUT)
#define GPIO74_LCD_FCLK_MD	(74 | GPIO_ALT_FN_2_OUT)
#define GPIO75_LCD_LCLK_MD	(75 | GPIO_ALT_FN_2_OUT)
#define GPIO76_LCD_PCLK_MD	(76 | GPIO_ALT_FN_2_OUT)
#define GPIO77_LCD_ACBIAS_MD	(77 | GPIO_ALT_FN_2_OUT)
#define GPIO78_nCS_2_MD		(78 | GPIO_ALT_FN_2_OUT)
#define GPIO79_nCS_3_MD		(79 | GPIO_ALT_FN_2_OUT)
#define GPIO79_pSKTSEL_MD	(79 | GPIO_ALT_FN_1_OUT)
#define GPIO80_nCS_4_MD		(80 | GPIO_ALT_FN_2_OUT)
#define GPIO81_NSSP_CLK_OUT 	(81 | GPIO_ALT_FN_1_OUT)
#define GPIO81_NSSP_CLK_IN  	(81 | GPIO_ALT_FN_1_IN)
#define GPIO82_NSSP_FRM_OUT 	(82 | GPIO_ALT_FN_1_OUT)
#define GPIO82_NSSP_FRM_IN  	(82 | GPIO_ALT_FN_1_IN)
#define GPIO83_NSSP_TX      	(83 | GPIO_ALT_FN_1_OUT)
#define GPIO83_NSSP_RX      	(83 | GPIO_ALT_FN_2_IN)
#define GPIO84_NSSP_TX      	(84 | GPIO_ALT_FN_1_OUT)
#define GPIO84_NSSP_RX      	(84 | GPIO_ALT_FN_2_IN)
#define GPIO85_nPCE_1_MD	(85 | GPIO_ALT_FN_1_OUT)
#define GPIO92_MMCDAT0_MD	(92 | GPIO_ALT_FN_1_OUT)
#define GPIO104_pSKTSEL_MD	(104 | GPIO_ALT_FN_1_OUT)
#define GPIO109_MMCDAT1_MD	(109 | GPIO_ALT_FN_1_OUT)
#define GPIO110_MMCDAT2_MD	(110 | GPIO_ALT_FN_1_OUT)
#define GPIO110_MMCCS0_MD	(110 | GPIO_ALT_FN_1_OUT)
#define GPIO111_MMCDAT3_MD	(111 | GPIO_ALT_FN_1_OUT)
#define GPIO110_MMCCS1_MD	(111 | GPIO_ALT_FN_1_OUT)
#define GPIO112_MMCCMD_MD	(112 | GPIO_ALT_FN_1_OUT)
#define GPIO113_I2S_SYSCLK_MD	(113 | GPIO_ALT_FN_1_OUT)
#define GPIO113_AC97_RESET_N_MD	(113 | GPIO_ALT_FN_2_OUT)
#define GPIO117_I2CSCL_MD	(117 | GPIO_ALT_FN_1_OUT)
#define GPIO118_I2CSDA_MD	(118 | GPIO_ALT_FN_1_IN)

/*
 * Power Manager
 */

#define PMCR		__REG(0x40F00000)  /* Power Manager Control Register */
#define PSSR		__REG(0x40F00004)  /* Power Manager Sleep Status Register */
#define PSPR		__REG(0x40F00008)  /* Power Manager Scratch Pad Register */
#define PWER		__REG(0x40F0000C)  /* Power Manager Wake-up Enable Register */
#define PRER		__REG(0x40F00010)  /* Power Manager GPIO Rising-Edge Detect Enable Register */
#define PFER		__REG(0x40F00014)  /* Power Manager GPIO Falling-Edge Detect Enable Register */
#define PEDR		__REG(0x40F00018)  /* Power Manager GPIO Edge Detect Status Register */
#define PCFR		__REG(0x40F0001C)  /* Power Manager General Configuration Register */
#define PGSR0		__REG(0x40F00020)  /* Power Manager GPIO Sleep State Register for GP[31-0] */
#define PGSR1		__REG(0x40F00024)  /* Power Manager GPIO Sleep State Register for GP[63-32] */
#define PGSR2		__REG(0x40F00028)  /* Power Manager GPIO Sleep State Register for GP[84-64] */
#define PGSR3		__REG(0x40F0002C)  /* Power Manager GPIO Sleep State Register for GP[118-96] */
#define RCSR		__REG(0x40F00030)  /* Reset Controller Status Register */

#define PSLR		__REG(0x40F00034)	/* Power Manager Sleep Config Register */
#define PSTR		__REG(0x40F00038)	/*Power Manager Standby Config Register */
#define PSNR		__REG(0x40F0003C)	/*Power Manager Sense Config Register */
#define PVCR		__REG(0x40F00040)	/*Power Manager VoltageControl Register */
#define PKWR		__REG(0x40F00050)	/* Power Manager KB Wake-up Enable Reg */
#define PKSR		__REG(0x40F00054)	/* Power Manager KB Level-Detect Register */
#define PCMD(x)	__REG2(0x40F00080, (x)<<2)
#define PCMD0	__REG(0x40F00080 + 0 * 4)
#define PCMD1	__REG(0x40F00080 + 1 * 4)
#define PCMD2	__REG(0x40F00080 + 2 * 4)
#define PCMD3	__REG(0x40F00080 + 3 * 4)
#define PCMD4	__REG(0x40F00080 + 4 * 4)
#define PCMD5	__REG(0x40F00080 + 5 * 4)
#define PCMD6	__REG(0x40F00080 + 6 * 4)
#define PCMD7	__REG(0x40F00080 + 7 * 4)
#define PCMD8	__REG(0x40F00080 + 8 * 4)
#define PCMD9	__REG(0x40F00080 + 9 * 4)
#define PCMD10	__REG(0x40F00080 + 10 * 4)
#define PCMD11	__REG(0x40F00080 + 11 * 4)
#define PCMD12	__REG(0x40F00080 + 12 * 4)
#define PCMD13	__REG(0x40F00080 + 13 * 4)
#define PCMD14	__REG(0x40F00080 + 14 * 4)
#define PCMD15	__REG(0x40F00080 + 15 * 4)
#define PCMD16	__REG(0x40F00080 + 16 * 4)
#define PCMD17	__REG(0x40F00080 + 17 * 4)
#define PCMD18	__REG(0x40F00080 + 18 * 4)
#define PCMD19	__REG(0x40F00080 + 19 * 4)
#define PCMD20	__REG(0x40F00080 + 20 * 4)
#define PCMD21	__REG(0x40F00080 + 21 * 4)
#define PCMD22	__REG(0x40F00080 + 22 * 4)
#define PCMD23	__REG(0x40F00080 + 23 * 4)
#define PCMD24	__REG(0x40F00080 + 24 * 4)
#define PCMD25	__REG(0x40F00080 + 25 * 4)
#define PCMD26	__REG(0x40F00080 + 26 * 4)
#define PCMD27	__REG(0x40F00080 + 27 * 4)
#define PCMD28	__REG(0x40F00080 + 28 * 4)
#define PCMD29	__REG(0x40F00080 + 29 * 4)
#define PCMD30	__REG(0x40F00080 + 30 * 4)
#define PCMD31	__REG(0x40F00080 + 31 * 4)

#define PCMD_MBC	(1<<12)
#define PCMD_DCE	(1<<11)
#define PCMD_LC	(1<<10)
/* FIXME:  PCMD_SQC need be checked.   */
#define PCMD_SQC	(3<<8)	/* currently only bit 8 is changeable,
				   bit 9 should be 0 all day. */
#define PVCR_VCSA	(0x1<<14)
#define PVCR_CommandDelay (0xf80)
#define PCFR_PI2C_EN	(0x1 << 6)

#define PSSR_OTGPH	(1 << 6)	/* OTG Peripheral control Hold */
#define PSSR_RDH	(1 << 5)	/* Read Disable Hold */
#define PSSR_PH		(1 << 4)	/* Peripheral Control Hold */
#define PSSR_STS	(1 << 3)	/* Standby Mode Status */
#define PSSR_VFS	(1 << 2)	/* VDD Fault Status */
#define PSSR_BFS	(1 << 1)	/* Battery Fault Status */
#define PSSR_SSS	(1 << 0)	/* Software Sleep Status */

#define PSLR_SL_ROD	(1 << 20)	/* Sleep-Mode/Depp-Sleep Mode nRESET_OUT Disable */

#define PCFR_RO		(1 << 15)	/* RDH Override */
#define PCFR_PO		(1 << 14)	/* PH Override */
#define PCFR_GPROD	(1 << 12)	/* GPIO nRESET_OUT Disable */
#define PCFR_L1_EN	(1 << 11)	/* Sleep Mode L1 converter Enable */
#define PCFR_FVC	(1 << 10)	/* Frequency/Voltage Change */
#define PCFR_DC_EN	(1 << 7)	/* Sleep/deep-sleep DC-DC Converter Enable */
#define PCFR_PI2CEN	(1 << 6)	/* Enable PI2C controller */
#define PCFR_GPR_EN	(1 << 4)	/* nRESET_GPIO Pin Enable */
#define PCFR_DS		(1 << 3)	/* Deep Sleep Mode */
#define PCFR_FS		(1 << 2)	/* Float Static Chip Selects */
#define PCFR_FP		(1 << 1)	/* Float PCMCIA controls */
#define PCFR_OPDE	(1 << 0)	/* 3.6864 MHz oscillator power-down enable */

#define RCSR_GPR	(1 << 3)	/* GPIO Reset */
#define RCSR_SMR	(1 << 2)	/* Sleep Mode */
#define RCSR_WDR	(1 << 1)	/* Watchdog Reset */
#define RCSR_HWR	(1 << 0)	/* Hardware Reset */

#define PWER_GPIO(Nb)	(1 << Nb)	/* GPIO [0..15] wake-up enable     */
#define PWER_GPIO0	PWER_GPIO (0)	/* GPIO  [0] wake-up enable        */
#define PWER_GPIO1	PWER_GPIO (1)	/* GPIO  [1] wake-up enable        */
#define PWER_GPIO2	PWER_GPIO (2)	/* GPIO  [2] wake-up enable        */
#define PWER_GPIO3	PWER_GPIO (3)	/* GPIO  [3] wake-up enable        */
#define PWER_GPIO4	PWER_GPIO (4)	/* GPIO  [4] wake-up enable        */
#define PWER_GPIO5	PWER_GPIO (5)	/* GPIO  [5] wake-up enable        */
#define PWER_GPIO6	PWER_GPIO (6)	/* GPIO  [6] wake-up enable        */
#define PWER_GPIO7	PWER_GPIO (7)	/* GPIO  [7] wake-up enable        */
#define PWER_GPIO8	PWER_GPIO (8)	/* GPIO  [8] wake-up enable        */
#define PWER_GPIO9	PWER_GPIO (9)	/* GPIO  [9] wake-up enable        */
#define PWER_GPIO10	PWER_GPIO (10)	/* GPIO [10] wake-up enable        */
#define PWER_GPIO11	PWER_GPIO (11)	/* GPIO [11] wake-up enable        */
#define PWER_GPIO12	PWER_GPIO (12)	/* GPIO [12] wake-up enable        */
#define PWER_GPIO13	PWER_GPIO (13)	/* GPIO [13] wake-up enable        */
#define PWER_GPIO14	PWER_GPIO (14)	/* GPIO [14] wake-up enable        */
#define PWER_GPIO15	PWER_GPIO (15)	/* GPIO [15] wake-up enable        */
#define PWER_RTC	0x80000000	/* RTC alarm wake-up enable        */


/*
 * SSP Serial Port Registers
 * PXA250, PXA255, PXA26x and PXA27x SSP controllers are all slightly different.
 * PXA255, PXA26x and PXA27x have extra ports, registers and bits.
 */

 /* Common PXA2xx bits first */
#define SSCR0_DSS	(0x0000000f)	/* Data Size Select (mask) */
#define SSCR0_DataSize(x)  ((x) - 1)	/* Data Size Select [4..16] */
#define SSCR0_FRF	(0x00000030)	/* FRame Format (mask) */
#define SSCR0_Motorola	(0x0 << 4)	/* Motorola's Serial Peripheral Interface (SPI) */
#define SSCR0_TI	(0x1 << 4)	/* Texas Instruments' Synchronous Serial Protocol (SSP) */
#define SSCR0_National	(0x2 << 4)	/* National Microwire */
#define SSCR0_ECS	(1 << 6)	/* External clock select */
#define SSCR0_SSE	(1 << 7)	/* Synchronous Serial Port Enable */
#define SSCR0_SCR	(0x0000ff00)	/* Serial Clock Rate (mask) */
#define SSCR0_SerClkDiv(x) ((((x) - 2)/2) << 8) /* Divisor [2..512] */

#define SSCR1_RIE	(1 << 0)	/* Receive FIFO Interrupt Enable */
#define SSCR1_TIE	(1 << 1)	/* Transmit FIFO Interrupt Enable */
#define SSCR1_LBM	(1 << 2)	/* Loop-Back Mode */
#define SSCR1_SPO	(1 << 3)	/* Motorola SPI SSPSCLK polarity setting */
#define SSCR1_SPH	(1 << 4)	/* Motorola SPI SSPSCLK phase setting */
#define SSCR1_MWDS	(1 << 5)	/* Microwire Transmit Data Size */
#define SSCR1_TFT	(0x000003c0)	/* Transmit FIFO Threshold (mask) */
#define SSCR1_TxTresh(x) (((x) - 1) << 6) /* level [1..16] */
#define SSCR1_RFT	(0x00003c00)	/* Receive FIFO Threshold (mask) */
#define SSCR1_RxTresh(x) (((x) - 1) << 10) /* level [1..16] */

#define SSSR_TNF	(1 << 2)	/* Transmit FIFO Not Full */
#define SSSR_RNE	(1 << 3)	/* Receive FIFO Not Empty */
#define SSSR_BSY	(1 << 4)	/* SSP Busy */
#define SSSR_TFS	(1 << 5)	/* Transmit FIFO Service Request */
#define SSSR_RFS	(1 << 6)	/* Receive FIFO Service Request */
#define SSSR_ROR	(1 << 7)	/* Receive FIFO Overrun */

#define SSCR0_TIM		(1 << 23)	/* Transmit FIFO Under Run Interrupt Mask */
#define SSCR0_RIM		(1 << 22)	/* Receive FIFO Over Run interrupt Mask */
#define SSCR0_NCS		(1 << 21)	/* Network Clock Select */
#define SSCR0_EDSS		(1 << 20)	/* Extended Data Size Select */

/* extra bits in PXA255, PXA26x and PXA27x SSP ports */
#define SSCR0_PSP		(3 << 4)	/* PSP - Programmable Serial Protocol */
#define SSCR1_TTELP		(1 << 31)	/* TXD Tristate Enable Last Phase */
#define SSCR1_TTE		(1 << 30)	/* TXD Tristate Enable */
#define SSCR1_EBCEI		(1 << 29)	/* Enable Bit Count Error interrupt */
#define SSCR1_SCFR		(1 << 28)	/* Slave Clock free Running */
#define SSCR1_ECRA		(1 << 27)	/* Enable Clock Request A */
#define SSCR1_ECRB		(1 << 26)	/* Enable Clock request B */
#define SSCR1_SCLKDIR	(1 << 25)	/* Serial Bit Rate Clock Direction */
#define SSCR1_SFRMDIR	(1 << 24)	/* Frame Direction */
#define SSCR1_RWOT		(1 << 23)	/* Receive Without Transmit */
#define SSCR1_TRAIL		(1 << 22)	/* Trailing Byte */
#define SSCR1_TSRE		(1 << 21)	/* Transmit Service Request Enable */
#define SSCR1_RSRE		(1 << 20)	/* Receive Service Request Enable */
#define SSCR1_TINTE		(1 << 19)	/* Receiver Time-out Interrupt enable */
#define SSCR1_PINTE		(1 << 18)	/* Peripheral Trailing Byte Interupt Enable */
#define SSCR1_STRF		(1 << 15)	/* Select FIFO or EFWR */
#define SSCR1_EFWR		(1 << 14)	/* Enable FIFO Write/Read */

#define SSSR_BCE		(1 << 23)	/* Bit Count Error */
#define SSSR_CSS		(1 << 22)	/* Clock Synchronisation Status */
#define SSSR_TUR		(1 << 21)	/* Transmit FIFO Under Run */
#define SSSR_EOC		(1 << 20)	/* End Of Chain */
#define SSSR_TINT		(1 << 19)	/* Receiver Time-out Interrupt */
#define SSSR_PINT		(1 << 18)	/* Peripheral Trailing Byte Interrupt */

#define SSPSP_DMYSTOP(x)	(x << 23)	/* Dummy Stop */
#define SSPSP_SFRMWDTH(x)	(x << 16)	/* Serial Frame Width */
#define SSPSP_SFRMDLY(x)	(x << 9)	/* Serial Frame Delay */
#define SSPSP_DMYSTRT(x)	(x << 7)	/* Dummy Start */
#define SSPSP_STRTDLY(x)	(x << 4)	/* Start Delay */
#define SSPSP_ETDS			(1 << 3)	/* End of Transfer data State */
#define SSPSP_SFRMP			(1 << 2)	/* Serial Frame Polarity */
#define SSPSP_SCMODE(x)		(x << 0)	/* Serial Bit Rate Clock Mode */


#define SSCR0_P1	__REG(0x41000000)  /* SSP Port 1 Control Register 0 */
#define SSCR1_P1	__REG(0x41000004)  /* SSP Port 1 Control Register 1 */
#define SSSR_P1		__REG(0x41000008)  /* SSP Port 1 Status Register */
#define SSITR_P1	__REG(0x4100000C)  /* SSP Port 1 Interrupt Test Register */
#define SSDR_P1		__REG(0x41000010)  /* (Write / Read) SSP Port 1 Data Write Register/SSP Data Read Register */

/* Support existing PXA25x drivers */
#define SSCR0		SSCR0_P1  /* SSP Control Register 0 */
#define SSCR1		SSCR1_P1  /* SSP Control Register 1 */
#define SSSR		SSSR_P1	  /* SSP Status Register */
#define SSITR		SSITR_P1  /* SSP Interrupt Test Register */
#define SSDR		SSDR_P1	  /* (Write / Read) SSP Data Write Register/SSP Data Read Register */

/* PXA27x ports */
#if defined (CONFIG_PXA27x)
#define SSTO_P1		__REG(0x41000028)  /* SSP Port 1 Time Out Register */
#define SSPSP_P1	__REG(0x4100002C)  /* SSP Port 1 Programmable Serial Protocol */
#define SSCR0_P2	__REG(0x41700000)  /* SSP Port 2 Control Register 0 */
#define SSCR1_P2	__REG(0x41700004)  /* SSP Port 2 Control Register 1 */
#define SSSR_P2		__REG(0x41700008)  /* SSP Port 2 Status Register */
#define SSITR_P2	__REG(0x4170000C)  /* SSP Port 2 Interrupt Test Register */
#define SSDR_P2		__REG(0x41700010)  /* (Write / Read) SSP Port 2 Data Write Register/SSP Data Read Register */
#define SSTO_P2		__REG(0x41700028)  /* SSP Port 2 Time Out Register */
#define SSPSP_P2	__REG(0x4170002C)  /* SSP Port 2 Programmable Serial Protocol */
#define SSCR0_P3	__REG(0x41900000)  /* SSP Port 3 Control Register 0 */
#define SSCR1_P3	__REG(0x41900004)  /* SSP Port 3 Control Register 1 */
#define SSSR_P3		__REG(0x41900008)  /* SSP Port 3 Status Register */
#define SSITR_P3	__REG(0x4190000C)  /* SSP Port 3 Interrupt Test Register */
#define SSDR_P3		__REG(0x41900010)  /* (Write / Read) SSP Port 3 Data Write Register/SSP Data Read Register */
#define SSTO_P3		__REG(0x41900028)  /* SSP Port 3 Time Out Register */
#define SSPSP_P3	__REG(0x4190002C)  /* SSP Port 3 Programmable Serial Protocol */
#else /* PXA255 (only port 2) and PXA26x ports*/
#define SSTO_P1		__REG(0x41000028)  /* SSP Port 1 Time Out Register */
#define SSPSP_P1	__REG(0x4100002C)  /* SSP Port 1 Programmable Serial Protocol */
#define SSCR0_P2	__REG(0x41400000)  /* SSP Port 2 Control Register 0 */
#define SSCR1_P2	__REG(0x41400004)  /* SSP Port 2 Control Register 1 */
#define SSSR_P2		__REG(0x41400008)  /* SSP Port 2 Status Register */
#define SSITR_P2	__REG(0x4140000C)  /* SSP Port 2 Interrupt Test Register */
#define SSDR_P2		__REG(0x41400010)  /* (Write / Read) SSP Port 2 Data Write Register/SSP Data Read Register */
#define SSTO_P2		__REG(0x41400028)  /* SSP Port 2 Time Out Register */
#define SSPSP_P2	__REG(0x4140002C)  /* SSP Port 2 Programmable Serial Protocol */
#define SSCR0_P3	__REG(0x41500000)  /* SSP Port 3 Control Register 0 */
#define SSCR1_P3	__REG(0x41500004)  /* SSP Port 3 Control Register 1 */
#define SSSR_P3		__REG(0x41500008)  /* SSP Port 3 Status Register */
#define SSITR_P3	__REG(0x4150000C)  /* SSP Port 3 Interrupt Test Register */
#define SSDR_P3		__REG(0x41500010)  /* (Write / Read) SSP Port 3 Data Write Register/SSP Data Read Register */
#define SSTO_P3		__REG(0x41500028)  /* SSP Port 3 Time Out Register */
#define SSPSP_P3	__REG(0x4150002C)  /* SSP Port 3 Programmable Serial Protocol */
#endif

#define SSCR0_P(x) (*(((x) == 1) ? &SSCR0_P1 : ((x) == 2) ? &SSCR0_P2 : ((x) == 3) ? &SSCR0_P3 : NULL))
#define SSCR1_P(x) (*(((x) == 1) ? &SSCR1_P1 : ((x) == 2) ? &SSCR1_P2 : ((x) == 3) ? &SSCR1_P3 : NULL))
#define SSSR_P(x) (*(((x) == 1) ? &SSSR_P1 : ((x) == 2) ? &SSSR_P2 : ((x) == 3) ? &SSSR_P3 : NULL))
#define SSITR_P(x) (*(((x) == 1) ? &SSITR_P1 : ((x) == 2) ? &SSITR_P2 : ((x) == 3) ? &SSITR_P3 : NULL))
#define SSDR_P(x) (*(((x) == 1) ? &SSDR_P1 : ((x) == 2) ? &SSDR_P2 : ((x) == 3) ? &SSDR_P3 : NULL))
#define SSTO_P(x) (*(((x) == 1) ? &SSTO_P1 : ((x) == 2) ? &SSTO_P2 : ((x) == 3) ? &SSTO_P3 : NULL))
#define SSPSP_P(x) (*(((x) == 1) ? &SSPSP_P1 : ((x) == 2) ? &SSPSP_P2 : ((x) == 3) ? &SSPSP_P3 : NULL))

/*
 * MultiMediaCard (MMC) controller
 */

#define MMC_STRPCL	__REG(0x41100000)  /* Control to start and stop MMC clock */
#define MMC_STAT	__REG(0x41100004)  /* MMC Status Register (read only) */
#define MMC_CLKRT	__REG(0x41100008)  /* MMC clock rate */
#define MMC_SPI		__REG(0x4110000c)  /* SPI mode control bits */
#define MMC_CMDAT	__REG(0x41100010)  /* Command/response/data sequence control */
#define MMC_RESTO	__REG(0x41100014)  /* Expected response time out */
#define MMC_RDTO	__REG(0x41100018)  /* Expected data read time out */
#define MMC_BLKLEN	__REG(0x4110001c)  /* Block length of data transaction */
#define MMC_NOB		__REG(0x41100020)  /* Number of blocks, for block mode */
#define MMC_PRTBUF	__REG(0x41100024)  /* Partial MMC_TXFIFO FIFO written */
#define MMC_I_MASK	__REG(0x41100028)  /* Interrupt Mask */
#define MMC_I_REG	__REG(0x4110002c)  /* Interrupt Register (read only) */
#define MMC_CMD		__REG(0x41100030)  /* Index of current command */
#define MMC_ARGH	__REG(0x41100034)  /* MSW part of the current command argument */
#define MMC_ARGL	__REG(0x41100038)  /* LSW part of the current command argument */
#define MMC_RES		__REG(0x4110003c)  /* Response FIFO (read only) */
#define MMC_RXFIFO	__REG(0x41100040)  /* Receive FIFO (read only) */
#define MMC_TXFIFO	__REG(0x41100044)  /* Transmit FIFO (write only) */


/*
 * Core Clock
 */

#define CCCR		__REG(0x41300000)  /* Core Clock Configuration Register */
#define CKEN		__REG(0x41300004)  /* Clock Enable Register */
#define OSCC		__REG(0x41300008)  /* Oscillator Configuration Register */
#define CCSR		__REG(0x4130000C)  /* Core Clock Status Register */

#define CCCR_N_MASK	0x0380		/* Run Mode Frequency to Turbo Mode Frequency Multiplier */
#define CCCR_M_MASK	0x0060		/* Memory Frequency to Run Mode Frequency Multiplier */
#define CCCR_L_MASK	0x001f		/* Crystal Frequency to Memory Frequency Multiplier */

#define CKEN24_CAMERA	(1 << 24)	/* Camera Interface Clock Enable */
#define CKEN23_SSP1	(1 << 23)	/* SSP1 Unit Clock Enable */
#define CKEN22_MEMC	(1 << 22)	/* Memory Controller Clock Enable */
#define CKEN21_MEMSTK	(1 << 21)	/* Memory Stick Host Controller */
#define CKEN20_IM	(1 << 20)	/* Internal Memory Clock Enable */
#define CKEN19_KEYPAD	(1 << 19)	/* Keypad Interface Clock Enable */
#define CKEN18_USIM	(1 << 18)	/* USIM Unit Clock Enable */
#define CKEN17_MSL	(1 << 17)	/* MSL Unit Clock Enable */
#define CKEN16_LCD	(1 << 16)	/* LCD Unit Clock Enable */
#define CKEN15_PWRI2C	(1 << 15)	/* PWR I2C Unit Clock Enable */
#define CKEN14_I2C	(1 << 14)	/* I2C Unit Clock Enable */
#define CKEN13_FICP	(1 << 13)	/* FICP Unit Clock Enable */
#define CKEN12_MMC	(1 << 12)	/* MMC Unit Clock Enable */
#define CKEN11_USB	(1 << 11)	/* USB Unit Clock Enable */
#define CKEN10_ASSP	(1 << 10)	/* ASSP (SSP3) Clock Enable */
#define CKEN10_USBHOST	(1 << 10)	/* USB Host Unit Clock Enable */
#define CKEN9_OSTIMER	(1 << 9)	/* OS Timer Unit Clock Enable */
#define CKEN9_NSSP	(1 << 9)	/* NSSP (SSP2) Clock Enable */
#define CKEN8_I2S	(1 << 8)	/* I2S Unit Clock Enable */
#define CKEN7_BTUART	(1 << 7)	/* BTUART Unit Clock Enable */
#define CKEN6_FFUART	(1 << 6)	/* FFUART Unit Clock Enable */
#define CKEN5_STUART	(1 << 5)	/* STUART Unit Clock Enable */
#define CKEN4_HWUART	(1 << 4)	/* HWUART Unit Clock Enable */
#define CKEN4_SSP3	(1 << 4)	/* SSP3 Unit Clock Enable */
#define CKEN3_SSP	(1 << 3)	/* SSP Unit Clock Enable */
#define CKEN3_SSP2	(1 << 3)	/* SSP2 Unit Clock Enable */
#define CKEN2_AC97	(1 << 2)	/* AC97 Unit Clock Enable */
#define CKEN1_PWM1	(1 << 1)	/* PWM1 Clock Enable */
#define CKEN0_PWM0	(1 << 0)	/* PWM0 Clock Enable */

#define OSCC_OON	(1 << 1)	/* 32.768kHz OON (write-once only bit) */
#define OSCC_OOK	(1 << 0)	/* 32.768kHz OOK (read-only bit) */


/*
 * LCD
 */

#define LCCR0		__REG(0x44000000)  /* LCD Controller Control Register 0 */
#define LCCR1		__REG(0x44000004)  /* LCD Controller Control Register 1 */
#define LCCR2		__REG(0x44000008)  /* LCD Controller Control Register 2 */
#define LCCR3		__REG(0x4400000C)  /* LCD Controller Control Register 3 */
#define DFBR0		__REG(0x44000020)  /* DMA Channel 0 Frame Branch Register */
#define DFBR1		__REG(0x44000024)  /* DMA Channel 1 Frame Branch Register */
#define LCSR		__REG(0x44000038)  /* LCD Controller Status Register */
#define LIIDR		__REG(0x4400003C)  /* LCD Controller Interrupt ID Register */
#define TMEDRGBR	__REG(0x44000040)  /* TMED RGB Seed Register */
#define TMEDCR		__REG(0x44000044)  /* TMED Control Register */

#define LCCR3_1BPP (0 << 24)
#define LCCR3_2BPP (1 << 24)
#define LCCR3_4BPP (2 << 24)
#define LCCR3_8BPP (3 << 24)
#define LCCR3_16BPP (4 << 24)

#define FDADR0		__REG(0x44000200)  /* DMA Channel 0 Frame Descriptor Address Register */
#define FSADR0		__REG(0x44000204)  /* DMA Channel 0 Frame Source Address Register */
#define FIDR0		__REG(0x44000208)  /* DMA Channel 0 Frame ID Register */
#define LDCMD0		__REG(0x4400020C)  /* DMA Channel 0 Command Register */
#define FDADR1		__REG(0x44000210)  /* DMA Channel 1 Frame Descriptor Address Register */
#define FSADR1		__REG(0x44000214)  /* DMA Channel 1 Frame Source Address Register */
#define FIDR1		__REG(0x44000218)  /* DMA Channel 1 Frame ID Register */
#define LDCMD1		__REG(0x4400021C)  /* DMA Channel 1 Command Register */

#define LCCR0_ENB	(1 << 0)	/* LCD Controller enable */
#define LCCR0_CMS	(1 << 1)	/* Color/Monochrome Display Select */
#define LCCR0_Color     (LCCR0_CMS*0)   /*  Color display                  */
#define LCCR0_Mono      (LCCR0_CMS*1)   /*  Monochrome display             */
#define LCCR0_SDS	(1 << 2)	/* Single/Dual Panel Display       */
                                        /* Select                          */
#define LCCR0_Sngl      (LCCR0_SDS*0)   /*  Single panel display           */
#define LCCR0_Dual      (LCCR0_SDS*1)   /*  Dual panel display             */

#define LCCR0_LDM	(1 << 3)	/* LCD Disable Done Mask */
#define LCCR0_SFM	(1 << 4)	/* Start of frame mask */
#define LCCR0_IUM	(1 << 5)	/* Input FIFO underrun mask */
#define LCCR0_EFM	(1 << 6)	/* End of Frame mask */
#define LCCR0_PAS	(1 << 7)	/* Passive/Active display Select   */
#define LCCR0_Pas       (LCCR0_PAS*0)   /*  Passive display (STN)          */
#define LCCR0_Act       (LCCR0_PAS*1)   /*  Active display (TFT)           */
#define LCCR0_DPD	(1 << 9)	/* Double Pixel Data (monochrome   */
                                        /* display mode)                   */
#define LCCR0_4PixMono  (LCCR0_DPD*0)   /*  4-Pixel/clock Monochrome       */
                                        /*  display                        */
#define LCCR0_8PixMono  (LCCR0_DPD*1)   /*  8-Pixel/clock Monochrome       */
                                        /*  display                        */
#define LCCR0_DIS	(1 << 10)	/* LCD Disable */
#define LCCR0_QDM	(1 << 11)	/* LCD Quick Disable mask */
#define LCCR0_PDD	(0xff << 12)	/* Palette DMA request delay */
#define LCCR0_PDD_S	12
#define LCCR0_BM	(1 << 20) 	/* Branch mask */
#define LCCR0_OUM	(1 << 21)	/* Output FIFO underrun mask */
#define LCCR0_LCDT      (1 << 22)       /* LCD panel type */
#define LCCR0_RDSTM     (1 << 23)       /* Read status interrupt mask */
#define LCCR0_CMDIM     (1 << 24)       /* Command interrupt mask */
#define LCCR0_OUC       (1 << 25)       /* Overlay Underlay control bit */
#define LCCR0_LDDALT    (1 << 26)       /* LDD alternate mapping control */

#define LCCR1_PPL       Fld (10, 0)      /* Pixels Per Line - 1 */
#define LCCR1_DisWdth(Pixel)            /* Display Width [1..800 pix.]  */ \
                        (((Pixel) - 1) << FShft (LCCR1_PPL))

#define LCCR1_HSW       Fld (6, 10)     /* Horizontal Synchronization     */
#define LCCR1_HorSnchWdth(Tpix)         /* Horizontal Synchronization     */ \
                                        /* pulse Width [1..64 Tpix]       */ \
                        (((Tpix) - 1) << FShft (LCCR1_HSW))

#define LCCR1_ELW       Fld (8, 16)     /* End-of-Line pixel clock Wait    */
                                        /* count - 1 [Tpix]                */
#define LCCR1_EndLnDel(Tpix)            /*  End-of-Line Delay              */ \
                                        /*  [1..256 Tpix]                  */ \
                        (((Tpix) - 1) << FShft (LCCR1_ELW))

#define LCCR1_BLW       Fld (8, 24)     /* Beginning-of-Line pixel clock   */
                                        /* Wait count - 1 [Tpix]           */
#define LCCR1_BegLnDel(Tpix)            /*  Beginning-of-Line Delay        */ \
                                        /*  [1..256 Tpix]                  */ \
                        (((Tpix) - 1) << FShft (LCCR1_BLW))


#define LCCR2_LPP       Fld (10, 0)     /* Line Per Panel - 1              */
#define LCCR2_DisHght(Line)             /*  Display Height [1..1024 lines] */ \
                        (((Line) - 1) << FShft (LCCR2_LPP))

#define LCCR2_VSW       Fld (6, 10)     /* Vertical Synchronization pulse  */
                                        /* Width - 1 [Tln] (L_FCLK)        */
#define LCCR2_VrtSnchWdth(Tln)          /*  Vertical Synchronization pulse */ \
                                        /*  Width [1..64 Tln]              */ \
                        (((Tln) - 1) << FShft (LCCR2_VSW))

#define LCCR2_EFW       Fld (8, 16)     /* End-of-Frame line clock Wait    */
                                        /* count [Tln]                     */
#define LCCR2_EndFrmDel(Tln)            /*  End-of-Frame Delay             */ \
                                        /*  [0..255 Tln]                   */ \
                        ((Tln) << FShft (LCCR2_EFW))

#define LCCR2_BFW       Fld (8, 24)     /* Beginning-of-Frame line clock   */
                                        /* Wait count [Tln]                */
#define LCCR2_BegFrmDel(Tln)            /*  Beginning-of-Frame Delay       */ \
                                        /*  [0..255 Tln]                   */ \
                        ((Tln) << FShft (LCCR2_BFW))

#if 0
#define LCCR3_PCD	(0xff)		/* Pixel clock divisor */
#define LCCR3_ACB	(0xff << 8)	/* AC Bias pin frequency */
#define LCCR3_ACB_S	8
#endif

#define LCCR3_API	(0xf << 16)	/* AC Bias pin trasitions per interrupt */
#define LCCR3_API_S	16
#define LCCR3_VSP	(1 << 20)	/* vertical sync polarity */
#define LCCR3_HSP	(1 << 21)	/* horizontal sync polarity */
#define LCCR3_PCP	(1 << 22)	/* Pixel Clock Polarity (L_PCLK)   */
#define LCCR3_PixRsEdg  (LCCR3_PCP*0)   /*  Pixel clock Rising-Edge        */
#define LCCR3_PixFlEdg  (LCCR3_PCP*1)   /*  Pixel clock Falling-Edge       */

#define LCCR3_OEP       (1 << 23)       /* Output Enable Polarity (L_BIAS, */
                                        /* active display mode)            */
#define LCCR3_OutEnH    (LCCR3_OEP*0)   /*  Output Enable active High      */
#define LCCR3_OutEnL    (LCCR3_OEP*1)   /*  Output Enable active Low       */

#if 0
#define LCCR3_BPP	(7 << 24)	/* bits per pixel */
#define LCCR3_BPP_S	24
#endif
#define LCCR3_DPC	(1 << 27)	/* double pixel clock mode */


#define LCCR3_PCD       Fld (8, 0)      /* Pixel Clock Divisor */
#define LCCR3_PixClkDiv(Div)            /* Pixel Clock Divisor */ \
                        (((Div) << FShft (LCCR3_PCD)))


#define LCCR3_BPP       Fld (3, 24)     /* Bit Per Pixel */
#define LCCR3_Bpp(Bpp)                  /* Bit Per Pixel */ \
                        (((Bpp) << FShft (LCCR3_BPP)))

#define LCCR3_ACB       Fld (8, 8)      /* AC Bias */
#define LCCR3_Acb(Acb)                  /* BAC Bias */ \
                        (((Acb) << FShft (LCCR3_ACB)))

#define LCCR3_HorSnchH  (LCCR3_HSP*0)   /*  Horizontal Synchronization     */
                                        /*  pulse active High              */
#define LCCR3_HorSnchL  (LCCR3_HSP*1)   /*  Horizontal Synchronization     */

#define LCCR3_VrtSnchH  (LCCR3_VSP*0)   /*  Vertical Synchronization pulse */
                                        /*  active High                    */
#define LCCR3_VrtSnchL  (LCCR3_VSP*1)   /*  Vertical Synchronization pulse */
                                        /*  active Low                     */

#define LCSR_LDD	(1 << 0)	/* LCD Disable Done */
#define LCSR_SOF	(1 << 1)	/* Start of frame */
#define LCSR_BER	(1 << 2)	/* Bus error */
#define LCSR_ABC	(1 << 3)	/* AC Bias count */
#define LCSR_IUL	(1 << 4)	/* input FIFO underrun Lower panel */
#define LCSR_IUU	(1 << 5)	/* input FIFO underrun Upper panel */
#define LCSR_OU		(1 << 6)	/* output FIFO underrun */
#define LCSR_QD		(1 << 7)	/* quick disable */
#define LCSR_EOF	(1 << 8)	/* end of frame */
#define LCSR_BS		(1 << 9)	/* branch status */
#define LCSR_SINT	(1 << 10)	/* subsequent interrupt */

#define LDCMD_PAL	(1 << 26)	/* instructs DMA to load palette buffer */

#define LCSR_LDD	(1 << 0)	/* LCD Disable Done */
#define LCSR_SOF	(1 << 1)	/* Start of frame */
#define LCSR_BER	(1 << 2)	/* Bus error */
#define LCSR_ABC	(1 << 3)	/* AC Bias count */
#define LCSR_IUL	(1 << 4)	/* input FIFO underrun Lower panel */
#define LCSR_IUU	(1 << 5)	/* input FIFO underrun Upper panel */
#define LCSR_OU		(1 << 6)	/* output FIFO underrun */
#define LCSR_QD		(1 << 7)	/* quick disable */
#define LCSR_EOF	(1 << 8)	/* end of frame */
#define LCSR_BS		(1 << 9)	/* branch status */
#define LCSR_SINT	(1 << 10)	/* subsequent interrupt */

#define LDCMD_PAL	(1 << 26)	/* instructs DMA to load palette buffer */

/*
 * Memory controller
 */

#define MDCNFG		__REG(0x48000000)  /* SDRAM Configuration Register 0 */
#define MDREFR		__REG(0x48000004)  /* SDRAM Refresh Control Register */
#define MSC0		__REG(0x48000008)  /* Static Memory Control Register 0 */
#define MSC1		__REG(0x4800000C)  /* Static Memory Control Register 1 */
#define MSC2		__REG(0x48000010)  /* Static Memory Control Register 2 */
#define MECR		__REG(0x48000014)  /* Expansion Memory (PCMCIA/Compact Flash) Bus Configuration */
#define SXLCR		__REG(0x48000018)  /* LCR value to be written to SDRAM-Timing Synchronous Flash */
#define SXCNFG		__REG(0x4800001C)  /* Synchronous Static Memory Control Register */
#define SXMRS		__REG(0x48000024)  /* MRS value to be written to Synchronous Flash or SMROM */
#define MCMEM0		__REG(0x48000028)  /* Card interface Common Memory Space Socket 0 Timing */
#define MCMEM1		__REG(0x4800002C)  /* Card interface Common Memory Space Socket 1 Timing */
#define MCATT0		__REG(0x48000030)  /* Card interface Attribute Space Socket 0 Timing Configuration */
#define MCATT1		__REG(0x48000034)  /* Card interface Attribute Space Socket 1 Timing Configuration */
#define MCIO0		__REG(0x48000038)  /* Card interface I/O Space Socket 0 Timing Configuration */
#define MCIO1		__REG(0x4800003C)  /* Card interface I/O Space Socket 1 Timing Configuration */
#define MDMRS		__REG(0x48000040)  /* MRS value to be written to SDRAM */
#define BOOT_DEF	__REG(0x48000044)  /* Read-Only Boot-Time Register. Contains BOOT_SEL and PKG_SEL */

/*
 * More handy macros for PCMCIA
 *
 * Arg is socket number
 */
#define MCMEM(s)	__REG2(0x48000028, (s)<<2 )  /* Card interface Common Memory Space Socket s Timing */
#define MCATT(s)	__REG2(0x48000030, (s)<<2 )  /* Card interface Attribute Space Socket s Timing Configuration */
#define MCIO(s)		__REG2(0x48000038, (s)<<2 )  /* Card interface I/O Space Socket s Timing Configuration */

/* MECR register defines */
#define MECR_NOS	(1 << 0)	/* Number Of Sockets: 0 -> 1 sock, 1 -> 2 sock */
#define MECR_CIT	(1 << 1)	/* Card Is There: 0 -> no card, 1 -> card inserted */

#define MDREFR_K0DB4	(1 << 29)	/* SDCLK0 Divide by 4 Control/Status */
#define MDREFR_K2FREE	(1 << 25)	/* SDRAM Free-Running Control */
#define MDREFR_K1FREE	(1 << 24)	/* SDRAM Free-Running Control */
#define MDREFR_K0FREE	(1 << 23)	/* SDRAM Free-Running Control */
#define MDREFR_SLFRSH	(1 << 22)	/* SDRAM Self-Refresh Control/Status */
#define MDREFR_APD	(1 << 20)	/* SDRAM/SSRAM Auto-Power-Down Enable */
#define MDREFR_K2DB2	(1 << 19)	/* SDCLK2 Divide by 2 Control/Status */
#define MDREFR_K2RUN	(1 << 18)	/* SDCLK2 Run Control/Status */
#define MDREFR_K1DB2	(1 << 17)	/* SDCLK1 Divide by 2 Control/Status */
#define MDREFR_K1RUN	(1 << 16)	/* SDCLK1 Run Control/Status */
#define MDREFR_E1PIN	(1 << 15)	/* SDCKE1 Level Control/Status */
#define MDREFR_K0DB2	(1 << 14)	/* SDCLK0 Divide by 2 Control/Status */
#define MDREFR_K0RUN	(1 << 13)	/* SDCLK0 Run Control/Status */
#define MDREFR_E0PIN	(1 << 12)	/* SDCKE0 Level Control/Status */


#ifdef CONFIG_PXA27x

/*
 * Keypad
 */
#define KPC             __REG(0x41500000) /* Keypad Interface Control register */
#define KPDK            __REG(0x41500008) /* Keypad Interface Direct Key register */
#define KPREC           __REG(0x41500010) /* Keypad Interface Rotary Encoder register */
#define KPMK            __REG(0x41500018) /* Keypad Interface Matrix Key register */
#define KPAS            __REG(0x41500020) /* Keypad Interface Automatic Scan register */
#define KPASMKP0        __REG(0x41500028) /* Keypad Interface Automatic Scan Multiple Key Presser register 0 */
#define KPASMKP1        __REG(0x41500030) /* Keypad Interface Automatic Scan Multiple Key Presser register 1 */
#define KPASMKP2        __REG(0x41500038) /* Keypad Interface Automatic Scan Multiple Key Presser register 2 */
#define KPASMKP3        __REG(0x41500040) /* Keypad Interface Automatic Scan Multiple Key Presser register 3 */
#define KPKDI           __REG(0x41500048) /* Keypad Interface Key Debounce Interval register */

#define KPC_AS          (0x1 << 30)  /* Automatic Scan bit */
#define KPC_ASACT       (0x1 << 29)  /* Automatic Scan on Activity */
#define KPC_MI          (0x1 << 22)  /* Matrix interrupt bit */
#define KPC_IMKP        (0x1 << 21)  /* Ignore Multiple Key Press */
#define KPC_MS7         (0x1 << 20)  /* Matrix scan line 7 */
#define KPC_MS6         (0x1 << 19)  /* Matrix scan line 6 */
#define KPC_MS5         (0x1 << 18)  /* Matrix scan line 5 */
#define KPC_MS4         (0x1 << 17)  /* Matrix scan line 4 */
#define KPC_MS3         (0x1 << 16)  /* Matrix scan line 3 */
#define KPC_MS2         (0x1 << 15)  /* Matrix scan line 2 */
#define KPC_MS1         (0x1 << 14)  /* Matrix scan line 1 */
#define KPC_MS0         (0x1 << 13)  /* Matrix scan line 0 */
#define KPC_MS_ALL      (KPC_MS0 | KPC_MS1 | KPC_MS2 | KPC_MS3 | KPC_MS4 | KPC_MS5 | KPC_MS6 | KPC_MS7)
#define KPC_ME          (0x1 << 12)  /* Matrix Keypad Enable */
#define KPC_MIE         (0x1 << 11)  /* Matrix Interrupt Enable */
#define KPC_DK_DEB_SEL	(0x1 <<  9)  /* Direct Keypad Debounce Select */
#define KPC_DI          (0x1 <<  5)  /* Direct key interrupt bit */
#define KPC_RE_ZERO_DEB (0x1 <<  4)  /* Rotary Encoder Zero Debounce */
#define KPC_REE1        (0x1 <<  3)  /* Rotary Encoder1 Enable */
#define KPC_REE0        (0x1 <<  2)  /* Rotary Encoder0 Enable */
#define KPC_DE          (0x1 <<  1)  /* Direct Keypad Enable */
#define KPC_DIE         (0x1 <<  0)  /* Direct Keypad interrupt Enable */

#define KPDK_DKP        (0x1 << 31)
#define KPDK_DK7        (0x1 <<  7)
#define KPDK_DK6        (0x1 <<  6)
#define KPDK_DK5        (0x1 <<  5)
#define KPDK_DK4        (0x1 <<  4)
#define KPDK_DK3        (0x1 <<  3)
#define KPDK_DK2        (0x1 <<  2)
#define KPDK_DK1        (0x1 <<  1)
#define KPDK_DK0        (0x1 <<  0)

#define KPREC_OF1       (0x1 << 31)
#define kPREC_UF1       (0x1 << 30)
#define KPREC_OF0       (0x1 << 15)
#define KPREC_UF0       (0x1 << 14)

#define KPMK_MKP        (0x1 << 31)
#define KPAS_SO         (0x1 << 31)
#define KPASMKPx_SO     (0x1 << 31)

/*
 * UHC: USB Host Controller (OHCI-like) register definitions
 */
#define UHC_BASE_PHYS	(0x4C000000)
#define UHCREV		__REG(0x4C000000) /* UHC HCI Spec Revision */
#define UHCHCON		__REG(0x4C000004) /* UHC Host Control Register */
#define UHCCOMS		__REG(0x4C000008) /* UHC Command Status Register */
#define UHCINTS		__REG(0x4C00000C) /* UHC Interrupt Status Register */
#define UHCINTE		__REG(0x4C000010) /* UHC Interrupt Enable */
#define UHCINTD		__REG(0x4C000014) /* UHC Interrupt Disable */
#define UHCHCCA		__REG(0x4C000018) /* UHC Host Controller Comm. Area */
#define UHCPCED		__REG(0x4C00001C) /* UHC Period Current Endpt Descr */
#define UHCCHED		__REG(0x4C000020) /* UHC Control Head Endpt Descr */
#define UHCCCED		__REG(0x4C000024) /* UHC Control Current Endpt Descr */
#define UHCBHED		__REG(0x4C000028) /* UHC Bulk Head Endpt Descr */
#define UHCBCED		__REG(0x4C00002C) /* UHC Bulk Current Endpt Descr */
#define UHCDHEAD	__REG(0x4C000030) /* UHC Done Head */
#define UHCFMI		__REG(0x4C000034) /* UHC Frame Interval */
#define UHCFMR		__REG(0x4C000038) /* UHC Frame Remaining */
#define UHCFMN		__REG(0x4C00003C) /* UHC Frame Number */
#define UHCPERS		__REG(0x4C000040) /* UHC Periodic Start */
#define UHCLS		__REG(0x4C000044) /* UHC Low Speed Threshold */

#define UHCRHDA		__REG(0x4C000048) /* UHC Root Hub Descriptor A */
#define UHCRHDA_NOCP	(1 << 12)	/* No over current protection */

#define UHCRHDB		__REG(0x4C00004C) /* UHC Root Hub Descriptor B */
#define UHCRHS		__REG(0x4C000050) /* UHC Root Hub Status */
#define UHCRHPS1	__REG(0x4C000054) /* UHC Root Hub Port 1 Status */
#define UHCRHPS2	__REG(0x4C000058) /* UHC Root Hub Port 2 Status */
#define UHCRHPS3	__REG(0x4C00005C) /* UHC Root Hub Port 3 Status */

#define UHCSTAT		__REG(0x4C000060) /* UHC Status Register */
#define UHCSTAT_UPS3	(1 << 16)	/* USB Power Sense Port3 */
#define UHCSTAT_SBMAI	(1 << 15)	/* System Bus Master Abort Interrupt*/
#define UHCSTAT_SBTAI	(1 << 14)	/* System Bus Target Abort Interrupt*/
#define UHCSTAT_UPRI	(1 << 13)	/* USB Port Resume Interrupt */
#define UHCSTAT_UPS2	(1 << 12)	/* USB Power Sense Port 2 */
#define UHCSTAT_UPS1	(1 << 11)	/* USB Power Sense Port 1 */
#define UHCSTAT_HTA	(1 << 10)	/* HCI Target Abort */
#define UHCSTAT_HBA	(1 << 8)	/* HCI Buffer Active */
#define UHCSTAT_RWUE	(1 << 7)	/* HCI Remote Wake Up Event */

#define UHCHR           __REG(0x4C000064) /* UHC Reset Register */
#define UHCHR_SSEP3	(1 << 11)	/* Sleep Standby Enable for Port3 */
#define UHCHR_SSEP2	(1 << 10)	/* Sleep Standby Enable for Port2 */
#define UHCHR_SSEP1	(1 << 9)	/* Sleep Standby Enable for Port1 */
#define UHCHR_PCPL	(1 << 7)	/* Power control polarity low */
#define UHCHR_PSPL	(1 << 6)	/* Power sense polarity low */
#define UHCHR_SSE	(1 << 5)	/* Sleep Standby Enable */
#define UHCHR_UIT	(1 << 4)	/* USB Interrupt Test */
#define UHCHR_SSDC	(1 << 3)	/* Simulation Scale Down Clock */
#define UHCHR_CGR	(1 << 2)	/* Clock Generation Reset */
#define UHCHR_FHR	(1 << 1)	/* Force Host Controller Reset */
#define UHCHR_FSBIR	(1 << 0)	/* Force System Bus Iface Reset */

#define UHCHIE          __REG(0x4C000068) /* UHC Interrupt Enable Register*/
#define UHCHIE_UPS3IE	(1 << 14)	/* Power Sense Port3 IntEn */
#define UHCHIE_UPRIE	(1 << 13)	/* Port Resume IntEn */
#define UHCHIE_UPS2IE	(1 << 12)	/* Power Sense Port2 IntEn */
#define UHCHIE_UPS1IE	(1 << 11)	/* Power Sense Port1 IntEn */
#define UHCHIE_TAIE	(1 << 10)	/* HCI Interface Transfer Abort
					   Interrupt Enable*/
#define UHCHIE_HBAIE	(1 << 8)	/* HCI Buffer Active IntEn */
#define UHCHIE_RWIE	(1 << 7)	/* Remote Wake-up IntEn */

#define UHCHIT          __REG(0x4C00006C) /* UHC Interrupt Test register */

/* Camera Interface */
#define CICR0		__REG(0x50000000)
#define CICR1		__REG(0x50000004)
#define CICR2		__REG(0x50000008)
#define CICR3		__REG(0x5000000C)
#define CICR4		__REG(0x50000010)
#define CISR		__REG(0x50000014)
#define CIFR		__REG(0x50000018)
#define CITOR		__REG(0x5000001C)
#define CIBR0		__REG(0x50000028)
#define CIBR1		__REG(0x50000030)
#define CIBR2		__REG(0x50000038)

#define CICR0_DMAEN	(1 << 31)	/* DMA request enable */
#define CICR0_PAR_EN	(1 << 30)	/* Parity enable */
#define CICR0_SL_CAP_EN	(1 << 29)	/* Capture enable for slave mode */
#define CICR0_ENB	(1 << 28)	/* Camera interface enable */
#define CICR0_DIS	(1 << 27)	/* Camera interface disable */
#define CICR0_SIM	(0x7 << 24)	/* Sensor interface mode mask */
#define CICR0_TOM	(1 << 9)	/* Time-out mask */
#define CICR0_RDAVM	(1 << 8)	/* Receive-data-available mask */
#define CICR0_FEM	(1 << 7)	/* FIFO-empty mask */
#define CICR0_EOLM	(1 << 6)	/* End-of-line mask */
#define CICR0_PERRM	(1 << 5)	/* Parity-error mask */
#define CICR0_QDM	(1 << 4)	/* Quick-disable mask */
#define CICR0_CDM	(1 << 3)	/* Disable-done mask */
#define CICR0_SOFM	(1 << 2)	/* Start-of-frame mask */
#define CICR0_EOFM	(1 << 1)	/* End-of-frame mask */
#define CICR0_FOM	(1 << 0)	/* FIFO-overrun mask */

#define CICR1_TBIT	(1 << 31)	/* Transparency bit */
#define CICR1_RGBT_CONV	(0x3 << 30)	/* RGBT conversion mask */
#define CICR1_PPL	(0x3f << 15)	/* Pixels per line mask */
#define CICR1_RGB_CONV	(0x7 << 12)	/* RGB conversion mask */
#define CICR1_RGB_F	(1 << 11)	/* RGB format */
#define CICR1_YCBCR_F	(1 << 10)	/* YCbCr format */
#define CICR1_RGB_BPP	(0x7 << 7)	/* RGB bis per pixel mask */
#define CICR1_RAW_BPP	(0x3 << 5)	/* Raw bis per pixel mask */
#define CICR1_COLOR_SP	(0x3 << 3)	/* Color space mask */
#define CICR1_DW	(0x7 << 0)	/* Data width mask */

#define CICR2_BLW	(0xff << 24)	/* Beginning-of-line pixel clock
					   wait count mask */
#define CICR2_ELW	(0xff << 16)	/* End-of-line pixel clock
					   wait count mask */
#define CICR2_HSW	(0x3f << 10)	/* Horizontal sync pulse width mask */
#define CICR2_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR2_FSW	(0x7 << 0)	/* Frame stabilization
					   wait count mask */

#define CICR3_BFW	(0xff << 24)	/* Beginning-of-frame line clock
					   wait count mask */
#define CICR3_EFW	(0xff << 16)	/* End-of-frame line clock
					   wait count mask */
#define CICR3_VSW	(0x3f << 10)	/* Vertical sync pulse width mask */
#define CICR3_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR3_LPF	(0x3ff << 0)	/* Lines per frame mask */

#define CICR4_MCLK_DLY	(0x3 << 24)	/* MCLK Data Capture Delay mask */
#define CICR4_PCLK_EN	(1 << 23)	/* Pixel clock enable */
#define CICR4_PCP	(1 << 22)	/* Pixel clock polarity */
#define CICR4_HSP	(1 << 21)	/* Horizontal sync polarity */
#define CICR4_VSP	(1 << 20)	/* Vertical sync polarity */
#define CICR4_MCLK_EN	(1 << 19)	/* MCLK enable */
#define CICR4_FR_RATE	(0x7 << 8)	/* Frame rate mask */
#define CICR4_DIV	(0xff << 0)	/* Clock divisor mask */

#define CISR_FTO	(1 << 15)	/* FIFO time-out */
#define CISR_RDAV_2	(1 << 14)	/* Channel 2 receive data available */
#define CISR_RDAV_1	(1 << 13)	/* Channel 1 receive data available */
#define CISR_RDAV_0	(1 << 12)	/* Channel 0 receive data available */
#define CISR_FEMPTY_2	(1 << 11)	/* Channel 2 FIFO empty */
#define CISR_FEMPTY_1	(1 << 10)	/* Channel 1 FIFO empty */
#define CISR_FEMPTY_0	(1 << 9)	/* Channel 0 FIFO empty */
#define CISR_EOL	(1 << 8)	/* End of line */
#define CISR_PAR_ERR	(1 << 7)	/* Parity error */
#define CISR_CQD	(1 << 6)	/* Camera interface quick disable */
#define CISR_SOF	(1 << 5)	/* Start of frame */
#define CISR_CDD	(1 << 4)	/* Camera interface disable done */
#define CISR_EOF	(1 << 3)	/* End of frame */
#define CISR_IFO_2	(1 << 2)	/* FIFO overrun for Channel 2 */
#define CISR_IFO_1	(1 << 1)	/* FIFO overrun for Channel 1 */
#define CISR_IFO_0	(1 << 0)	/* FIFO overrun for Channel 0 */

#define CIFR_FLVL2	(0x7f << 23)	/* FIFO 2 level mask */
#define CIFR_FLVL1	(0x7f << 16)	/* FIFO 1 level mask */
#define CIFR_FLVL0	(0xff << 8)	/* FIFO 0 level mask */
#define CIFR_THL_0	(0x3 << 4)	/* Threshold Level for Channel 0 FIFO */
#define CIFR_RESET_F	(1 << 3)	/* Reset input FIFOs */
#define CIFR_FEN2	(1 << 2)	/* FIFO enable for channel 2 */
#define CIFR_FEN1	(1 << 1)	/* FIFO enable for channel 1 */
#define CIFR_FEN0	(1 << 0)	/* FIFO enable for channel 0 */

#define SRAM_SIZE		0x40000 /* 4x64K  */

#define SRAM_MEM_PHYS		0x5C000000

#define IMPMCR		__REG(0x58000000) /* IM Power Management Control Reg */
#define IMPMSR		__REG(0x58000008) /* IM Power Management Status Reg */

#define IMPMCR_PC3		(0x3 << 22) /* Bank 3 Power Control */
#define IMPMCR_PC3_RUN_MODE	(0x0 << 22) /*   Run mode */
#define IMPMCR_PC3_STANDBY_MODE	(0x1 << 22) /*   Standby mode */
#define IMPMCR_PC3_AUTO_MODE	(0x3 << 22) /*   Automatically controlled */

#define IMPMCR_PC2		(0x3 << 20) /* Bank 2 Power Control */
#define IMPMCR_PC2_RUN_MODE	(0x0 << 20) /*   Run mode */
#define IMPMCR_PC2_STANDBY_MODE	(0x1 << 20) /*   Standby mode */
#define IMPMCR_PC2_AUTO_MODE	(0x3 << 20) /*   Automatically controlled */

#define IMPMCR_PC1		(0x3 << 18) /* Bank 1 Power Control */
#define IMPMCR_PC1_RUN_MODE	(0x0 << 18) /*   Run mode */
#define IMPMCR_PC1_STANDBY_MODE	(0x1 << 18) /*   Standby mode */
#define IMPMCR_PC1_AUTO_MODE	(0x3 << 18) /*   Automatically controlled */

#define IMPMCR_PC0		(0x3 << 16) /* Bank 0 Power Control */
#define IMPMCR_PC0_RUN_MODE	(0x0 << 16) /*   Run mode */
#define IMPMCR_PC0_STANDBY_MODE	(0x1 << 16) /*   Standby mode */
#define IMPMCR_PC0_AUTO_MODE	(0x3 << 16) /*   Automatically controlled */

#define IMPMCR_AW3		(1 << 11) /* Bank 3 Automatic Wake-up enable */
#define IMPMCR_AW2		(1 << 10) /* Bank 2 Automatic Wake-up enable */
#define IMPMCR_AW1		(1 << 9)  /* Bank 1 Automatic Wake-up enable */
#define IMPMCR_AW0		(1 << 8)  /* Bank 0 Automatic Wake-up enable */

#define IMPMCR_DST		(0xFF << 0) /* Delay Standby Time, ms */

#define IMPMSR_PS3		(0x3 << 6) /* Bank 3 Power Status: */
#define IMPMSR_PS3_RUN_MODE	(0x0 << 6) /*    Run mode */
#define IMPMSR_PS3_STANDBY_MODE	(0x1 << 6) /*    Standby mode */

#define IMPMSR_PS2		(0x3 << 4) /* Bank 2 Power Status: */
#define IMPMSR_PS2_RUN_MODE	(0x0 << 4) /*    Run mode */
#define IMPMSR_PS2_STANDBY_MODE	(0x1 << 4) /*    Standby mode */

#define IMPMSR_PS1		(0x3 << 2) /* Bank 1 Power Status: */
#define IMPMSR_PS1_RUN_MODE	(0x0 << 2) /*    Run mode */
#define IMPMSR_PS1_STANDBY_MODE	(0x1 << 2) /*    Standby mode */

#define IMPMSR_PS0		(0x3 << 0) /* Bank 0 Power Status: */
#define IMPMSR_PS0_RUN_MODE	(0x0 << 0) /*    Run mode */
#define IMPMSR_PS0_STANDBY_MODE	(0x1 << 0) /*    Standby mode */

#endif

/* PWRMODE register M field values */

#define PWRMODE_IDLE		0x1
#define PWRMODE_STANDBY		0x2
#define PWRMODE_SLEEP		0x3
#define PWRMODE_DEEPSLEEP	0x7

#endif
