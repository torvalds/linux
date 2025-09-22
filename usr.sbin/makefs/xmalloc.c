/*	$OpenBSD: xmalloc.c,v 1.2 2016/10/16 20:26:56 natano Exp $	*/

#include <err.h>
#include <stdlib.h>
#include <string.h>

void *
emalloc(size_t size)
{
	void *v;

	if ((v = malloc(size)) == NULL)
		err(1, "malloc");
	return v;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *v;

	if ((v = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return v;
}

void *
erealloc(void *ptr, size_t size)
{
	void *v;

	if ((v = realloc(ptr, size)) == NULL)
		err(1, "realloc");
	return v;
}

char *
estrdup(const char *s)
{
	char *s2;

	if ((s2 = strdup(s)) == NULL)
		err(1, "strdup");
	return s2;
}
