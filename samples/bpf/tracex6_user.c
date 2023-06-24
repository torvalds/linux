// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "perf-sys.h"

#define SAMPLE_PERIOD  0x7fffffffffffffffULL

/* counters, values, values2 */
static int map_fd[3];

static void check_on_cpu(int cpu, struct perf_event_attr *attr)
{
	struct bpf_perf_event_value value2;
	int pmu_fd, error = 0;
	cpu_set_t set;
	__u64 value;

	/* Move to target CPU */
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	assert(sched_setaffinity(0, sizeof(set), &set) == 0);
	/* Open perf event and attach to the perf_event_array */
	pmu_fd = sys_perf_event_open(attr, -1/*pid*/, cpu/*cpu*/, -1/*group_fd*/, 0);
	if (pmu_fd < 0) {
		fprintf(stderr, "sys_perf_event_open failed on CPU %d\n", cpu);
		error = 1;
		goto on_exit;
	}
	assert(bpf_map_update_elem(map_fd[0], &cpu, &pmu_fd, BPF_ANY) == 0);
	assert(ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0) == 0);
	/* Trigger the kprobe */
	bpf_map_get_next_key(map_fd[1], &cpu, NULL);
	/* Check the value */
	if (bpf_map_lookup_elem(map_fd[1], &cpu, &value)) {
		fprintf(stderr, "Value missing for CPU %d\n", cpu);
		error = 1;
		goto on_exit;
	} else {
		fprintf(stderr, "CPU %d: %llu\n", cpu, value);
	}
	/* The above bpf_map_lookup_elem should trigger the second kprobe */
	if (bpf_map_lookup_elem(map_fd[2], &cpu, &value2)) {
		fprintf(stderr, "Value2 missing for CPU %d\n", cpu);
		error = 1;
		goto on_exit;
	} else {
		fprintf(stderr, "CPU %d: counter: %llu, enabled: %llu, running: %llu\n", cpu,
			value2.counter, value2.enabled, value2.running);
	}

on_exit:
	assert(bpf_map_delete_elem(map_fd[0], &cpu) == 0 || error);
	assert(ioctl(pmu_fd, PERF_EVENT_IOC_DISABLE, 0) == 0 || error);
	assert(close(pmu_fd) == 0 || error);
	assert(bpf_map_delete_elem(map_fd[1], &cpu) == 0 || error);
	exit(error);
}

static void test_perf_event_array(struct perf_event_attr *attr,
				  const char *name)
{
	int i, status, nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	pid_t pid[nr_cpus];
	int err = 0;

	printf("Test reading %s counters\n", name);

	for (i = 0; i < nr_cpus; i++) {
		pid[i] = fork();
		assert(pid[i] >= 0);
		if (pid[i] == 0) {
			check_on_cpu(i, attr);
			exit(1);
		}
	}

	for (i = 0; i < nr_cpus; i++) {
		assert(waitpid(pid[i], &status, 0) == pid[i]);
		err |= status;
	}

	if (err)
		printf("Test: %s FAILED\n", name);
}

static void test_bpf_perf_event(void)
{
	struct perf_event_attr attr_cycles = {
		.freq = 0,
		.sample_period = SAMPLE_PERIOD,
		.inherit = 0,
		.type = PERF_TYPE_HARDWARE,
		.read_format = 0,
		.sample_type = 0,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	struct perf_event_attr attr_clock = {
		.freq = 0,
		.sample_period = SAMPLE_PERIOD,
		.inherit = 0,
		.type = PERF_TYPE_SOFTWARE,
		.read_format = 0,
		.sample_type = 0,
		.config = PERF_COUNT_SW_CPU_CLOCK,
	};
	struct perf_event_attr attr_raw = {
		.freq = 0,
		.sample_period = SAMPLE_PERIOD,
		.inherit = 0,
		.type = PERF_TYPE_RAW,
		.read_format = 0,
		.sample_type = 0,
		/* Intel Instruction Retired */
		.config = 0xc0,
	};
	struct perf_event_attr attr_l1d_load = {
		.freq = 0,
		.sample_period = SAMPLE_PERIOD,
		.inherit = 0,
		.type = PERF_TYPE_HW_CACHE,
		.read_format = 0,
		.sample_type = 0,
		.config =
			PERF_COUNT_HW_CACHE_L1D |
			(PERF_COUNT_HW_CACHE_OP_READ << 8) |
			(PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16),
	};
	struct perf_event_attr attr_llc_miss = {
		.freq = 0,
		.sample_period = SAMPLE_PERIOD,
		.inherit = 0,
		.type = PERF_TYPE_HW_CACHE,
		.read_format = 0,
		.sample_type = 0,
		.config =
			PERF_COUNT_HW_CACHE_LL |
			(PERF_COUNT_HW_CACHE_OP_READ << 8) |
			(PERF_COUNT_HW_CACHE_RESULT_MISS << 16),
	};
	struct perf_event_attr attr_msr_tsc = {
		.freq = 0,
		.sample_period = 0,
		.inherit = 0,
		/* From /sys/bus/event_source/devices/msr/ */
		.type = 7,
		.read_format = 0,
		.sample_type = 0,
		.config = 0,
	};

	test_perf_event_array(&attr_cycles, "HARDWARE-cycles");
	test_perf_event_array(&attr_clock, "SOFTWARE-clock");
	test_perf_event_array(&attr_raw, "RAW-instruction-retired");
	test_perf_event_array(&attr_l1d_load, "HW_CACHE-L1D-load");

	/* below tests may fail in qemu */
	test_perf_event_array(&attr_llc_miss, "HW_CACHE-LLC-miss");
	test_perf_event_array(&attr_msr_tsc, "Dynamic-msr-tsc");
}

int main(int argc, char **argv)
{
	struct bpf_link *links[2];
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];
	int i = 0;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		return 0;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	map_fd[0] = bpf_object__find_map_fd_by_name(obj, "counters");
	map_fd[1] = bpf_object__find_map_fd_by_name(obj, "values");
	map_fd[2] = bpf_object__find_map_fd_by_name(obj, "values2");
	if (map_fd[0] < 0 || map_fd[1] < 0 || map_fd[2] < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	bpf_object__for_each_program(prog, obj) {
		links[i] = bpf_program__attach(prog);
		if (libbpf_get_error(links[i])) {
			fprintf(stderr, "ERROR: bpf_program__attach failed\n");
			links[i] = NULL;
			goto cleanup;
		}
		i++;
	}

	test_bpf_perf_event();

cleanup:
	for (i--; i >= 0; i--)
		bpf_link__destroy(links[i]);

	bpf_object__close(obj);
	return 0;
}
