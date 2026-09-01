// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Ptrace
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "audit.h"
#include "common.h"
#include "trace.h"

/* Copied from security/yama/yama_lsm.c */
#define YAMA_SCOPE_DISABLED 0
#define YAMA_SCOPE_RELATIONAL 1

static void create_domain(struct __test_metadata *const _metadata)
{
	int ruleset_fd;
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_MAKE_BLOCK,
	};

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	EXPECT_LE(0, ruleset_fd)
	{
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

static int get_yama_ptrace_scope(void)
{
	int ret;
	char buf[2] = {};
	const int fd = open("/proc/sys/kernel/yama/ptrace_scope", O_RDONLY);

	if (fd < 0)
		return 0;

	if (read(fd, buf, 1) < 0) {
		close(fd);
		return -1;
	}

	ret = atoi(buf);
	close(fd);
	return ret;
}

/* clang-format off */
FIXTURE(scoped_domains) {};
/* clang-format on */

/*
 * Test multiple tracing combinations between a parent process P1 and a child
 * process P2.
 *
 * Yama's scoped ptrace is presumed disabled.  If enabled, this optional
 * restriction is enforced in addition to any Landlock check, which means that
 * all P2 requests to trace P1 would be denied.
 */
#include "scoped_base_variants.h"

FIXTURE_SETUP(scoped_domains)
{
}

FIXTURE_TEARDOWN(scoped_domains)
{
}

/* Test PTRACE_TRACEME and PTRACE_ATTACH for parent and child. */
TEST_F(scoped_domains, trace)
{
	pid_t child, parent;
	int status, err_proc_read;
	int pipe_child[2], pipe_parent[2];
	int yama_ptrace_scope;
	char buf_parent;
	long ret;
	bool can_read_child, can_trace_child, can_read_parent, can_trace_parent;

	yama_ptrace_scope = get_yama_ptrace_scope();
	ASSERT_LE(0, yama_ptrace_scope);

	if (yama_ptrace_scope > YAMA_SCOPE_DISABLED)
		TH_LOG("Incomplete tests due to Yama restrictions (scope %d)",
		       yama_ptrace_scope);

	/*
	 * can_read_child is true if a parent process can read its child
	 * process, which is only the case when the parent process is not
	 * isolated from the child with a dedicated Landlock domain.
	 */
	can_read_child = !variant->domain_parent;

	/*
	 * can_trace_child is true if a parent process can trace its child
	 * process.  This depends on two conditions:
	 * - The parent process is not isolated from the child with a dedicated
	 *   Landlock domain.
	 * - Yama allows tracing children (up to YAMA_SCOPE_RELATIONAL).
	 */
	can_trace_child = can_read_child &&
			  yama_ptrace_scope <= YAMA_SCOPE_RELATIONAL;

	/*
	 * can_read_parent is true if a child process can read its parent
	 * process, which is only the case when the child process is not
	 * isolated from the parent with a dedicated Landlock domain.
	 */
	can_read_parent = !variant->domain_child;

	/*
	 * can_trace_parent is true if a child process can trace its parent
	 * process.  This depends on two conditions:
	 * - The child process is not isolated from the parent with a dedicated
	 *   Landlock domain.
	 * - Yama is disabled (YAMA_SCOPE_DISABLED).
	 */
	can_trace_parent = can_read_parent &&
			   yama_ptrace_scope <= YAMA_SCOPE_DISABLED;

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
		if (!__test_passed(_metadata))
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

		/* Tests PTRACE_MODE_READ on the parent. */
		err_proc_read = test_ptrace_read(parent);
		if (can_read_parent) {
			EXPECT_EQ(0, err_proc_read);
		} else {
			EXPECT_EQ(EACCES, err_proc_read);
		}

		/* Tests PTRACE_ATTACH on the parent. */
		ret = ptrace(PTRACE_ATTACH, parent, NULL, 0);
		if (can_trace_parent) {
			EXPECT_EQ(0, ret);
		} else {
			EXPECT_EQ(-1, ret);
			EXPECT_EQ(EPERM, errno);
		}
		if (ret == 0) {
			ASSERT_EQ(parent, waitpid(parent, &status, 0));
			ASSERT_EQ(1, WIFSTOPPED(status));
			ASSERT_EQ(0, ptrace(PTRACE_DETACH, parent, NULL, 0));
		}

		/* Tests child PTRACE_TRACEME. */
		ret = ptrace(PTRACE_TRACEME);
		if (can_trace_child) {
			EXPECT_EQ(0, ret);
		} else {
			EXPECT_EQ(-1, ret);
			EXPECT_EQ(EPERM, errno);
		}

		/*
		 * Signals that the PTRACE_ATTACH test is done and the
		 * PTRACE_TRACEME test is ongoing.
		 */
		ASSERT_EQ(1, write(pipe_child[1], ".", 1));

		if (can_trace_child) {
			ASSERT_EQ(0, raise(SIGSTOP));
		}

		/* Waits for the parent PTRACE_ATTACH test. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));
		_exit(_metadata->exit_code);
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
	if (can_trace_child) {
		ASSERT_EQ(child, waitpid(child, &status, 0));
		ASSERT_EQ(1, WIFSTOPPED(status));
		ASSERT_EQ(0, ptrace(PTRACE_DETACH, child, NULL, 0));
	} else {
		/* The child should not be traced by the parent. */
		EXPECT_EQ(-1, ptrace(PTRACE_DETACH, child, NULL, 0));
		EXPECT_EQ(ESRCH, errno);
	}

	/* Tests PTRACE_MODE_READ on the child. */
	err_proc_read = test_ptrace_read(child);
	if (can_read_child) {
		EXPECT_EQ(0, err_proc_read);
	} else {
		EXPECT_EQ(EACCES, err_proc_read);
	}

	/* Tests PTRACE_ATTACH on the child. */
	ret = ptrace(PTRACE_ATTACH, child, NULL, 0);
	if (can_trace_child) {
		EXPECT_EQ(0, ret);
	} else {
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EPERM, errno);
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
		_metadata->exit_code = KSFT_FAIL;
}

