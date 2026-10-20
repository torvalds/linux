// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>

#include "kselftest_harness.h"
#include "wrappers.h"

TEST(kernfs_listxattr)
{
	ssize_t len;
	int fd;

	/* Read-only file that can never have any extended attributes set.
	 * However, on systems with SELinux enabled, security.selinux xattr
	 * may be present. Skip the content check if any xattrs are found.
	 */
	fd = open("/sys/kernel/warn_count", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	len = flistxattr(fd, NULL, 0);
	ASSERT_GE(len, 0);

	if (len > 0) {
		close(fd);
		SKIP(return, "xattrs present on /sys/kernel/warn_count, skipping xattr content check");
	}

	EXPECT_EQ(close(fd), 0);
}

TEST(kernfs_getxattr)
{
	int fd;
	char buf[1];

	/* Read-only file that can never have any extended attributes set. */
	fd = open("/sys/kernel/warn_count", O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_LT(fgetxattr(fd, "user.foo", buf, sizeof(buf)), 0);
	ASSERT_EQ(errno, ENODATA);
	EXPECT_EQ(close(fd), 0);
}

/*
 * Exercise the kernfs dentry cache: lookup, revalidation of positive and
 * negative dentries, readdir and namespace tagging.
 *
 * These drive kernfs from kernel context rather than VFS create/unlink,
 * which is what ->d_revalidate() exists for: writing cgroup.subtree_control
 * adds and removes files in every child cgroup with no VFS operation
 * touching those names.
 */

#define CG_SCRATCH	"kernfs_selftest"
#define TEST_IFNAME	"kfstest0"

/*
 * Controllers that add a file to each child cgroup when enabled.  The probe
 * file must be owned by the controller: cgroup_base_files[] entries such as
 * cpu.stat exist in every cgroup regardless, and every file the cpu
 * controller does own is behind a Kconfig symbol, so cpu is not usable here.
 */
static const struct {
	const char *name;
	const char *probe_file;
} controllers[] = {
	{ "memory",	"memory.current" },
	{ "pids",	"pids.current" },
};

static int find_cgroup2_root(char *buf, size_t len)
{
	char line[PATH_MAX * 2];
	FILE *f;
	int ret = -1;

	f = fopen("/proc/self/mounts", "re");
	if (!f)
		return -1;

	while (fgets(line, sizeof(line), f)) {
		char mnt[PATH_MAX], type[64];

		/* Octal escaping can expand a path fourfold; bound both %s. */
		if (sscanf(line, "%*s %4095s %63s", mnt, type) != 2)
			continue;
		if (strcmp(type, "cgroup2"))
			continue;
		if (strlen(mnt) >= len)
			break;
		strcpy(buf, mnt);
		ret = 0;
		break;
	}

	fclose(f);
	return ret;
}

static int write_file(const char *path, const char *val)
{
	ssize_t len = strlen(val);
	int fd, ret;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ret = write(fd, val, len) == len ? 0 : -1;
	close(fd);
	return ret;
}

static bool file_has_word(const char *path, const char *word)
{
	char buf[4096], *tok, *save;
	bool found = false;
	ssize_t n;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0)
		return false;
	buf[n] = '\0';

	for (tok = strtok_r(buf, "\n ", &save); tok;
	     tok = strtok_r(NULL, "\n ", &save)) {
		if (!strcmp(tok, word)) {
			found = true;
			break;
		}
	}
	return found;
}

static bool path_is_mounted(const char *path)
{
	char line[PATH_MAX * 2];
	bool found = false;
	FILE *f;

	f = fopen("/proc/self/mounts", "re");
	if (!f)
		return false;
	while (fgets(line, sizeof(line), f)) {
		char mnt[PATH_MAX];

		if (sscanf(line, "%*s %4095s", mnt) != 1)
			continue;
		if (!strcmp(mnt, path)) {
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

/* Shared by the stress tests below. */
static bool stress_deadline(const struct timespec *end)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec > end->tv_sec ||
	       (now.tv_sec == end->tv_sec && now.tv_nsec >= end->tv_nsec);
}

FIXTURE(kernfs_cgroup)
{
	char scratch[PATH_MAX];		/* <cg2>/kernfs_selftest.<pid> */
	char child[PATH_MAX];		/* <scratch>/child */
	char probe[PATH_MAX];		/* child's controller file */
	char scratch_sc[PATH_MAX];	/* scratch's cgroup.subtree_control */
	char root_sc[PATH_MAX];		/* root's cgroup.subtree_control */
	char enable[32];		/* "+<controller>" */
	char disable[32];		/* "-<controller>" */
	char mnt[PATH_MAX];		/* our own mount, if we made one */
	bool mounted;
	bool enabled_at_root;
};

/* A cgroup stays busy briefly after its last task exits. */
static void rmdir_retry(const char *path)
{
	int i;

	for (i = 0; i < 500; i++) {
		if (!rmdir(path) || errno != EBUSY)
			return;
		usleep(10000);
	}
}

/*
 * Undo whatever SETUP managed to do.  The harness skips TEARDOWN after a
 * failed or skipped SETUP, so SETUP must call this before returning early.
 */
static void kernfs_cgroup_undo(FIXTURE_DATA(kernfs_cgroup) *self)
{
	rmdir_retry(self->child);
	rmdir(self->scratch);
	if (self->enabled_at_root)
		write_file(self->root_sc, self->disable);
	if (self->mounted) {
		umount2(self->mnt, MNT_DETACH);
		rmdir(self->mnt);
	}
	self->enabled_at_root = false;
	self->mounted = false;
}

FIXTURE_SETUP(kernfs_cgroup)
{
	char root[PATH_MAX], ctl[PATH_MAX];
	const char *probe_file = NULL;
	size_t i;

	if (geteuid())
		SKIP(return, "test needs to run as root");

	/*
	 * A private mount namespace stops our mounts leaking, but does not
	 * isolate the cgroup hierarchy: cgroup2 has one default hierarchy
	 * however many times it is mounted.  The scratch cgroups live in the
	 * host's and must be removed, not discarded with the namespace.
	 */
	if (unshare(CLONE_NEWNS))
		SKIP(return, "unshare(CLONE_NEWNS): %s", strerror(errno));
	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
		SKIP(return, "make / private: %s", strerror(errno));

	/* Use an existing cgroup2 mount if there is one, else make our own. */
	if (find_cgroup2_root(root, sizeof(root))) {
		strcpy(self->mnt, "/tmp/kernfs_selftest_cg2.XXXXXX");
		if (!mkdtemp(self->mnt))
			SKIP(return, "mkdtemp: %s", strerror(errno));
		if (mount("none", self->mnt, "cgroup2", 0, NULL)) {
			rmdir(self->mnt);
			SKIP(return, "mount cgroup2: %s", strerror(errno));
		}
		self->mounted = true;
		strcpy(root, self->mnt);
	}

	snprintf(self->root_sc, sizeof(self->root_sc),
		 "%s/cgroup.subtree_control", root);
	snprintf(ctl, sizeof(ctl), "%s/cgroup.controllers", root);

	/* Named after our pid so we cannot collide with anything else. */
	snprintf(self->scratch, sizeof(self->scratch), "%s/%s.%d", root,
		 CG_SCRATCH, getpid());
	snprintf(self->child, sizeof(self->child), "%s/child", self->scratch);
	snprintf(self->scratch_sc, sizeof(self->scratch_sc),
		 "%s/cgroup.subtree_control", self->scratch);

	for (i = 0; i < ARRAY_SIZE(controllers); i++) {
		if (!file_has_word(ctl, controllers[i].name))
			continue;
		snprintf(self->enable, sizeof(self->enable), "+%s",
			 controllers[i].name);
		snprintf(self->disable, sizeof(self->disable), "-%s",
			 controllers[i].name);
		probe_file = controllers[i].probe_file;

		/*
		 * A controller must be in the root's subtree_control before
		 * it appears in our scratch cgroup.  Note if we enabled it,
		 * so it can be put back.
		 */
		self->enabled_at_root = !file_has_word(self->root_sc,
						       controllers[i].name);
		if (self->enabled_at_root &&
		    write_file(self->root_sc, self->enable)) {
			self->enabled_at_root = false;
			probe_file = NULL;
			continue;
		}
		break;
	}
	if (!probe_file) {
		kernfs_cgroup_undo(self);
		SKIP(return, "no usable cgroup2 controller");
	}

	snprintf(self->probe, sizeof(self->probe), "%s/%s", self->child,
		 probe_file);

	/*
	 * Only an unusable environment may skip.  A scratch cgroup named
	 * after our own pid should always be creatable, so failing to make
	 * one is a result -- skipping would let a broken kernel look green.
	 */
	if (mkdir(self->scratch, 0755)) {
		int err = errno;

		kernfs_cgroup_undo(self);
		if (err == EROFS || err == EACCES || err == EPERM)
			SKIP(return, "mkdir %s: %s", self->scratch,
			     strerror(err));
		ASSERT_EQ(err, 0) TH_LOG("mkdir %s: %s", self->scratch,
					 strerror(err));
	}
	if (mkdir(self->child, 0755)) {
		int err = errno;

		kernfs_cgroup_undo(self);
		ASSERT_EQ(err, 0) TH_LOG("mkdir %s: %s", self->child,
					 strerror(err));
	}

	/*
	 * The tests below need the probe file to appear and disappear with
	 * the controller, so it must be absent now, before anything enables
	 * it in the scratch cgroup.  Skip rather than fail: a probe file that
	 * is already there means the table names one the controller does not
	 * own, not that the kernel is broken.
	 */
	if (!access(self->probe, F_OK)) {
		kernfs_cgroup_undo(self);
		SKIP(return, "%s is not owned by the %s controller",
		     probe_file, self->enable + 1);
	}
}

FIXTURE_TEARDOWN(kernfs_cgroup)
{
	write_file(self->scratch_sc, self->disable);
	kernfs_cgroup_undo(self);
}

/*
 * Walking already-cached dentries must not invalidate them.  Spurious
 * invalidation is not merely slow: d_invalidate() calls detach_mounts(), so
 * an unrelated lookup would silently tear down any mount below.
 */
TEST_F(kernfs_cgroup, path_walk_does_not_invalidate)
{
	char src[] = "/tmp/kernfs_selftest_bind.XXXXXX";
	char sub[PATH_MAX], probe[PATH_MAX];
	int i;

	snprintf(sub, sizeof(sub), "%s/sub", self->child);
	ASSERT_EQ(mkdir(sub, 0755), 0);

	if (!mkdtemp(src)) {
		rmdir(sub);
		SKIP(return, "mkdtemp: %s", strerror(errno));
	}
	if (mount(src, sub, NULL, MS_BIND, NULL)) {
		int err = errno;

		rmdir(sub);
		rmdir(src);
		SKIP(return, "bind mount onto a cgroup dir: %s", strerror(err));
	}
	ASSERT_TRUE(path_is_mounted(sub));

	/* Walk a sibling path through the same directory, repeatedly. */
	snprintf(probe, sizeof(probe), "%s/cgroup.procs", self->child);
	for (i = 0; i < 8; i++) {
		int fd = open(probe, O_RDONLY | O_CLOEXEC);

		if (fd >= 0)
			close(fd);
	}

	EXPECT_TRUE(path_is_mounted(sub));

	umount2(sub, MNT_DETACH);
	rmdir(sub);
	rmdir(src);
}

/*
 * A cached negative dentry must be invalidated when the kernel creates the
 * name behind the dcache's back.  That is what kernfs_dir_changed() and
 * kernfs_elem_dir::rev are for.
 */
TEST_F(kernfs_cgroup, negative_dentry_invalidated_by_kernel_create)
{
	struct stat st;

	/* Caches a negative dentry for the probe file. */
	ASSERT_EQ(stat(self->probe, &st), -1);
	ASSERT_EQ(errno, ENOENT);

	/* The kernel now creates it, with no VFS operation on that name. */
	ASSERT_EQ(write_file(self->scratch_sc, self->enable), 0);

	EXPECT_EQ(stat(self->probe, &st), 0);
}

/* The mirror image: a cached positive dentry must go when the node does. */
TEST_F(kernfs_cgroup, positive_dentry_invalidated_by_kernel_remove)
{
	struct stat st;

	ASSERT_EQ(write_file(self->scratch_sc, self->enable), 0);
	/* Caches a positive dentry. */
	ASSERT_EQ(stat(self->probe, &st), 0);

	ASSERT_EQ(write_file(self->scratch_sc, self->disable), 0);

	ASSERT_EQ(stat(self->probe, &st), -1);
	EXPECT_EQ(errno, ENOENT);
}

/* Opening a removed node fails; it never returns stale content. */
TEST_F(kernfs_cgroup, open_after_rmdir_fails)
{
	char path[PATH_MAX];
	char buf[64];
	int fd;

	snprintf(path, sizeof(path), "%s/cgroup.procs", self->child);

	fd = open(path, O_RDONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(rmdir(self->child), 0);

	/* Lookup by path must fail. */
	EXPECT_EQ(open(path, O_RDONLY | O_CLOEXEC), -1);
	EXPECT_EQ(errno, ENOENT);

	/*
	 * An fd held across removal must fail rather than return stale
	 * content.  rmdir() deactivates the node before it returns, so
	 * kernfs_seq_start() fails to get an active reference.
	 */
	EXPECT_LT(read(fd, buf, sizeof(buf)), 0);
	EXPECT_EQ(errno, ENODEV);
	EXPECT_EQ(close(fd), 0);

	ASSERT_EQ(mkdir(self->child, 0755), 0);
}

/* readdir returns every entry exactly once. */
TEST_F(kernfs_cgroup, readdir_no_duplicates)
{
	char names[512][NAME_MAX + 1];
	struct dirent *de;
	int n = 0, i, j;
	DIR *d;

	ASSERT_EQ(write_file(self->scratch_sc, self->enable), 0);

	d = opendir(self->child);
	ASSERT_NE(d, NULL);
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		ASSERT_LT(n, (int)ARRAY_SIZE(names));
		strncpy(names[n], de->d_name, NAME_MAX);
		names[n][NAME_MAX] = '\0';
		n++;
	}
	closedir(d);

	ASSERT_GT(n, 0);
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			EXPECT_STRNE(names[i], names[j]);
}

#define RESUME_DIRS	24

/*
 * Resuming at an entry that has gone must carry on after it, never before.
 * Take a cookie for every entry, then remove each one, seek to its cookie
 * and read the rest; nothing already reported may come back.
 */
TEST_F(kernfs_cgroup, readdir_resume_at_removed_entry)
{
	/* The cgroup's own control files are listed alongside ours. */
	char names[128][NAME_MAX + 1];
	long pos[128];
	char path[PATH_MAX];
	struct dirent *de;
	int n = 0, i, j;
	DIR *d;

	for (i = 0; i < RESUME_DIRS; i++) {
		snprintf(path, sizeof(path), "%s/e%02d", self->scratch, i);
		ASSERT_EQ(mkdir(path, 0755), 0);
	}

	/* Record the cookie before reading each entry, with its name. */
	d = opendir(self->scratch);
	ASSERT_NE(d, NULL);
	while (1) {
		long here = telldir(d);

		de = readdir(d);
		if (!de)
			break;
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		ASSERT_LT(n, (int)ARRAY_SIZE(pos));
		pos[n] = here;
		strncpy(names[n], de->d_name, NAME_MAX);
		names[n][NAME_MAX] = '\0';
		n++;
	}
	closedir(d);
	ASSERT_GT(n, 1);

	for (i = 0; i < n; i++) {
		/* Only the directories we made can be removed and put back. */
		if (strncmp(names[i], "e", 1))
			continue;

		snprintf(path, sizeof(path), "%s/%s", self->scratch, names[i]);
		ASSERT_EQ(rmdir(path), 0);

		/* Reopen so the seek has to reach the kernel. */
		d = opendir(self->scratch);
		ASSERT_NE(d, NULL);
		seekdir(d, pos[i]);
		while ((de = readdir(d))) {
			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			for (j = 0; j < i; j++)
				ASSERT_STRNE(de->d_name, names[j])
					TH_LOG("resuming at %s (gone) went back to %s",
					       names[i], names[j]);
		}
		closedir(d);

		ASSERT_EQ(mkdir(path, 0755), 0);
	}

	for (i = 0; i < RESUME_DIRS; i++) {
		snprintf(path, sizeof(path), "%s/e%02d", self->scratch, i);
		EXPECT_EQ(rmdir(path), 0);
	}
}

#define CHURN_ROUNDS	400
#define CHURN_BUFSZ	512	/* small, so a listing takes several calls */

/*
 * The files appear at the end of the enabling write and go at the start of
 * the disabling one, so the window where they exist is the short one.
 */
#define CHURN_DWELL_ON	2000
#define CHURN_DWELL_OFF	200

struct kernfs_dirent64 {
	unsigned long long	d_ino;
	long long		d_off;
	unsigned short		d_reclen;
	unsigned char		d_type;
	char			d_name[];
};

/*
 * The same resume, but inside one getdents(2) call.  rmdir(2) cannot reach
 * that window because iterate_dir() holds the listed directory's i_rwsem
 * for the whole listing; cgroup.subtree_control can, having no VFS
 * operation on the names it adds and removes.  The files that are not the
 * controller's stay throughout, so each must appear exactly once.
 *
 * A stress test: it has not been seen to catch the ordering bug, and is
 * here to keep the unlocked window under load for lockdep and KASAN.
 */
TEST_F(kernfs_cgroup, readdir_resume_vs_internal_remove)
{
	char buf[CHURN_BUFSZ] __attribute__((aligned(8)));
	char stable[128][NAME_MAX + 1];
	int nstable = 0, i, r;
	int withctl = 0, without = 0;
	int seen[128], status;
	pid_t churner;
	DIR *d;

	/* With the controller off, whatever is left is what must persist. */
	ASSERT_EQ(write_file(self->scratch_sc, self->disable), 0);
	d = opendir(self->child);
	ASSERT_NE(d, NULL);
	for (;;) {
		struct dirent *de = readdir(d);

		if (!de)
			break;
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		ASSERT_LT(nstable, (int)ARRAY_SIZE(stable));
		strncpy(stable[nstable], de->d_name, NAME_MAX);
		stable[nstable][NAME_MAX] = '\0';
		nstable++;
	}
	closedir(d);
	ASSERT_GT(nstable, 0);

	churner = fork();
	ASSERT_GE(churner, 0);
	if (churner == 0) {
		for (;;) {
			if (write_file(self->scratch_sc, self->enable))
				_exit(10);
			usleep(CHURN_DWELL_ON);
			if (write_file(self->scratch_sc, self->disable))
				_exit(11);
			usleep(CHURN_DWELL_OFF);
		}
	}

	for (r = 0; r < CHURN_ROUNDS; r++) {
		int fd = open(self->child, O_RDONLY | O_DIRECTORY);
		int extra = 0;
		int n;

		ASSERT_GE(fd, 0);
		memset(seen, 0, sizeof(seen));

		while ((n = syscall(SYS_getdents64, fd, buf, sizeof(buf))) > 0) {
			int off = 0;

			while (off < n) {
				struct kernfs_dirent64 *de = (void *)(buf + off);
				bool known = false;

				off += de->d_reclen;
				for (i = 0; i < nstable; i++)
					if (!strcmp(de->d_name, stable[i])) {
						seen[i]++;
						known = true;
					}
				if (!known && strcmp(de->d_name, ".") &&
				    strcmp(de->d_name, ".."))
					extra++;
			}
		}
		ASSERT_GE(n, 0);
		EXPECT_EQ(close(fd), 0);

		if (extra)
			withctl++;
		else
			without++;

		for (i = 0; i < nstable; i++)
			ASSERT_EQ(seen[i], 1)
				TH_LOG("round %d: %s seen %d times",
				       r, stable[i], seen[i]);
	}

	/* The churn must have been running, or the listings prove nothing. */
	EXPECT_EQ(kill(churner, SIGKILL), 0);
	ASSERT_EQ(waitpid(churner, &status, 0), churner);
	ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
		TH_LOG("churner exited on its own: status %d", status);

	/*
	 * They also have to have overlapped it.  How much depends on the
	 * machine, so say the race could not be arranged rather than fail.
	 */
	if (!withctl || !without)
		SKIP(return, "listings did not span the churn: %d with, %d without",
		     withctl, without);
}

/*
 * A telldir() cookie must resolve back to the same entry after seekdir().
 * kernfs encodes the cookie as the node's name hash, so this covers
 * kernfs_dir_pos() as well as plain iteration.
 */
TEST_F(kernfs_cgroup, readdir_seekdir_roundtrip)
{
	char names[512][NAME_MAX + 1];
	struct dirent *de;
	long pos[512];
	int n = 0, i;
	DIR *d;

	ASSERT_EQ(write_file(self->scratch_sc, self->enable), 0);

	d = opendir(self->child);
	ASSERT_NE(d, NULL);

	/* Record the cookie *before* reading each entry, with its name. */
	while (1) {
		long here = telldir(d);

		de = readdir(d);
		if (!de)
			break;
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		ASSERT_LT(n, (int)ARRAY_SIZE(pos));
		pos[n] = here;
		strncpy(names[n], de->d_name, NAME_MAX);
		names[n][NAME_MAX] = '\0';
		n++;
	}
	ASSERT_GT(n, 0);

	/* Seeking back to a cookie must land on the entry it was taken at. */
	for (i = 0; i < n; i++) {
		seekdir(d, pos[i]);
		de = readdir(d);
		ASSERT_NE(de, NULL);
		EXPECT_STREQ(de->d_name, names[i]);
	}

	closedir(d);
}

#define STRESS_SECS	2
#define STRESS_DIRS	4
#define STRESS_READERS	4

/*
 * Hammer lookup against creation and removal.  Revalidation holds no lock
 * against the writers, so what makes it safe is that every answer it can
 * give is one the caller already handles: a reader must only ever see
 * success or an errno meaning "it went away", never garbage or a hang.
 */
TEST_F(kernfs_cgroup, lookup_vs_create_remove_stress)
{
	pid_t pids[STRESS_DIRS + STRESS_READERS];
	struct timespec end;
	int i, status, n = 0;

	clock_gettime(CLOCK_MONOTONIC, &end);
	end.tv_sec += STRESS_SECS;

	for (i = 0; i < STRESS_DIRS; i++) {
		pid_t pid = fork();

		ASSERT_GE(pid, 0);
		if (pid == 0) {
			char dir[PATH_MAX];

			snprintf(dir, sizeof(dir), "%s/s%d", self->scratch, i);
			while (!stress_deadline(&end)) {
				if (mkdir(dir, 0755) && errno != EEXIST)
					_exit(10);
				if (rmdir(dir) && errno != ENOENT &&
				    errno != EBUSY)
					_exit(11);
			}
			_exit(0);
		}
		pids[n++] = pid;
	}

	for (i = 0; i < STRESS_READERS; i++) {
		pid_t pid = fork();

		ASSERT_GE(pid, 0);
		if (pid == 0) {
			/* Start each reader on a different directory. */
			unsigned int seq = i;

			while (!stress_deadline(&end)) {
				int which = seq++ % STRESS_DIRS;
				char path[PATH_MAX];
				struct stat st;
				int fd;

				snprintf(path, sizeof(path),
					 "%s/s%d/cgroup.procs",
					 self->scratch, which);

				if (stat(path, &st) && errno != ENOENT &&
				    errno != ENODEV)
					_exit(20);

				fd = open(path, O_RDONLY | O_CLOEXEC);
				if (fd < 0) {
					if (errno != ENOENT && errno != ENODEV)
						_exit(21);
				} else {
					close(fd);
				}

				if (access(path, F_OK) && errno != ENOENT &&
				    errno != ENODEV)
					_exit(22);
			}
			_exit(0);
		}
		pids[n++] = pid;
	}

	for (i = 0; i < n; i++) {
		ASSERT_EQ(waitpid(pids[i], &status, 0), pids[i]);
		ASSERT_TRUE(WIFEXITED(status));
		EXPECT_EQ(WEXITSTATUS(status), 0);
	}

	for (i = 0; i < STRESS_DIRS; i++) {
		char dir[PATH_MAX];

		snprintf(dir, sizeof(dir), "%s/s%d", self->scratch, i);
		rmdir_retry(dir);
	}
}

struct kernfs_handle {
	struct file_handle h;
	unsigned char buf[MAX_HANDLE_SZ];
};

static int kernfs_encode(const char *path, struct kernfs_handle *fh)
{
	int mount_id;

	memset(fh, 0, sizeof(*fh));
	fh->h.handle_bytes = sizeof(fh->buf);
	return name_to_handle_at(AT_FDCWD, path, &fh->h, &mount_id, 0);
}

/*
 * Skip only where file handles do not work at all.  ENOENT must still
 * fail: mkdir leaves a negative dentry cached, so the name resolves only
 * after ->d_revalidate() drops it.  The encode tests revalidation too.
 */
static bool fh_unsupported(int err)
{
	return err == EOPNOTSUPP || err == EPERM || err == ENOSYS;
}

/*
 * Decoding a file needs CAP_DAC_READ_SEARCH in the initial user
 * namespace.  Probe once so the tests skip instead of fail.
 */
static bool fh_can_decode(int mfd, struct kernfs_handle *fh)
{
	int fd = open_by_handle_at(mfd, &fh->h, O_PATH);

	if (fd < 0)
		return errno != EPERM;
	close(fd);
	return true;
}

/*
 * A file handle reaches a node without a lookup through its parent.  A
 * live node must decode.  A removed one must not, because
 * kernfs_find_and_get_node_by_id() refuses inactive nodes.
 *
 * Use O_PATH: opening a removed node fails with ENODEV, which would hide
 * what is being tested.
 */
TEST_F(kernfs_cgroup, exportfs_decode_and_stale)
{
	char victim[PATH_MAX], procs[PATH_MAX];
	struct kernfs_handle fh;
	struct stat st;
	int mfd, fd;

	snprintf(victim, sizeof(victim), "%s/fh", self->scratch);
	snprintf(procs, sizeof(procs), "%s/cgroup.procs", victim);
	ASSERT_EQ(mkdir(victim, 0755), 0);

	/* Any fd on the filesystem identifies it to open_by_handle_at(). */
	mfd = open(self->scratch, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(mfd, 0);

	if (kernfs_encode(procs, &fh)) {
		int err = errno;

		close(mfd);
		rmdir(victim);
		ASSERT_TRUE(fh_unsupported(err))
			TH_LOG("name_to_handle_at: %s", strerror(err));
		SKIP(return, "name_to_handle_at: %s", strerror(err));
	}

	if (!fh_can_decode(mfd, &fh)) {
		close(mfd);
		rmdir(victim);
		SKIP(return, "open_by_handle_at: no CAP_DAC_READ_SEARCH");
	}

	fd = open_by_handle_at(mfd, &fh.h, O_PATH);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(fstat(fd, &st), 0);
	EXPECT_EQ(st.st_nlink, 1);
	EXPECT_EQ(close(fd), 0);

	ASSERT_EQ(rmdir(victim), 0);

	fd = open_by_handle_at(mfd, &fh.h, O_PATH);
	EXPECT_LT(fd, 0);
	if (fd >= 0)
		close(fd);
	else
		EXPECT_EQ(errno, ESTALE);

	EXPECT_EQ(close(mfd), 0);
}

#define FH_STRESS_SECS	2
#define FH_DECODE_CAP	10000

/*
 * Decode file handles while the node is being removed.  A decode must
 * answer with a usable handle or ESTALE, never garbage and never a hang.
 *
 * The link count is checked too.  An inode that reaches the inode hash
 * after the removal cleared link counts keeps the 1 it was born with, so
 * it never gets an IN_DELETE_SELF.  This has not been seen to fire: it
 * needs the decode to stall between the lookup by id and the hash insert,
 * and nothing there blocks.  It is kept because it is cheap and only
 * looks once the directory is gone, so it cannot fail falsely.
 */
TEST_F(kernfs_cgroup, exportfs_decode_vs_rmdir_stress)
{
	int mfd, bad = 0, rounds = 0;
	struct timespec end;

	mfd = open(self->scratch, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(mfd, 0);

	clock_gettime(CLOCK_MONOTONIC, &end);
	end.tv_sec += FH_STRESS_SECS;

	while (!stress_deadline(&end)) {
		char victim[PATH_MAX], procs[PATH_MAX];
		int last = -1, fd, i;
		struct kernfs_handle fh;
		struct stat st;
		pid_t pid;

		snprintf(victim, sizeof(victim), "%s/fh%d", self->scratch,
			 rounds++);
		snprintf(procs, sizeof(procs), "%s/cgroup.procs", victim);
		if (mkdir(victim, 0755))
			break;
		if (kernfs_encode(procs, &fh)) {
			int err = errno;

			rmdir(victim);
			ASSERT_TRUE(fh_unsupported(err))
				TH_LOG("name_to_handle_at: %s", strerror(err));
			SKIP(goto out, "name_to_handle_at: %s", strerror(err));
		}
		if (rounds == 1 && !fh_can_decode(mfd, &fh)) {
			rmdir(victim);
			SKIP(goto out,
			     "open_by_handle_at: no CAP_DAC_READ_SEARCH");
		}

		pid = fork();
		ASSERT_GE(pid, 0);
		if (pid == 0) {
			rmdir_retry(victim);
			_exit(0);
		}

		/*
		 * Decode until the removal deactivates the node.  Keep the
		 * last one that worked: it ran closest to the removal.
		 */
		for (i = 0; i < FH_DECODE_CAP; i++) {
			fd = open_by_handle_at(mfd, &fh.h, O_PATH);
			if (fd < 0)
				break;
			if (last >= 0)
				close(last);
			last = fd;
		}
		ASSERT_EQ(waitpid(pid, NULL, 0), pid);

		if (last >= 0) {
			if (access(victim, F_OK) && errno == ENOENT &&
			    !fstat(last, &st) && st.st_nlink != 0)
				bad++;
			close(last);
		}
		rmdir(victim);
	}

	EXPECT_EQ(bad, 0)
		TH_LOG("%d of %d rounds decoded a removed node whose inode kept its link count",
		       bad, rounds);
out:
	close(mfd);
}

/*
 * sysfs is namespace tagged (KERNFS_NS) and supports rename; cgroup2 does
 * neither.  Run in a private netns with its own sysfs so the host is
 * untouched.
 */
FIXTURE(kernfs_netns)
{
	char mnt[PATH_MAX];
	char net[PATH_MAX];
	bool mounted;
};

FIXTURE_SETUP(kernfs_netns)
{
	if (geteuid())
		SKIP(return, "test needs to run as root");

	if (unshare(CLONE_NEWNS | CLONE_NEWNET))
		SKIP(return, "unshare(CLONE_NEWNS|CLONE_NEWNET): %s",
		     strerror(errno));

	/* Don't let our sysfs mount escape into the parent namespace. */
	ASSERT_EQ(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	strcpy(self->mnt, "/tmp/kernfs_selftest_sysfs.XXXXXX");
	if (!mkdtemp(self->mnt))
		SKIP(return, "mkdtemp: %s", strerror(errno));

	if (mount("none", self->mnt, "sysfs", 0, NULL)) {
		rmdir(self->mnt);
		SKIP(return, "mount sysfs: %s", strerror(errno));
	}
	self->mounted = true;

	snprintf(self->net, sizeof(self->net), "%s/class/net", self->mnt);
}

FIXTURE_TEARDOWN(kernfs_netns)
{
	if (self->mounted)
		umount2(self->mnt, MNT_DETACH);
	rmdir(self->mnt);
}

/*
 * sysfs must show this network namespace's interfaces, not the parent's.
 *
 * Do not assume a fresh netns contains only "lo": fallback tunnel devices
 * (tunl0, sit0, gre0, ...) are created in every namespace unless
 * net.core.fb_tunnels_only_for_init_net is set, so which names appear
 * depends on the modules the host has.  Check the set instead --
 * if_nametoindex() resolves in the current netns, so every name sysfs shows
 * must resolve there, and the counts must agree.
 *
 * Count only symlinks.  Not every entry is a device: bonding adds a
 * bonding_masters attribute to /sys/class/net in every namespace.
 */
TEST_F(kernfs_netns, ns_tag_isolates_class_net)
{
	struct if_nameindex *idx, *i;
	bool found_lo = false;
	int n = 0, want = 0;
	struct dirent *de;
	DIR *d;

	d = opendir(self->net);
	ASSERT_NE(d, NULL);
	while ((de = readdir(d))) {
		if (de->d_type != DT_LNK)
			continue;
		EXPECT_NE(if_nametoindex(de->d_name), 0u)
			TH_LOG("%s is not in this netns", de->d_name);
		if (!strcmp(de->d_name, "lo"))
			found_lo = true;
		n++;
	}
	closedir(d);

	idx = if_nameindex();
	ASSERT_NE(idx, NULL);
	for (i = idx; i->if_index; i++)
		want++;
	if_freenameindex(idx);

	EXPECT_TRUE(found_lo);
	EXPECT_EQ(n, want);
}

/*
 * After a rename the old name must stop resolving and the new one must
 * start, even though both dentries are already cached.
 */
TEST_F(kernfs_netns, rename_is_revalidated)
{
	char old_path[PATH_MAX], new_path[PATH_MAX];
	struct ifreq ifr = {};
	struct stat st;
	int sk;

	snprintf(old_path, sizeof(old_path), "%s/lo", self->net);
	snprintf(new_path, sizeof(new_path), "%s/%s", self->net, TEST_IFNAME);

	/* Warm both dentries: one positive, one negative. */
	ASSERT_EQ(stat(old_path, &st), 0);
	ASSERT_EQ(stat(new_path, &st), -1);
	ASSERT_EQ(errno, ENOENT);

	sk = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	ASSERT_GE(sk, 0);
	strcpy(ifr.ifr_name, "lo");
	strcpy(ifr.ifr_newname, TEST_IFNAME);
	if (ioctl(sk, SIOCSIFNAME, &ifr)) {
		close(sk);
		SKIP(return, "SIOCSIFNAME: %s", strerror(errno));
	}
	close(sk);

	EXPECT_EQ(stat(old_path, &st), -1);
	EXPECT_EQ(errno, ENOENT);
	EXPECT_EQ(stat(new_path, &st), 0);
}

static int netdev_rename(const char *from, const char *to)
{
	struct ifreq ifr = {};
	int sk, ret;

	sk = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sk < 0)
		return -1;
	strncpy(ifr.ifr_name, from, IFNAMSIZ - 1);
	strncpy(ifr.ifr_newname, to, IFNAMSIZ - 1);
	ret = ioctl(sk, SIOCSIFNAME, &ifr);
	close(sk);
	return ret;
}

/*
 * Bounded by a count, not by time: every rename is logged and not rate
 * limited, so a timed loop would flood the kernel log.
 */
#define RENAME_FLIPS		200
#define RENAME_READERS		4

/*
 * Rename an interface while other tasks look up the names it moves
 * between.  This renames its /sys/class/net entry through
 * kernfs_rename_ns() with the parent unchanged.
 *
 * The renamer checks what is certain: SIOCSIFNAME returns once the rename
 * is done and nothing else renames here, so the new name must resolve and
 * the old must not.  The readers cannot check that, because the name can
 * move between their two lstat() calls.  They only check that a lookup
 * returns success or ENOENT, and keep the lock busy while renames run.
 *
 * lstat() not stat(): /sys/class/net/<dev> is a symlink and is renamed
 * before the directory it points at, so the two are not atomic.
 */
TEST_F(kernfs_netns, rename_vs_lookup_stress)
{
	char old_path[PATH_MAX], new_path[PATH_MAX];
	pid_t pids[RENAME_READERS];
	int i, status, n = 0, bad = 0;
	struct stat st;
	int done[2];

	snprintf(old_path, sizeof(old_path), "%s/lo", self->net);
	snprintf(new_path, sizeof(new_path), "%s/%s", self->net, TEST_IFNAME);

	if (netdev_rename("lo", TEST_IFNAME))
		SKIP(return, "SIOCSIFNAME: %s", strerror(errno));
	if (netdev_rename(TEST_IFNAME, "lo"))
		SKIP(return, "SIOCSIFNAME back: %s", strerror(errno));

	/* Readers run until the renamer closes the write end. */
	ASSERT_EQ(pipe2(done, O_NONBLOCK | O_CLOEXEC), 0);

	for (i = 0; i < RENAME_READERS; i++) {
		pid_t pid = fork();

		ASSERT_GE(pid, 0);
		if (pid == 0) {
			struct stat rst;
			char c;

			close(done[1]);
			while (read(done[0], &c, 1) < 0 && errno == EAGAIN) {
				if (lstat(old_path, &rst) && errno != ENOENT)
					_exit(20);
				if (lstat(new_path, &rst) && errno != ENOENT)
					_exit(21);
			}
			_exit(0);
		}
		pids[n++] = pid;
	}
	close(done[0]);

	for (i = 0; i < RENAME_FLIPS; i++) {
		if (netdev_rename("lo", TEST_IFNAME))
			break;
		if (lstat(new_path, &st) || !lstat(old_path, &st)) {
			bad++;
			break;
		}
		if (netdev_rename(TEST_IFNAME, "lo"))
			break;
		if (lstat(old_path, &st) || !lstat(new_path, &st)) {
			bad++;
			break;
		}
	}
	close(done[1]);

	for (i = 0; i < n; i++) {
		ASSERT_EQ(waitpid(pids[i], &status, 0), pids[i]);
		ASSERT_TRUE(WIFEXITED(status));
		EXPECT_EQ(WEXITSTATUS(status), 0);
	}

	EXPECT_EQ(bad, 0)
		TH_LOG("a completed rename left the wrong name resolving");

	/* Leave the interface as the fixture found it. */
	netdev_rename(TEST_IFNAME, "lo");
}

TEST_HARNESS_MAIN
