/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 The Regents of the University of California.
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
 *	from: FreeBSD: src/sys/i386/include/sysarch.h,v 1.14 2000/09/21
 * $FreeBSD$
 */

/*
 * Architecture specific syscalls (sparc64)
 */
#ifndef _MACHINE_SYSARCH_H_
#define _MACHINE_SYSARCH_H_

#include <machine/utrap.h>

#define	SPARC_UTRAP_INSTALL	1
#define	SPARC_SIGTRAMP_INSTALL	2

struct sparc_utrap_install_args {
	int num;
	const struct sparc_utrap_args *handlers;
};

struct sparc_sigtramp_install_args {
	void	*sia_new;
	void	**sia_old;
};

struct sparc_utrap_args {
	utrap_entry_t type;
	utrap_handler_t new_precise;
	utrap_handler_t new_deferred;
	utrap_handler_t *old_precise;
	utrap_handler_t *old_deferred;
};

#ifndef _KERNEL
#include <sys/cdefs.h>

__BEGIN_DECLS
int	__sparc_utrap_install(utrap_entry_t _type,
	    utrap_handler_t _new_precise, utrap_handler_t _new_deferred,
	    utrap_handler_t *_old_precise, utrap_handler_t *_old_deferred);
int	sysarch(int _number, void *_args);
__END_DECLS

#endif

#endif /* !_MACHINE_SYSARCH_H_ */
