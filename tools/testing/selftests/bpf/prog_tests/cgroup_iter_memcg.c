// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cgroup_helpers.h"
#include "cgroup_iter_memcg.h"
#include "cgroup_iter_memcg.skel.h"

static int read_stats(struct bpf_link *link)
{
	int fd, ret = 0;
	ssize_t bytes;

	fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_OK_FD(fd, "bpf_iter_create"))
		return 1;

	/*
	 * Invoke iter program by reading from its fd. We're not expecting any
	 * data to be written by the bpf program so the result should be zero.
	 * Results will be read directly through the custom data section
	 * accessible through skel->data_query.memcg_query.
	 */
	bytes = read(fd, NULL, 0);
	if (!ASSERT_EQ(bytes, 0, "read fd"))
		ret = 1;

	close(fd);
	return ret;
}

static void test_anon(struct bpf_link *link, struct memcg_query *memcg_query)
{
	void *map;
	size_t len;

	len = sysconf(_SC_PAGESIZE) * 1024;

	/*
	 * Increase memcg anon usage by mapping and writing
	 * to a new anon region.
	 */
	map = mmap(NULL, len, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (!ASSERT_NEQ(map, MAP_FAILED, "mmap anon"))
		return;

	memset(map, 1, len);

	if (!ASSERT_OK(read_stats(link), "read stats"))
		goto cleanup;

	ASSERT_GT(memcg_query->nr_anon_mapped, 0, "final anon mapped val");

cleanup:
	munmap(map, len);
}

static void test_file(struct bpf_link *link, struct memcg_query *memcg_query)
{
	void *map;
	size_t len;
	char *path;
	int fd;

	len = sysconf(_SC_PAGESIZE) * 1024;
	path = "/tmp/test_cgroup_iter_memcg";

	/*
	 * Increase memcg file usage by creating and writing
	 * to a mapped file.
	 */
	fd = open(path, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_OK_FD(fd, "open fd"))
		return;
	if (!ASSERT_OK(ftruncate(fd, len), "ftruncate"))
		goto cleanup_fd;

	map = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);
	if (!ASSERT_NEQ(map, MAP_FAILED, "mmap file"))
		goto cleanup_fd;

	memset(map, 1, len);

	if (!ASSERT_OK(read_stats(link), "read stats"))
		goto cleanup_map;

	ASSERT_GT(memcg_query->nr_file_pages, 0, "final file value");
	ASSERT_GT(memcg_query->nr_file_mapped, 0, "final file mapped value");

cleanup_map:
	munmap(map, len);
cleanup_fd:
	close(fd);
	unlink(path);
}

static void test_shmem(struct bpf_link *link, struct memcg_query *memcg_query)
{
	size_t len;
	int fd;

	len = sysconf(_SC_PAGESIZE) * 1024;

	/*
	 * Increase memcg shmem usage by creating and writing
	 * to a shmem object.
	 */
	fd = shm_open("/tmp_shmem", O_CREAT | O_RDWR, 0644);
	if (!ASSERT_OK_FD(fd, "shm_open"))
		return;

	if (!ASSERT_OK(fallocate(fd, 0, 0, len), "fallocate"))
		goto cleanup;

	if (!ASSERT_OK(read_stats(link), "read stats"))
		goto cleanup;

	ASSERT_GT(memcg_query->nr_shmem, 0, "final shmem value");

cleanup:
	close(fd);
	shm_unlink("/tmp_shmem");
}

#define NR_PIPES 64
static void test_kmem(struct bpf_link *link, struct memcg_query *memcg_query)
{
	int fds[NR_PIPES][2], i;

	/*
	 * Increase kmem value by creating pipes which will allocate some
	 * kernel buffers.
	 */
	for (i = 0; i < NR_PIPES; i++) {
		if (!ASSERT_OK(pipe(fds[i]), "pipe"))
			goto cleanup;
	}

	if (!ASSERT_OK(read_stats(link), "read stats"))
		goto cleanup;

	ASSERT_GT(memcg_query->memcg_kmem, 0, "kmem value");

cleanup:
	for (i = i - 1; i >= 0; i--) {
		close(fds[i][0]);
		close(fds[i][1]);
	}
}

static void test_pgfault(struct bpf_link *link, struct memcg_query *memcg_query)
{
	void *map;
	size_t len;

	len = sysconf(_SC_PAGESIZE) * 1024;

	/* Create region to use for triggering a page fault. */
	map = mmap(NULL, len, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (!ASSERT_NEQ(map, MAP_FAILED, "mmap anon"))
		return;

	/* Trigger page fault. */
	memset(map, 1, len);

	if (!ASSERT_OK(read_stats(link), "read stats"))
		goto cleanup;

	ASSERT_GT(memcg_query->pgfault, 0, "final pgfault val");

cleanup:
	munmap(map, len);
}

void test_cgroup_iter_memcg(void)
{
	char *cgroup_rel_path = "/cgroup_iter_memcg_test";
	struct cgroup_iter_memcg *skel;
	struct bpf_link *link;
	int cgroup_fd;

	cgroup_fd = cgroup_setup_and_join(cgroup_rel_path);
	if (!ASSERT_OK_FD(cgroup_fd, "cgroup_setup_and_join"))
		return;

	skel = cgroup_iter_memcg__open_and_load();
	if (!ASSERT_OK_PTR(skel, "cgroup_iter_memcg__open_and_load"))
		goto cleanup_cgroup_fd;

	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo = {
		.cgroup.cgroup_fd = cgroup_fd,
		.cgroup.order = BPF_CGROUP_ITER_SELF_ONLY,
	};
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	link = bpf_program__attach_iter(skel->progs.cgroup_memcg_query, &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_iter"))
		goto cleanup_skel;

	if (test__start_subtest("cgroup_iter_memcg__anon"))
		test_anon(link, &skel->data_query->memcg_query);
	if (test__start_subtest("cgroup_iter_memcg__shmem"))
		test_shmem(link, &skel->data_query->memcg_query);
	if (test__start_subtest("cgroup_iter_memcg__file"))
		test_file(link, &skel->data_query->memcg_query);
	if (test__start_subtest("cgroup_iter_memcg__kmem"))
		test_kmem(link, &skel->data_query->memcg_query);
	if (test__start_subtest("cgroup_iter_memcg__pgfault"))
		test_pgfault(link, &skel->data_query->memcg_query);

	bpf_link__destroy(link);
cleanup_skel:
	cgroup_iter_memcg__destroy(skel);
cleanup_cgroup_fd:
	close(cgroup_fd);
	cleanup_cgroup_environment();
}
