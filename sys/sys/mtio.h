/*	$OpenBSD: mtio.h,v 1.9 2007/06/01 18:44:48 krw Exp $	*/
/*	$NetBSD: mtio.h,v 1.14 1997/04/15 06:50:19 lukem Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)mtio.h	8.1 (Berkeley) 6/2/93
 */

#ifndef _SYS_MTIO_H_
#define _SYS_MTIO_H_

/*
 * Structures and definitions for mag tape io control commands
 */

/* structure for MTIOCTOP - mag tape op command */
struct mtop {
	short	mt_op;		/* operations defined below */
	int	mt_count;	/* how many of them */
};

/* operations */
#define MTWEOF		0	/* write an end-of-file record */
#define MTFSF		1	/* forward space file */
#define MTBSF		2	/* backward space file */
#define MTFSR		3	/* forward space record */
#define MTBSR		4	/* backward space record */
#define MTREW		5	/* rewind */
#define MTOFFL		6	/* rewind and put the drive offline */
#define MTNOP		7	/* no operation, sets status only */
#define MTRETEN		8	/* retension */
#define MTERASE		9	/* erase entire tape */
#define MTEOM		10	/* forward to end of media */
#define MTNBSF		11	/* backward space to beginning of file */
#define MTCACHE		12	/* enable controller cache */
#define MTNOCACHE	13	/* disable controller cache */
#define MTSETBSIZ	14	/* set block size; 0 for variable */
#define MTSETDNSTY	15	/* set density code for current mode */

/* structure for MTIOCGET - mag tape get status command */

struct mtget {
	short	mt_type;	/* type of magtape device */
/* the following two registers are grossly device dependent */
	short	mt_dsreg;	/* ``drive status'' register */
	short	mt_erreg;	/* ``error'' register */
/* end device-dependent registers */
	short	mt_resid;	/* residual count */
	int	mt_fileno;	/* current file number relative to BOT. */ 
	int	mt_blkno;	/* current block number relative to BOF. */
	int	mt_blksiz;	/* current block size */
	int	mt_density;	/* current density code */
	int	mt_mblksiz;	/* default block size */
	int	mt_mdensity;	/* default density code */
};

/*
 * Constants for mt_type byte.  These are the same
 * for controllers compatible with the types listed.
 */
#define	MT_ISTS		0x01		/* TS-11 */
#define	MT_ISHT		0x02		/* TM03 Massbus: TE16, TU45, TU77 */
#define	MT_ISTM		0x03		/* TM11/TE10 Unibus */
#define	MT_ISMT		0x04		/* TM78/TU78 Massbus */
#define	MT_ISUT		0x05		/* SI TU-45 emulation on Unibus */
#define	MT_ISCPC	0x06		/* SUN */
#define	MT_ISAR		0x07		/* SUN */
#define	MT_ISTMSCP	0x08		/* DEC TMSCP protocol (TU81, TK50) */
#define MT_ISCY		0x09		/* CCI Cipher */
#define MT_ISCT		0x0a		/* HP 1/4 tape */
#define MT_ISFHP	0x0b		/* HP 7980 1/2 tape */
#define MT_ISEXABYTE	0x0c		/* Exabyte */
#define MT_ISEXA8200	0x0c		/* Exabyte EXB-8200 */
#define MT_ISEXA8500	0x0d		/* Exabyte EXB-8500 */
#define MT_ISVIPER1	0x0e		/* Archive Viper-150 */
#define MT_ISPYTHON	0x0f		/* Archive Python (DAT) */
#define MT_ISHPDAT	0x10		/* HP 35450A DAT drive */
#define MT_ISWANGTEK	0x11		/* WANGTEK 5150ES */
#define MT_ISCALIPER	0x12		/* Caliper CP150 */
#define MT_ISWTEK5099	0x13		/* WANGTEK 5099ES */
#define MT_ISVIPER2525	0x14		/* Archive Viper 2525 */
#define MT_ISMFOUR	0x11		/* M4 Data 1/2 9track drive */
#define MT_ISTK50	0x12		/* DEC SCSI TK50 */
#define MT_ISMT02	0x13		/* Emulex MT02 SCSI tape controller */

/* bits defined for the mt_dsreg field */
#define MT_DS_RDONLY	0x10		/* tape mounted readonly */
#define MT_DS_MOUNTED	0x03		/* tape mounted (for control opens) */

/* mag tape io control commands */
#define	MTIOCTOP	_IOW('m', 1, struct mtop)	/* do a mag tape op */
#define	MTIOCGET	_IOR('m', 2, struct mtget)	/* get tape status */
#define MTIOCIEOT	_IO('m', 3)			/* ignore EOT error */
#define MTIOCEEOT	_IO('m', 4)			/* enable EOT error */

/*
 * When more SCSI-3 SSC (streaming device) devices are out there
 * that support the full 32 byte type 2 structure, we'll have to
 * rethink these ioctls to support all the entities they haul into
 * the picture (64 bit blocks, logical file record numbers, etc..).
 */
#define MTIOCRDSPOS	_IOR('m', 5, u_int32_t)	/* get logical blk addr */
#define MTIOCRDHPOS	_IOR('m', 6, u_int32_t)	/* get hardware blk addr */
#define MTIOCSLOCATE	_IOW('m', 5, u_int32_t)	/* seek to logical blk addr */
#define MTIOCHLOCATE	_IOW('m', 6, u_int32_t)	/* seek to hardware blk addr */

#ifdef	_KERNEL
/*
 * minor device number
 */

#define	T_UNIT		003		/* unit selection */
#define	T_NOREWIND	004		/* no rewind on close */
#define	T_DENSEL	030		/* density select */
#define	T_800BPI	000		/* select  800 bpi */
#define	T_1600BPI	010		/* select 1600 bpi */
#define	T_6250BPI	020		/* select 6250 bpi */
#define	T_BADBPI	030		/* undefined selection */
#endif /* _KERNEL */

#endif /* !_SYS_MTIO_H_ */
