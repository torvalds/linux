/*	$OpenBSD: sigio_pipe.c,v 1.1 2020/09/16 14:02:23 mpi Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
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

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "common.h"

int
test_pipe_badpgid(void)
{
	int fds[2];

	assert(pipe(fds) == 0);
	return test_common_badpgid(fds[0]);
}

int
test_pipe_badsession(void)
{
	int fds[2];

	assert(pipe(fds) == 0);
	return test_common_badsession(fds[0]);
}

int
test_pipe_cansigio(void)
{
	int fds[2];

	assert(pipe(fds) == 0);
	return test_common_cansigio(fds);
}

int
test_pipe_getown(void)
{
	int fds[2];

	assert(pipe(fds) == 0);
	return test_common_getown(fds[0]);
}

int
test_pipe_read(void)
{
	int fds[2];

	assert(pipe(fds) == 0);
	return test_common_read(fds);
}

int
test_pipe_write(void)
{
	int fds[2];

	assert(pipe(fds) == 0);
	return test_common_write(fds);
}
