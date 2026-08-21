// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Filesystem tracepoints
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
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

#define TRACE_TASK "trace_fs_test"

/*
 * Like REGEX_DENY_ACCESS_FS(), but pins the logged field to a specific value
 * ("0" or "1") so a test can tell a suppressed (quiet) denial from a logged
 * one.  The tracepoint fires for every denial; logged carries the audit
 * verdict.
 */
#define REGEX_DENY_ACCESS_FS_LOGGED(task, log) \
	TRACE_PREFIX(task)                     \
	"landlock_deny_access_fs: "            \
	"domain=[0-9a-f]\\+ "                  \
	"same_exec=[01] "                      \
	"logged=" log " "                      \
	"blockers=[a-z_|]* "                   \
	"dev=[0-9]\\+:[0-9]\\+ "               \
	"ino=[0-9]\\+ "                        \
	"path=[^ ]*$"

/* clang-format off */
FIXTURE(trace_fs) {
	/* clang-format on */
	int tracefs_ok;
};

FIXTURE_SETUP(trace_fs)
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

	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ADD_RULE_FS_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CHECK_RULE_FS_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_DENY_ACCESS_FS_ENABLE, true));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(trace_fs)
{
	if (!self->tracefs_ok)
		return;

	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_enable_event(TRACEFS_ADD_RULE_FS_ENABLE, false);
	tracefs_enable_event(TRACEFS_CHECK_RULE_FS_ENABLE, false);
	tracefs_enable_event(TRACEFS_DENY_ACCESS_FS_ENABLE, false);
	tracefs_fixture_teardown();
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * Baseline: verifies that without Landlock, the operation succeeds and no
 * check_rule or deny_access trace events fire.
 */
TEST_F(trace_fs, unsandboxed)
{
	char *buf;
	int count, status, fd;
	pid_t pid;

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		/*
		 * No sandbox: verify that a normal FS access does not produce
		 * Landlock trace events.
		 */
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

	count = tracefs_count_matches(buf, REGEX_CHECK_RULE_FS(TRACE_TASK));
	EXPECT_EQ(0, count);
	count = tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK));
	EXPECT_EQ(0, count);

	free(buf);
}

/*
 * Verifies that adding a filesystem rule emits a landlock_add_rule_fs trace
 * event with the expected path and field values: ruleset ID is non-zero,
 * access_rights is non-zero, and path matches.
 */
TEST_F(trace_fs, add_rule_fs)
{
	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE |
				     LANDLOCK_ACCESS_FS_WRITE_FILE |
				     LANDLOCK_ACCESS_FS_READ_DIR,
	};
	struct landlock_path_beneath_attr path_beneath = {
		.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE,
	};
	char *buf, field_buf[64];
	int ruleset_fd, count;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	path_beneath.parent_fd = open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
	ASSERT_LE(0, path_beneath.parent_fd);

	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				       &path_beneath, 0));
	ASSERT_EQ(0, close(path_beneath.parent_fd));
	ASSERT_EQ(0, close(ruleset_fd));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	count = tracefs_count_matches(buf, REGEX_ADD_RULE_FS(TRACE_TASK));
	EXPECT_EQ(1, count)
	{
		TH_LOG("Expected 1 add_rule_fs event, got %d\n%s", count, buf);
	}

	/* Ruleset ID should be non-zero. */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_ADD_RULE_FS(TRACE_TASK),
					   "ruleset", field_buf,
					   sizeof(field_buf)));
	EXPECT_STRNE("0", field_buf);

	/* Access rights should be non-zero. */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_ADD_RULE_FS(TRACE_TASK),
					   "access_rights", field_buf,
					   sizeof(field_buf)));
	EXPECT_STRNE("", field_buf);

	/* Path should be /usr. */
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, REGEX_ADD_RULE_FS(TRACE_TASK),
					"path", field_buf, sizeof(field_buf)));
	EXPECT_STREQ("/usr", field_buf);

	free(buf);
}

