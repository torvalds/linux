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

#pragma ident	"@(#)threadsappend.c	1.3	07/05/25 SMI"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/*
 * The size of the output file, "go.out", should be 80*8192*2 = 1310720
 *
 * $ cd /tmp; go; ls -l go.out
 * done.
 * -rwxr-xr-x	1 jdm	staff	1310720 Apr 13 19:45 go.out
 * $ cd /zfs; go; ls -l go.out
 * done.
 * -rwxr-xr-x	1 jdm	staff	663552 Apr 13 19:45 go.out
 *
 * The file on zfs is short as it does not appear that zfs is making the
 * implicit seek to EOF and the actual write atomic. From the SUSv3
 * interface spec, behavior is undefined if concurrent writes are performed
 * from multi-processes to a single file. So I don't know if this is a
 * standards violation, but I cannot find any such disclaimers in our
 * man pages. This issue came up at a customer site in another context, and
 * the suggestion was to open the file with O_APPEND, but that wouldn't
 * help with zfs(see 4977529). Also see bug# 5031301.
 */

static int outfd = 0;

static void *
go(void *data)
{
	int i = 0, n = *(int *)data;
	ssize_t	ret = 0;
	char buf[8192] = {0};
	(void) memset(buf, n, sizeof (buf));

	for (i = 0; i < 80; i++) {
		ret = write(outfd, buf, sizeof (buf));
	}
	return (NULL);
}

static void
usage()
{
	(void) fprintf(stderr,
	    "usage: zfs_threadsappend <file name>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	pthread_t threads[2];
	int	  ret = 0;
	long	  ncpus = 0;
	int	  i;

	if (argc != 2) {
		usage();
	}

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpus < 0) {
		(void) fprintf(stderr,
		    "Invalid return from sysconf(_SC_NPROCESSORS_ONLN)"
		    " : errno (decimal)=%d\n", errno);
		exit(1);
	}
	if (ncpus < 2) {
		(void) fprintf(stderr,
		    "Must execute this binary on a multi-processor system\n");
		exit(1);
	}

	outfd = open(argv[optind++], O_RDWR|O_CREAT|O_APPEND|O_TRUNC, 0777);
	if (outfd == -1) {
		(void) fprintf(stderr,
		    "zfs_threadsappend: "
		    "open(%s, O_RDWR|O_CREAT|O_APPEND|O_TRUNC, 0777)"
		    " failed\n", argv[optind]);
		perror("open");
		exit(1);
	}

	for (i = 0; i < 2; i++) {
		ret = pthread_create(&threads[i], NULL, go, (void *)&i);
		if (ret != 0) {
			(void) fprintf(stderr,
			    "zfs_threadsappend: thr_create(#%d) "
			    "failed error=%d\n", i+1, ret);
			exit(1);
		}
	}

	for (i = 0; i < 2; i++) {
		if (pthread_join(threads[i], NULL) != 0)
			break;
	}

	return (0);
}
