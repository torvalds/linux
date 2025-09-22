/*	$OpenBSD: nfsdiskless.h,v 1.10 2013/09/20 23:51:44 fgsch Exp $	*/
/*	$NetBSD: nfsdiskless.h,v 1.9 1996/02/18 11:54:00 fvdl Exp $	*/

/*
 * Copyright (c) 1991, 1993
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
 *	@(#)nfsdiskless.h	8.2 (Berkeley) 3/30/95
 */

#ifndef _NFS_DISKLESS_H_
#define _NFS_DISKLESS_H_

/*
 * Structure that must be initialized for a diskless nfs client.
 * This structure is used by nfs_mountroot() to set up the root and swap
 * vnodes plus do a partial ifconfig(8) and route(8) so that the critical
 * net interface can communicate with the server.
 * Whether or not the swap area is nfs mounted is determined
 * by the value in swdevt[0]. (equal to NODEV --> swap over nfs)
 * Currently only works for AF_INET protocols.
 * NB: All fields are stored in net byte order to avoid hassles with
 * client/server byte ordering differences.
 */
struct nfs_dlmount {
	struct nfs_args ndm_args;
	struct sockaddr_in ndm_saddr;  		/* Address of file server */
	char		ndm_host[MNAMELEN]; 	/* Host name for mount pt */
	u_char		ndm_fh[NFSX_V3FHMAX]; 	/* The file's file handle */
};
struct nfs_diskless {
	struct sockaddr_in nd_boot;	/* Address of boot server */
	struct nfs_dlmount nd_root; 	/* Mount info for root */
	struct nfs_dlmount nd_swap; 	/* Mount info for swap */
	struct vnode	   *sw_vp;
};

int nfs_boot_init(struct nfs_diskless *nd, struct proc *procp);
int nfs_boot_getfh(struct sockaddr_in *bpsin, char *key,
		struct nfs_dlmount *ndmntp, int retries);
#endif	/* _NFS_DISKLESS_H_ */

