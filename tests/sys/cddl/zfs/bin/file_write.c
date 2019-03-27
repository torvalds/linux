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

#pragma ident	"@(#)file_write.c	1.4	07/10/09 SMI"

#include "file_common.h"
#include <libgen.h>

static unsigned char bigbuffer[BIGBUFFERSIZE];

/*
 * Writes (or appends) a given value to a file repeatedly.
 * See header file for defaults.
 */

static void usage(void);
static char *execname;

int
main(int argc, char **argv)
{
	int		bigfd;
	int		c;
	int		oflag = 0;
	int		err = 0;
	int		k;
	long		i;
	int64_t		good_writes = 0;
	uint8_t		nxtfillchar;
	/*
	 * Default Parameters
	 */
	int		write_count = BIGFILESIZE;
	uint8_t		fillchar = DATA;
	int		block_size = BLOCKSZ;
	char		*filename = NULL;
	char		*operation = NULL;
	off_t		noffset, offset = 0;
	int		verbose = 0;
	int		rsync = 0;
	int		wsync = 0;

	execname = argv[0];

	/*
	 * Process Arguments
	 */
	while ((c = getopt(argc, argv, "b:c:d:s:f:o:vwr")) != -1) {
		switch (c) {
			case 'b':
				block_size = atoi(optarg);
				break;
			case 'c':
				write_count = atoi(optarg);
				break;
			case 'd':
				fillchar = atoi(optarg);
				break;
			case 's':
				offset = atoll(optarg);
				break;
			case 'f':
				filename = optarg;
				break;
			case 'o':
				operation = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'w':
				wsync = 1;
				break;
			case 'r':
				rsync = 1;
				break;
			case '?':
				(void) printf("unknown arg %c\n", optopt);
				usage();
				break;
		}
	}

	/*
	 * Validate Parameters
	 */
	if (!filename) {
		(void) printf("Filename not specified (-f <file>)\n");
		err++;
	}

	if (!operation) {
		(void) printf("Operation not specified (-o <operation>).\n");
		err++;
	}

	if (block_size > BIGBUFFERSIZE) {
		(void) printf("block_size is too large max==%d.\n",
		    BIGBUFFERSIZE);
		err++;
	}

	if (err) usage();

	/*
	 * Prepare the buffer and determine the requested operation
	 */
	nxtfillchar = fillchar;
	k = 0;

	for (i = 0; i < block_size; i++) {
		bigbuffer[i] = nxtfillchar;

		if (fillchar == 0) {
			if ((k % DATA_RANGE) == 0) {
				k = 0;
			}
			nxtfillchar = k++;
		}
	}

	/*
	 * using the strncmp of operation will make the operation match the
	 * first shortest match - as the operations are unique from the first
	 * character this means that we match single character operations
	 */
	if ((strncmp(operation, "create", strlen(operation) + 1)) == 0 ||
	    (strncmp(operation, "overwrite", strlen(operation) + 1)) == 0) {
		oflag = (O_RDWR|O_CREAT);
	} else if ((strncmp(operation, "append", strlen(operation) + 1)) == 0) {
		oflag = (O_RDWR|O_APPEND);
	} else {
		(void) printf("valid operations are <create|append> not '%s'\n",
		    operation);
		usage();
	}

#ifdef UNSUPPORTED
	if (rsync) {
		oflag = oflag | O_RSYNC;
	}
#endif

	if (wsync) {
		oflag = oflag | O_SYNC;
	}

	/*
	 * Given an operation (create/overwrite/append), open the file
	 * accordingly and perform a write of the appropriate type.
	 */
	if ((bigfd = open(filename, oflag, 0666)) == -1) {
		(void) printf("open %s: failed [%s]%d. Aborting!\n", filename,
		    strerror(errno), errno);
		exit(errno);
	}
	noffset = lseek(bigfd, offset, SEEK_SET);
	if (noffset != offset) {
		(void) printf("lseek %s (%lld/%lld) failed [%s]%d.Aborting!\n",
		    filename, offset, noffset, strerror(errno), errno);
		exit(errno);
	}

	if (verbose) {
		(void) printf("%s: block_size = %d, write_count = %d, "
		    "offset = %lld, data = %s%d\n", filename, block_size,
		    write_count, offset,
		    (fillchar == 0) ? "0->" : "",
		    (fillchar == 0) ? DATA_RANGE : fillchar);
	}

	for (i = 0; i < write_count; i++) {
		ssize_t n;

		if ((n = write(bigfd, &bigbuffer, block_size)) == -1) {
			(void) printf("write failed (%ld), good_writes = %lld, "
			    "error: %s[%d]\n", (long)n, good_writes,
			    strerror(errno),
			    errno);
			exit(errno);
		}
		good_writes++;
	}

	if (verbose) {
		(void) printf("Success: good_writes = %lld (%lld)\n",
		    good_writes, (good_writes * block_size));
	}

	return (0);
}

static void
usage(void)
{
	char *base = (char *)"file_write";
	char *exec = (char *)execname;

	if (exec != NULL)
		exec = strdup(exec);
	if (exec != NULL)
		base = basename(exec);

	(void) printf("Usage: %s [-v] -o {create,overwrite,append} -f file_name"
	    " [-b block_size]\n"
	    "\t[-s offset] [-c write_count] [-d data]\n"
	    "\twhere [data] equal to zero causes chars "
	    "0->%d to be repeated throughout\n", base, DATA_RANGE);

	if (exec) {
		free(exec);
	}

	exit(1);
}
