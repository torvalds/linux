// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include "test_legacy_printk.skel.h"

static int execute_one_variant(bool legacy)
{
	struct test_legacy_printk *skel;
	int err, zero = 0, my_pid = getpid(), res, map_fd;

	skel = test_legacy_printk__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return -errno;

	bpf_program__set_autoload(skel->progs.handle_legacy, legacy);
	bpf_program__set_autoload(skel->progs.handle_modern, !legacy);

	err = test_legacy_printk__load(skel);
	/* no ASSERT_OK, we expect one of two variants can fail here */
	if (err)
		goto err_out;

	if (legacy) {
		map_fd = bpf_map__fd(skel->maps.my_pid_map);
		err = bpf_map_update_elem(map_fd, &zero, &my_pid, BPF_ANY);
		if (!ASSERT_OK(err, "my_pid_map_update"))
			goto err_out;
		err = bpf_map_lookup_elem(map_fd, &zero, &res);
	} else {
		skel->bss->my_pid_var = my_pid;
	}

	err = test_legacy_printk__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto err_out;

	usleep(1); /* trigger */

	if (legacy) {
		map_fd = bpf_map__fd(skel->maps.res_map);
		err = bpf_map_lookup_elem(map_fd, &zero, &res);
		if (!ASSERT_OK(err, "res_map_lookup"))
			goto err_out;
	} else {
		res = skel->bss->res_var;
	}

	if (!ASSERT_GT(res, 0, "res")) {
		err = -EINVAL;
		goto err_out;
	}

err_out:
	test_legacy_printk__destroy(skel);
	return err;
}

void test_legacy_printk(void)
{
	/* legacy variant should work everywhere */
	ASSERT_OK(execute_one_variant(true /* legacy */), "legacy_case");

	/* execute modern variant, can fail the load on old kernels */
	execute_one_variant(false);
}
