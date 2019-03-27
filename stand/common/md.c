/*-
 * Copyright (c) 2009 Marcel Moolenaar
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

#include <stand.h>
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <machine/stdarg.h>

#include "bootstrap.h"

#define	MD_BLOCK_SIZE	512

#ifndef MD_IMAGE_SIZE
#error Must be compiled with MD_IMAGE_SIZE defined
#endif
#if (MD_IMAGE_SIZE == 0 || MD_IMAGE_SIZE % MD_BLOCK_SIZE)
#error Image size must be a multiple of 512.
#endif

/*
 * Preloaded image gets put here.
 * Applications that patch the object with the image can determine
 * the size looking at the start and end markers (strings),
 * so we want them contiguous.
 */
static struct {
	u_char start[MD_IMAGE_SIZE];
	u_char end[128];
} md_image = {
	.start = "MFS Filesystem goes here",
	.end = "MFS Filesystem had better STOP here",
};

/* devsw I/F */
static int md_init(void);
static int md_strategy(void *, int, daddr_t, size_t, char *, size_t *);
static int md_open(struct open_file *, ...);
static int md_close(struct open_file *);
static int md_print(int);

struct devsw md_dev = {
	"md",
	DEVT_DISK,
	md_init,
	md_strategy,
	md_open,
	md_close,
	noioctl,
	md_print,
	NULL
};

static int
md_init(void)
{

	return (0);
}

static int
md_strategy(void *devdata, int rw, daddr_t blk, size_t size,
    char *buf, size_t *rsize)
{
	struct devdesc *dev = (struct devdesc *)devdata;
	size_t ofs;

	if (dev->d_unit != 0)
		return (ENXIO);

	if (blk < 0 || blk >= (MD_IMAGE_SIZE / MD_BLOCK_SIZE))
		return (EIO);

	if (size % MD_BLOCK_SIZE)
		return (EIO);

	ofs = blk * MD_BLOCK_SIZE;
	if ((ofs + size) > MD_IMAGE_SIZE)
		size = MD_IMAGE_SIZE - ofs;

	if (rsize != NULL)
		*rsize = size;

	switch (rw & F_MASK) {
	case F_READ:
		bcopy(md_image.start + ofs, buf, size);
		return (0);
	case F_WRITE:
		bcopy(buf, md_image.start + ofs, size);
		return (0);
	}

	return (ENODEV);
}

static int
md_open(struct open_file *f, ...)
{
	va_list ap;
	struct devdesc *dev;

	va_start(ap, f);
	dev = va_arg(ap, struct devdesc *);
	va_end(ap);

	if (dev->d_unit != 0)
		return (ENXIO);

	return (0);
}

static int
md_close(struct open_file *f)
{
	struct devdesc *dev;

	dev = (struct devdesc *)(f->f_devdata);
	return ((dev->d_unit != 0) ? ENXIO : 0);
}

static int
md_print(int verbose)
{

	printf("%s devices:", md_dev.dv_name);
	if (pager_output("\n") != 0)
		return (1);

	printf("MD (%u bytes)", MD_IMAGE_SIZE);
	return (pager_output("\n"));
}
