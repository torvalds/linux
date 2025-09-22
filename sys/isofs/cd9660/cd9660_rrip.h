/*	$OpenBSD: cd9660_rrip.h,v 1.3 2003/06/02 23:28:05 millert Exp $	*/
/*	$NetBSD: cd9660_rrip.h,v 1.6 1994/12/13 22:33:24 mycroft Exp $	*/

/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
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
 *	@(#)cd9660_rrip.h	8.2 (Berkeley) 12/5/94
 */

typedef struct {
	char   type			[ISODCL (  0,    1)];
	u_char length			[ISODCL (  2,    2)]; /* 711 */
	u_char version			[ISODCL (  3,    3)];
} ISO_SUSP_HEADER;

typedef struct {
	ISO_SUSP_HEADER			h;
	char mode			[ISODCL (  4,   11)]; /* 733 */
	char links			[ISODCL ( 12,   19)]; /* 733 */
	char uid			[ISODCL ( 20,   27)]; /* 733 */
	char gid			[ISODCL ( 28,   35)]; /* 733 */
} ISO_RRIP_ATTR;

typedef struct {
	ISO_SUSP_HEADER			h;
	char dev_t_high			[ISODCL (  4,   11)]; /* 733 */
	char dev_t_low			[ISODCL ( 12,   19)]; /* 733 */
} ISO_RRIP_DEVICE;

#define	ISO_SUSP_CFLAG_CONTINUE	0x01
#define	ISO_SUSP_CFLAG_CURRENT	0x02
#define	ISO_SUSP_CFLAG_PARENT	0x04
#define	ISO_SUSP_CFLAG_ROOT	0x08
#define	ISO_SUSP_CFLAG_VOLROOT	0x10
#define	ISO_SUSP_CFLAG_HOST	0x20

typedef struct {
	u_char cflag			[ISODCL (  1,    1)];
	u_char clen			[ISODCL (  2,    2)];
	u_char name			[1];			/* XXX */
} ISO_RRIP_SLINK_COMPONENT;
#define	ISO_RRIP_SLSIZ	2

typedef struct {
	ISO_SUSP_HEADER			h;
	u_char flags			[ISODCL (  4,    4)];
	u_char component		[ISODCL (  5,    5)];
} ISO_RRIP_SLINK;

typedef struct {
	ISO_SUSP_HEADER			h;
	char flags			[ISODCL (  4,    4)];
} ISO_RRIP_ALTNAME;

typedef struct {
	ISO_SUSP_HEADER			h;
	char dir_loc			[ISODCL (  4,    11)]; /* 733 */
} ISO_RRIP_CLINK;

typedef struct {
	ISO_SUSP_HEADER			h;
	char dir_loc			[ISODCL (  4,    11)]; /* 733 */
} ISO_RRIP_PLINK;

typedef struct {
	ISO_SUSP_HEADER			h;
} ISO_RRIP_RELDIR;

#define	ISO_SUSP_TSTAMP_FORM17	0x80
#define	ISO_SUSP_TSTAMP_FORM7	0x00
#define	ISO_SUSP_TSTAMP_CREAT	0x01
#define	ISO_SUSP_TSTAMP_MODIFY	0x02
#define	ISO_SUSP_TSTAMP_ACCESS	0x04
#define	ISO_SUSP_TSTAMP_ATTR	0x08
#define	ISO_SUSP_TSTAMP_BACKUP	0x10
#define	ISO_SUSP_TSTAMP_EXPIRE	0x20
#define	ISO_SUSP_TSTAMP_EFFECT	0x40

typedef struct {
	ISO_SUSP_HEADER			h;
	u_char flags			[ISODCL (  4,    4)];
	u_char time			[ISODCL (  5,    5)];
} ISO_RRIP_TSTAMP;

typedef struct {
	ISO_SUSP_HEADER			h;
	u_char flags			[ISODCL (  4,    4)];
} ISO_RRIP_IDFLAG;

typedef struct {
	ISO_SUSP_HEADER			h;
	char len_id			[ISODCL (  4,    4)];
	char len_des			[ISODCL (  5,	 5)];
	char len_src			[ISODCL (  6,	 6)];
	char version			[ISODCL (  7,	 7)];
} ISO_RRIP_EXTREF;

typedef struct {
	ISO_SUSP_HEADER			h;
	char check			[ISODCL (  4,	 5)];
	char skip			[ISODCL (  6,	 6)];
} ISO_RRIP_OFFSET;

typedef struct {
	ISO_SUSP_HEADER			h;
	char location			[ISODCL (  4,	11)];
	char offset			[ISODCL ( 12,	19)];
	char length			[ISODCL ( 20,	27)];
} ISO_RRIP_CONT;
