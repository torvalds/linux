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
// Test that values in /proc/uptime increment monotonically
// while shifting across CPUs.
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "proc-uptime.h"

static inline int sys_sched_getaffinity(pid_t pid, unsigned int len, unsigned long *m)
{
	return syscall(SYS_sched_getaffinity, pid, len, m);
}

static inline int sys_sched_setaffinity(pid_t pid, unsigned int len, unsigned long *m)
{
	return syscall(SYS_sched_setaffinity, pid, len, m);
}

int main(void)
{
	unsigned int len;
	unsigned long *m;
	unsigned int cpu;
	uint64_t u0, u1, i0, i1;
	int fd;

	/* find out "nr_cpu_ids" */
	m = NULL;
	len = 0;
	do {
		len += sizeof(unsigned long);
		free(m);
		m = malloc(len);
	} while (sys_sched_getaffinity(0, len, m) == -1 && errno == EINVAL);

	fd = open("/proc/uptime", O_RDONLY);
	assert(fd >= 0);

	proc_uptime(fd, &u0, &i0);
	for (cpu = 0; cpu < len * 8; cpu++) {
		memset(m, 0, len);
		m[cpu / (8 * sizeof(unsigned long))] |= 1UL << (cpu % (8 * sizeof(unsigned long)));

		/* CPU might not exist, ignore error */
		sys_sched_setaffinity(0, len, m);

		proc_uptime(fd, &u1, &i1);
		assert(u1 >= u0);
		assert(i1 >= i0);
		u0 = u1;
		i0 = i1;
	}

	return 0;
}
