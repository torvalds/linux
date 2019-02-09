#define _GNU_SOURCE

#include <cap-ng.h>
#include <err.h>
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

#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT			47
# define PR_CAP_AMBIENT_IS_SET		1
# define PR_CAP_AMBIENT_RAISE		2
# define PR_CAP_AMBIENT_LOWER		3
# define PR_CAP_AMBIENT_CLEAR_ALL	4
#endif

static int nerrs;

static void vmaybe_write_file(bool enoent_ok, char *filename, char *fmt, va_list ap)
{
	char buf[4096];
	int fd;
	ssize_t written;
	int buf_len;

	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (buf_len < 0) {
		err(1, "vsnprintf failed");
	}
	if (buf_len >= sizeof(buf)) {
		errx(1, "vsnprintf output truncated");
	}

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		if ((errno == ENOENT) && enoent_ok)
			return;
		err(1, "open of %s failed", filename);
	}
	written = write(fd, buf, buf_len);
	if (written != buf_len) {
		if (written >= 0) {
			errx(1, "short write to %s", filename);
		} else {
			err(1, "write to %s failed", filename);
		}
	}
	if (close(fd) != 0) {
		err(1, "close of %s failed", filename);
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

	/*
	 * TODO: If we're already root, we could skip creating the userns.
	 */

	if (unshare(CLONE_NEWNS) == 0) {
		printf("[NOTE]\tUsing global UIDs for tests\n");
		if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) != 0)
			err(1, "PR_SET_KEEPCAPS");
		if (setresuid(inner_uid, inner_uid, -1) != 0)
			err(1, "setresuid");

		// Re-enable effective caps
		capng_get_caps_process();
		for (i = 0; i < CAP_LAST_CAP; i++)
			if (capng_have_capability(CAPNG_PERMITTED, i))
				capng_update(CAPNG_ADD, CAPNG_EFFECTIVE, i);
		if (capng_apply(CAPNG_SELECT_CAPS) != 0)
			err(1, "capng_apply");

		have_outer_privilege = true;
	} else if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == 0) {
		printf("[NOTE]\tUsing a user namespace for tests\n");
		maybe_write_file("/proc/self/setgroups", "deny");
		write_file("/proc/self/uid_map", "%d %d 1", inner_uid, outer_uid);
		write_file("/proc/self/gid_map", "0 %d 1", outer_gid);

		have_outer_privilege = false;
	} else {
		errx(1, "must be root or be able to create a userns");
	}

	if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
		err(1, "remount everything private");

	return have_outer_privilege;
}

static void chdir_to_tmpfs(void)
{
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) != cwd)
		err(1, "getcwd");

	if (mount("private_tmp", ".", "tmpfs", 0, "mode=0777") != 0)
		err(1, "mount private tmpfs");

	if (chdir(cwd) != 0)
		err(1, "chdir to private tmpfs");

	if (umount2(".", MNT_DETACH) != 0)
		err(1, "detach private tmpfs");
}

static void copy_fromat_to(int fromfd, const char *fromname, const char *toname)
{
	int from = openat(fromfd, fromname, O_RDONLY);
	if (from == -1)
		err(1, "open copy source");

	int to = open(toname, O_CREAT | O_WRONLY | O_EXCL, 0700);

	while (true) {
		char buf[4096];
		ssize_t sz = read(from, buf, sizeof(buf));
		if (sz == 0)
			break;
		if (sz < 0)
			err(1, "read");

		if (write(to, buf, sz) != sz)
			err(1, "write");	/* no short writes on tmpfs */
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
			printf("[FAIL]\tChild died\n");
			nerrs++;
		} else if (WEXITSTATUS(status) != 0) {
			printf("[FAIL]\tChild failed\n");
			nerrs++;
		} else {
			printf("[OK]\tChild succeeded\n");
		}

		return false;
	} else {
		err(1, "fork");
	}
}

