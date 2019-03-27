/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
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
 *      @(#)extern.h	8.1 (Berkeley) 6/6/93
 *
 * $FreeBSD$
 */


#include <sys/types.h>
#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */

/*
 * Options for setup_myprinter().
 */
#define SUMP_NOHEADER	0x0001		/* Do not print a header line */
#define SUMP_CHDIR_SD	0x0002		/* chdir into the spool directory */

__BEGIN_DECLS
void	 abort_q(struct printer *_pp);
void	 bottomq_cmd(int _argc, char *_argv[]);
void	 clean_gi(int _argc, char *_argv[]);
void	 clean_q(struct printer *_pp);
void	 disable_q(struct printer *_pp);
void	 down_gi(int _argc, char *_argv[]);
void	 down_q(struct printer *_pp);
void	 enable_q(struct printer *_pp);
void	 generic(void (*_specificrtn)(struct printer *_pp), int _cmdopts,
	    void (*_initcmd)(int _argc, char *_argv[]),
	    int _argc, char *_argv[]);
void	 help(int _argc, char *_argv[]);
void	 quit(int _argc, char *_argv[]);
void	 restart_q(struct printer *_pp);
void	 setstatus_gi(int _argc, char *_argv[]);
void	 setstatus_q(struct printer *_pp);
void	 start_q(struct printer *_pp);
void	 status(struct printer *_pp);
void	 stop_q(struct printer *_pp);
void	 tclean_gi(int _argc, char *_argv[]);
void	 topq_cmd(int _argc, char *_argv[]);
void	 up_q(struct printer *_pp);
void	 topq(int _argc, char *_argv[]);	/* X-version */

/* from lpc.c: */
struct printer	*setup_myprinter(char *_pwanted, struct printer *_pp,
		    int _sump_opts);
__END_DECLS

extern int NCMDS;
extern struct cmd cmdtab[];
extern uid_t	uid, euid;
