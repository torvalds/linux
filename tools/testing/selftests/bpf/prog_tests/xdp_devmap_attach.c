// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/bpf.h>
#include <linux/if_link.h>
#include <test_progs.h>

#include "test_xdp_devmap_helpers.skel.h"
#include "test_xdp_with_devmap_frags_helpers.skel.h"
#include "test_xdp_with_devmap_helpers.skel.h"

#define IFINDEX_LO 1

static void test_xdp_with_devmap_helpers(void)
{
	struct test_xdp_with_devmap_helpers *skel;
	struct bpf_prog_info info = {};
	struct bpf_devmap_val val = {
		.ifindex = IFINDEX_LO,
	};
	__u32 len = sizeof(info);
	int err, dm_fd, map_fd;
	__u32 idx = 0;


	skel = test_xdp_with_devmap_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_with_devmap_helpers__open_and_load"))
		return;

	dm_fd = bpf_program__fd(skel->progs.xdp_redir_prog);
	err = bpf_xdp_attach(IFINDEX_LO, dm_fd, XDP_FLAGS_SKB_MODE, NULL);
	if (!ASSERT_OK(err, "Generic attach of program with 8-byte devmap"))
		goto out_close;

	err = bpf_xdp_detach(IFINDEX_LO, XDP_FLAGS_SKB_MODE, NULL);
	ASSERT_OK(err, "XDP program detach");

	dm_fd = bpf_program__fd(skel->progs.xdp_dummy_dm);
	map_fd = bpf_map__fd(skel->maps.dm_ports);
	err = bpf_obj_get_info_by_fd(dm_fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_obj_get_info_by_fd"))
		goto out_close;

	val.bpf_prog.fd = dm_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_OK(err, "Add program to devmap entry");

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	ASSERT_OK(err, "Read devmap entry");
	ASSERT_EQ(info.id, val.bpf_prog.id, "Match program id to devmap entry prog_id");

	/* can not attach BPF_XDP_DEVMAP program to a device */
	err = bpf_xdp_attach(IFINDEX_LO, dm_fd, XDP_FLAGS_SKB_MODE, NULL);
	if (!ASSERT_NEQ(err, 0, "Attach of BPF_XDP_DEVMAP program"))
		bpf_xdp_detach(IFINDEX_LO, XDP_FLAGS_SKB_MODE, NULL);

	val.ifindex = 1;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_prog);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add non-BPF_XDP_DEVMAP program to devmap entry");

	/* Try to attach BPF_XDP program with frags to devmap when we have
	 * already loaded a BPF_XDP program on the map
	 */
	idx = 1;
	val.ifindex = 1;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_dm_frags);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add BPF_XDP program with frags to devmap entry");

out_close:
	test_xdp_with_devmap_helpers__destroy(skel);
}

static void test_neg_xdp_devmap_helpers(void)
{
	struct test_xdp_devmap_helpers *skel;

	skel = test_xdp_devmap_helpers__open_and_load();
	if (!ASSERT_EQ(skel, NULL,
		    "Load of XDP program accessing egress ifindex without attach type")) {
		test_xdp_devmap_helpers__destroy(skel);
	}
}

void test_xdp_with_devmap_frags_helpers(void)
{
	struct test_xdp_with_devmap_frags_helpers *skel;
	struct bpf_prog_info info = {};
	struct bpf_devmap_val val = {
		.ifindex = IFINDEX_LO,
	};
	__u32 len = sizeof(info);
	int err, dm_fd_frags, map_fd;
	__u32 idx = 0;

	skel = test_xdp_with_devmap_frags_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_with_devmap_helpers__open_and_load"))
		return;

	dm_fd_frags = bpf_program__fd(skel->progs.xdp_dummy_dm_frags);
	map_fd = bpf_map__fd(skel->maps.dm_ports);
	err = bpf_obj_get_info_by_fd(dm_fd_frags, &info, &len);
	if (!ASSERT_OK(err, "bpf_obj_get_info_by_fd"))
		goto out_close;

	val.bpf_prog.fd = dm_fd_frags;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_OK(err, "Add frags program to devmap entry");

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	ASSERT_OK(err, "Read devmap entry");
	ASSERT_EQ(info.id, val.bpf_prog.id,
		  "Match program id to devmap entry prog_id");

	/* Try to attach BPF_XDP program to devmap when we have
	 * already loaded a BPF_XDP program with frags on the map
	 */
	idx = 1;
	val.ifindex = 1;
	val.bpf_prog.fd = bpf_program__fd(skel->progs.xdp_dummy_dm);
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_NEQ(err, 0, "Add BPF_XDP program to devmap entry");

out_close:
	test_xdp_with_devmap_frags_helpers__destroy(skel);
}

void serial_test_xdp_devmap_attach(void)
{
	if (test__start_subtest("DEVMAP with programs in entries"))
		test_xdp_with_devmap_helpers();

	if (test__start_subtest("DEVMAP with frags programs in entries"))
		test_xdp_with_devmap_frags_helpers();

	if (test__start_subtest("Verifier check of DEVMAP programs"))
		test_neg_xdp_devmap_helpers();
}
