/*	$OpenBSD: installboot.c,v 1.17 2025/02/19 21:30:46 kettenis Exp $	*/

/*
 * Copyright (c) 2012, 2013 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "installboot.h"

int	config;
int	nowrite;
int	prepare;
int	stages;
int	verbose;

char	*root;
char	*stage1;
char	*stage2;

static __dead void
usage(void)
{
	fprintf(stderr, "usage:\t%1$s [-cnv] [-r root] disk [stage1%2$s]\n"
	    "\t%1$s [-nv] -p disk\n",
	    getprogname(), (stages >= 2) ? " [stage2]" : "");

	exit(1);
}

int
main(int argc, char **argv)
{
	char *dev, *realdev;
	int devfd, opt;

	md_init();

	while ((opt = getopt(argc, argv, "cnpr:v")) != -1) {
		switch (opt) {
		case 'c':
			config = 1;
			break;
		case 'n':
			nowrite = 1;
			break;
		case 'p':
			prepare = 1;
			break;
		case 'r':
			root = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1 || argc > stages + 1)
		usage();
	if (prepare && (root != NULL || argc > 1))
		usage();

	dev = argv[0];
	if (argc > 1)
		stage1 = argv[1];
	if (argc > 2)
		stage2 = argv[2];

	if ((devfd = opendev(dev, (nowrite ? O_RDONLY : O_RDWR), OPENDEV_PART,
	    &realdev)) == -1)
		err(1, "open: %s", realdev);

	if (prepare) {
#if SOFTRAID
		sr_prepareboot(devfd, dev);
#else
		md_prepareboot(devfd, realdev);
#endif
		return 0;
	}

	/* Prefix stages with root, unless they were user supplied. */
	if (root == NULL)
		root = "/";
	if (verbose)
		fprintf(stderr, "Using %s as root\n", root);
	if (argc <= 1 && stage1 != NULL) {
		stage1 = fileprefix(root, stage1);
		if (stage1 == NULL)
			exit(1);
	}
	if (argc <= 2 && stage2 != NULL) {
		stage2 = fileprefix(root, stage2);
		if (stage2 == NULL)
			exit(1);
	}

        if (verbose) {
		fprintf(stderr, "%s bootstrap on %s\n",
		    (nowrite ? "would install" : "installing"), realdev);
		if (stage1 || stage2) {
			if (stage1)
				fprintf(stderr, "using first-stage %s", stage1);
			if (stage2)
				fprintf(stderr, ", second-stage %s", stage2);
			fprintf(stderr, "\n");
		}
	}

	md_loadboot();

#ifdef SOFTRAID
	sr_installboot(devfd, dev);
#else
	md_installboot(devfd, realdev);
#endif

	return 0;
}
