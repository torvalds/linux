/*
 * Copyright _ 2018 Alexey Dobriyan <adobriyan@gmail.com>
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
// Test that values in /proc/uptime increment monotonically.
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "proc-uptime.h"

int main(void)
{
	uint64_t start, u0, u1, i0, i1;
	int fd;

	fd = open("/proc/uptime", O_RDONLY);
	assert(fd >= 0);

	proc_uptime(fd, &u0, &i0);
	start = u0;
	do {
		proc_uptime(fd, &u1, &i1);
		assert(u1 >= u0);
		assert(i1 >= i0);
		u0 = u1;
		i0 = i1;
	} while (u1 - start < 100);

	return 0;
}
