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
#include <dev/pci/ydsvar.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <stdio.h>
#include "yds_hwmcode.h"

#define FILENAME "yds"

void
hswapn(u_int32_t *p, int wcount)
{
	for (; wcount; wcount -=4) {
		*p = htonl(*p);
		p++;
	}
}

int
main(int argc, char *argv[])
{
	struct	yds_firmware yfproto, *yf;
	int len, fd;
	ssize_t rlen;

	len = sizeof(*yf) - sizeof(yfproto.data) +
	    sizeof(yds_dsp_mcode) + sizeof(yds_ds1_ctrl_mcode) +
	    sizeof(yds_ds1e_ctrl_mcode);

	yf = (struct yds_firmware *)malloc(len);
	bzero(yf, len);

	yf->dsplen = htonl(sizeof(yds_dsp_mcode));
	yf->ds1len = htonl(sizeof(yds_ds1_ctrl_mcode));
	yf->ds1elen = htonl(sizeof(yds_ds1e_ctrl_mcode));

	bcopy(yds_dsp_mcode, &yf->data[0], sizeof(yds_dsp_mcode));
	hswapn((u_int32_t *)&yf->data[0], sizeof(yds_dsp_mcode));

	bcopy(yds_ds1_ctrl_mcode, &yf->data[0] + sizeof(yds_dsp_mcode),
	    sizeof(yds_ds1_ctrl_mcode));
	hswapn((u_int32_t *)&yf->data[sizeof(yds_dsp_mcode)],
	    sizeof(yds_ds1_ctrl_mcode));

	bcopy(yds_ds1e_ctrl_mcode,
	    &yf->data[0] + sizeof(yds_dsp_mcode) + sizeof(yds_ds1_ctrl_mcode),
	    sizeof(yds_ds1e_ctrl_mcode));
	hswapn((u_int32_t *)&yf->data[sizeof(yds_dsp_mcode) +
	    sizeof(yds_ds1_ctrl_mcode)],
	    sizeof(yds_ds1e_ctrl_mcode));

	printf("creating %s length %d [%zu+%zu+%zu]\n",
	    FILENAME, len, sizeof(yds_dsp_mcode),
	    sizeof(yds_ds1_ctrl_mcode), sizeof(yds_ds1e_ctrl_mcode));
	fd = open(FILENAME, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, FILENAME);

	rlen = write(fd, yf, len);
	if (rlen == -1)
		err(1, "%s", FILENAME);
	if (rlen != len)
		errx(1, "%s: short write", FILENAME);
	free(yf);
	close(fd);
	return 0;
}
