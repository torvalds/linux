/*	$OpenBSD: build.c,v 1.8 2014/07/12 19:01:50 tedu Exp $	*/

/*
 * Copyright (c) 2004 Theo de Raadt <deraadt@openbsd.org>
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
#include <dev/ic/tivar.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ti_fw.h"
#include "ti_fw2.h"

static void
output(const char *name,
    const int FwReleaseMajor, const int FwReleaseMinor,
    const int FwReleaseFix, const u_int32_t FwStartAddr,
    const u_int32_t FwTextAddr, const int FwTextLen,
    const u_int32_t FwRodataAddr, const int FwRodataLen,
    const u_int32_t FwDataAddr, const int FwDataLen,
    const u_int32_t FwSbssAddr, const int FwSbssLen,
    const u_int32_t FwBssAddr, const int FwBssLen,
    const u_int32_t *FwText, int sizetext,
    const u_int32_t *FwRodata, int sizerodata,
    const u_int32_t *FwData, int sizedata)
{
	struct	tigon_firmware tfproto, *tf;
	int len, fd, i, cnt;
	u_int32_t *b;
	ssize_t rlen;

	len = sizeof(tfproto) - sizeof(tfproto.data) + sizetext +
	    sizerodata + sizedata;
	tf = (struct tigon_firmware *)malloc(len);
	bzero(tf, len);

	tf->FwReleaseMajor = FwReleaseMajor;
	tf->FwReleaseMinor = FwReleaseMinor;
	tf->FwReleaseFix = FwReleaseFix;
	tf->FwStartAddr = FwStartAddr;

	tf->FwTextAddr = FwTextAddr;
	tf->FwTextLen = FwTextLen;

	tf->FwRodataAddr = FwRodataAddr;
	tf->FwRodataLen = FwRodataLen;

	tf->FwDataAddr = FwDataAddr;
	tf->FwDataLen = FwDataLen;

	tf->FwSbssAddr = FwSbssAddr;
	tf->FwSbssLen = FwSbssLen;

	tf->FwBssAddr = FwBssAddr;
	tf->FwBssLen = FwBssLen;

	tf->FwTextOffset = 0;
	tf->FwRodataOffset = sizetext;
	tf->FwDataOffset = sizetext + sizerodata;

	bcopy(FwText, &tf->data[tf->FwTextOffset], FwTextLen);
	bcopy(FwRodata, &tf->data[tf->FwRodataOffset], FwRodataLen);
	bcopy(FwData, &tf->data[tf->FwDataOffset], FwDataLen);

	b = (u_int32_t *)tf;
	cnt = len / sizeof(u_int32_t);
	for (i = 0; i < cnt; i++)
		 b[i] = htole32(b[i]);

	printf("creating %s length %d [%d+%d+%d] [%d+%d+%d]\n",
	    name, len, FwTextLen, FwRodataLen, FwDataLen,
	    sizetext, sizerodata, sizedata);
	fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", name);

	rlen = write(fd, tf, len);
	if (rlen == -1)
		err(1, "%s", name);
	if (rlen != len)
		errx(1, "%s: short write", name);
	free(tf);
	close(fd);
}


int
main(int argc, char *argv[])
{

	output("tigon1",
	    tigonFwReleaseMajor, tigonFwReleaseMinor,
	    tigonFwReleaseFix, tigonFwStartAddr,
	    tigonFwTextAddr, tigonFwTextLen,
	    tigonFwRodataAddr, tigonFwRodataLen,
	    tigonFwDataAddr, tigonFwDataLen,
	    tigonFwSbssAddr, tigonFwSbssLen,
	    tigonFwBssAddr, tigonFwBssLen,
	    tigonFwText, sizeof tigonFwText,
	    tigonFwRodata, sizeof tigonFwRodata,
	    tigonFwData, sizeof tigonFwData);

	output("tigon2",
	    tigon2FwReleaseMajor, tigon2FwReleaseMinor,
	    tigon2FwReleaseFix, tigon2FwStartAddr,
	    tigon2FwTextAddr, tigon2FwTextLen,
	    tigon2FwRodataAddr, tigon2FwRodataLen,
	    tigon2FwDataAddr, tigon2FwDataLen,
	    tigon2FwSbssAddr, tigon2FwSbssLen,
	    tigon2FwBssAddr, tigon2FwBssLen,
	    tigon2FwText, sizeof tigon2FwText,
	    tigon2FwRodata, sizeof tigon2FwRodata,
	    tigon2FwData, sizeof tigon2FwData);

	return 0;
}
