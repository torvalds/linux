/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 1999 Scott Mitchell
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: if_xereg.h,v 1.5 1999/05/20 21:53:58 scott Exp $
 * $FreeBSD$
 */
#ifndef DEV_XE_IF_XEREG_H
#define DEV_XE_IF_XEREG_H

/*
 * Register definitions for Xircom PCMCIA Ethernet controllers, based on
 * Rev. B of the "Dingo" 10/100 controller used in Xircom CEM56 and RealPort
 * Ethernet/modem cards.  The Dingo can be configured to be register
 * compatible with the "Mohawk" 10/100 controller used in Xircom CE3 cards
 * (also some Intel and Compaq OEM versions of the CE3).  The older 10Mbps CE2
 * cards seem to use earlier revisions of the same device.  Some registers and
 * bits below are marked 'CE2 only'; these are used by Werner Koch's xirc2ps
 * driver that was originally for the CE2 but, according to the spec, aren't
 * present on the Dingo.  They often seem to relate to operation on coax
 * cables, which Mohawk can do in theory (it has the SSI interface) so they
 * _might_ also work on Mohawk. I've also noted the few registers that are
 * specific to Dingo.
 *
 * As far as I can tell, the Dingo is basically a Mohawk device with a few
 * registers and support for a second PCMCIA function (the modem) added.  In
 * Dingo mode the SSI (non-MII) PHY interface of the Mohawk is not available.
 * The CE2 chip is most likely a Mohawk without the MII and definitely with a
 * slightly different register set.
 *
 * In all cases, the controller uses a paged model of register access.  The
 * first eight registers are always the same, the function of the second eight 
 * is selected by the value in the Page Register (reg 0x01).
 *
 * References:
 * 1. Dingo External Reference Specification, Revision B.  Xircom Inc.,
 *    Thousand Oaks, California.  August 1998.  Available under licence from
 *    Xircom, http://www.xircom.com/
 * 2. ML6692 100BASE-TX Physical Layer with MII specification.  MicroLinear
 *    Corp, San Jose, California.  May 1997.  Available for download from
 *    http://www.microlinear.com/
 * 3. DP83840 10/100 Mb/s Ethernet Physical Layer specification.  National
 *    Semiconductor Corp., Arlington, Texas.  March 1997.  Available for
 *    download from http://www.ns.com/
 * 4. Werner Koch's xirc2ps driver for Linux, for all the CE2 and CE3 frobs
 *    that aren't documented in the Xircom spec.  Available for download from
 *    http://www.d.shuttle.de/isil/xircom/xirc2ps.html
 */

/*******************
 * PCMCIA registers
 *******************/

/*
 * These are probably Dingo-specific, but you won't need them unless you have
 * a CEM card that needs a bit of hackery to get the Ethernet function to
 * operate.  All addresses are in card attribute space.
 */
#define DINGO_CIS		0x0000	/* Start of CIS tuples */
#define DINGO_ETH		0x0800	/* Ethernet configuration registers */
#define DINGO_COR		0x0820	/* Dingo configuration option registers */
#define DINGO_2ND		0x0840  /* 2nd function configuration registers */


/*
 * Ethernet configuration registers
 */
#define DINGO_ECOR	(DINGO_ETH+0)	/* Ethernet Configuration Option Register */
#define DINGO_ECSR	(DINGO_ETH+2)	/* Ethernet Configuration Status Register */
#define DINGO_EBAR0	(DINGO_ETH+10)	/* Ethernet Base Address Register bits 7:4 (3:0 always 0) */
#define DINGO_EBAR1	(DINGO_ETH+12)	/* Ethernet Base Address Register bits 15:8 */

/* DINGO_ECOR bits */
#define DINGO_ECOR_ETH_ENABLE	0x01	/* 1 = Enable Ethernet part of adapter */
#define DINGO_ECOR_IOB_ENABLE	0x02	/* 1 = Enable EBAR, else use INDEX bits */
#define DINGO_ECOR_INT_ENABLE	0x04	/* 1 = Enable Ethernet interrupts */
#define DINGO_ECOR_IOB_INDEX	0x18	/* 00 = 0x300; 01 = 0x310; 10 = 0x320; 11 = no IO base */
#define DINGO_ECOR_IOB_SHIFT	0x03
#define DINGO_ECOR_IRQ_STSCHG	0x20	/* 1 = Route interrupts to -STSCHG pin, else use -INT pin */
#define DINGO_ECOR_IRQ_LEVEL	0x40	/* 1 = Level-triggered interrupts, else edge-triggered */
#define DINGO_ECOR_SRESET	0x80	/* 1 = Soft reset Ethernet adpater.  Must write to 0 */

/* DINGO_ECSR bits */
#define DINGO_ECSR_INT_ACK	0x01	/* 1 = Host must acknowledge interrupts (Clear ECSR_INT bit) */
#define DINGO_ECSR_INT		0x02	/* 1 = Interrupt service requested */
#define DINGO_ECSR_POWER_DOWN	0x04	/* 1 = Power down Ethernet adapter */

