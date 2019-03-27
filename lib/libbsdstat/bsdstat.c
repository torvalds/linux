/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2007 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <string.h>

#include "bsdstat.h"

static void
bsdstat_setfmt(struct bsdstat *sf, const char *fmt0)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	char fmt[4096];
	char *fp, *tok;
	int i, j;

	j = 0;
	strlcpy(fmt, fmt0, sizeof(fmt));
	for (fp = fmt; (tok = strsep(&fp, ", ")) != NULL;) {
		for (i = 0; i < sf->nstats; i++)
			if (strcasecmp(tok, sf->stats[i].name) == 0)
				break;
		if (i >= sf->nstats) {
			fprintf(stderr, "%s: unknown statistic name \"%s\" "
				"skipped\n", sf->name, tok);
			continue;
		}
		if (j+4 > (int) sizeof(sf->fmts)) {
			fprintf(stderr, "%s: not enough room for all stats; "
				"stopped at %s\n", sf->name, tok);
			break;
		}
		if (j != 0)
			sf->fmts[j++] = ' ';
		sf->fmts[j++] = FMTS_IS_STAT;
		sf->fmts[j++] = i & 0xff;
		sf->fmts[j++] = (i >> 8) & 0xff;
	}
	sf->fmts[j] = '\0';
#undef N
}

static void 
bsdstat_collect(struct bsdstat *sf)
{
	fprintf(stderr, "%s: don't know how to collect data\n", sf->name);
}

static void 
bsdstat_update_tot(struct bsdstat *sf)
{
	fprintf(stderr, "%s: don't know how to update total data\n", sf->name);
}

static int 
bsdstat_get(struct bsdstat *sf, int s, char b[] __unused, size_t bs __unused)
{
	fprintf(stderr, "%s: don't know how to get stat #%u\n", sf->name, s);
	return 0;
}

static void
bsdstat_print_header(struct bsdstat *sf, FILE *fd)
{
	const unsigned char *cp;
	int i;
	const struct fmt *f;

	for (cp = sf->fmts; *cp != '\0'; cp++) {
		if (*cp == FMTS_IS_STAT) {
			i = *(++cp);
			i |= ((int) *(++cp)) << 8;
			f = &sf->stats[i];
			fprintf(fd, "%*s", f->width, f->label);
		} else
			putc(*cp, fd);
	}
	putc('\n', fd);
}

static void
bsdstat_print_current(struct bsdstat *sf, FILE *fd)
{
	char buf[32];
	const unsigned char *cp;
	int i;
	const struct fmt *f;

	for (cp = sf->fmts; *cp != '\0'; cp++) {
		if (*cp == FMTS_IS_STAT) {
			i = *(++cp);
			i |= ((int) *(++cp)) << 8;
			f = &sf->stats[i];
			if (sf->get_curstat(sf, i, buf, sizeof(buf)))
				fprintf(fd, "%*s", f->width, buf);
		} else
			putc(*cp, fd);
	}
	putc('\n', fd);
}

static void
bsdstat_print_total(struct bsdstat *sf, FILE *fd)
{
	char buf[32];
	const unsigned char *cp;
	const struct fmt *f;
	int i;

	for (cp = sf->fmts; *cp != '\0'; cp++) {
		if (*cp == FMTS_IS_STAT) {
			i = *(++cp);
			i |= ((int) *(++cp)) << 8;
			f = &sf->stats[i];
			if (sf->get_totstat(sf, i, buf, sizeof(buf)))
				fprintf(fd, "%*s", f->width, buf);
		} else
			putc(*cp, fd);
	}
	putc('\n', fd);
}

static void
bsdstat_print_verbose(struct bsdstat *sf, FILE *fd)
{
	const struct fmt *f;
	char s[32];
	int i, width;

	width = 0;
	for (i = 0; i < sf->nstats; i++) {
		f = &sf->stats[i];
		if (f->width > width)
			width = f->width;
	}
	for (i = 0; i < sf->nstats; i++) {
		f = &sf->stats[i];
		if (sf->get_totstat(sf, i, s, sizeof(s)) && strcmp(s, "0"))
			fprintf(fd, "%-*s %s\n", width, s, f->desc);
	}
}

static void
bsdstat_print_fields(struct bsdstat *sf, FILE *fd)
{
	int i, w, width;

	width = 0;
	for (i = 0; i < sf->nstats; i++) {
		w = strlen(sf->stats[i].name);
		if (w > width)
			width = w;
	}
	for (i = 0; i < sf->nstats; i++) {
		const struct fmt *f = &sf->stats[i];
		if (f->width != 0)
			fprintf(fd, "%-*s %s\n", width, f->name, f->desc);
	}
}

void
bsdstat_init(struct bsdstat *sf, const char *name, const struct fmt *stats, int nstats)
{
	sf->name = name;
	sf->stats = stats;
	sf->nstats = nstats;
	sf->setfmt = bsdstat_setfmt;
	sf->collect_cur = bsdstat_collect;
	sf->collect_tot = bsdstat_collect;
	sf->update_tot = bsdstat_update_tot;
	sf->get_curstat = bsdstat_get;
	sf->get_totstat = bsdstat_get;
	sf->print_header = bsdstat_print_header;
	sf->print_current = bsdstat_print_current;
	sf->print_total = bsdstat_print_total;
	sf->print_verbose = bsdstat_print_verbose;
	sf->print_fields = bsdstat_print_fields;
}
