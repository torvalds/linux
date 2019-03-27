/*	$OpenBSD: libgen.h,v 1.4 1999/05/28 22:00:22 espie Exp $	*/
/*	$FreeBSD$	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LIBGEN_H_
#define	_LIBGEN_H_

#include <sys/cdefs.h>

__BEGIN_DECLS
char	*basename(char *);
char	*dirname(char *);
__END_DECLS

/*
 * In FreeBSD 12, the prototypes of basename() and dirname() were
 * modified to comply to POSIX. These functions may now modify their
 * input. Unfortunately, our copy of xinstall(8) shipped with previous
 * versions of FreeBSD is built using the host headers and libc during
 * the bootstrapping phase and depends on the old behavior.
 *
 * Apply a workaround where we explicitly link against basename@FBSD_1.0
 * and dirname@FBSD_1.0 in case these functions are called on constant
 * strings, instead of making the program crash at runtime.
 */
#if defined(__generic) && !defined(__cplusplus)
__BEGIN_DECLS
char	*__old_basename(char *);
char	*__old_dirname(char *);
__END_DECLS
__sym_compat(basename, __old_basename, FBSD_1.0);
__sym_compat(dirname, __old_dirname, FBSD_1.0);
#define	basename(x)	__generic(x, const char *, __old_basename, basename)(x)
#define	dirname(x)	__generic(x, const char *, __old_dirname, dirname)(x)
#endif

#endif /* !_LIBGEN_H_ */
