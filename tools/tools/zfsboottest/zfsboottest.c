/*-
 * Copyright (c) 2010 Doug Rabson
 * Copyright (c) 2011 Andriy Gapon
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* $FreeBSD$ */

#include <sys/param.h>
#include <sys/disk.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <md5.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#define NBBY 8

int
pager_output(const char *line)
{

	fprintf(stderr, "%s", line);
	return (0);
}

uint64_t
ldi_get_size(void *priv)
{
	struct stat sb;
	int fd;

	fd = *(int *)priv;
	if (fstat(fd, &sb) != 0)
		return (0);
	if (S_ISCHR(sb.st_mode) && ioctl(fd, DIOCGMEDIASIZE, &sb.st_size) != 0)
		return (0);
	return (sb.st_size);
}

#define ZFS_TEST
#define	printf(...)	 fprintf(stderr, __VA_ARGS__)
#include "libzfs.h"
#include "zfsimpl.c"
#undef printf

static int
vdev_read(vdev_t *vdev, void *priv, off_t off, void *buf, size_t bytes)
{
	int fd = *(int *)priv;

	if (pread(fd, buf, bytes, off) != bytes)
		return (-1);
	return (0);
}

static int
zfs_read(spa_t *spa, dnode_phys_t *dn, void *buf, size_t size, off_t off)
{
	const znode_phys_t *zp = (const znode_phys_t *) dn->dn_bonus;
	size_t n;
	int rc;

	n = size;
	if (off + n > zp->zp_size)
		n = zp->zp_size - off;

	rc = dnode_read(spa, dn, off, buf, n);
	if (rc != 0)
		return (-rc);

	return (n);
}

int
main(int argc, char** argv)
{
	char buf[512], hash[33];
	MD5_CTX ctx;
	struct stat sb;
	struct zfsmount zfsmnt;
	dnode_phys_t dn;
#if 0
	uint64_t rootobj;
#endif
	spa_t *spa;
	off_t off;
	ssize_t n;
	int i, failures, *fd;

	zfs_init();
	if (argc == 1) {
		static char *av[] = {
			"zfsboottest",
			"/dev/gpt/system0",
			"/dev/gpt/system1",
			"-",
			"/boot/loader",
			"/boot/support.4th",
			"/boot/kernel/kernel",
			NULL,
		};
		argc = sizeof(av) / sizeof(av[0]) - 1;
		argv = av;
	}
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-") == 0)
			break;
	}
	fd = malloc(sizeof(fd[0]) * (i - 1));
	if (fd == NULL)
		errx(1, "Unable to allocate memory.");
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-") == 0)
			break;
		fd[i - 1] = open(argv[i], O_RDONLY);
		if (fd[i - 1] == -1) {
			warn("open(%s) failed", argv[i]);
			continue;
		}
		if (vdev_probe(vdev_read, &fd[i - 1], NULL) != 0) {
			warnx("vdev_probe(%s) failed", argv[i]);
			close(fd[i - 1]);
		}
	}

	STAILQ_FOREACH(spa, &zfs_pools, spa_link) {
		if (zfs_spa_init(spa)) {
			fprintf(stderr, "can't init pool %s\n", spa->spa_name);
			exit(1);
		}
	}

	spa_all_status();

	spa = STAILQ_FIRST(&zfs_pools);
	if (spa == NULL) {
		fprintf(stderr, "no pools\n");
		exit(1);
	}

#if 0
	uint64_t rootobj;
	if (zfs_get_root(spa, &rootobj)) {
		fprintf(stderr, "can't get root\n");
		exit(1);
	}

	if (zfs_mount(spa, rootobj, &zfsmnt)) {
#else
	if (zfs_mount(spa, 0, &zfsmnt)) {
		fprintf(stderr, "can't mount\n");
		exit(1);
#endif
	}

	printf("\n");
	for (++i, failures = 0; i < argc; i++) {
		if (zfs_lookup(&zfsmnt, argv[i], &dn)) {
			fprintf(stderr, "%s: can't lookup\n", argv[i]);
			failures++;
			continue;
		}

		if (zfs_dnode_stat(spa, &dn, &sb)) {
			fprintf(stderr, "%s: can't stat\n", argv[i]);
			failures++;
			continue;
		}

		off = 0;
		MD5Init(&ctx);
		do {
			n = sb.st_size - off;
			n = n > sizeof(buf) ? sizeof(buf) : n;
			n = zfs_read(spa, &dn, buf, n, off);
			if (n < 0) {
				fprintf(stderr, "%s: zfs_read failed\n",
				    argv[i]);
				failures++;
				break;
			}
			MD5Update(&ctx, buf, n);
			off += n;
		} while (off < sb.st_size);
		if (off < sb.st_size)
			continue;
		MD5End(&ctx, hash);
		printf("%s %s\n", hash, argv[i]);
	}

	return (failures == 0 ? 0 : 1);
}
