// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 * Copyright (C) 2026 Amutable GmbH
 */

#ifndef __RESOLVEAT_H__
#define __RESOLVEAT_H__

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/openat2.h>
#include "kselftest_harness.h"

#define BUILD_BUG_ON(e) ((void)(sizeof(struct { int:(-!!(e)); })))

#define OPEN_HOW_SIZE_VER0	24 /* sizeof first published struct */
#define OPEN_HOW_SIZE_LATEST	OPEN_HOW_SIZE_VER0

__maybe_unused
static bool needs_openat2(const struct open_how *how)
{
	return how->resolve != 0;
}

__maybe_unused
static int raw_openat2(int dfd, const char *path, void *how, size_t size)
{
	int ret = syscall(__NR_openat2, dfd, path, how, size);

	return ret >= 0 ? ret : -errno;
}

__maybe_unused
static int sys_openat2(int dfd, const char *path, struct open_how *how)
{
	return raw_openat2(dfd, path, how, sizeof(*how));
}

__maybe_unused
static int sys_openat(int dfd, const char *path, struct open_how *how)
{
	int ret = openat(dfd, path, how->flags, how->mode);

	return ret >= 0 ? ret : -errno;
}

__maybe_unused
static int sys_renameat2(int olddirfd, const char *oldpath,
			 int newdirfd, const char *newpath, unsigned int flags)
{
	int ret = syscall(__NR_renameat2, olddirfd, oldpath,
					  newdirfd, newpath, flags);

	return ret >= 0 ? ret : -errno;
}

__maybe_unused
static int touchat(int dfd, const char *path)
{
	int fd = openat(dfd, path, O_CREAT, 0700);

	if (fd >= 0)
		close(fd);
	return fd;
}

__maybe_unused
static char *fdreadlink(struct __test_metadata *_metadata, int fd)
{
	char *target, *tmp;

	ASSERT_GT(asprintf(&tmp, "/proc/self/fd/%d", fd), 0);

	target = malloc(PATH_MAX);
	ASSERT_NE(target, NULL);
	memset(target, 0, PATH_MAX);

	ASSERT_GT(readlink(tmp, target, PATH_MAX), 0);

	free(tmp);
	return target;
}

__maybe_unused
static bool fdequal(struct __test_metadata *_metadata, int fd,
		    int dfd, const char *path)
{
	char *fdpath, *dfdpath, *other;
	bool cmp;

	fdpath = fdreadlink(_metadata, fd);
	dfdpath = fdreadlink(_metadata, dfd);

	if (!path) {
		ASSERT_GT(asprintf(&other, "%s", dfdpath), 0);
	} else if (*path == '/') {
		ASSERT_GT(asprintf(&other, "%s", path), 0);
	} else {
		ASSERT_GT(asprintf(&other, "%s/%s", dfdpath, path), 0);
	}

	cmp = !strcmp(fdpath, other);

	free(fdpath);
	free(dfdpath);
	free(other);
	return cmp;
}

static bool openat2_supported = false;

__attribute__((constructor))
static void __detect_openat2_supported(void)
{
	struct open_how how = {};
	int fd;

	BUILD_BUG_ON(sizeof(struct open_how) != OPEN_HOW_SIZE_VER0);

	/* Check openat2(2) support. */
	fd = sys_openat2(AT_FDCWD, ".", &how);
	openat2_supported = (fd >= 0);

	if (fd >= 0)
		close(fd);
}

#endif /* __RESOLVEAT_H__ */
