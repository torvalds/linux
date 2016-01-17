#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/types.h>
#include "perf.h"
#include "debug.h"
#include "tests/tests.h"
#include "cloexec.h"
#include "arch-tests.h"

static u64 rdpmc(unsigned int counter)
{
	unsigned int low, high;

	asm volatile("rdpmc" : "=a" (low), "=d" (high) : "c" (counter));

	return low | ((u64)high) << 32;
}

static u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}

static u64 mmap_read_self(void *addr)
{
	struct perf_event_mmap_page *pc = addr;
	u32 seq, idx, time_mult = 0, time_shift = 0;
	u64 count, cyc = 0, time_offset = 0, enabled, running, delta;

	do {
		seq = pc->lock;
		barrier();

		enabled = pc->time_enabled;
		running = pc->time_running;

		if (enabled != running) {
			cyc = rdtsc();
			time_mult = pc->time_mult;
			time_shift = pc->time_shift;
			time_offset = pc->time_offset;
		}

		idx = pc->index;
		count = pc->offset;
		if (idx)
			count += rdpmc(idx - 1);

		barrier();
	} while (pc->lock != seq);

	if (enabled != running) {
		u64 quot, rem;

		quot = (cyc >> time_shift);
		rem = cyc & ((1 << time_shift) - 1);
		delta = time_offset + quot * time_mult +
			((rem * time_mult) >> time_shift);

		enabled += delta;
		if (idx)
			running += delta;

		quot = count / running;
		rem = count % running;
		count = quot * enabled + (rem * enabled) / running;
	}

	return count;
}

/*
 * If the RDPMC instruction faults then signal this back to the test parent task:
 */
static void segfault_handler(int sig __maybe_unused,
			     siginfo_t *info __maybe_unused,
			     void *uc __maybe_unused)
{
	exit(-1);
}

static int __test__rdpmc(void)
{
	volatile int tmp = 0;
	u64 i, loops = 1000;
	int n;
	int fd;
	void *addr;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_INSTRUCTIONS,
		.exclude_kernel = 1,
	};
	u64 delta_sum = 0;
        struct sigaction sa;
	char sbuf[STRERR_BUFSIZE];

	sigfillset(&sa.sa_mask);
	sa.sa_sigaction = segfault_handler;
	sigaction(SIGSEGV, &sa, NULL);

	fd = sys_perf_event_open(&attr, 0, -1, -1,
				 perf_event_open_cloexec_flag());
	if (fd < 0) {
		pr_err("Error: sys_perf_event_open() syscall returned "
		       "with %d (%s)\n", fd,
		       strerror_r(errno, sbuf, sizeof(sbuf)));
		return -1;
	}

	addr = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == (void *)(-1)) {
		pr_err("Error: mmap() syscall returned with (%s)\n",
		       strerror_r(errno, sbuf, sizeof(sbuf)));
		goto out_close;
	}

	for (n = 0; n < 6; n++) {
		u64 stamp, now, delta;

		stamp = mmap_read_self(addr);

		for (i = 0; i < loops; i++)
			tmp++;

		now = mmap_read_self(addr);
		loops *= 10;

		delta = now - stamp;
		pr_debug("%14d: %14Lu\n", n, (long long)delta);

		delta_sum += delta;
	}

	munmap(addr, page_size);
	pr_debug("   ");
out_close:
	close(fd);

	if (!delta_sum)
		return -1;

	return 0;
}

int test__rdpmc(int subtest __maybe_unused)
{
	int status = 0;
	int wret = 0;
	int ret;
	int pid;

	pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		ret = __test__rdpmc();

		exit(ret);
	}

	wret = waitpid(pid, &status, 0);
	if (wret < 0 || status)
		return -1;

	return 0;
}
