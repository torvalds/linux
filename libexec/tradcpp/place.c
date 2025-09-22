/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "array.h"
#include "place.h"

struct placefile {
	struct place includedfrom;
	char *dir;
	char *name;
	int depth;
	bool fromsystemdir;
};
DECLARRAY(placefile, static UNUSED);
DEFARRAY(placefile, static);

static struct placefilearray placefiles;
static bool overall_failure;

static const char *myprogname;

static FILE *debuglogfile;

////////////////////////////////////////////////////////////
// placefiles

static
struct placefile *
placefile_create(const struct place *from, const char *name,
		 bool fromsystemdir)
{
	struct placefile *pf;
	const char *s;
	size_t len;

	pf = domalloc(sizeof(*pf));
	pf->includedfrom = *from;

	s = strrchr(name, '/');
	len = (s == NULL) ? 0 : s - name;
	pf->dir = dostrndup(name, len);

	pf->name = dostrdup(name);
	pf->fromsystemdir = fromsystemdir;

	if (from->file != NULL) {
		pf->depth = from->file->depth + 1;
	} else {
		pf->depth = 1;
	}
	return pf;
}

static
void
placefile_destroy(struct placefile *pf)
{
	dostrfree(pf->name);
	dofree(pf, sizeof(*pf));
}

DESTROYALL_ARRAY(placefile, );

const char *
place_getparsedir(const struct place *place)
{
	if (place->file == NULL) {
		return ".";
	}
	return place->file->dir;
}

static
struct placefile *
placefile_find(const struct place *incfrom, const char *name)
{
	unsigned i, num;
	struct placefile *pf;

	num = placefilearray_num(&placefiles);
	for (i=0; i<num; i++) {
		pf = placefilearray_get(&placefiles, i);
		if (place_eq(incfrom, &pf->includedfrom) &&
		    !strcmp(name, pf->name)) {
			return pf;
		}
	}
	return NULL;
}

void
place_changefile(struct place *p, const char *name)
{
	struct placefile *pf;

	assert(p->type == P_FILE);
	if (!strcmp(name, p->file->name)) {
		return;
	}
	pf = placefile_find(&p->file->includedfrom, name);
	if (pf == NULL) {
		pf = placefile_create(&p->file->includedfrom, name,
				      p->file->fromsystemdir);
		placefilearray_add(&placefiles, pf, NULL);
	}
	p->file = pf;
}

const struct placefile *
place_addfile(const struct place *place, const char *file, bool issystem)
{
	struct placefile *pf;

	pf = placefile_create(place, file, issystem);
	placefilearray_add(&placefiles, pf, NULL);
	if (pf->depth > 120) {
		complain(place, "Maximum include nesting depth exceeded");
		die();
	}
	return pf;
}

////////////////////////////////////////////////////////////
// places

void
place_setnowhere(struct place *p)
{
	p->type = P_NOWHERE;
	p->file = NULL;
	p->line = 0;
	p->column = 0;
}

void
place_setbuiltin(struct place *p, unsigned num)
{
	p->type = P_BUILTIN;
	p->file = NULL;
	p->line = num;
	p->column = 1;
}

void
place_setcommandline(struct place *p, unsigned line, unsigned column)
{
	p->type = P_COMMANDLINE;
	p->file = NULL;
	p->line = line;
	p->column = column;
}

void
place_setfilestart(struct place *p, const struct placefile *pf)
{
	p->type = P_FILE;
	p->file = pf;
	p->line = 1;
	p->column = 1;
}

void
place_addcolumns(struct place *p, unsigned cols)
{
	unsigned newcol;

	newcol = p->column + cols;
	if (newcol < p->column) {
		/* overflow (use the old place to complain) */
		complain(p, "Column numbering overflow");
		die();
	}
	p->column = newcol;
}

void
place_addlines(struct place *p, unsigned lines)
{
	unsigned nextline;

	nextline = p->line + lines;
	if (nextline < p->line) {
		/* overflow (use the old place to complain) */
		complain(p, "Line numbering overflow");
		die();
	}
	p->line = nextline;
}

const char *
place_getname(const struct place *p)
{
	switch (p->type) {
	    case P_NOWHERE: return "<nowhere>";
	    case P_BUILTIN: return "<built-in>";
	    case P_COMMANDLINE: return "<command-line>";
	    case P_FILE: return p->file->name;
	}
	assert(0);
	return NULL;
}

bool
place_samefile(const struct place *a, const struct place *b)
{
	if (a->type != b->type) {
		return false;
	}
	if (a->file != b->file) {
		return false;
	}
	return true;
}

bool
place_eq(const struct place *a, const struct place *b)
{
	if (!place_samefile(a, b)) {
		return false;
	}
	if (a->line != b->line || a->column != b->column) {
		return false;
	}
	return true;
}

static
void
place_printfrom(const struct place *p)
{
	const struct place *from;

	if (p->file == NULL) {
		return;
	}
	from = &p->file->includedfrom;
	if (from->type != P_NOWHERE) {
		place_printfrom(from);
		fprintf(stderr, "In file included from %s:%u:%u:\n",
			place_getname(from), from->line, from->column);
	}
}

////////////////////////////////////////////////////////////
// complaints

void
complain_init(const char *pn)
{
	myprogname = pn;
}

void
complain(const struct place *p, const char *fmt, ...)
{
	va_list ap;

	if (p != NULL) {
		place_printfrom(p);
		fprintf(stderr, "%s:%u:%u: ", place_getname(p),
			p->line, p->column);
	} else {
		fprintf(stderr, "%s: ", myprogname);
	}
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void
complain_fail(void)
{
	overall_failure = true;
}

bool
complain_failed(void)
{
	return overall_failure;
}

////////////////////////////////////////////////////////////
// debug logging

void
debuglog_open(const struct place *p, /*const*/ char *file)
{
	assert(debuglogfile == NULL);
	debuglogfile = fopen(file, "w");
	if (debuglogfile == NULL) {
		complain(p, "%s: %s", file, strerror(errno));
		die();
	}
}

void
debuglog_close(void)
{
	if (debuglogfile != NULL) {
		fclose(debuglogfile);
		debuglogfile = NULL;
	}
}

PF(2, 3) void
debuglog(const struct place *p, const char *fmt, ...)
{
	va_list ap;

	if (debuglogfile == NULL) {
		return;
	}

	fprintf(debuglogfile, "%s:%u: ", place_getname(p), p->line);
	va_start(ap, fmt);
	vfprintf(debuglogfile, fmt, ap);
	va_end(ap);
	fprintf(debuglogfile, "\n");
	fflush(debuglogfile);
}

PF(3, 4) void
debuglog2(const struct place *p, const struct place *p2, const char *fmt, ...)
{
	va_list ap;

	if (debuglogfile == NULL) {
		return;
	}

	fprintf(debuglogfile, "%s:%u: ", place_getname(p), p->line);
	if (place_samefile(p, p2)) {
		fprintf(debuglogfile, "(block began at line %u) ",
			p2->line);
	} else {
		fprintf(debuglogfile, "(block began at %s:%u)",
			place_getname(p2), p2->line);
	}
	va_start(ap, fmt);
	vfprintf(debuglogfile, fmt, ap);
	va_end(ap);
	fprintf(debuglogfile, "\n");
	fflush(debuglogfile);
}

////////////////////////////////////////////////////////////
// module init and cleanup

void
place_init(void)
{
	placefilearray_init(&placefiles);
}

void
place_cleanup(void)
{
	placefilearray_destroyall(&placefiles);
	placefilearray_cleanup(&placefiles);
}
