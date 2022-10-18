// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "bpf_iter_ipv6_route.skel.h"
#include "bpf_iter_netlink.skel.h"
#include "bpf_iter_bpf_map.skel.h"
#include "bpf_iter_task.skel.h"
#include "bpf_iter_task_stack.skel.h"
#include "bpf_iter_task_file.skel.h"
#include "bpf_iter_task_vma.skel.h"
#include "bpf_iter_task_btf.skel.h"
#include "bpf_iter_tcp4.skel.h"
#include "bpf_iter_tcp6.skel.h"
#include "bpf_iter_udp4.skel.h"
#include "bpf_iter_udp6.skel.h"
#include "bpf_iter_unix.skel.h"
#include "bpf_iter_vma_offset.skel.h"
#include "bpf_iter_test_kern1.skel.h"
#include "bpf_iter_test_kern2.skel.h"
#include "bpf_iter_test_kern3.skel.h"
#include "bpf_iter_test_kern4.skel.h"
#include "bpf_iter_bpf_hash_map.skel.h"
#include "bpf_iter_bpf_percpu_hash_map.skel.h"
#include "bpf_iter_bpf_array_map.skel.h"
#include "bpf_iter_bpf_percpu_array_map.skel.h"
#include "bpf_iter_bpf_sk_storage_helpers.skel.h"
#include "bpf_iter_bpf_sk_storage_map.skel.h"
#include "bpf_iter_test_kern5.skel.h"
#include "bpf_iter_test_kern6.skel.h"
#include "bpf_iter_bpf_link.skel.h"
#include "bpf_iter_ksym.skel.h"
#include "bpf_iter_sockmap.skel.h"

static int duration;

static void test_btf_id_or_null(void)
{
	struct bpf_iter_test_kern3 *skel;

	skel = bpf_iter_test_kern3__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "bpf_iter_test_kern3__open_and_load")) {
		bpf_iter_test_kern3__destroy(skel);
		return;
	}
}

static void do_dummy_read_opts(struct bpf_program *prog, struct bpf_iter_attach_opts *opts)
{
	struct bpf_link *link;
	char buf[16] = {};
	int iter_fd, len;

	link = bpf_program__attach_iter(prog, opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		return;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* not check contents, but ensure read() ends without error */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	CHECK(len < 0, "read", "read failed: %s\n", strerror(errno));

	close(iter_fd);

free_link:
	bpf_link__destroy(link);
}

static void do_dummy_read(struct bpf_program *prog)
{
	do_dummy_read_opts(prog, NULL);
}

static void do_read_map_iter_fd(struct bpf_object_skeleton **skel, struct bpf_program *prog,
				struct bpf_map *map)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;
	struct bpf_link *link;
	char buf[16] = {};
	int iter_fd, len;

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = bpf_map__fd(map);
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(prog, &opts);
	if (!ASSERT_OK_PTR(link, "attach_map_iter"))
		return;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_map_iter")) {
		bpf_link__destroy(link);
		return;
	}

	/* Close link and map fd prematurely */
	bpf_link__destroy(link);
	bpf_object__destroy_skeleton(*skel);
	*skel = NULL;

	/* Try to let map free work to run first if map is freed */
	usleep(100);
	/* Memory used by both sock map and sock local storage map are
	 * freed after two synchronize_rcu() calls, so wait for it
	 */
	kern_sync_rcu();
	kern_sync_rcu();

	/* Read after both map fd and link fd are closed */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	ASSERT_GE(len, 0, "read_iterator");

	close(iter_fd);
}

static int read_fd_into_buffer(int fd, char *buf, int size)
{
	int bufleft = size;
	int len;

	do {
		len = read(fd, buf, bufleft);
		if (len > 0) {
			buf += len;
			bufleft -= len;
		}
	} while (len > 0);

	return len < 0 ? len : size - bufleft;
}

static void test_ipv6_route(void)
{
	struct bpf_iter_ipv6_route *skel;

	skel = bpf_iter_ipv6_route__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_ipv6_route__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_ipv6_route);

	bpf_iter_ipv6_route__destroy(skel);
}

static void test_netlink(void)
{
	struct bpf_iter_netlink *skel;

	skel = bpf_iter_netlink__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_netlink__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_netlink);

	bpf_iter_netlink__destroy(skel);
}

static void test_bpf_map(void)
{
	struct bpf_iter_bpf_map *skel;

	skel = bpf_iter_bpf_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_map__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_bpf_map);

	bpf_iter_bpf_map__destroy(skel);
}

static int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}

static void check_bpf_link_info(const struct bpf_program *prog)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;
	struct bpf_link_info info = {};
	struct bpf_link *link;
	__u32 info_len;
	int err;

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.tid = getpid();
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	link = bpf_program__attach_iter(prog, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		return;

	info_len = sizeof(info);
	err = bpf_obj_get_info_by_fd(bpf_link__fd(link), &info, &info_len);
	ASSERT_OK(err, "bpf_obj_get_info_by_fd");
	ASSERT_EQ(info.iter.task.tid, getpid(), "check_task_tid");

	bpf_link__destroy(link);
}

