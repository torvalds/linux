/*	$OpenBSD: piixreg.h,v 1.6 2020/01/21 06:37:24 claudio Exp $	*/

/*
 * Copyright (c) 2005 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _DEV_PCI_PIIXREG_H_
#define _DEV_PCI_PIIXREG_H_

/*
 * Intel PCI-to-ISA / IDE Xcelerator (PIIX) register definitions.
 */

/*
 * Power management registers.
 */

/* PCI configuration registers */
#define PIIX_SMB_BASE	0x90		/* SMBus base address */
#define PIIX_SMB_BASE_MASK	0xfffe
#define PIIX_SMB_HOSTC	0xd0		/* SMBus host configuration */
#define PIIX_SMB_HOSTC_HSTEN	(1 << 16)	/* enable host controller */
#define PIIX_SMB_HOSTC_SMI	(0 << 17)	/* SMI */
#define PIIX_SMB_HOSTC_IRQ	(4 << 17)	/* IRQ */
#define PIIX_SMB_HOSTC_INTMASK	(7 << 17)

/* SMBus I/O registers */
#define PIIX_SMB_HS	0x00		/* host status */
#define PIIX_SMB_HS_BUSY	(1 << 0)	/* running a command */
#define PIIX_SMB_HS_INTR	(1 << 1)	/* command completed */
#define PIIX_SMB_HS_DEVERR	(1 << 2)	/* command error */
#define PIIX_SMB_HS_BUSERR	(1 << 3)	/* transaction collision */
#define PIIX_SMB_HS_FAILED	(1 << 4)	/* failed bus transaction */
#define PIIX_SMB_HS_BITS	"\020\001BUSY\002INTR\003DEVERR\004BUSERR\005FAILED"
#define PIIX_SMB_HC	0x02		/* host control */
#define PIIX_SMB_HC_INTREN	(1 << 0)	/* enable interrupts */
#define PIIX_SMB_HC_KILL	(1 << 1)	/* kill current transaction */
#define PIIX_SMB_HC_CMD_QUICK	(0 << 2)	/* QUICK command */
#define PIIX_SMB_HC_CMD_BYTE	(1 << 2)	/* BYTE command */
#define PIIX_SMB_HC_CMD_BDATA	(2 << 2)	/* BYTE DATA command */
#define PIIX_SMB_HC_CMD_WDATA	(3 << 2)	/* WORD DATA command */
#define PIIX_SMB_HC_CMD_BLOCK	(5 << 2)	/* BLOCK command */
#define PIIX_SMB_HC_START	(1 << 6)	/* start transaction */
#define PIIX_SMB_HCMD	0x03		/* host command */
#define PIIX_SMB_TXSLVA	0x04		/* transmit slave address */
#define PIIX_SMB_TXSLVA_READ	(1 << 0)	/* read direction */
#define PIIX_SMB_TXSLVA_ADDR(x)	(((x) & 0x7f) << 1) /* 7-bit address */
#define PIIX_SMB_HD0	0x05		/* host data 0 */
#define PIIX_SMB_HD1	0x06		/* host data 1 */
#define PIIX_SMB_HBDB	0x07		/* host block data byte */
#define PIIX_SMB_SC	0x08		/* slave control */
#define PIIX_SMB_SC_ALERTEN	(1 << 3)	/* enable SMBALERT# */

#define PIIX_SMB_SIZE	0x10		/* SMBus I/O space size */

/* AMD SB800 configuration registers */
#define SB800_PMREG_BASE	0xcd6
#define SB800_PMREG_SIZE	2	/* index/data pair */
#define SB800_PMREG_SMB0EN	0x2c	/* 16-bit register */
#define SB800_PMREG_SMB0SEL	0x2e	/* bus selection */
#define SB800_PMREG_SMB0SELEN	0x2f	/* bus selection enable */
#define SB800_SMB0EN_EN		0x0001
#define SB800_SMB0EN_BASE_MASK	0xffe0
#define SB800_SMB0SELEN_EN	0x01

#define SB800_SMB_HOSTC	0x10		/* I2C bus configuration */
#define SB800_SMB_HOSTC_INTMASK	0x1	/* 0: SMI 1: IRQ */

#define SB800_SMB_SIZE	0x14		/* SMBus I/O space size */

/*
 * Newer FCH registers in the PMIO space.
 * See BKDG for Family 16h Models 30h-3Fh 3.26.13 PMx00 and PMx04.
 */
#define AMDFCH41_PM_DECODE_EN	0x00	/* 16-bit register */
#define AMDFCH41_PM_PORT_INDEX	0x02
#define AMDFCH41_SMBUS_EN	0x10

#endif	/* !_DEV_PCI_PIIXREG_H_ */
