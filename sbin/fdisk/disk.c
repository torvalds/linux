/*	$OpenBSD: disk.c,v 1.76 2025/07/18 18:11:51 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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

#include <sys/param.h>		/* DEV_BSIZE */
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/stat.h>
#include <sys/disklabel.h>

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "part.h"
#include "disk.h"
#include "misc.h"

struct disk		disk;
struct disklabel	dl;

char		*readsectors(const uint64_t, const uint32_t);
int		 writesectors(const void *, const uint64_t, const uint32_t);

void
DISK_open(const char *name, const int oflags)
{
	struct stat		st;
	uint64_t		ns, bs, sz, spc;

	disk.dk_name = strdup(name);
	if (disk.dk_name == NULL)
		err(1, "dk_name");
	disk.dk_fd = opendev(disk.dk_name, oflags, OPENDEV_PART, NULL);
	if (disk.dk_fd == -1)
		err(1, "opendev('%s', 0x%x)", disk.dk_name, oflags);
	if (fstat(disk.dk_fd, &st) == -1)
		err(1, "fstat('%s)", disk.dk_name);
	if (!S_ISCHR(st.st_mode))
		errx(1, "%s is not a character device", disk.dk_name);
	if (ioctl(disk.dk_fd, DIOCGPDINFO, &dl) == -1)
		err(1, "DIOCGPDINFO");

	/* Set geometry to use in MBR partitions. */
	if (disk.dk_size > 0) {
		/* -l has set disk size. */
		sz = disk.dk_size;
		disk.dk_heads = 1;
		disk.dk_sectors = 64;
		disk.dk_size = DL_BLKTOSEC(&dl, sz);
		disk.dk_cylinders = disk.dk_size / disk.dk_sectors;
	} else if (disk.dk_cylinders > 0) {
		/* -c/-h/-s has set disk geometry & therefore size. */
		sz = disk.dk_cylinders * disk.dk_heads * disk.dk_sectors;
		disk.dk_size = DL_BLKTOSEC(&dl, sz);
		disk.dk_sectors = DL_BLKTOSEC(&dl, disk.dk_sectors);
	} else {
		disk.dk_cylinders = dl.d_ncylinders;
		disk.dk_heads = dl.d_ntracks;
		disk.dk_sectors = dl.d_nsectors;
		/* MBR handles only first UINT32_MAX sectors. */
		spc = (uint64_t)disk.dk_heads * disk.dk_sectors;
		sz = DL_GETDSIZE(&dl);
		if (sz > UINT32_MAX) {
			disk.dk_cylinders = UINT32_MAX / spc;
			disk.dk_size = disk.dk_cylinders * spc;
		} else
			disk.dk_size = sz;
	}

	if (disk.dk_size == 0)
		errx(1, "disk size is 0");

	if (disk.dk_bootprt.prt_ns > 0) {
		ns = disk.dk_bootprt.prt_ns + DL_BLKSPERSEC(&dl) - 1;
		bs = disk.dk_bootprt.prt_bs + DL_BLKSPERSEC(&dl) - 1;
		disk.dk_bootprt.prt_ns = DL_BLKTOSEC(&dl, ns);
		disk.dk_bootprt.prt_bs = DL_BLKTOSEC(&dl, bs);
	}
}

void
DISK_printgeometry(const char *units)
{
	const struct unit_type	*ut;
	const int		 secsize = dl.d_secsize;
	double			 size;

	size = units_size(units, disk.dk_size, &ut);
	printf("Disk: %s\tgeometry: %d/%d/%d [%.0f ", disk.dk_name,
	    disk.dk_cylinders, disk.dk_heads, disk.dk_sectors, size);
	if (ut->ut_conversion == 0 && secsize != DEV_BSIZE)
		printf("%d-byte ", secsize);
	printf("%s]\n", ut->ut_lname);
}

/*
 * The caller must free() the returned memory!
 */
char *
readsectors(const uint64_t sector, const uint32_t count)
{
	char			*secbuf;
	ssize_t			 len;
	off_t			 off, where;
	size_t			 bytes;

	where = sector * dl.d_secsize;
	bytes = count * dl.d_secsize;

	off = lseek(disk.dk_fd, where, SEEK_SET);
	if (off == -1) {
#ifdef DEBUG
		warn("lseek(%lld) for read", (int64_t)where);
#endif	/* DEBUG */
		return NULL;
	}

	secbuf = calloc(1, bytes);
	if (secbuf == NULL)
		return NULL;

	len = read(disk.dk_fd, secbuf, bytes);
	if (len == -1) {
#ifdef DEBUG
		warn("read(%zu @ %lld)", bytes, (int64_t)where);
#endif	/* DEBUG */
		free(secbuf);
		return NULL;
	}
	if (len != (ssize_t)bytes) {
#ifdef DEBUG
		warnx("short read(%zu @ %lld)", bytes, (int64_t)where);
#endif	/* DEBUG */
		free(secbuf);
		return NULL;
	}

	return secbuf;
}

int
writesectors(const void *buf, const uint64_t sector, const uint32_t count)
{
	ssize_t			len;
	off_t			off, where;
	size_t			bytes;

	where = sector * dl.d_secsize;
	bytes = count * dl.d_secsize;

	off = lseek(disk.dk_fd, where, SEEK_SET);
	if (off == -1) {
#ifdef DEBUG
		warn("lseek(%lld) for write", (int64_t)where);
#endif	/* DEBUG */
		return -1;
	}

	len = write(disk.dk_fd, buf, bytes);
	if (len == -1) {
#ifdef DEBUG
		warn("write(%zu @ %lld)", bytes, (int64_t)where);
#endif	/* DEBUG */
		return -1;
	}
	if (len != (ssize_t)bytes) {
#ifdef DEBUG
		warnx("short write(%zu @ %lld)", bytes, (int64_t)where);
#endif	/* DEBUG */
		return -1;
	}

	return 0;
}

int
DISK_readbytes(void *buf, const uint64_t sector, const size_t sz)
{
	char			*secbuf;
	uint32_t		 count;

	count = (sz + dl.d_secsize - 1) / dl.d_secsize;

	secbuf = readsectors(sector, count);
	if (secbuf == NULL)
		return -1;

	memcpy(buf, secbuf, sz);
	free(secbuf);

	return 0;
}

int
DISK_writebytes(const void *buf, const uint64_t sector, const size_t sz)
{
	char 			*secbuf;
	uint32_t		 count;
	int			 rslt;

	count = (sz + dl.d_secsize - 1) / dl.d_secsize;

	secbuf = readsectors(sector, count);
	if (secbuf == NULL)
		return -1;

	memcpy(secbuf, buf, sz);
	rslt = writesectors(secbuf, sector, count);
	free(secbuf);

	return rslt;
}