/*
 * EBAR0/EBAR1 set the I/O base address of the Ethernet adapter when
 * ECOR_IOB_ENABLE is set.  12 significant bits.
 */


/*
 * Dingo configuration registers
 */
#define DINGO_DCOR0	(DINGO_COR+0)	/* Dingo Configuration Options Register 0 */
#define DINGO_DCOR1	(DINGO_COR+2)	/* Dingo Configuration Options Register 1 */
#define DINGO_DCOR2	(DINGO_COR+4)	/* Dingo Configuration Options Register 2 */
#define DINGO_DCOR3	(DINGO_COR+6)	/* Dingo Configuration Options Register 3 */
#define DINGO_DCOR4	(DINGO_COR+8)	/* Dingo Configuration Options Register 4 */

/* DINGO_DCOR0 bits */
#define DINGO_DCOR0_SF_INT	0x01	/* 1 = Enable 2ndF interrupts (alternate to SFCOR:2) */
#define DINGO_DCOR0_DECODE	0x04	/* 1 = Decode 2ndF interrupts in Dingo, else in 2ndF */
#define DINGO_DCOR0_BUS		0x08	/* 1 = 2ndF bus is ISA, else PCMCIA */
#define DINGO_DCOR0_LED3_POWER	0x10	/* 1 = Drive LED3 line from SFCSR:2 */
#define DINGO_DCOR0_LED3_RESET	0x20	/* 1 = Drive LED3 line from SFCOR:7 */
#define DINGO_DCOR0_MR_POWER	0x40	/* 1 = Drive MRESET line from SFCSR:2 */
#define DINGO_DCOR0_MR_RESET	0x80	/* 1 = Drive MRESET line from SFCOR:7 */

/* DINGO_DCOR1 bits */
#define DINGO_DCOR1_INT_STSCHG	0x01	/* 1 = Route 2ndF interrupts to -STSCHG (alternate to SFCOR:5) */
#define DINGO_DCOR1_MSTSCHG	0x02	/* 1 = Route 2ndF -MSTSCHG line to -STSCHG */
#define DINGO_DCOR1_EEDIO	0x04	/* 1 = Use EEDIO pin as data line 6 to 2ndF */
#define DINGO_DCOR1_INT_LEVEL	0x08	/* 1 = Force level-triggered interrupts from 2ndF */
#define DINGO_DCOR1_SHADOW_CSR	0x10	/* Reserved, always write 0 */
#define DINGO_DCOR1_SHADOW_IOB	0x20	/* Reserved, always write 0 */
#define DINGO_DCOR1_CSR_WAIT	0xC0	/* Reserved, always write 0 */
#define DINGO_DCOR1_CSR_SHIFT	0x06

/* DINGO_DCOR2 bits */
#define DINGO_DCOR2_SHM_BASE	0x0f	/* Bits 15-12 of Ethernet shared memory window */
#define DINGO_DCOR2_SHM_SHIFT	0x00
#define DINGO_DCOR2_SHADOW_COR	0x10	/* Reserved, always write 0 */

/*
 * DCOR3/DCOR4 configure Dingo to assert -IOIS16 on any access to each pair of 
 * ports in the range SFIOB+0 .. SFIOB+31.  Each pair can be set individually,
 * eg. DCOR3:0 enables this function on ports SFIOB+0 and SFIOB+1.
 */


/*
 * Second function configuration registers
 */
#define DINGO_SFCOR	(DINGO_2ND+0)	/* 2nd Function Configuration Option Register */
#define DINGO_SFCSR	(DINGO_2ND+2)	/* 2nd Function Configuration Status Register */
#define DINGO_SFBAR0	(DINGO_2ND+10)	/* 2nd Function Base Address Register bits 7:0 */
#define DINGO_SFBAR1	(DINGO_2ND+12)	/* 2nd Function Base Address Register bits 15:8 */
#define DINGO_SFILR	(DINGO_2ND+18)	/* 2nd Function I/O Limit Register */

/* DINGO_SFCOR bits */
#define DINGO_SFCOR_SF_ENABLE	0x01	/* 1 = Enable second fuction */
#define DINGO_SFCOR_IOB_ENABLE	0x02	/* 1 = Enable SFBAR, else use COM_SELECT bits */
#define DINGO_SFCOR_INT_ENABLE	0x04	/* 1 = Enable second function interrupts */
#define DINGO_SFCOR_COM_SELECT	0x18	/* 00 = 0x3f8; 01 = 0x2f8; 10 = 0x3e8; 11 = 0x2e8 */
#define DINGO_SFCOR_COM_SHIFT	0x03
#define DINGO_SFCOR_IRQ_STSCHG	0x20	/* 1 = Route interrupts to -STSCHG pin, else use -INT pin */
#define DINGO_SFCOR_IRQ_LEVEL	0x40	/* 1 = Level-triggered interrupts, else edge-triggered */
#define DINGO_SFCOR_SRESET	0x80	/* 1 = Soft reset second function.  Must write to 0 */

