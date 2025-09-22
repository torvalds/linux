/*	$OpenBSD: bootstrap.c,v 1.14 2025/09/17 16:12:10 deraadt Exp $	*/

/*
 * Copyright (c) 2013 Joel Sing <jsing@openbsd.org>
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

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "installboot.h"

void
bootstrap(int devfd, char *dev, char *bootfile)
{
	struct disklabel dl;
	struct disklabel *lp;
	struct partition *pp;
	char *boot, *p, part;
	size_t bootsize;
	size_t bootsec;
	struct stat sb;
	int fd, i;

	/*
	 * Install bootstrap code onto the given disk, preserving the
	 * existing disklabel.
	 */

	/* Read disklabel from disk. */
	if (ioctl(devfd, DIOCGDINFO, &dl) == -1)
		err(1, "disklabel");
	if (dl.d_secsize == 0) {
		warnx("disklabel has sector size of 0, assuming %d", DEV_BSIZE);
		dl.d_secsize = DEV_BSIZE;
	}

	/* Read bootstrap file. */
	if (verbose)
		fprintf(stderr, "reading bootstrap from %s\n", bootfile);
	fd = open(bootfile, O_RDONLY);
	if (fd == -1)
		err(1, "open %s", bootfile);
	if (fstat(fd, &sb) == -1)
		err(1, "fstat %s", bootfile);
	bootsec = howmany((ssize_t)sb.st_size, dl.d_secsize);
	bootsize = bootsec * dl.d_secsize;
	if (verbose)
		fprintf(stderr, "bootstrap is %zu bytes "
		    "(%zu sectors @ %u bytes = %zu bytes)\n",
		    (ssize_t)sb.st_size, bootsec, dl.d_secsize, bootsize);
	boot = calloc(1, bootsize);
	if (boot == NULL)
		err(1, "calloc");
	if (read(fd, boot, bootsize) != (ssize_t)sb.st_size)
		err(1, "read");
	close(fd);

	/*
	 * Check that the bootstrap will fit - partitions must not overlap,
	 * or if they do, the partition type must be either FS_BOOT or
	 * FS_UNUSED. The 'c' partition will always overlap and is ignored.
	 */
	if (verbose)
		fprintf(stderr, "ensuring used partitions do not overlap "
		    "with bootstrap sectors 0-%zu\n", bootsec);
	for (i = 0; i < dl.d_npartitions; i++) {
		part = DL_PARTNUM2NAME(i);
		pp = &dl.d_partitions[i];
		if (i == RAW_PART)
			continue;
		if (DL_GETPSIZE(pp) == 0)
			continue;
		if ((u_int64_t)bootsec <= DL_GETPOFFSET(pp))
			continue;
		switch (pp->p_fstype) {
		case FS_BOOT:
			break;
		case FS_UNUSED:
			warnx("bootstrap overlaps with unused partition %c",
			    part);
			break;
		default:
			errx(1, "bootstrap overlaps with partition %c", part);
		}
	}

	/*
	 * Make sure the bootstrap has left space for the disklabel.
	 * N.B.: LABELSECTOR *is* a DEV_BSIZE quantity!
	 */
	lp = (struct disklabel *)(boot + (LABELSECTOR * DEV_BSIZE) +
	    LABELOFFSET);
	for (i = 0, p = (char *)lp; i < (int)sizeof(*lp); i++)
		if (p[i] != 0)
			errx(1, "bootstrap has data in disklabel area");

	/* Patch the disklabel into the bootstrap code. */
	memcpy(lp, &dl, sizeof(dl));

	/* Write the bootstrap out to the disk. */
	if (verbose)
		fprintf(stderr, "%s bootstrap to disk\n",
		    (nowrite ? "would write" : "writing"));
	if (nowrite)
		return;
	if (pwrite(devfd, boot, bootsize, 0) != (ssize_t)bootsize)
		err(1, "pwrite");
}
