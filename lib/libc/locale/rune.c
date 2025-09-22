/*	$OpenBSD: rune.c,v 1.11 2024/08/18 02:22:29 guenther Exp $ */
/*	$NetBSD: rune.c,v 1.26 2004/05/09 11:26:33 kleink Exp $	*/

/*-
 * Copyright (c)1999 Citrus Project,
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

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "runetype.h"
#include "rune_local.h"

#define SAFE_ADD(x, y)			\
do {					\
	if ((x) > SIZE_MAX - (y))	\
		return NULL;		\
	(x) += (y);			\
} while (0);

static int readrange(_RuneLocale *, _RuneRange *, uint32_t, void *, FILE *);
static void _freeentry(_RuneRange *);

static int
readrange(_RuneLocale *rl, _RuneRange *rr, uint32_t nranges, void *lastp,
	FILE *fp)
{
	uint32_t i;
	_RuneEntry *re;
	_FileRuneEntry fre;

	re = (_RuneEntry *)rl->rl_variable;

	rr->rr_nranges = nranges;
	if (rr->rr_nranges == 0) {
		rr->rr_rune_ranges = NULL;
		return 0;
	}

	rr->rr_rune_ranges = re;
	for (i = 0; i < rr->rr_nranges; i++) {
		if ((void *)re >= lastp)
			return -1;

		if (fread(&fre, sizeof(fre), 1, fp) != 1)
			return -1;

		re->re_min = ntohl((uint32_t)fre.fre_min);
		re->re_max = ntohl((uint32_t)fre.fre_max);
		re->re_map = ntohl((uint32_t)fre.fre_map);
		re++;
	}
	rl->rl_variable = re;
	return 0;
}

static int
readentry(_RuneRange *rr, FILE *fp)
{
	_RuneEntry *re;
	size_t l, i, j;
	int error;

	re = rr->rr_rune_ranges;
	for (i = 0; i < rr->rr_nranges; i++) {
		if (re[i].re_map != 0) {
			re[i].re_rune_types = NULL;
			continue;
		}

		if (re[i].re_max < re[i].re_min) {
			error = EINVAL;
			goto fail;
		}

		l = re[i].re_max - re[i].re_min + 1;
		re[i].re_rune_types = calloc(l, sizeof(_RuneType));
		if (!re[i].re_rune_types) {
			error = ENOMEM;
			goto fail;
		}

		if (fread(re[i].re_rune_types, sizeof(_RuneType), l, fp) != l)
			goto fail2;

		for (j = 0; j < l; j++)
			re[i].re_rune_types[j] = ntohl(re[i].re_rune_types[j]);
	}
	return 0;

fail:
	for (j = 0; j < i; j++) {
		free(re[j].re_rune_types);
		re[j].re_rune_types = NULL;
	}
	return error;
fail2:
	for (j = 0; j <= i; j++) {
		free(re[j].re_rune_types);
		re[j].re_rune_types = NULL;
	}
	return errno;
}

/* XXX: temporary implementation */
static int
find_codeset(_RuneLocale *rl)
{
	char *top, *codeset, *tail, *ep;

	if (rl->rl_variable == NULL)
		return 0;

	/* end of rl_variable region */
	ep = (char *)rl->rl_variable;
	ep += rl->rl_variable_len;
	rl->rl_codeset = NULL;
	if (!(top = strstr(rl->rl_variable, _RUNE_CODESET)))
		return 0;
	tail = strpbrk(top, " \t");
	codeset = top + sizeof(_RUNE_CODESET) - 1;
	if (tail) {
		*top = *tail;
		*tail = '\0';
		rl->rl_codeset = strdup(codeset);
		strlcpy(top + 1, tail + 1, (unsigned)(ep - (top + 1)));
	} else {
		*top = '\0';
		rl->rl_codeset = strdup(codeset);
	}
	return (rl->rl_codeset == NULL);
}