/* DINGO_SFCSR bits */
#define DINGO_SFCSR_INT_ACK	0x01	/* 1 = Host must acknowledge interrupts (Clear SFCSR_INT bit) */
#define DINGO_SFCSR_INT		0x02	/* 1 = Interrupt service requested */
#define DINGO_SFCSR_POWER_DOWN	0x04	/* 1 = Power down second function */

/*
 * SFBAR0/SFBAR1 set the I/O base address of the second function when
 * SFCOR_IOB_ENABLE is set.  16 significant bits.
 */

/*
 * SFILR is a bitmap of address lines 7:0 decoded by the second function
 * device.  Eg. a device with 16 ports should write 0x0f to this register.
 */



/********************************
 * Ethernet controller registers
 ********************************/

/*
 * Common registers (available from any register page)
 *
 * Note: The EDP is actually 32 bits wide, occupying registers 2-5.  In PCMCIA 
 * operation we can only access 16 bits at once, through registers 4 & 5.
 */
#define XE_CR			0x00	/* Command register (write) */
#define XE_ESR			0x00	/* Ethernet status register (read) */
#define XE_PR			0x01	/* Page select register */
#define XE_EDP			0x04	/* Ethernet data port */
#define XE_ISR			0x06	/* Ethernet interrupt status register (read) */
#define XE_GIR			0x07	/* Global interrupt register (Dingo only) */

/* XE_CR bits */
#define XE_CR_TX_PACKET		0x01	/* Transmit packet */
#define XE_CR_SOFT_RESET	0x02	/* Software reset */
#define XE_CR_ENABLE_INTR	0x04	/* Enable interrupts */
#define XE_CR_FORCE_INTR	0x08	/* Force an interrupt */
#define XE_CR_CLEAR_FIFO	0x10	/* Clear FIFO after transmit overrun */
#define XE_CR_CLEAR_OVERRUN	0x20	/* Clear receive overrun condition */
#define XE_CR_RESTART_TX	0x40	/* Restart TX after 16 collisions or TX underrun */

/* XE_ESR bits */
#define XE_ESR_FULL_PACKET_RX	0x01	/* At least one full packet received */
#define XE_ESR_PART_PACKET_RX	0x02	/* At least 64 bytes of packet received */
#define XE_ESR_REJECT_PACKET	0x04	/* Partial packet rejected */
#define XE_ESR_TX_PENDING	0x08	/* At least one packet waiting to transmit */
#define XE_ESR_BAD_POLARITY	0x10	/* Bad cable polarity? (CE2 only) */
#define XE_ESR_MEDIA_SELECT	0x20	/* SSI(?) media select: 1 = Twisted pair; 0 = AUI */

/* XE_ISR bits */
#define XE_ISR_TX_OVERFLOW	0x01	/* No space in transmit buffer */
#define XE_ISR_TX_PACKET	0x02	/* Packet sent successfully */
#define XE_ISR_MAC_INTR		0x04	/* Some kind of MAC interrupt happened */
#define XE_ISR_RX_EARLY		0x10	/* Incoming packet in early receive mode */
#define XE_ISR_RX_PACKET	0x20	/* Complete packet received successfully */
#define XE_ISR_RX_REJECT	0x40	/* Partial incoming packet rejected by MAC */
#define XE_ISR_FORCE_INTR	0x80	/* Interrupt forced */

/* XE_GIR bits */
#define XE_GIR_ETH_IRQ		0x01	/* Ethernet IRQ pending */
#define XE_GIR_ETH_MASK		0x02	/* 1 = Mask Ethernet interrupts to host */
#define XE_GIR_SF_IRQ		0x04	/* Second function IRQ pending */
#define XE_GIR_SF_MASK		0x08	/* 1 = Mask second function interrupts to host */


/*
 * Page 0 registers
 */
#define XE_TSO			0x08	/* Transmit space open (17 bits) */
#define XE_TRS			0x0a	/* Transmit reservation size (CE2 only, removed in rev. 1) */
#define XE_DO			0x0c	/* Data offset register (13 bits/3 flags, write) */
#define XE_RSR			0x0c	/* Receive status register (read) */
#define XE_TPR			0x0d	/* Packets transmitted register (read) */
#define XE_RBC			0x0e	/* Received byte count (13 bits/3 flags, read) */

/* XE_DO bits */
#define XE_DO_OFFSET		0x1fff	/* First byte fetched when CHANGE_OFFSET issued */
#define XE_DO_OFFSET_SHIFT	0x00
#define XE_DO_CHANGE_OFFSET	0x2000	/* Flush RX FIFO, start fetching from OFFSET */
#define XE_DO_SHARED_MEM	0x4000	/* Enable shared memory mode */
#define XE_DO_SKIP_RX_PACKET	0x8000	/* Skip to next packet in buffer memory */

