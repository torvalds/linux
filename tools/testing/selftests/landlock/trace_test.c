// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Tracepoints
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "trace.h"

#define TRACE_TASK "trace_test"

/* clang-format off */
FIXTURE(trace) {
	/* clang-format on */
	int tracefs_ok;
};

FIXTURE_SETUP(trace)
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

	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CREATE_DOMAIN_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ENFORCE_DOMAIN_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ADD_RULE_FS_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ADD_RULE_NET_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CHECK_RULE_FS_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CHECK_RULE_NET_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_DENY_ACCESS_FS_ENABLE, true));
	ASSERT_EQ(0,
		  tracefs_enable_event(TRACEFS_DENY_ACCESS_NET_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_FREE_DOMAIN_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_FREE_RULESET_ENABLE, true));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(trace)
{
	if (!self->tracefs_ok)
		return;

	/* Disables landlock events and clears PID filter. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, false);
	tracefs_enable_event(TRACEFS_CREATE_DOMAIN_ENABLE, false);
	tracefs_enable_event(TRACEFS_ENFORCE_DOMAIN_ENABLE, false);
	tracefs_enable_event(TRACEFS_ADD_RULE_FS_ENABLE, false);
	tracefs_enable_event(TRACEFS_ADD_RULE_NET_ENABLE, false);
	tracefs_enable_event(TRACEFS_CHECK_RULE_FS_ENABLE, false);
	tracefs_enable_event(TRACEFS_CHECK_RULE_NET_ENABLE, false);
	tracefs_enable_event(TRACEFS_DENY_ACCESS_FS_ENABLE, false);
	tracefs_enable_event(TRACEFS_DENY_ACCESS_NET_ENABLE, false);
	tracefs_enable_event(TRACEFS_FREE_DOMAIN_ENABLE, false);
	tracefs_enable_event(TRACEFS_FREE_RULESET_ENABLE, false);
	tracefs_clear_pid_filter();
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/*
	 * The mount namespace is cleaned up automatically when the test process
	 * (harness child) exits.
	 */
}

/*
 * Verifies that no trace events are emitted when the tracepoints are disabled.
 */
TEST_F(trace, no_trace_when_disabled)
{
	char *buf;

	/* Disable all landlock events. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0,
		  tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CREATE_DOMAIN_ENABLE, false));
	ASSERT_EQ(0,
		  tracefs_enable_event(TRACEFS_ENFORCE_DOMAIN_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ADD_RULE_FS_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ADD_RULE_NET_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CHECK_RULE_FS_ENABLE, false));
	ASSERT_EQ(0,
		  tracefs_enable_event(TRACEFS_CHECK_RULE_NET_ENABLE, false));
	ASSERT_EQ(0,
		  tracefs_enable_event(TRACEFS_DENY_ACCESS_FS_ENABLE, false));
	ASSERT_EQ(0,
		  tracefs_enable_event(TRACEFS_DENY_ACCESS_NET_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_DENY_PTRACE_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_DENY_SCOPE_SIGNAL_ENABLE,
					  false));
	ASSERT_EQ(0, tracefs_enable_event(
			     TRACEFS_DENY_SCOPE_ABSTRACT_UNIX_SOCKET_ENABLE,
			     false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_FREE_DOMAIN_ENABLE, false));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_FREE_RULESET_ENABLE, false));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/*
	 * Trigger both allowed and denied accesses to verify neither check_rule
	 * nor check_access events fire when disabled.
	 */
	sandbox_child_fs_access(_metadata, "/usr", LANDLOCK_ACCESS_FS_READ_DIR,
				LANDLOCK_ACCESS_FS_READ_DIR, "/tmp");

	/* Read trace buffer and verify no landlock events at all. */
	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(0, tracefs_count_matches(buf, "landlock_"))
	{
		TH_LOG("Expected 0 landlock events when disabled\n%s", buf);
	}

	free(buf);
}

/*
 * Verifies that landlock_create_ruleset emits a trace event with the correct
 * handled access masks.
 */
TEST_F(trace, create_ruleset)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE,
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP,
	};
	int ruleset_fd;
	char *buf, *dot;
	char field[64];

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, close(ruleset_fd));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(1,
		  tracefs_count_matches(buf, REGEX_CREATE_RULESET(TRACE_TASK)))
	{
		TH_LOG("Expected 1 create_ruleset event\n%s", buf);
	}

	/* Verify handled_fs matches what we requested. */
	EXPECT_EQ(0,
		  tracefs_extract_field(buf, REGEX_CREATE_RULESET(TRACE_TASK),
					"handled_fs", field, sizeof(field)));
	EXPECT_STREQ("read_file", field);

	/* Verify handled_net matches. */
	EXPECT_EQ(0,
		  tracefs_extract_field(buf, REGEX_CREATE_RULESET(TRACE_TASK),
					"handled_net", field, sizeof(field)));
	EXPECT_STREQ("bind_tcp", field);

	/* Verify version is 0 at creation (no rules added yet). */
	EXPECT_EQ(0,
		  tracefs_extract_field(buf, REGEX_CREATE_RULESET(TRACE_TASK),
					"ruleset", field, sizeof(field)));
	/* Format is <hex>.<dec>; version is after the dot. */
	dot = strchr(field, '.');
	ASSERT_NE(0, !!dot);
	EXPECT_STREQ("0", dot + 1);

	free(buf);
}

