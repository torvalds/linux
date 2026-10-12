// SPDX-License-Identifier: GPL-2.0
/*
 * may_decode_fh() refuses a file handle below a mount with locked children
 * to root in a user namespace. That has to hold while the mount is lazily
 * unmounted and its children, the locked ones too, are taken off it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
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

#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE		1
#endif
#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC	O_CLOEXEC
#endif
#ifndef AT_RECURSIVE
#define AT_RECURSIVE		0x8000
#endif

#define DIR_LEN		64
#define PATH_LEN	128

#define ROUNDS		100	/* lazy umounts raced per test */
#define RACERS		3	/* threads in open_by_handle_at() */
#define EXTRA_MOUNTS	64	/* make umount_tree() hold mount_lock longer */
#define MAX_DELAY_US	3000	/* before the umount */
#define TAIL_US		2000	/* after it */

struct handle {
	struct file_handle fh;
	unsigned char buf[MAX_HANDLE_SZ];
};

/* exit codes of the child */
enum {
	CHILD_OK,
	CHILD_SETUP,
	CHILD_DECODED,		/* decoded past a locked child */
	CHILD_MOUNTED,		/* not refused while mounted */
	CHILD_UNMOUNTED,	/* not refused once unmounted */
	CHILD_ALLOWED,		/* refused where nothing is locked */
	CHILD_NOUSERNS,		/* no user namespace to be had */
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

/* Become root in a new user namespace with a private mount namespace. */
static int enter_userns(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();
	char map[32];

	prctl(PR_SET_DUMPABLE, 1);
	/* EINVAL: no USER_NS, ENOSPC: user.max_user_namespaces is 0, EPERM: an LSM */
	if (unshare(CLONE_NEWUSER | CLONE_NEWNS))
		return errno == EINVAL || errno == ENOSPC || errno == EPERM ?
		       CHILD_NOUSERNS : CHILD_SETUP;
	if (write_file("/proc/self/setgroups", "deny") && errno != ENOENT)
		return CHILD_SETUP;
	snprintf(map, sizeof(map), "0 %d 1", uid);
	if (write_file("/proc/self/uid_map", map))
		return CHILD_SETUP;
	snprintf(map, sizeof(map), "0 %d 1", gid);
	if (write_file("/proc/self/gid_map", map))
		return CHILD_SETUP;
	if (setgid(0) || setuid(0))
		return CHILD_SETUP;
	if (mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL))
		return CHILD_SETUP;
	return CHILD_OK;
}

static int get_handle(const char *path, struct handle *h)
{
	int mntid;

	h->fh.handle_bytes = MAX_HANDLE_SZ;
	return name_to_handle_at(AT_FDCWD, path, &h->fh, &mntid, 0);
}

