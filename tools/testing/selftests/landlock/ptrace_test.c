// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Ptrace
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"

static void create_domain(struct __test_metadata *const _metadata)
{
	int ruleset_fd;
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_MAKE_BLOCK,
	};

	ruleset_fd = landlock_create_ruleset(&ruleset_attr,
			sizeof(ruleset_attr), 0);
	EXPECT_LE(0, ruleset_fd) {
		TH_LOG("Failed to create a ruleset: %s", strerror(errno));
	}
	EXPECT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	EXPECT_EQ(0, landlock_restrict_self(ruleset_fd, 0));
	EXPECT_EQ(0, close(ruleset_fd));
}

static int test_ptrace_read(const pid_t pid)
{
	static const char path_template[] = "/proc/%d/environ";
	char procenv_path[sizeof(path_template) + 10];
	int procenv_path_size, fd;

	procenv_path_size = snprintf(procenv_path, sizeof(procenv_path),
			path_template, pid);
	if (procenv_path_size >= sizeof(procenv_path))
		return E2BIG;

	fd = open(procenv_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return errno;
	/*
	 * Mixing error codes from close(2) and open(2) should not lead to any
	 * (access type) confusion for this test.
	 */
	if (close(fd) != 0)
		return errno;
	return 0;
}

/* clang-format off */
FIXTURE(hierarchy) {};
/* clang-format on */

FIXTURE_VARIANT(hierarchy) {
	const bool domain_both;
	const bool domain_parent;
	const bool domain_child;
};

/*
 * Test multiple tracing combinations between a parent process P1 and a child
 * process P2.
 *
 * Yama's scoped ptrace is presumed disabled.  If enabled, this optional
 * restriction is enforced in addition to any Landlock check, which means that
 * all P2 requests to trace P1 would be denied.
 */

/*
 *        No domain
 *
 *   P1-.               P1 -> P2 : allow
 *       \              P2 -> P1 : allow
 *        'P2
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, allow_without_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = false,
	.domain_child = false,
};

/*
 *        Child domain
 *
 *   P1--.              P1 -> P2 : allow
 *        \             P2 -> P1 : deny
 *        .'-----.
 *        |  P2  |
 *        '------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, allow_with_one_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = false,
	.domain_child = true,
};

/*
 *        Parent domain
 * .------.
 * |  P1  --.           P1 -> P2 : deny
 * '------'  \          P2 -> P1 : allow
 *            '
 *            P2
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, deny_with_parent_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = true,
	.domain_child = false,
};

/*
 *        Parent + child domain (siblings)
 * .------.
 * |  P1  ---.          P1 -> P2 : deny
 * '------'   \         P2 -> P1 : deny
 *         .---'--.
 *         |  P2  |
 *         '------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, deny_with_sibling_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = true,
	.domain_child = true,
};

/*
 *         Same domain (inherited)
 * .-------------.
 * | P1----.     |      P1 -> P2 : allow
 * |        \    |      P2 -> P1 : allow
 * |         '   |
 * |         P2  |
 * '-------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, allow_sibling_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = false,
	.domain_child = false,
};

/*
 *         Inherited + child domain
 * .-----------------.
 * |  P1----.        |  P1 -> P2 : allow
 * |         \       |  P2 -> P1 : deny
 * |        .-'----. |
 * |        |  P2  | |
 * |        '------' |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, allow_with_nested_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = false,
	.domain_child = true,
};

/*
 *         Inherited + parent domain
 * .-----------------.
 * |.------.         |  P1 -> P2 : deny
 * ||  P1  ----.     |  P2 -> P1 : allow
 * |'------'    \    |
 * |             '   |
 * |             P2  |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, deny_with_nested_and_parent_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = true,
	.domain_child = false,
};

/*
 *         Inherited + parent and child domain (siblings)
 * .-----------------.
 * | .------.        |  P1 -> P2 : deny
 * | |  P1  .        |  P2 -> P1 : deny
 * | '------'\       |
 * |          \      |
 * |        .--'---. |
 * |        |  P2  | |
 * |        '------' |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(hierarchy, deny_with_forked_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = true,
	.domain_child = true,
};

FIXTURE_SETUP(hierarchy)
{ }

FIXTURE_TEARDOWN(hierarchy)
{ }

/* Test PTRACE_TRACEME and PTRACE_ATTACH for parent and child. */
TEST_F(hierarchy, trace)
{
	pid_t child, parent;
	int status, err_proc_read;
	int pipe_child[2], pipe_parent[2];
	char buf_parent;
	long ret;

	/*
	 * Removes all effective and permitted capabilities to not interfere
	 * with cap_ptrace_access_check() in case of PTRACE_MODE_FSCREDS.
	 */
	drop_caps(_metadata);

	parent = getpid();
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	if (variant->domain_both) {
		create_domain(_metadata);
		if (!_metadata->passed)
			/* Aborts before forking. */
			return;
	}

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		char buf_child;

		ASSERT_EQ(0, close(pipe_parent[1]));
		ASSERT_EQ(0, close(pipe_child[0]));
		if (variant->domain_child)
			create_domain(_metadata);

		/* Waits for the parent to be in a domain, if any. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));

		/* Tests PTRACE_ATTACH and PTRACE_MODE_READ on the parent. */
		err_proc_read = test_ptrace_read(parent);
		ret = ptrace(PTRACE_ATTACH, parent, NULL, 0);
		if (variant->domain_child) {
			EXPECT_EQ(-1, ret);
			EXPECT_EQ(EPERM, errno);
			EXPECT_EQ(EACCES, err_proc_read);
		} else {
			EXPECT_EQ(0, ret);
			EXPECT_EQ(0, err_proc_read);
		}
		if (ret == 0) {
			ASSERT_EQ(parent, waitpid(parent, &status, 0));
			ASSERT_EQ(1, WIFSTOPPED(status));
			ASSERT_EQ(0, ptrace(PTRACE_DETACH, parent, NULL, 0));
		}

		/* Tests child PTRACE_TRACEME. */
		ret = ptrace(PTRACE_TRACEME);
		if (variant->domain_parent) {
			EXPECT_EQ(-1, ret);
			EXPECT_EQ(EPERM, errno);
		} else {
			EXPECT_EQ(0, ret);
		}

		/*
		 * Signals that the PTRACE_ATTACH test is done and the
		 * PTRACE_TRACEME test is ongoing.
		 */
		ASSERT_EQ(1, write(pipe_child[1], ".", 1));

		if (!variant->domain_parent) {
			ASSERT_EQ(0, raise(SIGSTOP));
		}

		/* Waits for the parent PTRACE_ATTACH test. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));
		_exit(_metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
		return;
	}

	ASSERT_EQ(0, close(pipe_child[1]));
	ASSERT_EQ(0, close(pipe_parent[0]));
	if (variant->domain_parent)
		create_domain(_metadata);

	/* Signals that the parent is in a domain, if any. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	/*
	 * Waits for the child to test PTRACE_ATTACH on the parent and start
	 * testing PTRACE_TRACEME.
	 */
	ASSERT_EQ(1, read(pipe_child[0], &buf_parent, 1));

	/* Tests child PTRACE_TRACEME. */
	if (!variant->domain_parent) {
		ASSERT_EQ(child, waitpid(child, &status, 0));
		ASSERT_EQ(1, WIFSTOPPED(status));
		ASSERT_EQ(0, ptrace(PTRACE_DETACH, child, NULL, 0));
	} else {
		/* The child should not be traced by the parent. */
		EXPECT_EQ(-1, ptrace(PTRACE_DETACH, child, NULL, 0));
		EXPECT_EQ(ESRCH, errno);
	}

	/* Tests PTRACE_ATTACH and PTRACE_MODE_READ on the child. */
	err_proc_read = test_ptrace_read(child);
	ret = ptrace(PTRACE_ATTACH, child, NULL, 0);
	if (variant->domain_parent) {
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EPERM, errno);
		EXPECT_EQ(EACCES, err_proc_read);
	} else {
		EXPECT_EQ(0, ret);
		EXPECT_EQ(0, err_proc_read);
	}
	if (ret == 0) {
		ASSERT_EQ(child, waitpid(child, &status, 0));
		ASSERT_EQ(1, WIFSTOPPED(status));
		ASSERT_EQ(0, ptrace(PTRACE_DETACH, child, NULL, 0));
	}

	/* Signals that the parent PTRACE_ATTACH test is done. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
			WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->passed = 0;
}

TEST_HARNESS_MAIN
