// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <test_progs.h>
#include <bpf/btf.h>
#include "task_local_storage_helpers.h"
#include "task_local_storage.skel.h"
#include "task_local_storage_exit_creds.skel.h"
#include "task_ls_recursion.skel.h"
#include "task_storage_nodeadlock.skel.h"
#include "uptr_test_common.h"
#include "task_ls_uptr.skel.h"
#include "uptr_update_failure.skel.h"
#include "uptr_failure.skel.h"
#include "uptr_map_failure.skel.h"

static void test_sys_enter_exit(void)
{
	struct task_local_storage *skel;
	int err;

	skel = task_local_storage__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	skel->bss->target_pid = sys_gettid();

	err = task_local_storage__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	sys_gettid();
	sys_gettid();

	/* 3x syscalls: 1x attach and 2x gettid */
	ASSERT_EQ(skel->bss->enter_cnt, 3, "enter_cnt");
	ASSERT_EQ(skel->bss->exit_cnt, 3, "exit_cnt");
	ASSERT_EQ(skel->bss->mismatch_cnt, 0, "mismatch_cnt");
out:
	task_local_storage__destroy(skel);
}

static void test_exit_creds(void)
{
	struct task_local_storage_exit_creds *skel;
	int err, run_count, sync_rcu_calls = 0;
	const int MAX_SYNC_RCU_CALLS = 1000;

	skel = task_local_storage_exit_creds__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = task_local_storage_exit_creds__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* trigger at least one exit_creds() */
	if (CHECK_FAIL(system("ls > /dev/null")))
		goto out;

	/* kern_sync_rcu is not enough on its own as the read section we want
	 * to wait for may start after we enter synchronize_rcu, so our call
	 * won't wait for the section to finish. Loop on the run counter
	 * as well to ensure the program has run.
	 */
	do {
		kern_sync_rcu();
		run_count = __atomic_load_n(&skel->bss->run_count, __ATOMIC_SEQ_CST);
	} while (run_count == 0 && ++sync_rcu_calls < MAX_SYNC_RCU_CALLS);

	ASSERT_NEQ(sync_rcu_calls, MAX_SYNC_RCU_CALLS,
		   "sync_rcu count too high");
	ASSERT_NEQ(run_count, 0, "run_count");
	ASSERT_EQ(skel->bss->valid_ptr_count, 0, "valid_ptr_count");
	ASSERT_NEQ(skel->bss->null_ptr_count, 0, "null_ptr_count");
out:
	task_local_storage_exit_creds__destroy(skel);
}

static void test_recursion(void)
{
	int err, map_fd, prog_fd, task_fd;
	struct task_ls_recursion *skel;
	struct bpf_prog_info info;
	__u32 info_len = sizeof(info);
	long value;

	task_fd = sys_pidfd_open(getpid(), 0);
	if (!ASSERT_NEQ(task_fd, -1, "sys_pidfd_open"))
		return;

	skel = task_ls_recursion__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out;

	err = task_ls_recursion__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* trigger sys_enter, make sure it does not cause deadlock */
	skel->bss->test_pid = getpid();
	sys_gettid();
	skel->bss->test_pid = 0;
	task_ls_recursion__detach(skel);

	/* Refer to the comment in BPF_PROG(on_update) for
	 * the explanation on the value 201 and 100.
	 */
	map_fd = bpf_map__fd(skel->maps.map_a);
	err = bpf_map_lookup_elem(map_fd, &task_fd, &value);
	ASSERT_OK(err, "lookup map_a");
	ASSERT_EQ(value, 201, "map_a value");
	ASSERT_EQ(skel->bss->nr_del_errs, 1, "bpf_task_storage_delete busy");

	map_fd = bpf_map__fd(skel->maps.map_b);
	err = bpf_map_lookup_elem(map_fd, &task_fd, &value);
	ASSERT_OK(err, "lookup map_b");
	ASSERT_EQ(value, 100, "map_b value");

	prog_fd = bpf_program__fd(skel->progs.on_update);
	memset(&info, 0, sizeof(info));
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	ASSERT_OK(err, "get prog info");
	ASSERT_EQ(info.recursion_misses, 0, "on_update prog recursion");

	prog_fd = bpf_program__fd(skel->progs.on_enter);
	memset(&info, 0, sizeof(info));
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	ASSERT_OK(err, "get prog info");
	ASSERT_EQ(info.recursion_misses, 0, "on_enter prog recursion");

out:
	close(task_fd);
	task_ls_recursion__destroy(skel);
}

