// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include <linux/bpf.h>
#include "bpf/libbpf_internal.h"
#include "test_raw_tp_test_run.skel.h"

void test_raw_tp_test_run(void)
{
	int comm_fd = -1, err, nr_online, i, prog_fd;
	__u64 args[2] = {0x1234ULL, 0x5678ULL};
	int expected_retval = 0x1234 + 0x5678;
	struct test_raw_tp_test_run *skel;
	char buf[] = "new_name";
	bool *online = NULL;
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.ctx_in = args,
		.ctx_size_in = sizeof(args),
		.flags = BPF_F_TEST_RUN_ON_CPU,
	);

	err = parse_cpu_mask_file("/sys/devices/system/cpu/online", &online,
				  &nr_online);
	if (!ASSERT_OK(err, "parse_cpu_mask_file"))
		return;

	skel = test_raw_tp_test_run__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	err = test_raw_tp_test_run__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	comm_fd = open("/proc/self/comm", O_WRONLY|O_TRUNC);
	if (!ASSERT_GE(comm_fd, 0, "open /proc/self/comm"))
		goto cleanup;

	err = write(comm_fd, buf, sizeof(buf));
	ASSERT_GE(err, 0, "task rename");

	ASSERT_NEQ(skel->bss->count, 0, "check_count");
	ASSERT_EQ(skel->data->on_cpu, 0xffffffff, "check_on_cpu");

	prog_fd = bpf_program__fd(skel->progs.rename);
	opts.ctx_in = args;
	opts.ctx_size_in = sizeof(__u64);

	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_NEQ(err, 0, "test_run should fail for too small ctx");

	opts.ctx_size_in = sizeof(args);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(opts.retval, expected_retval, "check_retval");

	for (i = 0; i < nr_online; i++) {
		if (!online[i])
			continue;

		opts.cpu = i;
		opts.retval = 0;
		err = bpf_prog_test_run_opts(prog_fd, &opts);
		ASSERT_OK(err, "test_run_opts");
		ASSERT_EQ(skel->data->on_cpu, i, "check_on_cpu");
		ASSERT_EQ(opts.retval, expected_retval, "check_retval");
	}

	/* invalid cpu ID should fail with ENXIO */
	opts.cpu = 0xffffffff;
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_EQ(errno, ENXIO, "test_run_opts should fail with ENXIO");
	ASSERT_ERR(err, "test_run_opts_fail");

	/* non-zero cpu w/o BPF_F_TEST_RUN_ON_CPU should fail with EINVAL */
	opts.cpu = 1;
	opts.flags = 0;
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_EQ(errno, EINVAL, "test_run_opts should fail with EINVAL");
	ASSERT_ERR(err, "test_run_opts_fail");

cleanup:
	close(comm_fd);
	test_raw_tp_test_run__destroy(skel);
	free(online);
}
