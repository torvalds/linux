/*	$OpenBSD: ttyflags.c,v 1.17 2022/12/04 23:50:47 cheloha Exp $	*/
/*	$NetBSD: ttyflags.c,v 1.8 1996/04/09 05:20:30 cgd Exp $	*/

/*
 * Copyright (c) 1996 Theo de Raadt
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttyent.h>
#include <unistd.h>

int all(int);
int ttys(char **, int);
int ttyflags(struct ttyent *, int);
void usage(void);

int nflag, vflag;

/*
 * Ttyflags sets the device-specific tty flags, based on the contents
 * of /etc/ttys.  It can either set all of the ttys' flags, or set
 * the flags of the ttys specified on the command line.
 */
int
main(int argc, char *argv[])
{
	int aflag, ch, rval, pflag = 0;

	aflag = nflag = vflag = 0;
	while ((ch = getopt(argc, argv, "panv")) != -1)
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'n':		/* undocumented */
			nflag = 1;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (aflag && argc != 0)
		usage();

	if (setttyent() == 0)
		err(1, "setttyent");

	if (aflag)
		rval = all(pflag);
	else
		rval = ttys(argv, pflag);

	if (endttyent() == 0)
		warn("endttyent");

	exit(rval);
}

/*
 * Change all /etc/ttys entries' flags.
 */
int
all(int print)
{
	struct ttyent *tep;
	int rval;

	rval = 0;
	for (tep = getttyent(); tep != NULL; tep = getttyent()) {
		/* pseudo-tty ignore TIOCSFLAGS, so don't bother */
		if (tep->ty_type == NULL ||
		    strcmp(tep->ty_type, "network") == 0)
			continue;
		if (ttyflags(tep, print))
			rval = 1;
	}
	return (rval);
}

/*
 * Change the specified ttys' flags.
 */
int
ttys(char **ttylist, int print)
{
	struct ttyent *tep;
	int rval;

	rval = 0;
	for (; *ttylist != NULL; ttylist++) {
		tep = getttynam(*ttylist);
		if (tep == NULL) {
			warnx("couldn't find an entry in %s for \"%s\"",
			    _PATH_TTYS, *ttylist);
			rval = 1;
			continue;
		}

		if (ttyflags(tep, print))
			rval = 1;
	}
	return (rval);
}


/*
 * Actually do the work; find out what the new flags value should be,
 * open the device, and change the flags.
 */
int
ttyflags(struct ttyent *tep, int print)
{
	int fd, flags = 0, rval = 0, st, sep = 0;
	char path[PATH_MAX];
	char strflags[256];

	st = tep->ty_status;
	strflags[0] = '\0';

	/* Find the full device path name. */
	(void)snprintf(path, sizeof path, "%s%s", _PATH_DEV, tep->ty_name);

	if (print == 0) {
		/* Convert ttyent.h flags into ioctl flags. */
		if (st & TTY_LOCAL) {
			flags |= TIOCFLAG_CLOCAL;
			(void)strlcat(strflags, "local", sizeof strflags);
			sep++;
		}
		if (st & TTY_RTSCTS) {
			flags |= TIOCFLAG_CRTSCTS;
			if (sep++)
				(void)strlcat(strflags, "|", sizeof strflags);
			(void)strlcat(strflags, "rtscts", sizeof strflags);
		}
		if (st & TTY_SOFTCAR) {
			flags |= TIOCFLAG_SOFTCAR;
			if (sep++)
				(void)strlcat(strflags, "|", sizeof strflags);
			(void)strlcat(strflags, "softcar", sizeof strflags);
		}
		if (st & TTY_MDMBUF) {
			flags |= TIOCFLAG_MDMBUF;
			if (sep++)
				(void)strlcat(strflags, "|", sizeof strflags);
			(void)strlcat(strflags, "mdmbuf", sizeof strflags);
		}
		if (vflag)
			printf("%s setting flags to: %s\n", path, strflags);
	}

	if (nflag)
		return (0);

	/* Open the device NON-BLOCKING, set the flags, and close it. */
	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) == -1) {
		if (!(errno == ENXIO ||
		      (errno == ENOENT && (st & TTY_ON) == 0)))
			rval = 1;
		if (vflag)
			warn("open %s", path);
		return (rval);
	}
	if (print == 0) {
		if (ioctl(fd, TIOCSFLAGS, &flags) == -1)
			if (errno != ENOTTY || vflag) {
				warn("TIOCSFLAGS on %s", path);
				rval = (errno != ENOTTY);
			}
	} else {
		if (ioctl(fd, TIOCGFLAGS, &flags) == -1)
			if (errno != ENOTTY || vflag) {
				warn("TIOCGFLAGS on %s", path);
				rval = (errno != ENOTTY);
			}
		if (flags & TIOCFLAG_CLOCAL) {
			(void)strlcat(strflags, "local", sizeof strflags);
			sep++;
		}
		if (flags & TIOCFLAG_CRTSCTS) {
			if (sep++)
				(void)strlcat(strflags, "|", sizeof strflags);
			(void)strlcat(strflags, "rtscts", sizeof strflags);
		}
		if (flags & TIOCFLAG_SOFTCAR) {
			if (sep++)
				(void)strlcat(strflags, "|", sizeof strflags);
			(void)strlcat(strflags, "softcar", sizeof strflags);
		}
		if (flags & TIOCFLAG_MDMBUF) {
			if (sep++)
				(void)strlcat(strflags, "|", sizeof strflags);
			(void)strlcat(strflags, "mdmbuf", sizeof strflags);
		}
		printf("%s flags are: %s\n", path, strflags);
	}
	if (close(fd) == -1) {
		warn("close %s", path);
		return (1);
	}
	return (rval);
}

/*
 * Print usage information when a bogus set of arguments is given.
 */
void
usage(void)
{
	(void)fprintf(stderr, "usage: ttyflags [-pv] [-a | tty ...]\n");
	exit(1);
}
