// SPDX-License-Identifier: GPL-2.0

#include <sys/sysinfo.h>
#include <test_progs.h>
#include "network_helpers.h"
#include "netcnt_prog.skel.h"
#include "netcnt_common.h"

#define CG_NAME "/netcnt"

void serial_test_netcnt(void)
{
	union percpu_net_cnt *percpu_netcnt = NULL;
	struct bpf_cgroup_storage_key key;
	int map_fd, percpu_map_fd;
	struct netcnt_prog *skel;
	unsigned long packets;
	union net_cnt netcnt;
	unsigned long bytes;
	int cpu, nproc;
	int cg_fd = -1;
	char cmd[128];

	skel = netcnt_prog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "netcnt_prog__open_and_load"))
		return;

	nproc = bpf_num_possible_cpus();
	percpu_netcnt = malloc(sizeof(*percpu_netcnt) * nproc);
	if (!ASSERT_OK_PTR(percpu_netcnt, "malloc(percpu_netcnt)"))
		goto err;

	cg_fd = test__join_cgroup(CG_NAME);
	if (!ASSERT_GE(cg_fd, 0, "test__join_cgroup"))
		goto err;

	skel->links.bpf_nextcnt = bpf_program__attach_cgroup(skel->progs.bpf_nextcnt, cg_fd);
	if (!ASSERT_OK_PTR(skel->links.bpf_nextcnt,
			   "attach_cgroup(bpf_nextcnt)"))
		goto err;

	snprintf(cmd, sizeof(cmd), "%s ::1 -A -c 10000 -q > /dev/null", ping_command(AF_INET6));
	ASSERT_OK(system(cmd), cmd);

	map_fd = bpf_map__fd(skel->maps.netcnt);
	if (!ASSERT_OK(bpf_map_get_next_key(map_fd, NULL, &key), "bpf_map_get_next_key"))
		goto err;

	if (!ASSERT_OK(bpf_map_lookup_elem(map_fd, &key, &netcnt), "bpf_map_lookup_elem(netcnt)"))
		goto err;

	percpu_map_fd = bpf_map__fd(skel->maps.percpu_netcnt);
	if (!ASSERT_OK(bpf_map_lookup_elem(percpu_map_fd, &key, &percpu_netcnt[0]),
		       "bpf_map_lookup_elem(percpu_netcnt)"))
		goto err;

	/* Some packets can be still in per-cpu cache, but not more than
	 * MAX_PERCPU_PACKETS.
	 */
	packets = netcnt.packets;
	bytes = netcnt.bytes;
	for (cpu = 0; cpu < nproc; cpu++) {
		ASSERT_LE(percpu_netcnt[cpu].packets, MAX_PERCPU_PACKETS, "MAX_PERCPU_PACKETS");

		packets += percpu_netcnt[cpu].packets;
		bytes += percpu_netcnt[cpu].bytes;
	}

	/* No packets should be lost */
	ASSERT_GE(packets, 10000, "packets");

	/* Let's check that bytes counter matches the number of packets
	 * multiplied by the size of ipv6 ICMP packet.
	 */
	ASSERT_GE(bytes, packets * 104, "bytes");

err:
	if (cg_fd != -1)
		close(cg_fd);
	free(percpu_netcnt);
	netcnt_prog__destroy(skel);
}
