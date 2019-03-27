/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky <hselasky@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/param.h>

#include <bootstrap.h>
#include <stdarg.h>

#include <stand.h>
#include <disk.h>

#define	HAVE_STANDARD_DEFS

#include USB_GLOBAL_INCLUDE_FILE

#include "umass_common.h"

static int umass_disk_init(void);
static int umass_disk_open(struct open_file *,...);
static int umass_disk_close(struct open_file *);
static void umass_disk_cleanup(void);
static int umass_disk_ioctl(struct open_file *, u_long, void *);
static int umass_disk_strategy(void *, int, daddr_t, size_t, char *, size_t *);
static int umass_disk_print(int);

struct devsw umass_disk = {
	.dv_name = "umass",
	.dv_type = DEVT_DISK,
	.dv_init = umass_disk_init,
	.dv_strategy = umass_disk_strategy,
	.dv_open = umass_disk_open,
	.dv_close = umass_disk_close,
	.dv_ioctl = umass_disk_ioctl,
	.dv_print = umass_disk_print,
	.dv_cleanup = umass_disk_cleanup,
};

static int
umass_disk_init(void)
{
	uint32_t time;

	usb_init();
	usb_needs_explore_all();

	/* wait 8 seconds for a USB mass storage device to appear */
	for (time = 0; time < (8 * hz); time++) {
		usb_idle();
		delay(1000000 / hz);
		time++;
		callout_process(1);
		if (umass_uaa.device != NULL)
			return (0);
	}
	return (0);
}

static int
umass_disk_strategy(void *devdata, int flag, daddr_t dblk, size_t size,
    char *buf, size_t *rsizep)
{
	if (umass_uaa.device == NULL)
		return (ENXIO);
	if (rsizep != NULL)
		*rsizep = 0;

	flag &= F_MASK;
	if (flag == F_WRITE) {
		if (usb_msc_write_10(umass_uaa.device, 0, dblk, size >> 9, buf) != 0)
			return (EINVAL);
	} else if (flag == F_READ) {
		if (usb_msc_read_10(umass_uaa.device, 0, dblk, size >> 9, buf) != 0)
			return (EINVAL);
	} else {
		return (EROFS);
	}

	if (rsizep != NULL)
		*rsizep = size;
	return (0);
}

static int
umass_disk_open_sub(struct disk_devdesc *dev)
{
	uint32_t nblock;
	uint32_t blocksize;

	if (usb_msc_read_capacity(umass_uaa.device, 0, &nblock, &blocksize) != 0)
		return (EINVAL);

	return (disk_open(dev, ((uint64_t)nblock + 1) * (uint64_t)blocksize, blocksize));
}

static int
umass_disk_open(struct open_file *f,...)
{
	va_list ap;
	struct disk_devdesc *dev;

	va_start(ap, f);
	dev = va_arg(ap, struct disk_devdesc *);
	va_end(ap);

	if (umass_uaa.device == NULL)
		return (ENXIO);
	if (dev->d_unit != 0)
		return (EIO);
	return (umass_disk_open_sub(dev));
}

static int
umass_disk_ioctl(struct open_file *f, u_long cmd, void *buf)
{
	struct disk_devdesc *dev;
	uint32_t nblock;
	uint32_t blocksize;
	int rc;

	dev = (struct disk_devdesc *)(f->f_devdata);
	if (dev == NULL)
		return (EINVAL);

	rc = disk_ioctl(dev, cmd, buf);
	if (rc != ENOTTY)
		return (rc);

	switch (cmd) {
	case DIOCGSECTORSIZE:
	case DIOCGMEDIASIZE:
		if (usb_msc_read_capacity(umass_uaa.device, 0,
		    &nblock, &blocksize) != 0)
			return (EINVAL);

		if (cmd == DIOCGMEDIASIZE)
			*(uint64_t*)buf = nblock;
		else
			*(uint32_t*)buf = blocksize;

		return (0);
	default:
		return (ENXIO);
	}
}

static int
umass_disk_close(struct open_file *f)
{
	struct disk_devdesc *dev;

	dev = (struct disk_devdesc *)f->f_devdata;
	return (disk_close(dev));
}

static int
umass_disk_print(int verbose)
{
	struct disk_devdesc dev;

	printf("%s devices:", umass_disk.dv_name);
	if (pager_output("\n") != 0)
		return (1);

	memset(&dev, 0, sizeof(dev));

	ret = pager_output("    umass0   UMASS device\n");
	if (ret != 0)
		return (ret);
	dev.d_dev = &umass_disk;
	dev.d_unit = 0;
	dev.d_slice = D_SLICENONE;
	dev.d_partition = D_PARTNONE;

	if (umass_disk_open_sub(&dev) == 0) {
		ret = disk_print(&dev, "    umass0", verbose);
		disk_close(&dev);
	}
	return (ret);
}

static void
umass_disk_cleanup(void)
{

	usb_uninit();
}


/* USB specific functions */

extern void callout_process(int);
extern void usb_idle(void);
extern void usb_init(void);
extern void usb_uninit(void);

void
DELAY(unsigned int usdelay)
{
	delay(usdelay);
}

int
pause(const char *what, int timeout)
{
	if (timeout == 0)
		timeout = 1;

	delay((1000000 / hz) * timeout);

	return (0);
}