/*
 * Verifies that the ruleset version increments with each add_rule call and that
 * create_domain records the correct version.
 */
TEST_F(trace, ruleset_version)
{
	pid_t pid;
	int status;
	char *buf;
	const char *dot;
	char field[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		struct landlock_path_beneath_attr path_beneath = {
			.allowed_access = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		/* First rule: version becomes 1. */
		path_beneath.parent_fd =
			open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd < 0)
			_exit(1);
		landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				  &path_beneath, 0);
		close(path_beneath.parent_fd);

		/* Second rule: version becomes 2. */
		path_beneath.parent_fd =
			open("/tmp", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd < 0)
			_exit(1);
		landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				  &path_beneath, 0);
		close(path_beneath.parent_fd);

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, 0))
			_exit(1);
		close(ruleset_fd);
		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* Verify create_ruleset has version=0. */
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, REGEX_CREATE_RULESET(TRACE_TASK),
					"ruleset", field, sizeof(field)));
	dot = strchr(field, '.');
	ASSERT_NE(0, !!dot);
	EXPECT_STREQ("0", dot + 1);

	/* Verify 2 add_rule_fs events were emitted. */
	EXPECT_EQ(2, tracefs_count_matches(buf, REGEX_ADD_RULE_FS(TRACE_TASK)))
	{
		TH_LOG("Expected 2 add_rule_fs events\n%s", buf);
	}

	/*
	 * Verify create_domain records version=2 (after 2 add_rule calls).  The
	 * ruleset field format is <hex_id>.<dec_version>.
	 */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_CREATE_DOMAIN(TRACE_TASK),
					   "ruleset", field, sizeof(field)));
	dot = strchr(field, '.');
	ASSERT_NE(0, !!dot);
	EXPECT_STREQ("2", dot + 1);

	free(buf);
}

/*
 * Verifies that landlock_create_domain emits a trace event linking the ruleset
 * ID to the new domain ID.
 */
TEST_F(trace, create_domain)
{
	pid_t pid;
	int status, check_count;
	char *buf;
	char parent_id[64], domain_id[64], check_domain[64];

	/* Clear before the sandboxed child. */
	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		struct landlock_path_beneath_attr path_beneath = {
			.allowed_access = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int ruleset_fd, fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		path_beneath.parent_fd =
			open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd < 0)
			_exit(1);

		landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				  &path_beneath, 0);
		close(path_beneath.parent_fd);

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, 0))
			_exit(1);
		close(ruleset_fd);

		/* Trigger a check_rule to verify domain_id correlation. */
		fd = open("/usr", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd >= 0)
			close(fd);

		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* Verify create_domain event exists. */
	EXPECT_EQ(1,
		  tracefs_count_matches(buf, REGEX_CREATE_DOMAIN(TRACE_TASK)))
	{
		TH_LOG("Expected 1 create_domain event\n%s", buf);
	}

	/* Extract the domain ID from create_domain. */
	EXPECT_EQ(0, tracefs_extract_field(buf, REGEX_CREATE_DOMAIN(TRACE_TASK),
					   "domain", domain_id,
					   sizeof(domain_id)));

	/* Verify domain ID is non-zero. */
	EXPECT_NE(0, strcmp(domain_id, "0"));

	/* Verify parent=0 (first restriction, no prior domain). */
	EXPECT_EQ(0, tracefs_extract_field(buf, REGEX_CREATE_DOMAIN(TRACE_TASK),
					   "parent", parent_id,
					   sizeof(parent_id)));
	EXPECT_STREQ("0", parent_id);

	/*
	 * Verify the same domain ID appears in the check_rule event, confirming
	 * end-to-end correlation.
	 */
	check_count =
		tracefs_count_matches(buf, REGEX_CHECK_RULE_FS(TRACE_TASK));
	ASSERT_LE(1, check_count)
	{
		TH_LOG("Expected check_rule_fs events\n%s", buf);
	}

	EXPECT_EQ(0, tracefs_extract_field(buf, REGEX_CHECK_RULE_FS(TRACE_TASK),
					   "domain", check_domain,
					   sizeof(check_domain)));
	EXPECT_STREQ(domain_id, check_domain);

	free(buf);
}

/* Builds a rule-less scope-based ruleset; returns the fd or -1. */
static int build_enforce_ruleset(void)
{
	const struct landlock_ruleset_attr attr = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};

	return landlock_create_ruleset(&attr, sizeof(attr), 0);
}

/*
 * Verifies that nested landlock_restrict_self calls produce trace events with
 * correct parent domain IDs: the second create_domain's parent should be the
 * first domain's ID.
 */
