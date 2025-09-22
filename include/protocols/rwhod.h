/*	$OpenBSD: rwhod.h,v 1.4 2003/06/02 19:34:12 millert Exp $	*/
/*	$NetBSD: rwhod.h,v 1.3 1994/10/26 00:56:51 cgd Exp $	*/

/*
 * Copyright (c) 1983 The Regents of the University of California.
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
 *	@(#)rwhod.h	5.6 (Berkeley) 4/3/91
 */

#ifndef _RWHOD_H_
#define	_RWHOD_H_

/*
 * rwho protocol packet format.
 */
struct	outmp {
	char	out_line[8];		/* tty name */
	char	out_name[8];		/* user id */
	int32_t	out_time;		/* time on */
};

struct	whod {
	char	wd_vers;		/* protocol version # */
	char	wd_type;		/* packet type, see below */
	char	wd_pad[2];
	int32_t	wd_sendtime;		/* time stamp by sender */
	int32_t	wd_recvtime;		/* time stamp applied by receiver */
	char	wd_hostname[32];	/* hosts's name */
	int32_t	wd_loadav[3];		/* load average as in uptime */
	int32_t	wd_boottime;		/* time system booted */
	struct	whoent {
		struct		outmp we_utmp;	/* active tty info */
		int32_t		we_idle;	/* tty idle time */
	} wd_we[1024 / sizeof (struct whoent)];
};

#define	WHODVERSION	1
#define	WHODTYPE_STATUS	1		/* host status */

#define	_PATH_RWHODIR	"/var/rwho"

#endif /* !_RWHOD_H_ */
