/*
 * I'm tired of doing "vsnprintf()" etc just to open a
 * file, so here's a "return static buffer with printf"
 * interface for paths.
 *
 * It's obviously not thread-safe. Sue me. But it's quite
 * useful for doing things like
 *
 *   f = open(mkpath("%s/%s.perf", base, name), O_RDONLY);
 *
 * which is what it's designed for.
 */
#include "cache.h"
#include "util.h"
#include <limits.h>

static char bad_path[] = "/bad-path/";
/*
 * One hack:
 */
static char *get_pathname(void)
{
	static char pathname_array[4][PATH_MAX];
	static int idx;

	return pathname_array[3 & ++idx];
}

static char *cleanup_path(char *path)
{
	/* Clean it up */
	if (!memcmp(path, "./", 2)) {
		path += 2;
		while (*path == '/')
			path++;
	}
	return path;
}

char *mkpath(const char *fmt, ...)
{
	va_list args;
	unsigned len;
	char *pathname = get_pathname();

	va_start(args, fmt);
	len = vsnprintf(pathname, PATH_MAX, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}
