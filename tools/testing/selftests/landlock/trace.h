/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock trace test helpers
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kselftest_harness.h"

#define TRACEFS_ROOT "/sys/kernel/tracing"
#define TRACEFS_LANDLOCK_DIR TRACEFS_ROOT "/events/landlock"
#define TRACEFS_CREATE_RULESET_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_create_ruleset/enable"
#define TRACEFS_CREATE_DOMAIN_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_create_domain/enable"
#define TRACEFS_ENFORCE_DOMAIN_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_enforce_domain/enable"
#define TRACEFS_ADD_RULE_FS_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_add_rule_fs/enable"
#define TRACEFS_ADD_RULE_NET_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_add_rule_net/enable"
#define TRACEFS_CHECK_RULE_FS_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_check_rule_fs/enable"
#define TRACEFS_CHECK_RULE_NET_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_check_rule_net/enable"
#define TRACEFS_DENY_ACCESS_FS_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_deny_access_fs/enable"
#define TRACEFS_DENY_ACCESS_NET_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_deny_access_net/enable"
#define TRACEFS_DENY_PTRACE_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_deny_ptrace/enable"
#define TRACEFS_DENY_SCOPE_SIGNAL_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_deny_scope_signal/enable"
#define TRACEFS_DENY_SCOPE_ABSTRACT_UNIX_SOCKET_ENABLE \
	TRACEFS_LANDLOCK_DIR                           \
	"/landlock_deny_scope_abstract_unix_socket/enable"
#define TRACEFS_FREE_DOMAIN_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_free_domain/enable"
#define TRACEFS_FREE_RULESET_ENABLE \
	TRACEFS_LANDLOCK_DIR "/landlock_free_ruleset/enable"
#define TRACEFS_TRACE TRACEFS_ROOT "/trace"
#define TRACEFS_SET_EVENT_PID TRACEFS_ROOT "/set_event_pid"
#define TRACEFS_OPTIONS_EVENT_FORK TRACEFS_ROOT "/options/event-fork"

#define TRACE_BUFFER_SIZE (64 * 1024)

/*
 * Trace line prefix: matches the ftrace "trace" file format.  Format: "
 * <task>-<pid> [<cpu>] <flags> <timestamp>: "
 *
 * The task parameter must be a string literal truncated to 15 chars
 * (TASK_COMM_LEN - 1), matching what the kernel stores in task->comm.  The
 * pattern accepts either the expected task name or "<...>" because the ftrace
 * comm cache may evict short-lived processes (e.g., forked children that exit
 * before the trace buffer is read).
 *
 * No unescaped '.' in any REGEX macro; literal dots use '\\.'.
 */
#define TRACE_PREFIX(task)  \
	"^ *\\(<\\.\\.\\.>" \
	"\\|" task "\\)"    \
	"-[0-9]\\+ *\\[[0-9]\\+\\] [^ ]\\+ \\+[0-9]\\+\\.[0-9]\\+: "

/*
 * Task name for events emitted by kworker threads (e.g., free_domain fires from
 * a work queue, not from the test process).
 */
#define KWORKER_TASK "kworker/[0-9]\\+:[0-9]\\+"

#define REGEX_ADD_RULE_FS(task)           \
	TRACE_PREFIX(task)                \
	"landlock_add_rule_fs: "          \
	"ruleset=[0-9a-f]\\+\\.[0-9]\\+ " \
	"access_rights=[a-z_|]* "         \
	"dev=[0-9]\\+:[0-9]\\+ "          \
	"ino=[0-9]\\+ "                   \
	"path=[^ ]\\+$"

#define REGEX_ADD_RULE_NET(task)          \
	TRACE_PREFIX(task)                \
	"landlock_add_rule_net: "         \
	"ruleset=[0-9a-f]\\+\\.[0-9]\\+ " \
	"access_rights=[a-z_|]* "         \
	"port=[0-9]\\+$"

#define REGEX_CREATE_RULESET(task)        \
	TRACE_PREFIX(task)                \
	"landlock_create_ruleset: "       \
	"ruleset=[0-9a-f]\\+\\.[0-9]\\+ " \
	"handled_fs=[a-z_|]* "            \
	"handled_net=[a-z_|]* "           \
	"scoped=[a-z_|]*$"

#define REGEX_CREATE_DOMAIN(task)  \
	TRACE_PREFIX(task)         \
	"landlock_create_domain: " \
	"domain=[0-9a-f]\\+ "      \
	"parent=[0-9a-f]\\+ "      \
	"ruleset=[0-9a-f]\\+\\.[0-9]\\+$"

