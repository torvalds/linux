/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)iso_rrip.h	8.2 (Berkeley) 1/23/94
 * $FreeBSD$
 */


/*
 *	Analyze function flag (similar to RR field bits)
 */
#define	ISO_SUSP_ATTR		0x0001
#define	ISO_SUSP_DEVICE		0x0002
#define	ISO_SUSP_SLINK		0x0004
#define	ISO_SUSP_ALTNAME	0x0008
#define	ISO_SUSP_CLINK		0x0010
#define	ISO_SUSP_PLINK		0x0020
#define	ISO_SUSP_RELDIR		0x0040
#define	ISO_SUSP_TSTAMP		0x0080
#define	ISO_SUSP_IDFLAG		0x0100
#define	ISO_SUSP_EXTREF		0x0200
#define	ISO_SUSP_CONT		0x0400
#define	ISO_SUSP_OFFSET		0x0800
#define	ISO_SUSP_STOP		0x1000
#define	ISO_SUSP_UNKNOWN	0x8000

#ifdef _KERNEL
typedef struct {
	struct iso_node	*inop;
	int		fields;		/* interesting fields in this analysis */
	daddr_t		iso_ce_blk;	/* block of continuation area */
	off_t		iso_ce_off;	/* offset of continuation area */
	int		iso_ce_len;	/* length of continuation area */
	struct iso_mnt	*imp;		/* mount structure */
	cd_ino_t	*inump;		/* inode number pointer */
	char		*outbuf;	/* name/symbolic link output area */
	u_short		*outlen;	/* length of above */
	u_short		maxlen;		/* maximum length of above */
	int		cont;		/* continuation of above */
} ISO_RRIP_ANALYZE;

struct iso_directory_record;

int cd9660_rrip_analyze(struct iso_directory_record *isodir,
			    struct iso_node *inop, struct iso_mnt *imp);
int cd9660_rrip_getname(struct iso_directory_record *isodir,
			    char *outbuf, u_short *outlen,
			    cd_ino_t *inump, struct iso_mnt *imp);
int cd9660_rrip_getsymname(struct iso_directory_record *isodir,
			       char *outbuf, u_short *outlen,
			       struct iso_mnt *imp);
int cd9660_rrip_offset(struct iso_directory_record *isodir,
			   struct iso_mnt *imp);
#endif /* _KERNEL */