static bool stop;

static void waitall(const pthread_t *tids, int nr)
{
	int i;

	stop = true;
	for (i = 0; i < nr; i++)
		pthread_join(tids[i], NULL);
}

static void *sock_create_loop(void *arg)
{
	struct task_storage_nodeadlock *skel = arg;
	int fd;

	while (!stop) {
		fd = socket(AF_INET, SOCK_STREAM, 0);
		close(fd);
		if (skel->bss->nr_get_errs || skel->bss->nr_del_errs)
			stop = true;
	}

	return NULL;
}

static void test_nodeadlock(void)
{
	struct task_storage_nodeadlock *skel;
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	const int nr_threads = 32;
	pthread_t tids[nr_threads];
	int i, prog_fd, err;
	cpu_set_t old, new;

	/* Pin all threads to one cpu to increase the chance of preemption
	 * in a sleepable bpf prog.
	 */
	CPU_ZERO(&new);
	CPU_SET(0, &new);
	err = sched_getaffinity(getpid(), sizeof(old), &old);
	if (!ASSERT_OK(err, "getaffinity"))
		return;
	err = sched_setaffinity(getpid(), sizeof(new), &new);
	if (!ASSERT_OK(err, "setaffinity"))
		return;

	skel = task_storage_nodeadlock__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	/* Unnecessary recursion and deadlock detection are reproducible
	 * in the preemptible kernel.
	 */
	if (!skel->kconfig->CONFIG_PREEMPTION) {
		test__skip();
		goto done;
	}

	err = task_storage_nodeadlock__attach(skel);
	ASSERT_OK(err, "attach prog");

	for (i = 0; i < nr_threads; i++) {
		err = pthread_create(&tids[i], NULL, sock_create_loop, skel);
		if (err) {
			/* Only assert once here to avoid excessive
			 * PASS printing during test failure.
			 */
			ASSERT_OK(err, "pthread_create");
			waitall(tids, i);
			goto done;
		}
	}

	/* With 32 threads, 1s is enough to reproduce the issue */
	sleep(1);
	waitall(tids, nr_threads);

	info_len = sizeof(info);
	prog_fd = bpf_program__fd(skel->progs.socket_post_create);
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	ASSERT_OK(err, "get prog info");
	ASSERT_EQ(info.recursion_misses, 0, "prog recursion");

	ASSERT_EQ(skel->bss->nr_get_errs, 0, "bpf_task_storage_get busy");
	ASSERT_EQ(skel->bss->nr_del_errs, 0, "bpf_task_storage_delete busy");

done:
	task_storage_nodeadlock__destroy(skel);
	sched_setaffinity(getpid(), sizeof(old), &old);
}

static struct user_data udata __attribute__((aligned(16))) = {
	.a = 1,
	.b = 2,
};

static struct user_data udata2 __attribute__((aligned(16))) = {
	.a = 3,
	.b = 4,
};

static void check_udata2(int expected)
{
	udata2.result = udata2.nested_result = 0;
	usleep(1);
	ASSERT_EQ(udata2.result, expected, "udata2.result");
	ASSERT_EQ(udata2.nested_result, expected, "udata2.nested_result");
}

