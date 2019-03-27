/*-
 * Copyright (c) 1998, 1999 Takanori Watanabe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.    IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __INTPMREG_H__
#define	__INTPMREG_H__

/* Register definitions for non-ICH Intel Chipset SMBUS controllers. */

/* PCI Config Registers. */
#define	PCI_BASE_ADDR_SMB	0x90	/* IO BAR. */
#define	PCI_BASE_ADDR_PM	0x40
#define	PCI_HST_CFG_SMB		0xd2	/* Host Configuration */
#define	PCI_INTR_SMB_MASK	0xe
#define	PCI_INTR_SMB_SMI	0
#define	PCI_INTR_SMB_IRQ_PCI	2
#define	PCI_INTR_SMB_IRQ9	8
#define	PCI_INTR_SMB_ENABLE	1
#define	PCI_SLV_CMD_SMB		0xd3 /*SLAVE COMMAND*/
#define	PCI_SLV_SDW_SMB_1	0xd4 /*SLAVE SHADOW PORT 1*/
#define	PCI_SLV_SDW_SMB_2	0xd5 /*SLAVE SHADOW PORT 2*/
#define	PCI_REVID_SMB		0xd6

/* PIXX4 SMBus Registers in the SMB BAR. */
#define	PIIX4_SMBHSTSTS		0x00
#define	PIIX4_SMBHSTSTAT_BUSY	(1<<0)
#define	PIIX4_SMBHSTSTAT_INTR	(1<<1)
#define	PIIX4_SMBHSTSTAT_ERR	(1<<2)
#define	PIIX4_SMBHSTSTAT_BUSC	(1<<3)
#define	PIIX4_SMBHSTSTAT_FAIL	(1<<4)
#define	PIIX4_SMBSLVSTS		0x01
#define	PIIX4_SMBSLVSTS_ALART	(1<<5)
#define	PIIX4_SMBSLVSTS_SDW2	(1<<4)
#define	PIIX4_SMBSLVSTS_SDW1	(1<<3)
#define	PIIX4_SMBSLVSTS_SLV	(1<<2)
#define	PIIX4_SMBSLVSTS_BUSY	(1<<0)
#define	PIIX4_SMBHSTCNT		0x02
#define	PIIX4_SMBHSTCNT_START	(1<<6)
#define	PIIX4_SMBHSTCNT_PROT_QUICK	0
#define	PIIX4_SMBHSTCNT_PROT_BYTE	(1<<2)
#define	PIIX4_SMBHSTCNT_PROT_BDATA	(2<<2)
#define	PIIX4_SMBHSTCNT_PROT_WDATA	(3<<2)
#define	PIIX4_SMBHSTCNT_PROT_BLOCK	(5<<2)
#define	PIIX4_SMBHSTCNT_KILL	(1<<1)
#define	PIIX4_SMBHSTCNT_INTREN	(1)
#define	PIIX4_SMBHSTCMD		0x03
#define	PIIX4_SMBHSTADD		0x04
#define	LSB			0x1
#define	PIIX4_SMBHSTDAT0	0x05
#define	PIIX4_SMBHSTDAT1	0x06
#define	PIIX4_SMBBLKDAT		0x07
#define	PIIX4_SMBSLVCNT		0x08
#define	PIIX4_SMBSLVCNT_ALTEN	(1<<3)
#define	PIIX4_SMBSLVCNT_SD2EN	(1<<2)
#define	PIIX4_SMBSLVCNT_SD1EN	(1<<1)
#define	PIIX4_SMBSLVCNT_SLVEN	(1)
#define	PIIX4_SMBSLVCMD		0x09
#define	PIIX4_SMBSLVEVT		0x0a
#define	PIIX4_SMBSLVDAT		0x0c

/* SMBus alert response address. */
#define	SMBALTRESP		0x18

#define	SMBBLOCKTRANS_MAX	32

#endif /* !__INTPMREG_H__ */
