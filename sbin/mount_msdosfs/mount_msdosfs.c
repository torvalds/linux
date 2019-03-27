/*	$NetBSD: mount_msdos.c,v 1.18 1997/09/16 12:24:18 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/iconv.h>
#include <sys/linker.h>
#include <sys/module.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
/* must be after stdio to declare fparseln */
#include <libutil.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "mntopts.h"

static gid_t	a_gid(char *);
static uid_t	a_uid(char *);
static mode_t	a_mask(char *);
static void	usage(void) __dead2;
static int	set_charset(struct iovec **iov, int *iovlen, const char *, const char *);

int
main(int argc, char **argv)
{
	struct iovec *iov = NULL;
	int iovlen = 0;
	struct stat sb;
	int c, set_gid, set_uid, set_mask, set_dirmask;
	char *dev, *dir, mntpath[MAXPATHLEN], *csp;
	char fstype[] = "msdosfs";
	char errmsg[255] = {0};
	char *cs_dos = NULL;
	char *cs_local = NULL;
	mode_t mask = 0, dirmask = 0;
	uid_t uid = 0;
	gid_t gid = 0;

	set_gid = set_uid = set_mask = set_dirmask = 0;

	while ((c = getopt(argc, argv, "sl9u:g:m:M:o:L:D:W:")) != -1) {
		switch (c) {
		case 's':
			build_iovec(&iov, &iovlen, "shortnames", NULL, (size_t)-1);
			break;
		case 'l':
			build_iovec(&iov, &iovlen, "longnames", NULL, (size_t)-1);
			break;
		case '9':
			build_iovec_argf(&iov, &iovlen, "nowin95", "", (size_t)-1);
			break;
		case 'u':
			uid = a_uid(optarg);
			set_uid = 1;
			break;
		case 'g':
			gid = a_gid(optarg);
			set_gid = 1;
			break;
		case 'm':
			mask = a_mask(optarg);
			set_mask = 1;
			break;
		case 'M':
			dirmask = a_mask(optarg);
			set_dirmask = 1;
			break;
		case 'L': {
			const char *quirk = NULL;
			if (setlocale(LC_CTYPE, optarg) == NULL)
				err(EX_CONFIG, "%s", optarg);
			csp = strchr(optarg,'.');
			if (!csp)
				err(EX_CONFIG, "%s", optarg);
			quirk = kiconv_quirkcs(csp + 1, KICONV_VENDOR_MICSFT);
			build_iovec_argf(&iov, &iovlen, "cs_local", quirk);
			cs_local = strdup(quirk);
			}
			break;
		case 'D':
			cs_dos = strdup(optarg);
			build_iovec_argf(&iov, &iovlen, "cs_dos", cs_dos, (size_t)-1);
			break;
		case 'o': {
			char *p = NULL;
			char *val = strdup("");
			p = strchr(optarg, '=');
			if (p != NULL) {
				free(val);
				*p = '\0';
				val = p + 1;
			}
			build_iovec(&iov, &iovlen, optarg, val, (size_t)-1);
			}
			break;
		case 'W':
			if (strcmp(optarg, "iso22dos") == 0) {
				cs_local = strdup("ISO8859-2");
				cs_dos = strdup("CP852");
			} else if (strcmp(optarg, "iso72dos") == 0) {
				cs_local = strdup("ISO8859-7");
				cs_dos = strdup("CP737");
			} else if (strcmp(optarg, "koi2dos") == 0) {
				cs_local = strdup("KOI8-R");
				cs_dos = strdup("CP866");
			} else if (strcmp(optarg, "koi8u2dos") == 0) {
				cs_local = strdup("KOI8-U");
				cs_dos = strdup("CP866");
			} else {
				err(EX_NOINPUT, "%s", optarg);
			}
			build_iovec(&iov, &iovlen, "cs_local", cs_local, (size_t)-1);
			build_iovec(&iov, &iovlen, "cs_dos", cs_dos, (size_t)-1);
			break;
		case '?':
		default:
			usage();
			break;
		}
	}

	if (optind + 2 != argc)
		usage();

	if (set_mask && !set_dirmask) {
		dirmask = mask;
		set_dirmask = 1;
	}
	else if (set_dirmask && !set_mask) {
		mask = dirmask;
		set_mask = 1;
	}

	dev = argv[optind];
	dir = argv[optind + 1];

	if (cs_local != NULL) {
		if (set_charset(&iov, &iovlen, cs_local, cs_dos) == -1)
			err(EX_OSERR, "msdosfs_iconv");
		build_iovec_argf(&iov, &iovlen, "kiconv", "");
	} else if (cs_dos != NULL) {
		build_iovec_argf(&iov, &iovlen, "cs_local", "ISO8859-1");
		if (set_charset(&iov, &iovlen, "ISO8859-1", cs_dos) == -1)
			err(EX_OSERR, "msdosfs_iconv");
		build_iovec_argf(&iov, &iovlen, "kiconv", "");
	}

	/*
	 * Resolve the mountpoint with realpath(3) and remove unnecessary
	 * slashes from the devicename if there are any.
	 */
	if (checkpath(dir, mntpath) != 0)
		err(EX_USAGE, "%s", mntpath);
	(void)rmslashes(dev, dev);

	if (!set_gid || !set_uid || !set_mask) {
		if (stat(mntpath, &sb) == -1)
			err(EX_OSERR, "stat %s", mntpath);

		if (!set_uid)
			uid = sb.st_uid;
		if (!set_gid)
			gid = sb.st_gid;
		if (!set_mask)
			mask = dirmask =
				sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
	}

	build_iovec(&iov, &iovlen, "fstype", fstype, (size_t)-1);
	build_iovec(&iov, &iovlen, "fspath", mntpath, (size_t)-1);
	build_iovec(&iov, &iovlen, "from", dev, (size_t)-1);
	build_iovec(&iov, &iovlen, "errmsg", errmsg, sizeof(errmsg));
	build_iovec_argf(&iov, &iovlen, "uid", "%d", uid);
	build_iovec_argf(&iov, &iovlen, "gid", "%u", gid);
	build_iovec_argf(&iov, &iovlen, "mask", "%u", mask);
	build_iovec_argf(&iov, &iovlen, "dirmask", "%u", dirmask);

	if (nmount(iov, iovlen, 0) < 0) {
		if (errmsg[0])
			err(1, "%s: %s", dev, errmsg);
		else
			err(1, "%s", dev);
	}

	exit (0);
}

