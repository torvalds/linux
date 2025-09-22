/*	$OpenBSD: if_ecreg.h,v 1.2 2001/01/25 03:50:50 todd Exp $	*/
/*	$NetBSD: if_ecreg.h,v 1.1 1997/11/02 00:44:26 thorpej Exp $	*/

/*
 * 3Com Etherlink II (3c503) register definitions.
 *
 * Copyright (C) 1993, David Greenman.  This software may be used, modified,
 * copied, distributed, and sold, in both source and binary form provided that
 * the above copyright and these terms are retained.  Under no circumstances is
 * the author responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its use.
 */

#ifndef _DEV_ISA_IF_ECREG_H_
#define	_DEV_ISA_IF_ECREG_H_

#define ELINK2_NIC_OFFSET	0
#define ELINK2_ASIC_OFFSET	0x400	/* offset to nic i/o regs */

/*
 * XXX - The I/O address range is fragmented in the 3c503; this is the
 *	number of regs at iobase.
 */
#define ELINK2_NIC_PORTS	16
#define	ELINK2_ASIC_PORTS	16

/* tx memory starts in second bank on 8bit cards */
#define ELINK2_TX_PAGE_OFFSET_8BIT	0x20

/* tx memory starts in first bank on 16bit cards */
#define ELINK2_TX_PAGE_OFFSET_16BIT	0x0

/* ...and rx memory starts in second bank */
#define ELINK2_RX_PAGE_OFFSET_16BIT	0x20


/*
 * Page Start Register.  Must match PSTART in NIC.
 */
#define ELINK2_PSTR		0

/*
 * Page Stop Register.  Must match PSTOP in NIC.
 */
#define ELINK2_PSPR		1

/*
 * DrQ Timer Register.  Determines number of bytes to be transferred during a
 * DMA burst.
 */
#define ELINK2_DQTR		2

/*
 * Base Configuration Register.  Read-only register which contains the
 * board-configured I/O base address of the adapter.  Bit encoded.
 */
#define ELINK2_BCFR		3

/*
 * EPROM Configuration Register.  Read-only register which contains the
 * board-configured memory base address.  Bit encoded.
 */
#define ELINK2_PCFR		4

/*
 * GA Configuration Register.  Gate-Array Configuration Register.
 *
 * mbs2  mbs1  mbs0	start address
 *  0     0     0	0x0000
 *  0     0     1	0x2000
 *  0     1     0	0x4000
 *  0     1     1	0x6000
 *
 * Note that with adapters with only 8K, the setting for 0x2000 must always be
 * used.
 */
#define ELINK2_GACFR		5

#define ELINK2_GACFR_MBS0	0x01
#define ELINK2_GACFR_MBS1	0x02
#define ELINK2_GACFR_MBS2	0x04

#define ELINK2_GACFR_RSEL	0x08	/* enable shared memory */
#define ELINK2_GACFR_TEST	0x10	/* for GA testing */
#define ELINK2_GACFR_OWS	0x20	/* select 0WS access to GA */
#define ELINK2_GACFR_TCM	0x40	/* Mask DMA interrupts */
#define ELINK2_GACFR_NIM	0x80	/* Mask NIC interrupts */

/*
 * Control Register.  Miscellaneous control functions.
 */
#define ELINK2_CR		6

#define ELINK2_CR_RST		0x01	/* Reset GA and NIC */
#define ELINK2_CR_XSEL		0x02	/* Transceiver select.  BNC=1(def) AUI=0 */
#define ELINK2_CR_EALO		0x04	/* window EA PROM 0-15 to I/O base */
#define ELINK2_CR_EAHI		0x08	/* window EA PROM 16-31 to I/O base */
#define ELINK2_CR_SHARE		0x10	/* select interrupt sharing option */
#define ELINK2_CR_DBSEL		0x20	/* Double buffer select */
#define ELINK2_CR_DDIR		0x40	/* DMA direction select */
#define ELINK2_CR_START		0x80	/* Start DMA controller */

/*
 * Status Register.  Miscellaneous status information.
 */
#define ELINK2_STREG		7

#define ELINK2_STREG_REV	0x07	/* GA revision */
#define ELINK2_STREG_DIP	0x08	/* DMA in progress */
#define ELINK2_STREG_DTC	0x10	/* DMA terminal count */
#define ELINK2_STREG_OFLW	0x20	/* Overflow */
#define ELINK2_STREG_UFLW	0x40	/* Underflow */
#define ELINK2_STREG_DPRDY	0x80	/* Data port ready */

/*
 * Interrupt/DMA Configuration Register
 */
#define ELINK2_IDCFR		8

#define ELINK2_IDCFR_DRQ	0x07	/* DMA request */
#define ELINK2_IDCFR_UNUSED	0x08	/* not used */
#if 0
#define ELINK2_IDCFR_IRQ	0xF0	/* Interrupt request */
#else
#define ELINK2_IDCFR_IRQ2	0x10	/* Interrupt request 2 select */
#define ELINK2_IDCFR_IRQ3	0x20	/* Interrupt request 3 select */
#define ELINK2_IDCFR_IRQ4	0x40	/* Interrupt request 4 select */
#define ELINK2_IDCFR_IRQ5	0x80	/* Interrupt request 5 select */
#endif

/*
 * DMA Address Register MSB
 */
#define ELINK2_DAMSB		9

/*
 * DMA Address Register LSB
 */
#define ELINK2_DALSB		0x0a

/*
 * Vector Pointer Register 2
 */
#define ELINK2_VPTR2		0x0b

/*
 * Vector Pointer Register 1
 */
#define ELINK2_VPTR1		0x0c

/*
 * Vector Pointer Register 0
 */
#define ELINK2_VPTR0		0x0d

/*
 * Register File Access MSB
 */
#define ELINK2_RFMSB		0x0e

/*
 * Register File Access LSB
 */
#define ELINK2_RFLSB		0x0f

#endif /* _DEV_ISA_IF_ECREG_H_ */
