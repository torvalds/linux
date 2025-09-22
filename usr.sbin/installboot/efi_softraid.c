/*	$OpenBSD: efi_softraid.c,v 1.4 2022/11/07 15:56:09 kn Exp $	*/
/*
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2022 Klemens Nanni <kn@openbsd.org>
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

#include <dev/biovar.h>

#include <stdio.h>
#include <unistd.h>

#include "installboot.h"

void
sr_install_bootblk(int devfd, int vol, int disk)
{
	struct bioc_disk bd;
	char *realdev;
	int diskfd;
	char part;

	diskfd = sr_open_chunk(devfd, vol, disk, &bd, &realdev, &part);
	if (diskfd == -1)
		return;

	if (verbose)
		fprintf(stderr, "%s%c: %s boot blocks on %s\n", bd.bd_vendor,
		    part, (nowrite ? "would install" : "installing"), realdev);

	/* Write boot blocks to device. */
	md_installboot(diskfd, realdev);

	close(diskfd);
}

void
sr_install_bootldr(int devfd, char *dev)
{
	/*
	 * EFI platforms have a single stage bootstrap.
	 * sr_install_bootblk() installs it on each softraid chunk.
	 * The softraid volume does not require any bootstrap code.
	 */
}