static int matches_log_ptrace(struct __test_metadata *const _metadata,
			      int audit_fd, const pid_t opid)
{
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" blockers=ptrace opid=%d ocomm=\"ptrace_test\"$";
	char log_match[sizeof(log_template) + 10];
	int log_match_len;

	log_match_len =
		snprintf(log_match, sizeof(log_match), log_template, opid);
	if (log_match_len > sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_ACCESS, log_match,
				  NULL);
}

FIXTURE(audit)
{
	struct audit_filter audit_filter;
	int audit_fd;
};

FIXTURE_SETUP(audit)
{
	disable_caps(_metadata);
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	self->audit_fd = audit_init_with_exe_filter(&self->audit_filter);
	EXPECT_LE(0, self->audit_fd);
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

FIXTURE_TEARDOWN_PARENT(audit)
{
	EXPECT_EQ(0, audit_cleanup(-1, NULL));
}

/* Test PTRACE_TRACEME and PTRACE_ATTACH for parent and child. */
TEST_F(audit, trace)
{
	pid_t child;
	int status;
	int pipe_child[2], pipe_parent[2];
	int yama_ptrace_scope;
	char buf_parent;
	struct audit_records records;

	/* Makes sure there is no superfluous logged records. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);

	yama_ptrace_scope = get_yama_ptrace_scope();
	ASSERT_LE(0, yama_ptrace_scope);

	if (yama_ptrace_scope > YAMA_SCOPE_DISABLED)
		TH_LOG("Incomplete tests due to Yama restrictions (scope %d)",
		       yama_ptrace_scope);

	/*
	 * Removes all effective and permitted capabilities to not interfere
	 * with cap_ptrace_access_check() in case of PTRACE_MODE_FSCREDS.
	 */
	drop_caps(_metadata);

	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		char buf_child;

		ASSERT_EQ(0, close(pipe_parent[1]));
		ASSERT_EQ(0, close(pipe_child[0]));

		/* Waits for the parent to be in a domain, if any. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));

		/* Tests child PTRACE_TRACEME. */
		EXPECT_EQ(-1, ptrace(PTRACE_TRACEME));
		EXPECT_EQ(EPERM, errno);
		/* We should see the child process. */
		EXPECT_EQ(0, matches_log_ptrace(_metadata, self->audit_fd,
						getpid()));

		EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
		EXPECT_EQ(0, records.access);
		/* Checks for a domain creation. */
		EXPECT_EQ(1, records.domain);

		/*
		 * Signals that the PTRACE_ATTACH test is done and the
		 * PTRACE_TRACEME test is ongoing.
		 */
		ASSERT_EQ(1, write(pipe_child[1], ".", 1));

		/* Waits for the parent PTRACE_ATTACH test. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf_child, 1));
		_exit(_metadata->exit_code);
		return;
	}

	ASSERT_EQ(0, close(pipe_child[1]));
	ASSERT_EQ(0, close(pipe_parent[0]));
	create_domain(_metadata);

	/* Signals that the parent is in a domain. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));

	/*
	 * Waits for the child to test PTRACE_ATTACH on the parent and start
	 * testing PTRACE_TRACEME.
	 */
	ASSERT_EQ(1, read(pipe_child[0], &buf_parent, 1));

	/* The child should not be traced by the parent. */
	EXPECT_EQ(-1, ptrace(PTRACE_DETACH, child, NULL, 0));
	EXPECT_EQ(ESRCH, errno);

	/* Tests PTRACE_ATTACH on the child. */
	EXPECT_EQ(-1, ptrace(PTRACE_ATTACH, child, NULL, 0));
	EXPECT_EQ(EPERM, errno);
	EXPECT_EQ(0, matches_log_ptrace(_metadata, self->audit_fd, child));

	/* Signals that the parent PTRACE_ATTACH test is done. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;

	/* Makes sure there is no superfluous logged records. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);
}

/* Trace tests */

/* clang-format off */
FIXTURE(trace_ptrace) {
	/* clang-format on */
	int tracefs_ok;
};

FIXTURE_SETUP(trace_ptrace)
{
	int ret;

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWNS));
	ASSERT_EQ(0, mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL));

	ret = tracefs_fixture_setup();
	if (ret) {
		clear_cap(_metadata, CAP_SYS_ADMIN);
		self->tracefs_ok = 0;
		SKIP(return, "tracefs not available");
	}
	self->tracefs_ok = 1;

	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_DENY_PTRACE_ENABLE, true));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(trace_ptrace)
{
	if (!self->tracefs_ok)
		return;

	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_enable_event(TRACEFS_DENY_PTRACE_ENABLE, false);
	tracefs_fixture_teardown();
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/* clang-format off */
FIXTURE_VARIANT(trace_ptrace)
{
	/* clang-format on */
	bool sandbox;
	bool sandbox_target;
	int expect_denied;
};

/* Denied: sandboxed child ptraces unsandboxed parent (tracee_domain=0). */
/* clang-format off */
FIXTURE_VARIANT_ADD(trace_ptrace, denied) {
	/* clang-format on */
	.sandbox = true,
	.sandbox_target = false,
	.expect_denied = 1,
};

/*
 * Denied: sandboxed child ptraces a sandboxed parent, so the tracee is in a
 * domain and tracee_domain= is non-zero.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(trace_ptrace, denied_scoped_target) {
	/* clang-format on */
	.sandbox = true,
	.sandbox_target = true,
	.expect_denied = 1,
};

