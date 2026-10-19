// SPDX-License-Identifier: GPL-2.0
/*
 * MNT_LOCKED keeps the owner of a user namespace from revealing what a
 * mount covers. The lock has to be set on the copy in that namespace and
 * only there, it has to survive the expiry of a mount placed beneath the
 * locked one, and it has to survive the propagated umount of such a copy.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "../../kselftest_harness.h"

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH	0x00000004
#endif
#ifndef MOVE_MOUNT_BENEATH
#define MOVE_MOUNT_BENEATH	0x00000200
#endif

#define DIR_LEN		64
#define PATH_LEN	192
#define FLAGS		(MS_NOSUID | MS_NODEV | MS_NOEXEC)

/* exit codes of the children, each test says what they mean */
enum {
	CHILD_OK,
	CHILD_SETUP,
	CHILD_STEP1,
	CHILD_STEP2,
	CHILD_STEP3,
	CHILD_STEP4,
};

static int write_file(const char *path, const char *s)
{
	ssize_t n = -1;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		n = write(fd, s, strlen(s));
		close(fd);
	}
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

static int create_file(const char *path, const char *s)
{
	ssize_t n = -1;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd >= 0) {
		n = write(fd, s, strlen(s));
		close(fd);
	}
	return n == (ssize_t)strlen(s) ? 0 : -1;
}

/* Root in a new user namespace with a copy of the mount namespace. */
static int enter_userns(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();
	char map[32];

	prctl(PR_SET_DUMPABLE, 1);
	if (unshare(CLONE_NEWUSER | CLONE_NEWNS))
		return -1;
	if (write_file("/proc/self/setgroups", "deny") && errno != ENOENT)
		return -1;
	snprintf(map, sizeof(map), "0 %d 1", uid);
	if (write_file("/proc/self/uid_map", map))
		return -1;
	snprintf(map, sizeof(map), "0 %d 1", gid);
	if (write_file("/proc/self/gid_map", map))
		return -1;
	return setgid(0) || setuid(0) ? -1 : 0;
}

static int wait_child(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static int wait_byte(int fd)
{
	char c;

	return read(fd, &c, 1) == 1 ? 0 : -1;
}

static int send_byte(int fd)
{
	return write(fd, "x", 1) == 1 ? 0 : -1;
}

/* A read of @path fails: the file is covered. */
static bool covered(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd >= 0)
		close(fd);
	return fd < 0;
}

FIXTURE(locked_mount) {
	char base[DIR_LEN];
	bool tracing;		/* debugfs with the tracefs automount is there */
};

