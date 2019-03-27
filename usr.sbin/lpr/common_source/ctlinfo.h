/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2001,2011  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * $FreeBSD$
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

/*
 * ctlinfo - This collection of routines will know everything there is to
 * know about the information inside a control file ('cf*') which is used
 * to describe a print job in lpr & friends.  The eventual goal is that it
 * will be the ONLY source file to know what's inside these control-files.
 */

struct cjprivate;			/* used internal to ctl* routines */

struct cjobinfo {
	int	 cji_dfcount;		/* number of data files to print */
	int	 cji_uncount;		/* number of unlink-file requests */
	char	*cji_accthost;		/* the host that this job came from,
					 * for accounting purposes (usually
					 * the host where the original 'lpr'
					 * was done) */
	char	*cji_acctuser;		/* userid who should be charged for
					 * this job (usually, the userid which
					 * did the original 'lpr') */
	char	*cji_class;		/* class-name */
	char	*cji_curqueue;		/* printer-queue that this cf-file is
					 * curently sitting in (mainly used
					 * in syslog error messages) */
	char	*cji_fname;		/* filename of the control file */
	char	*cji_jobname;		/* job-name (for banner) */
	char	*cji_mailto;		/* userid to send email to (or null) */
	char	*cji_headruser;		/* "literal" user-name (for banner) or
					 * NULL if no banner-page is wanted */
	struct cjprivate *cji_priv;
};

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */

__BEGIN_DECLS
void		 ctl_freeinf(struct cjobinfo *_cjinf);
struct cjobinfo	*ctl_readcf(const char *_ptrname, const char *_cfname);
char		*ctl_renametf(const char *_ptrname, const char *_tfname);
__END_DECLS
