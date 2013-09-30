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

static char bad_path[] = "/bad-path/";
/*
 * Two hacks:
 */

static const char *get_perf_dir(void)
{
	return ".";
}

#ifndef HAVE_STRLCPY_SUPPORT
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
#endif

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

static char *perf_vsnpath(char *buf, size_t n, const char *fmt, va_list args)
{
	const char *perf_dir = get_perf_dir();
	size_t len;

	len = strlen(perf_dir);
	if (n < len + 1)
		goto bad;
	memcpy(buf, perf_dir, len);
	if (len && !is_dir_sep(perf_dir[len-1]))
		buf[len++] = '/';
	len += vsnprintf(buf + len, n - len, fmt, args);
	if (len >= n)
		goto bad;
	return cleanup_path(buf);
bad:
	strlcpy(buf, bad_path, n);
	return buf;
}

char *perf_pathdup(const char *fmt, ...)
{
	char path[PATH_MAX];
	va_list args;
	va_start(args, fmt);
	(void)perf_vsnpath(path, sizeof(path), fmt, args);
	va_end(args);
	return xstrdup(path);
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

char *perf_path(const char *fmt, ...)
{
	const char *perf_dir = get_perf_dir();
	char *pathname = get_pathname();
	va_list args;
	unsigned len;

	len = strlen(perf_dir);
	if (len > PATH_MAX-100)
		return bad_path;
	memcpy(pathname, perf_dir, len);
	if (len && perf_dir[len-1] != '/')
		pathname[len++] = '/';
	va_start(args, fmt);
	len += vsnprintf(pathname + len, PATH_MAX - len, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}

/* strip arbitrary amount of directory separators at end of path */
static inline int chomp_trailing_dir_sep(const char *path, int len)
{
	while (len && is_dir_sep(path[len - 1]))
		len--;
	return len;
}

/*
 * If path ends with suffix (complete path components), returns the
 * part before suffix (sans trailing directory separators).
 * Otherwise returns NULL.
 */
char *strip_path_suffix(const char *path, const char *suffix)
{
	int path_len = strlen(path), suffix_len = strlen(suffix);

	while (suffix_len) {
		if (!path_len)
			return NULL;

		if (is_dir_sep(path[path_len - 1])) {
			if (!is_dir_sep(suffix[suffix_len - 1]))
				return NULL;
			path_len = chomp_trailing_dir_sep(path, path_len);
			suffix_len = chomp_trailing_dir_sep(suffix, suffix_len);
		}
		else if (path[--path_len] != suffix[--suffix_len])
			return NULL;
	}

	if (path_len && !is_dir_sep(path[path_len - 1]))
		return NULL;
	return strndup(path, chomp_trailing_dir_sep(path, path_len));
}