static int decode(int dfd, struct handle *h)
{
	return open_by_handle_at(dfd, &h->fh, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

struct race {
	int dfd;
	struct handle *h;
	atomic_int go;
	atomic_int stop;
	atomic_int won;		/* the first decoded descriptor */
};

static void *racer(void *arg)
{
	struct race *r = arg;

	while (!atomic_load(&r->go))
		;
	while (!atomic_load(&r->stop)) {
		int none = -1;
		int fd;

		fd = decode(r->dfd, r->h);
		if (fd < 0)
			continue;
		if (!atomic_compare_exchange_strong(&r->won, &none, fd))
			close(fd);
	}
	return NULL;
}

/* stop the @n racers started so far and wait for them, they spin on @r */
static void stop_racers(struct race *r, pthread_t *th, int n)
{
	int i;

	atomic_store(&r->stop, 1);
	atomic_store(&r->go, 1);	/* one still waiting for the start sees stop next */
	for (i = 0; i < n; i++)
		pthread_join(th[i], NULL);
}

/*
 * Bind @h recursively at @w, or take a detached copy of it, and race
 * open_by_handle_at() of @inner against the lazy umount. The decoded
 * descriptor, if there was one, is left in @won.
 */
static int race_round(const char *h, const char *w, struct handle *inner,
		      bool dissolve, int *won)
{
	struct race r = { .h = inner, .won = -1 };
	pthread_t th[RACERS];
	int treefd = -1, fd, i;

	if (dissolve) {
		treefd = syscall(__NR_open_tree, AT_FDCWD, h,
				 OPEN_TREE_CLONE | AT_RECURSIVE | OPEN_TREE_CLOEXEC);
		if (treefd < 0)
			return CHILD_SETUP;
		/* an ordinary descriptor on the copy, treefd's close dissolves it */
		r.dfd = openat(treefd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	} else {
		if (mount(h, w, NULL, MS_BIND | MS_REC, NULL))
			return CHILD_SETUP;
		r.dfd = open(w, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	}
	if (r.dfd < 0)
		return CHILD_SETUP;

	fd = decode(r.dfd, inner);
	if (fd >= 0 || errno != EPERM) {
		if (fd >= 0)
			close(fd);
		return CHILD_MOUNTED;
	}

	for (i = 0; i < RACERS; i++) {
		if (pthread_create(&th[i], NULL, racer, &r)) {
			stop_racers(&r, th, i);
			return CHILD_SETUP;
		}
	}
	atomic_store(&r.go, 1);
	usleep(rand() % MAX_DELAY_US);
	if (dissolve)
		close(treefd);
	else if (umount2(w, MNT_DETACH)) {
		stop_racers(&r, th, RACERS);
		return CHILD_SETUP;
	}
	usleep(TAIL_US);
	stop_racers(&r, th, RACERS);

	fd = decode(r.dfd, inner);
	if (fd >= 0) {
		close(fd);
		return CHILD_UNMOUNTED;
	}
	close(r.dfd);
	*won = atomic_load(&r.won);
	return CHILD_OK;
}

static int race_child(const char *h, const char *w, struct handle *inner,
		      bool dissolve)
{
	int ret, won, i;

	srand(getpid());
	ret = enter_userns();
	if (ret)
		return ret;
	for (i = 0; i < ROUNDS; i++) {
		ret = race_round(h, w, inner, dissolve, &won);
		if (ret)
			return ret;
		if (won >= 0)
			return CHILD_DECODED;
	}
	return CHILD_OK;
}

/* a bind without locked children decodes while mounted and not after */
static int allowed_child(const char *plain, const char *w, struct handle *h)
{
	int dfd, fd, ret;

	ret = enter_userns();
	if (ret)
		return ret;
	if (mount(plain, w, NULL, MS_BIND | MS_REC, NULL))
		return CHILD_SETUP;
	dfd = open(w, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd < 0)
		return CHILD_SETUP;
	fd = decode(dfd, h);
	if (fd < 0)
		return CHILD_ALLOWED;
	close(fd);
	if (umount2(w, MNT_DETACH))
		return CHILD_SETUP;
	fd = decode(dfd, h);
	if (fd >= 0) {
		close(fd);
		return CHILD_UNMOUNTED;
	}
	return errno == EPERM ? CHILD_OK : CHILD_UNMOUNTED;
}

FIXTURE(locked_handle) {
	char base[DIR_LEN];
	char h[PATH_LEN];		/* the tree with the covered directory */
	char plain[PATH_LEN];		/* a tree with no mount in it */
	char w[PATH_LEN];		/* where the child binds either */
	struct handle inner;		/* h/top/secret/inner, covered */
	struct handle uncovered;	/* plain/inner */
};

FIXTURE_SETUP(locked_handle)
{
	char p[PATH_LEN], e[PATH_LEN];
	int i;

	if (geteuid())
		SKIP(return, "test requires root");

	snprintf(self->base, sizeof(self->base), "/tmp/locked_handle.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, NULL), 0);

	snprintf(self->h, sizeof(self->h), "%s/h", self->base);
	snprintf(self->plain, sizeof(self->plain), "%s/plain", self->base);
	snprintf(self->w, sizeof(self->w), "%s/w", self->base);
	ASSERT_EQ(mkdir(self->h, 0755), 0);
	ASSERT_EQ(mkdir(self->plain, 0755), 0);
	ASSERT_EQ(mkdir(self->w, 0755), 0);
	snprintf(p, sizeof(p), "%s/plain/inner", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	ASSERT_EQ(get_handle(p, &self->uncovered), 0);

	/* the handle is for this filesystem */
	ASSERT_EQ(mount("tmpfs", self->h, "tmpfs", 0, NULL), 0);
	snprintf(p, sizeof(p), "%s/h/top", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	snprintf(p, sizeof(p), "%s/h/top/secret", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	snprintf(p, sizeof(p), "%s/h/top/secret/inner", self->base);
	ASSERT_EQ(mkdir(p, 0755), 0);
	ASSERT_EQ(get_handle(p, &self->inner), 0);
	snprintf(e, sizeof(e), "%s/h/empty", self->base);
	ASSERT_EQ(mkdir(e, 0755), 0);

	/* cover it, and some more so the umount takes longer */
	snprintf(p, sizeof(p), "%s/h/top/secret", self->base);
	ASSERT_EQ(mount("tmpfs", p, "tmpfs", 0, NULL), 0);
	for (i = 0; i < EXTRA_MOUNTS; i++) {
		snprintf(p, sizeof(p), "%s/h/c%d", self->base, i);
		ASSERT_EQ(mkdir(p, 0755), 0);
		ASSERT_EQ(mount(e, p, NULL, MS_BIND, NULL), 0);
	}
}

FIXTURE_TEARDOWN(locked_handle)
{
	umount2(self->base, MNT_DETACH);
	rmdir(self->base);
}

static int wait_child(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

TEST_F(locked_handle, lazy_umount)
{
	pid_t pid;
	int ret;

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(race_child(self->h, self->w, &self->inner, false));
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	if (ret == CHILD_NOUSERNS)
		SKIP(return, "no user namespaces");
	EXPECT_EQ(ret, CHILD_OK);
}

TEST_F(locked_handle, dissolve)
{
	pid_t pid;
	int ret;

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(race_child(self->h, self->w, &self->inner, true));
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	if (ret == CHILD_NOUSERNS)
		SKIP(return, "no user namespaces");
	EXPECT_EQ(ret, CHILD_OK);
}

TEST_F(locked_handle, allowed_use)
{
	pid_t pid;
	int ret;

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0)
		_exit(allowed_child(self->plain, self->w, &self->uncovered));
	ret = wait_child(pid);
	TH_LOG("child exit code %d", ret);
	if (ret == CHILD_NOUSERNS)
		SKIP(return, "no user namespaces");
	EXPECT_EQ(ret, CHILD_OK);
}

TEST_HARNESS_MAIN
