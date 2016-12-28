/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
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
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <linux/bpf.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include "libbpf.h"
#include "bpf_load.h"
#include "perf-sys.h"

#define SAMPLE_FREQ 50

static bool sys_read_seen, sys_write_seen;

static void print_ksym(__u64 addr)
{
	struct ksym *sym;

	if (!addr)
		return;
	sym = ksym_search(addr);
	printf("%s;", sym->name);
	if (!strcmp(sym->name, "sys_read"))
		sys_read_seen = true;
	else if (!strcmp(sym->name, "sys_write"))
		sys_write_seen = true;
}

static void print_addr(__u64 addr)
{
	if (!addr)
		return;
	printf("%llx;", addr);
}

#define TASK_COMM_LEN 16

struct key_t {
	char comm[TASK_COMM_LEN];
	__u32 kernstack;
	__u32 userstack;
};

static void print_stack(struct key_t *key, __u64 count)
{
	__u64 ip[PERF_MAX_STACK_DEPTH] = {};
	static bool warned;
	int i;

	printf("%3lld %s;", count, key->comm);
	if (bpf_map_lookup_elem(map_fd[1], &key->kernstack, ip) != 0) {
		printf("---;");
	} else {
		for (i = PERF_MAX_STACK_DEPTH - 1; i >= 0; i--)
			print_ksym(ip[i]);
	}
	printf("-;");
	if (bpf_map_lookup_elem(map_fd[1], &key->userstack, ip) != 0) {
		printf("---;");
	} else {
		for (i = PERF_MAX_STACK_DEPTH - 1; i >= 0; i--)
			print_addr(ip[i]);
	}
	printf("\n");

	if (key->kernstack == -EEXIST && !warned) {
		printf("stackmap collisions seen. Consider increasing size\n");
		warned = true;
	} else if ((int)key->kernstack < 0 && (int)key->userstack < 0) {
		printf("err stackid %d %d\n", key->kernstack, key->userstack);
	}
}

static void int_exit(int sig)
{
	kill(0, SIGKILL);
	exit(0);
}

static void print_stacks(void)
{
	struct key_t key = {}, next_key;
	__u64 value;
	__u32 stackid = 0, next_id;
	int fd = map_fd[0], stack_map = map_fd[1];

	sys_read_seen = sys_write_seen = false;
	while (bpf_map_get_next_key(fd, &key, &next_key) == 0) {
		bpf_map_lookup_elem(fd, &next_key, &value);
		print_stack(&next_key, value);
		bpf_map_delete_elem(fd, &next_key);
		key = next_key;
	}

	if (!sys_read_seen || !sys_write_seen) {
		printf("BUG kernel stack doesn't contain sys_read() and sys_write()\n");
		int_exit(0);
	}

	/* clear stack map */
	while (bpf_map_get_next_key(stack_map, &stackid, &next_id) == 0) {
		bpf_map_delete_elem(stack_map, &next_id);
		stackid = next_id;
	}
}

static void test_perf_event_all_cpu(struct perf_event_attr *attr)
{
	int nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	int *pmu_fd = malloc(nr_cpus * sizeof(int));
	int i;

	/* open perf_event on all cpus */
	for (i = 0; i < nr_cpus; i++) {
		pmu_fd[i] = sys_perf_event_open(attr, -1, i, -1, 0);
		if (pmu_fd[i] < 0) {
			printf("sys_perf_event_open failed\n");
			goto all_cpu_err;
		}
		assert(ioctl(pmu_fd[i], PERF_EVENT_IOC_SET_BPF, prog_fd[0]) == 0);
		assert(ioctl(pmu_fd[i], PERF_EVENT_IOC_ENABLE, 0) == 0);
	}
	system("dd if=/dev/zero of=/dev/null count=5000k");
	print_stacks();
all_cpu_err:
	for (i--; i >= 0; i--)
		close(pmu_fd[i]);
	free(pmu_fd);
}

static void test_perf_event_task(struct perf_event_attr *attr)
{
	int pmu_fd;

	/* open task bound event */
	pmu_fd = sys_perf_event_open(attr, 0, -1, -1, 0);
	if (pmu_fd < 0) {
		printf("sys_perf_event_open failed\n");
		return;
	}
	assert(ioctl(pmu_fd, PERF_EVENT_IOC_SET_BPF, prog_fd[0]) == 0);
	assert(ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0) == 0);
	system("dd if=/dev/zero of=/dev/null count=5000k");
	print_stacks();
	close(pmu_fd);
}

static void test_bpf_perf_event(void)
{
	struct perf_event_attr attr_type_hw = {
		.sample_freq = SAMPLE_FREQ,
		.freq = 1,
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.inherit = 1,
	};
	struct perf_event_attr attr_type_sw = {
		.sample_freq = SAMPLE_FREQ,
		.freq = 1,
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
		.inherit = 1,
	};

	test_perf_event_all_cpu(&attr_type_hw);
	test_perf_event_task(&attr_type_hw);
	test_perf_event_all_cpu(&attr_type_sw);
	test_perf_event_task(&attr_type_sw);
}


int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	setrlimit(RLIMIT_MEMLOCK, &r);

	signal(SIGINT, int_exit);

	if (load_kallsyms()) {
		printf("failed to process /proc/kallsyms\n");
		return 1;
	}

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 2;
	}

	if (fork() == 0) {
		read_trace_pipe();
		return 0;
	}
	test_bpf_perf_event();

	int_exit(0);
	return 0;
}
