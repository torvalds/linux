/*	$OpenBSD: uhareg.h,v 1.5 2010/06/30 19:06:16 mk Exp $	*/
/*	$NetBSD: uhareg.h,v 1.2 1996/09/01 00:54:41 mycroft Exp $	*/

/*
 * Copyright (c) 1994, 1996 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * Ported for use with the UltraStor 14f by Gary Close (gclose@wvnvms.wvnet.edu)
 * Slight fixes to timeouts to run with the 34F
 * Thanks to Julian Elischer for advice and help with this port.
 *
 * Originally written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems for use under the MACH(2.5) operating system.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * commenced: Sun Sep 27 18:14:01 PDT 1992
 * slight mod to make work with 34F as well: Wed Jun  2 18:05:48 WST 1993
 */

typedef u_long physaddr;
typedef u_long physlen;

/************************** board definitions *******************************/
/*
 * I/O Port Interface
 */
#define U14_LMASK		0x0000	/* local doorbell mask reg */
#define U14_LINT		0x0001	/* local doorbell int/stat reg */
#define U14_SMASK		0x0002	/* system doorbell mask reg */
#define U14_SINT		0x0003	/* system doorbell int/stat reg */
#define U14_ID			0x0004	/* product id reg (2 ports) */
#define U14_CONFIG		0x0006	/* config reg (2 ports) */
#define U14_OGMPTR		0x0008	/* outgoing mail ptr (4 ports) */
#define U14_ICMPTR		0x000c	/* incoming mail ptr (4 ports) */

#define	U24_CONFIG		0x0005	/* config reg (3 ports) */
#define	U24_LMASK		0x000c	/* local doorbell mask reg */
#define	U24_LINT		0x000d	/* local doorbell int/stat reg */
#define	U24_SMASK		0x000e	/* system doorbell mask reg */
#define	U24_SINT		0x000f	/* system doorbell int/stat reg */
#define	U24_OGMCMD		0x0016	/* outgoing commands */
#define	U24_OGMPTR		0x0017	/* outgoing mail ptr (4 ports) */
#define	U24_ICMCMD		0x001b	/* incoming commands */
#define	U24_ICMPTR		0x001c	/* incoming mail ptr (4 ports) */

/*
 * UHA_LMASK bits (read only)
 */
#define UHA_LDIE		0x80	/* local doorbell int enabled */
#define UHA_SRSTE		0x40	/* soft reset enabled */
#define UHA_ABORTEN		0x10	/* abort MSCP enabled */
#define UHA_OGMINTEN		0x01	/* outgoing mail interrupt enabled */

/*
 * UHA_LINT bits (read only)
 */
#define U14_LDIP		0x80	/* local doorbell int pending */
#define	U24_LDIP		0x02	/* local doorbell int pending */

/*
 * UHA_LINT bits (write only)
 */
#define U14_OGMFULL		0x01	/* outgoing mailbox is full */
#define U14_ABORT		0x10	/* abort MSCP */

#define	U24_OGMFULL		0x02	/* outgoing mailbox is full */

#define	UHA_SBRST		0x40	/* scsi bus reset */
#define	UHA_ADRST		0x80	/* adapter soft reset */
#define	UHA_ASRST		0xc0	/* adapter and scsi reset */

/*
 * UHA_SMASK bits (read/write)
 */
#define UHA_ENSINT		0x80	/* enable system doorbell interrupt */
#define UHA_EN_ABORT_COMPLETE   0x10	/* enable abort MSCP complete int */
#define UHA_ENICM		0x01	/* enable ICM interrupt */

/*
 * UHA_SINT bits (read)
 */
#define U14_SDIP		0x80	/* system doorbell int pending */
#define	U24_SDIP		0x02	/* system doorbell int pending */

#define UHA_ABORT_SUCC		0x10	/* abort MSCP successful */
#define UHA_ABORT_FAIL		0x18	/* abort MSCP failed */

/*
 * UHA_SINT bits (write)
 */
#define U14_ICM_ACK		0x01	/* acknowledge ICM and clear */
#define	U24_ICM_ACK		0x02	/* acknowledge ICM and clear */

#define	UHA_ABORT_ACK		0x18	/* acknowledge status and clear */

