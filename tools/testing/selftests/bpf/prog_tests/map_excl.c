// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Google LLC. */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <test_progs.h>
#include <bpf/btf.h>

#include "map_excl.skel.h"
#include "bpf_iter_bpf_array_map.skel.h"

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE	32
#endif

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

static void test_map_excl_no_map_in_map(void)
{
	__u8 hash[SHA256_DIGEST_SIZE] = {};
	LIBBPF_OPTS(bpf_map_create_opts, excl_opts,
		    .excl_prog_hash = hash,
		    .excl_prog_hash_size = sizeof(hash));
	LIBBPF_OPTS(bpf_map_create_opts, outer_opts);
	int excl_fd, tmpl_fd = -1, outer_fd = -1, err;
	__u32 key = 0;

	excl_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "excl_inner", 4, 4, 1, &excl_opts);
	if (!ASSERT_OK_FD(excl_fd, "create exclusive map"))
		return;

	outer_opts.inner_map_fd = excl_fd;
	err = bpf_map_create(BPF_MAP_TYPE_ARRAY_OF_MAPS, "outer_from_excl",
			     4, 4, 1, &outer_opts);
	if (err >= 0)
		close(err);
	ASSERT_EQ(err, -ENOTSUPP, "reject exclusive map as map-in-map template");

	tmpl_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "tmpl", 4, 4, 1, NULL);
	if (!ASSERT_OK_FD(tmpl_fd, "create inner template"))
		goto out;

	outer_opts.inner_map_fd = tmpl_fd;
	outer_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY_OF_MAPS, "outer", 4, 4, 1, &outer_opts);
	if (!ASSERT_OK_FD(outer_fd, "create map-of-maps"))
		goto out;

	err = bpf_map_update_elem(outer_fd, &key, &excl_fd, 0);
	ASSERT_EQ(err, -ENOTSUPP, "reject exclusive map as map-in-map element");
out:
	if (outer_fd >= 0)
		close(outer_fd);
	if (tmpl_fd >= 0)
		close(tmpl_fd);
	close(excl_fd);
}

static void test_map_excl_no_map_iter(void)
{
	__u8 hash[SHA256_DIGEST_SIZE] = {};
	LIBBPF_OPTS(bpf_map_create_opts, excl_opts,
		    .excl_prog_hash = hash,
		    .excl_prog_hash_size = sizeof(hash));
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_bpf_array_map *skel = NULL;
	union bpf_iter_link_info linfo;
	struct bpf_link *link;
	int excl_fd;

	excl_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "excl_iter", 4, 8, 3, &excl_opts);
	if (!ASSERT_OK_FD(excl_fd, "create exclusive map"))
		return;

	skel = bpf_iter_bpf_array_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_array_map__open_and_load"))
		goto out;

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = excl_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	link = bpf_program__attach_iter(skel->progs.dump_bpf_array_map, &opts);
	if (!ASSERT_ERR_PTR(link, "reject exclusive map as iter target")) {
		bpf_link__destroy(link);
		goto out;
	}
	ASSERT_EQ(libbpf_get_error(link), -EPERM, "iter attach errno");
out:
	bpf_iter_bpf_array_map__destroy(skel);
	close(excl_fd);
}

static void test_map_excl_create_validation(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, o);
	__u8 hash[SHA256_DIGEST_SIZE] = {};
	int fd;

	o.excl_prog_hash = hash;
	o.excl_prog_hash_size = SHA256_DIGEST_SIZE / 2;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "excl", 4, 4, 1, &o);
	if (fd >= 0)
		close(fd);
	ASSERT_EQ(fd, -EINVAL, "reject short excl_prog_hash_size");

	o.excl_prog_hash = hash;
	o.excl_prog_hash_size = SHA256_DIGEST_SIZE * 2;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "excl", 4, 4, 1, &o);
	if (fd >= 0)
		close(fd);
	ASSERT_EQ(fd, -EINVAL, "reject long excl_prog_hash_size");

	o.excl_prog_hash = hash;
	o.excl_prog_hash_size = 0;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "excl", 4, 4, 1, &o);
	if (fd >= 0)
		close(fd);
	ASSERT_EQ(fd, -EINVAL, "reject hash pointer with zero size");

	o.excl_prog_hash = NULL;
	o.excl_prog_hash_size = SHA256_DIGEST_SIZE;
	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "excl", 4, 4, 1, &o);
	if (fd >= 0)
		close(fd);
	ASSERT_EQ(fd, -EINVAL, "reject size with NULL hash pointer");
}

void test_map_excl(void)
{
	if (test__start_subtest("map_excl_allowed"))
		test_map_excl_allowed();
	if (test__start_subtest("map_excl_denied"))
		test_map_excl_denied();
	if (test__start_subtest("map_excl_no_map_in_map"))
		test_map_excl_no_map_in_map();
	if (test__start_subtest("map_excl_no_map_iter"))
		test_map_excl_no_map_iter();
	if (test__start_subtest("map_excl_create_validation"))
		test_map_excl_create_validation();
}
