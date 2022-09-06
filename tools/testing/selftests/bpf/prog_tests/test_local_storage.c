// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <asm-generic/errno-base.h>
#include <sys/stat.h>
#include <test_progs.h>
#include <linux/limits.h>

#include "local_storage.skel.h"
#include "network_helpers.h"
#include "task_local_storage_helpers.h"

static unsigned int duration;

#define TEST_STORAGE_VALUE 0xbeefdead

struct storage {
	void *inode;
	unsigned int value;
};

/* Fork and exec the provided rm binary and return the exit code of the
 * forked process and its pid.
 */
static int run_self_unlink(int *monitored_pid, const char *rm_path)
{
	int child_pid, child_status, ret;
	int null_fd;

	child_pid = fork();
	if (child_pid == 0) {
		null_fd = open("/dev/null", O_WRONLY);
		dup2(null_fd, STDOUT_FILENO);
		dup2(null_fd, STDERR_FILENO);
		close(null_fd);

		*monitored_pid = getpid();
		/* Use the copied /usr/bin/rm to delete itself
		 * /tmp/copy_of_rm /tmp/copy_of_rm.
		 */
		ret = execlp(rm_path, rm_path, rm_path, NULL);
		if (ret)
			exit(errno);
	} else if (child_pid > 0) {
		waitpid(child_pid, &child_status, 0);
		return WEXITSTATUS(child_status);
	}

	return -EINVAL;
}

static bool check_syscall_operations(int map_fd, int obj_fd)
{
	struct storage val = { .value = TEST_STORAGE_VALUE },
		       lookup_val = { .value = 0 };
	int err;

	/* Looking up an existing element should fail initially */
	err = bpf_map_lookup_elem_flags(map_fd, &obj_fd, &lookup_val, 0);
	if (CHECK(!err || errno != ENOENT, "bpf_map_lookup_elem",
		  "err:%d errno:%d\n", err, errno))
		return false;

	/* Create a new element */
	err = bpf_map_update_elem(map_fd, &obj_fd, &val, BPF_NOEXIST);
	if (CHECK(err < 0, "bpf_map_update_elem", "err:%d errno:%d\n", err,
		  errno))
		return false;

	/* Lookup the newly created element */
	err = bpf_map_lookup_elem_flags(map_fd, &obj_fd, &lookup_val, 0);
	if (CHECK(err < 0, "bpf_map_lookup_elem", "err:%d errno:%d", err,
		  errno))
		return false;

	/* Check the value of the newly created element */
	if (CHECK(lookup_val.value != val.value, "bpf_map_lookup_elem",
		  "value got = %x errno:%d", lookup_val.value, val.value))
		return false;

	err = bpf_map_delete_elem(map_fd, &obj_fd);
	if (CHECK(err, "bpf_map_delete_elem()", "err:%d errno:%d\n", err,
		  errno))
		return false;

	/* The lookup should fail, now that the element has been deleted */
	err = bpf_map_lookup_elem_flags(map_fd, &obj_fd, &lookup_val, 0);
	if (CHECK(!err || errno != ENOENT, "bpf_map_lookup_elem",
		  "err:%d errno:%d\n", err, errno))
		return false;

	return true;
}

void test_test_local_storage(void)
{
	char tmp_dir_path[] = "/tmp/local_storageXXXXXX";
	int err, serv_sk = -1, task_fd = -1, rm_fd = -1;
	struct local_storage *skel = NULL;
	char tmp_exec_path[64];
	char cmd[256];

	skel = local_storage__open_and_load();
	if (CHECK(!skel, "skel_load", "lsm skeleton failed\n"))
		goto close_prog;

	err = local_storage__attach(skel);
	if (CHECK(err, "attach", "lsm attach failed: %d\n", err))
		goto close_prog;

	task_fd = sys_pidfd_open(getpid(), 0);
	if (CHECK(task_fd < 0, "pidfd_open",
		  "failed to get pidfd err:%d, errno:%d", task_fd, errno))
		goto close_prog;

	if (!check_syscall_operations(bpf_map__fd(skel->maps.task_storage_map),
				      task_fd))
		goto close_prog;

	if (CHECK(!mkdtemp(tmp_dir_path), "mkdtemp",
		  "unable to create tmpdir: %d\n", errno))
		goto close_prog;

	snprintf(tmp_exec_path, sizeof(tmp_exec_path), "%s/copy_of_rm",
		 tmp_dir_path);
	snprintf(cmd, sizeof(cmd), "cp /bin/rm %s", tmp_exec_path);
	if (CHECK_FAIL(system(cmd)))
		goto close_prog_rmdir;

	rm_fd = open(tmp_exec_path, O_RDONLY);
	if (CHECK(rm_fd < 0, "open", "failed to open %s err:%d, errno:%d",
		  tmp_exec_path, rm_fd, errno))
		goto close_prog_rmdir;

	if (!check_syscall_operations(bpf_map__fd(skel->maps.inode_storage_map),
				      rm_fd))
		goto close_prog_rmdir;

	/* Sets skel->bss->monitored_pid to the pid of the forked child
	 * forks a child process that executes tmp_exec_path and tries to
	 * unlink its executable. This operation should be denied by the loaded
	 * LSM program.
	 */
	err = run_self_unlink(&skel->bss->monitored_pid, tmp_exec_path);
	if (CHECK(err != EPERM, "run_self_unlink", "err %d want EPERM\n", err))
		goto close_prog_rmdir;

	/* Set the process being monitored to be the current process */
	skel->bss->monitored_pid = getpid();

	/* Move copy_of_rm to a new location so that it triggers the
	 * inode_rename LSM hook with a new_dentry that has a NULL inode ptr.
	 */
	snprintf(cmd, sizeof(cmd), "mv %s/copy_of_rm %s/check_null_ptr",
		 tmp_dir_path, tmp_dir_path);
	if (CHECK_FAIL(system(cmd)))
		goto close_prog_rmdir;

	CHECK(skel->data->inode_storage_result != 0, "inode_storage_result",
	      "inode_local_storage not set\n");

	serv_sk = start_server(AF_INET6, SOCK_STREAM, NULL, 0, 0);
	if (CHECK(serv_sk < 0, "start_server", "failed to start server\n"))
		goto close_prog_rmdir;

	CHECK(skel->data->sk_storage_result != 0, "sk_storage_result",
	      "sk_local_storage not set\n");

	if (!check_syscall_operations(bpf_map__fd(skel->maps.sk_storage_map),
				      serv_sk))
		goto close_prog_rmdir;

close_prog_rmdir:
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_dir_path);
	system(cmd);
close_prog:
	close(serv_sk);
	close(rm_fd);
	close(task_fd);
	local_storage__destroy(skel);
}
