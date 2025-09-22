/*	$OpenBSD: dk.c,v 1.15 2015/10/01 16:08:20 krw Exp $	*/

/*
 * Copyright 1996 1995 by Open Software Foundation, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "libsa.h"

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/reboot.h>
#include <machine/pdc.h>
#include <machine/iomod.h>

#include "dev_hppa.h"

const char *
dk_disklabel(struct hppa_dev *dp, struct disklabel *label)
{
	char buf[DEV_BSIZE];
	size_t ret;

	if (iodcstrategy(dp, F_READ, LABELSECTOR, DEV_BSIZE, buf, &ret))
		if (ret != DEV_BSIZE)
			return "cannot read disklabel";

	return (getdisklabel(buf, label));
}

int
dkopen(struct open_file *f, ...)
{
	struct disklabel *lp;
	struct hppa_dev *dp = f->f_devdata;
	const char *st;

#ifdef	DEBUG
	if (debug)
		printf("dkopen(%p)\n", f);
#endif

	if (!(dp->pz_dev = pdc_findev(-1, PCL_RANDOM)))
		return ENXIO;

	lp = dp->label;
	st = NULL;

#ifdef DEBUG
	if (debug)
		printf ("disklabel\n");
#endif

	if ((st = dk_disklabel(dp, lp)) != NULL) {
#ifdef DEBUG
		if (debug)
			printf ("dkopen: %s\n", st);
#endif
		/* we do not know if it's a disk or net, but do not fail */
	} else {
		u_int i;

		i = B_PARTITION(dp->bootdev);
		if (i >= lp->d_npartitions || !lp->d_partitions[i].p_size)
			return (EPART);

		dp->fsoff = lp->d_partitions[i].p_offset;
	}

#ifdef DEBUG
	if (debug)
		printf ("dkopen() ret\n");
#endif
	return (0);
}

int
dkclose(f)
	struct open_file *f;
{
	free (f->f_devdata, sizeof(struct hppa_dev));
	f->f_devdata = NULL;
	return 0;
}
