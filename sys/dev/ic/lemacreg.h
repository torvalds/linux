/*      $OpenBSD: lemacreg.h,v 1.3 2022/01/09 05:42:38 jsg Exp $ */
/*      $NetBSD: lemacreg.h,v 1.2 2001/06/13 10:46:03 wiz Exp $ */

/*
 * Copyright (c) 1994, 1995, 1997 Matt Thomas <matt@3am-software.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _LEMAC_H_
#define	_LEMAC_H_

#define	LEMAC_IOBASE_LOW	0x200
#define	LEMAC_IOBASE_HIGH	0x400

/*
 * This is list of registers used on a DEC EtherWORKS III card.
 * Each board occupies a 32 byte register space.  This can be
 * in either EISA or ISA space.  Currently we only support ISA
 * space.
 */

#define	LEMAC_REG_CS		0x00	/* Control and Status */
#define LEMAC_REG_CTL		0x01	/* Control */
#define LEMAC_REG_IC 		0x02	/* Interrupt Control */
#define LEMAC_REG_TS 		0x03	/* Transmit Status */
#define	LEMAC_REG_RSVD1		0x04	/* Reserved (not used) */
#define	LEMAC_REG_RSVD2		0x05	/* Reserved (not used) */
#define LEMAC_REG_FMQ		0x06	/* Free Memory Queue */
#define LEMAC_REG_FMC		0x07	/* Free Memory Queue Count */
#define LEMAC_REG_RQ		0x08	/* Receive Queue */
#define LEMAC_REG_RQC		0x09	/* Receive Queue Count */
#define LEMAC_REG_TQ		0x0A	/* Transmit Queue */
#define LEMAC_REG_TQC		0x0B	/* Transmit Queue Count */
#define LEMAC_REG_TDQ		0x0C	/* Transmit Done Queue */
#define LEMAC_REG_TDC		0x0D	/* Transmit Done Queue Count */
#define LEMAC_REG_PI1		0x0E	/* Page Index #1 */
#define LEMAC_REG_PI2		0x0F	/* Page Index #2 */
#define LEMAC_REG_DAT		0x10	/* Data */
#define LEMAC_REG_IOP		0x11	/* I/O Page */
#define LEMAC_REG_IOB		0x12	/* I/O Base */
#define LEMAC_REG_MPN		0x13	/* Memory Page */
#define LEMAC_REG_MBR		0x14	/* Memory Base */
#define LEMAC_REG_APD		0x15	/* Address PROM */
#define LEMAC_REG_EE1		0x16	/* EEPROM Data #1 */
#define LEMAC_REG_EE2		0x17	/* EEPROM Data #2 */
#define LEMAC_REG_PA0		0x18	/* Physical Address (Byte 0) */
#define LEMAC_REG_PA1		0x19	/* Physical Address (Byte 1) */
#define LEMAC_REG_PA2		0x1A	/* Physical Address (Byte 2) */
#define LEMAC_REG_PA3		0x1B	/* Physical Address (Byte 3) */
#define LEMAC_REG_PA4		0x1C	/* Physical Address (Byte 4) */
#define LEMAC_REG_PA5		0x1D	/* Physical Address (Byte 5) */
#define LEMAC_REG_CNF		0x1E	/* Configuration Management */
#define	LEMAC_IOSIZE		0x20	/* LEMAC uses 32 bytes of IOSPACE */


#define LEMAC_REG_EID0		0x80	/* EISA Identification 0 */
#define LEMAC_REG_EID1		0x81	/* EISA Identification 1 */
#define LEMAC_REG_EID2		0x82	/* EISA Identification 2 */
#define LEMAC_REG_EID3		0x83	/* EISA Identification 3 */
#define LEMAC_REG_EIC		0x84	/* EISA Control */

/* Control Page (Page 0) Definitions */

#define	LEMAC_MCTBL_BITS	9
#define	LEMAC_MCTBL_OFF		512
#define	LEMAC_MCTBL_SIZE	(1 << (LEMAC_MCTBL_BITS - 3))
#define	LEMAC_CRC32_POLY	0xEDB88320UL	/* CRC-32 Poly -- Little Endian) */

/* EEPROM Definitions */

#define	LEMAC_EEP_CKSUM		0	/* The valid checksum is 0 */
#define	LEMAC_EEP_SIZE		32	/* EEPROM is 32 bytes */
#define	LEMAC_EEP_DELAY		2000	/* 2ms = 2000us */
#define	LEMAC_EEP_PRDNM		8	/* Product Name Offset */
#define	LEMAC_EEP_PRDNMSZ	8	/* Product Name Size */
#define	LEMAC_EEP_SWFLAGS	16	/* Software Options Offset */
#define	LEMAC_EEP_SETUP		23	/* Setup Options Offset */

#define	LEMAC_EEP_SW_SQE	0x10	/* Enable TX_SQE on Transmits */
#define	LEMAC_EEP_SW_LAB	0x08	/* Enable TX_LAB on Transmits */
#define	LEMAC_EEP_ST_DRAM	0x02	/* Enable extra DRAM */

#define	LEMAC_ADP_ROMSZ		32	/* Size of Address PROM */

/* Receive Status Definitions */

