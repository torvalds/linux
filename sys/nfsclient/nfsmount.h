/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
 *	@(#)nfsmount.h	8.3 (Berkeley) 3/30/95
 * $FreeBSD$
 */

#ifndef _NFSCLIENT_NFSMOUNT_H_
#define _NFSCLIENT_NFSMOUNT_H_

#include <sys/socket.h>

#include <nfs/nfs_mountcommon.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpcsec_gss.h>

/*
 * Mount structure.
 * One allocated on every NFS mount.
 * Holds NFS specific information for mount.
 */
struct	nfsmount {
	struct	nfsmount_common nm_com;	/* Common fields for nlm */
	int	nm_numgrps;		/* Max. size of groupslist */
	u_char	nm_fh[NFSX_V4FH];	/* File handle of root dir */
	int	nm_fhsize;		/* Size of root file handle */
	int	nm_sotype;		/* Type of socket */
	int	nm_soproto;		/* and protocol */
	int	nm_soflags;		/* pr_flags for socket protocol */
	struct	sockaddr *nm_nam;	/* Addr of server */
	int	nm_deadthresh;		/* Threshold of timeouts-->dead server*/
	int	nm_rsize;		/* Max size of read rpc */
	int	nm_wsize;		/* Max size of write rpc */
	int	nm_readdirsize;		/* Size of a readdir rpc */
	int	nm_readahead;		/* Num. of blocks to readahead */
	int	nm_wcommitsize;		/* Max size of commit for write */
	int	nm_acdirmin;		/* Directory attr cache min lifetime */
	int	nm_acdirmax;		/* Directory attr cache max lifetime */
	int	nm_acregmin;		/* Reg file attr cache min lifetime */
	int	nm_acregmax;		/* Reg file attr cache max lifetime */
	u_char	nm_verf[NFSX_V3WRITEVERF]; /* V3 write verifier */
	TAILQ_HEAD(, buf) nm_bufq;	/* async io buffer queue */
	short	nm_bufqlen;		/* number of buffers in queue */
	short	nm_bufqwant;		/* process wants to add to the queue */
	int	nm_bufqiods;		/* number of iods processing queue */
	u_int64_t nm_maxfilesize;	/* maximum file size */
	struct nfs_rpcops *nm_rpcops;
	int	nm_tprintf_initial_delay;	/* initial delay */
	int	nm_tprintf_delay;		/* interval for messages */
	int	nm_secflavor;		 /* auth flavor to use for rpc */
	struct __rpc_client *nm_client;
	struct rpc_timers nm_timers[NFS_MAX_TIMER]; /* RTT Timers for rpcs */
	char	nm_principal[MNAMELEN];	/* GSS-API principal of server */
	gss_OID	nm_mech_oid;		/* OID of selected GSS-API mechanism */
	int	nm_nametimeo;		/* timeout for +ve entries (sec) */
	int	nm_negnametimeo;	/* timeout for -ve entries (sec) */

	/* NFSv4 */
	uint64_t nm_clientid;
	fsid_t	nm_fsid;
	u_int	nm_lease_time;
	time_t	nm_last_renewal;
};

#define	nm_mtx		nm_com.nmcom_mtx
#define	nm_flag		nm_com.nmcom_flag
#define	nm_state	nm_com.nmcom_state
#define	nm_mountp	nm_com.nmcom_mountp
#define	nm_timeo	nm_com.nmcom_timeo
#define	nm_retry	nm_com.nmcom_retry
#define	nm_hostname	nm_com.nmcom_hostname
#define	nm_getinfo	nm_com.nmcom_getinfo
#define	nm_vinvalbuf	nm_com.nmcom_vinvalbuf

#if defined(_KERNEL)
/*
 * Convert mount ptr to nfsmount ptr.
 */
#define VFSTONFS(mp)	((struct nfsmount *)((mp)->mnt_data))

#ifndef NFS_TPRINTF_INITIAL_DELAY
#define NFS_TPRINTF_INITIAL_DELAY       12
#endif

#ifndef NFS_TPRINTF_DELAY
#define NFS_TPRINTF_DELAY               30
#endif

#ifndef NFS_DEFAULT_NAMETIMEO
#define NFS_DEFAULT_NAMETIMEO		60
#endif

#ifndef NFS_DEFAULT_NEGNAMETIMEO
#define NFS_DEFAULT_NEGNAMETIMEO	60
#endif

#endif

#endif
