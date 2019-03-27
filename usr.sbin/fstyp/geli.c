/*-
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/disk.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <geom/eli/g_eli.h>

#include "fstyp.h"

int
fstyp_geli(FILE *fp, char *label __unused, size_t labelsize __unused)
{
	int error;
	off_t mediasize;
	u_int sectorsize;
	struct g_eli_metadata md;
	u_char *buf;

	error = ioctl(fileno(fp), DIOCGMEDIASIZE, &mediasize);
	if (error != 0)
		return (1);
	error = ioctl(fileno(fp), DIOCGSECTORSIZE, &sectorsize);
	if (error != 0)
		return (1);
	buf = (u_char *)read_buf(fp, mediasize - sectorsize, sectorsize);
	if (buf == NULL)
		goto gelierr;
	error = eli_metadata_decode(buf, &md);
	if (error)
		goto gelierr;

	if (strcmp(md.md_magic, G_ELI_MAGIC) == 0) {
		free(buf);
		return (0);
	}

gelierr:
	free(buf);

	return (1);
}