/*
 * Verifies that an allowed access emits check_rule events (rule matched during
 * pathwalk) but does NOT emit deny_access events (no denial).
 */
TEST_F(trace_fs, allowed_access)
{
	char *buf, field_buf[64];
	int count;

	ASSERT_EQ(0, tracefs_clear_buf());

	/* Rule allows READ_DIR for /usr, access /usr which is allowed. */
	sandbox_child_fs_access(_metadata, "/usr", LANDLOCK_ACCESS_FS_READ_DIR,
				LANDLOCK_ACCESS_FS_READ_DIR, "/usr");

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	count = tracefs_count_matches(buf, REGEX_CHECK_RULE_FS(TRACE_TASK));
	EXPECT_LE(1, count);

	/* Single-layer grants array, intersected with the request. */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_CHECK_RULE_FS(TRACE_TASK),
					   "grants", field_buf,
					   sizeof(field_buf)));
	EXPECT_STREQ("{read_dir}", field_buf);

	count = tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK));
	EXPECT_EQ(0, count);

	free(buf);
}

/*
 * Verifies that accessing a path whose access type is not in the handled set
 * does not emit landlock_check_rule events.  The ruleset handles READ_FILE, but
 * the directory open checks READ_DIR which is unhandled; Landlock has no
 * opinion and no rule evaluation occurs.
 */
TEST_F(trace_fs, check_rule_unhandled)
{
	char *buf;
	int count;

	ASSERT_EQ(0, tracefs_clear_buf());

	/* Handles READ_FILE only; READ_DIR is unhandled. */
	sandbox_child_fs_access(_metadata, "/usr", LANDLOCK_ACCESS_FS_READ_FILE,
				LANDLOCK_ACCESS_FS_READ_FILE, "/tmp");

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* No check_rule events because READ_DIR is not in the handled set. */
	count = tracefs_count_matches(buf, REGEX_CHECK_RULE_FS(TRACE_TASK));
	EXPECT_EQ(0, count);

	free(buf);
}

/*
 * Verifies that nested domains (child sandboxed under a parent domain) emit
 * check_rule events from both layers and produce a deny_access event when the
 * inner domain's rule does not cover the access.
 */
TEST_F(trace_fs, check_rule_nested)
{
	char *buf, field_buf[64], *comma;
	size_t first_len, second_len;
	int count_rule, count_access, status;
	pid_t pid;

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

		/* First layer: allow /usr. */
		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		path_beneath.parent_fd =
			open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd < 0) {
			close(ruleset_fd);
			_exit(1);
		}

		if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				      &path_beneath, 0)) {
			close(path_beneath.parent_fd);
			close(ruleset_fd);
			_exit(1);
		}
		close(path_beneath.parent_fd);

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, 0)) {
			close(ruleset_fd);
			_exit(1);
		}
		close(ruleset_fd);

		/* Second layer: also allow /usr. */
		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		path_beneath.parent_fd =
			open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd < 0) {
			close(ruleset_fd);
			_exit(1);
		}

		if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				      &path_beneath, 0)) {
			close(path_beneath.parent_fd);
			close(ruleset_fd);
			_exit(1);
		}
		close(path_beneath.parent_fd);

		if (landlock_restrict_self(ruleset_fd, 0)) {
			close(ruleset_fd);
			_exit(1);
		}
		close(ruleset_fd);

		/* Access /usr which is allowed by both layers. */
		fd = open("/usr", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd >= 0)
			close(fd);

		/* Access /tmp which has no rule in either layer. */
		fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd >= 0)
			close(fd);

		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	count_rule =
		tracefs_count_matches(buf, REGEX_CHECK_RULE_FS(TRACE_TASK));
	EXPECT_LE(1, count_rule);

	/*
	 * Both layers have the same rule, so the grants array must have two
	 * identical symbolic entries, e.g. {read_dir,read_dir}.
	 */
	ASSERT_EQ(0, tracefs_extract_field(buf, REGEX_CHECK_RULE_FS(TRACE_TASK),
					   "grants", field_buf,
					   sizeof(field_buf)));
	comma = strchr(field_buf, ',');
	EXPECT_NE(0, !!comma);
	if (comma) {
		/*
		 * Verify both entries are identical: compare the substring
		 * before the comma with the substring after it (stripping the
		 * braces).
		 */
		first_len = comma - field_buf - 1;
		second_len = strlen(comma + 1) - 1;
		EXPECT_EQ(first_len, second_len);
		EXPECT_EQ(0, strncmp(field_buf + 1, comma + 1, first_len));
	}

	count_access =
		tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK));
	EXPECT_LE(1, count_access);

	free(buf);
}