static void exec_other_validate_cap(const char *name,
				    bool eff, bool perm, bool inh, bool ambient)
{
	execl(name, name, (eff ? "1" : "0"),
	      (perm ? "1" : "0"), (inh ? "1" : "0"), (ambient ? "1" : "0"),
	      NULL);
	err(1, "execl");
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
		err(1, "open '%s'", our_path);

	chdir_to_tmpfs();

	copy_fromat_to(ourpath_fd, "validate_cap", "validate_cap");

	if (have_outer_privilege) {
		uid_t gid = getegid();

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_suidroot");
		if (chown("validate_cap_suidroot", 0, -1) != 0)
			err(1, "chown");
		if (chmod("validate_cap_suidroot", S_ISUID | 0700) != 0)
			err(1, "chmod");

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_suidnonroot");
		if (chown("validate_cap_suidnonroot", uid + 1, -1) != 0)
			err(1, "chown");
		if (chmod("validate_cap_suidnonroot", S_ISUID | 0700) != 0)
			err(1, "chmod");

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_sgidroot");
		if (chown("validate_cap_sgidroot", -1, 0) != 0)
			err(1, "chown");
		if (chmod("validate_cap_sgidroot", S_ISGID | 0710) != 0)
			err(1, "chmod");

		copy_fromat_to(ourpath_fd, "validate_cap",
			       "validate_cap_sgidnonroot");
		if (chown("validate_cap_sgidnonroot", -1, gid + 1) != 0)
			err(1, "chown");
		if (chmod("validate_cap_sgidnonroot", S_ISGID | 0710) != 0)
			err(1, "chmod");
}

	capng_get_caps_process();

	/* Make sure that i starts out clear */
	capng_update(CAPNG_DROP, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		err(1, "capng_apply");

	if (uid == 0) {
		printf("[RUN]\tRoot => ep\n");
		if (fork_wait())
			exec_validate_cap(true, true, false, false);
	} else {
		printf("[RUN]\tNon-root => no caps\n");
		if (fork_wait())
			exec_validate_cap(false, false, false, false);
	}

	printf("[OK]\tCheck cap_ambient manipulation rules\n");

	/* We should not be able to add ambient caps yet. */
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != -1 || errno != EPERM) {
		if (errno == EINVAL)
			printf("[FAIL]\tPR_CAP_AMBIENT_RAISE isn't supported\n");
		else
			printf("[FAIL]\tPR_CAP_AMBIENT_RAISE should have failed eith EPERM on a non-inheritable cap\n");
		return 1;
	}
	printf("[OK]\tPR_CAP_AMBIENT_RAISE failed on non-inheritable cap\n");

	capng_update(CAPNG_ADD, CAPNG_INHERITABLE, CAP_NET_RAW);
	capng_update(CAPNG_DROP, CAPNG_PERMITTED, CAP_NET_RAW);
	capng_update(CAPNG_DROP, CAPNG_EFFECTIVE, CAP_NET_RAW);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		err(1, "capng_apply");
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_RAW, 0, 0, 0) != -1 || errno != EPERM) {
		printf("[FAIL]\tPR_CAP_AMBIENT_RAISE should have failed on a non-permitted cap\n");
		return 1;
	}
	printf("[OK]\tPR_CAP_AMBIENT_RAISE failed on non-permitted cap\n");

	capng_update(CAPNG_ADD, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		err(1, "capng_apply");
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0) {
		printf("[FAIL]\tPR_CAP_AMBIENT_RAISE should have succeeded\n");
		return 1;
	}
	printf("[OK]\tPR_CAP_AMBIENT_RAISE worked\n");

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != 1) {
		printf("[FAIL]\tPR_CAP_AMBIENT_IS_SET is broken\n");
		return 1;
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0, 0) != 0)
		err(1, "PR_CAP_AMBIENT_CLEAR_ALL");

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0) {
		printf("[FAIL]\tPR_CAP_AMBIENT_CLEAR_ALL didn't work\n");
		return 1;
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0)
		err(1, "PR_CAP_AMBIENT_RAISE");

	capng_update(CAPNG_DROP, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		err(1, "capng_apply");

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0) {
		printf("[FAIL]\tDropping I should have dropped A\n");
		return 1;
	}

	printf("[OK]\tBasic manipulation appears to work\n");

	capng_update(CAPNG_ADD, CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE);
	if (capng_apply(CAPNG_SELECT_CAPS) != 0)
		err(1, "capng_apply");
	if (uid == 0) {
		printf("[RUN]\tRoot +i => eip\n");
		if (fork_wait())
			exec_validate_cap(true, true, true, false);
	} else {
		printf("[RUN]\tNon-root +i => i\n");
		if (fork_wait())
			exec_validate_cap(false, false, true, false);
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0, 0) != 0)
		err(1, "PR_CAP_AMBIENT_RAISE");

	printf("[RUN]\tUID %d +ia => eipa\n", uid);
	if (fork_wait())
		exec_validate_cap(true, true, true, true);

	/* The remaining tests need real privilege */

	if (!have_outer_privilege) {
		printf("[SKIP]\tSUID/SGID tests (needs privilege)\n");
		goto done;
	}

	if (uid == 0) {
		printf("[RUN]\tRoot +ia, suidroot => eipa\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_suidroot",
						true, true, true, true);

		printf("[RUN]\tRoot +ia, suidnonroot => ip\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_suidnonroot",
						false, true, true, false);

		printf("[RUN]\tRoot +ia, sgidroot => eipa\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_sgidroot",
						true, true, true, true);

		if (fork_wait()) {
			printf("[RUN]\tRoot, gid != 0, +ia, sgidroot => eip\n");
			if (setresgid(1, 1, 1) != 0)
				err(1, "setresgid");
			exec_other_validate_cap("./validate_cap_sgidroot",
						true, true, true, false);
		}

		printf("[RUN]\tRoot +ia, sgidnonroot => eip\n");
		if (fork_wait())
			exec_other_validate_cap("./validate_cap_sgidnonroot",
						true, true, true, false);
	} else {
		printf("[RUN]\tNon-root +ia, sgidnonroot => i\n");
		exec_other_validate_cap("./validate_cap_sgidnonroot",
						false, false, true, false);

		if (fork_wait()) {
			printf("[RUN]\tNon-root +ia, sgidroot => i\n");
			if (setresgid(1, 1, 1) != 0)
				err(1, "setresgid");
			exec_other_validate_cap("./validate_cap_sgidroot",
						false, false, true, false);
		}
	}

done:
	return nerrs ? 1 : 0;
}

int main(int argc, char **argv)
{
	char *tmp1, *tmp2, *our_path;

	/* Find our path */
	tmp1 = strdup(argv[0]);
	if (!tmp1)
		err(1, "strdup");
	tmp2 = dirname(tmp1);
	our_path = strdup(tmp2);
	if (!our_path)
		err(1, "strdup");
	free(tmp1);

	if (fork_wait()) {
		printf("[RUN]\t+++ Tests with uid == 0 +++\n");
		return do_tests(0, our_path);
	}

	if (fork_wait()) {
		printf("[RUN]\t+++ Tests with uid != 0 +++\n");
		return do_tests(1, our_path);
	}

	return nerrs ? 1 : 0;
}
