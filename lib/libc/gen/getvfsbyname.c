/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995
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
 */

#include <sys/cdefs.h>
__SCCSID("@(#)kvm_getvfsbyname.c	8.1 (Berkeley) 4/3/95");
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * Given a filesystem name, determine if it is resident in the kernel,
 * and if it is resident, return its xvfsconf structure.
 */
int
getvfsbyname(const char *fsname, struct xvfsconf *vfcp)
{
	struct xvfsconf *xvfsp;
	size_t buflen;
	int cnt, i;

	if (sysctlbyname("vfs.conflist", NULL, &buflen, NULL, 0) < 0)
		return (-1);
	xvfsp = malloc(buflen);
	if (xvfsp == NULL)
		return (-1);
	if (sysctlbyname("vfs.conflist", xvfsp, &buflen, NULL, 0) < 0) {
		free(xvfsp);
		return (-1);
	}
	cnt = buflen / sizeof(struct xvfsconf);
	for (i = 0; i < cnt; i++) {
		if (strcmp(fsname, xvfsp[i].vfc_name) == 0) {
			memcpy(vfcp, xvfsp + i, sizeof(struct xvfsconf));
			free(xvfsp);
			return (0);
		}
	}
	free(xvfsp);
	errno = ENOENT;
	return (-1);
}