TEST_F(trace, create_domain_nested)
{
	pid_t pid;
	int status;
	char *buf;
	const char *after_first;
	char first_domain[64], first_parent[64], second_parent[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		int ruleset_fd;

		/* First restriction. */
		ruleset_fd = build_enforce_ruleset();
		if (ruleset_fd < 0)
			_exit(1);
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, 0))
			_exit(1);
		close(ruleset_fd);

		/* Second restriction (nested). */
		ruleset_fd = build_enforce_ruleset();
		if (ruleset_fd < 0)
			_exit(1);
		if (landlock_restrict_self(ruleset_fd, 0))
			_exit(1);
		close(ruleset_fd);

		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* Should have 2 create_domain events. */
	EXPECT_EQ(2,
		  tracefs_count_matches(buf, REGEX_CREATE_DOMAIN(TRACE_TASK)))
	{
		TH_LOG("Expected 2 create_domain events\n%s", buf);
	}

	/*
	 * Extract domain and parent from each create_domain event.  The first
	 * event (parent=0) is the outer domain; the second (parent!=0) is the
	 * nested domain whose parent should match the first domain's ID.
	 */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_CREATE_DOMAIN(TRACE_TASK),
					   "domain", first_domain,
					   sizeof(first_domain)));
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_CREATE_DOMAIN(TRACE_TASK),
					   "parent", first_parent,
					   sizeof(first_parent)));
	EXPECT_STREQ("0", first_parent);

	/*
	 * Find the second create_domain by scanning past the first.
	 * tracefs_extract_field returns the first match, so search in the
	 * buffer after the first event.
	 *
	 * Skip past the first create_domain line. tracefs_extract_field matches
	 * the first line that matches the regex, so passing the buffer after
	 * the first matching line gives us the second event.
	 */
	after_first = strstr(buf, "landlock_create_domain:");
	ASSERT_NE(NULL, after_first);
	after_first = strchr(after_first, '\n');
	ASSERT_NE(NULL, after_first);

	ASSERT_EQ(0, tracefs_extract_field(
			     after_first + 1, REGEX_CREATE_DOMAIN(TRACE_TASK),
			     "parent", second_parent, sizeof(second_parent)));

	/* The second domain's parent should be the first domain's ID. */
	EXPECT_STREQ(first_domain, second_parent);

	free(buf);
}

/*
 * Verifies that landlock_add_rule does not emit a trace event when the syscall
 * fails (e.g., invalid ruleset fd).
 */
TEST_F(trace, add_rule_invalid_fd)
{
	struct landlock_path_beneath_attr path_beneath = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE,
	};
	char *buf;

	path_beneath.parent_fd = open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);

	/* Invalid ruleset fd (-1). */
	ASSERT_EQ(-1, landlock_add_rule(-1, LANDLOCK_RULE_PATH_BENEATH,
					&path_beneath, 0));
	ASSERT_EQ(0, close(path_beneath.parent_fd));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(0, tracefs_count_matches(buf, REGEX_ADD_RULE_FS(TRACE_TASK)))
	{
		TH_LOG("No add_rule_fs event expected on invalid fd\n%s", buf);
	}

	free(buf);
}

/*
 * Verifies that landlock_create_domain does not emit a trace event when the
 * syscall fails (e.g., invalid ruleset fd or unknown flags).
 */
TEST_F(trace, create_domain_invalid)
{
	int ruleset_fd;
	char *buf;

	ruleset_fd = build_enforce_ruleset();
	ASSERT_LE(0, ruleset_fd);

	/* Clear the trace buffer after create_ruleset event. */
	ASSERT_EQ(0, tracefs_clear_buf());

	/* Invalid fd. */
	ASSERT_EQ(-1, landlock_restrict_self(-1, 0));

	/* Unknown flags. */
	ASSERT_EQ(-1, landlock_restrict_self(ruleset_fd, -1));

	ASSERT_EQ(0, close(ruleset_fd));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(0,
		  tracefs_count_matches(buf, REGEX_CREATE_DOMAIN(TRACE_TASK)))
	{
		TH_LOG("No create_domain event expected on error\n%s", buf);
	}

	free(buf);
}

/*
 * Verifies that trace_landlock_free_domain fires when a domain is deallocated,
 * with the correct denials count.
 */
TEST_F(trace, free_domain)
{
	char *buf;
	int count;
	char denials_field[32];

	ASSERT_EQ(0, tracefs_clear_buf());

	/*
	 * The domain is freed via a work queue (kworker), so the free_domain
	 * trace event is emitted from a different PID.  Clear the PID filter
	 * BEFORE the child exits, so the kworker event passes the filter when
	 * it fires.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_clear_pid_filter();
	clear_cap(_metadata, CAP_SYS_ADMIN);

	sandbox_child_fs_access(_metadata, "/usr", LANDLOCK_ACCESS_FS_READ_DIR,
				LANDLOCK_ACCESS_FS_READ_DIR, "/tmp");

	/*
	 * Wait for the deferred deallocation work to run.  The domain is freed
	 * asynchronously from a kworker; poll until the event appears or a
	 * timeout is reached.
	 */
	for (int retry = 0; retry < 10; retry++) {
		usleep(100000);

		set_cap(_metadata, CAP_SYS_ADMIN);
		buf = tracefs_read_trace();
		clear_cap(_metadata, CAP_SYS_ADMIN);
		ASSERT_NE(NULL, buf);

		count = tracefs_count_matches(buf,
					      REGEX_FREE_DOMAIN(KWORKER_TASK));
		if (count >= 1)
			break;
		free(buf);
		buf = NULL;
	}

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, tracefs_set_pid_filter(getpid()));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	ASSERT_NE(NULL, buf);
	EXPECT_LE(1, count)
	{
		TH_LOG("Expected free_domain event, got %d\n%s", count, buf);
	}

	/* Verify denials count matches the single denial we triggered. */
	EXPECT_EQ(0, tracefs_extract_field(buf, REGEX_FREE_DOMAIN(KWORKER_TASK),
					   "denials", denials_field,
					   sizeof(denials_field)));
	EXPECT_STREQ("1", denials_field);

	free(buf);
}

