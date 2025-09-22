/*	$OpenBSD: wsconsctl.c,v 1.32 2019/06/28 13:32:46 deraadt Exp $	*/
/*	$NetBSD: wsconsctl.c,v 1.2 1998/12/29 22:40:20 hannken Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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

#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "wsconsctl.h"

extern const char *__progname;		/* from crt0.o */

extern struct field keyboard_field_tab[];
extern struct field mouse_field_tab[];
extern struct field display_field_tab[];

void	usage(void);

struct vartypesw {
	const	char *name;
	struct field *field_tab;
	void	(*init)(int,int);
	void	(*getval)(int);
	int	(*putval)(int);
	char *	(*nextdev)(int);
} typesw[] = {
	{ "keyboard", keyboard_field_tab, NULL,
	  keyboard_get_values, keyboard_put_values, keyboard_next_device },
	{ "mouse", mouse_field_tab, mouse_init,
	  mouse_get_values, mouse_put_values, mouse_next_device },
	{ "display", display_field_tab, NULL,
	  display_get_values, display_put_values, display_next_device },
	{ NULL }
};

struct vartypesw *tab_by_name(const char *, int *);

void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-an]\n"
	    "       %s [-n] [-f file] name ...\n"
	    "       %s [-n] [-f file] name=value ...\n",
	    __progname, __progname, __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, ch, error = 0, aflag = 0, do_merge, putval, devidx, devfd;
	struct vartypesw *sw = NULL;
	char *getsep = "=", *setsep = " -> ", *p;
	char *wdev = NULL;
	char *device;
	struct field *f;
	char devname[20];

	while ((ch = getopt(argc, argv, "af:nw")) != -1) {
		switch(ch) {
		case 'a':
			aflag = 1;
			break;
		case 'f':
			wdev = optarg;
			break;
		case 'n':
			getsep = setsep = NULL;
			break;
		case 'w':
			/* compat */
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0 && aflag != 0)
		errx(1, "excess arguments after -a");
	if (argc == 0)
		aflag = 1;

	if (aflag != 0) {
		for (sw = typesw; sw->name; sw++) {
			for (devidx = 0;; devidx++) {
				device = (*sw->nextdev)(devidx);
				if (!device ||
				    ((devfd = open(device, O_WRONLY)) == -1 &&
				     (devfd = open(device, O_RDONLY)) == -1)) {
					if (!device || errno != ENXIO) {
						if (device && errno != ENOENT) {
							warn("%s", device);
							error = 1;
						}
						break;
					} else
						continue;
				}

				if (devidx == 0)
					snprintf(devname, sizeof(devname),
					    "%s", sw->name);
				else
					snprintf(devname, sizeof(devname),
					    "%s%d", sw->name, devidx);

				if (sw->init != NULL)
					(*sw->init)(devfd, devidx);

				for (f = sw->field_tab; f->name; f++)
					if (!(f->flags &
					    (FLG_NOAUTO|FLG_WRONLY)))
						f->flags |= FLG_GET;
				(*sw->getval)(devfd);
				for (f = sw->field_tab; f->name; f++)
					if (f->flags & FLG_DEAD)
						continue;
					else if (f->flags & FLG_NOAUTO)
						warnx("Use explicit arg to "
						    "view %s.%s.",
						    devname, f->name);
					else if (f->flags & FLG_GET)
						pr_field(devname, f, getsep);
			}
		}
	} else if (argc > 0) {
		for (i = 0; i < argc; i++) {
			sw = tab_by_name(argv[i], &devidx);
			if (!sw)
				continue;

			if (!wdev)
				device = (*sw->nextdev)(devidx);
			else
				device = wdev;

			if (!device ||
			    ((devfd = open(device, O_WRONLY)) == -1 &&
			     (devfd = open(device, O_RDONLY)) == -1)) {
				if (!device) {
					const char *c = strchr(argv[i], '.');
					int k;
					if (!c)
						c = strchr(argv[i], '\0');
					k = c - argv[i];
					warnx("%*.*s: no such variable",
					    k, k, argv[i]);
				} else
					warn("%s", device);
				error = 1;
				continue;
			}

			if (devidx == 0)
				snprintf(devname, sizeof(devname),
				    "%s", sw->name);
			else
				snprintf(devname, sizeof(devname),
				    "%s%d", sw->name, devidx);

			if (sw->init != NULL)
				(*sw->init)(devfd, devidx);

			p = strchr(argv[i], '=');
			if (p == NULL) {
				if (!strchr(argv[i], '.')) {
					for (f = sw->field_tab; f->name; f++)
						if (!(f->flags &
						    (FLG_NOAUTO|FLG_WRONLY)))
							f->flags |= FLG_GET;
					(*sw->getval)(devfd);
					for (f = sw->field_tab; f->name; f++)
						if (f->flags & FLG_DEAD)
							continue;
						else if (f->flags & FLG_NOAUTO)
							warnx("Use explicit "
							    "arg to view "
							    "%s.%s.",
							    devname, f->name);
						else if (f->flags & FLG_GET)
							pr_field(devname, f,
							    getsep);
					continue;
				}

				f = field_by_name(sw->field_tab, argv[i]);
				if (f->flags & FLG_DEAD)
					continue;
				if ((f->flags & FLG_WRONLY)) {
					warnx("%s: write only", argv[i]);
					continue;
				}
				f->flags |= FLG_GET;
				(*sw->getval)(devfd);
				if (f->flags & FLG_DEAD)
					continue;
				pr_field(devname, f, getsep);
			} else {
				if (!strchr(argv[i], '.') ||
				    (strchr(argv[i], '.') > p)) {
					warnx("%s: illegal variable name",
					    argv[i]);
					continue;
				}
				if (p > argv[i] &&
				    (*(p - 1) == '+' || *(p - 1) == '-')) {
					do_merge = *(p - 1);
					*(p - 1) = '\0';
				} else
					do_merge = 0;
				*p++ = '\0';

				f = field_by_name(sw->field_tab, argv[i]);
				if (f->flags & FLG_DEAD)
					continue;
				if (f->flags & FLG_RDONLY) {
					warnx("%s: read only", argv[i]);
					continue;
				}
				if (do_merge || f->flags & FLG_INIT) {
					if (!(f->flags & FLG_MODIFY))
						errx(1, "%s: can only be set",
						    argv[i]);
					f->flags |= FLG_GET;
					(*sw->getval)(devfd);
					f->flags &= ~FLG_GET;
				}
				rd_field(f, p, do_merge);
				f->flags |= FLG_SET;
				putval = (*sw->putval)(devfd);
				f->flags &= ~FLG_SET;
				if (putval != 0 ||
				    f->flags & (FLG_DEAD|FLG_NOAUTO))
					continue;
				if (f->flags & FLG_WRONLY) {
					pr_field(devname, f, setsep);
				} else {
					if (!(f->flags & FLG_NORDBACK)) {
						f->flags |= FLG_GET;
						(*sw->getval)(devfd);
					}
					if (f->flags & FLG_DEAD)
						continue;
					pr_field(devname, f, setsep);
				}
			}

			close(devfd);
		}
	} else
		usage();

	exit(error);
}

struct vartypesw *
tab_by_name(const char *var, int *idx)
{
	struct vartypesw *sw;
	const char *p = strchr(var, '.');
	char *c;
	int i;

	for (sw = typesw; sw->name; sw++)
		if (!strncmp(sw->name, var, strlen(sw->name)))
			break;

	if (!p)
		p = strchr(var, '\0');

	if (!sw->name) {
		i = p - var;
		warnx("%*.*s: no such variable", i, i, var);
		return (NULL);
	}

	if ((p - var) > strlen(sw->name)) {
		c = (char *)var;
		c = c + strlen(sw->name);
		i = 0;
		while (c < p) {
			if (*c >= '0' && *c <= '9')
				i = i * 10 + *c - '0';
			else
				i = -1;
			c++;
		}
		if (i < 0 || i > 32) {
			i = p - var;
			warnx("%*.*s: no such variable", i, i, var);
			return (NULL);
		}
	} else
		i = 0;

	*idx = i;

	return (sw);
}
