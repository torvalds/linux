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
#include <linux/err.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#include "test_tcpnotify.h"
#include "trace_helpers.h"
#include "testing_helpers.h"

#define SOCKET_BUFFER_SIZE (getpagesize() < 8192L ? getpagesize() : 8192L)

pthread_t tid;
int rx_callbacks;

static void dummyfn(void *ctx, int cpu, void *data, __u32 size)
{
	struct tcp_notifier *t = data;

	if (t->type != 0xde || t->subtype != 0xad ||
	    t->source != 0xbe || t->hash != 0xef)
		return;
	rx_callbacks++;
}

void tcp_notifier_poller(struct perf_buffer *pb)
{
	int err;

	while (1) {
		err = perf_buffer__poll(pb, 100);
		if (err < 0 && err != -EINTR) {
			printf("failed perf_buffer__poll: %d\n", err);
			return;
		}
	}
}

static void *poller_thread(void *arg)
{
	struct perf_buffer *pb = arg;

	tcp_notifier_poller(pb);
	return arg;
}

int verify_result(const struct tcpnotify_globals *result)
{
	return (result->ncalls > 0 && result->ncalls == rx_callbacks ? 0 : 1);
}

int main(int argc, char **argv)
{
	const char *file = "test_tcpnotify_kern.o";
	struct bpf_map *perf_map, *global_map;
	struct tcpnotify_globals g = {0};
	struct perf_buffer *pb = NULL;
	const char *cg_path = "/foo";
	int prog_fd, rv, cg_fd = -1;
	int error = EXIT_FAILURE;
	struct bpf_object *obj;
	char test_script[80];
	cpu_set_t cpuset;
	__u32 key = 0;

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

	cg_fd = cgroup_setup_and_join(cg_path);
	if (cg_fd < 0)
		goto err;

	if (bpf_prog_test_load(file, BPF_PROG_TYPE_SOCK_OPS, &obj, &prog_fd)) {
		printf("FAILED: load_bpf_file failed for: %s\n", file);
		goto err;
	}

	rv = bpf_prog_attach(prog_fd, cg_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (rv) {
		printf("FAILED: bpf_prog_attach: %d (%s)\n",
		       error, strerror(errno));
		goto err;
	}

	perf_map = bpf_object__find_map_by_name(obj, "perf_event_map");
	if (!perf_map) {
		printf("FAIL:map '%s' not found\n", "perf_event_map");
		goto err;
	}

	global_map = bpf_object__find_map_by_name(obj, "global_map");
	if (!global_map) {
		printf("FAIL:map '%s' not found\n", "global_map");
		return -1;
	}

	pb = perf_buffer__new(bpf_map__fd(perf_map), 8, dummyfn, NULL, NULL, NULL);
	if (!pb)
		goto err;

	pthread_create(&tid, NULL, poller_thread, pb);

	sprintf(test_script,
		"iptables -A INPUT -p tcp --dport %d -j DROP",
		TESTPORT);
	if (system(test_script)) {
		printf("FAILED: execute command: %s, err %d\n", test_script, -errno);
		goto err;
	}

	sprintf(test_script,
		"nc 127.0.0.1 %d < /etc/passwd > /dev/null 2>&1 ",
		TESTPORT);
	if (system(test_script))
		printf("execute command: %s, err %d\n", test_script, -errno);

	sprintf(test_script,
		"iptables -D INPUT -p tcp --dport %d -j DROP",
		TESTPORT);
	if (system(test_script)) {
		printf("FAILED: execute command: %s, err %d\n", test_script, -errno);
		goto err;
	}

	rv = bpf_map_lookup_elem(bpf_map__fd(global_map), &key, &g);
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
	perf_buffer__free(pb);
	return error;
}
