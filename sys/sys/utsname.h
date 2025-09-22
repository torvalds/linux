/*	$OpenBSD: utsname.h,v 1.6 2013/03/24 17:45:50 deraadt Exp $	*/
/*	$NetBSD: utsname.h,v 1.6 1994/06/29 06:46:11 cgd Exp $	*/

/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chuck Karish of Mindcraft, Inc.
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
 *	@(#)utsname.h	8.1 (Berkeley) 1/4/94
 */

#ifndef	_SYS_UTSNAME_H
#define	_SYS_UTSNAME_H

#define SYS_NMLN	256

struct utsname {
	char	sysname[SYS_NMLN];	/* Name of this OS. */
	char	nodename[SYS_NMLN];	/* Name of this network node. */
	char	release[SYS_NMLN];	/* Release level. */
	char	version[SYS_NMLN];	/* Version level. */
	char	machine[SYS_NMLN];	/* Hardware type. */
};

#include <sys/cdefs.h>

#ifndef _KERNEL
__BEGIN_DECLS
int	uname(struct utsname *);
__END_DECLS
#endif /* !_KERNEL */

#endif	/* !_SYS_UTSNAME_H */