static pthread_mutex_t do_nothing_mutex;

static void *do_nothing_wait(void *arg)
{
	pthread_mutex_lock(&do_nothing_mutex);
	pthread_mutex_unlock(&do_nothing_mutex);

	pthread_exit(arg);
}

static void test_task_common_nocheck(struct bpf_iter_attach_opts *opts,
				     int *num_unknown, int *num_known)
{
	struct bpf_iter_task *skel;
	pthread_t thread_id;
	void *ret;

	skel = bpf_iter_task__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_task__open_and_load"))
		return;

	ASSERT_OK(pthread_mutex_lock(&do_nothing_mutex), "pthread_mutex_lock");

	ASSERT_OK(pthread_create(&thread_id, NULL, &do_nothing_wait, NULL),
		  "pthread_create");

	skel->bss->tid = getpid();

	do_dummy_read_opts(skel->progs.dump_task, opts);

	*num_unknown = skel->bss->num_unknown_tid;
	*num_known = skel->bss->num_known_tid;

	ASSERT_OK(pthread_mutex_unlock(&do_nothing_mutex), "pthread_mutex_unlock");
	ASSERT_FALSE(pthread_join(thread_id, &ret) || ret != NULL,
		     "pthread_join");

	bpf_iter_task__destroy(skel);
}

static void test_task_common(struct bpf_iter_attach_opts *opts, int num_unknown, int num_known)
{
	int num_unknown_tid, num_known_tid;

	test_task_common_nocheck(opts, &num_unknown_tid, &num_known_tid);
	ASSERT_EQ(num_unknown_tid, num_unknown, "check_num_unknown_tid");
	ASSERT_EQ(num_known_tid, num_known, "check_num_known_tid");
}

static void test_task_tid(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;
	int num_unknown_tid, num_known_tid;

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.tid = getpid();
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	test_task_common(&opts, 0, 1);

	linfo.task.tid = 0;
	linfo.task.pid = getpid();
	test_task_common(&opts, 1, 1);

	test_task_common_nocheck(NULL, &num_unknown_tid, &num_known_tid);
	ASSERT_GT(num_unknown_tid, 1, "check_num_unknown_tid");
	ASSERT_EQ(num_known_tid, 1, "check_num_known_tid");
}

static void test_task_pid(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.pid = getpid();
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	test_task_common(&opts, 1, 1);
}

static void test_task_pidfd(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;
	int pidfd;

	pidfd = pidfd_open(getpid(), 0);
	if (!ASSERT_GT(pidfd, 0, "pidfd_open"))
		return;

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.pid_fd = pidfd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	test_task_common(&opts, 1, 1);

	close(pidfd);
}

static void test_task_sleepable(void)
{
	struct bpf_iter_task *skel;

	skel = bpf_iter_task__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_task__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_task_sleepable);

	ASSERT_GT(skel->bss->num_expected_failure_copy_from_user_task, 0,
		  "num_expected_failure_copy_from_user_task");
	ASSERT_GT(skel->bss->num_success_copy_from_user_task, 0,
		  "num_success_copy_from_user_task");

	bpf_iter_task__destroy(skel);
}

static void test_task_stack(void)
{
	struct bpf_iter_task_stack *skel;

	skel = bpf_iter_task_stack__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_task_stack__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_task_stack);
	do_dummy_read(skel->progs.get_task_user_stacks);

	bpf_iter_task_stack__destroy(skel);
}

static void test_task_file(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_task_file *skel;
	union bpf_iter_link_info linfo;
	pthread_t thread_id;
	void *ret;

	skel = bpf_iter_task_file__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_task_file__open_and_load"))
		return;

	skel->bss->tgid = getpid();

	ASSERT_OK(pthread_mutex_lock(&do_nothing_mutex), "pthread_mutex_lock");

	ASSERT_OK(pthread_create(&thread_id, NULL, &do_nothing_wait, NULL),
		  "pthread_create");

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.tid = getpid();
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	do_dummy_read_opts(skel->progs.dump_task_file, &opts);

	ASSERT_EQ(skel->bss->count, 0, "check_count");
	ASSERT_EQ(skel->bss->unique_tgid_count, 1, "check_unique_tgid_count");

	skel->bss->last_tgid = 0;
	skel->bss->count = 0;
	skel->bss->unique_tgid_count = 0;

	do_dummy_read(skel->progs.dump_task_file);

	ASSERT_EQ(skel->bss->count, 0, "check_count");
	ASSERT_GT(skel->bss->unique_tgid_count, 1, "check_unique_tgid_count");

	check_bpf_link_info(skel->progs.dump_task_file);

	ASSERT_OK(pthread_mutex_unlock(&do_nothing_mutex), "pthread_mutex_unlock");
	ASSERT_OK(pthread_join(thread_id, &ret), "pthread_join");
	ASSERT_NULL(ret, "pthread_join");

	bpf_iter_task_file__destroy(skel);
}

#define TASKBUFSZ		32768

static char taskbuf[TASKBUFSZ];

