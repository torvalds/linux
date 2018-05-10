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
#include "trace_helpers.h"

static int pmu_fd;

static __u64 time_get_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static __u64 start_time;

#define MAX_CNT 100000ll

static int print_bpf_output(void *data, int size)
{
	static __u64 cnt;
	struct {
		__u64 pid;
		__u64 cookie;
	} *e = data;

	if (e->cookie != 0x12345678) {
		printf("BUG pid %llx cookie %llx sized %d\n",
		       e->pid, e->cookie, size);
		return LIBBPF_PERF_EVENT_ERROR;
	}

	cnt++;

	if (cnt == MAX_CNT) {
		printf("recv %lld events per sec\n",
		       MAX_CNT * 1000000000ll / (time_get_ns() - start_time));
		return LIBBPF_PERF_EVENT_DONE;
	}

	return LIBBPF_PERF_EVENT_CONT;
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
	int ret;

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
	ret = perf_event_poller(pmu_fd, print_bpf_output);
	kill(0, SIGINT);
	return ret;
}
