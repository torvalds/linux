/*	$OpenBSD: softraid.c,v 1.10 2025/09/17 16:12:10 deraadt Exp $	*/
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

#include <sys/types.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>

#include <dev/biovar.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "installboot.h"

static int sr_volume(int, char *, int *, int *);

static void
sr_prepare_chunk(int devfd, int vol, int disk)
{
	struct bioc_disk bd;
	char *realdev;
	char part;
	int diskfd;

	diskfd = sr_open_chunk(devfd, vol, disk, &bd, &realdev, &part);
	if (diskfd == -1)
		return;

	/* Prepare file system on device. */
	md_prepareboot(diskfd, realdev);

	close(diskfd);
}

void
sr_prepareboot(int devfd, char *dev)
{
	int vol = -1, ndisks = 0, disk;

	/* Use the normal process if this is not a softraid volume. */
	if (!sr_volume(devfd, dev, &vol, &ndisks)) {
		md_prepareboot(devfd, dev);
		return;
	}

	/* Prepare file system on each disk that is part of this volume. */
	for (disk = 0; disk < ndisks; disk++)
		sr_prepare_chunk(devfd, vol, disk);
}

void
sr_installboot(int devfd, char *dev)
{
	int	vol = -1, ndisks = 0, disk;

	/* Use the normal process if this is not a softraid volume. */
	if (!sr_volume(devfd, dev, &vol, &ndisks)) {
		md_installboot(devfd, dev);
		return;
	}

	/* Install boot loader in softraid volume. */
	sr_install_bootldr(devfd, dev);

	/* Install boot block on each disk that is part of this volume. */
	for (disk = 0; disk < ndisks; disk++)
		sr_install_bootblk(devfd, vol, disk);
}

int
sr_volume(int devfd, char *dev, int *vol, int *disks)
{
	struct	bioc_inq bi;
	struct	bioc_vol bv;
	int	i;

	/*
	 * Determine if the given device is a softraid volume.
	 */

	/* Get volume information. */
	memset(&bi, 0, sizeof(bi));
	if (ioctl(devfd, BIOCINQ, &bi) == -1)
		return 0;

	/* XXX - softraid volumes will always have a "softraid0" controller. */
	if (strncmp(bi.bi_dev, "softraid0", sizeof("softraid0")))
		return 0;

	/*
	 * XXX - this only works with the real disk name (e.g. sd0) - this
	 * should be extracted from the device name, or better yet, fixed in
	 * the softraid ioctl.
	 */
	/* Locate specific softraid volume. */
	for (i = 0; i < bi.bi_novol; i++) {
		memset(&bv, 0, sizeof(bv));
		bv.bv_volid = i;
		if (ioctl(devfd, BIOCVOL, &bv) == -1)
			err(1, "BIOCVOL");

		if (strncmp(dev, bv.bv_dev, sizeof(bv.bv_dev)) == 0) {
			*vol = i;
			*disks = bv.bv_nodisk;
			break;
		}
	}

	if (verbose)
		fprintf(stderr, "%s: softraid volume with %i disk(s)\n",
		    dev, *disks);

	return 1;
}

void
sr_status(struct bio_status *bs)
{
	int	i;

	for (i = 0; i < bs->bs_msg_count; i++)
		warnx("%s", bs->bs_msgs[i].bm_msg);

	if (bs->bs_status == BIO_STATUS_ERROR) {
		if (bs->bs_msg_count == 0)
			errx(1, "unknown error");
		else
			exit(1);
	}
}

int
sr_open_chunk(int devfd, int vol, int disk, struct bioc_disk *bd,
    char **realdev, char *part)
{
	int diskfd;

	/* Get device name for this disk/chunk. */
	memset(bd, 0, sizeof(*bd));
	bd->bd_volid = vol;
	bd->bd_diskid = disk;
	if (ioctl(devfd, BIOCDISK, bd) == -1)
		err(1, "BIOCDISK");

	/* Check disk status. */
	if (bd->bd_status != BIOC_SDONLINE &&
	    bd->bd_status != BIOC_SDREBUILD) {
		fprintf(stderr, "softraid chunk %u not online - skipping...\n",
		    disk);
		return -1;
	}

	/* Keydisks always have a size of zero. */
	if (bd->bd_size == 0)
		return -1;

	if (strlen(bd->bd_vendor) < 1)
		errx(1, "invalid disk name");
	*part = bd->bd_vendor[strlen(bd->bd_vendor) - 1];
	if (DL_PARTNAME2NUM(*part) == -1)
		errx(1, "invalid partition %c\n", *part);
	bd->bd_vendor[strlen(bd->bd_vendor) - 1] = '\0';

	/* Open device. */
	if ((diskfd = opendev(bd->bd_vendor, (nowrite ? O_RDONLY : O_RDWR),
	    OPENDEV_PART, realdev)) == -1)
		err(1, "open: %s", *realdev);

	return diskfd;
}