/*
 * Verifies that deny_access_fs includes the enriched fields: same_exec and
 * logged.
 */
TEST_F(trace, deny_access_fs_fields)
{
	char *buf;
	char field_buf[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	/* Trigger a denial: rule for /usr, access /tmp. */
	sandbox_child_fs_access(_metadata, "/usr", LANDLOCK_ACCESS_FS_READ_DIR,
				LANDLOCK_ACCESS_FS_READ_DIR, "/tmp");

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* Verify the enriched fields are present and have valid values. */
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_ACCESS_FS(TRACE_TASK), "same_exec",
			     field_buf, sizeof(field_buf)));
	/* Child is the same exec that restricted itself. */
	EXPECT_STREQ("1", field_buf);

	/* Same exec with default flags: audit would log this denial. */
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_ACCESS_FS(TRACE_TASK), "logged",
			     field_buf, sizeof(field_buf)));
	EXPECT_STREQ("1", field_buf);

	free(buf);
}

/*
 * Verifies that same_exec is 1 (true) for denials from the same executable that
 * called landlock_restrict_self().
 */
TEST_F(trace, same_exec_before_exec)
{
	pid_t pid;
	int status;
	char *buf;
	char field[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int ruleset_fd, dir_fd;

		ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		/* No rules: all read_dir access is denied. */
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, 0))
			_exit(1);
		close(ruleset_fd);

		/* Trigger denial without exec (same executable). */
		dir_fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (dir_fd >= 0)
			close(dir_fd);
		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* Should have at least one deny_access_fs denial. */
	EXPECT_LE(1,
		  tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK)));

	/* Verify same_exec=1 (same executable, no exec). */
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK),
					"same_exec", field, sizeof(field)));
	EXPECT_STREQ("1", field);

	/* Same exec with default flags: audit would log this denial. */
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK),
					"logged", field, sizeof(field)));
	EXPECT_STREQ("1", field);

	free(buf);
}

/*
 * Verifies that same_exec is 0 (false) for denials from a process that has
 * exec'd a new binary after landlock_restrict_self().  The sandboxed child
 * exec's true which opens "." and triggers a read_dir denial.  Covers the
 * "trace-only" visibility condition: with same_exec=0 and the default
 * log_new_exec=0, audit suppresses the denial (logged=0) but the trace event
 * still fires.
 */
TEST_F(trace, same_exec_after_exec)
{
	char *buf;
	char field[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	sandbox_child_exec_true(_metadata, 0);

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_LE(1, tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS("true")));

	/* Verify same_exec=0 (different executable after exec). */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS("true"),
					   "same_exec", field, sizeof(field)));
	EXPECT_STREQ("0", field);

	/*
	 * same_exec=0 with default log_new_exec=0: audit suppresses (logged=0).
	 */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS("true"),
					   "logged", field, sizeof(field)));
	EXPECT_STREQ("0", field);

	free(buf);
}

/*
 * Verifies that LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF suppresses logging
 * (logged=0) for a denial from the same executable.
 */
TEST_F(trace, log_flags_same_exec_off)
{
	pid_t pid;
	int status;
	char *buf;
	char field[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int ruleset_fd, dir_fd;

		ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(
			    ruleset_fd,
			    LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF))
			_exit(1);
		close(ruleset_fd);

		dir_fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (dir_fd >= 0)
			close(dir_fd);
		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_LE(1,
		  tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK)));

	/* Same-exec denial with LOG_SAME_EXEC_OFF: audit suppresses it. */
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK),
					"logged", field, sizeof(field)));
	EXPECT_STREQ("0", field);

	free(buf);
}

/*
 * Verifies that LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON causes a post-exec
 * denial to be logged (logged=1).  The child exec's true so that the denial
 * comes from a new executable (same_exec=0).
 */
TEST_F(trace, log_flags_new_exec_on)
{
	char *buf;
	char field[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	sandbox_child_exec_true(_metadata,
				LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON);

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_LE(1, tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS("true")));

	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS("true"),
					   "same_exec", field, sizeof(field)));
	EXPECT_STREQ("0", field);

	/* LOG_NEW_EXEC_ON: the post-exec denial (same_exec=0) is logged. */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS("true"),
					   "logged", field, sizeof(field)));
	EXPECT_STREQ("1", field);

	free(buf);
}

/*
 * Verifies that denials suppressed by audit log flags are still counted in
 * num_denials.  The child restricts itself with default flags (log_same_exec=1,
 * log_new_exec=0), then execs true which attempts to read a denied directory.
 * After exec, same_exec=0 and log_new_exec=0, so audit suppresses the denial.
 * But the trace event fires unconditionally and free_domain must report the
 * correct denials count.
 */
