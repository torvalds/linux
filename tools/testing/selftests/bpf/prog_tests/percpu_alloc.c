// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "percpu_alloc_array.skel.h"
#include "percpu_alloc_cgrp_local_storage.skel.h"
#include "percpu_alloc_fail.skel.h"

static void test_array(void)
{
	struct percpu_alloc_array *skel;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	skel = percpu_alloc_array__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc_array__open"))
		return;

	bpf_program__set_autoload(skel->progs.test_array_map_1, true);
	bpf_program__set_autoload(skel->progs.test_array_map_2, true);
	bpf_program__set_autoload(skel->progs.test_array_map_3, true);
	bpf_program__set_autoload(skel->progs.test_array_map_4, true);

	skel->bss->my_pid = getpid();
	skel->rodata->nr_cpus = libbpf_num_possible_cpus();

	err = percpu_alloc_array__load(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__load"))
		goto out;

	err = percpu_alloc_array__attach(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__attach"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.test_array_map_1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run array_map 1-4");
	ASSERT_EQ(topts.retval, 0, "test_run array_map 1-4");
	ASSERT_EQ(skel->bss->cpu0_field_d, 2, "cpu0_field_d");
	ASSERT_EQ(skel->bss->sum_field_c, 1, "sum_field_c");
out:
	percpu_alloc_array__destroy(skel);
}

static void test_array_sleepable(void)
{
	struct percpu_alloc_array *skel;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	skel = percpu_alloc_array__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc__open"))
		return;

	bpf_program__set_autoload(skel->progs.test_array_map_10, true);

	skel->bss->my_pid = getpid();
	skel->rodata->nr_cpus = libbpf_num_possible_cpus();

	err = percpu_alloc_array__load(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__load"))
		goto out;

	err = percpu_alloc_array__attach(skel);
	if (!ASSERT_OK(err, "percpu_alloc_array__attach"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.test_array_map_10);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run array_map_10");
	ASSERT_EQ(topts.retval, 0, "test_run array_map_10");
	ASSERT_EQ(skel->bss->cpu0_field_d, 2, "cpu0_field_d");
	ASSERT_EQ(skel->bss->sum_field_c, 1, "sum_field_c");
out:
	percpu_alloc_array__destroy(skel);
}

static void test_cgrp_local_storage(void)
{
	struct percpu_alloc_cgrp_local_storage *skel;
	int err, cgroup_fd, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	cgroup_fd = test__join_cgroup("/percpu_alloc");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup /percpu_alloc"))
		return;

	skel = percpu_alloc_cgrp_local_storage__open();
	if (!ASSERT_OK_PTR(skel, "percpu_alloc_cgrp_local_storage__open"))
		goto close_fd;

	skel->bss->my_pid = getpid();
	skel->rodata->nr_cpus = libbpf_num_possible_cpus();

	err = percpu_alloc_cgrp_local_storage__load(skel);
	if (!ASSERT_OK(err, "percpu_alloc_cgrp_local_storage__load"))
		goto destroy_skel;

	err = percpu_alloc_cgrp_local_storage__attach(skel);
	if (!ASSERT_OK(err, "percpu_alloc_cgrp_local_storage__attach"))
		goto destroy_skel;

	prog_fd = bpf_program__fd(skel->progs.test_cgrp_local_storage_1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run cgrp_local_storage 1-3");
	ASSERT_EQ(topts.retval, 0, "test_run cgrp_local_storage 1-3");
	ASSERT_EQ(skel->bss->cpu0_field_d, 2, "cpu0_field_d");
	ASSERT_EQ(skel->bss->sum_field_c, 1, "sum_field_c");

destroy_skel:
	percpu_alloc_cgrp_local_storage__destroy(skel);
close_fd:
	close(cgroup_fd);
}

static void test_failure(void) {
	RUN_TESTS(percpu_alloc_fail);
}

void test_percpu_alloc(void)
{
	if (test__start_subtest("array"))
		test_array();
	if (test__start_subtest("array_sleepable"))
		test_array_sleepable();
	if (test__start_subtest("cgrp_local_storage"))
		test_cgrp_local_storage();
	if (test__start_subtest("failure_tests"))
		test_failure();
}
