/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)acct.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _SYS_ACCT_H_
#define _SYS_ACCT_H_

#ifdef _KERNEL
#define float uint32_t
#endif

#define AC_COMM_LEN 16

/*
 * Accounting structure version 3 (current).
 * The first byte is always zero.
 * Time units are microseconds.
 */

struct acctv3 {
	uint8_t   ac_zero;		/* zero identifies new version */
	uint8_t   ac_version;		/* record version number */
	uint16_t  ac_len;		/* record length */

	char	  ac_comm[AC_COMM_LEN];	/* command name */
	float	  ac_utime;		/* user time */
	float	  ac_stime;		/* system time */
	float	  ac_etime;		/* elapsed time */
	time_t	  ac_btime;		/* starting time */
	uid_t	  ac_uid;		/* user id */
	gid_t	  ac_gid;		/* group id */
	float	  ac_mem;		/* average memory usage */
	float	  ac_io;		/* count of IO blocks */
	__dev_t   ac_tty;		/* controlling tty */
	uint32_t  ac_pad0;
	uint16_t  ac_len2;		/* record length */
	union {
		uint32_t  ac_align;	/* force v1 compatible alignment */

#define	AFORK	0x01			/* forked but not exec'ed */
/* ASU is no longer supported */
#define	ASU	0x02			/* used super-user permissions */
#define	ACOMPAT	0x04			/* used compatibility mode */
#define	ACORE	0x08			/* dumped core */
#define	AXSIG	0x10			/* killed by a signal */
#define ANVER	0x20			/* new record version */

		uint8_t   ac_flag;	/* accounting flags */
	} ac_trailer;

#define ac_flagx ac_trailer.ac_flag
};

struct acctv2 {
	uint8_t   ac_zero;		/* zero identifies new version */
	uint8_t   ac_version;		/* record version number */
	uint16_t  ac_len;		/* record length */

	char	  ac_comm[AC_COMM_LEN];	/* command name */
	float	  ac_utime;		/* user time */
	float	  ac_stime;		/* system time */
	float	  ac_etime;		/* elapsed time */
	time_t	  ac_btime;		/* starting time */
	uid_t	  ac_uid;		/* user id */
	gid_t	  ac_gid;		/* group id */
	float	  ac_mem;		/* average memory usage */
	float	  ac_io;		/* count of IO blocks */
	uint32_t  ac_tty;		/* controlling tty */

	uint16_t  ac_len2;		/* record length */
	union {
		uint32_t   ac_align;	/* force v1 compatible alignment */
		uint8_t   ac_flag;	/* accounting flags */
	} ac_trailer;
};

/*
 * Legacy accounting structure (rev. 1.5-1.18).
 * The first byte is always non-zero.
 * Some fields use a comp_t type which is a 3 bits base 8
 * exponent, 13 bit fraction ``floating point'' number.
 * Units are 1/AHZV1 seconds.
 */

typedef uint16_t comp_t;

struct acctv1 {
	char	  ac_comm[AC_COMM_LEN];	/* command name */
	comp_t	  ac_utime;		/* user time */
	comp_t	  ac_stime;		/* system time */
	comp_t	  ac_etime;		/* elapsed time */
	time_t	  ac_btime;		/* starting time */
	uid_t	  ac_uid;		/* user id */
	gid_t	  ac_gid;		/* group id */
	uint16_t  ac_mem;		/* average memory usage */
	comp_t	  ac_io;		/* count of IO blocks */
	uint32_t  ac_tty;		/* controlling tty */
	uint8_t   ac_flag;		/* accounting flags */
};

/*
 * 1/AHZV1 is the granularity of the data encoded in the comp_t fields.
 * This is not necessarily equal to hz.
 */
#define	AHZV1	64

#ifdef _KERNEL
struct thread;

int	acct_process(struct thread *td);
#undef float
#endif

#endif /* !_SYS_ACCT_H_ */