static int do_btf_read(struct bpf_iter_task_btf *skel)
{
	struct bpf_program *prog = skel->progs.dump_task_struct;
	struct bpf_iter_task_btf__bss *bss = skel->bss;
	int iter_fd = -1, err;
	struct bpf_link *link;
	char *buf = taskbuf;
	int ret = 0;

	link = bpf_program__attach_iter(prog, NULL);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		return ret;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	err = read_fd_into_buffer(iter_fd, buf, TASKBUFSZ);
	if (bss->skip) {
		printf("%s:SKIP:no __builtin_btf_type_id\n", __func__);
		ret = 1;
		test__skip();
		goto free_link;
	}

	if (CHECK(err < 0, "read", "read failed: %s\n", strerror(errno)))
		goto free_link;

	ASSERT_HAS_SUBSTR(taskbuf, "(struct task_struct)",
	      "check for btf representation of task_struct in iter data");
free_link:
	if (iter_fd > 0)
		close(iter_fd);
	bpf_link__destroy(link);
	return ret;
}

static void test_task_btf(void)
{
	struct bpf_iter_task_btf__bss *bss;
	struct bpf_iter_task_btf *skel;
	int ret;

	skel = bpf_iter_task_btf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_task_btf__open_and_load"))
		return;

	bss = skel->bss;

	ret = do_btf_read(skel);
	if (ret)
		goto cleanup;

	if (!ASSERT_NEQ(bss->tasks, 0, "no task iteration, did BPF program run?"))
		goto cleanup;

	ASSERT_EQ(bss->seq_err, 0, "check for unexpected err");

cleanup:
	bpf_iter_task_btf__destroy(skel);
}

static void test_tcp4(void)
{
	struct bpf_iter_tcp4 *skel;

	skel = bpf_iter_tcp4__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_tcp4__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_tcp4);

	bpf_iter_tcp4__destroy(skel);
}

static void test_tcp6(void)
{
	struct bpf_iter_tcp6 *skel;

	skel = bpf_iter_tcp6__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_tcp6__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_tcp6);

	bpf_iter_tcp6__destroy(skel);
}

static void test_udp4(void)
{
	struct bpf_iter_udp4 *skel;

	skel = bpf_iter_udp4__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_udp4__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_udp4);

	bpf_iter_udp4__destroy(skel);
}

static void test_udp6(void)
{
	struct bpf_iter_udp6 *skel;

	skel = bpf_iter_udp6__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_udp6__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_udp6);

	bpf_iter_udp6__destroy(skel);
}

static void test_unix(void)
{
	struct bpf_iter_unix *skel;

	skel = bpf_iter_unix__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_unix__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_unix);

	bpf_iter_unix__destroy(skel);
}

/* The expected string is less than 16 bytes */
static int do_read_with_fd(int iter_fd, const char *expected,
			   bool read_one_char)
{
	int len, read_buf_len, start;
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

	if (!ASSERT_STREQ(buf, expected, "read"))
		return -1;

	return 0;
}

static void test_anon_iter(bool read_one_char)
{
	struct bpf_iter_test_kern1 *skel;
	struct bpf_link *link;
	int iter_fd, err;

	skel = bpf_iter_test_kern1__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_test_kern1__open_and_load"))
		return;

	err = bpf_iter_test_kern1__attach(skel);
	if (!ASSERT_OK(err, "bpf_iter_test_kern1__attach")) {
		goto out;
	}

	link = skel->links.dump_task;
	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
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
	if (!ASSERT_OK_PTR(skel1, "bpf_iter_test_kern1__open_and_load"))
		return;

	link = bpf_program__attach_iter(skel1->progs.dump_task, NULL);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
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
	if (!ASSERT_OK_PTR(skel2, "bpf_iter_test_kern2__open_and_load"))
		goto unlink_path;

	err = bpf_link__update_program(link, skel2->progs.dump_task);
	if (!ASSERT_OK(err, "update_prog"))
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
	__u32 iter_size;
	char *buf;

	skel = bpf_iter_test_kern4__open();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_test_kern4__open"))
		return;

	/* create two maps: bpf program will only do bpf_seq_write
	 * for these two maps. The goal is one map output almost
	 * fills seq_file buffer and then the other will trigger
	 * overflow and needs restart.
	 */
	map1_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, 4, 8, 1, NULL);
	if (CHECK(map1_fd < 0, "bpf_map_create",
		  "map_creation failed: %s\n", strerror(errno)))
		goto out;
	map2_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, NULL, 4, 8, 1, NULL);
	if (CHECK(map2_fd < 0, "bpf_map_create",
		  "map_creation failed: %s\n", strerror(errno)))
		goto free_map1;

	/* bpf_seq_printf kernel buffer is 8 pages, so one map
	 * bpf_seq_write will mostly fill it, and the other map
	 * will partially fill and then trigger overflow and need
	 * bpf_seq_read restart.
	 */
	iter_size = sysconf(_SC_PAGE_SIZE) << 3;

	if (test_e2big_overflow) {
		skel->rodata->print_len = (iter_size + 8) / 8;
		expected_read_len = 2 * (iter_size + 8);
	} else if (!ret1) {
		skel->rodata->print_len = (iter_size - 8) / 8;
		expected_read_len = 2 * (iter_size - 8);
	} else {
		skel->rodata->print_len = 1;
		expected_read_len = 2 * 8;
	}
	skel->rodata->ret1 = ret1;

	if (!ASSERT_OK(bpf_iter_test_kern4__load(skel),
		  "bpf_iter_test_kern4__load"))
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
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto free_map2;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
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

	if (!ASSERT_EQ(total_read_len, expected_read_len, "read"))
		goto free_buf;

	if (!ASSERT_EQ(skel->bss->map1_accessed, 1, "map1_accessed"))
		goto free_buf;

	if (!ASSERT_EQ(skel->bss->map2_accessed, 2, "map2_accessed"))
		goto free_buf;

	ASSERT_EQ(skel->bss->map2_seqnum1, skel->bss->map2_seqnum2, "map2_seqnum");

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

