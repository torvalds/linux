/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, Javier Martín Rueda (jmrueda@diatel.upm.es)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD$
 */

/*
 * Intel EtherExpress Pro/10 Ethernet driver
 */

/*
 * Several constants.
 */

#define	CARD_TYPE_EX_10         1
#define	CARD_TYPE_EX_10_PLUS    2

/* Length of an ethernet address. */
#define	ETHER_ADDR_LEN	6
/* Default RAM size in board. */
#define	CARD_RAM_SIZE	0x8000
/* Number of I/O ports used. */
#define	EX_IOSIZE	16

/*
 * Intel EtherExpress Pro (i82595 based) registers
 */

/* Common registers to all banks. */

#define	CMD_REG		0
#define	REG1		1
#define	REG2		2
#define	REG3		3
#define	REG4		4
#define	REG5		5
#define	REG6		6
#define	REG7		7
#define	REG8		8
#define	REG9		9
#define	REG10		10
#define	REG11		11
#define	REG12		12
#define	REG13		13
#define	REG14		14
#define	REG15		15

/* Definitions for command register (CMD_REG). */

#define	Switch_Bank_CMD	0
#define	MC_Setup_CMD	3
#define	Transmit_CMD	4
#define	Diagnose_CMD	7
#define	Rcv_Enable_CMD	8
#define	Rcv_Stop	11
#define	Reset_CMD	14
#define	Resume_XMT_List_CMD 28
#define	Sel_Reset_CMD	30
#define	Abort		0x20
#define	Bank0_Sel	0x00
#define	Bank1_Sel	0x40
#define	Bank2_Sel	0x80

/* Bank 0 specific registers. */

#define	STATUS_REG	1
#define	ID_REG		2
#define	Id_Mask		0x2c
#define	Id_Sig		0x24
#define	Counter_bits	0xc0
#define	MASK_REG	3
#define	Exec_Int	0x08
#define	Tx_Int		0x04
#define	Rx_Int		0x02
#define	Rx_Stp_Int	0x01
#define	All_Int		0x0f
#define	RCV_BAR		4
#define	RCV_BAR_Lo	4
#define	RCV_BAR_Hi	5
#define	RCV_STOP_REG	6
#define	XMT_BAR		10
#define	HOST_ADDR_REG	12	/* 16-bit register */
#define	IO_PORT_REG	14	/* 16-bit register */

/* Bank 1 specific registers. */

#define	TriST_INT		0x80
#define	INT_NO_REG		2
#define	RCV_LOWER_LIMIT_REG	8
#define	RCV_UPPER_LIMIT_REG	9
#define	XMT_LOWER_LIMIT_REG	10
#define	XMT_UPPER_LIMIT_REG	11

/* Bank 2 specific registers. */

#define	Disc_Bad_Fr		0x80
#define	Tx_Chn_ErStp		0x40
#define	Tx_Chn_Int_Md		0x20
#define	Multi_IA		0x20
#define	No_SA_Ins		0x10
#define	RX_CRC_InMem		0x04
#define	Promisc_Mode		0x01
#define	BNC_bit			0x20
#define	TPE_bit			0x04
#define	I_ADDR_REG0		4
#define	EEPROM_REG		10
#define	Trnoff_Enable		0x10

/* EEPROM memory positions (16-bit wide). */

#define	EE_W0			0x00
# define EE_W0_PNP		0x0001
# define EE_W0_BUS16		0x0004
# define EE_W0_FLASH_ADDR_MASK	0x0038
# define EE_W0_FLASH_ADDR_SHIFT	3
# define EE_W0_AUTO_IO		0x0040
# define EE_W0_FLASH		0x0100
# define EE_W0_AUTO_NEG		0x0200
# define EE_W0_IO_MASK		0xFC00
# define EE_W0_IO_SHIFT		10

#define	EE_IRQ_No		1
#define	IRQ_No_Mask		0x07

#define	EE_W1			0x01
# define EE_W1_INT_SEL		0x0007
# define EE_W1_NO_LINK_INT	0x0008	/* Link Integrity Off		*/
# define EE_W1_NO_POLARITY	0x0010	/* Polarity Correction Off	*/
# define EE_W1_TPE_AUI		0x0020	/* 1 = TPE, 0 = AUI		*/
# define EE_W1_NO_JABBER_PREV	0x0040	/* Jabber prevention Off	*/
# define EE_W1_NO_AUTO_SELECT	0x0080	/* Auto Port Selection Off	*/
# define EE_W1_SMOUT		0x0100	/* SMout Pin Control 0= Input	*/
# define EE_W1_PROM		0x0200	/* Flash = 0, PROM = 1		*/
# define EE_W1_ALT_READY	0x2000	/* Alternate Ready, 0=normal	*/
# define EE_W1_FULL_DUPLEX	0x8000

#define	EE_W2			0x02
#define	EE_W3			0x03
#define	EE_W4			0x04

#define	EE_Eth_Addr_Lo		2
#define	EE_Eth_Addr_Mid		3
#define	EE_Eth_Addr_Hi		4

#define	EE_W5			0x05
# define EE_W5_BNC_TPE		0x0001	/* 0 = TPE, 1 = BNC		*/
# define EE_W5_BOOT_IPX		0x0002
# define EE_W5_BOOT_ODI		0x0004
# define EE_W5_BOOT_NDIS	(EE_W5_BOOT_IPX|EE_W5_BOOT_ODI)
# define EE_W5_NUM_CONN		0x0008	/* 0 = 2, 1 = 3			*/
# define EE_W5_NOFLASH		0x0010	/* No flash socket present	*/
# define EE_W5_PORT_TPE		0x0020	/* TPE present			*/
# define EE_W5_PORT_BNC		0x0040	/* BNC present			*/
# define EE_W5_PORT_AUI		0x0080	/* AUI present			*/
# define EE_W5_PWR_MGT		0x0100	/* Power Management		*/
# define EE_W5_CP		0x0200	/* COncurrent Processing	*/

#define	EE_W6			0x05
# define EE_W6_STEP_MASK	0x000F
# define EE_W6_BOARD_MASK	0xFFF0
# define EE_W6_BOARD_SHIFT	4

/* EEPROM serial interface. */

#define	EESK			0x01
#define	EECS			0x02
#define	EEDI			0x04
#define	EEDO			0x08
#define	EE_READ_CMD		(6 << 6)

/* Frame chain constants. */

/* Transmit header length (in board's ring buffer). */
#define	XMT_HEADER_LEN		8
#define	XMT_Chain_Point		4
#define	XMT_Byte_Count		6
#define	Done_bit		0x0080
#define	Ch_bit			0x8000

/* Transmit result bits. */
#define	No_Collisions_bits	0x000f
#define	TX_OK_bit		0x2000

/* Receive result bits. */
#define	RCV_Done		8
#define	RCV_OK_bit		0x2000