static void test_uptr_basic(void)
{
	int map_fd, parent_task_fd, ev_fd;
	struct value_type value = {};
	struct task_ls_uptr *skel;
	pid_t child_pid, my_tid;
	__u64 ev_dummy_data = 1;
	int err;

	my_tid = sys_gettid();
	parent_task_fd = sys_pidfd_open(my_tid, 0);
	if (!ASSERT_OK_FD(parent_task_fd, "parent_task_fd"))
		return;

	ev_fd = eventfd(0, 0);
	if (!ASSERT_OK_FD(ev_fd, "ev_fd")) {
		close(parent_task_fd);
		return;
	}

	skel = task_ls_uptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out;

	map_fd = bpf_map__fd(skel->maps.datamap);
	value.udata = &udata;
	value.nested.udata = &udata;
	err = bpf_map_update_elem(map_fd, &parent_task_fd, &value, BPF_NOEXIST);
	if (!ASSERT_OK(err, "update_elem(udata)"))
		goto out;

	err = task_ls_uptr__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	child_pid = fork();
	if (!ASSERT_NEQ(child_pid, -1, "fork"))
		goto out;

	/* Call syscall in the child process, but access the map value of
	 * the parent process in the BPF program to check if the user kptr
	 * is translated/mapped correctly.
	 */
	if (child_pid == 0) {
		/* child */

		/* Overwrite the user_data in the child process to check if
		 * the BPF program accesses the user_data of the parent.
		 */
		udata.a = 0;
		udata.b = 0;

		/* Wait for the parent to set child_pid */
		read(ev_fd, &ev_dummy_data, sizeof(ev_dummy_data));
		exit(0);
	}

	skel->bss->parent_pid = my_tid;
	skel->bss->target_pid = child_pid;

	write(ev_fd, &ev_dummy_data, sizeof(ev_dummy_data));

	err = waitpid(child_pid, NULL, 0);
	ASSERT_EQ(err, child_pid, "waitpid");
	ASSERT_EQ(udata.result, MAGIC_VALUE + udata.a + udata.b, "udata.result");
	ASSERT_EQ(udata.nested_result, MAGIC_VALUE + udata.a + udata.b, "udata.nested_result");

	skel->bss->target_pid = my_tid;

	/* update_elem: uptr changes from udata1 to udata2 */
	value.udata = &udata2;
	value.nested.udata = &udata2;
	err = bpf_map_update_elem(map_fd, &parent_task_fd, &value, BPF_EXIST);
	if (!ASSERT_OK(err, "update_elem(udata2)"))
		goto out;
	check_udata2(MAGIC_VALUE + udata2.a + udata2.b);

	/* update_elem: uptr changes from udata2 uptr to NULL */
	memset(&value, 0, sizeof(value));
	err = bpf_map_update_elem(map_fd, &parent_task_fd, &value, BPF_EXIST);
	if (!ASSERT_OK(err, "update_elem(udata2)"))
		goto out;
	check_udata2(0);

	/* update_elem: uptr changes from NULL to udata2 */
	value.udata = &udata2;
	value.nested.udata = &udata2;
	err = bpf_map_update_elem(map_fd, &parent_task_fd, &value, BPF_EXIST);
	if (!ASSERT_OK(err, "update_elem(udata2)"))
		goto out;
	check_udata2(MAGIC_VALUE + udata2.a + udata2.b);

	/* Check if user programs can access the value of user kptrs
	 * through bpf_map_lookup_elem(). Make sure the kernel value is not
	 * leaked.
	 */
	err = bpf_map_lookup_elem(map_fd, &parent_task_fd, &value);
	if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
		goto out;
	ASSERT_EQ(value.udata, NULL, "value.udata");
	ASSERT_EQ(value.nested.udata, NULL, "value.nested.udata");

	/* delete_elem */
	err = bpf_map_delete_elem(map_fd, &parent_task_fd);
	ASSERT_OK(err, "delete_elem(udata2)");
	check_udata2(0);

	/* update_elem: add uptr back to test map_free */
	value.udata = &udata2;
	value.nested.udata = &udata2;
	err = bpf_map_update_elem(map_fd, &parent_task_fd, &value, BPF_NOEXIST);
	ASSERT_OK(err, "update_elem(udata2)");

out:
	task_ls_uptr__destroy(skel);
	close(ev_fd);
	close(parent_task_fd);
}

static void test_uptr_across_pages(void)
{
	int page_size = getpagesize();
	struct value_type value = {};
	struct task_ls_uptr *skel;
	int err, task_fd, map_fd;
	void *mem;

	task_fd = sys_pidfd_open(getpid(), 0);
	if (!ASSERT_OK_FD(task_fd, "task_fd"))
		return;

	mem = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!ASSERT_OK_PTR(mem, "mmap(page_size * 2)")) {
		close(task_fd);
		return;
	}

	skel = task_ls_uptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out;

	map_fd = bpf_map__fd(skel->maps.datamap);
	value.udata = mem + page_size - offsetof(struct user_data, b);
	err = bpf_map_update_elem(map_fd, &task_fd, &value, 0);
	if (!ASSERT_ERR(err, "update_elem(udata)"))
		goto out;
	ASSERT_EQ(errno, EOPNOTSUPP, "errno");

	value.udata = mem + page_size - sizeof(struct user_data);
	err = bpf_map_update_elem(map_fd, &task_fd, &value, 0);
	ASSERT_OK(err, "update_elem(udata)");

