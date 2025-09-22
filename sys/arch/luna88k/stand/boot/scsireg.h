/*	$OpenBSD: scsireg.h,v 1.2 2013/10/29 21:49:07 miod Exp $	*/
/*	$NetBSD: scsireg.h,v 1.2 2013/01/22 15:48:40 tsutsui Exp $	*/

/*
 * Copyright (c) 1990, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
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
 *	@(#)scsireg.h	8.1 (Berkeley) 6/10/93
 */

/*
 * MB89352 SCSI Protocol Controller Hardware Description.
 */

struct scsidevice {
	volatile u_char	scsi_bdid, p0, p1, p2;		/* 000 */
	volatile u_char	scsi_sctl, p3, p4, p5;		/* 004 */
#define			SCTL_DISABLE	0x80
#define			SCTL_CTRLRST	0x40
#define			SCTL_DIAG	0x20
#define			SCTL_ABRT_ENAB	0x10
#define			SCTL_PARITY_ENAB 0x08
#define			SCTL_SEL_ENAB	0x04
#define			SCTL_RESEL_ENAB	0x02
#define			SCTL_INTR_ENAB	0x01
	volatile u_char	scsi_scmd, p6, p7, p8;		/* 008 */
#define			SCMD_RST	0x10
#define			SCMD_ICPT_XFR	0x08
#define			SCMD_PROG_XFR	0x04
#define			SCMD_PAD	0x01	/* if initiator */
#define			SCMD_PERR_STOP	0x01	/* if target */
			/* command codes */
#define			SCMD_BUS_REL	0x00
#define			SCMD_SELECT	0x20
#define			SCMD_RST_ATN	0x40
#define			SCMD_SET_ATN	0x60
#define			SCMD_XFR	0x80
#define			SCMD_XFR_PAUSE	0xa0
#define			SCMD_RST_ACK	0xc0
#define			SCMD_SET_ACK	0xe0
	volatile u_char	scsi_tmod, p9, p10, p11;	/* 00C */
#define			TMOD_SYNC	0x80
	volatile u_char	scsi_ints, p12, p13, p14;	/* 010 */
#define			INTS_SEL	0x80
#define			INTS_RESEL	0x40
#define			INTS_DISCON	0x20
#define			INTS_CMD_DONE	0x10
#define			INTS_SRV_REQ	0x08
#define			INTS_TIMEOUT	0x04
#define			INTS_HARD_ERR	0x02
#define			INTS_RST	0x01
	volatile u_char	scsi_psns, p15, p16, p17;	/* 014 */
#define			PSNS_REQ	0x80
#define			PSNS_ACK	0x40
#define			PSNS_ATN	0x20
#define			PSNS_SEL	0x10
#define			PSNS_BSY	0x08
#define		scsi_sdgc scsi_psns
#define			SDGC_XFER_ENAB	0x20
	volatile u_char	scsi_ssts, p18, p19, p20;	/* 018 */
#define			SSTS_INITIATOR	0x80
#define			SSTS_TARGET	0x40
#define			SSTS_BUSY	0x20
#define			SSTS_XFR	0x10
#define			SSTS_ACTIVE	(SSTS_INITIATOR|SSTS_XFR)
#define			SSTS_RST	0x08
#define			SSTS_TCZERO	0x04
#define			SSTS_DREG_FULL	0x02
#define			SSTS_DREG_EMPTY	0x01
	volatile u_char	scsi_serr, p21, p22, p23;	/* 01C */
#define			SERR_SCSI_PAR	0x80
#define			SERR_SPC_PAR	0x40
#define			SERR_XFER_OUT	0x20
#define			SERR_TC_PAR	0x08
#define			SERR_PHASE_ERR	0x04
#define			SERR_SHORT_XFR	0x02
#define			SERR_OFFSET	0x01
	volatile u_char	scsi_pctl, p24, p25, p26;	/* 020 */
#define			PCTL_BFINT_ENAB	0x80
	volatile u_char	scsi_mbc,  p27, p28, p29;	/* 024 */
	volatile u_char	scsi_dreg, p30, p31, p32;	/* 028 */
	volatile u_char	scsi_temp, p33, p34, p35;	/* 02C */
	volatile u_char	scsi_tch,  p36, p37, p38;	/* 030 */
	volatile u_char	scsi_tcm,  p39, p40, p41;	/* 034 */
	volatile u_char	scsi_tcl,  p42, p43, p44;	/* 038 */
	volatile u_char	scsi_exbf, p45, p46, p47;	/* 03C */
};

/* psns/pctl phase lines as bits */
#define	PHASE_MSG	0x04
#define	PHASE_CD	0x02		/* =1 if 'command' */
#define	PHASE_IO	0x01		/* =1 if data inbound */
/* Phase lines as values */
#define	PHASE		0x07		/* mask for psns/pctl phase */
#define	DATA_OUT_PHASE	0x00
#define	DATA_IN_PHASE	0x01
#define	CMD_PHASE	0x02
#define	STATUS_PHASE	0x03
#define	BUS_FREE_PHASE	0x04
#define	ARB_SEL_PHASE	0x05	/* Fuji chip combines arbitration with sel. */
#define	MESG_OUT_PHASE	0x06
#define	MESG_IN_PHASE	0x07