FIXTURE_SETUP(locked_mount)
{
	char p[PATH_LEN];
	struct stat st;

	if (geteuid())
		SKIP(return, "test requires root");

	snprintf(self->base, sizeof(self->base), "/tmp/locked_mount.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, "mode=0755"), 0);

	/* an automount that a user can name: the tracefs below debugfs */
	snprintf(p, sizeof(p), "%s/dbg", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	self->tracing = !mount("debugfs", p, "debugfs", FLAGS, NULL);
	if (self->tracing) {
		/* the cases trigger it themselves */
		snprintf(p, sizeof(p), "%s/dbg/tracing", self->base);
		self->tracing = !fstatat(AT_FDCWD, p, &st, AT_NO_AUTOMOUNT) &&
				S_ISDIR(st.st_mode);
	}
}

FIXTURE_TEARDOWN(locked_mount)
{
	umount2(self->base, MNT_DETACH);
	rmdir(self->base);
}

/*
 * The child keeps a directory descriptor on the host's debugfs mount, moves
 * to a user namespace of its own and triggers the automount through the
 * descriptor. The mount goes below the host's mount and propagates into the
 * child's copy. The child's copy has to be locked, the host's not.
 */
static int automount_child(const char *base, int dfd, int to_host, int from_host)
{
	char p[PATH_LEN];
	struct stat st;

	if (enter_userns())
		return CHILD_SETUP;
	if (fstatat(dfd, "tracing/.", &st, 0))
		return CHILD_STEP1;
	if (send_byte(to_host) || wait_byte(from_host))
		return CHILD_SETUP;
	/* the flags of the copy are locked: EPERM */
	snprintf(p, sizeof(p), "%s/dbg/tracing", base);
	if (!mount(NULL, p, NULL, MS_REMOUNT | MS_BIND, NULL) || errno != EPERM)
		return CHILD_STEP2;
	return CHILD_OK;
}

TEST_F(locked_mount, automount_locked_in_the_triggering_namespace)
{
	int to_host[2], from_host[2], dfd, ret;
	char p[PATH_LEN];
	pid_t pid;

	if (!self->tracing)
		SKIP(return, "test requires debugfs with the tracefs automount");

	snprintf(p, sizeof(p), "%s/dbg", self->base);
	ASSERT_EQ(mount(NULL, p, NULL, MS_SHARED, NULL), 0);
	dfd = open(p, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(dfd, 0);
	ASSERT_EQ(pipe(to_host), 0);
	ASSERT_EQ(pipe(from_host), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(automount_child(self->base, dfd, to_host[1], from_host[0]));
	close(dfd);
	ASSERT_EQ(wait_byte(to_host[0]), 0);

	/* the host's own mount isn't locked: the flags can go */
	snprintf(p, sizeof(p), "%s/dbg/tracing", self->base);
	EXPECT_EQ(mount(NULL, p, NULL, MS_REMOUNT | MS_BIND, NULL), 0);

	ASSERT_EQ(send_byte(from_host[1]), 0);
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	EXPECT_EQ(ret, CHILD_OK);
}

/*
 * A copy of a tree with a locked cover. The child puts a shrinkable mount
 * beneath the cover, which hands the lock down, unmounts the cover and then
 * asks for the umount of an unlocked ancestor. That expires the shrinkable
 * mount on a kernel that doesn't look at the lock, and the covered
 * directory is bare.
 */
static int expiry_child(const char *base)
{
	char srv[PATH_LEN], shr[PATH_LEN], x[PATH_LEN], c[PATH_LEN];
	char hidden[PATH_LEN], secret[PATH_LEN];

	snprintf(srv, sizeof(srv), "%s/srv", base);
	snprintf(shr, sizeof(shr), "%s/shr", base);
	snprintf(x, sizeof(x), "%s/x", base);
	snprintf(c, sizeof(c), "%s/c", base);
	snprintf(hidden, sizeof(hidden), "%s/x/hidden", base);
	snprintf(secret, sizeof(secret), "%s/x/hidden/secret", base);

	if (enter_userns())
		return CHILD_SETUP;
	if (mount(srv, x, NULL, MS_BIND | MS_REC, NULL) ||
	    mount(shr, c, NULL, MS_BIND, NULL))
		return CHILD_SETUP;
	/* the cover is locked */
	if (!umount2(hidden, 0) || errno != EINVAL)
		return CHILD_STEP1;
	if (syscall(__NR_move_mount, AT_FDCWD, c, AT_FDCWD, hidden, MOVE_MOUNT_BENEATH))
		return CHILD_STEP2;
	/* the lock moved down, the cover may go */
	if (umount2(hidden, 0))
		return CHILD_STEP3;
	/* the holder of the lock may not, in any way */
	if (!umount2(hidden, 0) || errno != EINVAL)
		return CHILD_STEP4;
	if (chdir(x))
		return CHILD_SETUP;
	umount2(x, 0);
	return covered(secret) ? CHILD_OK : CHILD_STEP4;
}

TEST_F(locked_mount, expiry_leaves_a_locked_mount_alone)
{
	char p[PATH_LEN], q[PATH_LEN];
	pid_t pid;
	int ret;

	if (!self->tracing)
		SKIP(return, "test requires debugfs with the tracefs automount");

	snprintf(p, sizeof(p), "%s/dbg/tracing", self->base);
	snprintf(q, sizeof(q), "%s/shr", self->base);
	ASSERT_EQ(mkdir(q, 0755), 0);
	/* a bind of an automount is shrinkable as well */
	ASSERT_EQ(mount(p, q, NULL, MS_BIND, NULL), 0);
	snprintf(p, sizeof(p), "%s/srv", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	ASSERT_EQ(mount("tmpfs", p, "tmpfs", 0, "mode=0755"), 0);
	snprintf(p, sizeof(p), "%s/srv/hidden", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	snprintf(p, sizeof(p), "%s/srv/hidden/secret", self->base);
	ASSERT_EQ(create_file(p, "covered-by-root\n"), 0);
	snprintf(p, sizeof(p), "%s/srv/hidden", self->base);
	ASSERT_EQ(mount("tmpfs", p, "tmpfs", 0, "mode=0755"), 0);
	snprintf(p, sizeof(p), "%s/x", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	snprintf(p, sizeof(p), "%s/c", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(expiry_child(self->base));
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	EXPECT_EQ(ret, CHILD_OK);
}

/*
 * The copy of the mount tree that a user namespace gets at its creation has
 * the automounts in it locked unless they are on an expiry list. A bind of
 * such a tree inside the namespace copies the lock to the automount but not
 * to the root of the bind, so the child may ask for the umount of that root.
 * That must not expire the locked automount below it: the root is busy, the
 * umount fails and the automount has to be there afterwards.
 */
static int copied_tree_child(const char *base)
{
	char dbg[PATH_LEN], x[PATH_LEN], tracing[PATH_LEN];
	struct stat root, st;

	snprintf(dbg, sizeof(dbg), "%s/dbg", base);
	snprintf(x, sizeof(x), "%s/x", base);
	snprintf(tracing, sizeof(tracing), "%s/x/tracing", base);

	if (enter_userns())
		return CHILD_SETUP;
	if (mount(dbg, x, NULL, MS_BIND | MS_REC, NULL))
		return CHILD_SETUP;
	/* the copy of the automount carries the lock */
	if (!umount2(tracing, 0) || errno != EINVAL)
		return CHILD_STEP1;
	/* the root of the bind doesn't; keep it busy */
	if (chdir(x))
		return CHILD_SETUP;
	if (!umount2(x, 0) || errno != EBUSY)
		return CHILD_STEP2;
	/* the automount below it must not have gone */
	if (stat(x, &root) || fstatat(AT_FDCWD, tracing, &st, AT_NO_AUTOMOUNT))
		return CHILD_SETUP;
	return st.st_dev != root.st_dev ? CHILD_OK : CHILD_STEP3;
}

TEST_F(locked_mount, umount_of_a_bind_leaves_a_locked_automount_alone)
{
	char p[PATH_LEN];
	struct stat st;
	pid_t pid;
	int ret;

	if (!self->tracing)
		SKIP(return, "test requires debugfs with the tracefs automount");

	/* the copy the child gets has to contain the automount */
	snprintf(p, sizeof(p), "%s/dbg/tracing/.", self->base);
	ASSERT_EQ(stat(p, &st), 0);
	snprintf(p, sizeof(p), "%s/x", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(copied_tree_child(self->base));
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	EXPECT_EQ(ret, CHILD_OK);
}

/*
 * Z is a user namespace with a copy of a tree in which P/d is covered by a
 * locked mount. The host mounts X on P/d, which propagates beneath Z's
 * cover, and unmounts it again. Z's cover has to be locked afterwards as
 * it was before.
 */
static int propagation_child(const char *base, int to_host, int from_host)
{
	char d[PATH_LEN], secret[PATH_LEN];

	snprintf(d, sizeof(d), "%s/P/d", base);
	snprintf(secret, sizeof(secret), "%s/P/d/secret", base);

	if (enter_userns())
		return CHILD_SETUP;
	/* the cover is locked */
	if (!umount2(d, 0) || errno != EINVAL)
		return CHILD_STEP1;
	if (send_byte(to_host) || wait_byte(from_host))
		return CHILD_SETUP;
	/* X came and went beneath it: still locked */
	if (!umount2(d, 0) || errno != EINVAL)
		return CHILD_STEP2;
	return covered(secret) ? CHILD_OK : CHILD_STEP3;
}

TEST_F(locked_mount, propagated_copy_keeps_the_cover_locked)
{
	int to_host[2], from_host[2], ret;
	char p[PATH_LEN];
	pid_t pid;

	snprintf(p, sizeof(p), "%s/P", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	ASSERT_EQ(mount("tmpfs", p, "tmpfs", 0, "mode=0755"), 0);
	ASSERT_EQ(mount(NULL, p, NULL, MS_SHARED, NULL), 0);
	snprintf(p, sizeof(p), "%s/P/d", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	snprintf(p, sizeof(p), "%s/P/d/secret", self->base);
	ASSERT_EQ(create_file(p, "covered-by-root\n"), 0);
	ASSERT_EQ(pipe(to_host), 0);
	ASSERT_EQ(pipe(from_host), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		/* M: a manager's namespace that covers P/d for Z */
		pid_t z;

		if (unshare(CLONE_NEWNS))
			_exit(CHILD_SETUP);
		snprintf(p, sizeof(p), "%s/P", self->base);
		if (mount(NULL, p, NULL, MS_SLAVE, NULL))
			_exit(CHILD_SETUP);
		snprintf(p, sizeof(p), "%s/P/d", self->base);
		if (mount("tmpfs", p, "tmpfs", 0, "mode=0755"))
			_exit(CHILD_SETUP);
		z = fork();
		if (z < 0)
			_exit(CHILD_SETUP);
		if (z == 0)
			_exit(propagation_child(self->base, to_host[1], from_host[0]));
		_exit(wait_child(z));
	}
	ASSERT_EQ(wait_byte(to_host[0]), 0);

	/* the host mounts on P/d and unmounts again; both propagate */
	snprintf(p, sizeof(p), "%s/P/d", self->base);
	ASSERT_EQ(mount("tmpfs", p, "tmpfs", 0, "mode=0755"), 0);
	ASSERT_EQ(umount2(p, 0), 0);

	ASSERT_EQ(send_byte(from_host[1]), 0);
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	EXPECT_EQ(ret, CHILD_OK);
}

TEST_HARNESS_MAIN