out:
	task_ls_uptr__destroy(skel);
	close(task_fd);
	munmap(mem, page_size * 2);
}

static void test_uptr_update_failure(void)
{
	struct value_lock_type value = {};
	struct uptr_update_failure *skel;
	int err, task_fd, map_fd;

	task_fd = sys_pidfd_open(getpid(), 0);
	if (!ASSERT_OK_FD(task_fd, "task_fd"))
		return;

	skel = uptr_update_failure__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out;

	map_fd = bpf_map__fd(skel->maps.datamap);

	value.udata = &udata;
	err = bpf_map_update_elem(map_fd, &task_fd, &value, BPF_F_LOCK);
	if (!ASSERT_ERR(err, "update_elem(udata, BPF_F_LOCK)"))
		goto out;
	ASSERT_EQ(errno, EOPNOTSUPP, "errno");

	err = bpf_map_update_elem(map_fd, &task_fd, &value, BPF_EXIST);
	if (!ASSERT_ERR(err, "update_elem(udata, BPF_EXIST)"))
		goto out;
	ASSERT_EQ(errno, ENOENT, "errno");

	err = bpf_map_update_elem(map_fd, &task_fd, &value, BPF_NOEXIST);
	if (!ASSERT_OK(err, "update_elem(udata, BPF_NOEXIST)"))
		goto out;

	value.udata = &udata2;
	err = bpf_map_update_elem(map_fd, &task_fd, &value, BPF_NOEXIST);
	if (!ASSERT_ERR(err, "update_elem(udata2, BPF_NOEXIST)"))
		goto out;
	ASSERT_EQ(errno, EEXIST, "errno");

out:
	uptr_update_failure__destroy(skel);
	close(task_fd);
}

static void test_uptr_map_failure(const char *map_name, int expected_errno)
{
	LIBBPF_OPTS(bpf_map_create_opts, create_attr);
	struct uptr_map_failure *skel;
	struct bpf_map *map;
	struct btf *btf;
	int map_fd, err;

	skel = uptr_map_failure__open();
	if (!ASSERT_OK_PTR(skel, "uptr_map_failure__open"))
		return;

	map = bpf_object__find_map_by_name(skel->obj, map_name);
	btf = bpf_object__btf(skel->obj);
	err = btf__load_into_kernel(btf);
	if (!ASSERT_OK(err, "btf__load_into_kernel"))
		goto done;

	create_attr.map_flags = bpf_map__map_flags(map);
	create_attr.btf_fd = btf__fd(btf);
	create_attr.btf_key_type_id = bpf_map__btf_key_type_id(map);
	create_attr.btf_value_type_id = bpf_map__btf_value_type_id(map);
	map_fd = bpf_map_create(bpf_map__type(map), map_name,
				bpf_map__key_size(map), bpf_map__value_size(map),
				0, &create_attr);
	if (ASSERT_ERR_FD(map_fd, "map_create"))
		ASSERT_EQ(errno, expected_errno, "errno");
	else
		close(map_fd);

done:
	uptr_map_failure__destroy(skel);
}

void test_task_local_storage(void)
{
	if (test__start_subtest("sys_enter_exit"))
		test_sys_enter_exit();
	if (test__start_subtest("exit_creds"))
		test_exit_creds();
	if (test__start_subtest("recursion"))
		test_recursion();
	if (test__start_subtest("nodeadlock"))
		test_nodeadlock();
	if (test__start_subtest("uptr_basic"))
		test_uptr_basic();
	if (test__start_subtest("uptr_across_pages"))
		test_uptr_across_pages();
	if (test__start_subtest("uptr_update_failure"))
		test_uptr_update_failure();
	if (test__start_subtest("uptr_map_failure_e2big")) {
		if (getpagesize() == PAGE_SIZE)
			test_uptr_map_failure("large_uptr_map", E2BIG);
		else
			test__skip();
	}
	if (test__start_subtest("uptr_map_failure_size0"))
		test_uptr_map_failure("empty_uptr_map", EINVAL);
	if (test__start_subtest("uptr_map_failure_kstruct"))
		test_uptr_map_failure("kstruct_uptr_map", EINVAL);
	RUN_TESTS(uptr_failure);
}