/* Allowed: unsandboxed child uses PTRACE_TRACEME. */
/* clang-format off */
FIXTURE_VARIANT_ADD(trace_ptrace, allowed) {
	/* clang-format on */
	.sandbox = false,
	.sandbox_target = false,
	.expect_denied = 0,
};

TEST_F(trace_ptrace, deny_ptrace)
{
	char *buf, field[64], expected_pid[16];
	int count, status;
	pid_t child, parent;

	if (!self->tracefs_ok)
		SKIP(return, "tracefs not available");

	parent = getpid();

	/*
	 * Set a known comm so the denied variant can verify both the trace line
	 * task name and the tracee_comm= field.
	 */
	prctl(PR_SET_NAME, "ll_trace_test");

	/*
	 * For the non-zero tracee_domain case, sandbox the parent (the tracee)
	 * before forking.  The child inherits that domain and adds its own
	 * layer, so the child (tracer) is not an ancestor of the tracee and the
	 * ptrace is still denied, with tracee_domain= naming the parent's
	 * domain.
	 */
	if (variant->sandbox_target)
		create_domain(_metadata);

	child = fork();
	ASSERT_LE(0, child);

	if (child == 0) {
		if (variant->sandbox) {
			struct landlock_ruleset_attr ruleset_attr = {
				.scoped = LANDLOCK_SCOPE_SIGNAL,
			};
			int ruleset_fd;

			/*
			 * Any scope creates a domain.  Ptrace denial checks
			 * domain ancestry, not specific flags.
			 */
			ruleset_fd = landlock_create_ruleset(
				&ruleset_attr, sizeof(ruleset_attr), 0);
			if (ruleset_fd < 0)
				_exit(1);

			prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
			if (landlock_restrict_self(ruleset_fd, 0)) {
				close(ruleset_fd);
				_exit(1);
			}
			close(ruleset_fd);

			/* PTRACE_ATTACH on unsandboxed parent: denied. */
			if (ptrace(PTRACE_ATTACH, parent, NULL, NULL) == 0) {
				ptrace(PTRACE_DETACH, parent, NULL, NULL);
				_exit(2);
			}
			if (errno != EPERM)
				_exit(3);
		} else {
			/* No sandbox: ptrace should succeed. */
			if (ptrace(PTRACE_TRACEME) != 0)
				_exit(1);
		}

		_exit(0);
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	count = tracefs_count_matches(buf, REGEX_DENY_PTRACE("ll_trace_test"));
	if (variant->expect_denied) {
		EXPECT_EQ(variant->expect_denied, count)
		{
			TH_LOG("Expected deny_ptrace event, got %d\n%s", count,
			       buf);
		}

		/* Verify tracee_pid is the parent's TGID. */
		snprintf(expected_pid, sizeof(expected_pid), "%d", parent);
		ASSERT_EQ(0, tracefs_extract_field(
				     buf, REGEX_DENY_PTRACE("ll_trace_test"),
				     "tracee_pid", field, sizeof(field)));
		EXPECT_STREQ(expected_pid, field);

		/* Verify tracee_comm matches prctl(PR_SET_NAME). */
		ASSERT_EQ(0, tracefs_extract_field(
				     buf, REGEX_DENY_PTRACE("ll_trace_test"),
				     "tracee_comm", field, sizeof(field)));
		EXPECT_STREQ("ll_trace_test", field);

		/*
		 * Verify tracee_domain: 0 when the tracee is unsandboxed,
		 * non-zero when the tracee is in a domain.
		 */
		ASSERT_EQ(0, tracefs_extract_field(
				     buf, REGEX_DENY_PTRACE("ll_trace_test"),
				     "tracee_domain", field, sizeof(field)));
		EXPECT_EQ(variant->sandbox_target, strcmp("0", field) != 0)
		{
			TH_LOG("Unexpected tracee_domain=%s", field);
		}
	} else {
		EXPECT_EQ(0, count)
		{
			TH_LOG("Expected 0 deny_ptrace events, got %d\n%s",
			       count, buf);
		}
	}

	free(buf);
}

/* clang-format off */
FIXTURE(trace_ptrace_traceme) {
	/* clang-format on */
	int tracefs_ok;
};

FIXTURE_SETUP(trace_ptrace_traceme)
{
	int ret;

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWNS));
	ASSERT_EQ(0, mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL));

	ret = tracefs_fixture_setup();
	if (ret) {
		clear_cap(_metadata, CAP_SYS_ADMIN);
		self->tracefs_ok = 0;
		SKIP(return, "tracefs not available");
	}
	self->tracefs_ok = 1;

	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_DENY_PTRACE_ENABLE, true));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(trace_ptrace_traceme)
{
	if (!self->tracefs_ok)
		return;

	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_enable_event(TRACEFS_DENY_PTRACE_ENABLE, false);
	tracefs_fixture_teardown();
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/* clang-format off */
FIXTURE_VARIANT(trace_ptrace_traceme)
{
	/* clang-format on */
	bool sandbox_tracer;
	bool sandbox_tracee;
	int expect_denied;
};

/*
 * Denied: a sandboxed tracer cannot trace the unsandboxed child that asked to
 * be traced with PTRACE_TRACEME (tracee_domain=0).
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(trace_ptrace_traceme, denied) {
	/* clang-format on */
	.sandbox_tracer = true,
	.sandbox_tracee = false,
	.expect_denied = 1,
};

