/*	$OpenBSD: dev.c,v 1.11 2020/12/09 18:10:19 krw Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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
/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Manuel Bouyer.
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
#include "libsa.h"
#include <sys/disklabel.h>
#include <machine/cpu.h>
#include <machine/pmon.h>

/*
 * PMON I/O
 */

char	pmon_bootdev[1 + 256];

struct pmon_iodata {
	int			fd;
	struct disklabel	label;
	off_t			partoff;
	off_t			curpos;
};

int	pmon_getdisklabel(struct pmon_iodata *pi);

int
pmon_iostrategy(void *f, int rw, daddr_t dblk, size_t size, void *buf,
    size_t *rsize)
{
	struct pmon_iodata *pi = (struct pmon_iodata *)f;
	off_t offs, pos;
	int rc;

	if (rsize != NULL)
		*rsize = 0;
	if (size == 0)
		return 0;

	if (rw != F_READ)
		return EOPNOTSUPP;

	offs = ((daddr_t)dblk + pi->partoff) * DEV_BSIZE;
	if (offs != pi->curpos) {
		pos = pmon_lseek(pi->fd, offs, 0 /* SEEK_SET */);
		if (pos != offs)
			return EINVAL;
	}

	/* note this expects size to fit in 32 bits */
	rc = pmon_read(pi->fd, buf, size);
	if (rc >= 0) {
		pi->curpos += rc;
		if (rsize != NULL)
			*rsize = rc;
	}

	if (rc != size)
		return EIO;
	return 0;
}

int
pmon_ioopen(struct open_file *f, ...)
{
	static const u_char zero[8] = { 0 };
	struct pmon_iodata *pi;
	int rc;
	va_list ap;
	uint unit, part;

	pi = alloc(sizeof *pi);
	if (pi == NULL)
		return ENOMEM;
	bzero(pi, sizeof *pi);
	f->f_devdata = pi;

	va_start(ap, f);
	unit = va_arg(ap, uint);
	part = va_arg(ap, uint);
	va_end(ap);

	/*
	 * Open the raw device through PMON.
	 */

	snprintf(pmon_bootdev, sizeof pmon_bootdev, "/dev/disk/%s%d",
	    f->f_dev->dv_name, unit);
	rc = pmon_open(pmon_bootdev, 0 /* O_RDONLY */);
	if (rc < 0)
		return ENXIO;

	pi->fd = rc;

	/*
	 * Read disklabel.
	 */

	if (pmon_getdisklabel(pi) != 0) {
		pmon_ioclose(f);
		return ENXIO;
	}

	if (part >= pi->label.d_npartitions) {
		pmon_ioclose(f);
		return EPART;
	}

	if (memcmp(pi->label.d_uid, zero, sizeof(pi->label.d_uid)) != 0) {
		const u_char *duid = pi->label.d_uid;

		snprintf(pmon_bootdev, sizeof(pmon_bootdev),
		    "bootduid=%02x%02x%02x%02x%02x%02x%02x%02x",
		    duid[0], duid[1], duid[2], duid[3],
		    duid[4], duid[5], duid[6], duid[7]);
	}

	pi->partoff = DL_GETPOFFSET(&pi->label.d_partitions[part]);
	pi->curpos = 0;

	return 0;
}

int
pmon_ioclose(struct open_file *f)
{
	struct pmon_iodata *pi;
	int rc;

	if (f->f_devdata != NULL) {
		pi = (struct pmon_iodata *)f->f_devdata;
		rc = pmon_close(pi->fd);
		free(pi, sizeof *pi);
		f->f_devdata = NULL;
	} else
		rc = 0;

	return rc;
}

/*
 * Read disk label from the device.
 */
int
pmon_getdisklabel(struct pmon_iodata *pi)
{
	char *msg;
	int sector;
	size_t rsize;
	struct disklabel *lp = &pi->label;
	char buf[DEV_BSIZE];

	bzero(lp, sizeof *lp);

	/*
	 * Find OpenBSD Partition in DOS partition table.
	 */
	sector = 0;
	if (pmon_iostrategy(pi, F_READ, DOSBBSECTOR, DEV_BSIZE, buf, &rsize))
		return ENXIO;

	if (*(u_int16_t *)&buf[DOSMBR_SIGNATURE_OFF] == DOSMBR_SIGNATURE) {
		int i;
		struct dos_partition *dp = (struct dos_partition *)buf;

		/*
		 * Lookup OpenBSD slice. If there is none, go ahead
		 * and try to read the disklabel off sector #0.
		 */
		memcpy(dp, &buf[DOSPARTOFF], NDOSPART * sizeof(*dp));
		for (i = 0; i < NDOSPART; i++) {
			if (dp[i].dp_typ == DOSPTYP_OPENBSD) {
				sector = letoh32(dp[i].dp_start);
				break;
			}
		}
	}

	if (pmon_iostrategy(pi, F_READ, sector + DOS_LABELSECTOR, DEV_BSIZE,
				buf, &rsize))
		return ENXIO;

	if ((msg = getdisklabel(buf + LABELOFFSET, lp))) {
		printf("getdisklabel: %s\n", msg);
		return ENXIO;
	}

	return 0;
}