/* XE_RSR bits */
#define XE_RSR_PHYS_PACKET	0x01	/* 1 = Physical packet, 0 = Multicast packet */
#define XE_RSR_BCAST_PACKET	0x02	/* Broadcast packet */
#define XE_RSR_LONG_PACKET	0x04	/* Packet >1518 bytes */
#define XE_RSR_ADDR_MATCH	0x08	/* Packet matched one of our node addresses */
#define XE_RSR_ALIGN_ERROR	0x10	/* Bad alignment? (CE2 only) */
#define XE_RSR_CRC_ERROR	0x20	/* Incorrect CRC */
#define XE_RSR_RX_OK		0x80	/* No errors on received packet */

/* XE_RBC bits */
#define XE_RBC_BYTE_COUNT	0x1fff	/* Bytes received for current packet */
#define XE_RBC_COUNT_SHIFT	0x00
#define XE_RBC_FULL_PACKET_RX	0x2000	/* These mirror bits 2:0 of ESR, if ECR:7 is set */
#define XE_RBC_PART_PACKET_RX	0x4000
#define XE_RBC_REJECT_PACKET	0x8000


/*
 * Page 1 registers
 */
#define XE_IMR0			0x0c	/* Interrupt mask register 0 */
#define XE_IMR1			0x0d	/* Interrupt mask register 1 (CE2 only) */
#define XE_ECR			0x0e	/* Ethernet configuration register */

/* XE_IMR0 bits */
#define XE_IMR0_TX_OVERFLOW	0x01	/* Masks for bits in ISR */
#define XE_IMR0_TX_PACKET	0x02
#define XE_IMR0_MAC_INTR	0x04
#define XE_IMR0_TX_RESGRANT 0x08	/* Tx reservation granted (CE2) */
#define XE_IMR0_RX_EARLY	0x10
#define XE_IMR0_RX_PACKET	0x20
#define XE_IMR0_RX_REJECT	0x40
#define XE_IMR0_FORCE_INTR	0x80

/* XE_IMR1 bits */
#define XE_IMR1_TX_UNDERRUN	0x01

/* XE_ECR bits */
#define XE_ECR_EARLY_TX		0x01	/* Enable early transmit mode */
#define XE_ECR_EARLY_RX		0x02	/* Enable early receive mode */
#define XE_ECR_FULL_DUPLEX 	0x04	/* Enable full-duplex (disable collision detection) */
#define XE_ECR_LONG_TPCABLE	0x08	/* CE2 only */
#define XE_ECR_NO_POL_COL	0x10	/* CE2 only */
#define XE_ECR_NO_LINK_PULSE	0x20	/* Don't check/send link pulses (not 10BT compliant) */
#define XE_ECR_NO_AUTO_TX	0x40	/* CE2 only */
#define XE_ECR_SOFT_COMPAT	0x80	/* Map ESR bits 2:0 to RBC bits 15:13 */


/*
 * Page 2 registers
 */
#define XE_RBS			0x08	/* Receive buffer start (16 bits) */
#define XE_LED			0x0a	/* LED control register */
#define XE_LED3			0x0b	/* LED3 control register */
#define XE_MSR			0x0c	/* Misc. setup register (Mohawk specific register?) */
#define XE_GPR2			0x0d	/* General purpose register 2 */

/*
 * LED function selection:
 * 000 - Disabled
 * 001 - Collision activity
 * 010 - !Collision activity
 * 011 - 10Mbit link detected
 * 100 - 100Mbit link detected
 * 101 - 10/100Mbit link detected
 * 110 - Automatic assertion
 * 111 - Transmit activity
 */

/* XE_LED bits */
#define XE_LED_LED0_MASK	0x07	/* LED0 function selection */
#define XE_LED_LED0_SHIFT	0x00
#define XE_LED_LED1_MASK	0x38	/* LED1 function selection */
#define XE_LED_LED1_SHIFT	0x03
#define XE_LED_LED0_RX		0x40	/* Add receive activity to LED0 */
#define XE_LED_LED1_RX		0x80	/* Add receive activity to LED1 */

/* XE_LED3 bits */
#define XE_LED3_MASK		0x07	/* LED3 function selection */
#define XE_LED3_SHIFT		0x00
#define XE_LED3_RX		0x40	/* Add receive activity to LED3 */

/* XE_MSR bits */
#define XE_MSR_128K_SRAM	0x01	/* Select 128K SRAM */
#define XE_MSR_RBS_BIT16	0x02	/* Bit 16 of RBS (only useful with big SRAM) */
#define XE_MSR_MII_SELECT	0x08	/* Select MII instead of SSI interface */
#define XE_MSR_HASH_TABLE	0x20	/* Enable hash table filtering */

