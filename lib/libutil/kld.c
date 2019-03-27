/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>

#include <errno.h>
#include <libutil.h>
#include <string.h>

int
kld_isloaded(const char *name)
{
	struct kld_file_stat fstat;
	struct module_stat mstat;
	const char *ko;
	int fid, mid;

	for (fid = kldnext(0); fid > 0; fid = kldnext(fid)) {
		fstat.version = sizeof(fstat);
		if (kldstat(fid, &fstat) != 0)
			continue;
		/* check if the file name matches the supplied name */
		if (strcmp(fstat.name, name) == 0)
			return (1);
		/* strip .ko and try again */
		if ((ko = strstr(fstat.name, ".ko")) != NULL &&
		    strlen(name) == (size_t)(ko - fstat.name) &&
		    strncmp(fstat.name, name, ko - fstat.name) == 0)
			return (1);
		/* look for a matching module within the file */
		for (mid = kldfirstmod(fid); mid > 0; mid = modfnext(mid)) {
			mstat.version = sizeof(mstat);
			if (modstat(mid, &mstat) != 0)
				continue;
			if (strcmp(mstat.name, name) == 0)
				return (1);
		}
	}
	return (0);
}

int
kld_load(const char *name)
{
	if (kldload(name) == -1 && errno != EEXIST)
		return (-1);
	return (0);
}
