// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include <linux/bpf.h>
#include "bpf/libbpf_internal.h"
#include "test_raw_tp_test_run.skel.h"

static int duration;

void test_raw_tp_test_run(void)
{
	struct bpf_prog_test_run_attr test_attr = {};
	int comm_fd = -1, err, nr_online, i, prog_fd;
	__u64 args[2] = {0x1234ULL, 0x5678ULL};
	int expected_retval = 0x1234 + 0x5678;
	struct test_raw_tp_test_run *skel;
	char buf[] = "new_name";
	bool *online = NULL;
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			    .ctx_in = args,
			    .ctx_size_in = sizeof(args),
			    .flags = BPF_F_TEST_RUN_ON_CPU,
		);

	err = parse_cpu_mask_file("/sys/devices/system/cpu/online", &online,
				  &nr_online);
	if (CHECK(err, "parse_cpu_mask_file", "err %d\n", err))
		return;

	skel = test_raw_tp_test_run__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		goto cleanup;

	err = test_raw_tp_test_run__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	comm_fd = open("/proc/self/comm", O_WRONLY|O_TRUNC);
	if (CHECK(comm_fd < 0, "open /proc/self/comm", "err %d\n", errno))
		goto cleanup;

	err = write(comm_fd, buf, sizeof(buf));
	CHECK(err < 0, "task rename", "err %d", errno);

	CHECK(skel->bss->count == 0, "check_count", "didn't increase\n");
	CHECK(skel->data->on_cpu != 0xffffffff, "check_on_cpu", "got wrong value\n");

	prog_fd = bpf_program__fd(skel->progs.rename);
	test_attr.prog_fd = prog_fd;
	test_attr.ctx_in = args;
	test_attr.ctx_size_in = sizeof(__u64);

	err = bpf_prog_test_run_xattr(&test_attr);
	CHECK(err == 0, "test_run", "should fail for too small ctx\n");

	test_attr.ctx_size_in = sizeof(args);
	err = bpf_prog_test_run_xattr(&test_attr);
	CHECK(err < 0, "test_run", "err %d\n", errno);
	CHECK(test_attr.retval != expected_retval, "check_retval",
	      "expect 0x%x, got 0x%x\n", expected_retval, test_attr.retval);

	for (i = 0; i < nr_online; i++) {
		if (!online[i])
			continue;

		opts.cpu = i;
		opts.retval = 0;
		err = bpf_prog_test_run_opts(prog_fd, &opts);
		CHECK(err < 0, "test_run_opts", "err %d\n", errno);
		CHECK(skel->data->on_cpu != i, "check_on_cpu",
		      "expect %d got %d\n", i, skel->data->on_cpu);
		CHECK(opts.retval != expected_retval,
		      "check_retval", "expect 0x%x, got 0x%x\n",
		      expected_retval, opts.retval);
	}

	/* invalid cpu ID should fail with ENXIO */
	opts.cpu = 0xffffffff;
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	CHECK(err != -1 || errno != ENXIO,
	      "test_run_opts_fail",
	      "should failed with ENXIO\n");

	/* non-zero cpu w/o BPF_F_TEST_RUN_ON_CPU should fail with EINVAL */
	opts.cpu = 1;
	opts.flags = 0;
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	CHECK(err != -1 || errno != EINVAL,
	      "test_run_opts_fail",
	      "should failed with EINVAL\n");

cleanup:
	close(comm_fd);
	test_raw_tp_test_run__destroy(skel);
	free(online);
}