/* XE_GPR2 bits */
#define XE_GPR2_GP3_OUT		0x01	/* Value written to GP3 line */
#define XE_GPR2_GP4_OUT		0x02	/* Value written to GP4 line */
#define XE_GPR2_GP3_SELECT	0x04	/* 1 = GP3 is output, 0 = GP3 is input */
#define XE_GPR2_GP4_SELECT	0x08	/* 1 = GP4 is output, 0 = GP3 is input */
#define XE_GPR2_GP3_IN		0x10	/* Value read from GP3 line */
#define XE_GPR2_GP4_IN		0x20	/* Value read from GP4 line */


/*
 * Page 3 registers
 */
#define XE_TPT			0x0a	/* Transmit packet threshold (13 bits) */


/*
 * Page 4 registers
 */
#define XE_GPR0			0x08	/* General purpose register 0 */
#define XE_GPR1			0x09	/* General purpose register 1 */
#define XE_BOV			0x0a	/* Bonding version register (read) */
#define XE_EES			0x0b	/* EEPROM control register */
#define XE_LMA			0x0c	/* Local memory address (CE2 only) */
#define XE_LMD			0x0e	/* Local memory data (CE2 only) */

/* XE_GPR0 bits */
#define XE_GPR0_GP1_OUT		0x01	/* Value written to GP1 line */
#define XE_GPR0_GP2_OUT		0x02	/* Value written to GP2 line */
#define XE_GPR0_GP1_SELECT	0x04	/* 1 = GP1 is output, 0 = GP1 is input */
#define XE_GPR0_GP2_SELECT	0x08	/* 1 = GP2 is output, 0 = GP2 is input */
#define XE_GPR0_GP1_IN		0x10	/* Value read from GP1 line */
#define XE_GPR0_GP2_IN		0x20	/* Value read from GP2 line */

/* XE_GPR1 bits */
#define XE_GPR1_POWER_DOWN	0x01	/* 0 = Power down analog section */
#define XE_GPR1_AIC			0x04	/* AIC bit (CE2 only) */

/* XE_BOV values */
#define XE_BOV_DINGO		0x55	/* Dingo in Dingo mode */
#define XE_BOV_MOHAWK		0x41	/* Original Mohawk */
#define XE_BOV_MOHAWK_REV1	0x45	/* Rev. 1 Mohawk, or Dingo in Mohawk mode */
#define XE_BOV_CEM28		0x11	/* CEM28 */

/* XE_EES bits */
#define XE_EES_SCL_OUTPUT	0x01	/* Value written to SCL line, when MANUAL_ROM set */
#define XE_EES_SDA_OUTPUT	0x02	/* Value written to SDA line, when MANUAL_ROM set */
#define XE_EES_SDA_INPUT	0x04	/* Value read from SDA line */
#define XE_EES_SDA_TRISTATE	0x08	/* 1 = SDA is output, 0 = SDA is input */
#define XE_EES_MANUAL_ROM	0x20	/* Enable manual contro of serial EEPROM */


/*
 * Page 5 registers (all read only)
 */
#define XE_CRHA			0x08	/* Current Rx host address (16 bits) */
#define XE_RHSA 		0x0a	/* Rx host start address (16 bits) */
#define XE_RNSA			0x0c	/* Rx network start address (16 bits) */
#define XE_CRNA			0x0e	/* Current Rx network address (16 bits) */


/*
 * Page 6 registers (all read only)
 */
#define XE_CTHA			0x08	/* Current Tx host address (16 bits) */
#define XE_THSA			0x0a	/* Tx host start address (16 bits) */
#define XE_TNSA			0x0c	/* Tx network statr address (16 bits) */
#define XE_CTNA			0x0e	/* Current Tx network address (16 bits) */


/*
 * Page 8 registers (all read only)
 */
#define XE_THBC			0x08	/* Tx host byte count (16 bits) */
#define XE_THPS			0x0a	/* Tx host packet size (16 bits) */
#define XE_TNBC			0x0c	/* Tx network byte count (16 bits) */
#define XE_TNPS			0x0e	/* Tx network packet size (16 bits) */


/*
 * Page 0x10 registers (all read only)
 */
#define XE_DINGOID		0x08	/* Dingo ID register (16 bits) (Dingo only) */
#define XE_RevID		0x0a	/* Dingo revision ID (16 bits) (Dingo only) */
#define XE_VendorID		0x0c	/* Dingo vendor ID   (16 bits) (Dingo only) */

/* Values for the above registers */
#define XE_DINGOID_DINGO3	0x444b	/* In both Dingo and Mohawk modes */
#define XE_RevID_DINGO3		0x0001
#define XE_VendorID_DINGO3	0x0041


/*
 * Page 0x40 registers
 */
