/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Rick Macklem, University of Guelph
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef	_NFS_NFSCL_H
#define	_NFS_NFSCL_H

/*
 * Extra stuff for a NFSv4 nfsnode.
 * MALLOC'd to the correct length for the name and file handle.
 * n4_data has the file handle, followed by the file name.
 * The macro NFS4NODENAME() returns a pointer to the start of the
 * name.
 */
struct nfsv4node {
	u_int16_t	n4_fhlen;
	u_int16_t	n4_namelen;
	u_int8_t	n4_data[1];
};

#define	NFS4NODENAME(n)	(&((n)->n4_data[(n)->n4_fhlen]))

/*
 * Just a macro to convert the nfscl_reqstart arguments.
 */
#define	NFSCL_REQSTART(n, p, v) 					\
	nfscl_reqstart((n), (p), VFSTONFS((v)->v_mount), 		\
	    VTONFS(v)->n_fhp->nfh_fh, VTONFS(v)->n_fhp->nfh_len, NULL,	\
	    NULL, 0, 0)

/*
 * These two macros convert between a lease duration and renew interval.
 * For now, just make the renew interval 1/2 the lease duration.
 * (They should be inverse operators.)
 */
#define	NFSCL_RENEW(l)	(((l) < 2) ? 1 : ((l) / 2))
#define	NFSCL_LEASE(r)	((r) * 2)

/* This macro checks to see if a forced dismount is about to occur. */
#define	NFSCL_FORCEDISM(m)	(((m)->mnt_kern_flag & MNTK_UNMOUNTF) != 0 || \
    (VFSTONFS(m)->nm_privflag & NFSMNTP_FORCEDISM) != 0)

/*
 * These flag bits are used for the argument to nfscl_fillsattr() to
 * indicate special handling of the attributes.
 */
#define	NFSSATTR_FULL		0x1
#define	NFSSATTR_SIZE0		0x2
#define	NFSSATTR_SIZENEG1	0x4
#define	NFSSATTR_SIZERDEV	0x8

/* Use this macro for debug printfs. */
#define	NFSCL_DEBUG(level, ...)	do {					\
		if (nfscl_debuglevel >= (level))			\
			printf(__VA_ARGS__);				\
	} while (0)

#endif	/* _NFS_NFSCL_H */
