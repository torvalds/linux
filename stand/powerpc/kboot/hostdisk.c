/*-
 * Copyright (C) 2014 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <stdarg.h>
#include "bootstrap.h"
#include "host_syscall.h"

static int hostdisk_init(void);
static int hostdisk_strategy(void *devdata, int flag, daddr_t dblk,
    size_t size, char *buf, size_t *rsize);
static int hostdisk_open(struct open_file *f, ...);
static int hostdisk_close(struct open_file *f);
static int hostdisk_ioctl(struct open_file *f, u_long cmd, void *data);
static int hostdisk_print(int verbose);

struct devsw hostdisk = {
	"/dev",
	DEVT_DISK,
	hostdisk_init,
	hostdisk_strategy,
	hostdisk_open,
	hostdisk_close,
	hostdisk_ioctl,
	hostdisk_print,
};

static int
hostdisk_init(void)
{

	return (0);
}

static int
hostdisk_strategy(void *devdata, int flag, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{
	struct devdesc *desc = devdata;
	daddr_t pos;
	int n;
	uint64_t res;
	uint32_t posl, posh;

	pos = dblk * 512;

	posl = pos & 0xffffffff;
	posh = (pos >> 32) & 0xffffffff;
	if (host_llseek(desc->d_unit, posh, posl, &res, 0) < 0) {
		printf("Seek error\n");
		return (EIO);
	}
	n = host_read(desc->d_unit, buf, size);

	if (n < 0)
		return (EIO);

	*rsize = n;
	return (0);
}

static int
hostdisk_open(struct open_file *f, ...)
{
	struct devdesc *desc;
	va_list vl;

	va_start(vl, f);
	desc = va_arg(vl, struct devdesc *);
	va_end(vl);

	desc->d_unit = host_open(desc->d_opendata, O_RDONLY, 0);

	if (desc->d_unit <= 0) {
		printf("hostdisk_open: couldn't open %s: %d\n",
		    (char *)desc->d_opendata, desc->d_unit);
		return (ENOENT);
	}

	return (0);
}

static int
hostdisk_close(struct open_file *f)
{
	struct devdesc *desc = f->f_devdata;

	host_close(desc->d_unit);
	return (0);
}

static int
hostdisk_ioctl(struct open_file *f, u_long cmd, void *data)
{

	return (EINVAL);
}

static int
hostdisk_print(int verbose)
{
	return (0);
}

