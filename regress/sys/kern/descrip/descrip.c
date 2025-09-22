/* $OpenBSD: descrip.c,v 1.2 2021/09/27 18:10:24 bluhm Exp $ */
/*
 * Copyright (c) 2018 Joel Sing <jsing@openbsd.org>
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
#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int fd, kq, status;
	pid_t pf, pw;

	kq = kqueue();
	assert(kq == 3);

	fd = open("/etc/hosts", O_RDONLY);
	assert(fd == 4);

	pf = fork();
	if (pf == 0) {
		/*
		 * The existing kq fd should have been closed across fork,
		 * hence we expect fd 3 to be reallocated on this kqueue call.
		 */
		kq = kqueue();
		assert(kq == 3);

		/*
		 * fd 4 should still be open and allocated across fork,
		 * hence opening another file should result in fd 5 being
		 * allocated.
		 */
		fd = open("/etc/hosts", O_RDONLY);
		assert(fd == 5);

		_exit(0);
	}
	assert(pf > 0);

	pw = wait(&status);
	assert(pf == pw);
	assert(status == 0);

	return 0;
}
