/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Directory access for NOLIBC
 * Copyright (C) 2025 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_DIRENT_H
#define _NOLIBC_DIRENT_H

#include "compiler.h"
#include "stdint.h"
#include "types.h"
#include "fcntl.h"

#include <linux/limits.h>

struct dirent {
	ino_t	d_ino;
	char	d_name[NAME_MAX + 1];
};

/* See comment of FILE in stdio.h */
typedef struct {
	char dummy[1];
} DIR;

static __attribute__((unused))
DIR *fdopendir(int fd)
{
	if (fd < 0) {
		SET_ERRNO(EBADF);
		return NULL;
	}
	return (DIR *)(intptr_t)~fd;
}

static __attribute__((unused))
DIR *opendir(const char *name)
{
	int fd;

	fd = open(name, O_RDONLY);
	if (fd == -1)
		return NULL;
	return fdopendir(fd);
}

static __attribute__((unused))
int closedir(DIR *dirp)
{
	intptr_t i = (intptr_t)dirp;

	if (i >= 0) {
		SET_ERRNO(EBADF);
		return -1;
	}
	return close(~i);
}

static __attribute__((unused))
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result)
{
	char buf[sizeof(struct linux_dirent64) + NAME_MAX + 1] __nolibc_aligned_as(struct linux_dirent64);
	struct linux_dirent64 *ldir = (void *)buf;
	intptr_t i = (intptr_t)dirp;
	int fd, ret;

	if (i >= 0)
		return EBADF;

	fd = ~i;

	ret = sys_getdents64(fd, ldir, sizeof(buf));
	if (ret < 0)
		return -ret;
	if (ret == 0) {
		*result = NULL;
		return 0;
	}

	/*
	 * getdents64() returns as many entries as fit the buffer.
	 * readdir() can only return one entry at a time.
	 * Make sure the non-returned ones are not skipped.
	 */
	ret = lseek(fd, ldir->d_off, SEEK_SET);
	if (ret == -1)
		return errno;

	entry->d_ino = ldir->d_ino;
	/* the destination should always be big enough */
	strlcpy(entry->d_name, ldir->d_name, sizeof(entry->d_name));
	*result = entry;
	return 0;
}

#endif /* _NOLIBC_DIRENT_H */
