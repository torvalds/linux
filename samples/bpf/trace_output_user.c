/* This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/perf_event.h>
#include <linux/bpf.h>
#include <errno.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <signal.h>
#include "libbpf.h"
#include "bpf_load.h"
#include "perf-sys.h"

static int pmu_fd;

int page_size;
int page_cnt = 8;
volatile struct perf_event_mmap_page *header;

typedef void (*print_fn)(void *data, int size);

static int perf_event_mmap(int fd)
{
	void *base;
	int mmap_size;

	page_size = getpagesize();
	mmap_size = page_size * (page_cnt + 1);

	base = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (base == MAP_FAILED) {
		printf("mmap err\n");
		return -1;
	}

	header = base;
	return 0;
}

static int perf_event_poll(int fd)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };

	return poll(&pfd, 1, 1000);
}

struct perf_event_sample {
	struct perf_event_header header;
	__u32 size;
	char data[];
};

static void perf_event_read(print_fn fn)
{
	__u64 data_tail = header->data_tail;
	__u64 data_head = header->data_head;
	__u64 buffer_size = page_cnt * page_size;
	void *base, *begin, *end;
	char buf[256];

	asm volatile("" ::: "memory"); /* in real code it should be smp_rmb() */
	if (data_head == data_tail)
		return;

	base = ((char *)header) + page_size;

	begin = base + data_tail % buffer_size;
	end = base + data_head % buffer_size;

	while (begin != end) {
		struct perf_event_sample *e;

		e = begin;
		if (begin + e->header.size > base + buffer_size) {
			long len = base + buffer_size - begin;

			assert(len < e->header.size);
			memcpy(buf, begin, len);
			memcpy(buf + len, base, e->header.size - len);
			e = (void *) buf;
			begin = base + e->header.size - len;
		} else if (begin + e->header.size == base + buffer_size) {
			begin = base;
		} else {
			begin += e->header.size;
		}

		if (e->header.type == PERF_RECORD_SAMPLE) {
			fn(e->data, e->size);
		} else if (e->header.type == PERF_RECORD_LOST) {
			struct {
				struct perf_event_header header;
				__u64 id;
				__u64 lost;
			} *lost = (void *) e;
			printf("lost %lld events\n", lost->lost);
		} else {
			printf("unknown event type=%d size=%d\n",
			       e->header.type, e->header.size);
		}
	}

	__sync_synchronize(); /* smp_mb() */
	header->data_tail = data_head;
}

static __u64 time_get_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static __u64 start_time;

#define MAX_CNT 100000ll

static void print_bpf_output(void *data, int size)
{
	static __u64 cnt;
	struct {
		__u64 pid;
		__u64 cookie;
	} *e = data;

	if (e->cookie != 0x12345678) {
		printf("BUG pid %llx cookie %llx sized %d\n",
		       e->pid, e->cookie, size);
		kill(0, SIGINT);
	}

	cnt++;

	if (cnt == MAX_CNT) {
		printf("recv %lld events per sec\n",
		       MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
		kill(0, SIGINT);
	}
}

static void test_bpf_perf_event(void)
{
	struct perf_event_attr attr = {
		.sample_type = PERF_SAMPLE_RAW,
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_BPF_OUTPUT,
	};
	int key = 0;

	pmu_fd = sys_perf_event_open(&attr, -1/*pid*/, 0/*cpu*/, -1/*group_fd*/, 0);

	assert(pmu_fd >= 0);
	assert(bpf_map_update_elem(map_fd[0], &key, &pmu_fd, BPF_ANY) == 0);
	ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0);
}

int main(int argc, char **argv)
{
	char filename[256];
	FILE *f;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	test_bpf_perf_event();

	if (perf_event_mmap(pmu_fd) < 0)
		return 1;

	f = popen("taskset 1 dd if=/dev/zero of=/dev/null", "r");
	(void) f;

	start_time = time_get_ns();
	for (;;) {
		perf_event_poll(pmu_fd);
		perf_event_read(print_bpf_output);
	}

	return 0;
}
