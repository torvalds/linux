/*
 * Copyright © 2018 Alexey Dobriyan <adobriyan@gmail.com>
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
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "proc.h"

static void proc_uptime(int fd, uint64_t *uptime, uint64_t *idle)
{
	uint64_t val1, val2;
	char buf[64], *p;
	ssize_t rv;

	/* save "p < end" checks */
	memset(buf, 0, sizeof(buf));
	rv = pread(fd, buf, sizeof(buf), 0);
	assert(0 <= rv && rv <= sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';

	p = buf;

	val1 = xstrtoull(p, &p);
	assert(p[0] == '.');
	assert('0' <= p[1] && p[1] <= '9');
	assert('0' <= p[2] && p[2] <= '9');
	assert(p[3] == ' ');

	val2 = (p[1] - '0') * 10 + p[2] - '0';
	*uptime = val1 * 100 + val2;

	p += 4;

	val1 = xstrtoull(p, &p);
	assert(p[0] == '.');
	assert('0' <= p[1] && p[1] <= '9');
	assert('0' <= p[2] && p[2] <= '9');
	assert(p[3] == '\n');

	val2 = (p[1] - '0') * 10 + p[2] - '0';
	*idle = val1 * 100 + val2;

	assert(p + 4 == buf + rv);
}
