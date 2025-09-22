/*      $OpenBSD: wdcreg.h,v 1.16 2006/05/07 21:15:47 miod Exp $     */
/*	$NetBSD: wdcreg.h,v 1.22 1999/03/07 14:02:54 bouyer Exp $	*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)wdreg.h	7.1 (Berkeley) 5/9/91
 */

#ifndef _DEV_IC_WDCREG_H_
#define _DEV_IC_WDCREG_H_

/*
 * Controller register (wdr_ctlr)
 */
#define WDCTL_4BIT	0x08	/* use four head bits (wd1003) */
#define WDCTL_RST	0x04	/* reset the controller */
#define WDCTL_IDS	0x02	/* disable controller interrupts */

/*
 * Status bits.
 */
#define WDCS_BSY	0x80	/* busy */
#define WDCS_DRDY	0x40	/* drive ready */
#define WDCS_DWF	0x20	/* drive write fault */
#define WDCS_DSC	0x10	/* drive seek complete */
#define WDCS_DRQ	0x08	/* data request */
#define WDCS_CORR	0x04	/* corrected data */
#define WDCS_IDX	0x02	/* index */
#define WDCS_ERR	0x01	/* error */
#define WDCS_BITS	"\020\010BSY\007DRDY\006DWF\005DSC\004DRQ\003CORR\002IDX\001ERR"

/*
 * Error bits.
 */
#define WDCE_BBK	0x80	/* bad block detected */
#define WDCE_CRC	0x80	/* CRC error (Ultra-DMA only) */
#define WDCE_UNC	0x40	/* uncorrectable data error */
#define WDCE_MC		0x20	/* media changed */
#define WDCE_IDNF	0x10	/* id not found */
#define WDCE_MCR	0x08	/* media change requested */
#define WDCE_ABRT	0x04	/* aborted command */
#define WDCE_TK0NF	0x02	/* track 0 not found */
#define WDCE_AMNF	0x01	/* address mark not found */

/*
 * Commands for Disk Controller.
 */
#define WDCC_NOP	0x00	/* NOP - Always fail with "aborted command" */
#define WDCC_RECAL	0x10	/* disk restore code -- resets cntlr */

#define WDCC_READ	0x20	/* disk read code */
#define WDCC_WRITE	0x30	/* disk write code */
#define WDCC__LONG	0x02	/* modifier -- access ecc bytes */
#define WDCC__NORETRY	0x01	/* modifier -- no retries */

#define WDCC_FORMAT	0x50	/* disk format code */
#define WDCC_DIAGNOSE	0x90	/* controller diagnostic */
#define WDCC_IDP	0x91	/* initialize drive parameters */

#define WDCC_READMULTI	0xc4	/* read multiple */
#define WDCC_WRITEMULTI	0xc5	/* write multiple */
#define WDCC_SETMULTI	0xc6	/* set multiple mode */

#define WDCC_READDMA	0xc8	/* read with DMA */
#define WDCC_WRITEDMA	0xca	/* write with DMA */

#define WDCC_ACKMC	0xdb	/* acknowledge media change */
#define WDCC_LOCK	0xde	/* lock drawer */
#define WDCC_UNLOCK	0xdf	/* unlock drawer */

#define WDCC_FLUSHCACHE	0xe7	/* Flush cache */
#define WDCC_IDENTIFY	0xec	/* read parameters from controller */
#define SET_FEATURES	0xef	/* set features */

#define WDCC_IDLE	0xe3	/* set idle timer & enter idle mode */
#define WDCC_IDLE_IMMED	0xe1	/* enter idle mode */
#define WDCC_SLEEP	0xe6	/* enter sleep mode */
#define WDCC_STANDBY	0xe2	/* set standby timer & enter standby mode */
#define WDCC_STANDBY_IMMED 0xe0	/* enter standby mode */
#define WDCC_CHECK_PWR	0xe5	/* check power mode */

#define WDCC_READ_EXT		0x24 /* read 48-bit addressing */
#define WDCC_WRITE_EXT		0x34 /* write 48-bit addressing */

#define WDCC_READMULTI_EXT	0x29 /* read multiple 48-bit addressing */
#define WDCC_WRITEMULTI_EXT	0x39 /* write multiple 48-bit addressing */

#define WDCC_READDMA_EXT	0x25 /* read 48-bit addressing with DMA */
#define WDCC_WRITEDMA_EXT	0x35 /* write 48-bit addressing with DMA */

#define WDCC_FLUSHCACHE_EXT	0xea /* 48-bit addressing flush cache */

/* security mode commands */
#define WDCC_SEC_SET_PASSWORD		0xf1 /* set user or master password */
#define WDCC_SEC_UNLOCK			0xf2 /* authenticate */
#define WDCC_SEC_ERASE_PREPARE		0xf3 
#define WDCC_SEC_ERASE_UNIT		0xf4 /* erase all user data */
#define WDCC_SEC_FREEZE_LOCK		0xf5 /* prevent password changes */
#define WDCC_SEC_DISABLE_PASSWORD	0xf6

