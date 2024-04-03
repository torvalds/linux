// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002-2005 Roman Zippel <zippel@linux-m68k.org>
 * Copyright (C) 2002-2005 Sam Ravnborg <sam@ravnborg.org>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "hashtable.h"
#include "lkc.h"

unsigned int strhash(const char *s)
{
	/* fnv32 hash */
	unsigned int hash = 2166136261U;

	for (; *s; s++)
		hash = (hash ^ *s) * 0x01000193;
	return hash;
}

/* hash table of all parsed Kconfig files */
static HASHTABLE_DEFINE(file_hashtable, 1U << 11);

struct file {
	struct hlist_node node;
	char name[];
};

/* file already present in list? If not add it */
const char *file_lookup(const char *name)
{
	struct file *file;
	size_t len;
	int hash = strhash(name);

	hash_for_each_possible(file_hashtable, file, node, hash)
		if (!strcmp(name, file->name))
			return file->name;

	len = strlen(name);
	file = xmalloc(sizeof(*file) + len + 1);
	memset(file, 0, sizeof(*file));
	memcpy(file->name, name, len);
	file->name[len] = '\0';

	hash_add(file_hashtable, &file->node, hash);

	str_printf(&autoconf_cmd, "\t%s \\\n", name);

	return file->name;
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
