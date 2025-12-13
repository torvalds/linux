// SPDX-License-Identifier: GPL-2.0
#include <arpa/inet.h>
#include <uapi/linux/bpf.h>
#include <linux/if_link.h>
#include <network_helpers.h>
#include <net/if.h>
#include <test_progs.h>

#include "test_xdp_devmap_helpers.skel.h"
#include "test_xdp_devmap_tailcall.skel.h"
#include "test_xdp_with_devmap_frags_helpers.skel.h"
#include "test_xdp_with_devmap_helpers.skel.h"

#define IFINDEX_LO 1
#define TEST_NS "devmap_attach_ns"

static void test_xdp_with_devmap_helpers(void)
{
	struct test_xdp_with_devmap_helpers *skel = NULL;
	struct bpf_prog_info info = {};
	struct bpf_devmap_val val = {
		.ifindex = IFINDEX_LO,
	};
	__u32 len = sizeof(info);
	int err, dm_fd, dm_fd_redir, map_fd;
	struct nstoken *nstoken = NULL;
	char data[ETH_HLEN] = {};
	__u32 idx = 0;

	SYS(out_close, "ip netns add %s", TEST_NS);
	nstoken = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto out_close;
	SYS(out_close, "ip link set dev lo up");

	skel = test_xdp_with_devmap_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_with_devmap_helpers__open_and_load"))
		goto out_close;

	dm_fd_redir = bpf_program__fd(skel->progs.xdp_redir_prog);
	err = bpf_xdp_attach(IFINDEX_LO, dm_fd_redir, XDP_FLAGS_SKB_MODE, NULL);
	if (!ASSERT_OK(err, "Generic attach of program with 8-byte devmap"))
		goto out_close;

	dm_fd = bpf_program__fd(skel->progs.xdp_dummy_dm);
	map_fd = bpf_map__fd(skel->maps.dm_ports);
	err = bpf_prog_get_info_by_fd(dm_fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd"))
		goto out_close;

	val.bpf_prog.fd = dm_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_OK(err, "Add program to devmap entry");

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	ASSERT_OK(err, "Read devmap entry");
	ASSERT_EQ(info.id, val.bpf_prog.id, "Match program id to devmap entry prog_id");

	/* send a packet to trigger any potential bugs in there */
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			    .data_in = &data,
			    .data_size_in = sizeof(data),
			    .flags = BPF_F_TEST_XDP_LIVE_FRAMES,
			    .repeat = 1,
		);
	err = bpf_prog_test_run_opts(dm_fd_redir, &opts);
	ASSERT_OK(err, "XDP test run");

	/* wait for the packets to be flushed */
	kern_sync_rcu();

	err = bpf_xdp_detach(IFINDEX_LO, XDP_FLAGS_SKB_MODE, NULL);
	ASSERT_OK(err, "XDP program detach");

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
	close_netns(nstoken);
	SYS_NOFAIL("ip netns del %s", TEST_NS);
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

static void test_xdp_devmap_tailcall(enum bpf_attach_type prog_dev,
				     enum bpf_attach_type prog_tail,
				     bool expect_reject)
{
	struct test_xdp_devmap_tailcall *skel;
	int err;

	skel = test_xdp_devmap_tailcall__open();
	if (!ASSERT_OK_PTR(skel, "test_xdp_devmap_tailcall__open"))
		return;

	bpf_program__set_expected_attach_type(skel->progs.xdp_devmap, prog_dev);
	bpf_program__set_expected_attach_type(skel->progs.xdp_entry, prog_tail);

	err = test_xdp_devmap_tailcall__load(skel);
	if (expect_reject)
		ASSERT_ERR(err, "test_xdp_devmap_tailcall__load");
	else
		ASSERT_OK(err, "test_xdp_devmap_tailcall__load");

	test_xdp_devmap_tailcall__destroy(skel);
}

static void test_xdp_with_devmap_frags_helpers(void)
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
	err = bpf_prog_get_info_by_fd(dm_fd_frags, &info, &len);
	if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd"))
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

static void test_xdp_with_devmap_helpers_veth(void)
{
	struct test_xdp_with_devmap_helpers *skel = NULL;
	struct bpf_prog_info info = {};
	struct bpf_devmap_val val = {};
	struct nstoken *nstoken = NULL;
	__u32 len = sizeof(info);
	int err, dm_fd, dm_fd_redir, map_fd, ifindex_dst;
	char data[ETH_HLEN] = {};
	__u32 idx = 0;

	SYS(out_close, "ip netns add %s", TEST_NS);
	nstoken = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto out_close;

	SYS(out_close, "ip link add veth_src type veth peer name veth_dst");
	SYS(out_close, "ip link set dev veth_src up");
	SYS(out_close, "ip link set dev veth_dst up");

	val.ifindex = if_nametoindex("veth_src");
	ifindex_dst = if_nametoindex("veth_dst");
	if (!ASSERT_NEQ(val.ifindex, 0, "val.ifindex") ||
	    !ASSERT_NEQ(ifindex_dst, 0, "ifindex_dst"))
		goto out_close;

	skel = test_xdp_with_devmap_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_with_devmap_helpers__open_and_load"))
		goto out_close;

	dm_fd_redir = bpf_program__fd(skel->progs.xdp_redir_prog);
	err = bpf_xdp_attach(val.ifindex, dm_fd_redir, XDP_FLAGS_DRV_MODE, NULL);
	if (!ASSERT_OK(err, "Attach of program with 8-byte devmap"))
		goto out_close;

	dm_fd = bpf_program__fd(skel->progs.xdp_dummy_dm);
	map_fd = bpf_map__fd(skel->maps.dm_ports);
	err = bpf_prog_get_info_by_fd(dm_fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd"))
		goto out_close;

	val.bpf_prog.fd = dm_fd;
	err = bpf_map_update_elem(map_fd, &idx, &val, 0);
	ASSERT_OK(err, "Add program to devmap entry");

	err = bpf_map_lookup_elem(map_fd, &idx, &val);
	ASSERT_OK(err, "Read devmap entry");
	ASSERT_EQ(info.id, val.bpf_prog.id, "Match program id to devmap entry prog_id");

	/* attach dummy to other side to enable reception */
	dm_fd = bpf_program__fd(skel->progs.xdp_dummy_prog);
	err = bpf_xdp_attach(ifindex_dst, dm_fd, XDP_FLAGS_DRV_MODE, NULL);
	if (!ASSERT_OK(err, "Attach of dummy XDP"))
		goto out_close;

	/* send a packet to trigger any potential bugs in there */
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			    .data_in = &data,
			    .data_size_in = sizeof(data),
			    .flags = BPF_F_TEST_XDP_LIVE_FRAMES,
			    .repeat = 1,
		);
	err = bpf_prog_test_run_opts(dm_fd_redir, &opts);
	ASSERT_OK(err, "XDP test run");

	/* wait for the packets to be flushed */
	kern_sync_rcu();

	err = bpf_xdp_detach(val.ifindex, XDP_FLAGS_DRV_MODE, NULL);
	ASSERT_OK(err, "XDP program detach");

	err = bpf_xdp_detach(ifindex_dst, XDP_FLAGS_DRV_MODE, NULL);
	ASSERT_OK(err, "XDP program detach");

out_close:
	close_netns(nstoken);
	SYS_NOFAIL("ip netns del %s", TEST_NS);
	test_xdp_with_devmap_helpers__destroy(skel);
}

void serial_test_xdp_devmap_attach(void)
{
	if (test__start_subtest("DEVMAP with programs in entries"))
		test_xdp_with_devmap_helpers();

	if (test__start_subtest("DEVMAP with frags programs in entries"))
		test_xdp_with_devmap_frags_helpers();

	if (test__start_subtest("Verifier check of DEVMAP programs")) {
		test_neg_xdp_devmap_helpers();
		test_xdp_devmap_tailcall(BPF_XDP_DEVMAP, BPF_XDP_DEVMAP, false);
		test_xdp_devmap_tailcall(0, 0, true);
		test_xdp_devmap_tailcall(BPF_XDP_DEVMAP, 0, true);
		test_xdp_devmap_tailcall(0, BPF_XDP_DEVMAP, true);
	}

	if (test__start_subtest("DEVMAP with programs in entries on veth"))
		test_xdp_with_devmap_helpers_veth();
}
