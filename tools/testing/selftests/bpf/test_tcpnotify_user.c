// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <pthread.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <asm/types.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>
#include <linux/bpf.h>
#include <sys/socket.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <sys/ioctl.h>
#include <linux/rtnetlink.h>
#include <signal.h>
#include <linux/perf_event.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#include "test_tcpnotify.h"
#include "trace_helpers.h"

#define SOCKET_BUFFER_SIZE (getpagesize() < 8192L ? getpagesize() : 8192L)

pthread_t tid;
int rx_callbacks;

static int dummyfn(void *data, int size)
{
	struct tcp_notifier *t = data;

	if (t->type != 0xde || t->subtype != 0xad ||
	    t->source != 0xbe || t->hash != 0xef)
		return 1;
	rx_callbacks++;
	return 0;
}

void tcp_notifier_poller(int fd)
{
	while (1)
		perf_event_poller(fd, dummyfn);
}

static void *poller_thread(void *arg)
{
	int fd = *(int *)arg;

	tcp_notifier_poller(fd);
	return arg;
}

int verify_result(const struct tcpnotify_globals *result)
{
	return (result->ncalls > 0 && result->ncalls == rx_callbacks ? 0 : 1);
}

static int bpf_find_map(const char *test, struct bpf_object *obj,
			const char *name)
{
	struct bpf_map *map;

	map = bpf_object__find_map_by_name(obj, name);
	if (!map) {
		printf("%s:FAIL:map '%s' not found\n", test, name);
		return -1;
	}
	return bpf_map__fd(map);
}

static int setup_bpf_perf_event(int mapfd)
{
	struct perf_event_attr attr = {
		.sample_type = PERF_SAMPLE_RAW,
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_BPF_OUTPUT,
	};
	int key = 0;
	int pmu_fd;

	pmu_fd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, 0);
	if (pmu_fd < 0)
		return pmu_fd;
	bpf_map_update_elem(mapfd, &key, &pmu_fd, BPF_ANY);

	ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0);
	return pmu_fd;
}

int main(int argc, char **argv)
{
	const char *file = "test_tcpnotify_kern.o";
	int prog_fd, map_fd, perf_event_fd;
	struct tcpnotify_globals g = {0};
	const char *cg_path = "/foo";
	int error = EXIT_FAILURE;
	struct bpf_object *obj;
	int cg_fd = -1;
	__u32 key = 0;
	int rv;
	char test_script[80];
	int pmu_fd;
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	if (setup_cgroup_environment())
		goto err;

	cg_fd = create_and_get_cgroup(cg_path);
	if (!cg_fd)
		goto err;

	if (join_cgroup(cg_path))
		goto err;

	if (bpf_prog_load(file, BPF_PROG_TYPE_SOCK_OPS, &obj, &prog_fd)) {
		printf("FAILED: load_bpf_file failed for: %s\n", file);
		goto err;
	}

	rv = bpf_prog_attach(prog_fd, cg_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (rv) {
		printf("FAILED: bpf_prog_attach: %d (%s)\n",
		       error, strerror(errno));
		goto err;
	}

	perf_event_fd = bpf_find_map(__func__, obj, "perf_event_map");
	if (perf_event_fd < 0)
		goto err;

	map_fd = bpf_find_map(__func__, obj, "global_map");
	if (map_fd < 0)
		goto err;

	pmu_fd = setup_bpf_perf_event(perf_event_fd);
	if (pmu_fd < 0 || perf_event_mmap(pmu_fd) < 0)
		goto err;

	pthread_create(&tid, NULL, poller_thread, (void *)&pmu_fd);

	sprintf(test_script,
		"/usr/sbin/iptables -A INPUT -p tcp --dport %d -j DROP",
		TESTPORT);
	system(test_script);

	sprintf(test_script,
		"/usr/bin/nc 127.0.0.1 %d < /etc/passwd > /dev/null 2>&1 ",
		TESTPORT);
	system(test_script);

	sprintf(test_script,
		"/usr/sbin/iptables -D INPUT -p tcp --dport %d -j DROP",
		TESTPORT);
	system(test_script);

	rv = bpf_map_lookup_elem(map_fd, &key, &g);
	if (rv != 0) {
		printf("FAILED: bpf_map_lookup_elem returns %d\n", rv);
		goto err;
	}

	sleep(10);

	if (verify_result(&g)) {
		printf("FAILED: Wrong stats Expected %d calls, got %d\n",
			g.ncalls, rx_callbacks);
		goto err;
	}

	printf("PASSED!\n");
	error = 0;
err:
	bpf_prog_detach(cg_fd, BPF_CGROUP_SOCK_OPS);
	close(cg_fd);
	cleanup_cgroup_environment();
	return error;
}
