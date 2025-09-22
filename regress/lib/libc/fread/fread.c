/*
 * Copyright (c) 2018 Todd C. Miller <millert@openbsd.org>
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

#include <sys/socket.h>
#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Test reading from a socket until EOF with multiple writes on
 * the other end.  The send and receive buffer sizes are reduced
 * to force multiple read(2) and write(2) calls to happen.
 *
 * Tests unbuffered, line buffered and fully-buffers.
 *
 * This test catches bugs in stdio/fread.c revs 1.13 and 1.17.
 */

static char test_string[] =
	"Now is the time for all good men to come to the aid of the party\n"
	"The quick brown fox jumps over the lazy dog\n"
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\n"
	"Insert test text here..\n";

static char *
iomode2str(int iomode)
{
	switch (iomode) {
	case _IOFBF:
		return "fully buffered";
	case _IOLBF:
		return "line buffered";
	case _IONBF:
		return "unbuffered";
	default:
		return "unknown";
	}
}

static void
dochild(int fd)
{
	size_t left;
	ssize_t nwritten;
	char *ts = test_string;

	left = strlen(test_string);
	while (left != 0) {
		nwritten = write(fd, ts, left);
		if (nwritten == -1)
			err(1, "write");
		left -= nwritten;
		ts += nwritten;
	}
	close(fd);
	_exit(0);
}

int
dotest(int iomode, char *iobuf, size_t iolen)
{
    char *ts = test_string;
    size_t nread, total = 0, off = 0;
    int sv[2], val;
    char buf[21];
    pid_t child;
    FILE *fp;

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) == -1)
	    err(1, "socketpair");
    val = 16;
    if (setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) == -1)
	    err(1, "setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF)");
    if (setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) == -1)
	    err(1, "setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF)");
    if (setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) == -1)
	    err(1, "setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF)");
    if (setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) == -1)
	    err(1, "setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF)");

    if ((fp = fdopen(sv[0], "r")) == NULL)
	    err(1, "fdopen");

    setvbuf(fp, iobuf, iomode, iolen);

    switch ((child = fork())) {
    case -1:
	    err(1, "fork");
    case 0:
	    close(sv[0]);
	    dochild(sv[1]);
    default:
	    close(sv[1]);
	    break;
    }

    while ((nread = fread(buf, 1, sizeof(buf), fp)) != 0) {
	    if (nread > sizeof(buf)) {
		    warnx("%s: max %zu bytes but got %zu",
			iomode2str(iomode), sizeof(buf), nread);
		    return 1;
	    }
	    if (strncmp(buf, test_string + off, nread) != 0) {
		    warnx("%s: mismatch: expected %.*s, got %.*s",
			iomode2str(iomode), (int)nread, test_string + off,
			(int)nread, buf);
		    return 1;
	    }
	    total += nread;
	    off += nread;
    }
    if (!feof(fp)) {
	    if (ferror(fp))
		    warn("%s: read error", iomode2str(iomode));
	    else
		    warnx("%s: missing EOF", iomode2str(iomode));
	    return 1;
    }
    fclose(fp);
    waitpid(child, NULL, 0);

    return 0;
}

int
main(int argc, char *argv[])
{
    char iobuf[4096];
    int errors = 0;

    errors += dotest(_IOFBF, iobuf, sizeof(iobuf));
    errors += dotest(_IOLBF, iobuf, sizeof(iobuf));
    errors += dotest(_IONBF, NULL, 0);

    return errors;
}
