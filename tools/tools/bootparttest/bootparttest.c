/*-
 * Copyright (c) 2012 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <libgeom.h>
#include <disk.h>
#include <part.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int disk_strategy(void *devdata, int rw, daddr_t blk,
    size_t size, char *buf, size_t *rsize);

/* stub struct devsw */
struct devsw {
	const char	dv_name[8];
	int		dv_type;
	void		*dv_init;
	int		(*dv_strategy)(void *devdata, int rw, daddr_t blk,
			    size_t size, char *buf, size_t *rsize);
	void		*dv_open;
	void		*dv_close;
	void		*dv_ioctl;
	void		*dv_print;
	void		*dv_cleanup;
} udisk = {
	.dv_name = "disk",
	.dv_strategy = disk_strategy
};

struct disk {
	uint64_t	mediasize;
	uint16_t	sectorsize;

	int		fd;
	int		file;
} disk;

static int
disk_strategy(void *devdata, int rw, daddr_t blk, size_t size, char *buf,
    size_t *rsize)
{
	struct disk_devdesc *dev = devdata;
	int ret;

	if (rw != 1 /* F_READ */)
		return (-1);
	if (rsize)
		*rsize = 0;
	printf("read %zu bytes from the block %lld [+%lld]\n", size,
	    (long long)blk, (long long)dev->d_offset);
	ret = pread(disk.fd, buf, size,
	    (blk + dev->d_offset) * disk.sectorsize);
	if (ret != size)
		return (-1);
	return (0);
}

int
main(int argc, char **argv)
{
	struct disk_devdesc dev;
	struct stat sb;
	const char *p;

	if (argc < 2)
		errx(1, "Usage: %s <GEOM provider name> | "
		    "<disk image file name>", argv[0]);
	memset(&disk, 0, sizeof(disk));
	memset(&dev, 0, sizeof(dev));
	dev.d_dev = &udisk;
	dev.d_slice = -1;
	dev.d_partition = -1;
	if (stat(argv[1], &sb) == 0 && S_ISREG(sb.st_mode)) {
		disk.fd = open(argv[1], O_RDONLY);
		if (disk.fd < 0)
			err(1, "open %s", argv[1]);
		disk.mediasize = sb.st_size;
		disk.sectorsize = 512;
		disk.file = 1;
	} else {
		disk.fd = g_open(argv[1], 0);
		if (disk.fd < 0)
			err(1, "g_open %s", argv[1]);
		disk.mediasize = g_mediasize(disk.fd);
		disk.sectorsize = g_sectorsize(disk.fd);
		p = strpbrk(argv[1], "0123456789");
		if (p != NULL)
			disk_parsedev(&dev, p, NULL);
	}
	printf("%s \"%s\" opened\n", disk.file ? "Disk image": "GEOM provider",
	    argv[1]);
	printf("Mediasize: %ju Bytes (%ju sectors)\nSectorsize: %u Bytes\n",
	    disk.mediasize, disk.mediasize / disk.sectorsize, disk.sectorsize);

	if (disk_open(&dev, disk.mediasize, disk.sectorsize) != 0)
		errx(1, "disk_open failed");
	printf("\tdisk0:\n");
	disk_print(&dev, "\tdisk0", 1);
	disk_close(&dev);

	if (disk.file)
		close(disk.fd);
	else
		g_close(disk.fd);
	return (0);
}
