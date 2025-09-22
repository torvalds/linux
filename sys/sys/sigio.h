/*	$OpenBSD: sigio.h,v 1.4 2020/01/08 16:27:42 visa Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)filedesc.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD: head/sys/sys/sigio.h 326023 2017-11-20 19:43:44Z pfg $
 */

#ifndef _SYS_SIGIO_H_
#define _SYS_SIGIO_H_

struct sigio;
LIST_HEAD(sigiolst, sigio);

/*
 * sigio registration
 *
 * Locking:
 *	S	sigio_lock
 */
struct sigio_ref {
	struct sigio	*sir_sigio;	/* [S] associated sigio struct */
};

#ifdef _KERNEL

/*
 * This structure holds the information needed to send a SIGIO or
 * a SIGURG signal to a process or process group when new data arrives
 * on a device or socket.  The structure is placed on an LIST belonging
 * to the proc or pgrp so that the entire list may be revoked when the
 * process exits or the process group disappears.
 *
 * Locking:
 *	I	immutable after creation
 *	S	sigio_lock
 */
struct sigio {
	union {
		struct	process *siu_proc;
					/* [I] process to receive
					 *     SIGIO/SIGURG */
		struct	pgrp *siu_pgrp; /* [I] process group to receive ... */
	} sio_u;
	LIST_ENTRY(sigio) sio_pgsigio;	/* [S] sigio's for process or group */
	struct	sigio_ref *sio_myref;	/* [I] location of the pointer that
					 *     holds the reference to
					 *     this structure */
	struct	ucred *sio_ucred;	/* [I] current credentials */
	pid_t	sio_pgid;		/* [I] pgid for signals */
};
#define	sio_proc	sio_u.siu_proc
#define	sio_pgrp	sio_u.siu_pgrp

static inline void
sigio_init(struct sigio_ref *sir)
{
	sir->sir_sigio = NULL;
}

void	sigio_copy(struct sigio_ref *, struct sigio_ref *);
void	sigio_free(struct sigio_ref *);
void	sigio_freelist(struct sigiolst *);
void	sigio_getown(struct sigio_ref *, u_long, caddr_t);
int	sigio_setown(struct sigio_ref *, u_long, caddr_t);

#endif /* _KERNEL */

#endif /* _SYS_SIGIO_H_ */
