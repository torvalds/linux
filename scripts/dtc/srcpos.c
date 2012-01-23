/*
 * Copyright 2007 Jon Loeliger, Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#define _GNU_SOURCE

#include <stdio.h>

#include "dtc.h"
#include "srcpos.h"


static char *dirname(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (slash) {
		int len = slash - path;
		char *dir = xmalloc(len + 1);

		memcpy(dir, path, len);
		dir[len] = '\0';
		return dir;
	}
	return NULL;
}

FILE *depfile; /* = NULL */
struct srcfile_state *current_srcfile; /* = NULL */

/* Detect infinite include recursion. */
#define MAX_SRCFILE_DEPTH     (100)
static int srcfile_depth; /* = 0 */

FILE *srcfile_relative_open(const char *fname, char **fullnamep)
{
	FILE *f;
	char *fullname;

	if (streq(fname, "-")) {
		f = stdin;
		fullname = xstrdup("<stdin>");
	} else {
		if (!current_srcfile || !current_srcfile->dir
		    || (fname[0] == '/'))
			fullname = xstrdup(fname);
		else
			fullname = join_path(current_srcfile->dir, fname);

		f = fopen(fullname, "r");
		if (!f)
			die("Couldn't open \"%s\": %s\n", fname,
			    strerror(errno));
	}

	if (depfile)
		fprintf(depfile, " %s", fullname);

	if (fullnamep)
		*fullnamep = fullname;
	else
		free(fullname);

	return f;
}

void srcfile_push(const char *fname)
{
	struct srcfile_state *srcfile;

	if (srcfile_depth++ >= MAX_SRCFILE_DEPTH)
		die("Includes nested too deeply");

	srcfile = xmalloc(sizeof(*srcfile));

	srcfile->f = srcfile_relative_open(fname, &srcfile->name);
	srcfile->dir = dirname(srcfile->name);
	srcfile->prev = current_srcfile;

	srcfile->lineno = 1;
	srcfile->colno = 1;

	current_srcfile = srcfile;
}

int srcfile_pop(void)
{
	struct srcfile_state *srcfile = current_srcfile;

	assert(srcfile);

	current_srcfile = srcfile->prev;

	if (fclose(srcfile->f))
		die("Error closing \"%s\": %s\n", srcfile->name,
		    strerror(errno));

	/* FIXME: We allow the srcfile_state structure to leak,
	 * because it could still be referenced from a location
	 * variable being carried through the parser somewhere.  To
	 * fix this we could either allocate all the files from a
	 * table, or use a pool allocator. */

	return current_srcfile ? 1 : 0;
}

/*
 * The empty source position.
 */

struct srcpos srcpos_empty = {
	.first_line = 0,
	.first_column = 0,
	.last_line = 0,
	.last_column = 0,
	.file = NULL,
};

#define TAB_SIZE      8

void srcpos_update(struct srcpos *pos, const char *text, int len)
{
	int i;

	pos->file = current_srcfile;

	pos->first_line = current_srcfile->lineno;
	pos->first_column = current_srcfile->colno;

	for (i = 0; i < len; i++)
		if (text[i] == '\n') {
			current_srcfile->lineno++;
			current_srcfile->colno = 1;
		} else if (text[i] == '\t') {
			current_srcfile->colno =
				ALIGN(current_srcfile->colno, TAB_SIZE);
		} else {
			current_srcfile->colno++;
		}

	pos->last_line = current_srcfile->lineno;
	pos->last_column = current_srcfile->colno;
}

struct srcpos *
srcpos_copy(struct srcpos *pos)
{
	struct srcpos *pos_new;

	pos_new = xmalloc(sizeof(struct srcpos));
	memcpy(pos_new, pos, sizeof(struct srcpos));

	return pos_new;
}



void
srcpos_dump(struct srcpos *pos)
{
	printf("file        : \"%s\"\n",
	       pos->file ? (char *) pos->file : "<no file>");
	printf("first_line  : %d\n", pos->first_line);
	printf("first_column: %d\n", pos->first_column);
	printf("last_line   : %d\n", pos->last_line);
	printf("last_column : %d\n", pos->last_column);
	printf("file        : %s\n", pos->file->name);
}


char *
srcpos_string(struct srcpos *pos)
{
	const char *fname = "<no-file>";
	char *pos_str;
	int rc;

	if (pos)
		fname = pos->file->name;


	if (pos->first_line != pos->last_line)
		rc = asprintf(&pos_str, "%s:%d.%d-%d.%d", fname,
			      pos->first_line, pos->first_column,
			      pos->last_line, pos->last_column);
	else if (pos->first_column != pos->last_column)
		rc = asprintf(&pos_str, "%s:%d.%d-%d", fname,
			      pos->first_line, pos->first_column,
			      pos->last_column);
	else
		rc = asprintf(&pos_str, "%s:%d.%d", fname,
			      pos->first_line, pos->first_column);

	if (rc == -1)
		die("Couldn't allocate in srcpos string");

	return pos_str;
}

void
srcpos_verror(struct srcpos *pos, char const *fmt, va_list va)
{
       const char *srcstr;

       srcstr = srcpos_string(pos);

       fprintf(stdout, "Error: %s ", srcstr);
       vfprintf(stdout, fmt, va);
       fprintf(stdout, "\n");
}

void
srcpos_error(struct srcpos *pos, char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	srcpos_verror(pos, fmt, va);
	va_end(va);
}


void
srcpos_warn(struct srcpos *pos, char const *fmt, ...)
{
	const char *srcstr;
	va_list va;
	va_start(va, fmt);

	srcstr = srcpos_string(pos);

	fprintf(stderr, "Warning: %s ", srcstr);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");

	va_end(va);
}
