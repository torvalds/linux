/*	$OpenBSD: ichreg.h,v 1.8 2022/01/09 05:42:46 jsg Exp $	*/

/*
 * Copyright (c) 2004, 2005 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEV_PCI_ICHREG_H_
#define _DEV_PCI_ICHREG_H_

/*
 * Intel I/O Controller Hub (ICH) register definitions.
 */

/*
 * LPC interface bridge registers.
 */

/* PCI configuration registers */
#define ICH_PMBASE	0x40		/* ACPI base address */
#define ICH_ACPI_CNTL	0x44		/* ACPI control */
#define ICH_ACPI_CNTL_ACPI_EN	(1 << 4)	/* ACPI enable */
#define ICH_GEN_PMCON1	0xa0		/* general PM configuration */
/* ICHx-M only */
#define ICH_GEN_PMCON1_SS_EN	0x08		/* enable SpeedStep */

/* Power management I/O registers */
#define ICH_PM_TMR	0x08		/* PM timer */
/* ICHx-M only */
#define ICH_PM_CNTL	0x20		/* power management control */
#define ICH_PM_ARB_DIS		0x01		/* disable arbiter */
#define ICH_PM_SS_CNTL	0x50		/* SpeedStep control */
#define ICH_PM_SS_STATE_LOW	0x01		/* low power state */

#define ICH_PMSIZE	128		/* ACPI I/O space size */

/*
 * SMBus controller registers.
 */

/* PCI configuration registers */
#define ICH_SMB_BASE	0x20		/* SMBus base address */
#define ICH_SMB_HOSTC	0x40		/* host configuration */
#define ICH_SMB_HOSTC_HSTEN	(1 << 0)	/* enable host controller */
#define ICH_SMB_HOSTC_SMIEN	(1 << 1)	/* generate SMI */
#define ICH_SMB_HOSTC_I2CEN	(1 << 2)	/* enable I2C commands */

/* SMBus I/O registers */
#define ICH_SMB_HS	0x00		/* host status */
#define ICH_SMB_HS_BUSY		(1 << 0)	/* running a command */
#define ICH_SMB_HS_INTR		(1 << 1)	/* command completed */
#define ICH_SMB_HS_DEVERR	(1 << 2)	/* command error */
#define ICH_SMB_HS_BUSERR	(1 << 3)	/* transaction collision */
#define ICH_SMB_HS_FAILED	(1 << 4)	/* failed bus transaction */
#define ICH_SMB_HS_SMBAL	(1 << 5)	/* SMBALERT# asserted */
#define ICH_SMB_HS_INUSE	(1 << 6)	/* bus semaphore */
#define ICH_SMB_HS_BDONE	(1 << 7)	/* byte received/transmitted */
#define ICH_SMB_HS_BITS		"\020\001BUSY\002INTR\003DEVERR\004BUSERR\005FAILED\006SMBAL\007INUSE\010BDONE"
#define ICH_SMB_HC	0x02		/* host control */
#define ICH_SMB_HC_INTREN	(1 << 0)	/* enable interrupts */
#define ICH_SMB_HC_KILL		(1 << 1)	/* kill current transaction */
#define ICH_SMB_HC_CMD_QUICK	(0 << 2)	/* QUICK command */
#define ICH_SMB_HC_CMD_BYTE	(1 << 2)	/* BYTE command */
#define ICH_SMB_HC_CMD_BDATA	(2 << 2)	/* BYTE DATA command */
#define ICH_SMB_HC_CMD_WDATA	(3 << 2)	/* WORD DATA command */
#define ICH_SMB_HC_CMD_PCALL	(4 << 2)	/* PROCESS CALL command */
#define ICH_SMB_HC_CMD_BLOCK	(5 << 2)	/* BLOCK command */
#define ICH_SMB_HC_CMD_I2CREAD	(6 << 2)	/* I2C READ command */
#define ICH_SMB_HC_CMD_BLOCKP	(7 << 2)	/* BLOCK PROCESS command */
#define ICH_SMB_HC_LASTB	(1 << 5)	/* last byte in block */
#define ICH_SMB_HC_START	(1 << 6)	/* start transaction */
#define ICH_SMB_HC_PECEN	(1 << 7)	/* enable PEC */
#define ICH_SMB_HCMD	0x03		/* host command */
#define ICH_SMB_TXSLVA	0x04		/* transmit slave address */
#define ICH_SMB_TXSLVA_READ	(1 << 0)	/* read direction */
#define ICH_SMB_TXSLVA_ADDR(x)	(((x) & 0x7f) << 1) /* 7-bit address */
#define ICH_SMB_HD0	0x05		/* host data 0 */
#define ICH_SMB_HD1	0x06		/* host data 1 */
#define ICH_SMB_HBDB	0x07		/* host block data byte */
#define ICH_SMB_PEC	0x08		/* PEC data */
#define ICH_SMB_RXSLVA	0x09		/* receive slave address */
#define ICH_SMB_SD	0x0a		/* receive slave data */
#define ICH_SMB_SD_MSG0(x)	((x) & 0xff)	/* data message byte 0 */
#define ICH_SMB_SD_MSG1(x)	((x) >> 8)	/* data message byte 1 */
#define ICH_SMB_AS	0x0c		/* auxiliary status */
#define ICH_SMB_AS_CRCE		(1 << 0)	/* CRC error */
#define ICH_SMB_AS_TCO		(1 << 1)	/* advanced TCO mode */
#define ICH_SMB_AC	0x0d		/* auxiliary control */
#define ICH_SMB_AC_AAC		(1 << 0)	/* automatically append CRC */
#define ICH_SMB_AC_E32B		(1 << 1)	/* enable 32-byte buffer */
#define ICH_SMB_SMLPC	0x0e		/* SMLink pin control */
#define ICH_SMB_SMLPC_LINK0	(1 << 0)	/* SMLINK0 pin state */
#define ICH_SMB_SMLPC_LINK1	(1 << 1)	/* SMLINK1 pin state */
#define ICH_SMB_SMLPC_CLKC	(1 << 2)	/* SMLINK0 pin is untouched */
#define ICH_SMB_SMBPC	0x0f		/* SMBus pin control */
#define ICH_SMB_SMBPC_CLK	(1 << 0)	/* SMBCLK pin state */
#define ICH_SMB_SMBPC_DATA	(1 << 1)	/* SMBDATA pin state */
#define ICH_SMB_SMBPC_CLKC	(1 << 2)	/* SMBCLK pin is untouched */
#define ICH_SMB_SS	0x10		/* slave status */
#define ICH_SMB_SS_HN		(1 << 0)	/* Host Notify command */
#define ICH_SMB_SCMD	0x11		/* slave command */
#define ICH_SMB_SCMD_INTREN	(1 << 0)	/* enable interrupts on HN */
#define ICH_SMB_SCMD_WKEN	(1 << 1)	/* wake on HN */
#define ICH_SMB_SCMD_SMBALDS	(1 << 2)	/* disable SMBALERT# intr */
#define ICH_SMB_NDADDR	0x14		/* notify device address */
#define ICH_SMB_NDADDR_ADDR(x)	((x) >> 1)	/* 7-bit address */
#define ICH_SMB_NDLOW	0x16		/* notify data low byte */
#define ICH_SMB_NDHIGH	0x17		/* notify data high byte */

