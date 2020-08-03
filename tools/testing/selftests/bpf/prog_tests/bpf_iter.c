// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include "bpf_iter_ipv6_route.skel.h"
#include "bpf_iter_netlink.skel.h"
#include "bpf_iter_bpf_map.skel.h"
#include "bpf_iter_task.skel.h"
#include "bpf_iter_task_file.skel.h"
#include "bpf_iter_test_kern1.skel.h"
#include "bpf_iter_test_kern2.skel.h"
#include "bpf_iter_test_kern3.skel.h"
#include "bpf_iter_test_kern4.skel.h"

static int duration;

static void test_btf_id_or_null(void)
{
	struct bpf_iter_test_kern3 *skel;

	skel = bpf_iter_test_kern3__open_and_load();
	if (CHECK(skel, "bpf_iter_test_kern3__open_and_load",
		  "skeleton open_and_load unexpectedly succeeded\n")) {
		bpf_iter_test_kern3__destroy(skel);
		return;
	}
}

static void do_dummy_read(struct bpf_program *prog)
{
	struct bpf_link *link;
	char buf[16] = {};
	int iter_fd, len;

	link = bpf_program__attach_iter(prog, NULL);
	if (CHECK(IS_ERR(link), "attach_iter", "attach_iter failed\n"))
		return;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (CHECK(iter_fd < 0, "create_iter", "create_iter failed\n"))
		goto free_link;

	/* not check contents, but ensure read() ends without error */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	CHECK(len < 0, "read", "read failed: %s\n", strerror(errno));

	close(iter_fd);

free_link:
	bpf_link__destroy(link);
}

