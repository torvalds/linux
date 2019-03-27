/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Ed Schouten <ed@FreeBSD.org>
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

#ifndef _UTMPX_H_
#define	_UTMPX_H_

#include <sys/cdefs.h>
#include <sys/_timeval.h>
#include <sys/_types.h>

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

struct utmpx {
	short		ut_type;	/* Type of entry. */
	struct timeval	ut_tv;		/* Time entry was made. */
	char		ut_id[8];	/* Record identifier. */
	pid_t		ut_pid;		/* Process ID. */
	char		ut_user[32];	/* User login name. */
	char		ut_line[16];	/* Device name. */
#if __BSD_VISIBLE
	char		ut_host[128];	/* Remote hostname. */
#else
	char		__ut_host[128];
#endif
	char		__ut_spare[64];
};

#define	EMPTY		0	/* No valid user accounting information. */
#define	BOOT_TIME	1	/* Time of system boot. */
#define	OLD_TIME	2	/* Time when system clock changed. */
#define	NEW_TIME	3	/* Time after system clock changed. */
#define	USER_PROCESS	4	/* A process. */
#define	INIT_PROCESS	5	/* A process spawned by the init process. */
#define	LOGIN_PROCESS	6	/* The session leader of a logged-in user. */
#define	DEAD_PROCESS	7	/* A session leader who has exited. */
#if __BSD_VISIBLE
#define	SHUTDOWN_TIME	8	/* Time of system shutdown. */
#endif

#if __BSD_VISIBLE
#define	UTXDB_ACTIVE	0	/* Active login sessions. */
#define	UTXDB_LASTLOGIN	1	/* Last login sessions. */
#define	UTXDB_LOG	2	/* Log indexed by time. */
#endif

__BEGIN_DECLS
void	endutxent(void);
struct utmpx *getutxent(void);
struct utmpx *getutxid(const struct utmpx *);
struct utmpx *getutxline(const struct utmpx *);
struct utmpx *pututxline(const struct utmpx *);
void	setutxent(void);

#if __BSD_VISIBLE
struct utmpx *getutxuser(const char *);
int	setutxdb(int, const char *);
#endif
__END_DECLS

#endif /* !_UTMPX_H_ */
