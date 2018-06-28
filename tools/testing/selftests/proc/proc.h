#pragma once
#undef NDEBUG
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static inline bool streq(const char *s1, const char *s2)
{
	return strcmp(s1, s2) == 0;
}

static unsigned long long xstrtoull(const char *p, char **end)
{
	if (*p == '0') {
		*end = (char *)p + 1;
		return 0;
	} else if ('1' <= *p && *p <= '9') {
		unsigned long long val;

		errno = 0;
		val = strtoull(p, end, 10);
		assert(errno == 0);
		return val;
	} else
		assert(0);
}

static struct dirent *xreaddir(DIR *d)
{
	struct dirent *de;

	errno = 0;
	de = readdir(d);
	assert(de || errno == 0);
	return de;
}