#define XE_CMD0			0x08	/* MAC Command register (write) */
#define XE_RST0			0x09	/* Receive status register */
#define XE_TXST0		0x0b	/* Transmit status register 0 */
#define XE_TXST1		0x0c	/* Transmit status register 1 */
#define XE_RX0Msk		0x0d	/* Receive status mask register */
#define XE_TX0Msk		0x0e	/* Transmit status 0 mask register */
#define XE_TX1Msk		0x0f	/* Transmit status 1 mask register */

/* CMD0 bits */
#define XE_CMD0_TX		0x01	/* CE2 only */
#define XE_CMD0_RX_ENABLE	0x04	/* Enable receiver */
#define XE_CMD0_RX_DISABLE	0x08	/* Disable receiver */
#define XE_CMD0_ABORT		0x10	/* CE2 only */
#define XE_CMD0_ONLINE		0x20	/* Take MAC online */
#define XE_CMD0_ACK_INTR	0x40	/* CE2 only */
#define XE_CMD0_OFFLINE		0x80	/* Take MAC offline */

/* RST0 bits */
#define XE_RST0_LONG_PACKET	0x02	/* Packet received with >1518 and <8184 bytes */
#define XE_RST0_CRC_ERROR	0x08	/* Packet received with incorrect CRC */
#define XE_RST0_RX_OVERRUN	0x10	/* Receiver overrun, byte(s) dropped */
#define XE_RST0_RX_ENABLE	0x20	/* Receiver enabled */
#define XE_RST0_RX_ABORT	0x40	/* Receive aborted: CRC, FIFO overrun or addr mismatch */
#define XE_RST0_RX_OK		0x80	/* Complete packet received OK */

/* TXST0 bits */
#define XE_TXST0_NO_CARRIER	0x01	/* Lost carrier.  Only valid in 10Mbit half-duplex */
#define XE_TXST0_16_COLLISIONS	0x02	/* Packet aborted after 16 collisions */
#define XE_TXST0_TX_UNDERRUN	0x08	/* MAC ran out of data to send */
#define XE_TXST0_LATE_COLLISION	0x10	/* Collision later than 512 bits */
#define XE_TXST0_SQE_FAIL	0x20	/* SQE test failed. */
#define XE_TXST0_TX_ABORT	0x40	/* Transmit aborted: collisions, underrun or overrun */
#define XE_TXST0_TX_OK		0x80	/* Complete packet sent OK */

/* TXST1 bits */
#define XE_TXST1_RETRY_COUNT	0x0f	/* Collision counter for current packet */
#define XE_TXST1_LINK_STATUS	0x10	/* Valid link status */

/* RX0Msk bits */
#define XE_RX0M_MP			0x01	/* Multicast packet? (CE2 only) */
#define XE_RX0M_LONG_PACKET	0x02	/* Masks for bits in RXST0 */
#define XE_RX0M_ALIGN_ERROR	0x04	/* Alignment error (CE2 only) */
#define XE_RX0M_CRC_ERROR	0x08
#define XE_RX0M_RX_OVERRUN	0x10
#define XE_RX0M_RX_ABORT	0x40
#define XE_RX0M_RX_OK		0x80

/* TX0Msk bits */
#define XE_TX0M_NO_CARRIER	0x01	/* Masks for bits in TXST0 */
#define XE_TX0M_16_COLLISIONS	0x02
#define XE_TX0M_TX_UNDERRUN	0x08
#define XE_TX0M_LATE_COLLISION	0x10
#define XE_TX0M_SQE_FAIL	0x20
#define XE_TX0M_TX_ABORT	0x40
#define XE_TX0M_TX_OK		0x80

/* TX1Msk bits */
#define	XE_TX1M_PKTDEF		0x20


/*
 * Page 0x42 registers
 */
#define XE_SWC0			0x08	/* Software configuration 0 */
#define XE_SWC1			0x09	/* Software configuration 1 */
#define XE_BOC			0x0a	/* Back-off configuration */
#define XE_TCD			0x0b	/* Transmit collision deferral */

/* SWC0 bits */
#define XE_SWC0_LOOPBACK_ENABLE	0x01	/* Enable loopback operation */
#define XE_SWC0_LOOPBACK_SOURCE	0x02	/* 1 = Transceiver, 0 = MAC */
#define XE_SWC0_ACCEPT_ERROR	0x04	/* Accept otherwise OK packets with CRC errors */
#define XE_SWC0_ACCEPT_SHORT	0x08	/* Accept otherwise OK packets that are too short */
#define XE_SWC0_NO_SRC_INSERT	0x20	/* Disable source insertion (CE2) */
#define XE_SWC0_NO_CRC_INSERT	0x40	/* Don't add CRC to outgoing packets */

/* SWC1 bits */
#define XE_SWC1_IA_ENABLE	0x01	/* Enable individual address filters */
#define XE_SWC1_ALLMULTI	0x02	/* Accept all multicast packets */
#define XE_SWC1_PROMISCUOUS	0x04	/* Accept all non-multicast packets */
#define XE_SWC1_BCAST_DISABLE	0x08	/* Reject broadcast packets */
#define XE_SWC1_MEDIA_SELECT	0x40	/* AUI media select (Mohawk only) */
#define XE_SWC1_AUTO_MEDIA	0x80	/* Auto media select (Mohawk only) */


