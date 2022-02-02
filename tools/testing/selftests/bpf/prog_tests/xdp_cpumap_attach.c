// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/bpf.h>
#include <linux/if_link.h>
#include <test_progs.h>

#include "test_xdp_with_cpumap_frags_helpers.skel.h"
#include "test_xdp_with_cpumap_helpers.skel.h"

#define IFINDEX_LO	1

void test_xdp_with_cpumap_helpers(void)
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
	err = bpf_xdp_attach(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE, NULL);
	if (!ASSERT_OK(err, "Generic attach of program with 8-byte CPUMAP"))
		goto out_close;

	err = bpf_xdp_detach(IFINDEX_LO, XDP_FLAGS_SKB_MODE, NULL);
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
	err = bpf_xdp_attach(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE, NULL);
	if (!ASSERT_NEQ(err, 0, "Attach of BPF_XDP_CPUMAP program"))
		bpf_xdp_detach(IFINDEX_LO, XDP_FLAGS_SKB_MODE, NULL);

	val.qsize = 192;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_prog);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add non-BPF_XDP_CPUMAP program to cpumap entry");

	/* Try to attach BPF_XDP program with frags to cpumap when we have
	 * already loaded a BPF_XDP program on the map
	 */
	idx = 1;
	val.qsize = 192;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_cm_frags);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add BPF_XDP program with frags to cpumap entry");

out_close:
	test_xdp_with_cpumap_helpers__destroy(skel);
}

void test_xdp_with_cpumap_frags_helpers(void)
{
	struct test_xdp_with_cpumap_frags_helpers *skel;
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info);
	struct bpf_cpumap_val val = {
		.qsize = 192,
	};
	int err, frags_prog_fd, map_fd;
	__u32 idx = 0;

	skel = test_xdp_with_cpumap_frags_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_with_cpumap_helpers__open_and_load"))
		return;

	frags_prog_fd = bpf_program__fd(skel->progs.xdp_dummy_cm_frags);
	map_fd = bpf_map__fd(skel->maps.cpu_map);
	err = bpf_obj_get_info_by_fd(frags_prog_fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_obj_get_info_by_fd"))
		goto out_close;

	val.bpf_prog.fd = frags_prog_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_OK(err, "Add program to cpumap entry");

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	ASSERT_OK(err, "Read cpumap entry");
	ASSERT_EQ(info.id, val.bpf_prog.id,
		  "Match program id to cpumap entry prog_id");

	/* Try to attach BPF_XDP program to cpumap when we have
	 * already loaded a BPF_XDP program with frags on the map
	 */
	idx = 1;
	val.qsize = 192;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_cm);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add BPF_XDP program to cpumap entry");

out_close:
	test_xdp_with_cpumap_frags_helpers__destroy(skel);
}

void serial_test_xdp_cpumap_attach(void)
{
	if (test__start_subtest("CPUMAP with programs in entries"))
		test_xdp_with_cpumap_helpers();

	if (test__start_subtest("CPUMAP with frags programs in entries"))
		test_xdp_with_cpumap_frags_helpers();
}