TEST_F(trace, non_audit_visible_denial_counting)
{
	char *buf = NULL;
	char denials_field[32];
	int count;

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, tracefs_clear());
	tracefs_clear_pid_filter();
	clear_cap(_metadata, CAP_SYS_ADMIN);

	sandbox_child_exec_true(_metadata, 0);

	/* Wait for free_domain event with retry. */
	for (int retry = 0; retry < 10; retry++) {
		usleep(100000);

		set_cap(_metadata, CAP_SYS_ADMIN);
		buf = tracefs_read_trace();
		clear_cap(_metadata, CAP_SYS_ADMIN);
		if (!buf)
			break;

		count = tracefs_count_matches(buf,
					      REGEX_FREE_DOMAIN(KWORKER_TASK));
		if (count >= 1)
			break;
		free(buf);
		buf = NULL;
	}

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, tracefs_set_pid_filter(getpid()));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/*
	 * The denial happened after exec (same_exec=0), so audit would suppress
	 * it.  But num_denials counts all denials regardless.
	 */
	ASSERT_NE(NULL, buf)
	{
		TH_LOG("free_domain event not found after 10 retries");
	}
	EXPECT_EQ(0, tracefs_extract_field(buf, REGEX_FREE_DOMAIN(KWORKER_TASK),
					   "denials", denials_field,
					   sizeof(denials_field)));
	EXPECT_STREQ("1", denials_field);

	free(buf);
}

/*
 * Verifies that landlock_add_rule_net emits a trace event with the correct port
 * and allowed access mask fields.
 */
TEST_F(trace, add_rule_net_fields)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP,
	};
	struct landlock_net_port_attr net_port = {
		.allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP,
		.port = 8080,
	};
	int ruleset_fd;
	char *buf;
	char field[64];

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, tracefs_clear_buf());

	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				       &net_port, 0));
	close(ruleset_fd);

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(1, tracefs_count_matches(buf, REGEX_ADD_RULE_NET(TRACE_TASK)))
	{
		TH_LOG("Expected 1 add_rule_net event\n%s", buf);
	}

	/*
	 * Verify the port is in host endianness, matching the UAPI convention
	 * (landlock_net_port_attr.port).  On little-endian, htons(8080) is
	 * 36895, so this comparison catches byte-order bugs.
	 */
	EXPECT_EQ(0, tracefs_extract_field(buf, REGEX_ADD_RULE_NET(TRACE_TASK),
					   "port", field, sizeof(field)));
	EXPECT_STREQ("8080", field);
	/*
	 * The allowed mask is the absolute value after transformation: the
	 * user-requested BIND_TCP plus all unhandled access rights (the other
	 * net access bits are unhandled because the ruleset only handles
	 * BIND_TCP).
	 */
	EXPECT_EQ(0,
		  tracefs_extract_field(buf, REGEX_ADD_RULE_NET(TRACE_TASK),
					"access_rights", field, sizeof(field)));
	EXPECT_STREQ("bind_tcp|connect_tcp|bind_udp|connect_send_udp", field);

	free(buf);
}

/*
 * Verifies that LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF suppresses audit
 * logging for child domains (logged=0) even though the child's own
 * per-execution flags are the defaults, while the trace event still fires
 * (tracing is unconditional).  The parent creates a domain with
 * LOG_SUBDOMAINS_OFF, then the child creates a sub-domain and triggers a
 * denial.
 */
TEST_F(trace, log_flags_subdomains_off)
{
	pid_t pid;
	int status;
	char *buf;
	char field[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int parent_fd, child_fd, dir_fd;

		/* Parent domain with LOG_SUBDOMAINS_OFF. */
		parent_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
		if (parent_fd < 0)
			_exit(1);

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(
			    parent_fd,
			    LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF))
			_exit(1);
		close(parent_fd);

		/* Child sub-domain with default flags. */
		child_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
		if (child_fd < 0)
			_exit(1);

		if (landlock_restrict_self(child_fd, 0))
			_exit(1);
		close(child_fd);

		/* Trigger a denial from the child domain. */
		dir_fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (dir_fd >= 0)
			close(dir_fd);
		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/*
	 * Trace fires unconditionally even though audit is disabled for the
	 * child domain (parent had LOG_SUBDOMAINS_OFF).
	 */
	EXPECT_LE(1,
		  tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK)))
	{
		TH_LOG("Expected deny_access_fs event despite "
		       "LOG_SUBDOMAINS_OFF\n%s",
		       buf);
	}

	/*
	 * The child's per-execution flags default to logging, but the
	 * ancestor's LOG_SUBDOMAINS_OFF disables it, so audit suppresses this
	 * denial (logged=0).  This is exactly the case the single logged field
	 * captures and the raw per-execution flags could not.
	 */
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK),
					"logged", field, sizeof(field)));
	EXPECT_STREQ("0", field);

	free(buf);
}

/* Verifies that landlock_free_ruleset fires when a ruleset FD is closed. */
TEST_F(trace, free_ruleset_on_close)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
	};
	int ruleset_fd;
	char *buf;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, tracefs_clear_buf());

	/* Closing the FD should trigger free_ruleset. */
	close(ruleset_fd);

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(1, tracefs_count_matches(buf, REGEX_FREE_RULESET(TRACE_TASK)))
	{
		TH_LOG("Expected 1 free_ruleset event\n%s", buf);
	}

	free(buf);
}

/*
 * Counts landlock_enforce_domain lines, filtered by @domain (NULL matches any),
 * @complete and @process_wide (a negative value matches any).  Builds the
 * anchored regex dynamically so a single helper covers every field assertion.
 */