/*
 * Page 0x44 registers (CE2 only)
 */
#define XE_TDR0			0x08	/* Time domain reflectometry register 0 */
#define XE_TDR1			0x09	/* Time domain reflectometry register 1 */
#define XE_RXC0			0x0a	/* Receive byte count low */
#define XE_RXC1			0x0b	/* Receive byte count high */


/*
 * Page 0x45 registers (CE2 only)
 */
#define XE_REV			0x0f	/* Revision (read) */


/*
 * Page 0x50-0x57: Individual address 0-9
 *
 * Used to filter incoming packets by matching against individual node
 * addresses.  If IA matching is enabled (SWC1, bit0) any incoming packet with 
 * a destination matching one of these 10 addresses will be received.  IA0 is
 * always enabled and usually matches the card's unique address.
 *
 * Addresses are stored LSB first, ie. IA00 (reg. 8 on page 0x50) contains the 
 * LSB of IA0, and so on.  The data is stored contiguously, in that addresses
 * can be broken across page boundaries.  That is:
 *
 * Reg: 50/8 50/9 50/a 50/b 50/c 50/d 50/e 50/f 51/8 51/9 ... 57/a 57/b
 *      IA00 IA01 IA02 IA03 IA04 IA05 IA10 IA11 IA12 IA13 ... IA94 IA95
 */

/*
 * Page 0x58: Multicast hash table filter
 *
 * In case the 10 individual addresses aren't enough, we also have a multicast
 * hash filter, enabled through MSR:5.  The most significant six bits of the
 * CRC on each incoming packet are reversed and used as an index into the 64
 * bits of the hash table.  If the appropriate bit is set the packet it
 * received, although higher layers may still need to filter it out.  The CRC
 * calculation is as follows:
 *
 * crc = 0xffffffff;
 * poly = 0x04c11db6;
 * for (i = 0; i < 6; i++) {
 *   current = mcast_addr[i];
 *   for (k = 1; k <= 8; k++) {
 *     if (crc & 0x80000000)
 *       crc31 = 0x01;
 *     else
 *       crc31 = 0;
 *     bit = crc31 ^ (current & 0x01);
 *     crc <<= 1;
 *     current >>= 1;
 *     if (bit)
 *       crc = (crc ^ poly)|1
 *   }
 * }
 */



/****************
 * MII registers
 ****************/

/*
 * Basic MII-compliant PHY register definitions.  According to the Dingo spec, 
 * PHYs from (at least) MicroLinear, National Semiconductor, ICS, TDK and
 * Quality Semiconductor have been used.  These apparently all come up with
 * PHY ID 0x00 unless the "interceptor module" on the Dingo 3 is in use.  With 
 * the interceptor enabled, the PHY is faked up to look like an ICS unit with
 * ID 0x16.  The interceptor can be enabled/disabled in software.
 *
 * The ML6692 (and maybe others) doesn't have a 10Mbps mode -- this is handled 
 * by an internal 10Mbps transceiver that we know nothing about... some cards
 * seem to work with the MII in 10Mbps mode, so I guess some PHYs must support 
 * it.  The question is, how can you figure out which one you have?  Just to
 * add to the fun there are also 10Mbps _only_ Mohawk/Dingo cards.  Aaargh!
 */

/*
 * Masks for the MII-related bits in GPR2
 */
#define XE_MII_CLK		XE_GPR2_GP3_OUT
#define XE_MII_DIR		XE_GPR2_GP4_SELECT
#define XE_MII_WRD		XE_GPR2_GP4_OUT
#define XE_MII_RDD		XE_GPR2_GP4_IN

/*
 * MII PHY ID register values
 */
#define PHY_ID_ML6692		0x0000	/* MicroLinear ML6692? Or unknown */
#define	PHY_ID_ICS1890		0x0015	/* ICS1890 */
#define	PHY_ID_QS6612		0x0181	/* Quality QS6612 */
#define	PHY_ID_DP83840		0x2000	/* National DP83840 */

/*
 * MII command (etc) bit strings.
 */
#define XE_MII_STARTDELIM	0x01
#define XE_MII_READOP		0x02
#define XE_MII_WRITEOP		0x01
#define XE_MII_TURNAROUND	0x02

/*
 * PHY registers.
 */
#define PHY_BMCR		0x00	/* Basic Mode Control Register */
#define PHY_BMSR		0x01	/* Basic Mode Status Register */
#define	PHY_ID1			0x02	/* PHY ID 1 */
#define	PHY_ID2			0x03	/* PHY ID 2 */
#define	PHY_ANAR		0x04	/* Auto-Negotiation Advertisement Register */
#define PHY_LPAR		0x05	/* Auto-Negotiation Link Partner Ability Register */
#define PHY_ANER		0x06	/* Auto-Negotiation Expansion Register */

