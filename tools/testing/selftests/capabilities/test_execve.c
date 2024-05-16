// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <cap-ng.h>
#include <linux/capability.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sched.h>
#include <sys/mount.h>
#include <limits.h>
#include <libgen.h>
#include <malloc.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include "../kselftest.h"

static int nerrs;
static pid_t mpid;	/*  main() pid is used to avoid duplicate test counts */

static void vmaybe_write_file(bool enoent_ok, char *filename, char *fmt, va_list ap)
{
	char buf[4096];
	int fd;
	ssize_t written;
	int buf_len;

	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (buf_len < 0)
		ksft_exit_fail_msg("vsnprintf failed - %s\n", strerror(errno));

	if (buf_len >= sizeof(buf))
		ksft_exit_fail_msg("vsnprintf output truncated\n");


	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		if ((errno == ENOENT) && enoent_ok)
			return;
		ksft_exit_fail_msg("open of %s failed - %s\n",
					filename, strerror(errno));
	}
	written = write(fd, buf, buf_len);
	if (written != buf_len) {
		if (written >= 0) {
			ksft_exit_fail_msg("short write to %s\n", filename);
		} else {
			ksft_exit_fail_msg("write to %s failed - %s\n",
						filename, strerror(errno));
		}
	}
	if (close(fd) != 0) {
		ksft_exit_fail_msg("close of %s failed - %s\n",
					filename, strerror(errno));
	}
}

static void maybe_write_file(char *filename, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmaybe_write_file(true, filename, fmt, ap);
	va_end(ap);
}

static void write_file(char *filename, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vmaybe_write_file(false, filename, fmt, ap);
	va_end(ap);
}

static bool create_and_enter_ns(uid_t inner_uid)
{
	uid_t outer_uid;
	gid_t outer_gid;
	int i;
	bool have_outer_privilege;

	outer_uid = getuid();
	outer_gid = getgid();

	if (outer_uid == 0 && unshare(CLONE_NEWNS) == 0) {
		ksft_print_msg("[NOTE]\tUsing global UIDs for tests\n");
		if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) != 0)
			ksft_exit_fail_msg("PR_SET_KEEPCAPS - %s\n",
						strerror(errno));
		if (setresuid(inner_uid, inner_uid, -1) != 0)
			ksft_exit_fail_msg("setresuid - %s\n", strerror(errno));

		// Re-enable effective caps
		capng_get_caps_process();
		for (i = 0; i < CAP_LAST_CAP; i++)
			if (capng_have_capability(CAPNG_PERMITTED, i))
				capng_update(CAPNG_ADD, CAPNG_EFFECTIVE, i);
		if (capng_apply(CAPNG_SELECT_CAPS) != 0)
			ksft_exit_fail_msg(
					"capng_apply - %s\n", strerror(errno));

		have_outer_privilege = true;
	} else if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == 0) {
		ksft_print_msg("[NOTE]\tUsing a user namespace for tests\n");
		maybe_write_file("/proc/self/setgroups", "deny");
		write_file("/proc/self/uid_map", "%d %d 1", inner_uid, outer_uid);
		write_file("/proc/self/gid_map", "0 %d 1", outer_gid);

		have_outer_privilege = false;
	} else {
		ksft_exit_skip("must be root or be able to create a userns\n");
	}

	if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
		ksft_exit_fail_msg("remount everything private - %s\n",
					strerror(errno));

	return have_outer_privilege;
}

static void chdir_to_tmpfs(void)
{
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != cwd)
		ksft_exit_fail_msg("getcwd - %s\n", strerror(errno));

	if (mount("private_tmp", ".", "tmpfs", 0, "mode=0777") != 0)
		ksft_exit_fail_msg("mount private tmpfs - %s\n",
					strerror(errno));

	if (chdir(cwd) != 0)
		ksft_exit_fail_msg("chdir to private tmpfs - %s\n",
					strerror(errno));
}

static void copy_fromat_to(int fromfd, const char *fromname, const char *toname)
{
	int from = openat(fromfd, fromname, O_RDONLY);
	if (from == -1)
		ksft_exit_fail_msg("open copy source - %s\n", strerror(errno));

	int to = open(toname, O_CREAT | O_WRONLY | O_EXCL, 0700);

	while (true) {
		char buf[4096];
		ssize_t sz = read(from, buf, sizeof(buf));
		if (sz == 0)
			break;
		if (sz < 0)
			ksft_exit_fail_msg("read - %s\n", strerror(errno));

		if (write(to, buf, sz) != sz)
			/* no short writes on tmpfs */
			ksft_exit_fail_msg("write - %s\n", strerror(errno));
	}

	close(from);
	close(to);
}