static void test_bpf_hash_map(void)
{
	__u32 expected_key_a = 0, expected_key_b = 0;
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_bpf_hash_map *skel;
	int err, i, len, map_fd, iter_fd;
	union bpf_iter_link_info linfo;
	__u64 val, expected_val = 0;
	struct bpf_link *link;
	struct key_t {
		int a;
		int b;
		int c;
	} key;
	char buf[64];

	skel = bpf_iter_bpf_hash_map__open();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_hash_map__open"))
		return;

	skel->bss->in_test_mode = true;

	err = bpf_iter_bpf_hash_map__load(skel);
	if (!ASSERT_OK(err, "bpf_iter_bpf_hash_map__load"))
		goto out;

	/* iterator with hashmap2 and hashmap3 should fail */
	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = bpf_map__fd(skel->maps.hashmap2);
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.dump_bpf_hash_map, &opts);
	if (!ASSERT_ERR_PTR(link, "attach_iter"))
		goto out;

	linfo.map.map_fd = bpf_map__fd(skel->maps.hashmap3);
	link = bpf_program__attach_iter(skel->progs.dump_bpf_hash_map, &opts);
	if (!ASSERT_ERR_PTR(link, "attach_iter"))
		goto out;

	/* hashmap1 should be good, update map values here */
	map_fd = bpf_map__fd(skel->maps.hashmap1);
	for (i = 0; i < bpf_map__max_entries(skel->maps.hashmap1); i++) {
		key.a = i + 1;
		key.b = i + 2;
		key.c = i + 3;
		val = i + 4;
		expected_key_a += key.a;
		expected_key_b += key.b;
		expected_val += val;

		err = bpf_map_update_elem(map_fd, &key, &val, BPF_ANY);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	/* Sleepable program is prohibited for hash map iterator */
	linfo.map.map_fd = map_fd;
	link = bpf_program__attach_iter(skel->progs.sleepable_dummy_dump, &opts);
	if (!ASSERT_ERR_PTR(link, "attach_sleepable_prog_to_iter"))
		goto out;

	linfo.map.map_fd = map_fd;
	link = bpf_program__attach_iter(skel->progs.dump_bpf_hash_map, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	if (!ASSERT_EQ(skel->bss->key_sum_a, expected_key_a, "key_sum_a"))
		goto close_iter;
	if (!ASSERT_EQ(skel->bss->key_sum_b, expected_key_b, "key_sum_b"))
		goto close_iter;
	if (!ASSERT_EQ(skel->bss->val_sum, expected_val, "val_sum"))
		goto close_iter;

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	bpf_iter_bpf_hash_map__destroy(skel);
}

static void test_bpf_percpu_hash_map(void)
{
	__u32 expected_key_a = 0, expected_key_b = 0;
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_bpf_percpu_hash_map *skel;
	int err, i, j, len, map_fd, iter_fd;
	union bpf_iter_link_info linfo;
	__u32 expected_val = 0;
	struct bpf_link *link;
	struct key_t {
		int a;
		int b;
		int c;
	} key;
	char buf[64];
	void *val;

	skel = bpf_iter_bpf_percpu_hash_map__open();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_percpu_hash_map__open"))
		return;

	skel->rodata->num_cpus = bpf_num_possible_cpus();
	val = malloc(8 * bpf_num_possible_cpus());

	err = bpf_iter_bpf_percpu_hash_map__load(skel);
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_percpu_hash_map__load"))
		goto out;

	/* update map values here */
	map_fd = bpf_map__fd(skel->maps.hashmap1);
	for (i = 0; i < bpf_map__max_entries(skel->maps.hashmap1); i++) {
		key.a = i + 1;
		key.b = i + 2;
		key.c = i + 3;
		expected_key_a += key.a;
		expected_key_b += key.b;

		for (j = 0; j < bpf_num_possible_cpus(); j++) {
			*(__u32 *)(val + j * 8) = i + j;
			expected_val += i + j;
		}

		err = bpf_map_update_elem(map_fd, &key, val, BPF_ANY);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = map_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.dump_bpf_percpu_hash_map, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	if (!ASSERT_EQ(skel->bss->key_sum_a, expected_key_a, "key_sum_a"))
		goto close_iter;
	if (!ASSERT_EQ(skel->bss->key_sum_b, expected_key_b, "key_sum_b"))
		goto close_iter;
	if (!ASSERT_EQ(skel->bss->val_sum, expected_val, "val_sum"))
		goto close_iter;

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	bpf_iter_bpf_percpu_hash_map__destroy(skel);
	free(val);
}

static void test_bpf_array_map(void)
{
	__u64 val, expected_val = 0, res_first_val, first_val = 0;
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	__u32 expected_key = 0, res_first_key;
	struct bpf_iter_bpf_array_map *skel;
	union bpf_iter_link_info linfo;
	int err, i, map_fd, iter_fd;
	struct bpf_link *link;
	char buf[64] = {};
	int len, start;

	skel = bpf_iter_bpf_array_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_array_map__open_and_load"))
		return;

	map_fd = bpf_map__fd(skel->maps.arraymap1);
	for (i = 0; i < bpf_map__max_entries(skel->maps.arraymap1); i++) {
		val = i + 4;
		expected_key += i;
		expected_val += val;

		if (i == 0)
			first_val = val;

		err = bpf_map_update_elem(map_fd, &i, &val, BPF_ANY);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = map_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.dump_bpf_array_map, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* do some tests */
	start = 0;
	while ((len = read(iter_fd, buf + start, sizeof(buf) - start)) > 0)
		start += len;
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	res_first_key = *(__u32 *)buf;
	res_first_val = *(__u64 *)(buf + sizeof(__u32));
	if (CHECK(res_first_key != 0 || res_first_val != first_val,
		  "bpf_seq_write",
		  "seq_write failure: first key %u vs expected 0, "
		  " first value %llu vs expected %llu\n",
		  res_first_key, res_first_val, first_val))
		goto close_iter;

	if (!ASSERT_EQ(skel->bss->key_sum, expected_key, "key_sum"))
		goto close_iter;
	if (!ASSERT_EQ(skel->bss->val_sum, expected_val, "val_sum"))
		goto close_iter;

	for (i = 0; i < bpf_map__max_entries(skel->maps.arraymap1); i++) {
		err = bpf_map_lookup_elem(map_fd, &i, &val);
		if (!ASSERT_OK(err, "map_lookup"))
			goto out;
		if (!ASSERT_EQ(i, val, "invalid_val"))
			goto out;
	}

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	bpf_iter_bpf_array_map__destroy(skel);
}

static void test_bpf_array_map_iter_fd(void)
{
	struct bpf_iter_bpf_array_map *skel;

	skel = bpf_iter_bpf_array_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_array_map__open_and_load"))
		return;

	do_read_map_iter_fd(&skel->skeleton, skel->progs.dump_bpf_array_map,
			    skel->maps.arraymap1);

	bpf_iter_bpf_array_map__destroy(skel);
}

static void test_bpf_percpu_array_map(void)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_bpf_percpu_array_map *skel;
	__u32 expected_key = 0, expected_val = 0;
	union bpf_iter_link_info linfo;
	int err, i, j, map_fd, iter_fd;
	struct bpf_link *link;
	char buf[64];
	void *val;
	int len;

	skel = bpf_iter_bpf_percpu_array_map__open();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_percpu_array_map__open"))
		return;

	skel->rodata->num_cpus = bpf_num_possible_cpus();
	val = malloc(8 * bpf_num_possible_cpus());

	err = bpf_iter_bpf_percpu_array_map__load(skel);
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_percpu_array_map__load"))
		goto out;

	/* update map values here */
	map_fd = bpf_map__fd(skel->maps.arraymap1);
	for (i = 0; i < bpf_map__max_entries(skel->maps.arraymap1); i++) {
		expected_key += i;

		for (j = 0; j < bpf_num_possible_cpus(); j++) {
			*(__u32 *)(val + j * 8) = i + j;
			expected_val += i + j;
		}

		err = bpf_map_update_elem(map_fd, &i, val, BPF_ANY);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = map_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.dump_bpf_percpu_array_map, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	if (!ASSERT_EQ(skel->bss->key_sum, expected_key, "key_sum"))
		goto close_iter;
	if (!ASSERT_EQ(skel->bss->val_sum, expected_val, "val_sum"))
		goto close_iter;

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	bpf_iter_bpf_percpu_array_map__destroy(skel);
	free(val);
}

/* An iterator program deletes all local storage in a map. */
static void test_bpf_sk_storage_delete(void)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_bpf_sk_storage_helpers *skel;
	union bpf_iter_link_info linfo;
	int err, len, map_fd, iter_fd;
	struct bpf_link *link;
	int sock_fd = -1;
	__u32 val = 42;
	char buf[64];

	skel = bpf_iter_bpf_sk_storage_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_sk_storage_helpers__open_and_load"))
		return;

	map_fd = bpf_map__fd(skel->maps.sk_stg_map);

	sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (!ASSERT_GE(sock_fd, 0, "socket"))
		goto out;
	err = bpf_map_update_elem(map_fd, &sock_fd, &val, BPF_NOEXIST);
	if (!ASSERT_OK(err, "map_update"))
		goto out;

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = map_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.delete_bpf_sk_storage_map,
					&opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	err = bpf_map_lookup_elem(map_fd, &sock_fd, &val);
	if (CHECK(!err || errno != ENOENT, "bpf_map_lookup_elem",
		  "map value wasn't deleted (err=%d, errno=%d)\n", err, errno))
		goto close_iter;

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	if (sock_fd >= 0)
		close(sock_fd);
	bpf_iter_bpf_sk_storage_helpers__destroy(skel);
}

/* This creates a socket and its local storage. It then runs a task_iter BPF
 * program that replaces the existing socket local storage with the tgid of the
 * only task owning a file descriptor to this socket, this process, prog_tests.
 * It then runs a tcp socket iterator that negates the value in the existing
 * socket local storage, the test verifies that the resulting value is -pid.
 */
static void test_bpf_sk_storage_get(void)
{
	struct bpf_iter_bpf_sk_storage_helpers *skel;
	int err, map_fd, val = -1;
	int sock_fd = -1;

	skel = bpf_iter_bpf_sk_storage_helpers__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_sk_storage_helpers__open_and_load"))
		return;

	sock_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (!ASSERT_GE(sock_fd, 0, "socket"))
		goto out;

	err = listen(sock_fd, 1);
	if (!ASSERT_OK(err, "listen"))
		goto close_socket;

	map_fd = bpf_map__fd(skel->maps.sk_stg_map);

	err = bpf_map_update_elem(map_fd, &sock_fd, &val, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem"))
		goto close_socket;

	do_dummy_read(skel->progs.fill_socket_owner);

	err = bpf_map_lookup_elem(map_fd, &sock_fd, &val);
	if (CHECK(err || val != getpid(), "bpf_map_lookup_elem",
	    "map value wasn't set correctly (expected %d, got %d, err=%d)\n",
	    getpid(), val, err))
		goto close_socket;

	do_dummy_read(skel->progs.negate_socket_local_storage);

	err = bpf_map_lookup_elem(map_fd, &sock_fd, &val);
	CHECK(err || val != -getpid(), "bpf_map_lookup_elem",
	      "map value wasn't set correctly (expected %d, got %d, err=%d)\n",
	      -getpid(), val, err);

close_socket:
	close(sock_fd);
out:
	bpf_iter_bpf_sk_storage_helpers__destroy(skel);
}

static void test_bpf_sk_stoarge_map_iter_fd(void)
{
	struct bpf_iter_bpf_sk_storage_map *skel;

	skel = bpf_iter_bpf_sk_storage_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_sk_storage_map__open_and_load"))
		return;

	do_read_map_iter_fd(&skel->skeleton, skel->progs.rw_bpf_sk_storage_map,
			    skel->maps.sk_stg_map);

	bpf_iter_bpf_sk_storage_map__destroy(skel);
}

static void test_bpf_sk_storage_map(void)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	int err, i, len, map_fd, iter_fd, num_sockets;
	struct bpf_iter_bpf_sk_storage_map *skel;
	union bpf_iter_link_info linfo;
	int sock_fd[3] = {-1, -1, -1};
	__u32 val, expected_val = 0;
	struct bpf_link *link;
	char buf[64];

	skel = bpf_iter_bpf_sk_storage_map__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_sk_storage_map__open_and_load"))
		return;

	map_fd = bpf_map__fd(skel->maps.sk_stg_map);
	num_sockets = ARRAY_SIZE(sock_fd);
	for (i = 0; i < num_sockets; i++) {
		sock_fd[i] = socket(AF_INET6, SOCK_STREAM, 0);
		if (!ASSERT_GE(sock_fd[i], 0, "socket"))
			goto out;

		val = i + 1;
		expected_val += val;

		err = bpf_map_update_elem(map_fd, &sock_fd[i], &val,
					  BPF_NOEXIST);
		if (!ASSERT_OK(err, "map_update"))
			goto out;
	}

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = map_fd;
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.oob_write_bpf_sk_storage_map, &opts);
	err = libbpf_get_error(link);
	if (!ASSERT_EQ(err, -EACCES, "attach_oob_write_iter")) {
		if (!err)
			bpf_link__destroy(link);
		goto out;
	}

	link = bpf_program__attach_iter(skel->progs.rw_bpf_sk_storage_map, &opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		goto out;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto free_link;

	skel->bss->to_add_val = time(NULL);
	/* do some tests */
	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	if (CHECK(len < 0, "read", "read failed: %s\n", strerror(errno)))
		goto close_iter;

	/* test results */
	if (!ASSERT_EQ(skel->bss->ipv6_sk_count, num_sockets, "ipv6_sk_count"))
		goto close_iter;

	if (!ASSERT_EQ(skel->bss->val_sum, expected_val, "val_sum"))
		goto close_iter;

	for (i = 0; i < num_sockets; i++) {
		err = bpf_map_lookup_elem(map_fd, &sock_fd[i], &val);
		if (!ASSERT_OK(err, "map_lookup") ||
		    !ASSERT_EQ(val, i + 1 + skel->bss->to_add_val, "check_map_value"))
			break;
	}

close_iter:
	close(iter_fd);
free_link:
	bpf_link__destroy(link);
out:
	for (i = 0; i < num_sockets; i++) {
		if (sock_fd[i] >= 0)
			close(sock_fd[i]);
	}
	bpf_iter_bpf_sk_storage_map__destroy(skel);
}

static void test_rdonly_buf_out_of_bound(void)
{
	DECLARE_LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	struct bpf_iter_test_kern5 *skel;
	union bpf_iter_link_info linfo;
	struct bpf_link *link;

	skel = bpf_iter_test_kern5__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_test_kern5__open_and_load"))
		return;

	memset(&linfo, 0, sizeof(linfo));
	linfo.map.map_fd = bpf_map__fd(skel->maps.hashmap1);
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);
	link = bpf_program__attach_iter(skel->progs.dump_bpf_hash_map, &opts);
	if (!ASSERT_ERR_PTR(link, "attach_iter"))
		bpf_link__destroy(link);

	bpf_iter_test_kern5__destroy(skel);
}

static void test_buf_neg_offset(void)
{
	struct bpf_iter_test_kern6 *skel;

	skel = bpf_iter_test_kern6__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "bpf_iter_test_kern6__open_and_load"))
		bpf_iter_test_kern6__destroy(skel);
}