#define REGEX_CHECK_RULE_FS(task)  \
	TRACE_PREFIX(task)         \
	"landlock_check_rule_fs: " \
	"domain=[0-9a-f]\\+ "      \
	"access_request=[a-z_|]* " \
	"dev=[0-9]\\+:[0-9]\\+ "   \
	"ino=[0-9]\\+ "            \
	"grants={[a-z_|,]*}$"

#define REGEX_CHECK_RULE_NET(task)  \
	TRACE_PREFIX(task)          \
	"landlock_check_rule_net: " \
	"domain=[0-9a-f]\\+ "       \
	"access_request=[a-z_|]* "  \
	"port=[0-9]\\+ "            \
	"grants={[a-z_|,]*}$"

#define REGEX_DENY_ACCESS_FS(task)  \
	TRACE_PREFIX(task)          \
	"landlock_deny_access_fs: " \
	"domain=[0-9a-f]\\+ "       \
	"same_exec=[01] "           \
	"logged=[01] "              \
	"blockers=[a-z_|]* "        \
	"dev=[0-9]\\+:[0-9]\\+ "    \
	"ino=[0-9]\\+ "             \
	"path=[^ ]*$"

#define REGEX_DENY_ACCESS_NET(task)  \
	TRACE_PREFIX(task)           \
	"landlock_deny_access_net: " \
	"domain=[0-9a-f]\\+ "        \
	"same_exec=[01] "            \
	"logged=[01] "               \
	"blockers=[a-z_|]* "         \
	"sport=[0-9]\\+ "            \
	"dport=[0-9]\\+$"

#define REGEX_DENY_PTRACE(task)      \
	TRACE_PREFIX(task)           \
	"landlock_deny_ptrace: "     \
	"domain=[0-9a-f]\\+ "        \
	"same_exec=[01] "            \
	"logged=[01] "               \
	"tracee_domain=[0-9a-f]\\+ " \
	"tracee_pid=[0-9]\\+ "       \
	"tracee_comm=[^ ]*$"

#define REGEX_DENY_SCOPE_SIGNAL(task)  \
	TRACE_PREFIX(task)             \
	"landlock_deny_scope_signal: " \
	"domain=[0-9a-f]\\+ "          \
	"same_exec=[01] "              \
	"logged=[01] "                 \
	"target_domain=[0-9a-f]\\+ "   \
	"target_pid=[0-9]\\+ "         \
	"target_comm=[^ ]*$"

#define REGEX_DENY_SCOPE_ABSTRACT_UNIX_SOCKET(task)  \
	TRACE_PREFIX(task)                           \
	"landlock_deny_scope_abstract_unix_socket: " \
	"domain=[0-9a-f]\\+ "                        \
	"same_exec=[01] "                            \
	"logged=[01] "                               \
	"peer_domain=[0-9a-f]\\+ "                   \
	"peer_pid=[0-9]\\+ "                         \
	"sun_path=[^ ]*$"

#define REGEX_FREE_DOMAIN(task)  \
	TRACE_PREFIX(task)       \
	"landlock_free_domain: " \
	"domain=[0-9a-f]\\+ "    \
	"denials=[0-9]\\+$"

#define REGEX_FREE_RULESET(task)  \
	TRACE_PREFIX(task)        \
	"landlock_free_ruleset: " \
	"ruleset=[0-9a-f]\\+\\.[0-9]\\+$"

static int __maybe_unused tracefs_write(const char *path, const char *value)
{
	int fd;
	ssize_t ret;
	size_t len = strlen(value);

	fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	ret = write(fd, value, len);
	close(fd);
	if (ret < 0)
		return -errno;
	if ((size_t)ret != len)
		return -EIO;

	return 0;
}

static int __maybe_unused tracefs_write_int(const char *path, int value)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%d", value);
	return tracefs_write(path, buf);
}

static int __maybe_unused tracefs_setup(void)
{
	struct stat st;

	/* Mount tracefs if not already mounted. */
	if (stat(TRACEFS_ROOT, &st) != 0) {
		int ret = mount("tracefs", TRACEFS_ROOT, "tracefs", 0, NULL);

		if (ret)
			return -errno;
	}

	/* Verify landlock events are available. */
	if (stat(TRACEFS_LANDLOCK_DIR, &st) != 0)
		return -ENOENT;

	return 0;
}

/*
 * Set up PID-based event filtering so only events from the current process and
 * its children are recorded.  This is analogous to audit's AUDIT_EXE filter: it
 * prevents events from unrelated processes from polluting the trace buffer.
 */
static int __maybe_unused tracefs_set_pid_filter(pid_t pid)
{
	int ret;

	/* Enable event-fork so children inherit the PID filter. */
	ret = tracefs_write(TRACEFS_OPTIONS_EVENT_FORK, "1");
	if (ret)
		return ret;

	return tracefs_write_int(TRACEFS_SET_EVENT_PID, pid);
}

