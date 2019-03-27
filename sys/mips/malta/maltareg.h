/*	$NetBSD: maltareg.h,v 1.1 2002/03/07 14:44:04 simonb Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Simon Burge for Wasabi Systems, Inc.
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
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
	Memory Map

	0000.0000 *	128MB	Typically SDRAM (on Core Board)
	0800.0000 *	256MB	Typically PCI
	1800.0000 *	 62MB	Typically PCI
	1be0.0000 *	  2MB	Typically System controller's internal registers
	1c00.0000 *	 32MB	Typically not used
	1e00.0000	  4MB	Monitor Flash
	1e40.0000	 12MB	reserved
	1f00.0000	 12MB	Switches
				LEDs
				ASCII display
				Soft reset
				FPGA revision number
				CBUS UART (tty2)
				General Purpose I/O
				I2C controller
	1f10.0000 *	 11MB	Typically System Controller specific
	1fc0.0000	  4MB	Maps to Monitor Flash
	1fd0.0000 *	  3MB	Typically System Controller specific

		  * depends on implementation of the Core Board and of software
 */

/*
	CPU interrupts

		NMI	South Bridge or NMI button
		 0	South Bridge INTR
		 1	South Bridge SMI
		 2	CBUS UART (tty2)
		 3	COREHI (Core Card)
		 4	CORELO (Core Card)
		 5	Not used, driven inactive (typically CPU internal timer interrupt

	IRQ mapping (as used by YAMON)

		0	Timer		South Bridge
		1	Keyboard	SuperIO
		2			Reserved by South Bridge (for cascading)
		3	UART (tty1)	SuperIO
		4	UART (tty0)	SuperIO
		5			Not used
		6	Floppy Disk	SuperIO
		7	Parallel Port	SuperIO
		8	Real Time Clock	South Bridge
		9	I2C bus		South Bridge
		10	PCI A,B,eth	PCI slot 1..4, Ethernet
		11	PCI C,audio	PCI slot 1..4, Audio, USB (South Bridge)
			PCI D,USB
		12	Mouse		SuperIO
		13			Reserved by South Bridge
		14	Primary IDE	Primary IDE slot
		15	Secondary IDE	Secondary IDE slot/Compact flash connector
 */

#define	MALTA_SYSTEMRAM_BASE	0x00000000ul  /* System RAM:	*/
#define	MALTA_SYSTEMRAM_SIZE	0x08000000  /*   128 MByte	*/

#define	MALTA_PCIMEM1_BASE	0x08000000ul  /* PCI 1 memory:	*/
#define	MALTA_PCIMEM1_SIZE	0x08000000  /*   128 MByte	*/

#define	MALTA_PCIMEM2_BASE	0x10000000ul  /* PCI 2 memory:	*/
#define	MALTA_PCIMEM2_SIZE	0x08000000  /*   128 MByte	*/

#define	MALTA_PCIMEM3_BASE	0x18000000ul  /* PCI 3 memory	*/
#define	MALTA_PCIMEM3_SIZE	0x03e00000  /*    62 MByte	*/

#define	MALTA_CORECTRL_BASE	0x1be00000ul  /* Core control:	*/
#define	MALTA_CORECTRL_SIZE	0x00200000  /*     2 MByte	*/

#define	MALTA_RESERVED_BASE1	0x1c000000ul  /* Reserved:	*/
#define	MALTA_RESERVED_SIZE1	0x02000000  /*    32 MByte	*/

#define	MALTA_MONITORFLASH_BASE	0x1e000000ul  /* Monitor Flash:	*/
#define	MALTA_MONITORFLASH_SIZE	0x003e0000  /*     4 MByte	*/
#define	MALTA_MONITORFLASH_SECTORSIZE 0x00010000 /* Sect. = 64 KB */

#define	MALTA_FILEFLASH_BASE	0x1e3e0000ul /* File Flash (for monitor): */
#define	MALTA_FILEFLASH_SIZE	0x00020000 /*   128 KByte	*/

#define	MALTA_FILEFLASH_SECTORSIZE 0x00010000 /* Sect. = 64 KB	*/

#define	MALTA_RESERVED_BASE2	0x1e400000ul  /* Reserved:	*/
#define	MALTA_RESERVED_SIZE2	0x00c00000  /*    12 MByte	*/

#define	MALTA_FPGA_BASE		0x1f000000ul  /* FPGA:		*/
#define	MALTA_FPGA_SIZE		0x00c00000  /*    12 MByte	*/

#define	MALTA_NMISTATUS		(MALTA_FPGA_BASE + 0x24)
#define	 MALTA_NMI_SB		 0x2	/* Pending NMI from the South Bridge */
#define	 MALTA_NMI_ONNMI	 0x1	/* Pending NMI from the ON/NMI push button */

#define	MALTA_NMIACK		(MALTA_FPGA_BASE + 0x104)
#define	 MALTA_NMIACK_ONNMI	 0x1	/* Write 1 to acknowledge ON/NMI */

#define	MALTA_SWITCH		(MALTA_FPGA_BASE + 0x200)
#define	 MALTA_SWITCH_MASK	 0xff	/* settings of DIP switch S2 */

#define	MALTA_STATUS		(MALTA_FPGA_BASE + 0x208)
#define	 MALTA_ST_MFWR		 0x10	/* Monitor Flash is write protected (JP1) */
#define	 MALTA_S54		 0x08	/* switch S5-4 - set YAMON factory default mode */
#define	 MALTA_S53		 0x04	/* switch S5-3 */
#define	 MALTA_BIGEND		 0x02	/* switch S5-2 - big endian mode */

