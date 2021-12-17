// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/bpf.h>
#include <linux/if_link.h>
#include <test_progs.h>

#include "test_xdp_with_cpumap_helpers.skel.h"

#define IFINDEX_LO	1

void serial_test_xdp_cpumap_attach(void)
{
	struct test_xdp_with_cpumap_helpers *skel;
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info);
	struct bpf_cpumap_val val = {
		.qsize = 192,
	};
	int err, prog_fd, map_fd;
	__u32 idx = 0;

	skel = test_xdp_with_cpumap_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_with_cpumap_helpers__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.xdp_redir_prog);
	err = bpf_set_link_xdp_fd(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE);
	if (!ASSERT_OK(err, "Generic attach of program with 8-byte CPUMAP"))
		goto out_close;

	err = bpf_set_link_xdp_fd(IFINDEX_LO, -1, XDP_FLAGS_SKB_MODE);
	ASSERT_OK(err, "XDP program detach");

	prog_fd = bpf_program__fd(skel->progs.xdp_dummy_cm);
	map_fd = bpf_map__fd(skel->maps.cpu_map);
	err = bpf_obj_get_info_by_fd(prog_fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_obj_get_info_by_fd"))
		goto out_close;

	val.bpf_prog.fd = prog_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_OK(err, "Add program to cpumap entry");

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	ASSERT_OK(err, "Read cpumap entry");
	ASSERT_EQ(info.id, val.bpf_prog.id, "Match program id to cpumap entry prog_id");

	/* can not attach BPF_XDP_CPUMAP program to a device */
	err = bpf_set_link_xdp_fd(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE);
	if (!ASSERT_NEQ(err, 0, "Attach of BPF_XDP_CPUMAP program"))
		bpf_set_link_xdp_fd(IFINDEX_LO, -1, XDP_FLAGS_SKB_MODE);

	val.qsize = 192;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_prog);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add non-BPF_XDP_CPUMAP program to cpumap entry");

out_close:
	test_xdp_with_cpumap_helpers__destroy(skel);
}
