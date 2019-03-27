/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)dir_rd_update.c	1.2	07/01/09 SMI"

/*
 * Assertion:
 *
 *	A read operation and directory update operation performed
 *      concurrently on the same directory can lead to deadlock
 *	on a UFS logging file system, but not on a ZFS file system.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define	TMP_DIR /tmp

static char dirpath[256];

int
main(int argc, char **argv)
{
	char *cp1 = "";
	int i = 0;
	int ret = 0;
	int testdd = 0;
	pid_t pid;
	static const int op_num = 5;

	if (argc == 1) {
		(void) printf("Usage: %s <mount point>\n", argv[0]);
		exit(-1);
	}
	for (i = 0; i < 256; i++) {
		dirpath[i] = 0;
	}

	cp1 = argv[1];
	(void) strcpy(&dirpath[0], (const char *)cp1);
	(void) strcat(&dirpath[strlen(dirpath)], "TMP_DIR");

	ret = mkdir(dirpath, 0777);
	if (ret != 0) {
		if (errno != EEXIST) {
			(void) printf(
			"%s: mkdir(<%s>, 0777) failed: errno (decimal)=%d\n",
				argv[0], dirpath, errno);
			exit(-1);
		}
	}
	testdd = open(dirpath, O_RDONLY|O_SYNC);
	if (testdd < 0) {
		(void) printf(
"%s: open(<%s>, O_RDONLY|O_SYNC) failed: errno (decimal)=%d\n",
			argv[0], dirpath, errno);
		exit(-1);
	} else {
		(void) close(testdd);
	}
	pid = fork();
	if (pid > 0) {
		int fd = open(dirpath, O_RDONLY|O_SYNC);
		char buf[16];
		int rdret;
		int j = 0;

		while (j < op_num) {
			(void) sleep(1);
			rdret = read(fd, buf, 16);
			if (rdret == -1) {
				(void) printf("readdir failed");
			}
			j++;
		}
	} else if (pid == 0) {
		int fd = open(dirpath, O_RDONLY);
		int chownret;
		int k = 0;

		while (k < op_num) {
			(void) sleep(1);
			chownret = fchown(fd, 0, 0);
			if (chownret == -1) {
				(void) printf("chown failed");
			}

			k++;
		}
	}

	return (0);
}