static int count_enforce_matches(const char *buf, const char *domain,
				 int complete, int process_wide,
				 int no_new_privs)
{
	char pattern[512], dom[80], comp[8], pw[8], nnp[8];

	if (domain)
		snprintf(dom, sizeof(dom), "%s", domain);
	else
		snprintf(dom, sizeof(dom), "[0-9a-f]\\+");
	if (complete < 0)
		snprintf(comp, sizeof(comp), "[01]");
	else
		snprintf(comp, sizeof(comp), "%d", complete);
	if (process_wide < 0)
		snprintf(pw, sizeof(pw), "[01]");
	else
		snprintf(pw, sizeof(pw), "%d", process_wide);
	if (no_new_privs < 0)
		snprintf(nnp, sizeof(nnp), "[01]");
	else
		snprintf(nnp, sizeof(nnp), "%d", no_new_privs);

	snprintf(pattern, sizeof(pattern),
		 TRACE_PREFIX(TRACE_TASK) "landlock_enforce_domain: "
					  "domain=%s "
					  "complete=%s process_wide=%s "
					  "no_new_privs=%s$",
		 dom, comp, pw, nnp);
	return tracefs_count_matches(buf, pattern);
}

/* Idle sibling: waits on the barrier so it is a live thread, then sleeps. */
static void *enforce_idle(void *arg)
{
	pthread_barrier_t *barrier = arg;

	pthread_barrier_wait(barrier);
	while (true)
		sleep(1);
	return NULL;
}

/*
 * Child body: spawns @nthreads idle siblings (barrier-synchronized so they are
 * live when the syscall runs), then enforces a domain with @flags.  Returns 0
 * on success; the process exits afterwards, reaping the siblings.
 */
static int child_enforce(int nthreads, __u32 flags)
{
	pthread_t threads[8];
	pthread_barrier_t barrier;
	int ruleset_fd, i;

	if (nthreads > 0) {
		if (pthread_barrier_init(&barrier, NULL, nthreads + 1))
			return 1;
		for (i = 0; i < nthreads; i++)
			if (pthread_create(&threads[i], NULL, enforce_idle,
					   &barrier))
				return 1;
		pthread_barrier_wait(&barrier);
	}

	ruleset_fd = build_enforce_ruleset();
	if (ruleset_fd < 0)
		return 1;

	/*
	 * LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS sets no_new_privs itself, so skip
	 * the prctl() to exercise that path; otherwise Landlock requires
	 * no_new_privs up front.
	 */
	if (!(flags & LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS))
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (landlock_restrict_self(ruleset_fd, flags))
		return 1;
	close(ruleset_fd);
	return 0;
}

/*
 * Runs in a spawned thread after the group leader called pthread_exit().  The
 * leader lingers as an un-reaped zombie, so get_nr_threads() still counts it
 * and this non-leader is not the only thread; enforcing here therefore reports
 * process_wide=0.
 */
static void *enforce_nonleader(void *arg)
{
	int ruleset_fd;

	ruleset_fd = build_enforce_ruleset();
	if (ruleset_fd < 0)
		_exit(1);
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (landlock_restrict_self(ruleset_fd, 0))
		_exit(1);
	_exit(0);
}

/*
 * Collapses the enforce_domain field cases into one parametrized test.  Each
 * variant runs child_enforce(nthreads, flags) and checks the resulting
 * enforce_domain events.  The flags column also selects how no_new_privs is
 * set: with LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS child_enforce() skips the
 * prctl() so the flag sets it (and, with TSYNC, propagates to the siblings);
 * otherwise a prior prctl() sets it on the caller (and TSYNC propagates that).
 */

/* clang-format off */
FIXTURE(trace_enforce) {
	/* clang-format on */
	int tracefs_ok;
};

FIXTURE_SETUP(trace_enforce)
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

	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CREATE_DOMAIN_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ENFORCE_DOMAIN_ENABLE, true));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(trace_enforce)
{
	if (!self->tracefs_ok)
		return;

	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, false);
	tracefs_enable_event(TRACEFS_CREATE_DOMAIN_ENABLE, false);
	tracefs_enable_event(TRACEFS_ENFORCE_DOMAIN_ENABLE, false);
	tracefs_fixture_teardown();
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/* clang-format off */
FIXTURE_VARIANT(trace_enforce) {
	/* clang-format on */
	/* Inputs to child_enforce(). */
	int nthreads;
	__u32 flags;
	/* Expected enforce_domain event counts. */
	int total;
	int complete;
	int process_wide;
	int no_new_privs;
};

/* clang-format off */

/* Single thread, no flags: prctl-backed no_new_privs. */
FIXTURE_VARIANT_ADD(trace_enforce, single) {
	.nthreads = 0, .flags = 0,
	.total = 1, .complete = 1, .process_wide = 1, .no_new_privs = 1,
};

/* Single thread: the NO_NEW_PRIVS flag sets no_new_privs (no prctl). */
FIXTURE_VARIANT_ADD(trace_enforce, no_new_privs) {
	.nthreads = 0, .flags = LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS,
	.total = 1, .complete = 1, .process_wide = 1, .no_new_privs = 1,
};

/* TSYNC on a lone thread still concludes, process-wide. */
FIXTURE_VARIANT_ADD(trace_enforce, tsync_single) {
	.nthreads = 0, .flags = LANDLOCK_RESTRICT_SELF_TSYNC,
	.total = 1, .complete = 1, .process_wide = 1, .no_new_privs = 1,
};

/* TSYNC sweeps N siblings; the caller's prctl-backed nnp propagates to all. */
FIXTURE_VARIANT_ADD(trace_enforce, tsync_multithread) {
	.nthreads = 3, .flags = LANDLOCK_RESTRICT_SELF_TSYNC,
	.total = 4, .complete = 1, .process_wide = 4, .no_new_privs = 4,
};

