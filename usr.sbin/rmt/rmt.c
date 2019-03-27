/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)rmt.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rmt
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mtio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int	tape = -1;

static char	*record;
static int	maxrecsize = -1;

#define	SSIZE	64
static char	device[SSIZE];
static char	count[SSIZE], mode[SSIZE], pos[SSIZE], op[SSIZE];

static char	resp[BUFSIZ];

static FILE	*debug;
#define	DEBUG(f)	if (debug) fprintf(debug, f)
#define	DEBUG1(f,a)	if (debug) fprintf(debug, f, a)
#define	DEBUG2(f,a1,a2)	if (debug) fprintf(debug, f, a1, a2)

static char	*checkbuf(char *, int);
static void	 error(int);
static void	 getstring(char *);

int
main(int argc, char **argv)
{
	int rval;
	char c;
	int n, i, cc;

	argc--, argv++;
	if (argc > 0) {
		debug = fopen(*argv, "w");
		if (debug == NULL) {
			DEBUG1("rmtd: error to open %s\n", *argv);
			exit(1);
		}
		(void)setbuf(debug, (char *)0);
	}
top:
	errno = 0;
	rval = 0;
	if (read(STDIN_FILENO, &c, 1) != 1)
		exit(0);
	switch (c) {

	case 'O':
		if (tape >= 0)
			(void) close(tape);
		getstring(device);
		getstring(mode);
		DEBUG2("rmtd: O %s %s\n", device, mode);
		/*
		 * XXX the rmt protocol does not provide a means to
		 * specify the permission bits; allow rw for everyone,
		 * as modified by the users umask
		 */
		tape = open(device, atoi(mode), 0666);
		if (tape < 0)
			goto ioerror;
		goto respond;

	case 'C':
		DEBUG("rmtd: C\n");
		getstring(device);		/* discard */
		if (close(tape) < 0)
			goto ioerror;
		tape = -1;
		goto respond;

	case 'L':
		getstring(count);
		getstring(pos);
		DEBUG2("rmtd: L %s %s\n", count, pos);
		rval = lseek(tape, (off_t)strtoll(count, NULL, 10), atoi(pos));
		if (rval < 0)
			goto ioerror;
		goto respond;

	case 'W':
		getstring(count);
		n = atoi(count);
		DEBUG1("rmtd: W %s\n", count);
		record = checkbuf(record, n);
		for (i = 0; i < n; i += cc) {
			cc = read(STDIN_FILENO, &record[i], n - i);
			if (cc <= 0) {
				DEBUG("rmtd: premature eof\n");
				exit(2);
			}
		}
		rval = write(tape, record, n);
		if (rval < 0)
			goto ioerror;
		goto respond;

	case 'R':
		getstring(count);
		DEBUG1("rmtd: R %s\n", count);
		n = atoi(count);
		record = checkbuf(record, n);
		rval = read(tape, record, n);
		if (rval < 0)
			goto ioerror;
		(void)sprintf(resp, "A%d\n", rval);
		(void)write(STDOUT_FILENO, resp, strlen(resp));
		(void)write(STDOUT_FILENO, record, rval);
		goto top;

	case 'I':
		getstring(op);
		getstring(count);
		DEBUG2("rmtd: I %s %s\n", op, count);
		{ struct mtop mtop;
		  mtop.mt_op = atoi(op);
		  mtop.mt_count = atoi(count);
		  if (ioctl(tape, MTIOCTOP, (char *)&mtop) < 0)
			goto ioerror;
		  rval = mtop.mt_count;
		}
		goto respond;

	case 'S':		/* status */
		DEBUG("rmtd: S\n");
		{ struct mtget mtget;
		  if (ioctl(tape, MTIOCGET, (char *)&mtget) < 0)
			goto ioerror;
		  rval = sizeof (mtget);
		  if (rval > 24)	/* original mtget structure size */
			rval = 24;
		  (void)sprintf(resp, "A%d\n", rval);
		  (void)write(STDOUT_FILENO, resp, strlen(resp));
		  (void)write(STDOUT_FILENO, (char *)&mtget, rval);
		  goto top;
		}

        case 'V':               /* version */
                getstring(op);
                DEBUG1("rmtd: V %s\n", op);
                rval = 2;
                goto respond;

	default:
		DEBUG1("rmtd: garbage command %c\n", c);
		exit(3);
	}
respond:
	DEBUG1("rmtd: A %d\n", rval);
	(void)sprintf(resp, "A%d\n", rval);
	(void)write(STDOUT_FILENO, resp, strlen(resp));
	goto top;
ioerror:
	error(errno);
	goto top;
}

void
getstring(char *bp)
{
	int i;
	char *cp = bp;

	for (i = 0; i < SSIZE; i++) {
		if (read(STDIN_FILENO, cp+i, 1) != 1)
			exit(0);
		if (cp[i] == '\n')
			break;
	}
	cp[i] = '\0';
}

static char *
checkbuf(char *rec, int size)
{

	if (size <= maxrecsize)
		return (rec);
	if (rec != NULL)
		free(rec);
	rec = malloc(size);
	if (rec == NULL) {
		DEBUG("rmtd: cannot allocate buffer space\n");
		exit(4);
	}
	maxrecsize = size;
	while (size > 1024 &&
	       setsockopt(0, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size)) < 0)
		size -= 1024;
	return (rec);
}

static void
error(int num)
{

	DEBUG2("rmtd: E %d (%s)\n", num, strerror(num));
	(void)snprintf(resp, sizeof(resp), "E%d\n%s\n", num, strerror(num));
	(void)write(STDOUT_FILENO, resp, strlen(resp));
}
