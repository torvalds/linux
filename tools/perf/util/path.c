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

char *mksnpath(char *buf, size_t n, const char *fmt, ...)
{
	va_list args;
	unsigned len;

	va_start(args, fmt);
	len = vsnprintf(buf, n, fmt, args);
	va_end(args);
	if (len >= n) {
		strlcpy(buf, bad_path, n);
		return buf;
	}
	return cleanup_path(buf);
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

char *perf_snpath(char *buf, size_t n, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	(void)perf_vsnpath(buf, n, fmt, args);
	va_end(args);
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


/* perf_mkstemp() - create tmp file honoring TMPDIR variable */
int perf_mkstemp(char *path, size_t len, const char *template)
{
	const char *tmp;
	size_t n;

	tmp = getenv("TMPDIR");
	if (!tmp)
		tmp = "/tmp";
	n = snprintf(path, len, "%s/%s", tmp, template);
	if (len <= n) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return mkstemp(path);
}


const char *make_relative_path(const char *abs_path, const char *base)
{
	static char buf[PATH_MAX + 1];
	int baselen;

	if (!base)
		return abs_path;

	baselen = strlen(base);
	if (prefixcmp(abs_path, base))
		return abs_path;
	if (abs_path[baselen] == '/')
		baselen++;
	else if (base[baselen - 1] != '/')
		return abs_path;

	strcpy(buf, abs_path + baselen);

	return buf;
}

/*
 * It is okay if dst == src, but they should not overlap otherwise.
 *
 * Performs the following normalizations on src, storing the result in dst:
 * - Ensures that components are separated by '/' (Windows only)
 * - Squashes sequences of '/'.
 * - Removes "." components.
 * - Removes ".." components, and the components the precede them.
 * Returns failure (non-zero) if a ".." component appears as first path
 * component anytime during the normalization. Otherwise, returns success (0).
 *
 * Note that this function is purely textual.  It does not follow symlinks,
 * verify the existence of the path, or make any system calls.
 */
int normalize_path_copy(char *dst, const char *src)
{
	char *dst0;

	if (has_dos_drive_prefix(src)) {
		*dst++ = *src++;
		*dst++ = *src++;
	}
	dst0 = dst;

	if (is_dir_sep(*src)) {
		*dst++ = '/';
		while (is_dir_sep(*src))
			src++;
	}

	for (;;) {
		char c = *src;

		/*
		 * A path component that begins with . could be
		 * special:
		 * (1) "." and ends   -- ignore and terminate.
		 * (2) "./"           -- ignore them, eat slash and continue.
		 * (3) ".." and ends  -- strip one and terminate.
		 * (4) "../"          -- strip one, eat slash and continue.
		 */
		if (c == '.') {
			if (!src[1]) {
				/* (1) */
				src++;
			} else if (is_dir_sep(src[1])) {
				/* (2) */
				src += 2;
				while (is_dir_sep(*src))
					src++;
				continue;
			} else if (src[1] == '.') {
				if (!src[2]) {
					/* (3) */
					src += 2;
					goto up_one;
				} else if (is_dir_sep(src[2])) {
					/* (4) */
					src += 3;
					while (is_dir_sep(*src))
						src++;
					goto up_one;
				}
			}
		}

		/* copy up to the next '/', and eat all '/' */
		while ((c = *src++) != '\0' && !is_dir_sep(c))
			*dst++ = c;
		if (is_dir_sep(c)) {
			*dst++ = '/';
			while (is_dir_sep(c))
				c = *src++;
			src--;
		} else if (!c)
			break;
		continue;

	up_one:
		/*
		 * dst0..dst is prefix portion, and dst[-1] is '/';
		 * go up one level.
		 */
		dst--;	/* go to trailing '/' */
		if (dst <= dst0)
			return -1;
		/* Windows: dst[-1] cannot be backslash anymore */
		while (dst0 < dst && dst[-1] != '/')
			dst--;
	}
	*dst = '\0';
	return 0;
}

/*
 * path = Canonical absolute path
 * prefix_list = Colon-separated list of absolute paths
 *
 * Determines, for each path in prefix_list, whether the "prefix" really
 * is an ancestor directory of path.  Returns the length of the longest
 * ancestor directory, excluding any trailing slashes, or -1 if no prefix
 * is an ancestor.  (Note that this means 0 is returned if prefix_list is
 * "/".) "/foo" is not considered an ancestor of "/foobar".  Directories
 * are not considered to be their own ancestors.  path must be in a
 * canonical form: empty components, or "." or ".." components are not
 * allowed.  prefix_list may be null, which is like "".
 */
int longest_ancestor_length(const char *path, const char *prefix_list)
{
	char buf[PATH_MAX+1];
	const char *ceil, *colon;
	int len, max_len = -1;

	if (prefix_list == NULL || !strcmp(path, "/"))
		return -1;

	for (colon = ceil = prefix_list; *colon; ceil = colon+1) {
		for (colon = ceil; *colon && *colon != PATH_SEP; colon++);
		len = colon - ceil;
		if (len == 0 || len > PATH_MAX || !is_absolute_path(ceil))
			continue;
		strlcpy(buf, ceil, len+1);
		if (normalize_path_copy(buf, buf) < 0)
			continue;
		len = strlen(buf);
		if (len > 0 && buf[len-1] == '/')
			buf[--len] = '\0';

		if (!strncmp(path, buf, len) &&
		    path[len] == '/' &&
		    len > max_len) {
			max_len = len;
		}
	}

	return max_len;
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
	return xstrndup(path, chomp_trailing_dir_sep(path, path_len));
}
