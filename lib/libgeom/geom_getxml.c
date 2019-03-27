/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Poul-Henning Kamp
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "libgeom.h"

/*
 * Amount of extra space we allocate to try and anticipate the size of
 * confxml.
 */
#define	GEOM_GETXML_SLACK	4096

/*
 * Number of times to retry in the face of the size of confxml exceeding
 * that of our buffer.
 */
#define	GEOM_GETXML_RETRIES	4

char *
geom_getxml(void)
{
	char *p;
	size_t l = 0;
	int mib[3];
	size_t sizep;
	int retries;

	sizep = sizeof(mib) / sizeof(*mib);
	if (sysctlnametomib("kern.geom.confxml", mib, &sizep) != 0)
		return (NULL);
	if (sysctl(mib, sizep, NULL, &l, NULL, 0) != 0)
		return (NULL);
	l += GEOM_GETXML_SLACK;

	for (retries = 0; retries < GEOM_GETXML_RETRIES; retries++) {
		p = malloc(l);
		if (p == NULL)
			return (NULL);
		if (sysctl(mib, sizep, p, &l, NULL, 0) == 0)
			return (reallocf(p, strlen(p) + 1));

		free(p);

		if (errno != ENOMEM)
			return (NULL);

		/*
		 * Our buffer wasn't big enough. Make it bigger and
		 * try again.
		 */
		l *= 2;
	}

	return (NULL);
}
