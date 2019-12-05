// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/wait.h>

#include "pidfd.h"
#include "../kselftest.h"

struct error {
	int  code;
	char msg[512];
};

static int error_set(struct error *err, int code, const char *fmt, ...)
{
	va_list args;
	int r;

	if (code == PIDFD_PASS || !err || err->code != PIDFD_PASS)
		return code;

	err->code = code;
	va_start(args, fmt);
	r = vsnprintf(err->msg, sizeof(err->msg), fmt, args);
	assert((size_t)r < sizeof(err->msg));
	va_end(args);

	return code;
}

static void error_report(struct error *err, const char *test_name)
{
	switch (err->code) {
	case PIDFD_ERROR:
		ksft_exit_fail_msg("%s test: Fatal: %s\n", test_name, err->msg);
		break;

	case PIDFD_FAIL:
		/* will be: not ok %d # error %s test: %s */
		ksft_test_result_error("%s test: %s\n", test_name, err->msg);
		break;

	case PIDFD_SKIP:
		/* will be: not ok %d # SKIP %s test: %s */
		ksft_test_result_skip("%s test: %s\n", test_name, err->msg);
		break;

	case PIDFD_XFAIL:
		ksft_test_result_pass("%s test: Expected failure: %s\n",
				      test_name, err->msg);
		break;

	case PIDFD_PASS:
		ksft_test_result_pass("%s test: Passed\n");
		break;

	default:
		ksft_exit_fail_msg("%s test: Unknown code: %d %s\n",
				   test_name, err->code, err->msg);
		break;
	}
}

static inline int error_check(struct error *err, const char *test_name)
{
	/* In case of error we bail out and terminate the test program */
	if (err->code == PIDFD_ERROR)
		error_report(err, test_name);

	return err->code;
}

struct child {
	pid_t pid;
	int   fd;
};

static struct child clone_newns(int (*fn)(void *), void *args,
				struct error *err)
{
	static int flags = CLONE_PIDFD | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;
	size_t stack_size = 1024;
	char *stack[1024] = { 0 };
	struct child ret;

	if (!(flags & CLONE_NEWUSER) && geteuid() != 0)
		flags |= CLONE_NEWUSER;

#ifdef __ia64__
	ret.pid = __clone2(fn, stack, stack_size, flags, args, &ret.fd);
#else
	ret.pid = clone(fn, stack + stack_size, flags, args, &ret.fd);
#endif

	if (ret.pid < 0) {
		error_set(err, PIDFD_ERROR, "clone failed (ret %d, errno %d)",
			  ret.fd, errno);
		return ret;
	}

	ksft_print_msg("New child: %d, fd: %d\n", ret.pid, ret.fd);

	return ret;
}

static inline void child_close(struct child *child)
{
	close(child->fd);
}

static inline int child_join(struct child *child, struct error *err)
{
	int r;

	r = wait_for_pid(child->pid);
	if (r < 0)
		error_set(err, PIDFD_ERROR, "waitpid failed (ret %d, errno %d)",
			  r, errno);
	else if (r > 0)
		error_set(err, r, "child %d reported: %d", child->pid, r);

	return r;
}

static inline int child_join_close(struct child *child, struct error *err)
{
	child_close(child);
	return child_join(child, err);
}

static inline void trim_newline(char *str)
{
	char *pos = strrchr(str, '\n');

	if (pos)
		*pos = '\0';
}

static int verify_fdinfo(int pidfd, struct error *err, const char *prefix,
			 size_t prefix_len, const char *expect, ...)
{
	char buffer[512] = {0, };
	char path[512] = {0, };
	va_list args;
	FILE *f;
	char *line = NULL;
	size_t n = 0;
	int found = 0;
	int r;

	va_start(args, expect);
	r = vsnprintf(buffer, sizeof(buffer), expect, args);
	assert((size_t)r < sizeof(buffer));
	va_end(args);

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", pidfd);
	f = fopen(path, "re");
	if (!f)
		return error_set(err, PIDFD_ERROR, "fdinfo open failed for %d",
				 pidfd);

	while (getline(&line, &n, f) != -1) {
		char *val;

		if (strncmp(line, prefix, prefix_len))
			continue;

		found = 1;

		val = line + prefix_len;
		r = strcmp(val, buffer);
		if (r != 0) {
			trim_newline(line);
			trim_newline(buffer);
			error_set(err, PIDFD_FAIL, "%s '%s' != '%s'",
				  prefix, val, buffer);
		}
		break;
	}

	free(line);
	fclose(f);

	if (found == 0)
		return error_set(err, PIDFD_FAIL, "%s not found for fd %d",
				 prefix, pidfd);

	return PIDFD_PASS;
}

static int child_fdinfo_nspid_test(void *args)
{
	struct error err;
	int pidfd;
	int r;

	/* if we got no fd for the sibling, we are done */
	if (!args)
		return PIDFD_PASS;

	/* verify that we can not resolve the pidfd for a process
	 * in a sibling pid namespace, i.e. a pid namespace it is
	 * not in our or a descended namespace
	 */
	r = mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0);
	if (r < 0) {
		ksft_print_msg("Failed to remount / private\n");
		return PIDFD_ERROR;
	}

	(void)umount2("/proc", MNT_DETACH);
	r = mount("proc", "/proc", "proc", 0, NULL);
	if (r < 0) {
		ksft_print_msg("Failed to remount /proc\n");
		return PIDFD_ERROR;
	}

	pidfd = *(int *)args;
	r = verify_fdinfo(pidfd, &err, "NSpid:", 6, "\t0\n");

	if (r != PIDFD_PASS)
		ksft_print_msg("NSpid fdinfo check failed: %s\n", err.msg);

	return r;
}

static void test_pidfd_fdinfo_nspid(void)
{
	struct child a, b;
	struct error err = {0, };
	const char *test_name = "pidfd check for NSpid in fdinfo";

	/* Create a new child in a new pid and mount namespace */
	a = clone_newns(child_fdinfo_nspid_test, NULL, &err);
	error_check(&err, test_name);

	/* Pass the pidfd representing the first child to the
	 * second child, which will be in a sibling pid namespace,
	 * which means that the fdinfo NSpid entry for the pidfd
	 * should only contain '0'.
	 */
	b = clone_newns(child_fdinfo_nspid_test, &a.fd, &err);
	error_check(&err, test_name);

	/* The children will have pid 1 in the new pid namespace,
	 * so the line must be 'NSPid:\t<pid>\t1'.
	 */
	verify_fdinfo(a.fd, &err, "NSpid:", 6, "\t%d\t%d\n", a.pid, 1);
	verify_fdinfo(b.fd, &err, "NSpid:", 6, "\t%d\t%d\n", b.pid, 1);

	/* wait for the process, check the exit status and set
	 * 'err' accordingly, if it is not already set.
	 */
	child_join_close(&a, &err);
	child_join_close(&b, &err);

	error_report(&err, test_name);
}

static void test_pidfd_dead_fdinfo(void)
{
	struct child a;
	struct error err = {0, };
	const char *test_name = "pidfd check fdinfo for dead process";

	/* Create a new child in a new pid and mount namespace */
	a = clone_newns(child_fdinfo_nspid_test, NULL, &err);
	error_check(&err, test_name);
	child_join(&a, &err);

	verify_fdinfo(a.fd, &err, "Pid:", 4, "\t-1\n");
	verify_fdinfo(a.fd, &err, "NSpid:", 6, "\t-1\n");
	child_close(&a);
	error_report(&err, test_name);
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(2);

	test_pidfd_fdinfo_nspid();
	test_pidfd_dead_fdinfo();

	return ksft_exit_pass();
}
