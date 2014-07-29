#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <stdbool.h>
#include <stdarg.h>

#ifndef CLONE_NEWNS
# define CLONE_NEWNS 0x00020000
#endif
#ifndef CLONE_NEWUTS
# define CLONE_NEWUTS 0x04000000
#endif
#ifndef CLONE_NEWIPC
# define CLONE_NEWIPC 0x08000000
#endif
#ifndef CLONE_NEWNET
# define CLONE_NEWNET 0x40000000
#endif
#ifndef CLONE_NEWUSER
# define CLONE_NEWUSER 0x10000000
#endif
#ifndef CLONE_NEWPID
# define CLONE_NEWPID 0x20000000
#endif

#ifndef MS_RELATIME
#define MS_RELATIME (1 << 21)
#endif
#ifndef MS_STRICTATIME
#define MS_STRICTATIME (1 << 24)
#endif

static void die(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void write_file(char *filename, char *fmt, ...)
{
	char buf[4096];
	int fd;
	ssize_t written;
	int buf_len;
	va_list ap;

	va_start(ap, fmt);
	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (buf_len < 0) {
		die("vsnprintf failed: %s\n",
		    strerror(errno));
	}
	if (buf_len >= sizeof(buf)) {
		die("vsnprintf output truncated\n");
	}

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		die("open of %s failed: %s\n",
		    filename, strerror(errno));
	}
	written = write(fd, buf, buf_len);
	if (written != buf_len) {
		if (written >= 0) {
			die("short write to %s\n", filename);
		} else {
			die("write to %s failed: %s\n",
				filename, strerror(errno));
		}
	}
	if (close(fd) != 0) {
		die("close of %s failed: %s\n",
			filename, strerror(errno));
	}
}

static void create_and_enter_userns(void)
{
	uid_t uid;
	gid_t gid;

	uid = getuid();
	gid = getgid();

	if (unshare(CLONE_NEWUSER) !=0) {
		die("unshare(CLONE_NEWUSER) failed: %s\n",
			strerror(errno));
	}

	write_file("/proc/self/uid_map", "0 %d 1", uid);
	write_file("/proc/self/gid_map", "0 %d 1", gid);

	if (setgroups(0, NULL) != 0) {
		die("setgroups failed: %s\n",
			strerror(errno));
	}
	if (setgid(0) != 0) {
		die ("setgid(0) failed %s\n",
			strerror(errno));
	}
	if (setuid(0) != 0) {
		die("setuid(0) failed %s\n",
			strerror(errno));
	}
}

static
bool test_unpriv_remount(int mount_flags, int remount_flags, int invalid_flags)
{
	pid_t child;

	child = fork();
	if (child == -1) {
		die("fork failed: %s\n",
			strerror(errno));
	}
	if (child != 0) { /* parent */
		pid_t pid;
		int status;
		pid = waitpid(child, &status, 0);
		if (pid == -1) {
			die("waitpid failed: %s\n",
				strerror(errno));
		}
		if (pid != child) {
			die("waited for %d got %d\n",
				child, pid);
		}
		if (!WIFEXITED(status)) {
			die("child did not terminate cleanly\n");
		}
		return WEXITSTATUS(status) == EXIT_SUCCESS ? true : false;
	}

	create_and_enter_userns();
	if (unshare(CLONE_NEWNS) != 0) {
		die("unshare(CLONE_NEWNS) failed: %s\n",
			strerror(errno));
	}

	if (mount("testing", "/tmp", "ramfs", mount_flags, NULL) != 0) {
		die("mount of /tmp failed: %s\n",
			strerror(errno));
	}

	create_and_enter_userns();

	if (unshare(CLONE_NEWNS) != 0) {
		die("unshare(CLONE_NEWNS) failed: %s\n",
			strerror(errno));
	}

	if (mount("/tmp", "/tmp", "none",
		  MS_REMOUNT | MS_BIND | remount_flags, NULL) != 0) {
		/* system("cat /proc/self/mounts"); */
		die("remount of /tmp failed: %s\n",
		    strerror(errno));
	}

	if (mount("/tmp", "/tmp", "none",
		  MS_REMOUNT | MS_BIND | invalid_flags, NULL) == 0) {
		/* system("cat /proc/self/mounts"); */
		die("remount of /tmp with invalid flags "
		    "succeeded unexpectedly\n");
	}
	exit(EXIT_SUCCESS);
}

static bool test_unpriv_remount_simple(int mount_flags)
{
	return test_unpriv_remount(mount_flags, mount_flags, 0);
}

static bool test_unpriv_remount_atime(int mount_flags, int invalid_flags)
{
	return test_unpriv_remount(mount_flags, mount_flags, invalid_flags);
}

int main(int argc, char **argv)
{
	if (!test_unpriv_remount_simple(MS_RDONLY|MS_NODEV)) {
		die("MS_RDONLY malfunctions\n");
	}
	if (!test_unpriv_remount_simple(MS_NODEV)) {
		die("MS_NODEV malfunctions\n");
	}
	if (!test_unpriv_remount_simple(MS_NOSUID|MS_NODEV)) {
		die("MS_NOSUID malfunctions\n");
	}
	if (!test_unpriv_remount_simple(MS_NOEXEC|MS_NODEV)) {
		die("MS_NOEXEC malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_RELATIME|MS_NODEV,
				       MS_NOATIME|MS_NODEV))
	{
		die("MS_RELATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_STRICTATIME|MS_NODEV,
				       MS_NOATIME|MS_NODEV))
	{
		die("MS_STRICTATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_NOATIME|MS_NODEV,
				       MS_STRICTATIME|MS_NODEV))
	{
		die("MS_RELATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_RELATIME|MS_NODIRATIME|MS_NODEV,
				       MS_NOATIME|MS_NODEV))
	{
		die("MS_RELATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_STRICTATIME|MS_NODIRATIME|MS_NODEV,
				       MS_NOATIME|MS_NODEV))
	{
		die("MS_RELATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_NOATIME|MS_NODIRATIME|MS_NODEV,
				       MS_STRICTATIME|MS_NODEV))
	{
		die("MS_RELATIME malfunctions\n");
	}
	if (!test_unpriv_remount(MS_STRICTATIME|MS_NODEV, MS_NODEV,
				 MS_NOATIME|MS_NODEV))
	{
		die("Default atime malfunctions\n");
	}
	return EXIT_SUCCESS;
}
