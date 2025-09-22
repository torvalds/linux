/*	$OpenBSD: setproctitle.c,v 1.16 2020/10/12 22:08:33 deraadt Exp $ */
/*
 * Copyright (c) 1994, 1995 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/exec.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define	MAX_PROCTITLE	2048

void
setproctitle(const char *fmt, ...)
{
	static struct ps_strings *ps;
	va_list ap;
	
	static char buf[MAX_PROCTITLE], *bufp = buf;
	int used;

	va_start(ap, fmt);
	if (fmt != NULL) {
		used = snprintf(buf, MAX_PROCTITLE, "%s: ", __progname);
		if (used >= MAX_PROCTITLE)
			used = MAX_PROCTITLE - 1;
		else if (used < 0)
			used = 0;
		(void)vsnprintf(buf + used, MAX_PROCTITLE - used, fmt, ap);
	} else
		(void)snprintf(buf, MAX_PROCTITLE, "%s", __progname);
	va_end(ap);

	if (ps == NULL) {
		struct _ps_strings _ps;
		const int mib[2] = { CTL_VM, VM_PSSTRINGS };
		size_t len;

		len = sizeof(_ps);
		if (sysctl(mib, 2, &_ps, &len, NULL, 0) != 0)
			return;
		ps = (struct ps_strings *)_ps.val;
	}
	ps->ps_nargvstr = 1;
	ps->ps_argvstr = &bufp;
}