/*
 * U14_CONFIG bits (read only)
 */
#define U14_DMA_CH5		0x0000	/* DMA channel 5 */
#define U14_DMA_CH6		0x4000	/* 6 */
#define U14_DMA_CH7		0x8000	/* 7 */
#define	U14_DMA_MASK		0xc000
#define U14_IRQ15		0x0000	/* IRQ 15 */
#define U14_IRQ14		0x1000	/* 14 */
#define U14_IRQ11		0x2000	/* 11 */
#define U14_IRQ10		0x3000	/* 10 */
#define	U14_IRQ_MASK		0x3000
#define	U14_HOSTID_MASK		0x0007

/*
 * U24_CONFIG bits (read only)
 */
#define	U24_MAGIC1		0x08
#define	U24_IRQ15		0x10
#define	U24_IRQ14		0x20
#define	U24_IRQ11		0x40
#define	U24_IRQ10		0x80
#define	U24_IRQ_MASK		0xf0

#define	U24_MAGIC2		0x04

#define	U24_HOSTID_MASK		0x07

/*
 * EISA registers (offset from slot base)
 */
#define	EISA_VENDOR		0x0c80	/* vendor ID (2 ports) */
#define	EISA_MODEL		0x0c82	/* model number (2 ports) */
#define	EISA_CONTROL		0x0c84
#define	 EISA_RESET		0x04
#define	 EISA_ERROR		0x02
#define	 EISA_ENABLE		0x01

/*
 * host_stat error codes
 */
#define UHA_NO_ERR		0x00	/* No error supposedly */
#define UHA_SBUS_ABORT_ERR	0x84	/* scsi bus abort error */
#define UHA_SBUS_TIMEOUT	0x91	/* scsi bus selection timeout */
#define UHA_SBUS_OVER_UNDER	0x92	/* scsi bus over/underrun */
#define UHA_BAD_SCSI_CMD	0x96	/* illegal scsi command */
#define UHA_AUTO_SENSE_ERR	0x9b	/* auto request sense err */
#define UHA_SBUS_RES_ERR	0xa3	/* scsi bus reset error */
#define UHA_BAD_SG_LIST		0xff	/* invalid scatter gath list */

#define UHA_NSEG	33	/* number of dma segments supported */

struct uha_dma_seg {
	physaddr seg_addr;
	physlen seg_len;
};

struct uha_mscp {
	u_char opcode:3;
#define UHA_HAC		0x01	/* host adapter command */
#define UHA_TSP		0x02	/* target scsi pass through command */
#define UHA_SDR		0x04	/* scsi device reset */
	u_char xdir:2;		/* xfer direction */
#define UHA_SDET	0x00	/* determined by scsi command */
#define UHA_SDIN	0x01	/* scsi data in */
#define UHA_SDOUT	0x02	/* scsi data out */
#define UHA_NODATA	0x03	/* no data xfer */
	u_char dcn:1;		/* disable disconnect for this command */
	u_char ca:1;		/* cache control */
	u_char sgth:1;		/* scatter gather flag */
	u_char target:3;
	u_char chan:2;		/* scsi channel (always 0 for 14f) */
	u_char lun:3;
	physaddr data_addr;
	physlen data_length;
	physaddr link_addr;
	u_char link_id;
	u_char sg_num;		/* number of scat gath segs */
	/*in s-g list if sg flag is */
	/*set. starts at 1, 8bytes per */
	u_char req_sense_length;
	u_char scsi_cmd_length;
	struct scsi_generic scsi_cmd;
	u_char host_stat;
	u_char target_stat;
	physaddr sense_ptr;	/* if 0 no auto sense */

	struct uha_dma_seg uha_dma[UHA_NSEG];
	struct scsi_sense_data mscp_sense;
	/*-----------------end of hardware supported fields----------------*/
	SLIST_ENTRY(uha_mscp) chain;
	struct uha_mscp *nexthash;
	long hashkey;
	struct scsi_xfer *xs;	/* the scsi_xfer for this cmd */
	int flags;
#define MSCP_ALLOC	0x01
#define MSCP_ABORT	0x02
	int timeout;
} __packed;

