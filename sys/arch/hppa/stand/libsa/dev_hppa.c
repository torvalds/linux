/*	$OpenBSD: dev_hppa.c,v 1.18 2024/04/14 03:26:25 jsg Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "libsa.h"
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/reboot.h>
#include <dev/cons.h>

#include <machine/iomod.h>

#include "dev_hppa.h"

extern int debug;

const char cdevs[][4] = {
	"ite", "", "", "", "", "", "", "",
	"", "", "", "", ""
};
const int ncdevs = nitems(cdevs);

const struct pdc_devs {
	char	name[3];
	int	dev_type;
} pdc_devs[] = {
	{ "dk",  0 },
	{ "ct",  1 },
	{ "lf",  2 },
	{ "",   -1 },
	{ "rd", -1 },
	{ "sw", -1 },
	{ "fl", -1 },
};

/* pass dev_t to the open routines */
int
devopen(f, fname, file)
	struct open_file *f;
	const char *fname;
	char **file;
{
	struct hppa_dev *hpd;
	const struct pdc_devs *dp = pdc_devs;
	int rc = 1;

	if (!(*file = strchr(fname, ':')))
		return ENODEV;
	else
		(*file)++;

#ifdef DEBUG
	if (debug)
		printf("devopen: ");
#endif

	for (dp = pdc_devs; dp < &pdc_devs[nitems(pdc_devs)]; dp++)
		if (!strncmp(fname, dp->name, sizeof(dp->name)-1))
			break;

	if (dp >= &pdc_devs[nitems(pdc_devs)] || dp->dev_type < 0)
		return ENODEV;
#ifdef DEBUG
	if (debug)
		printf("%s\n", dp->name);
#endif

	if (!(hpd = alloc(sizeof *hpd))) {
#ifdef DEBUG
		printf ("devopen: no mem\n");
#endif
	} else {
		bzero(hpd, sizeof *hpd);
		hpd->bootdev = bootdev;
		hpd->buf = (char *)(((u_int)hpd->ua_buf + IODC_MINIOSIZ-1) &
			~(IODC_MINIOSIZ-1));
		f->f_devdata = hpd;
		if ((rc = (*devsw[dp->dev_type].dv_open)(f, file)) == 0) {
			f->f_dev = &devsw[dp->dev_type];
			return 0;
		}
		free (hpd, 0);
		f->f_devdata = NULL;
	}

	if (!(f->f_flags & F_NODEV))
		f->f_dev = &devsw[dp->dev_type];

	if (!f->f_devdata)
		*file = NULL;

	return rc;
}

void
devboot(dev, p)
	dev_t dev;
	char *p;
{
	const char *q;
	int unit;

	if (!dev) {
		int type;

		switch (PAGE0->mem_boot.pz_class) {
		case PCL_RANDOM:
			type = 0;
			unit = PAGE0->mem_boot.pz_layers[0];
			break;
		case PCL_SEQU:
			type = 1;
			unit = PAGE0->mem_boot.pz_layers[0];
			break;
		case PCL_NET_MASK|PCL_SEQU:
			type = 2;
			unit = 0;
			break;
		default:
			type = 0;
			unit = 0;
			break;
		}
		dev = bootdev = MAKEBOOTDEV(type, 0, 0, unit, B_PARTITION(dev));
	}
#ifdef _TEST
	*p++ = '/';
	*p++ = 'd';
	*p++ = 'e';
	*p++ = 'v';
	*p++ = '/';
	*p++ = 'r';
#endif
	/* quick copy device name */
	for (q = pdc_devs[B_TYPE(dev)].name; (*p++ = *q++);)
		;
	unit = B_UNIT(dev);
	if (unit >= 10) {
		p[-1] = '0' + unit / 10;
		*p++ = '0' + (unit % 10);
	} else
		p[-1] = '0' + unit;
	*p++ = 'a' + B_PARTITION(dev);
	*p = '\0';
}

char ttyname_buf[8];

char *
ttyname(fd)
	int fd;
{
	snprintf(ttyname_buf, sizeof ttyname_buf, "%s%d",
	    cdevs[major(cn_tab->cn_dev)],
	    minor(cn_tab->cn_dev));
	return (ttyname_buf);
}

dev_t
ttydev(name)
	char *name;
{
	int i, unit = -1;
	char *no = name + strlen(name) - 1;

	while (no >= name && *no >= '0' && *no <= '9')
		unit = (unit < 0 ? 0 : (unit * 10)) + *no-- - '0';
	if (no < name || unit < 0)
		return (NODEV);
	for (i = 0; i < ncdevs; i++)
		if (strncmp(name, cdevs[i], no - name + 1) == 0)
			return (makedev(i, unit));
	return (NODEV);
}