/* Subcommands for SET_FEATURES (features register ) */
#define WDSF_8BIT_PIO_EN	0x01 /* Enable 8bit PIO (CFA featureset) */
#define WDSF_EN_WR_CACHE	0x02
#define WDSF_SET_MODE		0x03
#define WDSF_REASSIGN_EN	0x04 /* Obsolete in ATA-6 */
#define WDSF_APM_EN		0x05 /* Enable Adv. Power Management */
#define WDSF_PUIS_EN		0x06 /* Enable Power-Up In Standby */
#define WDSF_PUIS_SPINUP	0x07 /* Power-Up In Standby spin-up */
#define WDSF_CFA_MODE1_EN	0x0A /* Enable CFA power mode 1 */
#define WDSF_RMSN_DS		0x31 /* Disable Removable Media Status */
#define WDSF_RETRY_DS		0x33 /* Obsolete in ATA-6 */
#define WDSF_AAM_EN		0x42 /* Enable Autom. Acoustic Management */
#define WDSF_SET_CACHE_SGMT	0x54 /* Obsolete in ATA-6 */
#define WDSF_READAHEAD_DS	0x55 /* Disable read look-ahead */
#define WDSF_RLSE_EN		0x5D /* Enable release interrupt */
#define WDSF_SRV_EN		0x5E /* Enable SERVICE interrupt */
#define WDSF_POD_DS		0x66
#define WDSF_ECC_DS		0x77
#define WDSF_8BIT_PIO_DS	0x81 /* Disable 8bit PIO (CFA featureset) */
#define WDSF_WRITE_CACHE_DS	0x82
#define WDSF_REASSIGN_DS	0x84
#define WDSF_APM_DS		0x85 /* Disable Adv. Power Management */
#define WDSF_PUIS_DS		0x86 /* Disable Power-Up In Standby */
#define WDSF_ECC_EN		0x88
#define WDSF_CFA_MODE1_DS	0x8A /* Disable CFA power mode 1 */
#define WDSF_RMSN_EN		0x95 /* Enable Removable Media Status */
#define WDSF_RETRY_EN		0x99 /* Obsolete in ATA-6 */
#define WDSF_SET_CURRENT	0x9A /* Obsolete in ATA-6 */
#define WDSF_READAHEAD_EN	0xAA
#define WDSF_PREFETCH_SET	0xAB /* Obsolete in ATA-6 */
#define WDSF_AAM_DS		0xC2 /* Disable Autom. Acoustic Management */
#define WDSF_POD_EN		0xCC
#define WDSF_RLSE_DS		0xDD /* Disable release interrupt */
#define WDSF_SRV_DS		0xDE /* Disable SERVICE interrupt */

/* parameters uploaded to device/heads register */
#define WDSD_IBM	0xa0	/* forced to 512 byte sector, ecc */
#define WDSD_CHS	0x00	/* cylinder/head/sector addressing */
#define WDSD_LBA	0x40	/* logical block addressing */

/* Commands for ATAPI devices */
#define ATAPI_CHECK_POWER_MODE	0xe5
#define ATAPI_EXEC_DRIVE_DIAGS	0x90
#define ATAPI_IDLE_IMMEDIATE	0xe1
#define ATAPI_NOP		0x00
#define ATAPI_PKT_CMD		0xa0
#define ATAPI_IDENTIFY_DEVICE	0xa1
#define ATAPI_SOFT_RESET	0x08
#define ATAPI_DEVICE_RESET	0x08 /* ATA/ATAPI-5 name for soft reset */
#define ATAPI_SLEEP		0xe6
#define ATAPI_STANDBY_IMMEDIATE	0xe0
#define ATAPI_SMART		0xB0 /* SMART operations */
#define ATAPI_SETMAX		0xF9 /* Set Max Address */
#define ATAPI_WRITEEXT		0x34 /* Write sectors Ext */
#define ATAPI_SETMAXEXT		0x37 /* Set Max Address Ext */
#define ATAPI_WRITEMULTIEXT	0x39 /* Write Multi Ext */

/* Bytes used by ATAPI_PACKET_COMMAND ( feature register) */
#define ATAPI_PKT_CMD_FTRE_DMA	0x01
#define ATAPI_PKT_CMD_FTRE_OVL	0x02

/* ireason */
#define WDCI_CMD	0x01 /* command(1) or data(0) */
#define WDCI_IN		0x02 /* transfer to(1) or from(0) the host */
#define WDCI_RELEASE	0x04 /* bus released until completion */

#define PHASE_CMDOUT	(WDCS_DRQ | WDCI_CMD)
#define PHASE_DATAIN	(WDCS_DRQ | WDCI_IN)
#define PHASE_DATAOUT	WDCS_DRQ
#define PHASE_COMPLETED	(WDCI_IN | WDCI_CMD)
#define PHASE_ABORTED	0

#endif	/* !_DEV_IC_WDCREG_H_ */
