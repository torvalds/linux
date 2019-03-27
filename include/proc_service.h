/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2004 Marcel Moolenaar
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _PROC_SERVICE_H_
#define	_PROC_SERVICE_H_

#include <sys/types.h>
#include <sys/procfs.h>

typedef enum {
	PS_OK = 0,		/* No errors. */
	PS_ERR,			/* Generic error. */
	PS_BADADDR,		/* Bad address. */
	PS_BADLID,		/* Bad LWP Id. */
	PS_BADPID,		/* Bad process Id. */
	PS_NOFREGS,		/* FPU register set not available. */
	PS_NOSYM		/* Symbol not found. */
} ps_err_e;

struct ps_prochandle;		/* Opaque type. Defined by the implementor. */

__BEGIN_DECLS
ps_err_e ps_lcontinue(struct ps_prochandle *, lwpid_t);
ps_err_e ps_lgetfpregs(struct ps_prochandle *, lwpid_t, prfpregset_t *);
ps_err_e ps_lgetregs(struct ps_prochandle *, lwpid_t, prgregset_t);
ps_err_e ps_lsetfpregs(struct ps_prochandle *, lwpid_t, const prfpregset_t *);
ps_err_e ps_lsetregs(struct ps_prochandle *, lwpid_t, const prgregset_t);
#ifdef __i386__
ps_err_e ps_lgetxmmregs (struct ps_prochandle *, lwpid_t, char *);
ps_err_e ps_lsetxmmregs (struct ps_prochandle *, lwpid_t, const char *);
#endif
ps_err_e ps_lstop(struct ps_prochandle *, lwpid_t);
ps_err_e ps_linfo(struct ps_prochandle *, lwpid_t, void *);
ps_err_e ps_pcontinue(struct ps_prochandle *);
ps_err_e ps_pdmodel(struct ps_prochandle *, int *);
ps_err_e ps_pglobal_lookup(struct ps_prochandle *, const char *, const char *,
    psaddr_t *);
void	 ps_plog(const char *, ...);
ps_err_e ps_pread(struct ps_prochandle *, psaddr_t, void *, size_t);
ps_err_e ps_pstop(struct ps_prochandle *);
ps_err_e ps_pwrite(struct ps_prochandle *, psaddr_t, const void *, size_t);
__END_DECLS

#endif /* _PROC_SERVICE_H_ */
