// SPDX-License-Identifier: GPL-2.0
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
#include "path.h"
#include "cache.h"
#include <linux/kernel.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

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

int path__join(char *bf, size_t size, const char *path1, const char *path2)
{
	return scnprintf(bf, size, "%s%s%s", path1, path1[0] ? "/" : "", path2);
}

int path__join3(char *bf, size_t size, const char *path1, const char *path2, const char *path3)
{
	return scnprintf(bf, size, "%s%s%s%s%s", path1, path1[0] ? "/" : "",
			 path2, path2[0] ? "/" : "", path3);
}

bool is_regular_file(const char *file)
{
	struct stat st;

	if (stat(file, &st))
		return false;

	return S_ISREG(st.st_mode);
}

/* Helper function for filesystems that return a dent->d_type DT_UNKNOWN */
bool is_directory(const char *base_path, const struct dirent *dent)
{
	char path[PATH_MAX];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s", base_path, dent->d_name);
	if (stat(path, &st))
		return false;

	return S_ISDIR(st.st_mode);
}

bool is_executable_file(const char *base_path, const struct dirent *dent)
{
	char path[PATH_MAX];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s", base_path, dent->d_name);
	if (stat(path, &st))
		return false;

	return !S_ISDIR(st.st_mode) && (st.st_mode & S_IXUSR);
}
