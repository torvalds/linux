/*	$OpenBSD: devopen.c,v 1.11 2014/07/13 09:26:08 jasper Exp $	*/

/*
 * Copyright (c) 2004 Tom Cosgrove
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

#include "libsa.h"
#include "biosdev.h"
#include <sys/param.h>
#include <dev/cons.h>

extern int debug;

extern char *fs_name[];
extern int nfsname;
extern struct devsw netsw[];

extern char *bootmac;		/* Gets passed to kernel for network boot */

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
	char *p;
	char *stripdev;
	int i, l;
	int rc = 1;

	*file = (char *)fname;

#ifdef DEBUG
	if (debug)
		printf("devopen(%s):", fname);
#endif

	/* Make sure we have a prefix, e.g. hd0a: or tftp:. */
	for (p = (char *)fname; *p != ':' && *p != '\0'; ) p++;
	if (*p != ':')
		return 1;
	stripdev = p + 1;

	l = p - fname;			/* Length of device prefix. */
	for (i = 0; i < nfsname; i++) {
		if ((fs_name[i] != NULL) &&
		    (strncmp(fname, fs_name[i], l) == 0)) {

			/* Force oopen() etc to use this filesystem. */
			f->f_ops = &file_system[i];
			f->f_dev = dp = &netsw[0];

			rc = (*dp->dv_open)(f, NULL);
			if (rc == 0)
				*file = stripdev;
			else
				f->f_dev = NULL;
#ifdef DEBUG
			if (debug)
				putchar('\n');
#endif
			return rc;
		}
	}

	/*
	 * Assume that any network filesystems would be caught by the
	 * code above, so that the next phase of devopen() is only for
	 * local devices.
	 *
	 * Clear bootmac, to signal that we loaded this file from a
	 * non-network device.
	 */
	bootmac = NULL;

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
	*p++ = 't';
	*p++ = 'f';
	*p++ = 't';
	*p++ = 'p';
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
			return (makedev(i, unit));
	return NODEV;
}

int
cnspeed(dev_t dev, int sp)
{
	if (major(dev) == 8)	/* comN */
		return (comspeed(dev, sp));

	/* pc0 and anything else */
	return 9600;
}
