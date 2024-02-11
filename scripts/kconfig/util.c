// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002-2005 Roman Zippel <zippel@linux-m68k.org>
 * Copyright (C) 2002-2005 Sam Ravnborg <sam@ravnborg.org>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "lkc.h"

/* file already present in list? If not add it */
struct file *file_lookup(const char *name)
{
	struct file *file;

	for (file = file_list; file; file = file->next) {
		if (!strcmp(name, file->name)) {
			return file;
		}
	}

	file = xmalloc(sizeof(*file));
	memset(file, 0, sizeof(*file));
	file->name = xstrdup(name);
	file->next = file_list;
	file_list = file;
	return file;
}

/* Allocate initial growable string */
struct gstr str_new(void)
{
	struct gstr gs;
	gs.s = xmalloc(sizeof(char) * 64);
	gs.len = 64;
	gs.max_width = 0;
	strcpy(gs.s, "\0");
	return gs;
}

/* Free storage for growable string */
void str_free(struct gstr *gs)
{
	free(gs->s);
	gs->s = NULL;
	gs->len = 0;
}

/* Append to growable string */
void str_append(struct gstr *gs, const char *s)
{
	size_t l;
	if (s) {
		l = strlen(gs->s) + strlen(s) + 1;
		if (l > gs->len) {
			gs->s = xrealloc(gs->s, l);
			gs->len = l;
		}
		strcat(gs->s, s);
	}
}

/* Append printf formatted string to growable string */
void str_printf(struct gstr *gs, const char *fmt, ...)
{
	va_list ap;
	char s[10000]; /* big enough... */
	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	str_append(gs, s);
	va_end(ap);
}

/* Retrieve value of growable string */
char *str_get(struct gstr *gs)
{
	return gs->s;
}

void *xmalloc(size_t size)
{
	void *p = malloc(size);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

void *xcalloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb, size);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

void *xrealloc(void *p, size_t size)
{
	p = realloc(p, size);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

char *xstrdup(const char *s)
{
	char *p;

	p = strdup(s);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

char *xstrndup(const char *s, size_t n)
{
	char *p;

	p = strndup(s, n);
	if (p)
		return p;
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}
