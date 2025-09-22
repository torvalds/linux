/*	$OpenBSD: rmt.c,v 1.23 2019/06/28 13:32:50 deraadt Exp $	*/

/*
 * Copyright (c) 1983 Regents of the University of California.
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

/*
 * rmt
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>

int	tape = -1;

char	*record;
int	maxrecsize = -1;

#define	STRSIZE	64
char	device[PATH_MAX];
char	lastdevice[PATH_MAX] = "";
char	count[STRSIZE], mode[STRSIZE], pos[STRSIZE], op[STRSIZE];

char	resp[BUFSIZ];

FILE	*debug;
#define	DEBUG(f)	if (debug) fprintf(debug, f)
#define	DEBUG1(f,a)	if (debug) fprintf(debug, f, a)
#define	DEBUG2(f,a1,a2)	if (debug) fprintf(debug, f, a1, a2)

char		*checkbuf(char *, int);
void		getstring(char *, int);
void		error(int);
__dead void	usage(void);

int
main(int argc, char *argv[])
{
	off_t orval;
	int rval;
	char c;
	int n, i, cc;
	int ch, rflag = 0, wflag = 0;
	int f, acc;
	mode_t m;
	char *dir = NULL;
	char *devp;
	size_t dirlen;

	if (pledge("stdio rpath wpath cpath inet", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "d:rw")) != -1) {
		switch (ch) {
		case 'd':
			dir = optarg;
			if (*dir != '/')
				errx(1, "directory must be absolute");
			break;
		case 'r':
			rflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (rflag && wflag)
		usage();

	if (argc > 0) {
		debug = fopen(*argv, "w");
		if (debug == 0)
			err(1, "cannot open debug file");
		setvbuf(debug, NULL, _IONBF, 0);
	}

	if (dir) {
		if (chdir(dir) != 0)
			err(1, "chdir");
		dirlen = strlen(dir);
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
		getstring(device, sizeof(device));
		getstring(mode, sizeof(mode));
		DEBUG2("rmtd: O %s %s\n", device, mode);

		devp = device;
		f = atoi(mode);
		m = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
		acc = f & O_ACCMODE;
		if (dir) {
			/* Strip away valid directory prefix. */
			if (strncmp(dir, devp, dirlen) == 0 &&
			    (devp[dirlen - 1] == '/' ||
			     devp[dirlen] == '/')) {
			     devp += dirlen;
			     while (*devp == '/')
				devp++;
			}
			/* Don't allow directory traversal. */
			if (strchr(devp, '/')) {
				errno = EACCES;
				goto ioerror;
			}
			f |= O_NOFOLLOW;
		}
		if (rflag) {
			/*
			 * Only allow readonly open and ignore file
			 * creation requests.
			 */
			if (acc != O_RDONLY) {
				errno = EPERM;
				goto ioerror;
			}
			f &= ~O_CREAT;
		} else if (wflag) {
			/*
			 * Require, and force creation of, a nonexistent file,
			 * unless we are reopening the last opened file again,
			 * in which case it is opened read-only.
			 */
			if (strcmp(devp, lastdevice) != 0) {
				/*
				 * Disallow read-only open since that would
				 * only result in an empty file.
				 */
				if (acc == O_RDONLY) {
					errno = EPERM;
					goto ioerror;
				}
				f |= O_CREAT | O_EXCL;
			} else {
				acc = O_RDONLY;
			}
			/* Create readonly file */
			m = S_IRUSR|S_IRGRP|S_IROTH;
		}
		/* Apply new access mode. */
		f = (f & ~O_ACCMODE) | acc;

		tape = open(devp, f, m);
		if (tape == -1)
			goto ioerror;
		(void)strlcpy(lastdevice, devp, sizeof(lastdevice));
		goto respond;

	case 'C':
		DEBUG("rmtd: C\n");
		getstring(device, sizeof(device));	/* discard */
		if (close(tape) == -1)
			goto ioerror;
		tape = -1;
		goto respond;

	case 'L':
		getstring(count, sizeof(count));
		getstring(pos, sizeof(pos));
		DEBUG2("rmtd: L %s %s\n", count, pos);
		orval = lseek(tape, strtoll(count, NULL, 0), atoi(pos));
		if (orval == -1)
			goto ioerror;
		goto respond;

	case 'W':
		getstring(count, sizeof(count));
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
		if (rval == -1)
			goto ioerror;
		goto respond;

	case 'R':
		getstring(count, sizeof(count));
		DEBUG1("rmtd: R %s\n", count);
		n = atoi(count);
		record = checkbuf(record, n);
		rval = read(tape, record, n);
		if (rval == -1)
			goto ioerror;
		(void) snprintf(resp, sizeof resp, "A%d\n", rval);
		(void) write(STDOUT_FILENO, resp, strlen(resp));
		(void) write(STDOUT_FILENO, record, rval);
		goto top;

	case 'I':
		getstring(op, sizeof(op));
		getstring(count, sizeof(count));
		DEBUG2("rmtd: I %s %s\n", op, count);
		{ struct mtop mtop;
		  mtop.mt_op = atoi(op);
		  mtop.mt_count = atoi(count);
		  if (ioctl(tape, MTIOCTOP, (char *)&mtop) == -1)
			goto ioerror;
		  rval = mtop.mt_count;
		}
		goto respond;

	case 'S':		/* status */
		DEBUG("rmtd: S\n");
		{ struct mtget mtget;
		  if (ioctl(tape, MTIOCGET, (char *)&mtget) == -1)
			goto ioerror;
		  rval = sizeof (mtget);
		  (void) snprintf(resp, sizeof resp, "A%d\n", rval);
		  (void) write(STDOUT_FILENO, resp, strlen(resp));
		  (void) write(STDOUT_FILENO, (char *)&mtget, sizeof (mtget));
		  goto top;
		}

	default:
		DEBUG1("rmtd: garbage command %c\n", c);
		exit(3);
	}
respond:
	DEBUG1("rmtd: A %d\n", rval);
	(void) snprintf(resp, sizeof resp, "A%d\n", rval);
	(void) write(STDOUT_FILENO, resp, strlen(resp));
	goto top;
ioerror:
	error(errno);
	goto top;
}

void
getstring(char *bp, int size)
{
	char *cp = bp;
	char *ep = bp + size - 1;

	do {
		if (read(STDIN_FILENO, cp, 1) != 1)
			exit(0);
	} while (*cp != '\n' && ++cp < ep);
	*cp = '\0';
}

char *
checkbuf(char *record, int size)
{
	if (size <= maxrecsize)
		return (record);
	if (record != 0)
		free(record);
	record = malloc(size);
	if (record == 0) {
		DEBUG("rmtd: cannot allocate buffer space\n");
		exit(4);
	}
	maxrecsize = size;
	while (size > 1024 &&
	    setsockopt(0, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size)) == -1)
		size -= 1024;
	return (record);
}

void
error(int num)
{

	DEBUG2("rmtd: E %d (%s)\n", num, strerror(num));
	(void) snprintf(resp, sizeof (resp), "E%d\n%s\n", num, strerror(num));
	(void) write(STDOUT_FILENO, resp, strlen(resp));
}

__dead void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr, "usage: %s [-r | -w] [-d directory]\n",
	    __progname);
	exit(1);
}
