// SPDX-License-Identifier: GPL-2.0
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

char *mkpath(char *path_buf, size_t sz, const char *fmt, ...)
{
	va_list args;
	unsigned len;

	va_start(args, fmt);
	len = vsnprintf(path_buf, sz, fmt, args);
	va_end(args);
	if (len >= sz)
		strncpy(path_buf, "/bad-path/", sz);
	return cleanup_path(path_buf);
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

bool is_directory_at(int dir_fd, const char *path)
{
	struct stat st;

	if (fstatat(dir_fd, path, &st, /*flags=*/0))
		return false;

	return S_ISDIR(st.st_mode);
}