/* SCSI Messages */

#define	MSG_CMD_COMPLETE	0x00
#define MSG_EXT_MESSAGE		0x01
#define	MSG_SAVE_DATA_PTR	0x02
#define	MSG_RESTORE_PTR		0x03
#define	MSG_DISCONNECT		0x04
#define	MSG_INIT_DETECT_ERROR	0x05
#define	MSG_ABORT		0x06
#define	MSG_REJECT		0x07
#define	MSG_NOOP		0x08
#define	MSG_PARITY_ERROR	0x09
#define	MSG_BUS_DEVICE_RESET	0x0C
#define	MSG_IDENTIFY		0x80
#define	MSG_IDENTIFY_DR		0xc0	/* (disconnect/reconnect allowed) */
#define	MSG_SYNC_REQ 		0x01

/* SCSI Commands */

#define CMD_TEST_UNIT_READY	0x00
#define CMD_REQUEST_SENSE	0x03
#define	CMD_INQUIRY		0x12
#define CMD_SEND_DIAGNOSTIC	0x1D

#define CMD_REWIND		0x01
#define CMD_FORMAT_UNIT		0x04
#define CMD_READ_BLOCK_LIMITS	0x05
#define CMD_REASSIGN_BLOCKS	0x07
#define CMD_READ		0x08
#define CMD_WRITE		0x0A
#define CMD_WRITE_FILEMARK	0x10
#define CMD_SPACE		0x11
#define CMD_MODE_SELECT		0x15
#define CMD_RELEASE_UNIT	0x17
#define CMD_ERASE		0x19
#define CMD_MODE_SENSE		0x1A
#define CMD_LOADUNLOAD		0x1B
#define CMD_RECEIVE_DIAG	0x1C
#define CMD_SEND_DIAG		0x1D
#define CMD_P_A_MEDIA_REMOVAL	0x1E
#define CMD_READ_CAPACITY	0x25
#define CMD_READ_EXT		0x28
#define CMD_WRITE_EXT		0x2A
#define CMD_READ_DEFECT_DATA	0x37
#define		SD_MANUFAC_DEFECTS	0x14000000
#define		SD_GROWN_DEFECTS	0x0c000000
#define CMD_READ_BUFFER		0x3B
#define CMD_WRITE_BUFFER	0x3C
#define CMD_READ_FULL		0xF0
#define CMD_MEDIA_TEST		0xF1
#define CMD_ACCESS_LOG		0xF2
#define CMD_WRITE_FULL		0xFC
#define CMD_MANAGE_PRIMARY	0xFD
#define CMD_EXECUTE_DATA	0xFE

/* SCSI status bits */

#define	STS_CHECKCOND	0x02	/* Check Condition (ie., read sense) */
#define	STS_CONDMET	0x04	/* Condition Met (ie., search worked) */
#define	STS_BUSY	0x08
#define	STS_INTERMED	0x10	/* Intermediate status sent */
#define	STS_EXT		0x80	/* Extended status valid */

/* command descriptor blocks */

struct scsi_cdb6 {
	u_char	cmd;		/* command code */
	u_char	lun:  3,	/* logical unit on ctlr */
		lbah: 5;	/* msb of read/write logical block addr */
	u_char	lbam;		/* middle byte of l.b.a. */
	u_char	lbal;		/* lsb of l.b.a. */
	u_char	len;		/* transfer length */
	u_char	xtra;
};

struct scsi_cdb10 {
	u_char	cmd;		/* command code */
	u_char	lun: 3,		/* logical unit on ctlr */
		   : 4,
		rel: 1;		/* l.b.a. is relative addr if =1 */
	u_char	lbah;		/* msb of read/write logical block addr */
	u_char	lbahm;		/* high middle byte of l.b.a. */
	u_char	lbalm;		/* low middle byte of l.b.a. */
	u_char	lbal;		/* lsb of l.b.a. */
	u_char	reserved;
	u_char	lenh;		/* msb transfer length */
	u_char	lenl;		/* lsb transfer length */
	u_char	xtra;
};

/* basic sense data */

struct scsi_sense {
	u_char	valid: 1,	/* l.b.a. is valid */
		class: 3,
		code:  4;
	u_char	vu:    4,	/* vendor unique */
		lbah:  4;
	u_char	lbam;
	u_char	lbal;
};

struct scsi_xsense {
	u_char	valid: 1,	/* l.b.a. is valid */
		class: 3,
		code:  4;
	u_char	segment;
	u_char	filemark: 1,
		eom:      1,
		ili:      1,	/* illegal length indicator */
		rsvd:	  1,
		key:	  4;
	u_char	info1;
	u_char	info2;
	u_char	info3;
	u_char	info4;
	u_char	len;		/* additional sense length */
};

/* inquiry data */
struct scsi_inquiry {
	u_char	type;
	u_char	qual;
	u_char	version;
	u_char	rsvd;
	u_char	len;
	char	class[3];
	char	vendor_id[8];
	char	product_id[16];
	char	rev[4];
};

struct scsi_generic_cdb {
	int len;		/* cdb length (in bytes) */
	u_char cdb[28];		/* cdb to use on next read/write */
};
