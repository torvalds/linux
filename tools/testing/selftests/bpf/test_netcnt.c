// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"
#include "bpf_rlimit.h"
#include "netcnt_common.h"

#define BPF_PROG "./netcnt_prog.o"
#define TEST_CGROUP "/test-network-counters/"

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

int main(int argc, char **argv)
{
	struct percpu_net_cnt *percpu_netcnt;
	struct bpf_cgroup_storage_key key;
	int map_fd, percpu_map_fd;
	int error = EXIT_FAILURE;
	struct net_cnt netcnt;
	struct bpf_object *obj;
	int prog_fd, cgroup_fd;
	unsigned long packets;
	unsigned long bytes;
	int cpu, nproc;
	__u32 prog_cnt;

	nproc = get_nprocs_conf();
	percpu_netcnt = malloc(sizeof(*percpu_netcnt) * nproc);
	if (!percpu_netcnt) {
		printf("Not enough memory for per-cpu area (%d cpus)\n", nproc);
		goto err;
	}

	if (bpf_prog_load(BPF_PROG, BPF_PROG_TYPE_CGROUP_SKB,
			  &obj, &prog_fd)) {
		printf("Failed to load bpf program\n");
		goto out;
	}

	if (setup_cgroup_environment()) {
		printf("Failed to load bpf program\n");
		goto err;
	}

	/* Create a cgroup, get fd, and join it */
	cgroup_fd = create_and_get_cgroup(TEST_CGROUP);
	if (!cgroup_fd) {
		printf("Failed to create test cgroup\n");
		goto err;
	}

	if (join_cgroup(TEST_CGROUP)) {
		printf("Failed to join cgroup\n");
		goto err;
	}

	/* Attach bpf program */
	if (bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_INET_EGRESS, 0)) {
		printf("Failed to attach bpf program");
		goto err;
	}

	if (system("which ping6 &>/dev/null") == 0)
		assert(!system("ping6 localhost -c 10000 -f -q > /dev/null"));
	else
		assert(!system("ping -6 localhost -c 10000 -f -q > /dev/null"));

	if (bpf_prog_query(cgroup_fd, BPF_CGROUP_INET_EGRESS, 0, NULL, NULL,
			   &prog_cnt)) {
		printf("Failed to query attached programs");
		goto err;
	}

	map_fd = bpf_find_map(__func__, obj, "netcnt");
	if (map_fd < 0) {
		printf("Failed to find bpf map with net counters");
		goto err;
	}

	percpu_map_fd = bpf_find_map(__func__, obj, "percpu_netcnt");
	if (percpu_map_fd < 0) {
		printf("Failed to find bpf map with percpu net counters");
		goto err;
	}

	if (bpf_map_get_next_key(map_fd, NULL, &key)) {
		printf("Failed to get key in cgroup storage\n");
		goto err;
	}

	if (bpf_map_lookup_elem(map_fd, &key, &netcnt)) {
		printf("Failed to lookup cgroup storage\n");
		goto err;
	}

	if (bpf_map_lookup_elem(percpu_map_fd, &key, &percpu_netcnt[0])) {
		printf("Failed to lookup percpu cgroup storage\n");
		goto err;
	}

	/* Some packets can be still in per-cpu cache, but not more than
	 * MAX_PERCPU_PACKETS.
	 */
	packets = netcnt.packets;
	bytes = netcnt.bytes;
	for (cpu = 0; cpu < nproc; cpu++) {
		if (percpu_netcnt[cpu].packets > MAX_PERCPU_PACKETS) {
			printf("Unexpected percpu value: %llu\n",
			       percpu_netcnt[cpu].packets);
			goto err;
		}

		packets += percpu_netcnt[cpu].packets;
		bytes += percpu_netcnt[cpu].bytes;
	}

	/* No packets should be lost */
	if (packets != 10000) {
		printf("Unexpected packet count: %lu\n", packets);
		goto err;
	}

	/* Let's check that bytes counter matches the number of packets
	 * multiplied by the size of ipv6 ICMP packet.
	 */
	if (bytes != packets * 104) {
		printf("Unexpected bytes count: %lu\n", bytes);
		goto err;
	}

	error = 0;
	printf("test_netcnt:PASS\n");

err:
	cleanup_cgroup_environment();
	free(percpu_netcnt);

out:
	return error;
}
