/*-
 * Copyright (c) 2005 Robert N. M. Watson
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

#include <sys/types.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	NOBODY	"nobody"

/*
 * Simple exercise for the mlock() system call -- confirm that mlock() and
 * munlock() return success on an anonymously mapped memory page when running
 * with privilege; confirm that they fail with EPERM when running
 * unprivileged.
 */
int
main(int argc, char *argv[])
{
	struct passwd *pwd;
	int pagesize;
	char *page;

	if (geteuid() != 0)
		errx(-1, "mlock must run as root");

	errno = 0;
	pwd = getpwnam(NOBODY);
	if (pwd == NULL && errno == 0)
		errx(-1, "getpwnam: user \"%s\" not found", NOBODY);
	if (pwd == NULL)
		errx(-1, "getpwnam: %s", strerror(errno));
	if (pwd->pw_uid == 0)
		errx(-1, "getpwnam: user \"%s\" has uid 0", NOBODY);

	pagesize = getpagesize();
	page = mmap(NULL, pagesize, PROT_READ|PROT_WRITE, MAP_ANON, -1, 0);
	if (page == MAP_FAILED)
		errx(-1, "mmap: %s", strerror(errno));

	if (mlock(page, pagesize) < 0)
		errx(-1, "mlock privileged: %s", strerror(errno));

	if (munlock(page, pagesize) < 0)
		errx(-1, "munlock privileged: %s", strerror(errno));

	if (seteuid(pwd->pw_uid) < 0)
		errx(-1, "seteuid: %s", strerror(errno));

	if (mlock(page, pagesize) == 0)
		errx(-1, "mlock unprivileged: succeeded but shouldn't have");
	if (errno != EPERM)
		errx(-1, "mlock unprivileged: %s", strerror(errno));

	if (munlock(page, pagesize) == 0)
		errx(-1, "munlock unprivileged: succeeded but shouldn't have");
	if (errno != EPERM)
		errx(-1, "munlock unprivileged: %s", strerror(errno));

	return (0);
}
