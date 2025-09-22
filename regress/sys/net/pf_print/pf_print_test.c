/*	$OpenBSD: pf_print_test.c,v 1.1 2016/08/24 22:31:41 bluhm Exp $ */

/*
 * Copyright (c) 2008, 2013 Alexander Bluhm <bluhm@openbsd.org>
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
#include <sys/socket.h>
#include <sys/wait.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/pfvar.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	INET	1
#define	INET6	1

void			pf_print_host(struct pf_addr *, u_int16_t, u_int8_t);
#define	addlog		printf

char *ipv6_addrs[] = {
	"::",
	"::1",
	"1::",
	"1::1",
	"0:1::1:0",
	"::1:2:0",
	"0:1::",
	"::1:0:0:0",
	"1:2:3:4:5:6:7:8",
	"0:2:3:4:5:6:7:8",
	"1:2:3:4:5:6:7:0",
	"1:0:3:0:5:0:7:8",
	"::3:4:5:6:7:8",
	"1:2:3:4:5:6::",
	"0:2:3::6:7:8",
	"1:2:0:4:5::8",
	"1::4:5:0:0:8",
	"1::5:0:0:8",
	"1:0:0:4::8",
	"::4:5:6:0:0",
	"0:0:3:4:5::",
	"::4:5:0:0:0",
	"1234:5678:90ab:cdef:1234:5678:90ab:cdef",
	NULL
};

int
main(int argc, char *argv[])
{
	char str[100];
	struct pf_addr addr;
	FILE *fpipe;
	char **in, *out;
	size_t len;
	pid_t pid;
	int fds[2];
	int status, ret = 0;

	for (in = ipv6_addrs; *in; in++) {
		if (!inet_pton(AF_INET6, *in, &addr.v6))
			errx(2, "inet_pton %s", *in);
		if (!inet_ntop(AF_INET6, &addr.v6, str, sizeof(str)))
			errx(2, "inet_ntop %s", *in);
		if (strcmp(*in, str) != 0) {
			warnx("not equal\nin:\t%s\nstr:\t%s", *in, str);
			ret = 2;
		}
		if (pipe(fds) == -1)
			err(2, "pipe");
		if ((pid = fork()) == -1)
			err(2, "fork");
		if (pid == 0) {
			close(fds[0]);
			if (dup2(fds[1], 1) == -1)
				err(2, "dup2");
			close(fds[1]);
			pf_print_host(&addr, 0, AF_INET6);
			fflush(stdout);
			_exit(0);
		}
		close(fds[1]);
		if ((fpipe = fdopen(fds[0], "r")) == NULL)
			err(2, "fdopen");
		if ((out = fgetln(fpipe, &len)) == NULL)
			err(2, "fgetln");
		if (out[len - 1] == '\n')
			out[len - 1] = '\0';
		else {
			char *tmp;
			/* EOF without EOL, copy and add the NUL */
			if ((tmp = malloc(len + 1)) == NULL)
				err(2, "malloc");
			memcpy(tmp, out, len);
			tmp[len] = '\0';
			out = tmp;
		}
		if (fclose(fpipe) == EOF)
			err(2, "fclose");
		if (wait(&status) <= 0)
			err(2, "wait");
		if (status != 0)
			errx(2, "child exit status: %d", status);
		if (strcmp(*in, out) != 0) {
			warnx("not equal\nin:\t%s\nout:\t%s", *in, out);
			ret = 1;
		}
	}
	return (ret);
}

#include "pf_print_host.c"
