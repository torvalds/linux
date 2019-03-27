/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <stdlib.h>
#include <string.h>

#include "bitmap.h"

struct bitmap
bm_alloc(int size)
{
	struct bitmap   bm;
	int             szmap = (size / 8) + !!(size % 8);

	bm.size = size;
	bm.map = malloc(szmap);
	if (bm.map)
		memset(bm.map, 0, szmap);
	return bm;
}

void
bm_dealloc(struct bitmap * bm)
{
	free(bm->map);
}

static void
bm_getmask(int *pos, unsigned char *bmask)
{
	*bmask = (unsigned char) (1 << (*pos % 8));
	*pos /= 8;
}

void
bm_setbit(struct bitmap * bm, int pos)
{
	unsigned char   bmask;

	bm_getmask(&pos, &bmask);
	bm->map[pos] |= bmask;
}

void
bm_clrbit(struct bitmap * bm, int pos)
{
	unsigned char   bmask;

	bm_getmask(&pos, &bmask);
	bm->map[pos] &= ~bmask;
}

int
bm_isset(struct bitmap * bm, int pos)
{
	unsigned char   bmask;

	bm_getmask(&pos, &bmask);
	return !!(bm->map[pos] & bmask);
}

int
bm_firstunset(struct bitmap * bm)
{
	int             szmap = (bm->size / 8) + !!(bm->size % 8);
	int             at = 0;
	int             pos = 0;

	while (pos < szmap) {
		unsigned char   bmv = bm->map[pos++];
		unsigned char   bmask = 1;

		while (bmask & 0xff) {
			if ((bmv & bmask) == 0)
				return at;
			bmask <<= 1;
			++at;
		}
	}
	return at;
}

int
bm_lastset(struct bitmap * bm)
{
	int             szmap = (bm->size / 8) + !!(bm->size % 8);
	int             at = 0;
	int             pos = 0;
	int             ofs = 0;

	while (pos < szmap) {
		unsigned char   bmv = bm->map[pos++];
		unsigned char   bmask = 1;

		while (bmask & 0xff) {
			if ((bmv & bmask) != 0)
				ofs = at;
			bmask <<= 1;
			++at;
		}
	}
	return ofs;
}
