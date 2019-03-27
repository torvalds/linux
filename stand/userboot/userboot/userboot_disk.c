/*-
 * Copyright (c) 2011 Google, Inc.
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

/*
 * Userboot disk image handling.
 */

#include <sys/disk.h>
#include <stand.h>
#include <stdarg.h>
#include <bootstrap.h>

#include "disk.h"
#include "libuserboot.h"

struct userdisk_info {
	uint64_t	mediasize;
	uint16_t	sectorsize;
	int		ud_open;	/* reference counter */
	void		*ud_bcache;	/* buffer cache data */
};

int userboot_disk_maxunit = 0;

static int userdisk_maxunit = 0;
static struct userdisk_info	*ud_info;

static int	userdisk_init(void);
static void	userdisk_cleanup(void);
static int	userdisk_strategy(void *devdata, int flag, daddr_t dblk,
		    size_t size, char *buf, size_t *rsize);
static int	userdisk_realstrategy(void *devdata, int flag, daddr_t dblk,
		    size_t size, char *buf, size_t *rsize);
static int	userdisk_open(struct open_file *f, ...);
static int	userdisk_close(struct open_file *f);
static int	userdisk_ioctl(struct open_file *f, u_long cmd, void *data);
static int	userdisk_print(int verbose);

struct devsw userboot_disk = {
	"disk",
	DEVT_DISK,
	userdisk_init,
	userdisk_strategy,
	userdisk_open,
	userdisk_close,
	userdisk_ioctl,
	userdisk_print,
	userdisk_cleanup
};

/*
 * Initialize userdisk_info structure for each disk.
 */
static int
userdisk_init(void)
{
	off_t mediasize;
	u_int sectorsize;
	int i;

	userdisk_maxunit = userboot_disk_maxunit;
	if (userdisk_maxunit > 0) {
		ud_info = malloc(sizeof(*ud_info) * userdisk_maxunit);
		if (ud_info == NULL)
			return (ENOMEM);
		for (i = 0; i < userdisk_maxunit; i++) {
			if (CALLBACK(diskioctl, i, DIOCGSECTORSIZE,
			    &sectorsize) != 0 || CALLBACK(diskioctl, i,
			    DIOCGMEDIASIZE, &mediasize) != 0)
				return (ENXIO);
			ud_info[i].mediasize = mediasize;
			ud_info[i].sectorsize = sectorsize;
			ud_info[i].ud_open = 0;
			ud_info[i].ud_bcache = NULL;
		}
	}
	bcache_add_dev(userdisk_maxunit);
	return(0);
}

static void
userdisk_cleanup(void)
{

	if (userdisk_maxunit > 0)
		free(ud_info);
}

/*
 * Print information about disks
 */
static int
userdisk_print(int verbose)
{
	struct disk_devdesc dev;
	char line[80];
	int i, ret = 0;

	if (userdisk_maxunit == 0)
		return (0);

	printf("%s devices:", userboot_disk.dv_name);
	if ((ret = pager_output("\n")) != 0)
		return (ret);

	for (i = 0; i < userdisk_maxunit; i++) {
		snprintf(line, sizeof(line),
		    "    disk%d:   Guest drive image\n", i);
		ret = pager_output(line);
		if (ret != 0)
			break;
		dev.dd.d_dev = &userboot_disk;
		dev.dd.d_unit = i;
		dev.d_slice = D_SLICENONE;
		dev.d_partition = D_PARTNONE;
		if (disk_open(&dev, ud_info[i].mediasize,
		    ud_info[i].sectorsize) == 0) {
			snprintf(line, sizeof(line), "    disk%d", i);
			ret = disk_print(&dev, line, verbose);
			disk_close(&dev);
			if (ret != 0)
				break;
		}
	}
	return (ret);
}

/*
 * Attempt to open the disk described by (dev) for use by (f).
 */
static int
userdisk_open(struct open_file *f, ...)
{
	va_list			ap;
	struct disk_devdesc	*dev;

	va_start(ap, f);
	dev = va_arg(ap, struct disk_devdesc *);
	va_end(ap);

	if (dev->dd.d_unit < 0 || dev->dd.d_unit >= userdisk_maxunit)
		return (EIO);
	ud_info[dev->dd.d_unit].ud_open++;
	if (ud_info[dev->dd.d_unit].ud_bcache == NULL)
		ud_info[dev->dd.d_unit].ud_bcache = bcache_allocate();
	return (disk_open(dev, ud_info[dev->dd.d_unit].mediasize,
	    ud_info[dev->dd.d_unit].sectorsize));
}

static int
userdisk_close(struct open_file *f)
{
	struct disk_devdesc *dev;

	dev = (struct disk_devdesc *)f->f_devdata;
	ud_info[dev->dd.d_unit].ud_open--;
	if (ud_info[dev->dd.d_unit].ud_open == 0) {
		bcache_free(ud_info[dev->dd.d_unit].ud_bcache);
		ud_info[dev->dd.d_unit].ud_bcache = NULL;
	}
	return (disk_close(dev));
}

static int
userdisk_strategy(void *devdata, int rw, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{
	struct bcache_devdata bcd;
	struct disk_devdesc *dev;

	dev = (struct disk_devdesc *)devdata;
	bcd.dv_strategy = userdisk_realstrategy;
	bcd.dv_devdata = devdata;
	bcd.dv_cache = ud_info[dev->dd.d_unit].ud_bcache;
	return (bcache_strategy(&bcd, rw, dblk + dev->d_offset,
	    size, buf, rsize));
}

static int
userdisk_realstrategy(void *devdata, int rw, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{
	struct disk_devdesc *dev = devdata;
	uint64_t	off;
	size_t		resid;
	int		rc;

	rw &= F_MASK;
	if (rw == F_WRITE)
		return (EROFS);
	if (rw != F_READ)
		return (EINVAL);
	if (rsize)
		*rsize = 0;
	off = dblk * ud_info[dev->dd.d_unit].sectorsize;
	rc = CALLBACK(diskread, dev->dd.d_unit, off, buf, size, &resid);
	if (rc)
		return (rc);
	if (rsize)
		*rsize = size - resid;
	return (0);
}

static int
userdisk_ioctl(struct open_file *f, u_long cmd, void *data)
{
	struct disk_devdesc *dev;
	int rc;

	dev = (struct disk_devdesc *)f->f_devdata;
	rc = disk_ioctl(dev, cmd, data);
	if (rc != ENOTTY)
		return (rc);

	return (CALLBACK(diskioctl, dev->dd.d_unit, cmd, data));
}