/* Clear the PID filter to stop filtering by PID. */
static int __maybe_unused tracefs_clear_pid_filter(void)
{
	return tracefs_write(TRACEFS_SET_EVENT_PID, "");
}

static int __maybe_unused tracefs_enable_event(const char *enable_path,
					       bool enable)
{
	return tracefs_write(enable_path, enable ? "1" : "0");
}

static int __maybe_unused tracefs_clear(void)
{
	return tracefs_write(TRACEFS_TRACE, "");
}

/*
 * Reads the trace buffer content into a newly allocated buffer.  The caller is
 * responsible for freeing the returned buffer.  Returns NULL on error.
 */
static char __maybe_unused *tracefs_read_trace(void)
{
	char *buf;
	int fd;
	ssize_t total = 0, ret;

	buf = malloc(TRACE_BUFFER_SIZE);
	if (!buf)
		return NULL;

	fd = open(TRACEFS_TRACE, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		free(buf);
		return NULL;
	}

	while (total < TRACE_BUFFER_SIZE - 1) {
		ret = read(fd, buf + total, TRACE_BUFFER_SIZE - 1 - total);
		if (ret <= 0)
			break;
		total += ret;
	}
	close(fd);
	buf[total] = '\0';
	return buf;
}

/* Counts the number of lines in @buf matching the basic regex @pattern. */
static int __maybe_unused tracefs_count_matches(const char *buf,
						const char *pattern)
{
	regex_t regex;
	int count = 0;
	const char *line, *end;

	if (regcomp(&regex, pattern, 0) != 0)
		return -EINVAL;

	line = buf;
	while (*line) {
		end = strchr(line, '\n');
		if (!end)
			end = line + strlen(line);

		/* Create a temporary NUL-terminated line. */
		size_t len = end - line;
		char *tmp = malloc(len + 1);

		if (tmp) {
			memcpy(tmp, line, len);
			tmp[len] = '\0';
			if (regexec(&regex, tmp, 0, NULL, 0) == 0)
				count++;
			free(tmp);
		}

		if (*end == '\n')
			line = end + 1;
		else
			break;
	}

	regfree(&regex);
	return count;
}

/*
 * Extracts the value of a named field from a trace line in @buf.  Searches for
 * the first line matching @line_pattern, then extracts the value after
 * "@field_name=" into @out.  Stops at space or newline.
 *
 * Returns 0 on success, -ENOENT if no match.
 */
static int __maybe_unused tracefs_extract_field(const char *buf,
						const char *line_pattern,
						const char *field_name,
						char *out, size_t out_size)
{
	regex_t regex;
	const char *line, *end;

	if (regcomp(&regex, line_pattern, 0) != 0)
		return -EINVAL;

	line = buf;
	while (*line) {
		end = strchr(line, '\n');
		if (!end)
			end = line + strlen(line);

		size_t len = end - line;
		char *tmp = malloc(len + 1);

		if (tmp) {
			const char *field, *val_start;
			size_t field_len, val_len;

			memcpy(tmp, line, len);
			tmp[len] = '\0';

			if (regexec(&regex, tmp, 0, NULL, 0) != 0) {
				free(tmp);
				goto next;
			}

			/*
			 * Find "field_name=" in the line, ensuring a word
			 * boundary before the field name to avoid substring
			 * matches (e.g., "port" in "sport").
			 */
			field_len = strlen(field_name);
			field = tmp;
			while ((field = strstr(field, field_name))) {
				if (field[field_len] == '=' &&
				    (field == tmp || field[-1] == ' '))
					break;
				field++;
			}
			if (!field) {
				free(tmp);
				regfree(&regex);
				return -ENOENT;
			}

			val_start = field + field_len + 1;
			val_len = 0;
			while (val_start[val_len] &&
			       val_start[val_len] != ' ' &&
			       val_start[val_len] != '\n')
				val_len++;

			if (val_len >= out_size)
				val_len = out_size - 1;
			memcpy(out, val_start, val_len);
			out[val_len] = '\0';

			free(tmp);
			regfree(&regex);
			return 0;
		}
next:
		if (*end == '\n')
			line = end + 1;
		else
			break;
	}

	regfree(&regex);
	return -ENOENT;
}

/*
 * Common fixture setup for trace tests.  Mounts tracefs if needed and sets a
 * PID filter.  The caller must create a mount namespace first
 * (unshare(CLONE_NEWNS) + mount(MS_REC | MS_PRIVATE)) to isolate the tracefs
 * mount; the trace buffer, per-event enable flags, and PID filter are global
 * kernel state, scoped to the test by the PID filter.
 *
 * Returns 0 on success, -errno on failure (caller should SKIP).
 */
static int __maybe_unused tracefs_fixture_setup(void)
{
	int ret;

	ret = tracefs_setup();
	if (ret)
		return ret;

	return tracefs_set_pid_filter(getpid());
}