/* TSYNC + NO_NEW_PRIVS flag sets nnp on the caller and every swept sibling. */
FIXTURE_VARIANT_ADD(trace_enforce, tsync_no_new_privs) {
	.nthreads = 3,
	.flags = LANDLOCK_RESTRICT_SELF_TSYNC | LANDLOCK_RESTRICT_SELF_NO_NEW_PRIVS,
	.total = 4, .complete = 1, .process_wide = 4, .no_new_privs = 4,
};

/* Non-TSYNC on a multi-threaded process enforces only the caller. */
FIXTURE_VARIANT_ADD(trace_enforce, multithread_non_tsync) {
	.nthreads = 3, .flags = 0,
	.total = 1, .complete = 1, .process_wide = 0, .no_new_privs = 1,
};

/* clang-format on */

/*
 * One create_domain and variant->total enforce_domain events sharing that
 * domain ID; complete=1 marks the single concluding event, and the process_wide
 * / no_new_privs counts match the variant.  Counts are order-independent,
 * evaluated after the syscall returns.
 */
TEST_F(trace_enforce, enforce)
{
	pid_t pid;
	int status;
	char *buf;
	char domain[64];

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);
	if (pid == 0)
		_exit(child_enforce(variant->nthreads, variant->flags));

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(1,
		  tracefs_count_matches(buf, REGEX_CREATE_DOMAIN(TRACE_TASK)));
	EXPECT_EQ(variant->total, count_enforce_matches(buf, NULL, -1, -1, -1))
	{
		TH_LOG("Expected %d enforce_domain events\n%s", variant->total,
		       buf);
	}
	EXPECT_EQ(variant->complete,
		  count_enforce_matches(buf, NULL, 1, -1, -1));
	EXPECT_EQ(variant->total - variant->complete,
		  count_enforce_matches(buf, NULL, 0, -1, -1));
	EXPECT_EQ(variant->process_wide,
		  count_enforce_matches(buf, NULL, -1, 1, -1));
	EXPECT_EQ(variant->total - variant->process_wide,
		  count_enforce_matches(buf, NULL, -1, 0, -1));
	EXPECT_EQ(variant->no_new_privs,
		  count_enforce_matches(buf, NULL, -1, -1, 1));

	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_CREATE_DOMAIN(TRACE_TASK),
					   "domain", domain, sizeof(domain)));
	EXPECT_EQ(variant->total,
		  count_enforce_matches(buf, domain, -1, -1, -1));

	free(buf);
}

/*
 * A non-leader thread enforcing a domain while the group leader lingers as an
 * un-reaped zombie reports process_wide=0: get_nr_threads() counts the zombie
 * leader, so the group is not single-threaded.  This is the reachable half of
 * the caveat that process_wide==0 never proves the process is multi-threaded
 * (get_nr_threads(), unlike the leader-relative thread_group_empty(), counts
 * the zombie leader).
 */
TEST_F(trace, enforce_single_non_leader)
{
	pid_t pid;
	int status;
	char *buf;

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);
	if (pid == 0) {
		pthread_t worker;

		if (pthread_create(&worker, NULL, enforce_nonleader, NULL))
			_exit(1);
		/* Leader leaves; the worker enforces as a non-leader. */
		pthread_exit(NULL);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(1,
		  tracefs_count_matches(buf, REGEX_CREATE_DOMAIN(TRACE_TASK)));
	EXPECT_EQ(1, count_enforce_matches(buf, NULL, 1, 0, -1))
	{
		TH_LOG("Expected complete=1 process_wide=0 for non-leader\n%s",
		       buf);
	}

	free(buf);
}

/*
 * Verifies the flags-only path (ruleset_fd == -1) creates no domain and emits
 * neither create_domain nor enforce_domain, with and without TSYNC.
 */
TEST_F(trace, enforce_flags_only)
{
	pid_t pid;
	int status;
	char *buf;

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);
	if (pid == 0) {
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(
			    -1, LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF))
			_exit(1);
		if (landlock_restrict_self(
			    -1, LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
					LANDLOCK_RESTRICT_SELF_TSYNC))
			_exit(1);
		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	EXPECT_EQ(0,
		  tracefs_count_matches(buf, REGEX_CREATE_DOMAIN(TRACE_TASK)));
	EXPECT_EQ(0, count_enforce_matches(buf, NULL, -1, -1, -1))
	{
		TH_LOG("No enforce_domain expected on flags-only path\n%s",
		       buf);
	}

	free(buf);
}

static void enforce_nop_handler(int sig)
{
}

struct abort_signaler_data {
	pthread_t target;
	volatile bool stop;
};

/*
 * Hammers the target thread with SIGUSR1 to interrupt the TSYNC prepare wait.
 */
static void *abort_signaler(void *arg)
{
	struct abort_signaler_data *data = arg;

	while (!data->stop)
		pthread_kill(data->target, SIGUSR1);
	return NULL;
}

/*
 * Child body for the abort test: with idle siblings and a signaler interrupting
 * it, repeatedly enforces under TSYNC.  An interrupted attempt aborts its
 * just-created domain (create_domain + free_domain, zero enforce_domain) while
 * -ERESTARTNOINTR transparently restarts the syscall, so a successful retry may
 * add its own full lifecycle.
 */
