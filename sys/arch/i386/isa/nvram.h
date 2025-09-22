/*	$OpenBSD: nvram.h,v 1.7 2004/06/09 10:17:10 art Exp $	*/
/*	$NetBSD: nvram.h,v 1.5 1995/05/05 22:08:43 mycroft Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)rtc.h	8.1 (Berkeley) 6/11/93
 */

/*
 * The following information is found in the non-volatile RAM in the
 * MC146818A (or DS1287A or other compatible) RTC on AT-compatible PCs.
 */

/* NVRAM byte 0: bios diagnostic */
#define NVRAM_DIAG	(MC_NVRAM_START + 0)	/* RTC offset 0xe */

#define NVRAM_DIAG_BITS		"\020\010clock_battery\007ROM_cksum\006config_unit\005memory_size\004fixed_disk\003invalid_time"

/* NVRAM byte 1: reset code */
#define NVRAM_RESET	(MC_NVRAM_START + 1)	/* RTC offset 0xf */

#define	NVRAM_RESET_RST		0x00		/* normal reset */
#define	NVRAM_RESET_LOAD 	0x04		/* load system */
#define	NVRAM_RESET_JUMP	0x0a		/* jump through 40:67 */

/* NVRAM byte 2: diskette drive type in upper/lower nibble */
#define NVRAM_DISKETTE	(MC_NVRAM_START + 2)	/* RTC offset 0x10 */

#define	NVRAM_DISKETTE_NONE	 0		/* none present */
#define	NVRAM_DISKETTE_360K	 0x10		/* 360K */
#define	NVRAM_DISKETTE_12M	 0x20		/* 1.2M */
#define	NVRAM_DISKETTE_720K	 0x30		/* 720K */
#define	NVRAM_DISKETTE_144M	 0x40		/* 1.44M */
#define	NVRAM_DISKETTE_TYPE5	 0x50		/* 2.88M, presumably */
#define	NVRAM_DISKETTE_TYPE6	 0x60		/* 2.88M */

/* NVRAM byte 6: equipment type */
#define	NVRAM_EQUIPMENT	(MC_NVRAM_START + 6)

#define	NVRAM_EQUIPMENT_FLOPPY	0x01		/* floppy installed */
#define	NVRAM_EQUIPMENT_FPU	0x02		/* FPU installed */
#define	NVRAM_EQUIPMENT_KBD	0x04		/* keyboard installed */
#define	NVRAM_EQUIPMENT_DISPLAY	0x08		/* display installed */
#define	NVRAM_EQUIPMENT_EGAVGA	0x00		/* EGA or VGA */
#define	NVRAM_EQUIPMENT_COLOR40	0x10		/* 40 column color */
#define	NVRAM_EQUIPMENT_COLOR80	0x20		/* 80 column color */
#define	NVRAM_EQUIPMENT_MONO80	0x30		/* 80 column mono */
#define	NVRAM_EQUIPMENT_MONITOR	0x30		/* mask for monitor type */
#define	MVRAM_EQUIPMENT_NFDS	0xC0		/* mask for # of floppies */

/* NVRAM bytes 7 & 8: base memory size */
#define NVRAM_BASELO	(MC_NVRAM_START + 7)	/* low byte; RTC off. 0x15 */
#define NVRAM_BASEHI	(MC_NVRAM_START + 8)	/* high byte; RTC off. 0x16 */

/* NVRAM bytes 9 & 10: extended memory size */
#define NVRAM_EXTLO	(MC_NVRAM_START + 9)	/* low byte; RTC off. 0x17 */
#define NVRAM_EXTHI	(MC_NVRAM_START + 10)	/* high byte; RTC off. 0x18 */

/* NVRAM bytes 34 and 35: extended memory POSTed size */
#define NVRAM_PEXTLO	(MC_NVRAM_START + 34)	/* low byte; RTC off. 0x30 */
#define NVRAM_PEXTHI	(MC_NVRAM_START + 35)	/* high byte; RTC off. 0x31 */

/* NVRAM byte 36: current century.  (please increment in Dec99!) */
#define NVRAM_CENTURY	(MC_NVRAM_START + 36)	/* RTC offset 0x32 */