/*
 * Verifies that a denied FS access emits a landlock_deny_access_fs trace event
 * with the blocked access and path.
 */
TEST_F(trace_fs, deny_access_fs_denied)
{
	char *buf;
	int count;

	ASSERT_EQ(0, tracefs_clear_buf());

	/*
	 * Rule allows READ_DIR for /usr, but access /tmp which has no rule.
	 * READ_DIR access to /tmp is denied by absence and should emit a
	 * deny_access_fs event.
	 */
	sandbox_child_fs_access(_metadata, "/usr", LANDLOCK_ACCESS_FS_READ_DIR,
				LANDLOCK_ACCESS_FS_READ_DIR, "/tmp");

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	count = tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS(TRACE_TASK));
	EXPECT_LE(1, count);

	free(buf);
}

/*
 * A denied FS access covered by a quiet rule (LANDLOCK_ADD_RULE_QUIET with the
 * access listed in quiet_access_fs) still emits a landlock_deny_access_fs
 * event, but with logged=0, the same audit-logging verdict audit would apply to
 * suppress the record.
 */
TEST_F(trace_fs, deny_access_fs_quiet)
{
	char *buf, field[64];
	pid_t pid;
	int status;

	ASSERT_EQ(0, tracefs_clear_buf());

	pid = fork();
	ASSERT_LE(0, pid);
	if (pid == 0) {
		struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
			.quiet_access_fs = LANDLOCK_ACCESS_FS_READ_DIR,
		};
		struct landlock_path_beneath_attr path_beneath = {
			.allowed_access = 0,
		};
		int ruleset_fd, fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		/* Marks /tmp quiet without granting any access. */
		path_beneath.parent_fd =
			open("/tmp", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd < 0) {
			close(ruleset_fd);
			_exit(1);
		}
		if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				      &path_beneath, LANDLOCK_ADD_RULE_QUIET)) {
			close(path_beneath.parent_fd);
			close(ruleset_fd);
			_exit(1);
		}
		close(path_beneath.parent_fd);

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, 0)) {
			close(ruleset_fd);
			_exit(1);
		}
		close(ruleset_fd);

		/* Denied READ_DIR on the quiet /tmp: suppressed, logged=0. */
		fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd >= 0)
			close(fd);
		_exit(0);
	}
	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	/* The event fires with the suppressed verdict. */
	EXPECT_LE(1, tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS_LOGGED(
							TRACE_TASK, "0")));
	/* The quiet rule must not leave the denial logged. */
	EXPECT_EQ(0, tracefs_count_matches(buf, REGEX_DENY_ACCESS_FS_LOGGED(
							TRACE_TASK, "1")));

	/*
	 * Quiet suppresses only the logged verdict: the rest of the denial
	 * event stays populated (non-zero domain, non-empty blockers).
	 */
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_ACCESS_FS_LOGGED(TRACE_TASK, "0"),
			     "domain", field, sizeof(field)));
	EXPECT_STRNE("0", field);
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_ACCESS_FS_LOGGED(TRACE_TASK, "0"),
			     "blockers", field, sizeof(field)));
	EXPECT_STRNE("", field);

	free(buf);
}

TEST_HARNESS_MAIN