/*
 * 6300ESB watchdog timer registers.
 */

/* PCI configuration registers */
#define ICH_WDT_BASE	0x10		/* memory space base address */
#define ICH_WDT_CONF	0x60		/* configuration register */
#define ICH_WDT_CONF_MASK	0xffff		/* 16-bit register */
#define ICH_WDT_CONF_INT_MASK	0x3		/* interrupt type */
#define ICH_WDT_CONF_INT_IRQ	0x0		/* IRQ (APIC 1, INT 10) */
#define ICH_WDT_CONF_INT_SMI	0x2		/* SMI */
#define ICH_WDT_CONF_INT_DIS	0x3		/* disabled */
#define ICH_WDT_CONF_PRE	(1 << 2)	/* 2^5 clock divisor */
#define ICH_WDT_CONF_OUTDIS	(1 << 5)	/* WDT_TOUT# output disabled */
#define ICH_WDT_LOCK	0x68		/* lock register */
#define ICH_WDT_LOCK_LOCKED	(1 << 0)	/* register locked */
#define ICH_WDT_LOCK_ENABLED	(1 << 1)	/* WDT enabled */
#define ICH_WDT_LOCK_FREERUN	(1 << 2)	/* free running mode */

/* Memory mapped registers */
#define ICH_WDT_PRE1	0x00		/* preload value 1 */
#define ICH_WDT_PRE2	0x04		/* preload value 2 */
#define ICH_WDT_GIS	0x08		/* general interrupt status */
#define ICH_WDT_GIS_ACTIVE	(1 << 0)	/* interrupt active */
#define ICH_WDT_RELOAD	0x0c		/* reload register */
#define ICH_WDT_RELOAD_RLD	(1 << 8)	/* safe reload */
#define ICH_WDT_RELOAD_TIMEOUT	(1 << 9)	/* timeout occurred */

#endif	/* !_DEV_PCI_ICHREG_H_ */
