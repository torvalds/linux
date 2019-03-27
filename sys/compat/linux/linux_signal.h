/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Marcel Moolenaar
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

#ifndef _LINUX_SIGNAL_H_
#define _LINUX_SIGNAL_H_

/*
 * si_code values
 */
#define	LINUX_SI_USER		0	/* sent by kill, sigsend, raise */
#define	LINUX_SI_KERNEL		0x80	/* sent by the kernel from somewhere */
#define	LINUX_SI_QUEUE		-1	/* sent by sigqueue */
#define	LINUX_SI_TIMER		-2	/* sent by timer expiration */
#define	LINUX_SI_MESGQ		-3	/* sent by real time mesq state change */
#define	LINUX_SI_ASYNCIO	-4	/* sent by AIO completion */
#define	LINUX_SI_SIGIO		-5	/* sent by queued SIGIO */
#define	LINUX_SI_TKILL		-6	/* sent by tkill system call */

int linux_do_sigaction(struct thread *, int, l_sigaction_t *, l_sigaction_t *);
void ksiginfo_to_lsiginfo(const ksiginfo_t *ksi, l_siginfo_t *lsi, l_int sig);
void siginfo_to_lsiginfo(const siginfo_t *si, l_siginfo_t *lsi, l_int sig);
void lsiginfo_to_ksiginfo(const l_siginfo_t *lsi, ksiginfo_t *ksi, int sig);

#endif /* _LINUX_SIGNAL_H_ */