#define	MALTA_JMPRS		(MALTA_FPGA_BASE + 0x210)
#define	 MALTA_JMPRS_PCICLK	 0x1c	/* PCI clock frequency */
#define	 MALTA_JMPRS_EELOCK	 0x02	/* I2C EEPROM is write protected */

#define	MALTA_LEDBAR		(MALTA_FPGA_BASE + 0x408)
#define	MALTA_ASCIIWORD		(MALTA_FPGA_BASE + 0x410)
#define	MALTA_ASCII_BASE	(MALTA_FPGA_BASE + 0x418)
#define	MALTA_ASCIIPOS0		0x00
#define	MALTA_ASCIIPOS1		0x08
#define	MALTA_ASCIIPOS2		0x10
#define	MALTA_ASCIIPOS3		0x18
#define	MALTA_ASCIIPOS4		0x20
#define	MALTA_ASCIIPOS5		0x28
#define	MALTA_ASCIIPOS6		0x30
#define	MALTA_ASCIIPOS7		0x38

#define	MALTA_SOFTRES		(MALTA_FPGA_BASE + 0x500)
#define	 MALTA_GORESET		 0x42	/* write this to MALTA_SOFTRES for board reset */

/*
 * BRKRES is the number of milliseconds before a "break" on tty will
 * trigger a reset.  A value of 0 will disable the reset.
 */
#define	MALTA_BRKRES		(MALTA_FPGA_BASE + 0x508)
#define	 MALTA_BRKRES_MASK	 0xff

#define	MALTA_CBUSUART		(MALTA_FPGA_BASE + 0x900)
/* 16C550C UART, 8 bit registers on 8 byte boundaries */
/* RXTX    0x00 */
/* INTEN   0x08 */
/* IIFIFO  0x10 */
/* LCTRL   0x18 */
/* MCTRL   0x20 */
/* LSTAT   0x28 */
/* MSTAT   0x30 */
/* SCRATCH 0x38 */
#define	MALTA_CBUSUART_INTR	2

#define	MALTA_GPIO_BASE		(MALTA_FPGA_BASE + 0xa00)
#define	MALTA_GPOUT		0x0
#define	MALTA_GPINP		0x8

#define	MALTA_I2C_BASE		(MALTA_FPGA_BASE + 0xb00)
#define	MALTA_I2CINP		0x00
#define	MALTA_I2COE		0x08
#define	MALTA_I2COUT		0x10
#define	MALTA_I2CSEL		0x18

#define	MALTA_BOOTROM_BASE	0x1fc00000ul  /* Boot ROM:	*/
#define	MALTA_BOOTROM_SIZE	0x00400000  /*     4 MByte	*/

#define	MALTA_REVISION		0x1fc00010ul
#define	 MALTA_REV_FPGRV	 0xff0000	/* CBUS FPGA revision */
#define	 MALTA_REV_CORID	 0x00fc00	/* Core Board ID */
#define	 MALTA_REV_CORRV	 0x000300	/* Core Board Revision */
#define	 MALTA_REV_PROID	 0x0000f0	/* Product ID */
#define	 MALTA_REV_PRORV	 0x00000f	/* Product Revision */

/* PCI definitions */
#define	MALTA_SOUTHBRIDGE_INTR	   0

#define MALTA_PCI0_IO_BASE         MALTA_PCIMEM3_BASE
#define MALTA_PCI0_ADDR( addr )    (MALTA_PCI0_IO_BASE + (addr))

#define MALTA_RTCADR               0x70 // MALTA_PCI_IO_ADDR8(0x70)
#define MALTA_RTCDAT               0x71 // MALTA_PCI_IO_ADDR8(0x71)

#define MALTA_SMSC_COM1_ADR        0x3f8
#define MALTA_SMSC_COM2_ADR        0x2f8
#define MALTA_UART0ADR             MALTA_PCI0_ADDR(MALTA_SMSC_COM1_ADR)
#define MALTA_UART1ADR             MALTA_SMSC_COM2_ADR // MALTA_PCI0_ADDR(MALTA_SMSC_COM2_ADR)

#define MALTA_SMSC_1284_ADR        0x378
#define MALTA_1284ADR              MALTA_SMSC_1284_ADR // MALTA_PCI0_ADDR(MALTA_SMSC_1284_ADR)

#define MALTA_SMSC_FDD_ADR         0x3f0
#define MALTA_FDDADR               MALTA_SMSC_FDD_ADR // MALTA_PCI0_ADDR(MALTA_SMSC_FDD_ADR)

#define MALTA_SMSC_KYBD_ADR        0x60  /* Fixed 0x60, 0x64 */
#define MALTA_KYBDADR              MALTA_SMSC_KYBD_ADR // MALTA_PCI0_ADDR(MALTA_SMSC_KYBD_ADR)
#define MALTA_SMSC_MOUSE_ADR       MALTA_SMSC_KYBD_ADR
#define MALTA_MOUSEADR             MALTA_KYBDADR


#define	MALTA_DMA_PCI_PCIBASE	0x00000000UL
#define	MALTA_DMA_PCI_PHYSBASE	0x00000000UL
#define	MALTA_DMA_PCI_SIZE	(256 * 1024 * 1024)

#define	MALTA_DMA_ISA_PCIBASE	0x00800000UL
#define	MALTA_DMA_ISA_PHYSBASE	0x00000000UL
#define	MALTA_DMA_ISA_SIZE	(8 * 1024 * 1024)

#ifndef _LOCORE
void	led_bar(uint8_t);
void	led_display_word(uint32_t);
void	led_display_str(const char *);
void	led_display_char(int, uint8_t);
#endif
