/* $OpenBSD: undgram_selfconn.c,v 1.1 2021/12/09 17:25:54 mvs Exp $ */

/*
 * Copyright (c) 2021 Vitaliy Makkoveev <mvs@openbsd.org>
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

/*
 * unix(4) datagram socket could be connected to itself. There are two
 * cases: temporary connection for sendto(2) syscall and normal connection
 * provided by connect(2) syscall.
 * Be sure socket doesn't deadlock itself and doesn't crash kernel while
 * disconnecting and connecting again.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
	static struct sockaddr_un sun;
	int s, buf;
	ssize_t ret;

	umask(0077);

	memset(&sun, 0, sizeof(sun));
	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof(sun.sun_path) - 1,
	    "undgram_selfconn%d.socket", getpid());

	unlink(sun.sun_path);

	if ((s = socket(AF_UNIX, SOCK_DGRAM|SOCK_NONBLOCK, 0)) < 0)
		err(1, "socket");
	if (bind(s, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		err(1, "bind");

	if (sendto(s, &s, sizeof(s), 0,
	    (struct sockaddr *)&sun, sizeof(sun)) < 0)
		err(1, "sendto");

	/*
	 * Check received data to be sure the temporary connection
	 * was successful.
	 */
	if ((ret = recvfrom(s, &buf, sizeof(buf), 0, NULL, NULL)) < 0)
		err(1, "recvfrom");
	if (ret != sizeof(s))
		errx(1, "recvfrom: wrong size");
	if (buf != s)
		errx(1, "recvfrom: wrong data");

	if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		err(1, "connect");
	/* Disconnect and connect it again */
	if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		err(1, "connect");
	close(s);

	return 0;
}
