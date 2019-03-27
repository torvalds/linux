/*-
 * Copyright (C) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/wait.h>

#include <err.h>
#include <sysexits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pw.h"

int
pw_checkfd(char *nptr)
{
	const char *errstr;
	int fd = -1;

	if (strcmp(nptr, "-") == 0)
		return '-';
	fd = strtonum(nptr, 0, INT_MAX, &errstr);
	if (errstr != NULL)
		errx(EX_USAGE, "Bad file descriptor '%s': %s",
		    nptr, errstr);
	return (fd);
}

uintmax_t
pw_checkid(char *nptr, uintmax_t maxval)
{
	const char *errstr = NULL;
	uintmax_t id;

	id = strtounum(nptr, 0, maxval, &errstr);
	if (errstr)
		errx(EX_USAGE, "Bad id '%s': %s", nptr, errstr);
	return (id);
}

struct userconf *
get_userconfig(const char *config)
{
	char defaultcfg[MAXPATHLEN];

	if (config != NULL)
		return (read_userconfig(config));
	snprintf(defaultcfg, sizeof(defaultcfg), "%s/" _PW_CONF, conf.etcpath);
	return (read_userconfig(defaultcfg));
}

int
nis_update(void) {
	pid_t pid;
	int i;

	fflush(NULL);
	if ((pid = fork()) == -1) {
		warn("fork()");
		return (1);
	}
	if (pid == 0) {
		execlp("/usr/bin/make", "make", "-C", "/var/yp/", (char*) NULL);
		_exit(1);
	}
	waitpid(pid, &i, 0);
	if ((i = WEXITSTATUS(i)) != 0)
		errx(i, "make exited with status %d", i);
	return (i);
}
