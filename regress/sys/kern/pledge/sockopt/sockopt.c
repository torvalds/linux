/*
 * Copyright (c) 2017 Alexander Bluhm <bluhm@openbsd.org>
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

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/icmp6.h>

#include <err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
	char promises[1024];
	size_t i, l;
	int s, r;
	int optval;
	socklen_t optlen;

	if (strcmp(PROMISES, "0") != 0) {
		l = strlcpy(promises, "stdio ", sizeof(promises));
		for (i = 0;
		    PROMISES[i] != '\0'&& l < sizeof(promises);
		    i++, l++) {
			promises[l] = PROMISES[i] == '+' ?
			    ' ' : PROMISES[i];
		}
		if (l >= sizeof(promises))
			l = sizeof(promises) - 1;
		promises[l] = '\0';
		warnx("pledge(%s)", promises);
		if (pledge(promises, NULL) == -1)
			err(1, "pledge");
	}

	if ((s = socket(DOMAIN, TYPE, PROTOCOL)) == -1)
		err(1, "socket");
	optlen = sizeof(int);
	if (strcmp(CALL, "set") == 0) {
		optval = OPTVAL;
		r = setsockopt(s, LEVEL, OPTNAME, &optval, optlen);
	} else if (strcmp(CALL, "get") == 0) {
		optval = 0;
		r = getsockopt(s, LEVEL, OPTNAME, &optval, &optlen);
	} else {
		errx(1, "call: %s", CALL);
	}
	if (r == 0) {
		if (ERRNO != 0)
			errx(1, "success");
	} else if (r == -1) {
		if (errno != ERRNO)
			err(1, "error");
	} else {
		errx(1, "return: %d", r);
	}
	if (optval != OPTVAL)
		errx(1, "optval: %d", optval);

	return (0);
}
