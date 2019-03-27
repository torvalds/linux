/*-
 * Copyright (c) 2016 Andriy Gapon <avg@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kenv.h>

#include <libzfs.h>

/* Keep in sync with zfsboot.c. */
#define MAX_COMMAND_LEN	512

int main(int argc, const char * const *argv)
{
	char buf[32];
	libzfs_handle_t *hdl;
	uint64_t pool_guid;
	uint64_t vdev_guid;
	int zfs_fd;
	int len;

	if (argc != 2) {
		fprintf(stderr, "usage: zfsbootcfg <boot.config(5) options>\n");
		return (1);
	}

	len = strlen(argv[1]);
	if (len >= MAX_COMMAND_LEN) {
		fprintf(stderr, "options string is too long\n");
		return (1);
	}

	if (kenv(KENV_GET, "vfs.zfs.boot.primary_pool", buf, sizeof(buf)) <= 0) {
		perror("can't get vfs.zfs.boot.primary_pool");
		return (1);
	}
	pool_guid = strtoumax(buf, NULL, 10);
	if (pool_guid == 0) {
		perror("can't parse vfs.zfs.boot.primary_pool");
		return (1);
	}

	if (kenv(KENV_GET, "vfs.zfs.boot.primary_vdev", buf, sizeof(buf)) <= 0) {
		perror("can't get vfs.zfs.boot.primary_vdev");
		return (1);
	}
	vdev_guid = strtoumax(buf, NULL, 10);
	if (vdev_guid == 0) {
		perror("can't parse vfs.zfs.boot.primary_vdev");
		return (1);
	}

	if ((hdl = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "internal error: failed to "
		    "initialize ZFS library\n");
		return (1);
	}

	if (zpool_nextboot(hdl, pool_guid, vdev_guid, argv[1]) != 0) {
		perror("ZFS_IOC_NEXTBOOT failed");
		libzfs_fini(hdl);
		return (1);
	}

	libzfs_fini(hdl);
	printf("zfs next boot options are successfully written\n");
	return (0);
}
