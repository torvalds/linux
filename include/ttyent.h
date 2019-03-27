/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
 *	@(#)ttyent.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef	_TTYENT_H_
#define	_TTYENT_H_

#define	_PATH_TTYS	"/etc/ttys"

#define	_TTYS_OFF	"off"
#define	_TTYS_ON	"on"
#define	_TTYS_ONIFCONSOLE "onifconsole"
#define	_TTYS_ONIFEXISTS "onifexists"
#define	_TTYS_SECURE	"secure"
#define	_TTYS_INSECURE	"insecure"
#define	_TTYS_WINDOW	"window"
#define	_TTYS_GROUP	"group"
#define	_TTYS_NOGROUP	"none"
#define	_TTYS_DIALUP	"dialup"
#define	_TTYS_NETWORK	"network"

struct ttyent {
	char	*ty_name;	/* terminal device name */
	char	*ty_getty;	/* command to execute, usually getty */
	char	*ty_type;	/* terminal type for termcap */
#define	TTY_ON		0x01	/* enable logins (start ty_getty program) */
#define	TTY_SECURE	0x02	/* allow uid of 0 to login */
#define	TTY_DIALUP	0x04	/* is a dialup tty */
#define	TTY_NETWORK	0x08	/* is a network tty */
#define	TTY_IFEXISTS	0x10	/* configured as "onifexists" */
#define	TTY_IFCONSOLE	0x20	/* configured as "onifconsole" */
	int	ty_status;	/* status flags */
	char 	*ty_window;	/* command to start up window manager */
	char	*ty_comment;	/* comment field */
	char	*ty_group;	/* tty group */
};

#include <sys/cdefs.h>

__BEGIN_DECLS
struct ttyent *getttyent(void);
struct ttyent *getttynam(const char *);
int setttyent(void);
int endttyent(void);
int isdialuptty(const char *);
int isnettty(const char *);
__END_DECLS

#endif /* !_TTYENT_H_ */
