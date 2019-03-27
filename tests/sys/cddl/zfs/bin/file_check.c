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

#pragma ident	"@(#)file_check.c	1.3	07/05/25 SMI"

#include "file_common.h"

static unsigned char bigbuffer[BIGBUFFERSIZE];

/*
 * Given a filename, check that the file consists entirely
 * of a particular pattern. If the pattern is not specified a
 * default will be used. For default values see file_common.h
 */
int
main(int argc, char **argv)
{
	int		bigfd;
	long		i, n;
	uint8_t		fillchar = DATA;
	int		bigbuffersize = BIGBUFFERSIZE;
	int64_t		read_count = 0;

	/*
	 * Validate arguments
	 */
	if (argc < 2) {
		(void) printf("Usage: %s filename [pattern]\n",
		    argv[0]);
		exit(1);
	}

	if (argv[2]) {
		fillchar = atoi(argv[2]);
	}

	/*
	 * Read the file contents and check every character
	 * against the supplied pattern. Abort if the
	 * pattern check fails.
	 */
	if ((bigfd = open(argv[1], O_RDONLY)) == -1) {
		(void) printf("open %s failed %d\n", argv[1], errno);
		exit(1);
	}

	do {
		if ((n = read(bigfd, &bigbuffer, bigbuffersize)) == -1) {
			(void) printf("read failed (%ld), %d\n", n, errno);
			exit(errno);
		}

		for (i = 0; i < n; i++) {
			if (bigbuffer[i] != fillchar) {
				(void) printf("error %s: 0x%x != 0x%x)\n",
				    argv[1], bigbuffer[i], fillchar);
				exit(1);
			}
		}

		read_count += n;
	} while (n == bigbuffersize);

	return (0);
}