void
_freeentry(_RuneRange *rr)
{
	_RuneEntry *re;
	uint32_t i;

	re = rr->rr_rune_ranges;
	for (i = 0; i < rr->rr_nranges; i++) {
		free(re[i].re_rune_types);
		re[i].re_rune_types = NULL;
	}
}


_RuneLocale *
_Read_RuneMagi(FILE *fp)
{
	/* file */
	_FileRuneLocale frl;
	/* host data */
	char *hostdata;
	size_t hostdatalen;
	void *lastp;
	_RuneLocale *rl;
	struct stat sb;
	int x;
	uint32_t runetype_nranges, maplower_nranges, mapupper_nranges, var_len;

	if (fstat(fileno(fp), &sb) == -1)
		return NULL;

	if (sb.st_size < sizeof(_FileRuneLocale))
		return NULL;
	/* XXX more validation? */

	/* Someone might have read the magic number once already */
	rewind(fp);

	if (fread(&frl, sizeof(frl), 1, fp) != 1)
		return NULL;
	if (memcmp(frl.frl_magic, _RUNE_MAGIC_1, sizeof(frl.frl_magic)))
		return NULL;

	runetype_nranges = ntohl(frl.frl_runetype_ext.frr_nranges);
	maplower_nranges = ntohl(frl.frl_maplower_ext.frr_nranges);
	mapupper_nranges = ntohl(frl.frl_mapupper_ext.frr_nranges);
	var_len = ntohl((uint32_t)frl.frl_variable_len);

#if SIZE_MAX <= UINT32_MAX
	if (runetype_nranges > SIZE_MAX / sizeof(_RuneEntry) ||
	    maplower_nranges > SIZE_MAX / sizeof(_RuneEntry) ||
	    mapupper_nranges > SIZE_MAX / sizeof(_RuneEntry))
		return NULL;
#endif

	if (var_len > INT32_MAX)
		return NULL;

	hostdatalen = sizeof(*rl);
	SAFE_ADD(hostdatalen, var_len);
	SAFE_ADD(hostdatalen, runetype_nranges * sizeof(_RuneEntry));
	SAFE_ADD(hostdatalen, maplower_nranges * sizeof(_RuneEntry));
	SAFE_ADD(hostdatalen, mapupper_nranges * sizeof(_RuneEntry));

	if ((hostdata = calloc(hostdatalen, 1)) == NULL)
		return NULL;
	lastp = hostdata + hostdatalen;

	rl = (_RuneLocale *)hostdata;
	rl->rl_variable = rl + 1;

	rl->rl_variable_len = ntohl((uint32_t)frl.frl_variable_len);

	for (x = 0; x < _CACHED_RUNES; ++x) {
		rl->rl_runetype[x] = ntohl(frl.frl_runetype[x]);

		/* XXX assumes rune_t = uint32_t */
		rl->rl_maplower[x] = ntohl((uint32_t)frl.frl_maplower[x]);
		rl->rl_mapupper[x] = ntohl((uint32_t)frl.frl_mapupper[x]);
	}

	if (readrange(rl, &rl->rl_runetype_ext, runetype_nranges, lastp, fp) ||
	    readrange(rl, &rl->rl_maplower_ext, maplower_nranges, lastp, fp) ||
	    readrange(rl, &rl->rl_mapupper_ext, mapupper_nranges, lastp, fp))
		goto err;

	if (readentry(&rl->rl_runetype_ext, fp) != 0)
		goto err;

	if ((uint8_t *)rl->rl_variable + rl->rl_variable_len >
	    (uint8_t *)lastp)
		goto rune_err;

	if (rl->rl_variable_len == 0)
		rl->rl_variable = NULL;
	else if (fread(rl->rl_variable, rl->rl_variable_len, 1, fp) != 1)
		goto rune_err;
	if (find_codeset(rl))
		goto rune_err;

	/*
	 * error if we have junk at the tail
	 */
	if (ftello(fp) != sb.st_size)
		goto rune_err;

	return(rl);
rune_err:
	_freeentry(&rl->rl_runetype_ext);
err:
	free(hostdata);
	return NULL;
}
