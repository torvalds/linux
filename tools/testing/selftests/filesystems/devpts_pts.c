// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/wait.h>

static bool terminal_dup2(int duplicate, int original)
{
	int ret;

	ret = dup2(duplicate, original);
	if (ret < 0)
		return false;

	return true;
}

static int terminal_set_stdfds(int fd)
{
	int i;

	if (fd < 0)
		return 0;

	for (i = 0; i < 3; i++)
		if (!terminal_dup2(fd, (int[]){STDIN_FILENO, STDOUT_FILENO,
					       STDERR_FILENO}[i]))
			return -1;

	return 0;
}

static int login_pty(int fd)
{
	int ret;

	setsid();

	ret = ioctl(fd, TIOCSCTTY, NULL);
	if (ret < 0)
		return -1;

	ret = terminal_set_stdfds(fd);
	if (ret < 0)
		return -1;

	if (fd > STDERR_FILENO)
		close(fd);

	return 0;
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;
		return -1;
	}
	if (ret != pid)
		goto again;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	return 0;
}

static int resolve_procfd_symlink(int fd, char *buf, size_t buflen)
{
	int ret;
	char procfd[4096];

	ret = snprintf(procfd, 4096, "/proc/self/fd/%d", fd);
	if (ret < 0 || ret >= 4096)
		return -1;

	ret = readlink(procfd, buf, buflen);
	if (ret < 0 || (size_t)ret >= buflen)
		return -1;

	buf[ret] = '\0';

	return 0;
}

static int do_tiocgptpeer(char *ptmx, char *expected_procfd_contents)
{
	int ret;
	int master = -1, slave = -1, fret = -1;

	master = open(ptmx, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (master < 0) {
		fprintf(stderr, "Failed to open \"%s\": %s\n", ptmx,
			strerror(errno));
		return -1;
	}

	/*
	 * grantpt() makes assumptions about /dev/pts/ so ignore it. It's also
	 * not really needed.
	 */
	ret = unlockpt(master);
	if (ret < 0) {
		fprintf(stderr, "Failed to unlock terminal\n");
		goto do_cleanup;
	}

#ifdef TIOCGPTPEER
	slave = ioctl(master, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);
#endif
	if (slave < 0) {
		if (errno == EINVAL) {
			fprintf(stderr, "TIOCGPTPEER is not supported. "
					"Skipping test.\n");
			fret = EXIT_SUCCESS;
		}

		fprintf(stderr, "Failed to perform TIOCGPTPEER ioctl\n");
		goto do_cleanup;
	}

	pid_t pid = fork();
	if (pid < 0)
		goto do_cleanup;

	if (pid == 0) {
		char buf[4096];

		ret = login_pty(slave);
		if (ret < 0) {
			fprintf(stderr, "Failed to setup terminal\n");
			_exit(EXIT_FAILURE);
		}

		ret = resolve_procfd_symlink(STDIN_FILENO, buf, sizeof(buf));
		if (ret < 0) {
			fprintf(stderr, "Failed to retrieve pathname of pts "
					"slave file descriptor\n");
			_exit(EXIT_FAILURE);
		}

		if (strncmp(expected_procfd_contents, buf,
			    strlen(expected_procfd_contents)) != 0) {
			fprintf(stderr, "Received invalid contents for "
					"\"/proc/<pid>/fd/%d\" symlink: %s\n",
					STDIN_FILENO, buf);
			_exit(-1);
		}

		fprintf(stderr, "Contents of \"/proc/<pid>/fd/%d\" "
				"symlink are valid: %s\n", STDIN_FILENO, buf);

		_exit(EXIT_SUCCESS);
	}

	ret = wait_for_pid(pid);
	if (ret < 0)
		goto do_cleanup;

	fret = EXIT_SUCCESS;

do_cleanup:
	if (master >= 0)
		close(master);
	if (slave >= 0)
		close(slave);

	return fret;
}

static int verify_non_standard_devpts_mount(void)
{
	char *mntpoint;
	int ret = -1;
	char devpts[] = P_tmpdir "/devpts_fs_XXXXXX";
	char ptmx[] = P_tmpdir "/devpts_fs_XXXXXX/ptmx";

	ret = umount("/dev/pts");
	if (ret < 0) {
		fprintf(stderr, "Failed to unmount \"/dev/pts\": %s\n",
				strerror(errno));
		return -1;
	}

	(void)umount("/dev/ptmx");

	mntpoint = mkdtemp(devpts);
	if (!mntpoint) {
		fprintf(stderr, "Failed to create temporary mountpoint: %s\n",
				 strerror(errno));
		return -1;
	}

	ret = mount("devpts", mntpoint, "devpts", MS_NOSUID | MS_NOEXEC,
		    "newinstance,ptmxmode=0666,mode=0620,gid=5");
	if (ret < 0) {
		fprintf(stderr, "Failed to mount devpts fs to \"%s\" in new "
				"mount namespace: %s\n", mntpoint,
				strerror(errno));
		unlink(mntpoint);
		return -1;
	}

	ret = snprintf(ptmx, sizeof(ptmx), "%s/ptmx", devpts);
	if (ret < 0 || (size_t)ret >= sizeof(ptmx)) {
		unlink(mntpoint);
		return -1;
	}

	ret = do_tiocgptpeer(ptmx, mntpoint);
	unlink(mntpoint);
	if (ret < 0)
		return -1;

	return 0;
}

static int verify_ptmx_bind_mount(void)
{
	int ret;

	ret = mount("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND, NULL);
	if (ret < 0) {
		fprintf(stderr, "Failed to bind mount \"/dev/pts/ptmx\" to "
				"\"/dev/ptmx\" mount namespace\n");
		return -1;
	}

	ret = do_tiocgptpeer("/dev/ptmx", "/dev/pts/");
	if (ret < 0)
		return -1;

	return 0;
}

static int verify_invalid_ptmx_bind_mount(void)
{
	int ret;
	char mntpoint_fd;
	char ptmx[] = P_tmpdir "/devpts_ptmx_XXXXXX";

	mntpoint_fd = mkstemp(ptmx);
	if (mntpoint_fd < 0) {
		fprintf(stderr, "Failed to create temporary directory: %s\n",
				 strerror(errno));
		return -1;
	}

	ret = mount("/dev/pts/ptmx", ptmx, NULL, MS_BIND, NULL);
	close(mntpoint_fd);
	if (ret < 0) {
		fprintf(stderr, "Failed to bind mount \"/dev/pts/ptmx\" to "
				"\"%s\" mount namespace\n", ptmx);
		return -1;
	}

	ret = do_tiocgptpeer(ptmx, "/dev/pts/");
	if (ret == 0)
		return -1;

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;

	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "Standard input file desciptor is not attached "
				"to a terminal. Skipping test\n");
		exit(EXIT_FAILURE);
	}

	ret = unshare(CLONE_NEWNS);
	if (ret < 0) {
		fprintf(stderr, "Failed to unshare mount namespace\n");
		exit(EXIT_FAILURE);
	}

	ret = mount("", "/", NULL, MS_PRIVATE | MS_REC, 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to make \"/\" MS_PRIVATE in new mount "
				"namespace\n");
		exit(EXIT_FAILURE);
	}

	ret = verify_ptmx_bind_mount();
	if (ret < 0)
		exit(EXIT_FAILURE);

	ret = verify_invalid_ptmx_bind_mount();
	if (ret < 0)
		exit(EXIT_FAILURE);

	ret = verify_non_standard_devpts_mount();
	if (ret < 0)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}
