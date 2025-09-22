/*	$OpenBSD: ttyent.h,v 1.4 2003/06/02 19:34:12 millert Exp $	*/
/*	$NetBSD: ttyent.h,v 1.5 1994/10/26 00:56:36 cgd Exp $	*/

/*
 * Copyright (c) 1989 The Regents of the University of California.
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
 *	@(#)ttyent.h	5.7 (Berkeley) 4/3/91
 */

#ifndef	_TTYENT_H_
#define	_TTYENT_H_

#define	_PATH_TTYS	"/etc/ttys"

#define	_TTYS_OFF	"off"
#define	_TTYS_ON	"on"
#define	_TTYS_SECURE	"secure"
#define	_TTYS_WINDOW	"window"
#define	_TTYS_LOCAL	"local"
#define	_TTYS_RTSCTS	"rtscts"
#define	_TTYS_SOFTCAR	"softcar"
#define	_TTYS_MDMBUF	"mdmbuf"

struct ttyent {
	char	*ty_name;	/* terminal device name */
	char	*ty_getty;	/* command to execute, usually getty */
	char	*ty_type;	/* terminal type for termcap */
#define	TTY_ON		0x01	/* enable logins (start ty_getty program) */
#define	TTY_SECURE	0x02	/* allow uid of 0 to login */
#define	TTY_LOCAL	0x04	/* set 'CLOCAL' on open (dev. specific) */
#define	TTY_RTSCTS	0x08	/* set 'CRTSCTS' on open (dev. specific) */
#define	TTY_SOFTCAR	0x10	/* ignore hardware carrier (dev. spec.) */
#define	TTY_MDMBUF	0x20	/* set 'MDMBUF' on open (dev. specific) */
	int	ty_status;	/* status flags */
	char 	*ty_window;	/* command to start up window manager */
	char	*ty_comment;	/* comment field */
};

#include <sys/cdefs.h>

__BEGIN_DECLS
struct ttyent *getttyent(void);
struct ttyent *getttynam(const char *);
int setttyent(void);
int endttyent(void);
__END_DECLS

#endif /* !_TTYENT_H_ */
