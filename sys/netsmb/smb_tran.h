/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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

#ifndef _NETSMB_SMB_TRAN_H_
#define	_NETSMB_SMB_TRAN_H_

#include <sys/socket.h>

/*
 * Known transports
 */
#define	SMBT_NBTCP	1

/*
 * Transport parameters
 */
#define	SMBTP_SNDSZ	1		/* R  - int */
#define	SMBTP_RCVSZ	2		/* R  - int */
#define	SMBTP_TIMEOUT	3		/* RW - struct timespec */
#define	SMBTP_SELECTID	4		/* RW - (void *) */
#define SMBTP_UPCALL	5		/* RW - (* void)(void *) */

struct smb_tran_ops;

struct smb_tran_desc {
	sa_family_t	tr_type;
	int	(*tr_create)(struct smb_vc *vcp, struct thread *td);
	int	(*tr_done)(struct smb_vc *vcp, struct thread *td);
	int	(*tr_bind)(struct smb_vc *vcp, struct sockaddr *sap, struct thread *td);
	int	(*tr_connect)(struct smb_vc *vcp, struct sockaddr *sap, struct thread *td);
	int	(*tr_disconnect)(struct smb_vc *vcp, struct thread *td);
	int	(*tr_send)(struct smb_vc *vcp, struct mbuf *m0, struct thread *td);
	int	(*tr_recv)(struct smb_vc *vcp, struct mbuf **mpp, struct thread *td);
	void	(*tr_timo)(struct smb_vc *vcp);
	void	(*tr_intr)(struct smb_vc *vcp);
	int	(*tr_getparam)(struct smb_vc *vcp, int param, void *data);
	int	(*tr_setparam)(struct smb_vc *vcp, int param, void *data);
	int	(*tr_fatal)(struct smb_vc *vcp, int error);
#ifdef notyet
	int	(*tr_poll)(struct smb_vc *vcp, struct thread *td);
	int	(*tr_cmpaddr)(void *addr1, void *addr2);
#endif
	LIST_ENTRY(smb_tran_desc)	tr_link;
};

#define SMB_TRAN_CREATE(vcp,p)		(vcp)->vc_tdesc->tr_create(vcp,p)
#define SMB_TRAN_DONE(vcp,p)		(vcp)->vc_tdesc->tr_done(vcp,p)
#define	SMB_TRAN_BIND(vcp,sap,p)	(vcp)->vc_tdesc->tr_bind(vcp,sap,p)
#define	SMB_TRAN_CONNECT(vcp,sap,p)	(vcp)->vc_tdesc->tr_connect(vcp,sap,p)
#define	SMB_TRAN_DISCONNECT(vcp,p)	(vcp)->vc_tdesc->tr_disconnect(vcp,p)
#define	SMB_TRAN_SEND(vcp,m0,p)		(vcp)->vc_tdesc->tr_send(vcp,m0,p)
#define	SMB_TRAN_RECV(vcp,m,p)		(vcp)->vc_tdesc->tr_recv(vcp,m,p)
#define	SMB_TRAN_TIMO(vcp)		(vcp)->vc_tdesc->tr_timo(vcp)
#define	SMB_TRAN_INTR(vcp)		(vcp)->vc_tdesc->tr_intr(vcp)
#define	SMB_TRAN_GETPARAM(vcp,par,data)	(vcp)->vc_tdesc->tr_getparam(vcp, par, data)
#define	SMB_TRAN_SETPARAM(vcp,par,data)	(vcp)->vc_tdesc->tr_setparam(vcp, par, data)
#define	SMB_TRAN_FATAL(vcp, error)	(vcp)->vc_tdesc->tr_fatal(vcp, error)

#endif /* _NETSMB_SMB_TRAN_H_ */
