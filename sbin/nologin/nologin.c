/*	$OpenBSD: nologin.c,v 1.10 2023/03/08 04:43:06 guenther Exp $	*/

/*
 * Copyright (c) 1997, Jason Downs.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Distinctly different from _PATH_NOLOGIN. */
#define _PATH_NOLOGIN_TXT	"/etc/nologin.txt"

#define DEFAULT_MESG	"This account is currently not available.\n"

int
main(int argc, char *argv[])
{
	int nfd;
	ssize_t nrd;
	char nbuf[BUFSIZ];

	if (unveil(_PATH_NOLOGIN_TXT, "r") == -1)
		err(1, "unveil %s", _PATH_NOLOGIN_TXT);
	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");

	nfd = open(_PATH_NOLOGIN_TXT, O_RDONLY);
	if (nfd == -1) {
		write(STDOUT_FILENO, DEFAULT_MESG, strlen(DEFAULT_MESG));
		exit (1);
	}

	while ((nrd = read(nfd, nbuf, sizeof(nbuf))) != -1 && nrd != 0)
		write(STDOUT_FILENO, nbuf, nrd);
	close (nfd);

	exit (1);
}