static void test_ipv6_route(void)
{
	struct bpf_iter_ipv6_route *skel;

	skel = bpf_iter_ipv6_route__open_and_load();
	if (CHECK(!skel, "bpf_iter_ipv6_route__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	do_dummy_read(skel->progs.dump_ipv6_route);

	bpf_iter_ipv6_route__destroy(skel);
}

static void test_netlink(void)
{
	struct bpf_iter_netlink *skel;

	skel = bpf_iter_netlink__open_and_load();
	if (CHECK(!skel, "bpf_iter_netlink__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	do_dummy_read(skel->progs.dump_netlink);

	bpf_iter_netlink__destroy(skel);
}

static void test_bpf_map(void)
{
	struct bpf_iter_bpf_map *skel;

	skel = bpf_iter_bpf_map__open_and_load();
	if (CHECK(!skel, "bpf_iter_bpf_map__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	do_dummy_read(skel->progs.dump_bpf_map);

	bpf_iter_bpf_map__destroy(skel);
}

static void test_task(void)
{
	struct bpf_iter_task *skel;

	skel = bpf_iter_task__open_and_load();
	if (CHECK(!skel, "bpf_iter_task__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	do_dummy_read(skel->progs.dump_task);

	bpf_iter_task__destroy(skel);
}

static void test_task_file(void)
{
	struct bpf_iter_task_file *skel;

	skel = bpf_iter_task_file__open_and_load();
	if (CHECK(!skel, "bpf_iter_task_file__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	do_dummy_read(skel->progs.dump_task_file);

	bpf_iter_task_file__destroy(skel);
}

/* The expected string is less than 16 bytes */
static int do_read_with_fd(int iter_fd, const char *expected,
			   bool read_one_char)
{
	int err = -1, len, read_buf_len, start;
	char buf[16] = {};

	read_buf_len = read_one_char ? 1 : 16;
	start = 0;
	while ((len = read(iter_fd, buf + start, read_buf_len)) > 0) {
		start += len;
		if (CHECK(start >= 16, "read", "read len %d\n", len))
			return -1;
		read_buf_len = read_one_char ? 1 : 16 - start;
	}
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		return -1;

	err = strcmp(buf, expected);
	if (CHECK(err, "read", "incorrect read result: buf %s, expected %s\n",
		  buf, expected))
		return -1;

	return 0;
}

static void test_anon_iter(bool read_one_char)
{
	struct bpf_iter_test_kern1 *skel;
	struct bpf_link *link;
	int iter_fd, err;

	skel = bpf_iter_test_kern1__open_and_load();
	if (CHECK(!skel, "bpf_iter_test_kern1__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	err = bpf_iter_test_kern1__attach(skel);
	if (CHECK(err, "bpf_iter_test_kern1__attach",
		  "skeleton attach failed\n")) {
		goto out;
	}

	link = skel->links.dump_task;
	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (CHECK(iter_fd < 0, "create_iter", "create_iter failed\n"))
		goto out;

	do_read_with_fd(iter_fd, "abcd", read_one_char);
	close(iter_fd);

out:
	bpf_iter_test_kern1__destroy(skel);
}

static int do_read(const char *path, const char *expected)
{
	int err, iter_fd;

	iter_fd = open(path, O_RDONLY);
	if (CHECK(iter_fd < 0, "open", "open %s failed: %s\n",
		  path, strerror(errno)))
		return -1;

	err = do_read_with_fd(iter_fd, expected, false);
	close(iter_fd);
	return err;
}

static void test_file_iter(void)
{
	const char *path = "/sys/fs/bpf/bpf_iter_test1";
	struct bpf_iter_test_kern1 *skel1;
	struct bpf_iter_test_kern2 *skel2;
	struct bpf_link *link;
	int err;

	skel1 = bpf_iter_test_kern1__open_and_load();
	if (CHECK(!skel1, "bpf_iter_test_kern1__open_and_load",
		  "skeleton open_and_load failed\n"))
		return;

	link = bpf_program__attach_iter(skel1->progs.dump_task, NULL);
	if (CHECK(IS_ERR(link), "attach_iter", "attach_iter failed\n"))
		goto out;

	/* unlink this path if it exists. */
	unlink(path);

	err = bpf_link__pin(link, path);
	if (CHECK(err, "pin_iter", "pin_iter to %s failed: %d\n", path, err))
		goto free_link;

	err = do_read(path, "abcd");
	if (err)
		goto unlink_path;

	/* file based iterator seems working fine. Let us a link update
	 * of the underlying link and `cat` the iterator again, its content
	 * should change.
	 */
	skel2 = bpf_iter_test_kern2__open_and_load();
	if (CHECK(!skel2, "bpf_iter_test_kern2__open_and_load",
		  "skeleton open_and_load failed\n"))
		goto unlink_path;

	err = bpf_link__update_program(link, skel2->progs.dump_task);
	if (CHECK(err, "update_prog", "update_prog failed\n"))
		goto destroy_skel2;

	do_read(path, "ABCD");

destroy_skel2:
	bpf_iter_test_kern2__destroy(skel2);
unlink_path:
	unlink(path);
free_link:
	bpf_link__destroy(link);
out:
	bpf_iter_test_kern1__destroy(skel1);
}

static void test_overflow(bool test_e2big_overflow, bool ret1)
{
	__u32 map_info_len, total_read_len, expected_read_len;
	int err, iter_fd, map1_fd, map2_fd, len;
	struct bpf_map_info map_info = {};
	struct bpf_iter_test_kern4 *skel;
	struct bpf_link *link;
	__u32 page_size;
	char *buf;

	skel = bpf_iter_test_kern4__open();
	if (CHECK(!skel, "bpf_iter_test_kern4__open",
		  "skeleton open failed\n"))
		return;

	/* create two maps: bpf program will only do bpf_seq_write
	 * for these two maps. The goal is one map output almost
	 * fills seq_file buffer and then the other will trigger
	 * overflow and needs restart.
	 */
	map1_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1, 0);
	if (CHECK(map1_fd < 0, "bpf_create_map",
		  "map_creation failed: %s\n", strerror(errno)))
		goto out;
	map2_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, 4, 8, 1, 0);
	if (CHECK(map2_fd < 0, "bpf_create_map",
		  "map_creation failed: %s\n", strerror(errno)))
		goto free_map1;

	/* bpf_seq_printf kernel buffer is one page, so one map
	 * bpf_seq_write will mostly fill it, and the other map
	 * will partially fill and then trigger overflow and need
	 * bpf_seq_read restart.
	 */
	page_size = sysconf(_SC_PAGE_SIZE);

	if (test_e2big_overflow) {
		skel->rodata->print_len = (page_size + 8) / 8;
		expected_read_len = 2 * (page_size + 8);
	} else if (!ret1) {
		skel->rodata->print_len = (page_size - 8) / 8;
		expected_read_len = 2 * (page_size - 8);
	} else {
		skel->rodata->print_len = 1;
		expected_read_len = 2 * 8;
	}
	skel->rodata->ret1 = ret1;

	if (CHECK(bpf_iter_test_kern4__load(skel),
		  "bpf_iter_test_kern4__load", "skeleton load failed\n"))
		goto free_map2;

	/* setup filtering map_id in bpf program */
	map_info_len = sizeof(map_info);
	err = bpf_obj_get_info_by_fd(map1_fd, &map_info, &map_info_len);
	if (CHECK(err, "get_map_info", "get map info failed: %s\n",
		  strerror(errno)))
		goto free_map2;
	skel->bss->map1_id = map_info.id;

	err = bpf_obj_get_info_by_fd(map2_fd, &map_info, &map_info_len);
	if (CHECK(err, "get_map_info", "get map info failed: %s\n",
		  strerror(errno)))
		goto free_map2;
	skel->bss->map2_id = map_info.id;

	link = bpf_program__attach_iter(skel->progs.dump_bpf_map, NULL);
	if (CHECK(IS_ERR(link), "attach_iter", "attach_iter failed\n"))
		goto free_map2;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (CHECK(iter_fd < 0, "create_iter", "create_iter failed\n"))
		goto free_link;

	buf = malloc(expected_read_len);
	if (!buf)
		goto close_iter;

	/* do read */
	total_read_len = 0;
	if (test_e2big_overflow) {
		while ((len = read(iter_fd, buf, expected_read_len)) > 0)
			total_read_len += len;

		CHECK(len != -1 || errno != E2BIG, "read",
		      "expected ret -1, errno E2BIG, but get ret %d, error %s\n",
			  len, strerror(errno));
		goto free_buf;
	} else if (!ret1) {
		while ((len = read(iter_fd, buf, expected_read_len)) > 0)
			total_read_len += len;

		if (CHECK(len < 0, "read", "read failed: %s\n",
			  strerror(errno)))
			goto free_buf;
	} else {
		do {
			len = read(iter_fd, buf, expected_read_len);
			if (len > 0)
				total_read_len += len;
		} while (len > 0 || len == -EAGAIN);

		if (CHECK(len < 0, "read", "read failed: %s\n",
			  strerror(errno)))
			goto free_buf;
	}

	if (CHECK(total_read_len != expected_read_len, "read",
		  "total len %u, expected len %u\n", total_read_len,
		  expected_read_len))
		goto free_buf;

	if (CHECK(skel->bss->map1_accessed != 1, "map1_accessed",
		  "expected 1 actual %d\n", skel->bss->map1_accessed))
		goto free_buf;

	if (CHECK(skel->bss->map2_accessed != 2, "map2_accessed",
		  "expected 2 actual %d\n", skel->bss->map2_accessed))
		goto free_buf;

	CHECK(skel->bss->map2_seqnum1 != skel->bss->map2_seqnum2,
	      "map2_seqnum", "two different seqnum %lld %lld\n",
	      skel->bss->map2_seqnum1, skel->bss->map2_seqnum2);

free_buf:
	free(buf);
close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
free_map2:
	close(map2_fd);
free_map1:
	close(map1_fd);
out:
	bpf_iter_test_kern4__destroy(skel);
}

void test_bpf_iter(void)
{
	if (test__start_subtest("btf_id_or_null"))
		test_btf_id_or_null();
	if (test__start_subtest("ipv6_route"))
		test_ipv6_route();
	if (test__start_subtest("netlink"))
		test_netlink();
	if (test__start_subtest("bpf_map"))
		test_bpf_map();
	if (test__start_subtest("task"))
		test_task();
	if (test__start_subtest("task_file"))
		test_task_file();
	if (test__start_subtest("anon"))
		test_anon_iter(false);
	if (test__start_subtest("anon-read-one-char"))
		test_anon_iter(true);
	if (test__start_subtest("file"))
		test_file_iter();
	if (test__start_subtest("overflow"))
		test_overflow(false, false);
	if (test__start_subtest("overflow-e2big"))
		test_overflow(true, false);
	if (test__start_subtest("prog-ret-1"))
		test_overflow(false, true);
}
