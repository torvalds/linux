/*	$OpenBSD: acct.h,v 1.16 2024/02/25 00:07:13 deraadt Exp $	*/
/*	$NetBSD: acct.h,v 1.16 1995/03/26 20:23:52 jtc Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)acct.h	8.3 (Berkeley) 7/10/94
 */

#include <sys/syslimits.h>

/*
 * Accounting structures; these use a comp_t type which is a 3 bits base 8
 * exponent, 13 bit fraction ``floating point'' number.  Units are 1/AHZ
 * seconds.
 */
typedef u_int16_t comp_t;

struct acct {
	char	  ac_comm[_MAXCOMLEN];	/* command name, incl NUL */
	comp_t	  ac_utime;		/* user time */
	comp_t	  ac_stime;		/* system time */
	comp_t	  ac_etime;		/* elapsed time */
	comp_t	  ac_io;		/* count of IO blocks */
	time_t	  ac_btime;		/* starting time */
	uid_t	  ac_uid;		/* user id */
	gid_t	  ac_gid;		/* group id */
	u_int32_t ac_mem;		/* average memory usage */
	dev_t	  ac_tty;		/* controlling tty, or -1 */
	pid_t	  ac_pid;		/* process id */

	u_int32_t ac_flag;		/* accounting flags */
#define	AFORK	0x00000001	/* fork'd but not exec'd */
#define	AMAP	0x00000004	/* killed by syscall or stack mapping violation */
#define	ACORE	0x00000008	/* dumped core */
#define	AXSIG	0x00000010	/* killed by a signal */
#define	APLEDGE	0x00000020	/* killed due to pledge violation */
#define	ATRAP	0x00000040	/* memory access violation */
#define	AUNVEIL	0x00000080	/* unveil access violation */
#define	APINSYS	0x00000200	/* killed by syscall pin violation */
#define	ABTCFI	0x00000400	/* BT CFI violation */
};

/*
 * 1/AHZ is the granularity of the data encoded in the comp_t fields.
 * This is not necessarily equal to hz.
 */
#define	AHZ	64

#ifdef _KERNEL
int	acct_process(struct proc *p);
void	acct_shutdown(void);
#endif
