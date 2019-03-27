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

#ifndef _NFS_MOUNTCOMMON_H_
#define	_NFS_MOUNTCOMMON_H_

/*
 * The common fields of the nfsmount structure for the two clients
 * used by the nlm. It includes a function pointer that provides
 * a mechanism for getting the client specific info for an nfs vnode.
 */
typedef void	nfs_getinfofromvp_ftype(struct vnode *, uint8_t *, size_t *,
		    struct sockaddr_storage *, int *, off_t *,
		    struct timeval *);
typedef int	nfs_vinvalbuf_ftype(struct vnode *, int, struct thread *, int);

struct	nfsmount_common {
	struct mtx	nmcom_mtx;
	int	nmcom_flag;		/* Flags for soft/hard... */
	int	nmcom_state;		/* Internal state flags */
	struct	mount *nmcom_mountp;	/* Vfs structure for this filesystem */
	int	nmcom_timeo;		/* Init timer for NFSMNT_DUMBTIMR */
	int	nmcom_retry;		/* Max retries */
	char	nmcom_hostname[MNAMELEN];	/* server's name */
	nfs_getinfofromvp_ftype	*nmcom_getinfo;	/* Get info from nfsnode */
	nfs_vinvalbuf_ftype	*nmcom_vinvalbuf; /* Invalidate buffers */
};

#endif	/* _NFS_MOUNTCOMMON_H_ */