/*
 * Denied: a sandboxed child in its own domain asks to be traced by a tracer in
 * an unrelated domain, so the tracee is in a domain and tracee_domain= is
 * non-zero.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(trace_ptrace_traceme, denied_scoped_tracee) {
	/* clang-format on */
	.sandbox_tracer = true,
	.sandbox_tracee = true,
	.expect_denied = 1,
};

/* Allowed: unsandboxed child uses PTRACE_TRACEME with an unsandboxed tracer. */
/* clang-format off */
FIXTURE_VARIANT_ADD(trace_ptrace_traceme, allowed) {
	/* clang-format on */
	.sandbox_tracer = false,
	.sandbox_tracee = false,
	.expect_denied = 0,
};

TEST_F(trace_ptrace_traceme, deny_ptrace)
{
	char *buf, field[64], expected_pid[16];
	int count, status, sync_pipe[2];
	pid_t child;

	if (!self->tracefs_ok)
		SKIP(return, "tracefs not available");

	/*
	 * Set a known comm so the denied variant can verify both the trace line
	 * task name and the tracee_comm= field.  The tracee is the current
	 * (child) task for PTRACE_TRACEME, so the child inherits this name.
	 */
	prctl(PR_SET_NAME, "ll_trace_test");

	ASSERT_EQ(0, pipe2(sync_pipe, O_CLOEXEC));

	child = fork();
	ASSERT_LE(0, child);

	if (child == 0) {
		char c;

		close(sync_pipe[1]);

		/*
		 * The tracee is the current task; for the non-zero
		 * tracee_domain case it sandboxes itself in its own domain,
		 * unrelated to the tracer's domain, so PTRACE_TRACEME is still
		 * denied and tracee_domain= names the child's own domain.
		 */
		if (variant->sandbox_tracee)
			create_domain(_metadata);

		/* Waits for the tracer (parent) to enter its domain, if any. */
		if (read(sync_pipe[0], &c, 1) != 1)
			_exit(1);
		close(sync_pipe[0]);

		if (variant->expect_denied) {
			if (ptrace(PTRACE_TRACEME) == 0)
				_exit(2);
			if (errno != EPERM)
				_exit(3);
		} else {
			if (ptrace(PTRACE_TRACEME) != 0)
				_exit(4);
			/* Lets the tracer reap the trace-stop and detach. */
			raise(SIGSTOP);
		}

		_exit(0);
	}

	close(sync_pipe[0]);

	/*
	 * For a denial, the proposed tracer must be in a domain that is not an
	 * ancestor of the tracee's domain.  Sandboxing the parent after the
	 * fork gives it a domain unrelated to the child.
	 */
	if (variant->sandbox_tracer)
		create_domain(_metadata);

	/* Signals the child that the tracer is in its domain, if any. */
	ASSERT_EQ(1, write(sync_pipe[1], ".", 1));
	close(sync_pipe[1]);

	if (!variant->expect_denied) {
		/* PTRACE_TRACEME succeeded: reap the SIGSTOP and detach. */
		ASSERT_EQ(child, waitpid(child, &status, WUNTRACED));
		ASSERT_TRUE(WIFSTOPPED(status));
		ASSERT_EQ(0, ptrace(PTRACE_DETACH, child, NULL, 0));
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	count = tracefs_count_matches(buf, REGEX_DENY_PTRACE("ll_trace_test"));
	if (variant->expect_denied) {
		EXPECT_EQ(variant->expect_denied, count)
		{
			TH_LOG("Expected deny_ptrace event, got %d\n%s", count,
			       buf);
		}

		/* Verify tracee_pid is the child's TGID (the traced task). */
		snprintf(expected_pid, sizeof(expected_pid), "%d", child);
		ASSERT_EQ(0, tracefs_extract_field(
				     buf, REGEX_DENY_PTRACE("ll_trace_test"),
				     "tracee_pid", field, sizeof(field)));
		EXPECT_STREQ(expected_pid, field);

		/*
		 * Verify tracee_domain: 0 when the tracee is unsandboxed,
		 * non-zero when the tracee is in a domain.
		 */
		ASSERT_EQ(0, tracefs_extract_field(
				     buf, REGEX_DENY_PTRACE("ll_trace_test"),
				     "tracee_domain", field, sizeof(field)));
		EXPECT_EQ(variant->sandbox_tracee, strcmp("0", field) != 0)
		{
			TH_LOG("Unexpected tracee_domain=%s", field);
		}
	} else {
		EXPECT_EQ(0, count)
		{
			TH_LOG("Expected 0 deny_ptrace events, got %d\n%s",
			       count, buf);
		}
	}

	free(buf);
}

TEST_HARNESS_MAIN
