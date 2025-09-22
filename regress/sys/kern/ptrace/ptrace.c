/*	$OpenBSD: ptrace.c,v 1.5 2017/09/16 02:03:40 guenther Exp $	*/
/*
 * Copyright (c) 2005 Artur Grabowski <art@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <err.h>

static void
usage(void)
{
	fprintf(stderr, "Usage: ptrace [-bdirwI]\n");
	exit(1);
}

#define MAGIC	"asdfblahblahspam1235432blah"
#define MAGIC_I	0x47114217

int
main(int argc, char **argv)
{
	int ch;
	int bad = 0, i, write, I;
	pid_t pid;
	char *m;
	int ps;
	int status;
	int ret;

	ps = getpagesize();

	I = 0;
	i = 0;
	write = 0;

	while ((ch = getopt(argc, argv, "bdirwI")) != -1) {
		switch (ch) {
		case 'b':
			bad = 1;
			break;
		case 'i':
			i = 1;
			break;
		case 'd':
			i = 0;
			break;
		case 'r':
			write = 0;
			break;
		case 'w':
			write = 1;
			break;
		case 'I':
			I = 1;
			break;
		default:
			usage();
		}
	}

	m = mmap(0, ps, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
	if (m == MAP_FAILED)
		err(1, "mmap");

	if (!write) {
		if (I)
			memcpy(m, MAGIC, sizeof(MAGIC));
		else
			*(int *)m = MAGIC_I;
	}
	if (bad)
		if (mprotect(m, ps, PROT_NONE))
			err(1, "mprotect");

	switch ((pid = fork())) {
	case 0:
		pause();
		_exit(0);
	case -1:
		err(1, "fork");
	}

	ret = 0;

	if (ptrace(PT_ATTACH, pid, 0, 0) == -1) {
		warn("ptrace(PT_ATTACH)");
		ret = -1;
		goto out;
	}

	if (wait(&status) != pid) {
		warn("wait");
		ret = -1;
		goto out;
	}

	if (!write) {
		int req;

		if (I) {
			char foo[1024];
			struct ptrace_io_desc piod;

			if (i)
				piod.piod_op = PIOD_READ_I;
			else
				piod.piod_op = PIOD_READ_D;
			piod.piod_offs = m;
			piod.piod_addr = &foo;
			piod.piod_len = sizeof(MAGIC);

			if (ptrace(PT_IO, pid, (caddr_t)&piod, 0) == -1) {
				warn("ptrace(PT_IO)");
				if (errno == EFAULT)
					ret = 1;
				else
					ret = -1;
				goto out;
			}

			if (memcmp(foo, MAGIC, sizeof(MAGIC))) {
				warnx("mismatch %s != %s", foo, MAGIC);
				ret = 1;
				goto out;
			}
		} else {
			if (i)
				req = PT_READ_I;
			else
				req = PT_READ_D;

			i = ptrace(req, pid, m, sizeof(i));
			if (i != MAGIC_I) {
				warn("ptrace(%d): %d != %d", req, i, MAGIC_I);
				ret = 1;
				goto out;
			}
		}
	} else {
		errx(1, "lazy bum");
	}

out:
	if (ret == -1) {
		/* other errors */
		ret = 1;
	} else if (bad) {
		if (ret == 0) {
			warnx("protected memory unprotected");
			ret = 1;
		} else {
			ret = 0;
		}
	}

	kill(pid, SIGKILL);

	return ret;
}
