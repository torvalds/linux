// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google */

#include <test_progs.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>
#include "kmem_cache_iter.skel.h"

#define SLAB_NAME_MAX  32

struct kmem_cache_result {
	char name[SLAB_NAME_MAX];
	long obj_size;
};

static void subtest_kmem_cache_iter_check_task_struct(struct kmem_cache_iter *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.flags = 0,  /* Run it with the current task */
	);
	int prog_fd = bpf_program__fd(skel->progs.check_task_struct);

	/* Get task_struct and check it if's from a slab cache */
	ASSERT_OK(bpf_prog_test_run_opts(prog_fd, &opts), "prog_test_run");

	/* The BPF program should set 'found' variable */
	ASSERT_EQ(skel->bss->task_struct_found, 1, "task_struct_found");
}

static void subtest_kmem_cache_iter_check_slabinfo(struct kmem_cache_iter *skel)
{
	FILE *fp;
	int map_fd;
	char name[SLAB_NAME_MAX];
	unsigned long objsize;
	char rest_of_line[1000];
	struct kmem_cache_result r;
	int seen = 0;

	fp = fopen("/proc/slabinfo", "r");
	if (fp == NULL) {
		/* CONFIG_SLUB_DEBUG is not enabled */
		return;
	}

	map_fd = bpf_map__fd(skel->maps.slab_result);

	/* Ignore first two lines for header */
	fscanf(fp, "slabinfo - version: %*d.%*d\n");
	fscanf(fp, "# %*s %*s %*s %*s %*s %*s : %[^\n]\n", rest_of_line);

	/* Compare name and objsize only - others can be changes frequently */
	while (fscanf(fp, "%s %*u %*u %lu %*u %*u : %[^\n]\n",
		      name, &objsize, rest_of_line) == 3) {
		int ret = bpf_map_lookup_elem(map_fd, &seen, &r);

		if (!ASSERT_OK(ret, "kmem_cache_lookup"))
			break;

		ASSERT_STREQ(r.name, name, "kmem_cache_name");
		ASSERT_EQ(r.obj_size, objsize, "kmem_cache_objsize");

		seen++;
	}

	ASSERT_EQ(skel->bss->kmem_cache_seen, seen, "kmem_cache_seen_eq");

	fclose(fp);
}

static void subtest_kmem_cache_iter_open_coded(struct kmem_cache_iter *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, fd;

	/* No need to attach it, just run it directly */
	fd = bpf_program__fd(skel->progs.open_coded_iter);

	err = bpf_prog_test_run_opts(fd, &topts);
	if (!ASSERT_OK(err, "test_run_opts err"))
		return;
	if (!ASSERT_OK(topts.retval, "test_run_opts retval"))
		return;

	/* It should be same as we've seen from the explicit iterator */
	ASSERT_EQ(skel->bss->open_coded_seen, skel->bss->kmem_cache_seen, "open_code_seen_eq");
}

void test_kmem_cache_iter(void)
{
	struct kmem_cache_iter *skel = NULL;
	char buf[256];
	int iter_fd;

	skel = kmem_cache_iter__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kmem_cache_iter__open_and_load"))
		return;

	if (!ASSERT_OK(kmem_cache_iter__attach(skel), "skel_attach"))
		goto destroy;

	iter_fd = bpf_iter_create(bpf_link__fd(skel->links.slab_info_collector));
	if (!ASSERT_GE(iter_fd, 0, "iter_create"))
		goto destroy;

	memset(buf, 0, sizeof(buf));
	while (read(iter_fd, buf, sizeof(buf)) > 0) {
		/* Read out all contents */
		printf("%s", buf);
	}

	/* Next reads should return 0 */
	ASSERT_EQ(read(iter_fd, buf, sizeof(buf)), 0, "read");

	if (test__start_subtest("check_task_struct"))
		subtest_kmem_cache_iter_check_task_struct(skel);
	if (test__start_subtest("check_slabinfo"))
		subtest_kmem_cache_iter_check_slabinfo(skel);
	if (test__start_subtest("open_coded_iter"))
		subtest_kmem_cache_iter_open_coded(skel);

	close(iter_fd);

destroy:
	kmem_cache_iter__destroy(skel);
}
