/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alan L. Cox <alc@cs.rice.edu>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <link.h>

#include "libc_private.h"

/*
 * Retrieves page size information from the system.  Specifically, returns the
 * number of distinct page sizes that are supported by the system, if
 * "pagesize" is NULL and "nelem" is 0.  Otherwise, assigns up to "nelem" of
 * the system-supported page sizes to consecutive elements of the array
 * referenced by "pagesize", and returns the number of such page sizes that it
 * assigned to the array.  These page sizes are expressed in bytes.
 *
 * The implementation of this function does not directly or indirectly call
 * malloc(3) or any other dynamic memory allocator that may itself call this
 * function.
 */
int
getpagesizes(size_t pagesize[], int nelem)
{
	static u_long ps[MAXPAGESIZES];
	static int nops;
	size_t size;
	int error, i;

	if (nelem < 0 || (nelem > 0 && pagesize == NULL)) {
		errno = EINVAL;
		return (-1);
	}
	/* Cache the result of the sysctl(2). */
	if (nops == 0) {
		error = _elf_aux_info(AT_PAGESIZES, ps, sizeof(ps));
		size = sizeof(ps);
		if (error != 0 || ps[0] == 0) {
			if (sysctlbyname("hw.pagesizes", ps, &size, NULL, 0)
			    == -1)
				return (-1);
		}
		/* Count the number of page sizes that are supported. */
		nops = size / sizeof(ps[0]);
		while (nops > 0 && ps[nops - 1] == 0)
			nops--;
	}
	if (pagesize == NULL)
		return (nops);
	/* Return up to "nelem" page sizes from the cached result. */
	if (nelem > nops)
		nelem = nops;
	for (i = 0; i < nelem; i++)
		pagesize[i] = ps[i];
	return (nelem);
}