static bool fork_wait(void)
{
	pid_t child = fork();
	if (child == 0) {
		nerrs = 0;
		return true;
	} else if (child > 0) {
		int status;
		if (waitpid(child, &status, 0) != child ||
		    !WIFEXITED(status)) {
			ksft_print_msg("Child died\n");
			nerrs++;
		} else if (WEXITSTATUS(status) != 0) {
			ksft_print_msg("Child failed\n");
			nerrs++;
		} else {
			/* don't print this message for mpid */
			if (getpid() != mpid)
				ksft_test_result_pass("Passed\n");
		}
		return false;
	} else {
		ksft_exit_fail_msg("fork - %s\n", strerror(errno));
		return false;
	}
}

static void exec_other_validate_cap(const char *name,
				    bool eff, bool perm, bool inh, bool ambient)
{
	execl(name, name, (eff ? "1" : "0"),
	      (perm ? "1" : "0"), (inh ? "1" : "0"), (ambient ? "1" : "0"),
	      NULL);
	ksft_exit_fail_msg("execl - %s\n", strerror(errno));
}

static void exec_validate_cap(bool eff, bool perm, bool inh, bool ambient)
{
	exec_other_validate_cap("./validate_cap", eff, perm, inh, ambient);
}

static int do_tests(int uid, const char *our_path)
{
	bool have_outer_privilege = create_and_enter_ns(uid);

	int ourpath_fd = open(our_path, O_RDONLY | O_DIRECTORY);
	if (ourpath_fd == -1)
		ksft_exit_fail_msg("open '%s' - %s\n",
					our_path, strerror(errno));

	chdir_to_tmpfs();

	copy_fromat_to(ourpath_fd, "validate_cap", "validate_cap");

	if (have_outer_privilege) {
		uid_t gid = getegid();

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_suidroot");
		if (chown("validate_cap_suidroot", 0, -1) != 0)
			ksft_exit_fail_msg("chown - %s\n", strerror(errno));
		if (chmod("validate_cap_suidroot", S_ISUID | 0700) != 0)
			ksft_exit_fail_msg("chmod - %s\n", strerror(errno));

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_suidnonroot");
		if (chown("validate_cap_suidnonroot", uid + 1, -1) != 0)
			ksft_exit_fail_msg("chown - %s\n", strerror(errno));
		if (chmod("validate_cap_suidnonroot", S_ISUID | 0700) != 0)
			ksft_exit_fail_msg("chmod - %s\n", strerror(errno));

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_sgidroot");
		if (chown("validate_cap_sgidroot", -1, 0) != 0)
			ksft_exit_fail_msg("chown - %s\n", strerror(errno));
		if (chmod("validate_cap_sgidroot", S_ISGID | 0710) != 0)
			ksft_exit_fail_msg("chmod - %s\n", strerror(errno));

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_sgidnonroot");
		if (chown("validate_cap_sgidnonroot", -1, gid + 1) != 0)
			ksft_exit_fail_msg("chown - %s\n", strerror(errno));
		if (chmod("validate_cap_sgidnonroot", S_ISGID | 0710) != 0)
			ksft_exit_fail_msg("chmod - %s\n", strerror(errno));
	}

	capng_get_caps_process();

	/* Make sure that i starts out clear */
	capng_update(CAPNG_DROP, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		ksft_exit_fail_msg("capng_apply - %s\n", strerror(errno));

	if (uid == 0) {
		ksft_print_msg("[RUN]\tRoot => ep\n");
		if (fork_wait())
			exec_validate_cap(true, true, false, false);
	} else {
		ksft_print_msg("[RUN]\tNon-root => no caps\n");
		if (fork_wait())
			exec_validate_cap(false, false, false, false);
	}

	ksft_print_msg("Check cap_ambient manipulation rules\n");

	/* We should not be able to add ambient caps yet. */
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != -1 || errno != EPERM) {
		if (errno == EINVAL)
			ksft_test_result_fail(
				"PR_CAP_AMBIENT_RAISE isn't supported\n");
		else
			ksft_test_result_fail(
				"PR_CAP_AMBIENT_RAISE should have failed eith EPERM on a non-inheritable cap\n");
		return 1;
	}
	ksft_test_result_pass(
		"PR_CAP_AMBIENT_RAISE failed on non-inheritable cap\n");

	capng_update(CAPNG_ADD, CAPNG_INHERITABLE, CAP_NET_RAW);
	capng_update(CAPNG_DROP, CAPNG_PERMITTED, CAP_NET_RAW);
	capng_update(CAPNG_DROP, CAPNG_EFFECTIVE, CAP_NET_RAW);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		ksft_exit_fail_msg("capng_apply - %s\n", strerror(errno));
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_RAW, 0, 0, 0) != -1 || errno != EPERM) {
		ksft_test_result_fail(
			"PR_CAP_AMBIENT_RAISE should have failed on a non-permitted cap\n");
		return 1;
	}
	ksft_test_result_pass(
		"PR_CAP_AMBIENT_RAISE failed on non-permitted cap\n");

	capng_update(CAPNG_ADD, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		ksft_exit_fail_msg("capng_apply - %s\n", strerror(errno));
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0) {
		ksft_test_result_fail(
			"PR_CAP_AMBIENT_RAISE should have succeeded\n");
		return 1;
	}
	ksft_test_result_pass("PR_CAP_AMBIENT_RAISE worked\n");

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != 1) {
		ksft_test_result_fail("PR_CAP_AMBIENT_IS_SET is broken\n");
		return 1;
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0, 0) != 0)
		ksft_exit_fail_msg("PR_CAP_AMBIENT_CLEAR_ALL - %s\n",
					strerror(errno));

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0) {
		ksft_test_result_fail(
			"PR_CAP_AMBIENT_CLEAR_ALL didn't work\n");
		return 1;
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0)
		ksft_exit_fail_msg("PR_CAP_AMBIENT_RAISE - %s\n",
					strerror(errno));

	capng_update(CAPNG_DROP, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		ksft_exit_fail_msg("capng_apply - %s\n", strerror(errno));

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0) {
		ksft_test_result_fail("Dropping I should have dropped A\n");
		return 1;
	}

	ksft_test_result_pass("Basic manipulation appears to work\n");

	capng_update(CAPNG_ADD, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		ksft_exit_fail_msg("capng_apply - %s\n", strerror(errno));
	if (uid == 0) {
		ksft_print_msg("[RUN]\tRoot +i => eip\n");
		if (fork_wait())
			exec_validate_cap(true, true, true, false);
	} else {
		ksft_print_msg("[RUN]\tNon-root +i => i\n");
		if (fork_wait())
			exec_validate_cap(false, false, true, false);
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0)
		ksft_exit_fail_msg("PR_CAP_AMBIENT_RAISE - %s\n",
					strerror(errno));

	ksft_print_msg("[RUN]\tUID %d +ia => eipa\n", uid);
	if (fork_wait())
		exec_validate_cap(true, true, true, true);

	/* The remaining tests need real privilege */

	if (!have_outer_privilege) {
		ksft_test_result_skip("SUID/SGID tests (needs privilege)\n");
		goto done;
	}

	if (uid == 0) {
		ksft_print_msg("[RUN]\tRoot +ia, suidroot => eipa\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_suidroot",
						true, true, true, true);

		ksft_print_msg("[RUN]\tRoot +ia, suidnonroot => ip\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_suidnonroot",
						false, true, true, false);

		ksft_print_msg("[RUN]\tRoot +ia, sgidroot => eipa\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_sgidroot",
						true, true, true, true);

		if (fork_wait()) {
			ksft_print_msg(
				"[RUN]\tRoot, gid != 0, +ia, sgidroot => eip\n");
			if (setresgid(1, 1, 1) != 0)
				ksft_exit_fail_msg("setresgid - %s\n",
							strerror(errno));
			exec_other_validate_cap("./validate_cap_sgidroot",
						true, true, true, false);
		}

		ksft_print_msg("[RUN]\tRoot +ia, sgidnonroot => eip\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_sgidnonroot",
						true, true, true, false);
	} else {
		ksft_print_msg("[RUN]\tNon-root +ia, sgidnonroot => i\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_sgidnonroot",
					false, false, true, false);

		if (fork_wait()) {
			ksft_print_msg("[RUN]\tNon-root +ia, sgidroot => i\n");
			if (setresgid(1, 1, 1) != 0)
				ksft_exit_fail_msg("setresgid - %s\n",
							strerror(errno));
			exec_other_validate_cap("./validate_cap_sgidroot",
						false, false, true, false);
		}
	}

done:
	ksft_print_cnts();
	return nerrs ? 1 : 0;
}

int main(int argc, char **argv)
{
	char *tmp1, *tmp2, *our_path;

	/* Find our path */
	tmp1 = strdup(argv[0]);
	if (!tmp1)
		ksft_exit_fail_msg("strdup - %s\n", strerror(errno));
	tmp2 = dirname(tmp1);
	our_path = strdup(tmp2);
	if (!our_path)
		ksft_exit_fail_msg("strdup - %s\n", strerror(errno));
	free(tmp1);

	mpid = getpid();

	if (fork_wait()) {
		ksft_print_header();
		ksft_set_plan(12);
		ksft_print_msg("[RUN]\t+++ Tests with uid == 0 +++\n");
		return do_tests(0, our_path);
	}

	ksft_print_msg("==================================================\n");

	if (fork_wait()) {
		ksft_print_header();
		ksft_set_plan(9);
		ksft_print_msg("[RUN]\t+++ Tests with uid != 0 +++\n");
		return do_tests(1, our_path);
	}

	return nerrs ? 1 : 0;
}
