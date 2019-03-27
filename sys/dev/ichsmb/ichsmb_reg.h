/*-
 * ichsmb_reg.h
 *
 * Copyright (c) 2000 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $FreeBSD$
 */

#ifndef _DEV_ICHSMB_ICHSMB_REG_H_
#define _DEV_ICHSMB_ICHSMB_REG_H_

/*
 * Definitions for the SMBus controller logical device which is part of the
 * Intel 81801AA (ICH) and 81801AB (ICH0) I/O controller hub chips.
 */

/*
 * PCI configuration registers
 */
#define	ICH_SMB_BASE			0x20	/* base address register */
#define	ICH_HOSTC			0x40	/* host config register */
#define	  ICH_HOSTC_I2C_EN		0x04	/*   enable i2c mode */
#define	  ICH_HOSTC_SMB_SMI_EN		0x02	/*   SMI# instead of irq */
#define	  ICH_HOSTC_HST_EN		0x01	/*   enable host cntrlr */

/*
 * I/O registers
 */
#define ICH_HST_STA			0x00	/* host status */
#define   ICH_HST_STA_BYTE_DONE_STS	0x80	/*   byte send/rec'd */
#define   ICH_HST_STA_INUSE_STS		0x40	/*   device access mutex */
#define   ICH_HST_STA_SMBALERT_STS	0x20	/*   SMBALERT# signal */
#define   ICH_HST_STA_FAILED		0x10	/*   failed bus transaction */
#define   ICH_HST_STA_BUS_ERR		0x08	/*   transaction collision */
#define   ICH_HST_STA_DEV_ERR		0x04	/*   misc. smb device error */
#define   ICH_HST_STA_INTR		0x02	/*   command completed ok */
#define   ICH_HST_STA_HOST_BUSY		0x01	/*   command is running */
#define ICH_HST_CNT			0x02	/* host control */
#define   ICH_HST_CNT_START		0x40	/*   start command */
#define   ICH_HST_CNT_LAST_BYTE		0x20	/*   indicate last byte */
#define   ICH_HST_CNT_SMB_CMD_QUICK	0x00	/*   command: quick */
#define   ICH_HST_CNT_SMB_CMD_BYTE	0x04	/*   command: byte */
#define   ICH_HST_CNT_SMB_CMD_BYTE_DATA	0x08	/*   command: byte data */
#define   ICH_HST_CNT_SMB_CMD_WORD_DATA	0x0c	/*   command: word data */
#define   ICH_HST_CNT_SMB_CMD_PROC_CALL	0x10	/*   command: process call */
#define   ICH_HST_CNT_SMB_CMD_BLOCK	0x14	/*   command: block */
#define   ICH_HST_CNT_SMB_CMD_I2C_READ	0x18	/*   command: i2c read */
#define   ICH_HST_CNT_KILL		0x02	/*   kill current transaction */
#define   ICH_HST_CNT_INTREN		0x01	/*   enable interrupt */
#define ICH_HST_CMD			0x03	/* host command */
#define ICH_XMIT_SLVA			0x04	/* transmit slave address */
#define   ICH_XMIT_SLVA_READ		0x01	/*   direction: read */
#define   ICH_XMIT_SLVA_WRITE		0x00	/*   direction: write */
#define ICH_D0				0x05	/* host data 0 */
#define ICH_D1				0x06	/* host data 1 */
#define ICH_BLOCK_DB			0x07	/* block data byte */

#endif /* _DEV_ICHSMB_ICHSMB_REG_H_ */

