/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 * $FreeBSD$
 */

#ifndef _NFS_NFSSVC_H_
#define _NFS_NFSSVC_H_

/*
 * Flags for nfssvc() system call.
 */
#define	NFSSVC_OLDNFSD	0x004
#define	NFSSVC_ADDSOCK	0x008
#define	NFSSVC_NFSD	0x010

/*
 * and ones for nfsv4.
 */
#define	NFSSVC_NOPUBLICFH	0x00000020
#define	NFSSVC_STABLERESTART	0x00000040
#define	NFSSVC_NFSDNFSD		0x00000080
#define	NFSSVC_NFSDADDSOCK	0x00000100
#define	NFSSVC_IDNAME		0x00000200
#define	NFSSVC_GSSDDELETEALL	0x00000400
#define	NFSSVC_GSSDADDPORT	0x00000800
#define	NFSSVC_NFSUSERDPORT	0x00001000
#define	NFSSVC_NFSUSERDDELPORT	0x00002000
#define	NFSSVC_V4ROOTEXPORT	0x00004000
#define	NFSSVC_ADMINREVOKE	0x00008000
#define	NFSSVC_DUMPCLIENTS	0x00010000
#define	NFSSVC_DUMPLOCKS	0x00020000
#define	NFSSVC_GSSDADDFIRST	0x00040000
#define	NFSSVC_PUBLICFH		0x00080000
#define	NFSSVC_NFSCBD		0x00100000
#define	NFSSVC_CBADDSOCK	0x00200000
#define	NFSSVC_GETSTATS		0x00400000
#define	NFSSVC_BACKUPSTABLE	0x00800000
#define	NFSSVC_ZEROCLTSTATS	0x01000000	/* modifier for GETSTATS */
#define	NFSSVC_ZEROSRVSTATS	0x02000000	/* modifier for GETSTATS */
#define	NFSSVC_SUSPENDNFSD	0x04000000
#define	NFSSVC_RESUMENFSD	0x08000000
#define	NFSSVC_DUMPMNTOPTS	0x10000000
#define	NFSSVC_NEWSTRUCT	0x20000000
#define	NFSSVC_FORCEDISM	0x40000000
#define	NFSSVC_PNFSDS		0x80000000

/* Argument structure for NFSSVC_DUMPMNTOPTS. */
struct nfscl_dumpmntopts {
	char	*ndmnt_fname;		/* File Name */
	size_t	ndmnt_blen;		/* Size of buffer */
	void	*ndmnt_buf;		/* and the buffer */
};

#endif /* _NFS_NFSSVC_H */
