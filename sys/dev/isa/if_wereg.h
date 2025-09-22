/*	$OpenBSD: if_wereg.h,v 1.4 2022/01/09 05:42:44 jsg Exp $	*/
/*	$NetBSD: if_wereg.h,v 1.1 1997/11/03 21:22:50 thorpej Exp $	*/

/*
 * National Semiconductor DS8390 NIC register definitions.
 *
 * Copyright (C) 1993, David Greenman.  This software may be used, modified,
 * copied, distributed, and sold, in both source and binary form provided that
 * the above copyright and these terms are retained.  Under no circumstances is
 * the author responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its use.
 */

/*
 * Compile-time config flags
 */
/*
 * This sets the default for enabling/disabling the transceiver.
 */
#define WE_FLAGS_DISABLE_TRANSCEIVER	0x0001

/*
 * This forces the board to be used in 8/16-bit mode even if it autoconfigs
 * differently.
 */
#define WE_FLAGS_FORCE_8BIT_MODE	0x0002
#define WE_FLAGS_FORCE_16BIT_MODE	0x0004

/*
 * This disables the use of double transmit buffers.
 */
#define WE_FLAGS_NO_MULTI_BUFFERING	0x0008

/*
 *		Definitions for Western digital/SMC WD80x3 series ASIC
 */

/*
 * Memory Select Register (MSR)
 */
#define WE_MSR	0

/* next three definitions for Toshiba */
#define	WE_MSR_POW	0x02	/* 0 = power save, 1 = normal (R/W) */
#define	WE_MSR_BSY	0x04	/* gate array busy (R) */
#define	WE_MSR_LEN	0x20	/* 0 = 16-bit, 1 = 8-bit (R/W) */

#define WE_MSR_ADDR	0x3f	/* Memory decode bits 18-13 */
#define WE_MSR_MENB	0x40	/* Memory enable */
#define WE_MSR_RST	0x80	/* Reset board */

/*
 * Interface Configuration Register (ICR)
 */
#define WE_ICR	1

#define WE_ICR_16BIT	0x01	/* 16-bit interface */
#define WE_ICR_OAR	0x02	/* select register (0=BIO 1=EAR) */
#define WE_ICR_IR2	0x04	/* high order bit of encoded IRQ */
#define WE_ICR_MSZ	0x08	/* memory size (0=8k 1=32k) */
#define WE_ICR_RLA	0x10	/* recall LAN address */
#define WE_ICR_RX7	0x20	/* recall all but i/o and LAN address */
#define	WE_ICR_RIO	0x40	/* recall i/o address */
#define WE_ICR_STO	0x80	/* store to non-volatile memory */
#ifdef TOSH_ETHER
#define	WE_ICR_MEM	0xe0	/* shared mem address A15-A13 (R/W) */
#define	WE_ICR_MSZ1	0x0f	/* memory size, 0x08 = 64K, 0x04 = 32K,
				   0x02 = 16K, 0x01 = 8K */
				/* 64K can only be used if mem address
				   above 1MB */
				/* IAR holds address A23-A16 (R/W) */
#endif

/*
 * IO Address Register (IAR)
 */
#define WE_IAR	2

/*
 * EEROM Address Register
 */
#define WE_EAR	3

/*
 * Interrupt Request Register (IRR)
 */
#define WE_IRR	4

#define	WE_IRR_0WS	0x01	/* use 0 wait-states on 8 bit bus */
#define WE_IRR_OUT1	0x02	/* WD83C584 pin 1 output */
#define WE_IRR_OUT2	0x04	/* WD83C584 pin 2 output */
#define WE_IRR_OUT3	0x08	/* WD83C584 pin 3 output */
#define WE_IRR_FLASH	0x10	/* Flash RAM is in the ROM socket */

/*
 * The three bits of the encoded IRQ are decoded as follows:
 *
 * IR2 IR1 IR0  IRQ
 *  0   0   0   2/9
 *  0   0   1   3
 *  0   1   0   5
 *  0   1   1   7
 *  1   0   0   10
 *  1   0   1   11
 *  1   1   0   15
 *  1   1   1   4
 */
#define WE_IRR_IR0	0x20	/* bit 0 of encoded IRQ */
#define WE_IRR_IR1	0x40	/* bit 1 of encoded IRQ */
#define WE_IRR_IEN	0x80	/* Interrupt enable */

/*
 * LA Address Register (LAAR)
 */
#define WE_LAAR	5

#define WE_LAAR_ADDRHI	0x1f	/* bits 23-19 of RAM address */
#define WE_LAAR_0WS16	0x20	/* enable 0 wait-states on 16 bit bus */
#define WE_LAAR_L16EN	0x40	/* enable 16-bit operation */
#define WE_LAAR_M16EN	0x80	/* enable 16-bit memory access */