static void test_link_iter(void)
{
	struct bpf_iter_bpf_link *skel;

	skel = bpf_iter_bpf_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_bpf_link__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_bpf_link);

	bpf_iter_bpf_link__destroy(skel);
}

static void test_ksym_iter(void)
{
	struct bpf_iter_ksym *skel;

	skel = bpf_iter_ksym__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_ksym__open_and_load"))
		return;

	do_dummy_read(skel->progs.dump_ksym);

	bpf_iter_ksym__destroy(skel);
}

#define CMP_BUFFER_SIZE 1024
static char task_vma_output[CMP_BUFFER_SIZE];
static char proc_maps_output[CMP_BUFFER_SIZE];

/* remove \0 and \t from str, and only keep the first line */
static void str_strip_first_line(char *str)
{
	char *dst = str, *src = str;

	do {
		if (*src == ' ' || *src == '\t')
			src++;
		else
			*(dst++) = *(src++);

	} while (*src != '\0' && *src != '\n');

	*dst = '\0';
}

static void test_task_vma_common(struct bpf_iter_attach_opts *opts)
{
	int err, iter_fd = -1, proc_maps_fd = -1;
	struct bpf_iter_task_vma *skel;
	int len, read_size = 4;
	char maps_path[64];

	skel = bpf_iter_task_vma__open();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_task_vma__open"))
		return;

	skel->bss->pid = getpid();
	skel->bss->one_task = opts ? 1 : 0;

	err = bpf_iter_task_vma__load(skel);
	if (!ASSERT_OK(err, "bpf_iter_task_vma__load"))
		goto out;

	skel->links.proc_maps = bpf_program__attach_iter(
		skel->progs.proc_maps, opts);

	if (!ASSERT_OK_PTR(skel->links.proc_maps, "bpf_program__attach_iter")) {
		skel->links.proc_maps = NULL;
		goto out;
	}

	iter_fd = bpf_iter_create(bpf_link__fd(skel->links.proc_maps));
	if (!ASSERT_GE(iter_fd, 0, "create_iter"))
		goto out;

	/* Read CMP_BUFFER_SIZE (1kB) from bpf_iter. Read in small chunks
	 * to trigger seq_file corner cases.
	 */
	len = 0;
	while (len < CMP_BUFFER_SIZE) {
		err = read_fd_into_buffer(iter_fd, task_vma_output + len,
					  MIN(read_size, CMP_BUFFER_SIZE - len));
		if (!err)
			break;
		if (!ASSERT_GE(err, 0, "read_iter_fd"))
			goto out;
		len += err;
	}
	if (opts)
		ASSERT_EQ(skel->bss->one_task_error, 0, "unexpected task");

	/* read CMP_BUFFER_SIZE (1kB) from /proc/pid/maps */
	snprintf(maps_path, 64, "/proc/%u/maps", skel->bss->pid);
	proc_maps_fd = open(maps_path, O_RDONLY);
	if (!ASSERT_GE(proc_maps_fd, 0, "open_proc_maps"))
		goto out;
	err = read_fd_into_buffer(proc_maps_fd, proc_maps_output, CMP_BUFFER_SIZE);
	if (!ASSERT_GE(err, 0, "read_prog_maps_fd"))
		goto out;

	/* strip and compare the first line of the two files */
	str_strip_first_line(task_vma_output);
	str_strip_first_line(proc_maps_output);

	ASSERT_STREQ(task_vma_output, proc_maps_output, "compare_output");

	check_bpf_link_info(skel->progs.proc_maps);

out:
	close(proc_maps_fd);
	close(iter_fd);
	bpf_iter_task_vma__destroy(skel);
}

