/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
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
 *	from: @(#)nec765.h	7.1 (Berkeley) 5/9/91
 * $FreeBSD$
 */

/*
 * Nec 765 floppy disc controller definitions
 */

/* Main status register */
#define NE7_DAB		0x01	/* Diskette drive A is seeking, thus busy */
#define NE7_DBB		0x02	/* Diskette drive B is seeking, thus busy */
#define NE7_CB		0x10	/* Diskette Controller Busy */
#define NE7_NDM		0x20	/* Diskette Controller in Non Dma Mode */
#define NE7_DIO		0x40	/* Diskette Controller Data register I/O */
#define NE7_RQM		0x80	/* Diskette Controller ReQuest for Master */

/* Status register ST0 */
#define NE7_ST0BITS	"\020\010invld\007abnrml\006seek_cmplt\005equ_chck\004drive_notrdy\003top_head"

#define NE7_ST0_IC	0xc0	/* interrupt completion code */

#define NE7_ST0_IC_RC	0xc0	/* terminated due to ready changed, n/a */
#define NE7_ST0_IC_IV	0x80	/* invalid command; must reset FDC */
#define NE7_ST0_IC_AT	0x40	/* abnormal termination, check error stat */
#define NE7_ST0_IC_NT	0x00	/* normal termination */

#define NE7_ST0_SE	0x20	/* seek end */
#define NE7_ST0_EC	0x10	/* equipment check, recalibrated but no trk0 */
#define NE7_ST0_NR	0x08	/* not ready (n/a) */
#define NE7_ST0_HD	0x04	/* upper head selected */
#define NE7_ST0_DR	0x03	/* drive code */

/* Status register ST1 */
#define NE7_ST1BITS	"\020\010end_of_cyl\006bad_crc\005data_overrun\003sec_not_fnd\002write_protect\001no_am"

#define NE7_ST1_EN	0x80	/* end of cylinder, access past last record */
#define NE7_ST1_DE	0x20	/* data error, CRC fail in ID or data */
#define NE7_ST1_OR	0x10	/* DMA overrun, DMA failed to do i/o quickly */
#define NE7_ST1_ND	0x04	/* no data, sector not found or CRC in ID f. */
#define NE7_ST1_NW	0x02	/* not writeable, attempt to violate WP */
#define NE7_ST1_MA	0x01	/* missing address mark (in ID or data field)*/

/* Status register ST2 */
#define NE7_ST2BITS	"\020\007ctrl_mrk\006bad_crc\005wrong_cyl\004scn_eq\003scn_not_fnd\002bad_cyl\001no_dam"

#define NE7_ST2_CM	0x40	/* control mark; found deleted data */
#define NE7_ST2_DD	0x20	/* data error in data field, CRC fail */
#define NE7_ST2_WC	0x10	/* wrong cylinder, ID field mismatches cmd */
#define NE7_ST2_SH	0x08	/* scan equal hit */
#define NE7_ST2_SN	0x04	/* scan not satisfied */
#define NE7_ST2_BC	0x02	/* bad cylinder, cylinder marked 0xff */
#define NE7_ST2_MD	0x01	/* missing address mark in data field */

/* Status register ST3 */
#define NE7_ST3BITS	"\020\010fault\007write_protect\006drdy\005tk0\004two_side\003side_sel\002"

#define NE7_ST3_FT	0x80	/* fault; PC: n/a */
#define NE7_ST3_WP	0x40	/* write protected */
#define NE7_ST3_RD	0x20	/* ready; PC: always true */
#define NE7_ST3_T0	0x10	/* track 0 */
#define NE7_ST3_TS	0x08	/* two-sided; PC: n/a */
#define NE7_ST3_HD	0x04	/* upper head select */
#define NE7_ST3_US	0x03	/* unit select */

/* Data Rate Select Register DSR (enhanced controller) */
#define I8207X_DSR_SR	0x80	/* software reset */
#define I8207X_DSR_LP	0x40	/* low power */
#define I8207X_DSR_PS	0x1c	/* precompensation select */
#define I8207X_DSR_RS	0x03	/* data rate select */

/* Commands */
/*
 * the top three bits -- where appropriate -- are set as follows:
 *
 * MT  - multi-track; allow both sides to be handled in single cmd
 * MFM - modified frequency modulation; use MFM encoding
 * SK  - skip; skip sectors marked as "deleted"
 */

#define NE7CMD_MT	0x80	/* READ, WRITE, WRITEDEL, READDEL, SCAN* */
#define NE7CMD_MFM	0x40	/* same as MT, plus READTRK, READID, FORMAT */
#define NE7CMD_SK	0x20	/* READ, READDEL, SCAN* */

#define NE7CMD_READTRK	0x02	/*  read whole track */
#define NE7CMD_SPECIFY	0x03	/*  specify drive parameters - requires unit
				 *  parameters byte */
#define NE7CMD_SENSED	0x04	/*  sense drive - requires unit select byte */
#define NE7CMD_WRITE	0x05	/*  write - requires eight additional bytes */
#define NE7CMD_READ	0x06	/*  read - requires eight additional bytes */
#define NE7CMD_RECAL	0x07	/*  recalibrate drive - requires
				 *  unit select byte */
#define NE7CMD_SENSEI	0x08	/*  sense controller interrupt status */
#define NE7CMD_WRITEDEL	0x09	/*  write deleted data */
#define NE7CMD_READID	0x0a	/*  read ID field */
#define NE7CMD_READDEL	0x0c	/*  read deleted data */
#define NE7CMD_FORMAT	0x0d	/*  format - requires five additional bytes */
#define NE7CMD_SEEK	0x0f	/*  seek drive - requires unit select byte
				 *  and new cyl byte */
#define NE7CMD_VERSION	0x10	/*  get version */
#define NE7CMD_SCNEQU	0x11	/*  scan equal */
#define NE7CMD_SCNLE	0x19	/*  scan less or equal */
#define NE7CMD_SCNGE	0x1d	/*  scan greater or equal */

/*
 * Enhanced controller commands:
 */
#define I8207X_DUMPREG	0x0e	/*  dump internal registers */
#define I8207X_CONFIG	0x13	/*  configure enhanced features */

/*
 * "specify" definitions
 *
 * acronyms (times are relative to a FDC clock of 8 MHz):
 * srt - step rate; PC usually 3 ms
 * hut - head unload time; PC usually maximum of 240 ms
 * hlt - head load time; PC usually minimum of 2 ms
 * nd  - no DMA flag; PC usually not set (0)
 */

#define NE7_SPEC_1(srt, hut)	(((16 - (srt)) << 4) | (((hut) / 16)))
#define NE7_SPEC_2(hlt, nd)	(((hlt) & 0xFE) | ((nd) & 1))
