/*	$OpenBSD: devopen.c,v 1.4 2023/01/10 17:10:57 miod Exp $	*/
/*	$NetBSD: devopen.c,v 1.3 2013/01/16 15:46:20 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)conf.c	8.1 (Berkeley) 6/10/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)conf.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/reboot.h>
#include <lib/libkern/libkern.h>
#include <luna88k/stand/boot/samachdep.h>
#include <machine/disklabel.h>

#define MAXDEVNAME	16

static int make_device(const char *, int *, int *, int *, char **);

int
devopen(struct open_file *f, const char *fname, char **file)
{
	int dev, unit, part;
	int error;
	struct devsw *dp;
	int i;

	if (make_device(fname, &dev, &unit, &part, file) != 0)
		return ENXIO;

#ifdef DEBUG
	printf("%s: %s(%d,%d):%s\n", __func__,
	    devsw[dev].dv_name, unit, part, *file);
#endif
	dp = &devsw[dev];
	error = (*dp->dv_open)(f, unit, part);
	if (error != 0) {
#ifdef DEBUG
		printf("%s: open %s(%d,%d):%s failed (%s)\n", __func__,
		    devsw[dev].dv_name, unit, part, *file, strerror(error));
#endif
		return error;
	}

	for (i = 0; i < nfsys_disk; i++)
		file_system[i] = file_system_disk[i];
	nfsys = nfsys_disk;

#ifdef SUPPORT_ETHERNET
	if (strcmp(dp->dv_name, "le") == 0) {
		/* XXX mixing local fs_ops on netboot could be troublesome */
		file_system[0] = file_system_nfs[0];
		nfsys = 1;
	}
#endif

	f->f_dev = dp;

	/* Save boot device information to pass to the kernel */
	bootdev = MAKEBOOTDEV(dev, 0, unit / 10, 6 - unit % 10, part);

	return 0;
}

int
make_device(const char *str, int *devp, int *unitp, int *partp, char **fname)
{
	const char *cp;
	struct devsw *dp;
	int major, unit = 0, part = 0;
	int i;
	char devname[MAXDEVNAME + 1];

	/*
	 * parse path strings
	 */
				/* find end of dev type name */
	for (cp = str, i = 0; *cp != '\0' && *cp != '(' && i < MAXDEVNAME; i++)
			devname[i] = *cp++;
	if (*cp != '(') {
		return (-1);
	}
	devname[i] = '\0';
				/* compare dev type name */
	for (dp = devsw; dp->dv_name; dp++)
		if (!strcmp(devname, dp->dv_name))
			break;
	cp++;
	if (dp->dv_name == NULL) {
		return (-1);
	}
	major = dp - devsw;
				/* get mixed controller and unit number */
	for (; *cp != ',' && *cp != ')'; cp++) {
		if (*cp == '\0')
			return -1;
		if (*cp >= '0' && *cp <= '9')
			unit = unit * 10 + *cp - '0';
	}
	if (unit < 0 || unit >= 20 || (unit % 10) > 7) {
#ifdef DEBUG
		printf("%s: invalid unit number (%d)\n", __func__, unit);
#endif
		return (-1);
	}
				/* get optional partition number */
	if (*cp == ',')
		cp++;

	for (; /* *cp != ',' && */ *cp != ')'; cp++) {
		if (*cp == '\0')
			return -1;
		if (*cp >= '0' && *cp <= '9')
			part = part * 10 + *cp - '0';
	}
	if (part < 0 || part >= MAXPARTITIONS) {
#ifdef DEBUG
		printf("%s: invalid partition number (%d)\n", __func__, part);
#endif
		return (-1);
	}
				/* check out end of dev spec */
	*devp  = major;
	*unitp = unit;
	*partp = part;
	cp++;
	if (*cp == ':')
		cp++;
	if (*cp == '\0')
		*fname = "bsd";
	else
		*fname = (char *)cp;	/* XXX */
#ifdef DEBUG
	printf("%s(%s): major = %d, unit = %d, part = %d, fname = %s\n",
	    __func__, str, major, unit, part, *fname);
#endif

	return 0;
}
