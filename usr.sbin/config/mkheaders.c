/*	$OpenBSD: mkheaders.c,v 1.22 2016/10/16 17:50:00 tb Exp $	*/
/*	$NetBSD: mkheaders.c,v 1.12 1997/02/02 21:12:34 thorpej Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 *
 *	from: @(#)mkheaders.c	8.1 (Berkeley) 6/6/93
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

static int emitcnt(struct nvlist *);
static int emitopt(struct nvlist *);
static int emitwarn(const char *, char *, FILE *);
static char *cntname(const char *);

/*
 * Make headers containing counts, as needed.
 */
int
mkheaders(void)
{
	struct files *fi;
	struct nvlist *nv;

	for (fi = allfiles; fi != NULL; fi = fi->fi_next) {
		if (fi->fi_flags & FI_HIDDEN)
			continue;
		if (fi->fi_flags & (FI_NEEDSCOUNT | FI_NEEDSFLAG) &&
		    emitcnt(fi->fi_optf))
			return (1);
	}

	for (nv = defoptions; nv != NULL; nv = nv->nv_next)
		if (emitopt(nv))
			return (1);

	return (0);
}

static int
emitcnt(struct nvlist *head)
{
	struct nvlist *nv;
	FILE *fp;
	int cnt;
	char nam[100];
	char buf[BUFSIZ];
	char fname[BUFSIZ];

	(void)snprintf(fname, sizeof fname, "%s.h", head->nv_name);
	if ((fp = fopen(fname, "r")) == NULL)
		goto writeit;
	nv = head;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (nv == NULL)
			goto writeit;
		if (sscanf(buf, "#define %99s %d", nam, &cnt) != 2 ||
		    strcmp(nam, cntname(nv->nv_name)) != 0 ||
		    cnt != nv->nv_int)
			goto writeit;
		nv = nv->nv_next;
	}
	if (ferror(fp))
		return (emitwarn("read", fname, fp));
	(void)fclose(fp);
	if (nv == NULL)
		return (0);
writeit:
	if ((fp = fopen(fname, "w")) == NULL)
		return (emitwarn("writ", fname, NULL));
	for (nv = head; nv != NULL; nv = nv->nv_next)
		if (fprintf(fp, "#define\t%s\t%d\n",
		    cntname(nv->nv_name), nv->nv_int) < 0)
			return (emitwarn("writ", fname, fp));
	if (fclose(fp))
		return (emitwarn("writ", fname, NULL));
	return (0);
}

static int
emitopt(struct nvlist *nv)
{
	struct nvlist *option;
	char new_contents[BUFSIZ], buf[BUFSIZ];
	char fname[BUFSIZ];
	int totlen, nlines;
	FILE *fp;

	/*
	 * Generate the new contents of the file.
	 */
	if ((option = ht_lookup(opttab, nv->nv_str)) == NULL)
		totlen = snprintf(new_contents, sizeof new_contents,
		    "/* option `%s' not defined */\n",
		    nv->nv_str);
	else {
		if (option->nv_str != NULL)
			totlen = snprintf(new_contents, sizeof new_contents,
			    "#define\t%s\t%s\n",
			    option->nv_name, option->nv_str);
		else
			totlen = snprintf(new_contents, sizeof new_contents,
			    "#define\t%s\n",
			    option->nv_name);
	}

	if (totlen < 0 || totlen >= sizeof new_contents) {
		warnx("string too long");
		return (1);
	}

	/*
	 * Compare the new file to the old.
	 */
	snprintf(fname, sizeof fname, "opt_%s.h", nv->nv_name);
	if ((fp = fopen(fname, "r")) == NULL)
		goto writeit;
	nlines = 0;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (++nlines != 1 ||
		    strcmp(buf, new_contents) != 0)
			goto writeit;
	}
	if (ferror(fp))
		return (emitwarn("read", fname, fp));
	(void)fclose(fp);
	if (nlines == 1)
		return (0);
writeit:
	/*
	 * They're different, or the file doesn't exist.
	 */
	if ((fp = fopen(fname, "w")) == NULL)
		return (emitwarn("writ", fname, NULL));
	if (fprintf(fp, "%s", new_contents) < 0)
		return (emitwarn("writ", fname, fp));
	if (fclose(fp))
		return (emitwarn("writ", fname, fp));
	return (0);
}

static int
emitwarn(const char *what, char *fname, FILE *fp)
{

	warn("error %sing %s", what, fname);
	if (fp)
		(void)fclose(fp);
	return (1);
}

static char *
cntname(const char *src)
{
	char *dst, c;
	static char buf[100];

	dst = buf;
	*dst++ = 'N';
	while ((c = *src++) != 0)
		*dst++ = islower((unsigned char)c) ?
		    toupper((unsigned char)c) : c;
	*dst = 0;
	return (buf);
}
