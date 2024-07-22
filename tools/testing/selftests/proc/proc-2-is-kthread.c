/*
 * Copyright (c) 2024 Alexey Dobriyan <adobriyan@gmail.com>
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
/* Test that kernel thread is reported as such. */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	/*
	 * The following solutions don't really work:
	 *
	 * 1) jit kernel module which creates kernel thread:
	 * test becomes arch-specific,
	 * problems with mandatory module signing,
	 * problems with lockdown mode,
	 * doesn't work with CONFIG_MODULES=n at all,
	 * kthread creation API is formally unstable internal kernel API,
	 * need a mechanism to report test kernel thread's PID back,
	 *
	 * 2) ksoftirqd/0 and kswapd0 look like stable enough kernel threads,
	 * but their PIDs are unstable.
	 *
	 * Check against kthreadd which always seem to exist under pid 2.
	 */
	int fd = open("/proc/2/status", O_RDONLY);
	assert(fd >= 0);

	char buf[4096];
	ssize_t rv = read(fd, buf, sizeof(buf));
	assert(0 <= rv && rv < sizeof(buf));
	buf[rv] = '\0';

	assert(strstr(buf, "Kthread:\t1\n"));

	return 0;
}
