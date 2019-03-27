/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef _SYS_SIGIO_H_
#define _SYS_SIGIO_H_

/*
 * This structure holds the information needed to send a SIGIO or
 * a SIGURG signal to a process or process group when new data arrives
 * on a device or socket.  The structure is placed on an SLIST belonging
 * to the proc or pgrp so that the entire list may be revoked when the
 * process exits or the process group disappears.
 *
 * (c)	const
 * (pg)	locked by either the process or process group lock
 */
struct sigio {
	union {
		struct	proc *siu_proc; /* (c)	process to receive SIGIO/SIGURG */
		struct	pgrp *siu_pgrp; /* (c)	process group to receive ... */
	} sio_u;
	SLIST_ENTRY(sigio) sio_pgsigio;	/* (pg)	sigio's for process or group */
	struct	sigio **sio_myref;	/* (c)	location of the pointer that holds
					 * 	the reference to this structure */
	struct	ucred *sio_ucred;	/* (c)	current credentials */
	pid_t	sio_pgid;		/* (c)	pgid for signals */
};
#define	sio_proc	sio_u.siu_proc
#define	sio_pgrp	sio_u.siu_pgrp

SLIST_HEAD(sigiolst, sigio);

pid_t	fgetown(struct sigio **sigiop);
int	fsetown(pid_t pgid, struct sigio **sigiop);
void	funsetown(struct sigio **sigiop);
void	funsetownlst(struct sigiolst *sigiolst);

#endif /* _SYS_SIGIO_H_ */