static int child_abort(int nsiblings, int attempts)
{
	pthread_t threads[200];
	pthread_t signaler;
	pthread_barrier_t barrier;
	struct abort_signaler_data data = {};
	struct sigaction sa = {};
	int i;

	sa.sa_handler = enforce_nop_handler;
	if (sigaction(SIGUSR1, &sa, NULL))
		return 1;

	if (pthread_barrier_init(&barrier, NULL, nsiblings + 1))
		return 1;
	for (i = 0; i < nsiblings; i++)
		if (pthread_create(&threads[i], NULL, enforce_idle, &barrier))
			return 1;
	pthread_barrier_wait(&barrier);

	data.target = pthread_self();
	if (pthread_create(&signaler, NULL, abort_signaler, &data))
		return 1;

	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	for (i = 0; i < attempts; i++) {
		int ruleset_fd = build_enforce_ruleset();

		if (ruleset_fd < 0)
			break;
		/*
		 * Ignore the result: an abort returns an error, that is fine.
		 */
		landlock_restrict_self(ruleset_fd,
				       LANDLOCK_RESTRICT_SELF_TSYNC);
		close(ruleset_fd);
	}

	data.stop = true;
	pthread_join(signaler, NULL);
	return 0;
}

/*
 * Verifies the abort contract: a domain aborted by a thread-sync failure emits
 * create_domain and free_domain but zero enforce_domain.  The signal race is
 * probabilistic and -ERESTARTNOINTR may add a successful retry's lifecycle, so
 * events are grouped by domain ID and the test SKIPs if no abort occurred.
 */
TEST_F(trace, enforce_abort)
{
	pid_t pid;
	int status, retry;
	char *buf = NULL;
	const char *cursor;
	char domain[64];
	bool abort_found = false;

	ASSERT_EQ(0, tracefs_clear_buf());

	/* free_domain fires from a kworker, so widen the filter first. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_clear_pid_filter();
	clear_cap(_metadata, CAP_SYS_ADMIN);

	pid = fork();
	ASSERT_LE(0, pid);
	if (pid == 0)
		/*
		 * Match tsync_test's NUM_IDLE_THREADS: enough siblings that
		 * credential preparation runs in several serialized waves,
		 * giving the signaler a window to interrupt the thread-sync
		 * wait and abort the operation.  A handful of threads finishes
		 * in a single wave, leaving no window (the abort never fires).
		 */
		_exit(child_abort(200, 8));

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* Poll for the asynchronous free_domain events. */
	for (retry = 0; retry < 10; retry++) {
		usleep(100000);
		set_cap(_metadata, CAP_SYS_ADMIN);
		free(buf);
		buf = tracefs_read_trace();
		clear_cap(_metadata, CAP_SYS_ADMIN);
		ASSERT_NE(NULL, buf);
	}

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, tracefs_set_pid_filter(getpid()));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/*
	 * Walk every create_domain and look for one whose domain ID has zero
	 * enforce_domain events but a matching free_domain: that is an aborted
	 * domain (created, never enforced, freed).
	 */
	cursor = buf;
	while (tracefs_extract_field(cursor, REGEX_CREATE_DOMAIN(TRACE_TASK),
				     "domain", domain, sizeof(domain)) == 0) {
		const char *cd, *nl;
		char free_pattern[256];

		if (count_enforce_matches(buf, domain, -1, -1, -1) == 0) {
			snprintf(
				free_pattern, sizeof(free_pattern),
				TRACE_PREFIX(
					KWORKER_TASK) "landlock_free_domain: "
						      "domain=%s denials=[0-9]\\+$",
				domain);
			if (tracefs_count_matches(buf, free_pattern) >= 1)
				abort_found = true;
		}

		cd = strstr(cursor, "landlock_create_domain:");
		if (!cd)
			break;
		nl = strchr(cd, '\n');
		if (!nl)
			break;
		cursor = nl + 1;
	}

	if (!abort_found) {
		free(buf);
		SKIP(return, "signal race did not produce a thread-sync abort");
	}

	free(buf);
}

/*
 * The following tests are intentionally elided because the underlying kernel
 * mechanisms are already validated by audit tests:
 *
 * - Domain ID monotonicity: validated by audit_test.c:layers.  The same
 *   landlock_get_id_range() function serves both audit and trace.
 *
 * - Domain deallocation order (LIFO): validated by audit_test.c:layers.  Trace
 *   events fire from the same free_domain_work() code path.
 *
 * - Max-layer stacking (16 domains): validated by audit_test.c:layers.
 *
 * - IPv6 network tests: IPv6 hook dispatch uses the same
 *   current_check_access_socket() as IPv4, validated by net_test.c:audit tests.
 *
 * - Per-access-right full matrix (all 16 FS rights): hook dispatch is validated
 *   by fs_test.c:audit tests.  Trace tests verify representative samples to
 *   ensure bitmask encoding is correct.
 *
 * - Combined log flag variants (e.g., LOG_SUBDOMAINS_OFF + LOG_NEW_EXEC_ON):
 *   individual flag tests above cover each flag's effect on trace fields.  Flag
 *   combination logic is validated by audit_test.c:audit_flags tests.
 *
 * - fs.refer multi-record denials and fs.change_topology (mount):
 *   trace_denial() uses the same code path for all FS request types.  The
 *   DENTRY union member is validated by the deny_access_fs_fields
 *   test.  Audit tests in fs_test.c cover refer and mount denial specifics.
 */

TEST_HARNESS_MAIN