/* i/o base offset to station address/card-ID PROM */
#define WE_PROM	8

/*
 *	83C790 specific registers
 */
/*
 * Hardware Support Register (HWR) ('790)
 */
#define	WE790_HWR	4

#define	WE790_HWR_RST	0x10	/* hardware reset */
#define	WE790_HWR_LPRM	0x40	/* LAN PROM select */
#define	WE790_HWR_SWH	0x80	/* switch register set */

/*
 * ICR790 Interrupt Control Register for the 83C790
 */
#define	WE790_ICR	6

#define	WE790_ICR_EIL	0x01	/* enable interrupts */

/*
 * REV/IOPA Revision / I/O Pipe register for the 83C79X
 */
#define WE790_REV	7

#define WE790_REV_790	0x20
#define WE790_REV_795	0x40

/*
 * 79X RAM Address Register (RAR)
 *      Enabled with SWH bit=1 in HWR register
 */

#define WE790_RAR	0x0b

#define WE790_RAR_SZ8	0x00	/* 8k memory buffer */
#define WE790_RAR_SZ16	0x10	/* 16k memory buffer */
#define WE790_RAR_SZ32	0x20	/* 32k memory buffer */
#define WE790_RAR_SZ64	0x30	/* 64k memory buffer */

/*
 * General Control Register (GCR)
 * Enabled with SWH bit == 1 in HWR register
 */
#define	WE790_GCR	0x0d

#define	WE790_GCR_LIT	0x01	/* on for UTP */
#define	WE790_GCR_GPOUT	0x02	/* if BNC is enabled */
#define	WE790_GCR_IR0	0x04	/* bit 0 of encoded IRQ */
#define	WE790_GCR_IR1	0x08	/* bit 1 of encoded IRQ */
#define	WE790_GCR_ZWSEN	0x20	/* zero wait state enable */
#define	WE790_GCR_IR2	0x40	/* bit 2 of encoded IRQ */
/*
 * The three bits of the encoded IRQ are decoded as follows:
 *
 * IR2 IR1 IR0  IRQ
 *  0   0   0   none
 *  0   0   1   9
 *  0   1   0   3
 *  0   1   1   5
 *  1   0   0   7
 *  1   0   1   10
 *  1   1   0   11
 *  1   1   1   15
 */

/* i/o base offset to CARD ID */
#define WE_CARD_ID	WE_PROM+6

/* Board type codes in card ID */
#define WE_TYPE_WD8003S		0x02
#define WE_TYPE_WD8003E		0x03
#define WE_TYPE_WD8013EBT	0x05
#define	WE_TYPE_TOSHIBA1	0x11	/* named PCETA1 */
#define	WE_TYPE_TOSHIBA2	0x12	/* named PCETA2 */
#define	WE_TYPE_TOSHIBA3	0x13	/* named PCETB */
#define	WE_TYPE_TOSHIBA4	0x14	/* named PCETC */
#define	WE_TYPE_WD8003W		0x24
#define	WE_TYPE_WD8003EB	0x25
#define	WE_TYPE_WD8013W		0x26
#define WE_TYPE_WD8013EP	0x27
#define WE_TYPE_WD8013WC	0x28
#define WE_TYPE_WD8013EPC	0x29
#define	WE_TYPE_SMC8216T	0x2a
#define	WE_TYPE_SMC8216C	0x2b
#define WE_TYPE_WD8013EBP	0x2c

/* Bit definitions in card ID */
#define	WE_REV_MASK		0x1f	/* Revision mask */
#define	WE_SOFTCONFIG		0x20	/* Soft config */
#define	WE_LARGERAM		0x40	/* Large RAM */
#define	WE_MICROCHANEL		0x80	/* Microchannel bus (vs. isa) */

/*
 * Checksum total.  All 8 bytes in station address PROM will add up to this.
 */
#ifdef TOSH_ETHER
#define WE_ROM_CHECKSUM_TOTAL	0xA5
#else
#define WE_ROM_CHECKSUM_TOTAL	0xFF
#endif

#define WE_NIC_OFFSET		0x10	/* I/O base offset to NIC */
#define WE_ASIC_OFFSET		0	/* I/O base offset to ASIC */
#define	WE_NIC_NPORTS		16
#define	WE_ASIC_NPORTS		16
#define WE_NPORTS		(WE_NIC_NPORTS + WE_ASIC_NPORTS)

#define WE_PAGE_OFFSET	0	/* page offset for NIC access to mem */
