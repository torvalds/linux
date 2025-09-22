/*	$OpenBSD: devs.c,v 1.13 2023/02/23 19:48:22 miod Exp $	*/

/*
 * Copyright (c) 2006 Michael Shalayeff
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
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <libsa.h>
#include <lib/libsa/loadfile.h>

int sector;

void
machdep(void)
{
	tick_init();
	cninit();
}

int
devopen(struct open_file *f, const char *fname, char **file)
{  
	if (fname[0] != 'c' || fname[1] != 'f' || fname[2] != ':')
		return EINVAL;

	*file = (char *)fname + 3;
	f->f_flags |= F_NODEV;
	f->f_dev = &devsw[0];
	return (0);
}

void
devboot(dev_t bootdev, char *p)
{
	sector = bootdev;	/* passed from pbr */
	p[0] = 'c';
	p[1] = 'f';
	p[2] = '\0';
}

void
run_loadfile(uint64_t *marks, int howto)
{
	u_long entry;

	entry = marks[MARK_ENTRY];
	cache_flush();
	cache_disable();

	(*(void (*)(int,int,int))entry)(howto, marks[MARK_END], 0);
}

int
blkdevopen(struct open_file *f, ...)
{
	return 0;
}

int
blkdevstrategy(void *v, int flag, daddr_t dblk, size_t size, void *buf, size_t *rsize)
{

	if (flag != F_READ)
		return EROFS;

	if (size & (DEV_BSIZE - 1))
		return EINVAL;

	if (rsize)
		*rsize = size;

	if (size != 0 && readsects(0x40, sector + dblk, buf,
	    size / DEV_BSIZE) != 0)
		return EIO;

	return 0;
}

int
blkdevclose(struct open_file *f)
{
	return 0;
}
