// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#include <test_progs.h>

#include "copy_from_user_bprm.skel.h"

void test_copy_from_user_bprm(void)
{
	char data[] = "first\0second-argument\0SOME_ENV=a\0OTHER_ENV=something";
	char arg0[] = "first";
	char arg1[] = "second-argument";
	char env0[] = "SOME_ENV=a";
	char env1[] = "OTHER_ENV=something";
	struct copy_from_user_bprm *skel;
	pid_t child;
	int status;

	skel = copy_from_user_bprm__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	if (!ASSERT_OK(copy_from_user_bprm__attach(skel), "attach"))
		goto out;

	child = fork();
	if (!ASSERT_GE(child, 0, "fork"))
		goto out;

	if (!child) {
		char *const argv[] = { arg0, arg1, NULL };
		char *const envp[] = { env0, env1, NULL };

		skel->bss->monitored_pid = getpid();
		execvpe("true", argv, envp);
		_exit(errno);
	}

	if (!ASSERT_EQ(waitpid(child, &status, 0), child, "waitpid"))
		goto out;

	if (ASSERT_TRUE(WIFEXITED(status), "child_exited"))
		ASSERT_EQ(WEXITSTATUS(status), EPERM, "exec_errno");

	ASSERT_EQ(skel->bss->bprm_argc, 2, "bprm_argc");
	ASSERT_EQ(skel->bss->bprm_envc, 2, "bprm_envc");
	ASSERT_EQ(skel->bss->data_len, sizeof(data), "data_len");
	ASSERT_EQ(skel->bss->invalid_flags_ret, -EINVAL, "invalid_flags_ret");
	ASSERT_EQ(skel->bss->copy_ret, 0, "copy_ret");
	ASSERT_EQ(skel->bss->arg0_ret, sizeof(arg0), "arg0_ret");
	ASSERT_EQ(skel->bss->arg1_ret, sizeof(arg1), "arg1_ret");
	ASSERT_EQ(skel->bss->env0_ret, sizeof(env0), "env0_ret");
	ASSERT_EQ(skel->bss->env1_ret, sizeof(env1), "env1_ret");
	ASSERT_EQ(memcmp(skel->bss->data, data, sizeof(data)), 0, "data");
	ASSERT_EQ(memcmp(skel->bss->arg0, arg0, sizeof(arg0)), 0, "arg0");
	ASSERT_EQ(memcmp(skel->bss->arg1, arg1, sizeof(arg1)), 0, "arg1");
	ASSERT_EQ(memcmp(skel->bss->env0, env0, sizeof(env0)), 0, "env0");
	ASSERT_EQ(memcmp(skel->bss->env1, env1, sizeof(env1)), 0, "env1");

out:
	copy_from_user_bprm__destroy(skel);
}