gid_t
a_gid(char *s)
{
	struct group *gr;
	char *gname;
	gid_t gid;

	if ((gr = getgrnam(s)) != NULL)
		gid = gr->gr_gid;
	else {
		for (gname = s; *s && isdigit(*s); ++s);
		if (!*s)
			gid = atoi(gname);
		else
			errx(EX_NOUSER, "unknown group id: %s", gname);
	}
	return (gid);
}

uid_t
a_uid(char *s)
{
	struct passwd *pw;
	char *uname;
	uid_t uid;

	if ((pw = getpwnam(s)) != NULL)
		uid = pw->pw_uid;
	else {
		for (uname = s; *s && isdigit(*s); ++s);
		if (!*s)
			uid = atoi(uname);
		else
			errx(EX_NOUSER, "unknown user id: %s", uname);
	}
	return (uid);
}

mode_t
a_mask(char *s)
{
	int done, rv;
	char *ep;

	done = 0;
	rv = -1;
	if (*s >= '0' && *s <= '7') {
		done = 1;
		rv = strtol(optarg, &ep, 8);
	}
	if (!done || rv < 0 || *ep)
		errx(EX_USAGE, "invalid file mode: %s", s);
	return (rv);
}

void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n",
	"usage: mount_msdosfs [-9ls] [-D DOS_codepage] [-g gid] [-L locale]",
	"                     [-M mask] [-m mask] [-o options] [-u uid]",
	"		      [-W table] special node");
	exit(EX_USAGE);
}

int
set_charset(struct iovec **iov, int *iovlen, const char *cs_local, const char *cs_dos)
{
	int error;

	if (modfind("msdosfs_iconv") < 0)
		if (kldload("msdosfs_iconv") < 0 || modfind("msdosfs_iconv") < 0) {
			warnx("cannot find or load \"msdosfs_iconv\" kernel module");
			return (-1);
		}

	build_iovec_argf(iov, iovlen, "cs_win", ENCODING_UNICODE);
	error = kiconv_add_xlat16_cspairs(ENCODING_UNICODE, cs_local);
	if (error && errno != EEXIST)
		return (-1);
	if (cs_dos != NULL) {
		error = kiconv_add_xlat16_cspairs(cs_dos, cs_local);
		if (error && errno != EEXIST)
			return (-1);
	} else {
		build_iovec_argf(iov, iovlen, "cs_dos", cs_local);
		error = kiconv_add_xlat16_cspair(cs_local, cs_local,
				KICONV_FROM_UPPER | KICONV_LOWER);
		if (error && errno != EEXIST)
			return (-1);
	}

	return (0);
}
