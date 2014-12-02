#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
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

#ifndef MS_REC
# define MS_REC 16384
#endif
#ifndef MS_RELATIME
# define MS_RELATIME (1 << 21)
#endif
#ifndef MS_STRICTATIME
# define MS_STRICTATIME (1 << 24)
#endif

static void die(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void vmaybe_write_file(bool enoent_ok, char *filename, char *fmt, va_list ap)
{
	char buf[4096];
	int fd;
	ssize_t written;
	int buf_len;

	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (buf_len < 0) {
		die("vsnprintf failed: %s\n",
		    strerror(errno));
	}
	if (buf_len >= sizeof(buf)) {
		die("vsnprintf output truncated\n");
	}

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		if ((errno == ENOENT) && enoent_ok)
			return;
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

static int read_mnt_flags(const char *path)
{
	int ret;
	struct statvfs stat;
	int mnt_flags;

	ret = statvfs(path, &stat);
	if (ret != 0) {
		die("statvfs of %s failed: %s\n",
			path, strerror(errno));
	}
	if (stat.f_flag & ~(ST_RDONLY | ST_NOSUID | ST_NODEV | \
			ST_NOEXEC | ST_NOATIME | ST_NODIRATIME | ST_RELATIME | \
			ST_SYNCHRONOUS | ST_MANDLOCK)) {
		die("Unrecognized mount flags\n");
	}
	mnt_flags = 0;
	if (stat.f_flag & ST_RDONLY)
		mnt_flags |= MS_RDONLY;
	if (stat.f_flag & ST_NOSUID)
		mnt_flags |= MS_NOSUID;
	if (stat.f_flag & ST_NODEV)
		mnt_flags |= MS_NODEV;
	if (stat.f_flag & ST_NOEXEC)
		mnt_flags |= MS_NOEXEC;
	if (stat.f_flag & ST_NOATIME)
		mnt_flags |= MS_NOATIME;
	if (stat.f_flag & ST_NODIRATIME)
		mnt_flags |= MS_NODIRATIME;
	if (stat.f_flag & ST_RELATIME)
		mnt_flags |= MS_RELATIME;
	if (stat.f_flag & ST_SYNCHRONOUS)
		mnt_flags |= MS_SYNCHRONOUS;
	if (stat.f_flag & ST_MANDLOCK)
		mnt_flags |= ST_MANDLOCK;

	return mnt_flags;
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

	maybe_write_file("/proc/self/setgroups", "deny");
	write_file("/proc/self/uid_map", "0 %d 1", uid);
	write_file("/proc/self/gid_map", "0 %d 1", gid);

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
bool test_unpriv_remount(const char *fstype, const char *mount_options,
			 int mount_flags, int remount_flags, int invalid_flags)
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

	if (mount("testing", "/tmp", fstype, mount_flags, mount_options) != 0) {
		die("mount of %s with options '%s' on /tmp failed: %s\n",
		    fstype,
		    mount_options? mount_options : "",
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
	return test_unpriv_remount("ramfs", NULL, mount_flags, mount_flags, 0);
}

static bool test_unpriv_remount_atime(int mount_flags, int invalid_flags)
{
	return test_unpriv_remount("ramfs", NULL, mount_flags, mount_flags,
				   invalid_flags);
}

static bool test_priv_mount_unpriv_remount(void)
{
	pid_t child;
	int ret;
	const char *orig_path = "/dev";
	const char *dest_path = "/tmp";
	int orig_mnt_flags, remount_mnt_flags;

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

	orig_mnt_flags = read_mnt_flags(orig_path);

	create_and_enter_userns();
	ret = unshare(CLONE_NEWNS);
	if (ret != 0) {
		die("unshare(CLONE_NEWNS) failed: %s\n",
			strerror(errno));
	}

	ret = mount(orig_path, dest_path, "bind", MS_BIND | MS_REC, NULL);
	if (ret != 0) {
		die("recursive bind mount of %s onto %s failed: %s\n",
			orig_path, dest_path, strerror(errno));
	}

	ret = mount(dest_path, dest_path, "none",
		    MS_REMOUNT | MS_BIND | orig_mnt_flags , NULL);
	if (ret != 0) {
		/* system("cat /proc/self/mounts"); */
		die("remount of /tmp failed: %s\n",
		    strerror(errno));
	}

	remount_mnt_flags = read_mnt_flags(dest_path);
	if (orig_mnt_flags != remount_mnt_flags) {
		die("Mount flags unexpectedly changed during remount of %s originally mounted on %s\n",
			dest_path, orig_path);
	}
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	if (!test_unpriv_remount_simple(MS_RDONLY)) {
		die("MS_RDONLY malfunctions\n");
	}
	if (!test_unpriv_remount("devpts", "newinstance", MS_NODEV, MS_NODEV, 0)) {
		die("MS_NODEV malfunctions\n");
	}
	if (!test_unpriv_remount_simple(MS_NOSUID)) {
		die("MS_NOSUID malfunctions\n");
	}
	if (!test_unpriv_remount_simple(MS_NOEXEC)) {
		die("MS_NOEXEC malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_RELATIME,
				       MS_NOATIME))
	{
		die("MS_RELATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_STRICTATIME,
				       MS_NOATIME))
	{
		die("MS_STRICTATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_NOATIME,
				       MS_STRICTATIME))
	{
		die("MS_NOATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_RELATIME|MS_NODIRATIME,
				       MS_NOATIME))
	{
		die("MS_RELATIME|MS_NODIRATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_STRICTATIME|MS_NODIRATIME,
				       MS_NOATIME))
	{
		die("MS_STRICTATIME|MS_NODIRATIME malfunctions\n");
	}
	if (!test_unpriv_remount_atime(MS_NOATIME|MS_NODIRATIME,
				       MS_STRICTATIME))
	{
		die("MS_NOATIME|MS_DIRATIME malfunctions\n");
	}
	if (!test_unpriv_remount("ramfs", NULL, MS_STRICTATIME, 0, MS_NOATIME))
	{
		die("Default atime malfunctions\n");
	}
	if (!test_priv_mount_unpriv_remount()) {
		die("Mount flags unexpectedly changed after remount\n");
	}
	return EXIT_SUCCESS;
}
