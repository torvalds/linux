/*	$OpenBSD: build.c,v 1.8 2017/08/28 05:46:44 otto Exp $	*/

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
#include <dev/usb/if_kuevar.h>
#include <fcntl.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "kue_fw.h"

#define FILENAME "kue"

int
main(int argc, char *argv[])
{
	struct	kue_firmware kfproto, *kf;
	int len, fd;
	ssize_t rlen;

	len = sizeof(*kf) - sizeof(kfproto.data) +
	    sizeof(kue_code_seg) + sizeof(kue_fix_seg) +
	    sizeof(kue_trig_seg);

	kf = (struct kue_firmware *)malloc(len);
	bzero(kf, len);

	kf->codeseglen = htonl(sizeof(kue_code_seg));
	kf->fixseglen = htonl(sizeof(kue_fix_seg));
	kf->trigseglen = htonl(sizeof(kue_trig_seg));

	bcopy(kue_code_seg, &kf->data[0],
	    sizeof(kue_code_seg));
	bcopy(kue_fix_seg, &kf->data[0] + sizeof(kue_code_seg),
	    sizeof(kue_fix_seg));
	bcopy(kue_trig_seg,
	    &kf->data[0] + sizeof(kue_code_seg) + sizeof(kue_fix_seg),
	    sizeof(kue_trig_seg));

	printf("creating %s length %d [%zu+%zu+%zu]\n",
	    FILENAME, len, sizeof(kue_code_seg), sizeof(kue_fix_seg),
	    sizeof(kue_trig_seg));
	fd = open(FILENAME, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, FILENAME);

	rlen = write(fd, kf, len);
	if (rlen == -1)
		err(1, "%s", FILENAME);
	if (rlen != len)
		errx(1, "%s: short write", FILENAME);
	free(kf);
	close(fd);
	return 0;
}
