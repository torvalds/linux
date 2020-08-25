// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/bpf.h>
#include <linux/if_link.h>
#include <test_progs.h>

#include "test_xdp_with_cpumap_helpers.skel.h"

#define IFINDEX_LO	1

void test_xdp_with_cpumap_helpers(void)
{
	struct test_xdp_with_cpumap_helpers *skel;
	struct bpf_prog_info info = {};
	struct bpf_cpumap_val val = {
		.qsize = 192,
	};
	__u32 duration = 0, idx = 0;
	__u32 len = sizeof(info);
	int err, prog_fd, map_fd;

	skel = test_xdp_with_cpumap_helpers__open_and_load();
	if (CHECK_FAIL(!skel)) {
		perror("test_xdp_with_cpumap_helpers__open_and_load");
		return;
	}

	/* can not attach program with cpumaps that allow programs
	 * as xdp generic
	 */
	prog_fd = bpf_program__fd(skel->progs.xdp_redir_prog);
	err = bpf_set_link_xdp_fd(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE);
	CHECK(err == 0, "Generic attach of program with 8-byte CPUMAP",
	      "should have failed\n");

	prog_fd = bpf_program__fd(skel->progs.xdp_dummy_cm);
	map_fd = bpf_map__fd(skel->maps.cpu_map);
	err = bpf_obj_get_info_by_fd(prog_fd, &info, &len);
	if (CHECK_FAIL(err))
		goto out_close;

	val.bpf_prog.fd = prog_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	CHECK(err, "Add program to cpumap entry", "err %d errno %d\n",
	      err, errno);

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	CHECK(err, "Read cpumap entry", "err %d errno %d\n", err, errno);
	CHECK(info.id != val.bpf_prog.id, "Expected program id in cpumap entry",
	      "expected %u read %u\n", info.id, val.bpf_prog.id);

	/* can not attach BPF_XDP_CPUMAP program to a device */
	err = bpf_set_link_xdp_fd(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE);
	CHECK(err == 0, "Attach of BPF_XDP_CPUMAP program",
	      "should have failed\n");

	val.qsize = 192;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_prog);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	CHECK(err == 0, "Add non-BPF_XDP_CPUMAP program to cpumap entry",
	      "should have failed\n");

out_close:
	test_xdp_with_cpumap_helpers__destroy(skel);
}

void test_xdp_cpumap_attach(void)
{
	if (test__start_subtest("cpumap_with_progs"))
		test_xdp_with_cpumap_helpers();
}
