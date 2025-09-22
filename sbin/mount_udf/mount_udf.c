/*	$OpenBSD: mount_udf.c,v 1.9 2021/10/24 21:24:22 deraadt Exp $	*/

/*
 * Copyright (c) 2005 Pedro Martelletto <pedro@ambientworks.net>
 * All rights reserved.
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

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/cdio.h>

#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "mntopts.h"

const struct mntopt opts[] = { MOPT_STDOPTS, { NULL } };

u_int32_t lastblock(char *dev);
__dead void usage(void);


__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-o options] special node\n", __progname);

	exit(EXIT_FAILURE);
}

/* Find out media's last block by looking at the LBA of the lead-out track. */
u_int32_t
lastblock(char *dev)
{
	int fd, error;
	struct ioc_read_toc_entry t;
	struct cd_toc_entry te;

	fd = open(dev, O_RDONLY);
	if (fd == -1)
		err(1, "open");

	t.address_format = CD_LBA_FORMAT;
	t.starting_track = CD_TRACK_LEADOUT;
	t.data_len = sizeof(struct cd_toc_entry);
	t.data = &te;

	error = ioctl(fd, CDIOREADTOCENTRIES, &t);

	close(fd);

	return (error == -1 ? 0 : te.addr.lba);
}

int
main(int argc, char **argv)
{
	struct udf_args args;
	char node[PATH_MAX];
	int ch, flags = 0;

	while ((ch = getopt(argc, argv, "o:")) != -1)
		switch (ch) {
		case 'o':
			getmntopts(optarg, opts, &flags);
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	args.fspec = argv[0];
	args.lastblock = lastblock(argv[0]);

	if (realpath(argv[1], node) == NULL)
		err(1, "realpath %s", argv[1]);

	if (mount(MOUNT_UDF, node, flags, &args) == -1)
		err(1, "mount");

	exit(0);
}