static void __maybe_unused tracefs_fixture_teardown(void)
{
	tracefs_clear_pid_filter();
}

/*
 * Temporarily raises CAP_SYS_ADMIN effective capability, calls @func, then
 * drops the capability.  Returns the value from @func, or -EPERM if the
 * capability manipulation fails.
 */
static int __maybe_unused tracefs_priv_call(int (*func)(void))
{
	const cap_value_t admin = CAP_SYS_ADMIN;
	cap_t cap_p;
	int ret;

	cap_p = cap_get_proc();
	if (!cap_p)
		return -EPERM;

	if (cap_set_flag(cap_p, CAP_EFFECTIVE, 1, &admin, CAP_SET) ||
	    cap_set_proc(cap_p)) {
		cap_free(cap_p);
		return -EPERM;
	}

	ret = func();

	cap_set_flag(cap_p, CAP_EFFECTIVE, 1, &admin, CAP_CLEAR);
	cap_set_proc(cap_p);
	cap_free(cap_p);
	return ret;
}

/* Read the trace buffer with elevated privileges.  Returns NULL on failure. */
static char __maybe_unused *tracefs_read_buf(void)
{
	/* Cannot use tracefs_priv_call() because the return type is char *. */
	cap_t cap_p;
	char *buf;
	const cap_value_t admin = CAP_SYS_ADMIN;

	cap_p = cap_get_proc();
	if (!cap_p)
		return NULL;

	if (cap_set_flag(cap_p, CAP_EFFECTIVE, 1, &admin, CAP_SET) ||
	    cap_set_proc(cap_p)) {
		cap_free(cap_p);
		return NULL;
	}

	buf = tracefs_read_trace();

	cap_set_flag(cap_p, CAP_EFFECTIVE, 1, &admin, CAP_CLEAR);
	cap_set_proc(cap_p);
	cap_free(cap_p);
	return buf;
}

/* Clear the trace buffer with elevated privileges.  Returns 0 on success. */
static int __maybe_unused tracefs_clear_buf(void)
{
	return tracefs_priv_call(tracefs_clear);
}

/*
 * Forks a child that creates a Landlock sandbox and performs an FS access.  The
 * parent waits for the child, then reads the trace buffer.
 *
 * Requires common.h and wrappers.h to be included before trace.h.
 */
static void __maybe_unused sandbox_child_fs_access(
	struct __test_metadata *const _metadata, const char *rule_path,
	__u64 handled_access, __u64 allowed_access, const char *access_path)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr ruleset_attr = {
			.handled_access_fs = handled_access,
		};
		struct landlock_path_beneath_attr path_beneath = {
			.allowed_access = allowed_access,
		};
		int ruleset_fd, fd;

		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		path_beneath.parent_fd =
			open(rule_path, O_PATH | O_DIRECTORY | O_CLOEXEC);
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

		fd = open(access_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd >= 0)
			close(fd);

		_exit(0);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

/*
 * Forks a child that creates a Landlock sandbox allowing execute+read_dir for
 * /usr and execute-only for ".", then execs ./true.  The true binary opens "."
 * on startup, triggering a read_dir denial with same_exec=0.  The parent waits
 * for the child to exit.
 */
static void __maybe_unused sandbox_child_exec_true(
	struct __test_metadata *const _metadata, __u32 restrict_flags)
{
	pid_t pid;
	int status;

	pid = fork();
	ASSERT_LE(0, pid);

	if (pid == 0) {
		struct landlock_ruleset_attr attr = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_DIR |
					     LANDLOCK_ACCESS_FS_EXECUTE,
		};
		struct landlock_path_beneath_attr path_beneath = {
			.allowed_access = LANDLOCK_ACCESS_FS_EXECUTE |
					  LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int ruleset_fd;

		ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
		if (ruleset_fd < 0)
			_exit(1);

		path_beneath.parent_fd =
			open("/usr", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd >= 0) {
			landlock_add_rule(ruleset_fd,
					  LANDLOCK_RULE_PATH_BENEATH,
					  &path_beneath, 0);
			close(path_beneath.parent_fd);
		}

		path_beneath.allowed_access = LANDLOCK_ACCESS_FS_EXECUTE;
		path_beneath.parent_fd =
			open(".", O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (path_beneath.parent_fd >= 0) {
			landlock_add_rule(ruleset_fd,
					  LANDLOCK_RULE_PATH_BENEATH,
					  &path_beneath, 0);
			close(path_beneath.parent_fd);
		}

		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd, restrict_flags))
			_exit(1);
		close(ruleset_fd);

		execl("./true", "./true", NULL);
		_exit(1);
	}

	ASSERT_EQ(pid, waitpid(pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}
