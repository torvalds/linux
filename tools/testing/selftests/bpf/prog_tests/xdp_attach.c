// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_xdp_attach_fail.skel.h"

#define IFINDEX_LO 1
#define XDP_FLAGS_REPLACE		(1U << 4)

static void test_xdp_attach(const char *file)
{
	__u32 duration = 0, id1, id2, id0 = 0, len;
	struct bpf_object *obj1, *obj2, *obj3;
	struct bpf_prog_info info = {};
	int err, fd1, fd2, fd3;
	LIBBPF_OPTS(bpf_xdp_attach_opts, opts);

	len = sizeof(info);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj1, &fd1);
	if (CHECK_FAIL(err))
		return;
	err = bpf_prog_get_info_by_fd(fd1, &info, &len);
	if (CHECK_FAIL(err))
		goto out_1;
	id1 = info.id;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj2, &fd2);
	if (CHECK_FAIL(err))
		goto out_1;

	memset(&info, 0, sizeof(info));
	err = bpf_prog_get_info_by_fd(fd2, &info, &len);
	if (CHECK_FAIL(err))
		goto out_2;
	id2 = info.id;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj3, &fd3);
	if (CHECK_FAIL(err))
		goto out_2;

	err = bpf_xdp_attach(IFINDEX_LO, fd1, XDP_FLAGS_REPLACE, &opts);
	if (CHECK(err, "load_ok", "initial load failed"))
		goto out_close;

	err = bpf_xdp_query_id(IFINDEX_LO, 0, &id0);
	if (CHECK(err || id0 != id1, "id1_check",
		  "loaded prog id %u != id1 %u, err %d", id0, id1, err))
		goto out_close;

	err = bpf_xdp_attach(IFINDEX_LO, fd2, XDP_FLAGS_REPLACE, &opts);
	if (CHECK(!err, "load_fail", "load with expected id didn't fail"))
		goto out;

	opts.old_prog_fd = fd1;
	err = bpf_xdp_attach(IFINDEX_LO, fd2, 0, &opts);
	if (CHECK(err, "replace_ok", "replace valid old_fd failed"))
		goto out;
	err = bpf_xdp_query_id(IFINDEX_LO, 0, &id0);
	if (CHECK(err || id0 != id2, "id2_check",
		  "loaded prog id %u != id2 %u, err %d", id0, id2, err))
		goto out_close;

	err = bpf_xdp_attach(IFINDEX_LO, fd3, 0, &opts);
	if (CHECK(!err, "replace_fail", "replace invalid old_fd didn't fail"))
		goto out;

	err = bpf_xdp_detach(IFINDEX_LO, 0, &opts);
	if (CHECK(!err, "remove_fail", "remove invalid old_fd didn't fail"))
		goto out;

	opts.old_prog_fd = fd2;
	err = bpf_xdp_detach(IFINDEX_LO, 0, &opts);
	if (CHECK(err, "remove_ok", "remove valid old_fd failed"))
		goto out;

	err = bpf_xdp_query_id(IFINDEX_LO, 0, &id0);
	if (CHECK(err || id0 != 0, "unload_check",
		  "loaded prog id %u != 0, err %d", id0, err))
		goto out_close;
out:
	bpf_xdp_detach(IFINDEX_LO, 0, NULL);
out_close:
	bpf_object__close(obj3);
out_2:
	bpf_object__close(obj2);
out_1:
	bpf_object__close(obj1);
}

#define ERRMSG_LEN 64

struct xdp_errmsg {
	char msg[ERRMSG_LEN];
};

static void on_xdp_errmsg(void *ctx, int cpu, void *data, __u32 size)
{
	struct xdp_errmsg *ctx_errmg = ctx, *tp_errmsg = data;

	memcpy(&ctx_errmg->msg, &tp_errmsg->msg, ERRMSG_LEN);
}

static const char tgt_errmsg[] = "Invalid XDP flags for BPF link attachment";

static void test_xdp_attach_fail(const char *file)
{
	struct test_xdp_attach_fail *skel = NULL;
	struct xdp_errmsg errmsg = {};
	struct perf_buffer *pb = NULL;
	struct bpf_object *obj = NULL;
	int err, fd_xdp;

	LIBBPF_OPTS(bpf_link_create_opts, opts);

	skel = test_xdp_attach_fail__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_xdp_attach_fail__open_and_load"))
		goto out_close;

	err = test_xdp_attach_fail__attach(skel);
	if (!ASSERT_EQ(err, 0, "test_xdp_attach_fail__attach"))
		goto out_close;

	/* set up perf buffer */
	pb = perf_buffer__new(bpf_map__fd(skel->maps.xdp_errmsg_pb), 1,
			      on_xdp_errmsg, NULL, &errmsg, NULL);
	if (!ASSERT_OK_PTR(pb, "perf_buffer__new"))
		goto out_close;

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_XDP, &obj, &fd_xdp);
	if (!ASSERT_EQ(err, 0, "bpf_prog_test_load"))
		goto out_close;

	opts.flags = 0xFF; // invalid flags to fail to attach XDP prog
	err = bpf_link_create(fd_xdp, IFINDEX_LO, BPF_XDP, &opts);
	if (!ASSERT_EQ(err, -EINVAL, "bpf_link_create"))
		goto out_close;

	/* read perf buffer */
	err = perf_buffer__poll(pb, 100);
	if (!ASSERT_GT(err, -1, "perf_buffer__poll"))
		goto out_close;

	ASSERT_STRNEQ((const char *) errmsg.msg, tgt_errmsg,
		      42 /* strlen(tgt_errmsg) */, "check error message");

out_close:
	perf_buffer__free(pb);
	bpf_object__close(obj);
	test_xdp_attach_fail__destroy(skel);
}

void serial_test_xdp_attach(void)
{
	if (test__start_subtest("xdp_attach"))
		test_xdp_attach("./test_xdp.bpf.o");
	if (test__start_subtest("xdp_attach_dynptr"))
		test_xdp_attach("./test_xdp_dynptr.bpf.o");
	if (test__start_subtest("xdp_attach_failed"))
		test_xdp_attach_fail("./xdp_dummy.bpf.o");
}
