// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Google LLC. */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <test_progs.h>
#include <bpf/btf.h>

#include "map_excl.skel.h"

static void test_map_excl_allowed(void)
{
	struct map_excl *skel = map_excl__open();
	int err;

	err = bpf_map__set_exclusive_program(skel->maps.excl_map, skel->progs.should_have_access);
	if (!ASSERT_OK(err, "bpf_map__set_exclusive_program"))
		goto out;

	bpf_program__set_autoload(skel->progs.should_have_access, true);
	bpf_program__set_autoload(skel->progs.should_not_have_access, false);

	err = map_excl__load(skel);
	ASSERT_OK(err, "map_excl__load");
out:
	map_excl__destroy(skel);
}

static void test_map_excl_denied(void)
{
	struct map_excl *skel = map_excl__open();
	int err;

	err = bpf_map__set_exclusive_program(skel->maps.excl_map, skel->progs.should_have_access);
	if (!ASSERT_OK(err, "bpf_map__make_exclusive"))
		goto out;

	bpf_program__set_autoload(skel->progs.should_have_access, false);
	bpf_program__set_autoload(skel->progs.should_not_have_access, true);

	err = map_excl__load(skel);
	ASSERT_EQ(err, -EACCES, "exclusive map access not denied\n");
out:
	map_excl__destroy(skel);

}

void test_map_excl(void)
{
	if (test__start_subtest("map_excl_allowed"))
		test_map_excl_allowed();
	if (test__start_subtest("map_excl_denied"))
		test_map_excl_denied();
}
