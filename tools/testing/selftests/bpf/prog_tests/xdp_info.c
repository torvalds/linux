// SPDX-License-Identifier: GPL-2.0
#include <linux/if_link.h>
#include <test_progs.h>

#define IFINDEX_LO 1

void serial_test_xdp_info(void)
{
	__u32 len = sizeof(struct bpf_prog_info), duration = 0, prog_id;
	const char *file = "./xdp_dummy.o";
	struct bpf_prog_info info = {};
	struct bpf_object *obj;
	int err, prog_fd;

	/* Get prog_id for XDP_ATTACHED_NONE mode */

	err = bpf_xdp_query_id(IFINDEX_LO, 0, &prog_id);
	if (CHECK(err, "get_xdp_none", "errno=%d\n", errno))
		return;
	if (CHECK(prog_id, "prog_id_none", "unexpected prog_id=%u\n", prog_id))
		return;

	err = bpf_xdp_query_id(IFINDEX_LO, XDP_FLAGS_SKB_MODE, &prog_id);
	if (CHECK(err, "get_xdp_none_skb", "errno=%d\n", errno))
		return;
	if (CHECK(prog_id, "prog_id_none_skb", "unexpected prog_id=%u\n",
		  prog_id))
		return;

	/* Setup prog */

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &len);
	if (CHECK(err, "get_prog_info", "errno=%d\n", errno))
		goto out_close;

	err = bpf_xdp_attach(IFINDEX_LO, prog_fd, XDP_FLAGS_SKB_MODE, NULL);
	if (CHECK(err, "set_xdp_skb", "errno=%d\n", errno))
		goto out_close;

	/* Get prog_id for single prog mode */

	err = bpf_xdp_query_id(IFINDEX_LO, 0, &prog_id);
	if (CHECK(err, "get_xdp", "errno=%d\n", errno))
		goto out;
	if (CHECK(prog_id != info.id, "prog_id", "prog_id not available\n"))
		goto out;

	err = bpf_xdp_query_id(IFINDEX_LO, XDP_FLAGS_SKB_MODE, &prog_id);
	if (CHECK(err, "get_xdp_skb", "errno=%d\n", errno))
		goto out;
	if (CHECK(prog_id != info.id, "prog_id_skb", "prog_id not available\n"))
		goto out;

	err = bpf_xdp_query_id(IFINDEX_LO, XDP_FLAGS_DRV_MODE, &prog_id);
	if (CHECK(err, "get_xdp_drv", "errno=%d\n", errno))
		goto out;
	if (CHECK(prog_id, "prog_id_drv", "unexpected prog_id=%u\n", prog_id))
		goto out;

out:
	bpf_xdp_detach(IFINDEX_LO, 0, NULL);
out_close:
	bpf_object__close(obj);
}
