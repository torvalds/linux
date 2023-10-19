// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <test_progs.h>
#include <linux/limits.h>

#include "bprm_opts.skel.h"
#include "network_helpers.h"
#include "task_local_storage_helpers.h"

static const char * const bash_envp[] = { "TMPDIR=shouldnotbeset", NULL };

static int update_storage(int map_fd, int secureexec)
{
	int task_fd, ret = 0;

	task_fd = sys_pidfd_open(getpid(), 0);
	if (task_fd < 0)
		return errno;

	ret = bpf_map_update_elem(map_fd, &task_fd, &secureexec, BPF_NOEXIST);
	if (ret)
		ret = errno;

	close(task_fd);
	return ret;
}

static int run_set_secureexec(int map_fd, int secureexec)
{
	int child_pid, child_status, ret, null_fd;

	child_pid = fork();
	if (child_pid == 0) {
		null_fd = open("/dev/null", O_WRONLY);
		if (null_fd == -1)
			exit(errno);
		dup2(null_fd, STDOUT_FILENO);
		dup2(null_fd, STDERR_FILENO);
		close(null_fd);

		/* Ensure that all executions from hereon are
		 * secure by setting a local storage which is read by
		 * the bprm_creds_for_exec hook and sets bprm->secureexec.
		 */
		ret = update_storage(map_fd, secureexec);
		if (ret)
			exit(ret);

		/* If the binary is executed with securexec=1, the dynamic
		 * loader ingores and unsets certain variables like LD_PRELOAD,
		 * TMPDIR etc. TMPDIR is used here to simplify the example, as
		 * LD_PRELOAD requires a real .so file.
		 *
		 * If the value of TMPDIR is set, the bash command returns 10
		 * and if the value is unset, it returns 20.
		 */
		execle("/bin/bash", "bash", "-c",
		       "[[ -z \"${TMPDIR}\" ]] || exit 10 && exit 20", NULL,
		       bash_envp);
		exit(errno);
	} else if (child_pid > 0) {
		waitpid(child_pid, &child_status, 0);
		ret = WEXITSTATUS(child_status);

		/* If a secureexec occurred, the exit status should be 20 */
		if (secureexec && ret == 20)
			return 0;

		/* If normal execution happened, the exit code should be 10 */
		if (!secureexec && ret == 10)
			return 0;
	}

	return -EINVAL;
}

void test_test_bprm_opts(void)
{
	int err, duration = 0;
	struct bprm_opts *skel = NULL;

	skel = bprm_opts__open_and_load();
	if (CHECK(!skel, "skel_load", "skeleton failed\n"))
		goto close_prog;

	err = bprm_opts__attach(skel);
	if (CHECK(err, "attach", "attach failed: %d\n", err))
		goto close_prog;

	/* Run the test with the secureexec bit unset */
	err = run_set_secureexec(bpf_map__fd(skel->maps.secure_exec_task_map),
				 0 /* secureexec */);
	if (CHECK(err, "run_set_secureexec:0", "err = %d\n", err))
		goto close_prog;

	/* Run the test with the secureexec bit set */
	err = run_set_secureexec(bpf_map__fd(skel->maps.secure_exec_task_map),
				 1 /* secureexec */);
	if (CHECK(err, "run_set_secureexec:1", "err = %d\n", err))
		goto close_prog;

close_prog:
	bprm_opts__destroy(skel);
}