#define	LEMAC_RX_PLL		0x01	/* Phase Lock Lost */
#define	LEMAC_RX_CRC		0x02	/* CRC Error */
#define	LEMAC_RX_DBE		0x04	/* Dribble Bit Error */
#define	LEMAC_RX_MCM		0x08	/* Multicast Match */
#define	LEMAC_RX_IAM		0x10	/* Individual Address Match */
#define	LEMAC_RX_OK		0x80	/* No Errors */

/* Transmit Status Definitions (not valid if TXD == 0) */

#define	LEMAC_TS_RTRYMSK	0x0F	/* Retries of last TX PDU */
#define	LEMAC_TS_ECL		0x10	/* Excessive collision of ... */
#define	LEMAC_TS_LCL		0x20	/* Late collision of ... */
#define	LEMAC_TS_ID		0x40	/* Initially Deferred  ... */
#define	LEMAC_TS_NCL		0x08	/* No carrier loopback ... */

/* Transmit Control Definitions */

#define LEMAC_TX_ISA		0x01	/* Insert Source Address (no) */
#define LEMAC_TX_IFC		0x02	/* Insert Frame Check (yes) */
#define LEMAC_TX_PAD		0x04	/* Zero PAD to minimum length (yes) */
#define LEMAC_TX_LAB		0x08	/* Less Aggressive Backoff (no) */
#define LEMAC_TX_QMD		0x10	/* Q-Mode (yes) */
#define LEMAC_TX_STP		0x20	/* Stop on Error (no) */
#define LEMAC_TX_SQE		0x40	/* SQE Enable (yes) */

#define	LEMAC_TX_FLAGS		(LEMAC_TX_IFC|LEMAC_TX_PAD|LEMAC_TX_QMD|\
				 LEMAC_TX_SQE)
#define	LEMAC_TX_HDRSZ		4	/* Size of TX header */

/* Transmit Done Queue Status Definitions */

#define	LEMAC_TDQ_COL		0x03	/* Collision Mask */ 
#define	LEMAC_TDQ_NOCOL		0x00	/*   No Collisions */
#define	LEMAC_TDQ_ONECOL	0x01	/*   One Collision */
#define	LEMAC_TDQ_MULCOL	0x02	/*   Multiple Collisions */
#define	LEMAC_TDQ_EXCCOL	0x03	/*   Excessive Collisions */
#define	LEMAC_TDQ_ID		0x04	/* Initially Deferred */
#define	LEMAC_TDQ_LCL		0x08	/* Late Collision (will TX_STP) */
#define	LEMAC_TDQ_NCL		0x10	/* No carrier loopback */
#define	LEMAC_TDQ_SQE		0x20	/* SQE error */

/* Control / Status Definitions */

#define	LEMAC_CS_RXD		0x01	/* Receiver Disabled */
#define	LEMAC_CS_TXD		0x02	/* Transmitter Disabled */
#define	LEMAC_CS_RNE		0x04	/* Receive Queue Not Empty */
#define	LEMAC_CS_TNE		0x08	/* Transmit Done Queue Not Empty */
#define	LEMAC_CS_MBZ4		0x10	/* MBZ */
#define	LEMAC_CS_MCE		0x20	/* Multicast Enable */
#define	LEMAC_CS_PME		0x40	/* Promiscuous Mode Enable */
#define	LEMAC_CS_RA		0x80	/* Runt Accept */

/* Control Definitions */

#define	LEMAC_CTL_LED		0x02	/* LED state (inverted) */
#define	LEMAC_CTL_PSL		0x40	/* Port Select (1=AUI, 0=UTP) */
#define	LEMAC_CTL_APD		0x80	/* Auto Port Disable */

/* Interrupt Control Definitions */

#define	LEMAC_IC_RXD		0x01	/* Enable RXD Interrupt */
#define	LEMAC_IC_TXD		0x02	/* Enable TXD Interrupt */
#define	LEMAC_IC_RNE		0x04	/* Enable RNE Interrupt */
#define	LEMAC_IC_TNE		0x08	/* Enable TNE Interrupt */
#define	LEMAC_IC_ALL		0x0F	/* Enable RXD,TXD,RNE,TNE */
#define	LEMAC_IC_IRQMSK		0x60	/* Interrupt Select */
#define	LEMAC_IC_IRQ5		0x00	/*   Select IRQ 5 */
#define	LEMAC_IC_IRQ10		0x20	/*   Select IRQ 10 */
#define	LEMAC_IC_IRQ11		0x40	/*   Select IRQ 11 */
#define	LEMAC_IC_IRQ15		0x60	/*   Select IRQ 15 */
#define	LEMAC_IC_IE		0x80	/* Interrupt Enable */

/* I/O Page Definitions */

#define	LEMAC_IOP_EEINIT	0xC0	/* Perform a board init/reset */
#define	LEMAC_IOP_EEREAD	0xE0	/* Start a read from EEPROM */

/* Configuration / Management Definitions */

#define	LEMAC_CNF_DRAM		0x02	/* Extra on-board DRAM is available */
#define	LEMAC_CNF_NOLINK	0x20	/* UTP port is UP */

#endif	/* _LEMAC_H_ */
