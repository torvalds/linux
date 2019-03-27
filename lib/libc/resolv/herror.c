/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND ISC)
 *
 * Copyright (c) 1987, 1993
 *    The Regents of the University of California.  All rights reserved.
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
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static const char sccsid[] = "@(#)herror.c	8.1 (Berkeley) 6/4/93";
static const char rcsid[] = "$Id: herror.c,v 1.4 2005/04/27 04:56:41 sra Exp $";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include "namespace.h"
#include <sys/param.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/nameser.h>

#include <netdb.h>
#include <resolv.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"

#include "port_after.h"

const char *h_errlist[] = {
	"Resolver Error 0 (no error)",
	"Unknown host",				/*%< 1 HOST_NOT_FOUND */
	"Host name lookup failure",		/*%< 2 TRY_AGAIN */
	"Unknown server error",			/*%< 3 NO_RECOVERY */
	"No address associated with name",	/*%< 4 NO_ADDRESS */
};
const int h_nerr = { nitems(h_errlist) };

#undef	h_errno
int	h_errno;

/*%
 * herror --
 *	print the error indicated by the h_errno value.
 */
void
herror(const char *s) {
	struct iovec iov[4], *v = iov;
	char *t;

	if (s != NULL && *s != '\0') {
		DE_CONST(s, t);
		v->iov_base = t;
		v->iov_len = strlen(t);
		v++;
		DE_CONST(": ", t);
		v->iov_base = t;
		v->iov_len = 2;
		v++;
	}
	DE_CONST(hstrerror(*__h_errno()), t);
	v->iov_base = t;
	v->iov_len = strlen(v->iov_base);
	v++;
	DE_CONST("\n", t);
	v->iov_base = t;
	v->iov_len = 1;
	_writev(STDERR_FILENO, iov, (v - iov) + 1);
}

/*%
 * hstrerror --
 *	return the string associated with a given "host" errno value.
 */
const char *
hstrerror(int err) {
	if (err < 0)
		return ("Resolver internal error");
	else if (err < h_nerr)
		return (h_errlist[err]);
	return ("Unknown resolver error");
}

/*! \file */
