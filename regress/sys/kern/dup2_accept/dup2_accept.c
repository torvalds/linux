/*	$OpenBSD: dup2_accept.c,v 1.3 2018/07/10 08:08:00 mpi Exp $	*/
/*
 * Copyright (c) 2018 Martin Pieuchot
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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <stdlib.h>

/*
 * Listen on localhost:TEST_PORT for a connection
 */
#define	TEST_PORT       9876

static void *
dupper(void *arg)
{
	struct sockaddr_in addr;
	int s;

	sleep(1);

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s == -1)
		err(1, "socket");

	assert(s == 5);

	/*
	 * Make sure that dup'ing a LARVAL file fails with EBUSY.
	 *
	 * Otherwise abort the program before calling connect(2),
	 * this was previously panic'ing the kernel.
	 */
	if ((dup2(0, 4) != 4) && (errno != EBUSY))
		err(1, "dup2");

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(TEST_PORT);

	if (connect(5, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		err(1, "connect");

	return NULL;
}

int
main(void)
{
	struct sockaddr_in serv_addr;
	struct sockaddr client_addr;
	int error, s, len, fd;
	pthread_t tr;

	if ((error = pthread_create(&tr, NULL, dupper, NULL)))
		errc(1, error, "pthread_create");

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s == -1)
		err(1, "socket");

	assert(s == 3);

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	serv_addr.sin_port = htons(TEST_PORT);

	if (bind(s, (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
		err(1, "bind");

	if (listen(s, 3))
		err(1, "listen");

	len = sizeof(client_addr);
	fd = accept(s, &client_addr, &len);

	return 0;
}