/* BMCR bits */
#define PHY_BMCR_RESET		0x8000	/* Soft reset PHY.  Self-clearing */
#define PHY_BMCR_LOOPBK		0x4000	/* Enable loopback */
#define PHY_BMCR_SPEEDSEL	0x2000	/* 1=100Mbps, 0=10Mbps */
#define PHY_BMCR_AUTONEGENBL	0x1000	/* Auto-negotiation enabled */
#define PHY_BMCR_ISOLATE	0x0400	/* Isolate ML6692 from MII */
#define PHY_BMCR_AUTONEGRSTR	0x0200	/* Restart auto-negotiation.  Self-clearing */
#define PHY_BMCR_DUPLEX		0x0100	/* Full duplex operation */
#define PHY_BMCR_COLLTEST	0x0080	/* Enable collision test */

/* BMSR bits */
#define PHY_BMSR_100BT4		0x8000	/* 100Base-T4 capable */
#define PHY_BMSR_100BTXFULL	0x4000	/* 100Base-TX full duplex capable */
#define PHY_BMSR_100BTXHALF	0x2000	/* 100Base-TX half duplex capable */
#define PHY_BMSR_10BTFULL	0x1000	/* 10Base-T full duplex capable */
#define PHY_BMSR_10BTHALF	0x0800	/* 10Base-T half duplex capable */
#define PHY_BMSR_AUTONEGCOMP	0x0020	/* Auto-negotiation complete */
#define PHY_BMSR_CANAUTONEG	0x0008	/* Auto-negotiation supported */
#define PHY_BMSR_LINKSTAT	0x0004	/* Link is up */
#define PHY_BMSR_EXTENDED	0x0001	/* Extended register capabilities */

/* ANAR bits */
#define PHY_ANAR_NEXTPAGE	0x8000	/* Additional link code word pages */
#define PHY_ANAR_TLRFLT		0x2000	/* Remote wire fault detected */
#define PHY_ANAR_100BT4		0x0200	/* 100Base-T4 capable */
#define PHY_ANAR_100BTXFULL	0x0100	/* 100Base-TX full duplex capable */
#define PHY_ANAR_100BTXHALF	0x0080	/* 100Base-TX half duplex capable */
#define PHY_ANAR_10BTFULL	0x0040	/* 10Base-T full duplex capable */
#define PHY_ANAR_10BTHALF	0x0020	/* 10Base-T half duplex capable */
#define PHY_ANAR_PROTO4		0x0010	/* Protocol selection (00001 = 802.3) */
#define PHY_ANAR_PROTO3		0x0008
#define PHY_ANAR_PROTO2		0x0004
#define PHY_ANAR_PROTO1		0x0002
#define PHY_ANAR_PROTO0		0x0001
#define PHY_ANAR_8023		PHY_ANAR_PROTO0
#define	PHY_ANAR_DINGO		PHY_ANAR_100BT+PHY_ANAR_10BT_FD+PHY_ANAR_10BT+PHY_ANAR_8023
#define	PHY_ANAR_MOHAWK		PHY_ANAR_100BT+PHY_ANAR_10BT_FD+PHY_ANAR_10BT+PHY_ANAR_8023

/* LPAR bits */
#define PHY_LPAR_NEXTPAGE	0x8000	/* Additional link code word pages */
#define PHY_LPAR_LPACK		0x4000	/* Link partner acknowledged receipt */
#define PHY_LPAR_TLRFLT		0x2000	/* Remote wire fault detected */
#define PHY_LPAR_100BT4		0x0200	/* 100Base-T4 capable */
#define PHY_LPAR_100BTXFULL	0x0100	/* 100Base-TX full duplex capable */
#define PHY_LPAR_100BTXHALF	0x0080	/* 100Base-TX half duplex capable */
#define PHY_LPAR_10BTFULL	0x0040	/* 10Base-T full duplex capable */
#define PHY_LPAR_10BTHALF	0x0020	/* 10Base-T half duplex capable */
#define PHY_LPAR_PROTO4		0x0010	/* Protocol selection (00001 = 802.3) */
#define PHY_LPAR_PROTO3		0x0008
#define PHY_LPAR_PROTO2		0x0004
#define PHY_LPAR_PROTO1		0x0002
#define PHY_LPAR_PROTO0		0x0001

/* ANER bits */
#define PHY_ANER_MLFAULT	0x0010	/* More than one link is up! */
#define PHY_ANER_LPNPABLE	0x0008	/* Link partner supports next page */
#define PHY_ANER_NPABLE		0x0004	/* Local port supports next page */
#define PHY_ANER_PAGERX		0x0002	/* Page received */
#define PHY_ANER_LPAUTONEG	0x0001	/* Link partner can auto-negotiate */

#endif /* DEV_XE_IF_XEREG_H */
