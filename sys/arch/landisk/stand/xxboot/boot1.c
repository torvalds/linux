/*	$OpenBSD: boot1.c,v 1.12 2022/09/02 10:15:35 miod Exp $	*/
/*	$NetBSD: boot1.c,v 1.1 2006/09/01 21:26:19 uwe Exp $	*/

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David Laight.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <lib/libsa/stand.h>
#include "ufs12.h"

#include <sys/disklabel.h>

#define	XSTR(x)	#x
#define	STR(x)	XSTR(x)

static uint32_t bios_sector;

const char *boot1(uint32_t *);
void putstr(const char *str);
int blkdevstrategy(void *, int, daddr_t, size_t, void *, size_t *);
int blkdevopen(struct open_file *, ...);
int blkdevclose(struct open_file *);
int readsects(int dev, uint32_t lba, void *buf, size_t size);

extern struct disklabel ptn_disklabel;

struct fs_ops file_system[] = {
	{ ufs12_open, ufs12_close, ufs12_read, ufs12_write, ufs12_seek,
	  ufs12_stat, ufs12_readdir, ufs12_fchmod },
};
int nfsys = nitems(file_system);

struct devsw devsw[] = {
	{ "dk", blkdevstrategy, blkdevopen, blkdevclose, noioctl },
};
int     ndevs = nitems(devsw);

const char *
boot1(uint32_t *sector)
{
        struct stat sb;
	int fd = -1;

	bios_sector = *sector;

	putstr("\r\nOpenBSD/" MACHINE " Primary Bootstrap\r\n");

	do {
		/*
		 * Nothing at the start of the MBR partition, fallback on
		 * partition 'a' from the disklabel in this MBR partition.
		 */
		if (ptn_disklabel.d_magic != DISKMAGIC)
			break;
		if (ptn_disklabel.d_magic2 != DISKMAGIC)
			break;
		if (ptn_disklabel.d_partitions[0].p_fstype == FS_UNUSED)
			break;

		bios_sector = ptn_disklabel.d_partitions[0].p_offset;
		*sector = bios_sector;
		fd = open("boot", O_RDONLY);
	} while (0);

	if (fd == -1 || fstat(fd, &sb) == -1)
		return "Can't open /boot.\r\n";

#if 0
	if (sb.st_size > SECONDARY_MAX_LOAD)
		return "/boot too large.\r\n";
#endif

	if (read(fd, (void *)LOADADDRESS, sb.st_size) != sb.st_size)
		return "/boot load failed.\r\n";

	if (*(uint32_t *)(LOADADDRESS + 4) != 0x20041110)
		return "Invalid /boot file format.\r\n";

	return 0;
}

int
blkdevopen(struct open_file *f, ...)
{
	return 0;
}
int
blkdevclose(struct open_file *f)
{
	return 0;
}

int
blkdevstrategy(void *devdata, int flag, daddr_t dblk, size_t size, void *buf, size_t *rsize)
{

	if (flag != F_READ)
		return EROFS;

	if (size & (DEV_BSIZE - 1))
		return EINVAL;

	if (rsize)
		*rsize = size;

	if (size != 0 && readsects(0x40, bios_sector + dblk, buf,
				   size / DEV_BSIZE) != 0)
		return EIO;

	return 0;
}

void
twiddle(void)
{
	static int pos;

	putchar("|/-\\"[pos++ & 3]);
	putchar('\b');
}

int
devopen(struct open_file *f, const char *fname, char **file)
{
	*file = (char *)fname;
	f->f_flags |= F_NODEV;
	f->f_dev = &devsw[0];
	return (0);
}
