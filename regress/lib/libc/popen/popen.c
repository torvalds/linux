/*	$NetBSD: popen.c,v 1.1 1999/09/30 09:23:23 tron Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matthias Scheler.
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


#include <err.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

#define _PATH_CAT	"/bin/cat"
#define BUFSIZE		(640*1024)
			/* 640KB ought to be enough for everyone. */
#define DATAFILE	"popen.data"

int
main(int argc, char **argv)
{
	char *buffer, command[PATH_MAX];
	int index, in;
	FILE *pipe;

	if ((buffer = malloc(BUFSIZE*sizeof(char))) == NULL)
	    err(1, NULL);

	for (index=0; index<BUFSIZE; index++)
	    buffer[index]=arc4random();

	(void)snprintf(command, sizeof(command), "%s >%s",
	               _PATH_CAT, DATAFILE);
	if ((pipe = popen(command, "w")) == NULL)
	    err(1, "popen write");

	if (fwrite(buffer, sizeof(char), BUFSIZE, pipe) != BUFSIZE)
	    err(1, "write");

	if (pclose(pipe) == -1)
	    err(1, "pclose");

	(void)snprintf(command, sizeof(command), "%s %s",
	               _PATH_CAT, DATAFILE);
	if ((pipe = popen(command, "r")) == NULL)
	    err(1, "popen read");

	index = 0;
	while ((in = fgetc(pipe)) != EOF)
	    if (index == BUFSIZE) {
		errno = EFBIG;
		err(1, "read");
	    }
	    else
		if ((char)in != buffer[index++]) {
	    	    errno = EINVAL;
		    err(1, "read");
		}

	if (index < BUFSIZE) {
	    errno = EIO;
	    err(1, "read");
	}

	if (pclose(pipe) == -1)
	    err(1, "pclose");

	(void)unlink(DATAFILE);
	return 0;
}
