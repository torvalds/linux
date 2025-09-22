/*	$OpenBSD: pfioctl1.c,v 1.3 2022/02/26 20:14:06 bluhm Exp $ */
/*
 * Copyright (c) 2016 Sebastian Benoit <benno@openbsd.org>
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
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PF_SOCKET		"/dev/pf"

int	test_pf_status(int);

int
test_pf_status(int s)
{
	struct pf_status	status;
	int			ret = 0;

	if (ioctl(s, DIOCGETSTATUS, &status) == -1)
		err(1, "%s: DIOCGETSTATUS", __func__);
	if (!status.running)
		warnx("%s: pf is disabled", __func__);

	return (ret);
}


int
main(int argc, char *argv[])
{
	int			s;

	/* a file opened before pledge (!fdpledged) can be used for ioctls */
	if ((s = open(PF_SOCKET, O_RDWR)) == -1) {
		err(1, "%s: cannot open pf socket", __func__);
	}
	printf("pf ioctl with file opened before pledge succeeds (1)\n");
	test_pf_status(s);

	if (pledge("stdio pf", NULL) == -1)
		err(1, "pledge");

	printf("pf ioctl with file opened before pledge succeeds (2)\n");
	test_pf_status(s);
	close(s);
	exit(0);
}