void test_bpf_sockmap_map_iter_fd(void)
{
	struct bpf_iter_sockmap *skel;

	skel = bpf_iter_sockmap__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_sockmap__open_and_load"))
		return;

	do_read_map_iter_fd(&skel->skeleton, skel->progs.copy, skel->maps.sockmap);

	bpf_iter_sockmap__destroy(skel);
}

static void test_task_vma(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.tid = getpid();
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	test_task_vma_common(&opts);
	test_task_vma_common(NULL);
}

/* uprobe attach point */
static noinline int trigger_func(int arg)
{
	asm volatile ("");
	return arg + 1;
}

static void test_task_vma_offset_common(struct bpf_iter_attach_opts *opts, bool one_proc)
{
	struct bpf_iter_vma_offset *skel;
	struct bpf_link *link;
	char buf[16] = {};
	int iter_fd, len;
	int pgsz, shift;

	skel = bpf_iter_vma_offset__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bpf_iter_vma_offset__open_and_load"))
		return;

	skel->bss->pid = getpid();
	skel->bss->address = (uintptr_t)trigger_func;
	for (pgsz = getpagesize(), shift = 0; pgsz > 1; pgsz >>= 1, shift++)
		;
	skel->bss->page_shift = shift;

	link = bpf_program__attach_iter(skel->progs.get_vma_offset, opts);
	if (!ASSERT_OK_PTR(link, "attach_iter"))
		return;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GT(iter_fd, 0, "create_iter"))
		goto exit;

	while ((len = read(iter_fd, buf, sizeof(buf))) > 0)
		;
	buf[15] = 0;
	ASSERT_EQ(strcmp(buf, "OK\n"), 0, "strcmp");

	ASSERT_EQ(skel->bss->offset, get_uprobe_offset(trigger_func), "offset");
	if (one_proc)
		ASSERT_EQ(skel->bss->unique_tgid_cnt, 1, "unique_tgid_count");
	else
		ASSERT_GT(skel->bss->unique_tgid_cnt, 1, "unique_tgid_count");

	close(iter_fd);

