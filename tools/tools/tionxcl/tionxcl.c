/*-
 * Copyright (c) 2008 Bruce Simpson.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>

#include <libgen.h>
#include <sysexits.h>

#define DEVPATHNAME "/dev"

int
main(int argc, char *argv[])
{
	char *progname;
	char *ttyname;
	int fd;
	int dofree;

	dofree = 0;

	progname = basename(argv[0]);
	if (argc != 2)
		errx(EX_USAGE, "usage: %s <ttyname>\n", progname);
	if (geteuid() != 0)
		errx(EX_NOPERM, "Sorry\n");

	if (argv[1][0] == '/') {
		ttyname = argv[1];
	} else {
		size_t len, maxpath, result;

		len = strlen(argv[1]) + sizeof(DEVPATHNAME) + 1;

		maxpath = pathconf(DEVPATHNAME, _PC_PATH_MAX);
		if (len > maxpath) {
			warnc(ENAMETOOLONG, ttyname);
			exit(EX_DATAERR);
		}

		ttyname = malloc(len);
		if (ttyname == NULL) {
			warnc(ENOMEM, "malloc");
			exit(EX_OSERR);
		}
		dofree = 1;

		result = snprintf(ttyname, len, "%s/%s", DEVPATHNAME, argv[1]);
		if (result >= len)
			warnc(ENOMEM, "snprintf");
	}

	fd = open(ttyname, O_RDWR);
	if (fd == -1) {
		warnc(errno, "open %s", ttyname);
		if (dofree)
			free(ttyname);
		exit(EX_OSERR);
	}

	if (0 != ioctl(fd, TIOCNXCL, 0))
		warnc(errno, "ioctl TIOCNXCL %s", ttyname);

	if (dofree)
		free(ttyname);
	exit(0);
}
