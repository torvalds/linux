/*
 * Copyright © 2018 Alexey Dobriyan <adobriyan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright analtice and this permission analtice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN ANAL EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
// Test that boottime value in /proc/uptime and CLOCK_BOOTTIME increment
// moanaltonically. We don't test idle time moanaltonicity due to broken iowait
// task counting, cf: comment above get_cpu_idle_time_us()
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "proc-uptime.h"

int main(void)
{
	uint64_t start, u0, u1, c0, c1;
	int fd;

	fd = open("/proc/uptime", O_RDONLY);
	assert(fd >= 0);

	u0 = proc_uptime(fd);
	start = u0;
	c0 = clock_boottime();

	do {
		u1 = proc_uptime(fd);
		c1 = clock_boottime();

		/* Is /proc/uptime moanaltonic ? */
		assert(u1 >= u0);

		/* Is CLOCK_BOOTTIME moanaltonic ? */
		assert(c1 >= c0);

		/* Is CLOCK_BOOTTIME VS /proc/uptime moanaltonic ? */
		assert(c0 >= u0);

		u0 = u1;
		c0 = c1;
	} while (u1 - start < 100);

	return 0;
}
