/*	$OpenBSD: dev_i386.c,v 1.23 2019/05/10 21:20:43 mlarkin Exp $	*/

/*
 * Copyright (c) 1996-1999 Michael Shalayeff
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

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/disklabel.h>
#include <dev/cons.h>

#include "libsa.h"
#include "biosdev.h"
#include "disk.h"

#ifdef SOFTRAID
#include <dev/biovar.h>
#include <dev/softraidvar.h>
#include <lib/libsa/softraid.h>
#include "softraid_amd64.h"
#endif

extern int debug;

/* XXX use slot for 'rd' for 'hd' pseudo-device */
const char bdevs[][4] = {
	"wd", "", "fd", "", "sd", "st", "cd", "",
	"", "", "", "", "", "", "", "", "", "hd", ""
};
const int nbdevs = nitems(bdevs);

const char cdevs[][4] = {
	"cn", "", "", "", "", "", "", "",
	"com", "", "", "", "pc"
};
const int ncdevs = nitems(cdevs);

/* pass dev_t to the open routines */
int
devopen(struct open_file *f, const char *fname, char **file)
{
	struct devsw *dp = devsw;
	register int i, rc = 1;

	*file = (char *)fname;

#ifdef DEBUG
	if (debug)
		printf("devopen:");
#endif

	for (i = 0; i < ndevs && rc != 0; dp++, i++) {
#ifdef DEBUG
		if (debug)
			printf(" %s: ", dp->dv_name);
#endif
		if ((rc = (*dp->dv_open)(f, file)) == 0) {
			f->f_dev = dp;
			return 0;
		}
#ifdef DEBUG
		else if (debug)
			printf("%d", rc);
#endif

	}
#ifdef DEBUG
	if (debug)
		putchar('\n');
#endif

	if ((f->f_flags & F_NODEV) == 0)
		f->f_dev = dp;

	return rc;
}

void
devboot(dev_t bootdev, char *p)
{
#ifdef SOFTRAID
	struct sr_boot_volume *bv;
	struct sr_boot_chunk *bc;
	struct diskinfo *dip = NULL;
#endif
	int sr_boot_vol = -1;
	int part_type = FS_UNUSED;

#ifdef SOFTRAID
	/*
	 * Determine the partition type for the 'a' partition of the
	 * boot device.
	 */
	TAILQ_FOREACH(dip, &disklist, list)
		if (dip->bios_info.bios_number == bootdev &&
		    (dip->bios_info.flags & BDI_BADLABEL) == 0)
			part_type = dip->disklabel.d_partitions[0].p_fstype;

	/*
	 * See if we booted from a disk that is a member of a bootable
	 * softraid volume.
	 */
	SLIST_FOREACH(bv, &sr_volumes, sbv_link) {
		if (bv->sbv_flags & BIOC_SCBOOTABLE)
			SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link)
				if (bc->sbc_disk == bootdev)
					sr_boot_vol = bv->sbv_unit;
		if (sr_boot_vol != -1)
			break;
	}
#endif

	if (sr_boot_vol != -1 && part_type != FS_BSDFFS) {
		*p++ = 's';
		*p++ = 'r';
		*p++ = '0' + sr_boot_vol;
	} else if (bootdev & 0x100) {
		*p++ = 'c';
		*p++ = 'd';
		*p++ = '0';
	} else {
		if (bootdev & 0x80)
			*p++ = 'h';
		else
			*p++ = 'f';
		*p++ = 'd';
		*p++ = '0' + (bootdev & 0x7f);
	}
	*p++ = 'a';
	*p = '\0';
}

char ttyname_buf[8];

char *
ttyname(int fd)
{
	snprintf(ttyname_buf, sizeof ttyname_buf, "%s%d",
	    cdevs[major(cn_tab->cn_dev)], minor(cn_tab->cn_dev));

	return ttyname_buf;
}

dev_t
ttydev(char *name)
{
	int i, unit = -1;
	char *no = name + strlen(name) - 1;

	while (no >= name && *no >= '0' && *no <= '9')
		unit = (unit < 0 ? 0 : (unit * 10)) + *no-- - '0';
	if (no < name || unit < 0)
		return NODEV;
	for (i = 0; i < ncdevs; i++)
		if (strncmp(name, cdevs[i], no - name + 1) == 0)
			return makedev(i, unit);
	return NODEV;
}

int
cnspeed(dev_t dev, int sp)
{
	if (major(dev) == 8)	/* comN */
		return comspeed(dev, sp);

	/* pc0 and anything else */
	return 9600;
}
