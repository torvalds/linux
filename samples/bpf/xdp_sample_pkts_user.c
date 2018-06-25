// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/perf_event.h>
#include <linux/bpf.h>
#include <net/if.h>
#include <errno.h>
#include <assert.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <libbpf.h>
#include <bpf/bpf.h>

#include "perf-sys.h"
#include "trace_helpers.h"

#define MAX_CPUS 128
static int pmu_fds[MAX_CPUS], if_idx;
static struct perf_event_mmap_page *headers[MAX_CPUS];
static char *if_name;

static int do_attach(int idx, int fd, const char *name)
{
	int err;

	err = bpf_set_link_xdp_fd(idx, fd, 0);
	if (err < 0)
		printf("ERROR: failed to attach program to %s\n", name);

	return err;
}

static int do_detach(int idx, const char *name)
{
	int err;

	err = bpf_set_link_xdp_fd(idx, -1, 0);
	if (err < 0)
		printf("ERROR: failed to detach program from %s\n", name);

	return err;
}

#define SAMPLE_SIZE 64

static int print_bpf_output(void *data, int size)
{
	struct {
		__u16 cookie;
		__u16 pkt_len;
		__u8  pkt_data[SAMPLE_SIZE];
	} __packed *e = data;
	int i;

	if (e->cookie != 0xdead) {
		printf("BUG cookie %x sized %d\n",
		       e->cookie, size);
		return LIBBPF_PERF_EVENT_ERROR;
	}

	printf("Pkt len: %-5d bytes. Ethernet hdr: ", e->pkt_len);
	for (i = 0; i < 14 && i < e->pkt_len; i++)
		printf("%02x ", e->pkt_data[i]);
	printf("\n");

	return LIBBPF_PERF_EVENT_CONT;
}

static void test_bpf_perf_event(int map_fd, int num)
{
	struct perf_event_attr attr = {
		.sample_type = PERF_SAMPLE_RAW,
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_BPF_OUTPUT,
		.wakeup_events = 1, /* get an fd notification for every event */
	};
	int i;

	for (i = 0; i < num; i++) {
		int key = i;

		pmu_fds[i] = sys_perf_event_open(&attr, -1/*pid*/, i/*cpu*/,
						 -1/*group_fd*/, 0);

		assert(pmu_fds[i] >= 0);
		assert(bpf_map_update_elem(map_fd, &key,
					   &pmu_fds[i], BPF_ANY) == 0);
		ioctl(pmu_fds[i], PERF_EVENT_IOC_ENABLE, 0);
	}
}

static void sig_handler(int signo)
{
	do_detach(if_idx, if_name);
	exit(0);
}

int main(int argc, char **argv)
{
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type	= BPF_PROG_TYPE_XDP,
	};
	struct bpf_object *obj;
	struct bpf_map *map;
	int prog_fd, map_fd;
	char filename[256];
	int ret, err, i;
	int numcpus;

	if (argc < 2) {
		printf("Usage: %s <ifname>\n", argv[0]);
		return 1;
	}

	numcpus = get_nprocs();
	if (numcpus > MAX_CPUS)
		numcpus = MAX_CPUS;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	prog_load_attr.file = filename;

	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &prog_fd))
		return 1;

	if (!prog_fd) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	map = bpf_map__next(NULL, obj);
	if (!map) {
		printf("finding a map in obj file failed\n");
		return 1;
	}
	map_fd = bpf_map__fd(map);

	if_idx = if_nametoindex(argv[1]);
	if (!if_idx)
		if_idx = strtoul(argv[1], NULL, 0);

	if (!if_idx) {
		fprintf(stderr, "Invalid ifname\n");
		return 1;
	}
	if_name = argv[1];
	err = do_attach(if_idx, prog_fd, argv[1]);
	if (err)
		return err;

	if (signal(SIGINT, sig_handler) ||
	    signal(SIGHUP, sig_handler) ||
	    signal(SIGTERM, sig_handler)) {
		perror("signal");
		return 1;
	}

	test_bpf_perf_event(map_fd, numcpus);

	for (i = 0; i < numcpus; i++)
		if (perf_event_mmap_header(pmu_fds[i], &headers[i]) < 0)
			return 1;

	ret = perf_event_poller_multi(pmu_fds, headers, numcpus,
				      print_bpf_output);
	kill(0, SIGINT);
	return ret;
}
