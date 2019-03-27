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

#pragma ident	"@(#)mmapwrite.c	1.4	07/05/25 SMI"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>

/*
 * --------------------------------------------------------------------
 * Bug Id: 5032643
 *
 * Simply writing to a file and mmaping that file at the same time can
 * result in deadlock.  Nothing perverse like writing from the file's
 * own mapping is required.
 * --------------------------------------------------------------------
 */

static void *
mapper(void *fdp)
{
	void *addr;
	int fd = *(int *)fdp;

	if ((addr =
	    mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	for (;;) {
		if (mmap(addr, 8192, PROT_READ,
		    MAP_SHARED|MAP_FIXED, fd, 0) == MAP_FAILED) {
			perror("mmap");
			exit(1);
		}
	}
	/* NOTREACHED */
	return ((void *)1);
}

int
main(int argc, char **argv)
{
	int fd;
	char buf[BUFSIZ];
	pthread_t pt;

	if (argc != 2) {
		(void) printf("usage: %s <file name>\n", argv[0]);
		exit(1);
	}

	if ((fd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666)) == -1) {
		perror("open");
		exit(1);
	}

	if (pthread_create(&pt, NULL, mapper, &fd) != 0) {
		perror("pthread_create");
		exit(1);
	}
	for (;;) {
		if (write(fd, buf, sizeof (buf)) == -1) {
			perror("write");
			exit(1);
		}
	}

	/* NOTREACHED */
	return (0);
}