exit:
	bpf_link__destroy(link);
}

static void test_task_vma_offset(void)
{
	LIBBPF_OPTS(bpf_iter_attach_opts, opts);
	union bpf_iter_link_info linfo;

	memset(&linfo, 0, sizeof(linfo));
	linfo.task.pid = getpid();
	opts.link_info = &linfo;
	opts.link_info_len = sizeof(linfo);

	test_task_vma_offset_common(&opts, true);

	linfo.task.pid = 0;
	linfo.task.tid = getpid();
	test_task_vma_offset_common(&opts, true);

	test_task_vma_offset_common(NULL, false);
}

void test_bpf_iter(void)
{
	ASSERT_OK(pthread_mutex_init(&do_nothing_mutex, NULL), "pthread_mutex_init");

	if (test__start_subtest("btf_id_or_null"))
		test_btf_id_or_null();
	if (test__start_subtest("ipv6_route"))
		test_ipv6_route();
	if (test__start_subtest("netlink"))
		test_netlink();
	if (test__start_subtest("bpf_map"))
		test_bpf_map();
	if (test__start_subtest("task_tid"))
		test_task_tid();
	if (test__start_subtest("task_pid"))
		test_task_pid();
	if (test__start_subtest("task_pidfd"))
		test_task_pidfd();
	if (test__start_subtest("task_sleepable"))
		test_task_sleepable();
	if (test__start_subtest("task_stack"))
		test_task_stack();
	if (test__start_subtest("task_file"))
		test_task_file();
	if (test__start_subtest("task_vma"))
		test_task_vma();
	if (test__start_subtest("task_btf"))
		test_task_btf();
	if (test__start_subtest("tcp4"))
		test_tcp4();
	if (test__start_subtest("tcp6"))
		test_tcp6();
	if (test__start_subtest("udp4"))
		test_udp4();
	if (test__start_subtest("udp6"))
		test_udp6();
	if (test__start_subtest("unix"))
		test_unix();
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
	if (test__start_subtest("bpf_hash_map"))
		test_bpf_hash_map();
	if (test__start_subtest("bpf_percpu_hash_map"))
		test_bpf_percpu_hash_map();
	if (test__start_subtest("bpf_array_map"))
		test_bpf_array_map();
	if (test__start_subtest("bpf_array_map_iter_fd"))
		test_bpf_array_map_iter_fd();
	if (test__start_subtest("bpf_percpu_array_map"))
		test_bpf_percpu_array_map();
	if (test__start_subtest("bpf_sk_storage_map"))
		test_bpf_sk_storage_map();
	if (test__start_subtest("bpf_sk_storage_map_iter_fd"))
		test_bpf_sk_stoarge_map_iter_fd();
	if (test__start_subtest("bpf_sk_storage_delete"))
		test_bpf_sk_storage_delete();
	if (test__start_subtest("bpf_sk_storage_get"))
		test_bpf_sk_storage_get();
	if (test__start_subtest("rdonly-buf-out-of-bound"))
		test_rdonly_buf_out_of_bound();
	if (test__start_subtest("buf-neg-offset"))
		test_buf_neg_offset();
	if (test__start_subtest("link-iter"))
		test_link_iter();
	if (test__start_subtest("ksym"))
		test_ksym_iter();
	if (test__start_subtest("bpf_sockmap_map_iter_fd"))
		test_bpf_sockmap_map_iter_fd();
	if (test__start_subtest("vma_offset"))
		test_task_vma_offset();
}
