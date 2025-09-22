/*	$OpenBSD: getusershell.c,v 1.18 2019/12/10 02:35:16 millert Exp $ */
/*
 * Copyright (c) 1985, 1993
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

#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Local shells should NOT be added here.  They should be added in
 * /etc/shells.
 */

static char *okshells[] = { _PATH_BSHELL, _PATH_CSHELL, _PATH_KSHELL, NULL };
static char **curshell, **shells;
static char **initshells(void);

/*
 * Get a list of shells from _PATH_SHELLS, if it exists.
 */
char *
getusershell(void)
{
	char *ret;

	if (curshell == NULL)
		curshell = initshells();
	ret = *curshell;
	if (ret != NULL)
		curshell++;
	return (ret);
}

void
endusershell(void)
{
	char **s;

	if ((s = shells))
		while (*s)
			free(*s++);
	free(shells);
	shells = NULL;

	curshell = NULL;
}

void
setusershell(void)
{

	curshell = initshells();
}

static char **
initshells(void)
{
	size_t nshells, nalloc, linesize;
	char *line;
	FILE *fp;

	free(shells);
	shells = NULL;

	if ((fp = fopen(_PATH_SHELLS, "re")) == NULL)
		return (okshells);

	line = NULL;
	nalloc = 10; // just an initial guess
	nshells = 0;
	shells = reallocarray(NULL, nalloc, sizeof (char *));
	if (shells == NULL)
		goto fail;
	linesize = 0;
	while (getline(&line, &linesize, fp) != -1) {
		if (*line != '/')
			continue;
		line[strcspn(line, "#\n")] = '\0';
		if (!(shells[nshells] = strdup(line)))
			goto fail;

		if (nshells + 1 == nalloc) {
			char **new = reallocarray(shells, nalloc * 2, sizeof(char *));
			if (!new)
				goto fail;
			shells = new;
			nalloc *= 2;
		}
		nshells++;
	}
	free(line);
	shells[nshells] = NULL;
	(void)fclose(fp);
	return (shells);

fail:
	free(line);
	while (nshells)
		free(shells[nshells--]);
	free(shells);
	shells = NULL;
	(void)fclose(fp);
	return (okshells);
}
