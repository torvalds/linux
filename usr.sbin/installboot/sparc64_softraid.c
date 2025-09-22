/*	$OpenBSD: sparc64_softraid.c,v 1.9 2022/11/08 12:08:53 kn Exp $	*/
/*
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
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

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <dev/biovar.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "installboot.h"
#include "sparc64_installboot.h"

void
sr_install_bootblk(int devfd, int vol, int disk)
{
	struct bioc_disk bd;
	char *realdev;
	int diskfd;
	char part;

	diskfd = sr_open_chunk(devfd, vol, disk, &bd, &realdev, &part);
	if (diskfd == -1)
		return;

	if (verbose)
		fprintf(stderr, "%s%c: %s boot blocks on %s\n", bd.bd_vendor,
		    part, (nowrite ? "would install" : "installing"), realdev);

	/* Write boot blocks to device. */
	md_installboot(diskfd, realdev);

	close(diskfd);
}

void
sr_install_bootldr(int devfd, char *dev)
{
	struct bioc_installboot bb;

	/*
	 * Install boot loader into softraid boot loader storage area.
	 */
	memset(&bb, 0, sizeof(bb));
	bb.bb_bootblk = blkstore;
	bb.bb_bootblk_size = blksize;
	bb.bb_bootldr = ldrstore;
	bb.bb_bootldr_size = ldrsize;
	strncpy(bb.bb_dev, dev, sizeof(bb.bb_dev));
	if (verbose)
		fprintf(stderr, "%s: %s boot loader on softraid volume\n", dev,
		    (nowrite ? "would install" : "installing"));
	if (!nowrite) {
		if (ioctl(devfd, BIOCINSTALLBOOT, &bb) == -1)
			errx(1, "softraid installboot failed");
		sr_status(&bb.bb_bio.bio_status);
	}
}
